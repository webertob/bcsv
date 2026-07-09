"""Parquet <-> BCSV conversion utilities.

Core conversion logic and CLI entry points for parquet2bcsv and bcsv2parquet.
"""

import argparse
import json
import os
import sys
import time
from typing import List, Optional, Set, Tuple, Union

import pyarrow as pa
import pyarrow.compute as pc
import pyarrow.parquet as pq

import pybcsv

# ---- Constants ----
#
# Type-mapping notes
# ------------------
# Parquet types without a 1:1 BCSV equivalent are widened or collapsed:
#   • float16 / bfloat16  ->  BCSV FLOAT  (widened to IEEE 754 binary32)
#   • string / large_string  ->  BCSV STRING  (BCSV has no length distinction)


_FLAT_ARROW_TYPES = {
    pa.bool_(),
    pa.int8(),
    pa.int16(),
    pa.int32(),
    pa.int64(),
    pa.uint8(),
    pa.uint16(),
    pa.uint32(),
    pa.uint64(),
    pa.float32(),
    pa.float64(),
    pa.string(),
    pa.large_string(),
}

_ARROW_TO_BCSV = {
    pa.bool_(): pybcsv.ColumnType.BOOL,
    pa.int8(): pybcsv.ColumnType.INT8,
    pa.int16(): pybcsv.ColumnType.INT16,
    pa.int32(): pybcsv.ColumnType.INT32,
    pa.int64(): pybcsv.ColumnType.INT64,
    pa.uint8(): pybcsv.ColumnType.UINT8,
    pa.uint16(): pybcsv.ColumnType.UINT16,
    pa.uint32(): pybcsv.ColumnType.UINT32,
    pa.uint64(): pybcsv.ColumnType.UINT64,
    pa.float32(): pybcsv.ColumnType.FLOAT,
    pa.float64(): pybcsv.ColumnType.DOUBLE,
    pa.string(): pybcsv.ColumnType.STRING,
    pa.large_string(): pybcsv.ColumnType.STRING,
}

_FP16_TYPES = {pa.float16()}
try:
    _FP16_TYPES.add(pa.bfloat16())
except AttributeError:
    # bfloat16 not available in this PyArrow build
    pass

# Maps BCSV types to Arrow for the unflatten path (BCSV -> Parquet).
# BCSV STRING maps to Arrow string (not large_string) — the common case.
_BCSV_TO_ARROW = {
    pybcsv.ColumnType.BOOL: pa.bool_(),
    pybcsv.ColumnType.INT8: pa.int8(),
    pybcsv.ColumnType.INT16: pa.int16(),
    pybcsv.ColumnType.INT32: pa.int32(),
    pybcsv.ColumnType.INT64: pa.int64(),
    pybcsv.ColumnType.UINT8: pa.uint8(),
    pybcsv.ColumnType.UINT16: pa.uint16(),
    pybcsv.ColumnType.UINT32: pa.uint32(),
    pybcsv.ColumnType.UINT64: pa.uint64(),
    pybcsv.ColumnType.FLOAT: pa.float32(),
    pybcsv.ColumnType.DOUBLE: pa.float64(),
    pybcsv.ColumnType.STRING: pa.string(),
}

_MAX_FLATTEN_DEPTH = 64
_MAX_FIXED_LIST_SIZE = 64
_MAX_ESCAPES = 64

# Escape protocol: trailing underscores on parent identifier resolve name collisions.
# Consequence: column names ending with '_' are ambiguous — we cannot distinguish a
# legitimate trailing underscore from escape suffixes during unflattening.
# Enforcement: reject column names that end with '_' at every entry point.


def _check_underscore_name(name: str) -> None:
    """Raise if name ends with '_' (incompatible with escape protocol)."""
    if name.endswith("_"):
        raise ValueError(
            f"Column name '{name}' ends with '_'. Column names ending with "
            "underscore are not supported because the unflatten algorithm uses "
            "trailing underscores to resolve name collisions in nested schemas."
        )


# ---- Schema flattening (Parquet -> BCSV) ----


def _collect_reserved_names(schema: pa.Schema) -> Set[str]:
    """Collect top-level flat column names that reserve dot-notation paths."""
    reserved: Set[str] = set()
    for field in schema:
        if field.type in _FLAT_ARROW_TYPES or field.type in _FP16_TYPES:
            reserved.add(field.name)
    return reserved


def _resolve_collision(
    name: str,
    dtype: pa.DataType,
    path: str,
    suffix: str,
    reserved: Set[str],
) -> Tuple[str, pa.DataType]:
    """Escape parent identifier with underscores until no collision."""
    for esc in range(_MAX_ESCAPES):
        if path:
            candidate = (
                f"{path}{'_' * (esc + 1)}.{suffix}"
                if suffix
                else f"{path}{'_' * (esc + 1)}"
            )
        else:
            candidate = (
                f"{'_' * (esc + 1)}.{suffix}" if suffix else f"{'_' * (esc + 1)}"
            )
        if candidate not in reserved:
            reserved.add(candidate)
            return (candidate, dtype)
    raise ValueError(
        f"Cannot resolve name collision for '{name}' "
        f"(exceeded {_MAX_ESCAPES} escape attempts)."
    )


def _flatten_node(
    field: pa.Field,
    path: str,
    reserved: Set[str],
    depth: int = 0,
) -> List[Tuple[str, pa.DataType]]:
    """Recursively flatten a schema node into (flat_name, arrow_type) pairs."""
    if depth > _MAX_FLATTEN_DEPTH:
        raise ValueError(
            f"Nesting depth exceeds {_MAX_FLATTEN_DEPTH} for field '{field.name}'."
        )

    _check_underscore_name(field.name)
    name = f"{path}.{field.name}" if path else field.name

    if field.type in _FLAT_ARROW_TYPES or field.type in _FP16_TYPES:
        actual_type = pa.float32() if field.type in _FP16_TYPES else field.type
        if name in reserved:
            c, d = _resolve_collision(name, actual_type, path, field.name, reserved)
            return [(c, d)]
        reserved.add(name)
        return [(name, actual_type)]

    if pa.types.is_struct(field.type):
        result: List[Tuple[str, pa.DataType]] = []
        for i in range(field.type.num_fields):
            child = field.type.field(i)
            result.extend(_flatten_node(child, name, reserved, depth + 1))
        return result

    if pa.types.is_fixed_size_list(field.type):
        list_size = field.type.list_size
        value_type = field.type.value_type

        if list_size > _MAX_FIXED_LIST_SIZE:
            raise ValueError(
                f"FixedSizeList<{list_size}> in column '{name}' exceeds "
                f"max {_MAX_FIXED_LIST_SIZE} elements."
            )

        if pa.types.is_struct(value_type):
            result = []
            for idx in range(list_size):
                bracket = f"{name}[{idx}]"
                for j in range(value_type.num_fields):
                    child = value_type.field(j)
                    result.extend(_flatten_node(child, bracket, reserved, depth + 1))
            return result

        if value_type not in _FLAT_ARROW_TYPES and value_type not in _FP16_TYPES:
            raise _unsupported_type_error(name, field.type)

        widened_type = pa.float32() if value_type in _FP16_TYPES else value_type
        items = [(f"{name}[{i}]", widened_type) for i in range(list_size)]
        for n, _ in items:
            reserved.add(n)
        return items

    if pa.types.is_list(field.type) or pa.types.is_large_list(field.type):
        raise ValueError(
            f"Column '{name}' is a variable-length list. "
            "Only fixed-length lists supported."
        )

    if pa.types.is_map(field.type):
        raise ValueError(f"Column '{name}' is a Map type. Maps are not supported.")

    raise _unsupported_type_error(name, field.type)


def flatten_parquet_schema(schema: pa.Schema) -> List[Tuple[str, pa.DataType]]:
    """Flatten Parquet schema into (flat_name, arrow_type) pairs."""
    reserved = _collect_reserved_names(schema)
    result: List[Tuple[str, pa.DataType]] = []
    for field in schema:
        if field.type in _FLAT_ARROW_TYPES or field.type in _FP16_TYPES:
            _check_underscore_name(field.name)
            actual_type = pa.float32() if field.type in _FP16_TYPES else field.type
            result.append((field.name, actual_type))
        else:
            _check_underscore_name(field.name)
            result.extend(_flatten_node(field, "", reserved))
    return result


# ---- Flatten batch data ----


def flatten_batch(
    batch: pa.RecordBatch,
    flat_schema: List[Tuple[str, pa.DataType]],
    cached_schema: Optional[pa.Schema] = None,
) -> pa.RecordBatch:
    """Extract and reorder columns from a batch to match flat_schema.

    Passing a pre-built `cached_schema` avoids rebuilding a pa.Schema on every call.
    """
    output_arrays: List[pa.Array] = []
    output_schema = (
        cached_schema
        if cached_schema is not None
        else pa.schema([(name, dtype) for name, dtype in flat_schema])
    )

    for name, _dtype in flat_schema:
        arr = _extract_flat_array(batch, name)
        output_arrays.append(arr)

    return pa.RecordBatch.from_arrays(output_arrays, schema=output_schema)


def _extract_flat_array(batch: pa.RecordBatch, flat_name: str) -> pa.Array:
    """Navigate into nested columns to extract a flat array.

    For already-flattened Parquet files whose column names contain literal
    dots, tries an exact match first.  If found, returns the column
    directly without decomposition.
    """
    # Fast path: flat_name matches a literal column name (common for
    # pre-flattened Parquet files with dot-separated identifiers).
    for i in range(len(batch.schema)):
        if batch.schema.field(i).name == flat_name:
            return batch.column(i)

    # Undo collision-escape underscores before navigating: a struct path whose
    # flattened name collided with a literal dotted column was escaped (e.g.
    # 'a.b' -> 'a_.b') during flattening.  The batch's actual struct is named
    # 'a', so strip the escape suffixes to navigate it.  Real identifiers can
    # never end with '_' (rejected at flatten time), so this is unambiguous.
    parts = _strip_escape_suffixes(_decompose_name(flat_name))
    root_name: str = parts[0]  # type: ignore[assignment]

    col_idx = -1
    for i in range(len(batch.schema)):
        if batch.schema.field(i).name == root_name:
            col_idx = i
            break
    if col_idx < 0:
        raise ValueError(f"Column '{root_name}' not found in batch schema")

    arr = batch.column(col_idx)

    for part in parts[1:]:
        if isinstance(part, int):
            if pa.types.is_fixed_size_list(arr.type):
                list_size = arr.type.list_size
                if part < 0 or part >= list_size:
                    raise ValueError(
                        f"List index {part} out of range [0, {list_size}) "
                        f"for column '{flat_name}'."
                    )
                # FixedSizeList values are row-major: element `i` of row `r` lives
                # at child[r*list_size + i].  Extracting element `part` across all
                # rows is therefore a *strided* gather, not a contiguous slice.
                # flatten() yields the logical, offset-adjusted row-major child.
                num_rows = len(arr)
                flat_values = arr.flatten()
                gather = pa.array(
                    [r * list_size + part for r in range(num_rows)],
                    type=pa.int64(),
                )
                arr = flat_values.take(gather)
            else:
                arr = arr[part]
        else:
            if pa.types.is_struct(arr.type):
                for j in range(arr.type.num_fields):
                    if arr.type.field(j).name == part:
                        arr = arr.field(j)
                        break
                else:
                    raise ValueError(
                        f"Field '{part}' not found in struct at '{flat_name}'"
                    )
            else:
                raise ValueError(
                    f"Cannot descend into '{part}' in '{flat_name}': "
                    f"not a struct, got {arr.type}"
                )

    return arr


# ---- Name decomposition ----


def _decompose_name(name: str) -> List[Union[str, int]]:
    """Split a flat column name into hierarchical path components.

    Examples:
        'location.lat'    -> ['location', 'lat']
        'vals[0]'         -> ['vals', 0]
        'loc.readings[2]' -> ['loc', 'readings', 2]
        'a_.b'            -> ['a_', 'b']
        'id'              -> ['id']
    """
    parts: List[Union[str, int]] = []
    current = ""
    i = 0

    while i < len(name):
        ch = name[i]
        if ch == "[":
            if current:
                parts.append(current)
                current = ""
            close = name.index("]", i)
            idx = int(name[i + 1 : close])
            parts.append(idx)
            i = close + 1
            if i < len(name) and name[i] == ".":
                i += 1
        elif ch == ".":
            if current:
                parts.append(current)
                current = ""
            i += 1
        else:
            current += ch
            i += 1

    if current:
        parts.append(current)

    return parts


def _strip_escape_suffixes(parts: List[Union[str, int]]) -> List[Union[str, int]]:
    """Remove trailing underscores from each string part."""
    return [p.rstrip("_") if isinstance(p, str) else p for p in parts]


def _find_flat_column(col_map: dict, lookup_key: str) -> Optional[pa.Array]:
    """Look up a column in col_map by exact name."""
    return col_map.get(lookup_key)


# ---- Unflattening (BCSV -> Parquet) ----

# Trie node: maps to either a pa.DataType (leaf) or another trie (internal)
_TrieNode = dict


def _build_trie(names: List[str], types: List[pa.DataType]) -> _TrieNode:
    """Build a trie of column paths -> arrow types.

    Returns a dict where keys are component names (str for struct field,
    int for list index), and values are either pa.DataType (leaf) or
    nested dict (internal).
    """
    root: _TrieNode = {}

    for name, arrow_type in zip(names, types):
        parts = _decompose_name(name)
        stripped = _strip_escape_suffixes(parts)
        node = root
        for part in stripped[:-1]:
            if part not in node:
                node[part] = {}
            node = node[part]  # type: ignore[assignment]
        node[stripped[-1]] = arrow_type  # type: ignore[index]

    return root


def _trie_to_arrow_field(trie: _TrieNode) -> List[pa.Field]:
    """Convert a trie root to a list of top-level Arrow fields.

    The root trie contains multiple top-level entries. Each entry is either:
    - A flat field (value is pa.DataType)
    - A struct (value is a dict with str keys)
    - A fixed-size list (value is a dict with int keys)
    """
    fields: List[pa.Field] = []

    for key, value in trie.items():
        if isinstance(key, int):
            continue

        str_key = str(key)

        if value is None or not isinstance(value, dict):
            fields.append(pa.field(str_key, value))
            continue

        inner = value
        if not inner:
            fields.append(pa.field(str_key, pa.struct([])))
            continue

        int_keys = [k for k in inner if isinstance(k, int)]
        str_keys = [k for k in inner if isinstance(k, str)]

        if int_keys and not str_keys:
            max_idx = max(int_keys)
            base_type = inner.get(int_keys[0])
            if not isinstance(base_type, pa.DataType):
                base_type = pa.int64()
            fields.append(pa.field(str_key, pa.list_(base_type, max_idx + 1)))

        elif str_keys:
            struct_children: List[pa.Field] = []
            for ck, cv in inner.items():
                if not isinstance(ck, str):
                    continue
                if isinstance(cv, dict):
                    child_fields = _trie_to_arrow_field({ck: cv})
                    if child_fields:
                        struct_children.append(child_fields[0])
                elif cv is not None:
                    struct_children.append(pa.field(ck, cv))
            fields.append(pa.field(str_key, pa.struct(struct_children)))

        else:
            fields.append(pa.field(str_key, pa.struct([])))

    return fields


def unflatten_schema_to_arrow(
    names: List[str],
    types: List[pybcsv.ColumnType],
) -> pa.Schema:
    """Build Arrow schema from BCSV layout, reconstructing nesting."""
    arrow_types: List[pa.DataType] = []
    for t in types:
        if t in _BCSV_TO_ARROW:
            arrow_types.append(_BCSV_TO_ARROW[t])
        else:
            raise ValueError(
                f"Cannot map BCSV type '{t.name.lower()}' to Arrow. "
                f"Supported: {', '.join(sorted(k.name.lower() for k in _BCSV_TO_ARROW))}."
            )
    for n in names:
        _check_underscore_name(n)
    # Two distinct flat names that collapse to the same nested path (e.g. a
    # literal 'a.b' column and a struct path escaped to 'a_.b') cannot be
    # reconstructed unambiguously — the trie would silently merge them and drop
    # data.  Detect this and fail loudly; --no-unflatten preserves the columns.
    _seen_paths: dict = {}
    for n in names:
        key = tuple(_strip_escape_suffixes(_decompose_name(n)))
        if key in _seen_paths:
            path_str = ".".join(str(p) for p in key)
            raise ValueError(
                f"Cannot unflatten: columns '{_seen_paths[key]}' and '{n}' both "
                f"map to nested path '{path_str}'. Re-run with --no-unflatten to "
                "keep the flat columns."
            )
        _seen_paths[key] = n
    trie = _build_trie(names, arrow_types)
    fields = _trie_to_arrow_field(trie)
    return pa.schema(fields)


def unflatten_batch(
    batch: pa.RecordBatch,
    bcsv_layout: pybcsv.Layout,
    *,
    cached_schema: Optional[pa.Schema] = None,
) -> pa.RecordBatch:
    """Reconstruct nested Arrow structures from flat BCSV columns.

    When called in a tight loop, pass `cached_schema` (pre-computed via
    unflatten_schema_to_arrow) to avoid rebuilding the schema on every batch.
    """
    names = bcsv_layout.get_column_names()
    types = bcsv_layout.get_column_types()

    if cached_schema is not None:
        target_schema = cached_schema
    else:
        arrow_types: List[pa.DataType] = []
        for t in types:
            if t in _BCSV_TO_ARROW:
                arrow_types.append(_BCSV_TO_ARROW[t])
            else:
                raise ValueError(f"Cannot map BCSV type '{t.name.lower()}' to Arrow.")
        target_schema = unflatten_schema_to_arrow(names, types)

    col_map: dict = {}
    for i in range(len(batch.schema)):
        col_map[batch.schema.field(i).name] = batch.column(i)

    if len(target_schema) == len(batch.schema):
        target_names = [target_schema.field(i).name for i in range(len(target_schema))]
        batch_names = [batch.schema.field(i).name for i in range(len(batch.schema))]
        if target_names == batch_names:
            return batch

    output_arrays: List[pa.Array] = []
    for i in range(len(target_schema)):
        field = target_schema.field(i)
        arr = _build_nested_array(field, col_map, field.name)
        output_arrays.append(arr)

    return pa.RecordBatch.from_arrays(output_arrays, schema=target_schema)


def _build_nested_array(
    field: pa.Field,
    col_map: dict,
    lookup_key: str = "",
) -> pa.Array:
    """Build a nested array (struct or list) from flat columns."""
    if not pa.types.is_struct(field.type) and not pa.types.is_fixed_size_list(
        field.type
    ):
        arr = _find_flat_column(col_map, lookup_key)
        if arr is not None:
            return arr
        raise ValueError(f"Cannot find array for column '{lookup_key}'")

    if pa.types.is_fixed_size_list(field.type):
        list_size = field.type.list_size
        chunks: List[pa.Array] = []
        for idx in range(list_size):
            bracket = f"{lookup_key}[{idx}]"
            arr = _find_flat_column(col_map, bracket)
            if arr is not None:
                chunks.append(arr)
            else:
                raise ValueError(f"Cannot find array for '{bracket}'")
        # `chunks[i]` holds element `i` of every row (element-major).  The Arrow
        # FixedSizeList child must be row-major (child[r*list_size + i]), so
        # re-interleave: child[r*list_size + i] = chunks[i][r] = merged[i*num_rows + r].
        merged = pa.concat_arrays(chunks)
        num_rows = len(chunks[0])
        gather = pa.array(
            [
                (p % list_size) * num_rows + (p // list_size)
                for p in range(num_rows * list_size)
            ],
            type=pa.int64(),
        )
        child = merged.take(gather)
        return pa.FixedSizeListArray.from_arrays(child, list_size)

    struct_fields = field.type
    child_arrays: List[pa.Array] = []
    for i in range(struct_fields.num_fields):
        sf = struct_fields.field(i)
        child_key = f"{lookup_key}.{sf.name}"
        child_arr = _build_nested_array(sf, col_map, child_key)
        child_arrays.append(child_arr)

    return pa.StructArray.from_arrays(child_arrays, fields=struct_fields)


# ---- Validation ----


def _unsupported_type_error(name: str, arrow_type: pa.DataType) -> ValueError:
    type_name = str(arrow_type)
    if pa.types.is_timestamp(arrow_type):
        type_name = "timestamp"
    elif pa.types.is_date(arrow_type):
        type_name = "date"
    elif pa.types.is_time(arrow_type):
        type_name = "time"
    elif pa.types.is_duration(arrow_type):
        type_name = "duration"
    elif pa.types.is_decimal(arrow_type):
        type_name = "decimal"
    elif pa.types.is_interval(arrow_type):
        type_name = "interval"
    return ValueError(f"Unsupported Parquet type '{type_name}' for column '{name}'.")


def _flat_schema_to_bcsv_layout(
    flat_schema: List[Tuple[str, pa.DataType]],
) -> pybcsv.Layout:
    """Build a pybcsv.Layout from a flattened Parquet schema.

    Arrow types are mapped to BCSV types using _ARROW_TO_BCSV.  Types
    without a mapping are rejected with a clear error.
    """
    layout = pybcsv.Layout()
    for name, arrow_type in flat_schema:
        btype = _ARROW_TO_BCSV.get(arrow_type)
        if btype is None:
            raise ValueError(
                f"Cannot map Parquet type '{arrow_type}' to BCSV for column "
                f"'{name}'. Supported leaf types: bool, int8/16/32/64, "
                "uint8/16/32/64, float32/64, string, large_string."
            )
        layout.add_column(name, btype)
    return layout


def validate_parquet_schema(pf: pq.ParquetFile) -> None:
    """Reject Parquet files with schema evolution across row groups."""
    meta = pf.metadata
    if meta.num_row_groups <= 1:
        return

    rg0 = meta.row_group(0)
    base_cols = rg0.num_columns
    base_names = [rg0.column(i).path_in_schema for i in range(base_cols)]
    base_types = [rg0.column(i).physical_type for i in range(base_cols)]

    for gi in range(1, meta.num_row_groups):
        rg = meta.row_group(gi)
        if rg.num_columns != base_cols:
            raise ValueError(
                f"Schema evolution: row group {gi} has {rg.num_columns} "
                f"columns, expected {base_cols}. BCSV requires constant schema."
            )
        for ci in range(base_cols):
            actual = rg.column(ci).path_in_schema
            expected = base_names[ci]
            if actual != expected:
                raise ValueError(
                    f"Schema evolution: col {ci} in rg {gi} is "
                    f"'{actual}', expected '{expected}'. "
                    f"BCSV requires constant schema."
                )
            actual_type = rg.column(ci).physical_type
            if actual_type != base_types[ci]:
                raise ValueError(
                    f"Schema evolution: col '{expected}' in rg {gi} has type "
                    f"{actual_type}, expected {base_types[ci]}. "
                    f"BCSV requires constant schema."
                )


def _check_nulls(batch: pa.RecordBatch, row_offset: int) -> None:
    """Raise ValueError on the first null in any column of the batch.

    Uses pyarrow-native null location, which is offset-safe for sliced arrays
    (a hand-rolled bitmap scan mis-attributes the row number when the array has
    a non-zero offset, e.g. an element extracted from a FixedSizeList).
    """
    for i in range(len(batch.schema)):
        col = batch.column(i)
        if col.null_count == 0:
            continue

        field_name = batch.schema.field(i).name
        first_null = pc.index(pc.is_null(col), True).as_py()
        location = (
            f" at row {row_offset + first_null}"
            if first_null is not None and first_null >= 0
            else ""
        )
        raise ValueError(
            f"Null value detected in column '{field_name}'{location}. "
            "BCSV does not support nulls. Filter before conversion."
        )


# ---- Selection & helpers ----


def _parse_selection(
    layout: pybcsv.Layout,
    total_rows: int,
    columns: Optional[List[str]],
    row_slice: Optional[str],
) -> Tuple[List[str], List[pybcsv.ColumnType], int, Optional[int]]:
    """Parse column subset and row slice specification."""
    all_names = layout.get_column_names()
    all_types = layout.get_column_types()

    if columns:
        col_names = columns
        col_types: List[pybcsv.ColumnType] = []
        for cn in col_names:
            idx = layout.column_index(cn)
            col_types.append(all_types[idx])
    else:
        col_names = list(all_names)
        col_types = list(all_types)

    slice_start = 0
    slice_end: Optional[int] = None

    if row_slice:
        parts = row_slice.split(":")
        if len(parts) == 1:
            slice_start = int(parts[0])
        elif len(parts) == 2:
            slice_start = int(parts[0]) if parts[0] else 0
            slice_end = int(parts[1]) if parts[1] else total_rows
        else:
            raise ValueError(f"Invalid slice: '{row_slice}'. Use 'start:end'.")

        if not (0 <= slice_start <= total_rows):
            raise ValueError(
                f"Slice start {slice_start} out of range [0, {total_rows}]."
            )
        if slice_end is not None and not (slice_start <= slice_end <= total_rows):
            raise ValueError(
                f"Slice end {slice_end} out of range [{slice_start}, {total_rows}]."
            )

    return col_names, col_types, slice_start, slice_end


def _flat_arrow_schema(names: List[str], types: List[pybcsv.ColumnType]) -> pa.Schema:
    """Build a flat Arrow schema (no nesting) from column names + BCSV types."""
    fields: List[pa.Field] = []
    for n, t in zip(names, types):
        if t not in _BCSV_TO_ARROW:
            raise ValueError(
                f"Cannot map BCSV type '{t.name.lower()}' to Arrow. "
                f"Supported: {', '.join(sorted(k.name.lower() for k in _BCSV_TO_ARROW))}."
            )
        fields.append(pa.field(n, _BCSV_TO_ARROW[t]))
    return pa.schema(fields)


def _resolve_codec_flags(
    file_codec: str, compression_level: int
) -> Tuple[pybcsv.FileFlags, int]:
    """Map --file-codec + --compression-level to (FileFlags, effective_level)."""
    F = pybcsv.FileFlags
    valid = ("packet_lz4_batch", "packet_lz4", "packet", "stream_lz4", "stream")
    if file_codec not in valid:
        raise ValueError(
            f"Unknown --file-codec: {file_codec}. Valid: {', '.join(valid)}"
        )

    has_lz4 = file_codec in ("packet_lz4_batch", "packet_lz4", "stream_lz4")
    has_batch = file_codec == "packet_lz4_batch"
    is_stream = file_codec in ("stream_lz4", "stream")

    flags = F.NONE
    if has_batch:
        flags = F(int(flags) | int(F.BATCH_COMPRESS))
    if is_stream:
        flags = F(int(flags) | int(F.STREAM_MODE))

    effective_level = compression_level if has_lz4 else 0
    return flags, effective_level


# ---- Main conversion loops ----


def parquet_to_bcsv(
    input_path: str,
    output_path: str,
    row_codec: str = "delta",
    file_codec: str = "packet_lz4_batch",
    compression_level: int = 1,
    packet_size_kb: int = 1024,
    chunk_size: int = 512000,
    force: bool = False,
    verbose: bool = False,
    benchmark: bool = False,
    json_output: bool = False,
) -> dict:
    """Convert a Parquet file to BCSV with streaming batches.

    The BCSV layout is derived automatically from the Parquet schema.
    """
    if not force and os.path.exists(output_path):
        raise FileExistsError(
            f"Output file '{output_path}' already exists. Use --force to overwrite."
        )

    t0 = time.monotonic()
    pf = pq.ParquetFile(input_path)

    validate_parquet_schema(pf)

    flat_schema = flatten_parquet_schema(pf.schema_arrow)
    flat_arrow_schema = pa.schema(flat_schema)
    bcsv_layout = _flat_schema_to_bcsv_layout(flat_schema)

    num_cols = len(bcsv_layout.get_column_names())
    total_rows = 0
    rg_count = pf.metadata.num_row_groups
    flags, effective_level = _resolve_codec_flags(file_codec, compression_level)

    if verbose:
        print(
            f"Converting {pf.metadata.num_rows} rows, "
            f"{rg_count} row group(s) -> "
            f"{num_cols} BCSV columns",
            file=sys.stderr,
        )

    with pybcsv.Writer(bcsv_layout, row_codec) as writer:
        writer.open(
            output_path,
            overwrite=True,
            compression_level=effective_level,
            block_size_kb=packet_size_kb,
            flags=flags,
        )
        for batch in pf.iter_batches(batch_size=chunk_size):
            flat = flatten_batch(batch, flat_schema, flat_arrow_schema)
            _check_nulls(flat, total_rows)
            writer.write_batch(flat)
            total_rows += len(flat)
            del batch, flat

    elapsed = time.monotonic() - t0
    _emit_benchmark(benchmark, json_output, total_rows, elapsed)

    if verbose:
        print(f"Output written: {output_path}", file=sys.stderr)

    return {"rows": total_rows, "elapsed_s": round(elapsed, 4)}


def bcsv_to_parquet(
    input_path: str,
    output_path: str,
    columns: Optional[List[str]] = None,
    row_slice: Optional[str] = None,
    parquet_compression: str = "snappy",
    row_group_size: Optional[int] = None,
    unflatten: bool = True,
    chunk_size: int = 512000,
    force: bool = False,
    verbose: bool = False,
    benchmark: bool = False,
    json_output: bool = False,
) -> dict:
    """Convert a BCSV file to Parquet with streaming batches."""
    if not force and os.path.exists(output_path):
        raise FileExistsError(
            f"Output file '{output_path}' already exists. Use --force to overwrite."
        )

    t0 = time.monotonic()

    da = pybcsv.ReaderDirectAccess()
    da.open(input_path)
    bcsv_layout = da.layout()
    total_rows = da.row_count()
    da.close()

    col_names, col_types, slice_start, slice_end = _parse_selection(
        bcsv_layout, total_rows, columns, row_slice
    )

    if row_slice and total_rows == 0:
        raise ValueError(
            "Cannot apply --slice: the file reports 0 rows. Either it is empty, "
            "or it is a stream-mode BCSV file whose row count is unavailable "
            "without rebuilding the footer. Use an indexed (packet*) file for slicing."
        )

    arrow_schema = (
        unflatten_schema_to_arrow(col_names, col_types)
        if unflatten
        else _flat_arrow_schema(col_names, col_types)
    )

    # Pre-compute subset layout + cached Arrow schema once (outside the batch loop)
    subset_layout: Optional[pybcsv.Layout] = None
    unflatten_schema: Optional[pa.Schema] = None
    if unflatten:
        if col_names and col_names != list(bcsv_layout.get_column_names()):
            # Build the subset layout in the user-requested column order (col_names),
            # NOT file order — otherwise the streamed output and the empty-file
            # fallback (which uses arrow_schema, built from col_names) disagree, and
            # a requested reordering is silently ignored.
            type_by_name = dict(
                zip(
                    bcsv_layout.get_column_names(),
                    bcsv_layout.get_column_types(),
                )
            )
            subset_layout = pybcsv.Layout()
            for n in col_names:
                subset_layout.add_column(n, type_by_name[n])
        else:
            subset_layout = bcsv_layout
        layout_names = list(subset_layout.get_column_names())
        layout_types = list(subset_layout.get_column_types())
        unflatten_schema = unflatten_schema_to_arrow(layout_names, layout_types)

    total_written = 0
    rows_to_write: Optional[int] = (
        (slice_end - slice_start) if slice_end is not None else None
    )
    pq_writer: Optional[pq.ParquetWriter] = None

    if verbose:
        col_desc = ", ".join(col_names[:5]) + (
            f" ... ({len(col_names)} total)" if len(col_names) > 5 else ""
        )
        print(
            f"Converting {total_rows} rows, "
            f"slice={'all' if not row_slice else row_slice}, "
            f"columns=[{col_desc}]",
            file=sys.stderr,
        )

    def _create_writer(schema: pa.Schema) -> pq.ParquetWriter:
        return pq.ParquetWriter(
            output_path,
            schema,
            compression=parquet_compression,
        )

    with pybcsv.ReaderDirectAccess() as reader:
        reader.open(input_path, rebuild_footer=True)
        gen = pybcsv.iter_arrow_batches(
            reader,
            columns=col_names,
            batch_size=chunk_size,
            start_row=slice_start,
        )

        for batch in gen:
            if rows_to_write is not None and total_written >= rows_to_write:
                break

            if unflatten:
                batch = unflatten_batch(
                    batch,
                    subset_layout,  # type: ignore[arg-type]
                    cached_schema=unflatten_schema,
                )

            if pq_writer is None:
                pq_writer = _create_writer(batch.schema)

            if rows_to_write is not None:
                remaining = rows_to_write - total_written
                if len(batch) > remaining:
                    batch = batch.slice(0, remaining)

            pq_writer.write_batch(batch, row_group_size=row_group_size)
            total_written += len(batch)
            del batch

    if pq_writer:
        pq_writer.close()
    else:
        writer = _create_writer(arrow_schema)
        writer.close()

    elapsed = time.monotonic() - t0
    _emit_benchmark(benchmark, json_output, total_written, elapsed)

    if verbose:
        print(f"Output written: {output_path}", file=sys.stderr)

    return {"rows": total_written, "elapsed_s": round(elapsed, 4)}


def _emit_benchmark(
    benchmark: bool,
    json_output: bool,
    total_rows: int,
    elapsed: float,
) -> None:
    """Print benchmark output to stderr."""
    if not benchmark:
        return
    rate = total_rows / elapsed if elapsed > 0 else 0
    if json_output:
        print(
            json.dumps({"rows": total_rows, "elapsed_s": round(elapsed, 4)}),
            file=sys.stdout,
            flush=True,
        )
    else:
        print(
            f"Convert complete: {total_rows} rows in {elapsed:.3f}s "
            f"({rate:.0f} rows/sec)",
            file=sys.stderr,
        )


# ---- CLI Entry Points ----


def parquet2bcsv_cli() -> None:
    """CLI entry point for parquet2bcsv."""
    try:
        import pyarrow.parquet  # noqa: F401
    except ImportError:
        print(
            "Error: pyarrow is required for Parquet conversion. "
            "Install with: pip install pybcsv[arrow]",
            file=sys.stderr,
        )
        sys.exit(2)

    parser = argparse.ArgumentParser(
        prog="parquet2bcsv",
        description="Convert Parquet files to BCSV format.",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {pybcsv.__version__}",
    )
    parser.add_argument("input", help="Input Parquet file")
    parser.add_argument(
        "-o",
        "--output",
        default=None,
        help="Output BCSV file (default: INPUT.bcsv)",
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite existing output",
    )
    parser.add_argument(
        "--row-codec",
        default="delta",
        choices=["delta", "zoh", "flat"],
        help="Row codec (default: delta)",
    )
    parser.add_argument(
        "--file-codec",
        default="packet_lz4_batch",
        choices=[
            "packet_lz4_batch",
            "packet_lz4",
            "packet",
            "stream_lz4",
            "stream",
        ],
        help="File codec (default: packet_lz4_batch)",
    )
    parser.add_argument(
        "--compression-level",
        type=int,
        default=1,
        help="LZ4 compression level 0-9 (default: 1)",
    )
    parser.add_argument(
        "--packet-size-kb",
        type=int,
        default=1024,
        help="Packet size in KB (default: 1024)",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=512000,
        help="Internal batch size (default: 512000)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Verbose output to stderr",
    )
    parser.add_argument(
        "-b",
        "--benchmark",
        action="store_true",
        help="Print timing stats to stderr",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="With --benchmark: JSON timing to stdout",
    )

    args = parser.parse_args()
    output = args.output or f"{args.input}.bcsv"

    try:
        parquet_to_bcsv(
            input_path=args.input,
            output_path=output,
            row_codec=args.row_codec,
            file_codec=args.file_codec,
            compression_level=args.compression_level,
            packet_size_kb=args.packet_size_kb,
            chunk_size=args.chunk_size,
            force=args.force,
            verbose=args.verbose,
            benchmark=args.benchmark,
            json_output=args.json,
        )
    except (ValueError, FileExistsError, FileNotFoundError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1 if isinstance(exc, (ValueError, FileNotFoundError)) else 2)


def bcsv2parquet_cli() -> None:
    """CLI entry point for bcsv2parquet."""
    try:
        import pyarrow.parquet  # noqa: F401
    except ImportError:
        print(
            "Error: pyarrow is required for Parquet conversion. "
            "Install with: pip install pybcsv[arrow]",
            file=sys.stderr,
        )
        sys.exit(2)

    parser = argparse.ArgumentParser(
        prog="bcsv2parquet",
        description="Convert BCSV files to Parquet format.",
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {pybcsv.__version__}",
    )
    parser.add_argument("input", help="Input BCSV file")
    parser.add_argument(
        "-o",
        "--output",
        default=None,
        help="Output Parquet file (default: INPUT.parquet)",
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite existing output",
    )
    parser.add_argument(
        "--columns",
        default=None,
        help='Column selection by name: "col1,col2,..."',
    )
    parser.add_argument(
        "--slice",
        default=None,
        help="Python-style row slice: '10:100', ':1000', '500:'",
    )
    parser.add_argument(
        "--parquet-compression",
        default="snappy",
        choices=["none", "snappy", "gzip", "zstd", "lz4"],
        help="Parquet compression (default: snappy)",
    )
    parser.add_argument(
        "--row-group-size",
        type=int,
        default=None,
        help="Max rows per Parquet row group (applied per streamed batch; "
        "values larger than --chunk-size are bounded by the batch size)",
    )
    mut_unflatten = parser.add_mutually_exclusive_group()
    mut_unflatten.add_argument(
        "--unflatten",
        action="store_true",
        default=True,
        help="Reconstruct nested structs (default)",
    )
    mut_unflatten.add_argument(
        "--no-unflatten",
        action="store_false",
        dest="unflatten",
        help="Export flat columns without nesting",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=512000,
        help="Internal batch size (default: 512000)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Verbose output to stderr",
    )
    parser.add_argument(
        "-b",
        "--benchmark",
        action="store_true",
        help="Print timing stats to stderr",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="With --benchmark: JSON timing to stdout",
    )

    args = parser.parse_args()
    output = args.output or f"{args.input}.parquet"
    do_unflatten = args.unflatten

    col_list: Optional[List[str]] = None
    if args.columns:
        col_list = [c.strip() for c in args.columns.split(",")]

    try:
        bcsv_to_parquet(
            input_path=args.input,
            output_path=output,
            columns=col_list,
            row_slice=args.slice,
            parquet_compression=args.parquet_compression,
            row_group_size=args.row_group_size,
            unflatten=do_unflatten,
            chunk_size=args.chunk_size,
            force=args.force,
            verbose=args.verbose,
            benchmark=args.benchmark,
            json_output=args.json,
        )
    except (ValueError, FileExistsError, FileNotFoundError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1 if isinstance(exc, (ValueError, FileNotFoundError)) else 2)
