"""Unit tests for pybcsv Parquet conversion utilities.

Tests flatten/unflatten logic, name decomposition, validation,
type mapping, and NULL detection.
"""

import json
import os
import tempfile
import unittest
import warnings
from typing import Dict, List, Optional, Tuple

import pyarrow as pa
import pyarrow.compute as pc
import pyarrow.parquet as pq

import pybcsv
from pybcsv.parquet_utils import (
    _apply_null_policy,
    _check_nulls,
    _decompose_name,
    _flat_arrow_schema,
    _flat_schema_to_bcsv_layout,
    _parquet_key_value_metadata,
    _resolve_codec_flags,
    _strip_escape_suffixes,
    bcsv_to_parquet,
    metadata_json_path,
    read_metadata_json,
    flatten_batch,
    flatten_parquet_schema,
    parquet_to_bcsv,
    unflatten_batch,
    unflatten_schema_to_arrow,
    validate_parquet_schema,
    _MAX_FLATTEN_DEPTH,
    _MAX_FIXED_LIST_SIZE,
    _ARROW_TO_BCSV,
)


# pyarrow.compute builds its API dynamically, so the bundled type stubs do not
# declare is_nan/is_null even though they exist at runtime. These wrappers keep
# the call sites below free of repeated type-ignore comments.
def _is_nan(array) -> List[Optional[bool]]:
    return pc.is_nan(array).to_pylist()  # type: ignore[attr-defined]


def _is_null(array) -> List[Optional[bool]]:
    return pc.is_null(array).to_pylist()  # type: ignore[attr-defined]


def _make_parquet(tmp: str, table: pa.Table) -> None:
    pq.write_table(table, tmp)


class TestDecomposeName(unittest.TestCase):
    """Test _decompose_name with various column name formats."""

    def test_decompose_name_dot(self):
        self.assertEqual(_decompose_name("location.lat"), ["location", "lat"])

    def test_decompose_name_bracket(self):
        self.assertEqual(_decompose_name("vals[0]"), ["vals", 0])

    def test_decompose_name_mixed(self):
        self.assertEqual(_decompose_name("loc.readings[2]"), ["loc", "readings", 2])

    def test_decompose_name_escape(self):
        self.assertEqual(_decompose_name("a_.b"), ["a_", "b"])

    def test_decompose_name_flat(self):
        self.assertEqual(_decompose_name("id"), ["id"])

    def test_decompose_deep_nested(self):
        self.assertEqual(_decompose_name("a.b.c[0].d"), ["a", "b", "c", 0, "d"])


class TestStripEscapeSuffixes(unittest.TestCase):
    """Test _strip_escape_suffixes removes underscore collision markers."""

    def test_stripper_plain(self):
        self.assertEqual(_strip_escape_suffixes(["a", "b"]), ["a", "b"])

    def test_stripper_escape(self):
        self.assertEqual(_strip_escape_suffixes(["a_", "b"]), ["a", "b"])

    def test_stripper_double_escape(self):
        self.assertEqual(_strip_escape_suffixes(["a__", "b"]), ["a", "b"])

    def test_stripper_int_preserved(self):
        self.assertEqual(_strip_escape_suffixes(["vals", 0, 1]), ["vals", 0, 1])


class TestFlattenSchema(unittest.TestCase):
    """Test flatten_parquet_schema for various nested structures."""

    def test_flatten_simple_struct(self):
        schema = pa.schema([pa.field("a", pa.struct([pa.field("b", pa.int64())]))])
        flat = flatten_parquet_schema(schema)
        self.assertEqual(flat, [("a.b", pa.int64())])

    def test_flatten_simple_flat(self):
        schema = pa.schema(
            [
                pa.field("id", pa.int64()),
                pa.field("name", pa.string()),
            ]
        )
        flat = flatten_parquet_schema(schema)
        self.assertEqual(flat, [("id", pa.int64()), ("name", pa.string())])

    def test_flatten_deep_nesting(self):
        inner = pa.field("z", pa.int64())
        for level in range(9, 0, -1):
            inner = pa.field(f"l{level}", pa.struct([inner]))
        schema = pa.schema([inner])
        flat = flatten_parquet_schema(schema)
        self.assertEqual(len(flat), 1)
        self.assertEqual(flat[0][0], "l1.l2.l3.l4.l5.l6.l7.l8.l9.z")

    def test_flatten_max_depth_ok(self):
        inner = pa.field("z", pa.int64())
        for level in range(_MAX_FLATTEN_DEPTH - 1, 0, -1):
            inner = pa.field(f"l{level}", pa.struct([inner]))
        schema = pa.schema([inner])
        flat = flatten_parquet_schema(schema)
        self.assertEqual(len(flat), 1)

    def test_flatten_max_depth_rejected(self):
        inner = pa.field("z", pa.int64())
        for level in range(_MAX_FLATTEN_DEPTH + 1, 0, -1):
            inner = pa.field(f"l{level}", pa.struct([inner]))
        schema = pa.schema([inner])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("Nesting depth exceeds", str(ctx.exception))

    def test_flatten_fixed_list(self):
        schema = pa.schema([pa.field("x", pa.list_(pa.int64(), 3))])
        flat = flatten_parquet_schema(schema)
        expected = [
            ("x[0]", pa.int64()),
            ("x[1]", pa.int64()),
            ("x[2]", pa.int64()),
        ]
        self.assertEqual(flat, expected)

    def test_flatten_fixed_list_exceeds(self):
        schema = pa.schema(
            [pa.field("y", pa.list_(pa.int64(), _MAX_FIXED_LIST_SIZE + 1))]
        )
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("exceeds", str(ctx.exception))

    def test_flatten_variable_list_rejected(self):
        schema = pa.schema([pa.field("tags", pa.list_(pa.string()))])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("variable-length", str(ctx.exception))

    def test_flatten_collision_simple(self):
        schema = pa.schema(
            [
                pa.field("a.b", pa.int64()),
                pa.field("a", pa.struct([pa.field("b", pa.float32())])),
            ]
        )
        flat = flatten_parquet_schema(schema)
        flat_names = [n for n, _ in flat]
        self.assertIn("a.b", flat_names)
        self.assertIn("a_.b", flat_names)

    def test_flatten_collision_cascade(self):
        schema = pa.schema(
            [
                pa.field("a.b", pa.int64()),
                pa.field("a_.b", pa.float32()),
                pa.field("a", pa.struct([pa.field("b", pa.float64())])),
            ]
        )
        flat = flatten_parquet_schema(schema)
        flat_names = [n for n, _ in flat]
        self.assertIn("a.b", flat_names)
        self.assertIn("a_.b", flat_names)
        self.assertIn("a__.b", flat_names)

    def test_flatten_mixed_struct_list(self):
        schema = pa.schema(
            [
                pa.field(
                    "loc",
                    pa.struct(
                        [
                            pa.field("readings", pa.list_(pa.float64(), 2)),
                        ]
                    ),
                )
            ]
        )
        flat = flatten_parquet_schema(schema)
        expected = [
            ("loc.readings[0]", pa.float64()),
            ("loc.readings[1]", pa.float64()),
        ]
        self.assertEqual(flat, expected)

    def test_flatten_struct_of_fixed_list(self):
        schema = pa.schema(
            [
                pa.field(
                    "data",
                    pa.struct(
                        [
                            pa.field("arr", pa.list_(pa.int32(), 3)),
                        ]
                    ),
                )
            ]
        )
        flat = flatten_parquet_schema(schema)
        expected_names = {"data.arr[0]", "data.arr[1]", "data.arr[2]"}
        actual_names = {n for n, _ in flat}
        self.assertEqual(actual_names, expected_names)


class TestUnflattenSchema(unittest.TestCase):
    """Test unflatten_schema_to_arrow reconstructs nested structures."""

    def test_unflatten_simple_struct(self):
        names = ["a.b", "a.c"]
        types = [pybcsv.ColumnType.INT64, pybcsv.ColumnType.FLOAT]
        schema = unflatten_schema_to_arrow(names, types)
        self.assertEqual(len(schema), 1)
        self.assertEqual(schema.field(0).name, "a")
        self.assertTrue(pa.types.is_struct(schema.field(0).type))

    def test_unflatten_nested_struct(self):
        names = ["a.b.c", "a.b.d"]
        types = [pybcsv.ColumnType.INT64, pybcsv.ColumnType.FLOAT]
        schema = unflatten_schema_to_arrow(names, types)
        self.assertEqual(len(schema), 1)
        self.assertEqual(schema.field(0).name, "a")

    def test_unflatten_flat_unchanged(self):
        names = ["id", "name"]
        types = [pybcsv.ColumnType.INT64, pybcsv.ColumnType.STRING]
        schema = unflatten_schema_to_arrow(names, types)
        self.assertEqual(len(schema), 2)
        self.assertEqual(schema.field(0).name, "id")
        self.assertEqual(schema.field(1).name, "name")

    def test_unflatten_escape_recovery(self):
        names = ["a_.b"]
        types = [pybcsv.ColumnType.INT64]
        schema = unflatten_schema_to_arrow(names, types)
        self.assertEqual(len(schema), 1)
        self.assertEqual(schema.field(0).name, "a")


class TestFlatSchemaToBcsvLayout(unittest.TestCase):
    """Test _flat_schema_to_bcsv_layout builds layout from flattened schema."""

    def test_basic_types(self):
        flat = [("id", pa.int64()), ("val", pa.float32()), ("name", pa.string())]
        layout = _flat_schema_to_bcsv_layout(flat)
        names = layout.get_column_names()
        types = layout.get_column_types()
        self.assertEqual(names, ["id", "val", "name"])
        self.assertEqual(types[0], pybcsv.ColumnType.INT64)
        self.assertEqual(types[1], pybcsv.ColumnType.FLOAT)
        self.assertEqual(types[2], pybcsv.ColumnType.STRING)

    def test_unsupported_type_rejected(self):
        flat = [("ts", pa.timestamp("us"))]
        with self.assertRaises(ValueError) as ctx:
            _flat_schema_to_bcsv_layout(flat)
        self.assertIn("Cannot map Parquet type", str(ctx.exception))

    def test_empty_schema(self):
        layout = _flat_schema_to_bcsv_layout([])
        self.assertEqual(len(layout.get_column_names()), 0)

    def test_all_mapped_types(self):
        """Every Arrow type that the flatten step can produce must map to BCSV.

        Iterates _FLAT_ARROW_TYPES (the accepted input set) rather than
        _ARROW_TO_BCSV.keys() to catch missing entries.  fp16/bfloat16 are
        widened to float32 by the flatten step, so they are not in the flat
        schema; those are excluded here.
        """
        from pybcsv.parquet_utils import _FLAT_ARROW_TYPES, _FP16_TYPES

        for arrow_type in _FLAT_ARROW_TYPES:
            if arrow_type in _FP16_TYPES:
                continue  # widened to float32 before reaching this function
            flat = [("x", arrow_type)]
            layout = _flat_schema_to_bcsv_layout(flat)
            self.assertEqual(
                len(layout.get_column_names()), 1, f"Missing mapping for {arrow_type}"
            )

    def test_large_string_maps_to_string(self):
        """pa.large_string() must map to BCSV STRING (not raise)."""
        layout = _flat_schema_to_bcsv_layout([("txt", pa.large_string())])
        self.assertEqual(layout.get_column_types()[0], pybcsv.ColumnType.STRING)


class TestUnsupportedTypes(unittest.TestCase):
    """Test rejection of unsupported Parquet types."""

    def test_unsupported_timestamp(self):
        schema = pa.schema([pa.field("ts", pa.timestamp("us"))])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("Unsupported", str(ctx.exception))

    def test_unsupported_decimal(self):
        schema = pa.schema([pa.field("d", pa.decimal128(10, 2))])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        msg = str(ctx.exception)
        self.assertTrue("Unsupported" in msg or "decimal" in msg.lower())

    def test_unsupported_map(self):
        schema = pa.schema([pa.field("m", pa.map_(pa.string(), pa.int64()))])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        msg = str(ctx.exception)
        self.assertTrue("Map" in msg or "unsupported" in msg.lower())


class TestNullChecking(unittest.TestCase):
    """Test _check_nulls with bitmap scan."""

    def test_check_nulls_clean(self):
        batch = pa.RecordBatch.from_arrays(
            [pa.array([1, 2, 3]), pa.array([1.0, 2.0, 3.0])],
            schema=pa.schema([("a", pa.int64()), ("b", pa.float64())]),
        )
        _check_nulls(batch, 0)  # should pass

    def test_check_nulls_first_row(self):
        batch = pa.RecordBatch.from_arrays(
            [pa.array([None, 2, 3]), pa.array([1.0, 2.0, 3.0])],
            schema=pa.schema([("a", pa.int64()), ("b", pa.float64())]),
        )
        with self.assertRaises(ValueError) as ctx:
            _check_nulls(batch, 0)
        self.assertIn("a", str(ctx.exception))
        self.assertIn("row 0", str(ctx.exception))

    def test_check_nulls_middle(self):
        batch = pa.RecordBatch.from_arrays(
            [pa.array([1, 2, None, 4, 5])],
            schema=pa.schema([("x", pa.int64())]),
        )
        with self.assertRaises(ValueError) as ctx:
            _check_nulls(batch, 47000)
        self.assertIn("row 47002", str(ctx.exception))

    def test_check_nulls_offset(self):
        full = pa.RecordBatch.from_arrays(
            [pa.array([-1, 1, 2, None, 4, 5])],
            schema=pa.schema([("x", pa.int64())]),
        )
        sliced = full.slice(1, 4)
        with self.assertRaises(ValueError) as ctx:
            _check_nulls(sliced, 100)
        self.assertIn("row 102", str(ctx.exception))


class TestNullPolicy(unittest.TestCase):
    """_apply_null_policy: 'reject' (default) vs 'nan' (R1)."""

    @staticmethod
    def _batch(values, arrow_type, name="v"):
        return pa.RecordBatch.from_arrays(
            [pa.array(values, arrow_type)],
            schema=pa.schema([(name, arrow_type)]),
        )

    def test_reject_is_unchanged_behaviour(self):
        batch = self._batch([1.0, None, 3.0], pa.float64())
        with self.assertRaises(ValueError) as ctx:
            _apply_null_policy(batch, 0, "reject")
        # Historical message must survive verbatim.
        self.assertIn("BCSV does not support nulls", str(ctx.exception))

    def test_reject_passes_clean_batch_through(self):
        batch = self._batch([1.0, 2.0], pa.float64())
        out, filled = _apply_null_policy(batch, 0, "reject")
        self.assertIs(out, batch)
        self.assertEqual(filled, 0)

    def test_nan_fills_float64(self):
        batch = self._batch([1.0, None, 3.0, None], pa.float64())
        out, filled = _apply_null_policy(batch, 0, "nan")
        self.assertEqual(filled, 2)
        self.assertEqual(out.column(0).null_count, 0)
        self.assertEqual(_is_nan(out.column(0)),
                         [False, True, False, True])

    def test_nan_fills_float32(self):
        batch = self._batch([None, 2.0], pa.float32())
        out, filled = _apply_null_policy(batch, 0, "nan")
        self.assertEqual(filled, 1)
        self.assertEqual(out.column(0).type, pa.float32())
        self.assertEqual(_is_nan(out.column(0)), [True, False])

    def test_nan_fills_float16(self):
        # float16 is widened to BCSV FLOAT downstream; the fill happens first.
        batch = self._batch([None, 2.0], pa.float16())
        out, filled = _apply_null_policy(batch, 0, "nan")
        self.assertEqual(filled, 1)
        self.assertEqual(out.column(0).null_count, 0)

    def test_nan_leaves_clean_batch_untouched(self):
        batch = self._batch([1.0, 2.0], pa.float64())
        out, filled = _apply_null_policy(batch, 0, "nan")
        self.assertIs(out, batch)
        self.assertEqual(filled, 0)

    def test_nan_preserves_other_columns(self):
        batch = pa.RecordBatch.from_arrays(
            [pa.array([1.0, None], pa.float64()), pa.array([7, 8], pa.int32())],
            schema=pa.schema([("f", pa.float64()), ("i", pa.int32())]),
        )
        out, filled = _apply_null_policy(batch, 0, "nan")
        self.assertEqual(filled, 1)
        self.assertEqual(out.column(1).to_pylist(), [7, 8])
        self.assertEqual(out.schema, batch.schema)

    def test_nan_still_raises_on_integer_nulls(self):
        batch = self._batch([1, None, 3], pa.int32(), name="cnt")
        with self.assertRaises(ValueError) as ctx:
            _apply_null_policy(batch, 0, "nan")
        msg = str(ctx.exception)
        self.assertIn("cnt", msg)
        self.assertIn("int32", msg)
        self.assertIn("row 1", msg)

    def test_nan_still_raises_on_bool_nulls(self):
        batch = self._batch([True, None], pa.bool_(), name="flag")
        with self.assertRaises(ValueError):
            _apply_null_policy(batch, 0, "nan")

    def test_nan_integer_message_is_offset_safe(self):
        full = pa.RecordBatch.from_arrays(
            [pa.array([-1, 1, 2, None, 4, 5], pa.int32())],
            schema=pa.schema([("x", pa.int32())]),
        )
        with self.assertRaises(ValueError) as ctx:
            _apply_null_policy(full.slice(1, 4), 100, "nan")
        self.assertIn("row 102", str(ctx.exception))

    def test_invalid_policy_lists_valid_values(self):
        batch = self._batch([1.0], pa.float64())
        with self.assertRaises(ValueError) as ctx:
            _apply_null_policy(batch, 0, "fill")
        for valid in ("reject", "nan", "zero"):
            self.assertIn(valid, str(ctx.exception))

    def test_nan_error_points_at_the_zero_policy(self):
        batch = self._batch([1, None], pa.int32(), name="cnt")
        with self.assertRaises(ValueError) as ctx:
            _apply_null_policy(batch, 0, "nan")
        self.assertIn("null_policy='zero'", str(ctx.exception))


class TestZeroNullPolicy(unittest.TestCase):
    """null_policy='zero': fill every type with the BCSV default."""

    @staticmethod
    def _batch(values, arrow_type, name="v"):
        return pa.RecordBatch.from_arrays(
            [pa.array(values, arrow_type)],
            schema=pa.schema([(name, arrow_type)]),
        )

    def test_fills_floats_with_zero_not_nan(self):
        batch = self._batch([1.5, None], pa.float64())
        out, filled = _apply_null_policy(batch, 0, "zero")
        self.assertEqual(filled, 1)
        self.assertEqual(out.column(0).to_pylist(), [1.5, 0.0])
        self.assertEqual(_is_nan(out.column(0)), [False, False])

    def test_fills_integers(self):
        # The case 'nan' cannot serve, and the reason this policy exists.
        for arrow_type in (pa.int8(), pa.int32(), pa.int64(), pa.uint8(), pa.uint32()):
            with self.subTest(type=arrow_type):
                out, filled = _apply_null_policy(
                    self._batch([7, None], arrow_type), 0, "zero"
                )
                self.assertEqual(filled, 1)
                self.assertEqual(out.column(0).to_pylist(), [7, 0])

    def test_integer_fill_is_zero_not_int_min(self):
        """Guards the reason the fill is explicit: to_numpy() would give INT_MIN."""
        out, _ = _apply_null_policy(self._batch([1, None, 3], pa.int32()), 0, "zero")
        self.assertEqual(out.column(0).to_pylist(), [1, 0, 3])
        self.assertNotIn(-2147483648, out.column(0).to_pylist())

    def test_fills_bool_with_false(self):
        out, filled = _apply_null_policy(self._batch([True, None], pa.bool_()), 0, "zero")
        self.assertEqual(filled, 1)
        self.assertEqual(out.column(0).to_pylist(), [True, False])

    def test_fills_string_with_empty(self):
        out, filled = _apply_null_policy(self._batch(["a", None], pa.string()), 0, "zero")
        self.assertEqual(filled, 1)
        self.assertEqual(out.column(0).to_pylist(), ["a", ""])

    def test_leaves_clean_batch_untouched(self):
        batch = self._batch([1.0, 2.0], pa.float64())
        out, filled = _apply_null_policy(batch, 0, "zero")
        self.assertIs(out, batch)
        self.assertEqual(filled, 0)

    def test_counts_across_mixed_columns(self):
        batch = pa.RecordBatch.from_arrays(
            [
                pa.array([1.0, None, None], pa.float64()),
                pa.array([1, None, 3], pa.int32()),
                pa.array(["x", "y", None], pa.string()),
            ],
            schema=pa.schema(
                [("f", pa.float64()), ("i", pa.int32()), ("s", pa.string())]
            ),
        )
        out, filled = _apply_null_policy(batch, 0, "zero")
        self.assertEqual(filled, 4)
        self.assertEqual(out.column(0).to_pylist(), [1.0, 0.0, 0.0])
        self.assertEqual(out.column(1).to_pylist(), [1, 0, 3])
        self.assertEqual(out.column(2).to_pylist(), ["x", "y", ""])


class TestCodecFlags(unittest.TestCase):
    """Test _resolve_codec_flags mapping."""

    def test_packet(self):
        flags, level = _resolve_codec_flags("packet", 5)
        self.assertEqual(level, 0)
        self.assertEqual(flags, pybcsv.FileFlags.NONE)

    def test_packet_lz4(self):
        flags, level = _resolve_codec_flags("packet_lz4", 3)
        self.assertEqual(level, 3)

    def test_packet_lz4_batch(self):
        flags, level = _resolve_codec_flags("packet_lz4_batch", 1)
        self.assertEqual(level, 1)
        self.assertEqual(bool(int(flags) & int(pybcsv.FileFlags.BATCH_COMPRESS)), True)

    def test_stream(self):
        flags, level = _resolve_codec_flags("stream", 5)
        self.assertEqual(level, 0)
        self.assertEqual(bool(int(flags) & int(pybcsv.FileFlags.STREAM_MODE)), True)

    def test_stream_lz4(self):
        flags, level = _resolve_codec_flags("stream_lz4", 2)
        self.assertEqual(level, 2)
        self.assertEqual(bool(int(flags) & int(pybcsv.FileFlags.STREAM_MODE)), True)

    def test_invalid_codec(self):
        with self.assertRaises(ValueError):
            _resolve_codec_flags("invalid_codec", 1)


class TestUnderscoreNameRejection(unittest.TestCase):
    """Regression: column names ending in '_' must be rejected to prevent
    silent data corruption during escape-suffix stripping on roundtrip."""

    def test_flatten_schema_rejects_trailing_underscore(self):
        schema = pa.schema([pa.field("data_", pa.int64())])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("ends with '_'", str(ctx.exception))

    def test_flatten_schema_rejects_nested_trailing_underscore(self):
        schema = pa.schema(
            [pa.field("loc_", pa.struct([pa.field("lat", pa.float32())]))]
        )
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("ends with '_'", str(ctx.exception))

    def test_flatten_schema_allows_internal_underscore(self):
        schema = pa.schema([pa.field("my_data", pa.int64())])
        flat = flatten_parquet_schema(schema)
        self.assertEqual(flat, [("my_data", pa.int64())])

    def test_unflatten_schema_rejects_trailing_underscore(self):
        names = ["data_"]
        types = [pybcsv.ColumnType.INT64]
        with self.assertRaises(ValueError) as ctx:
            unflatten_schema_to_arrow(names, types)
        self.assertIn("ends with '_'", str(ctx.exception))


class TestFP16FixedSizeList(unittest.TestCase):
    """Regression: FixedSizeList<halffloat, N> must widen to float32, not crash."""

    def test_flatten_fp16_fixed_list(self):
        schema = pa.schema([pa.field("vals", pa.list_(pa.float16(), 3))])
        flat = flatten_parquet_schema(schema)
        expected = [
            ("vals[0]", pa.float32()),
            ("vals[1]", pa.float32()),
            ("vals[2]", pa.float32()),
        ]
        self.assertEqual(flat, expected)

    def test_check_fp16_fixed_list_supported(self):
        schema = pa.schema([pa.field("vals", pa.list_(pa.float16(), 3))])
        flatten_parquet_schema(schema)  # should not raise


class TestFlatArrowSchema(unittest.TestCase):
    """Test _flat_arrow_schema produces correct schemas."""

    def test_flat_schema_basic(self):
        names = ["id", "val", "name"]
        types = [
            pybcsv.ColumnType.INT64,
            pybcsv.ColumnType.DOUBLE,
            pybcsv.ColumnType.STRING,
        ]
        schema = _flat_arrow_schema(names, types)
        self.assertEqual(len(schema), 3)
        self.assertEqual(schema.field(0).name, "id")
        self.assertEqual(schema.field(0).type, pa.int64())
        self.assertEqual(schema.field(1).type, pa.float64())
        self.assertEqual(schema.field(2).type, pa.string())

    def test_flat_schema_empty(self):
        schema = _flat_arrow_schema([], [])
        self.assertEqual(len(schema), 0)


class TestFlattenBatch(unittest.TestCase):
    """Test flatten_batch transforms nested record batches."""

    def test_flatten_simple(self):
        batch = pa.RecordBatch.from_arrays(
            [pa.array([1, 2]), pa.array([10.0, 20.0])],
            schema=pa.schema([("a", pa.int64()), ("b", pa.float64())]),
        )
        flat_schema = [("a", pa.int64()), ("b", pa.float64())]
        result = flatten_batch(batch, flat_schema)
        self.assertEqual(result.num_rows, 2)
        self.assertEqual(result.num_columns, 2)

    def test_flatten_reordered(self):
        batch = pa.RecordBatch.from_arrays(
            [pa.array([1, 2]), pa.array([10.0, 20.0])],
            schema=pa.schema([("a", pa.int64()), ("b", pa.float64())]),
        )
        flat_schema = [("b", pa.float64()), ("a", pa.int64())]
        result = flatten_batch(batch, flat_schema)
        self.assertEqual(result.schema.field(0).name, "b")
        self.assertEqual(result.schema.field(1).name, "a")

    def test_flatten_already_flat_with_dots(self):
        """Parquet file where columns are already flat with dot-separated names.

        This is a regression test for column names like 'sim.ve.counter' where
        the Parquet schema has no nesting but the name contains dots.  The
        flatten_batch function must recognize the name as a literal column name
        and not decompose it into a non-existent struct path.
        """
        batch = pa.RecordBatch.from_arrays(
            [
                pa.array([1, 2, 3]),
                pa.array([10.0, 20.0, 30.0]),
                pa.array([100, 200, 300]),
            ],
            schema=pa.schema(
                [
                    ("sim.ve.counter", pa.int64()),
                    ("sim.ve.value", pa.float64()),
                    ("time", pa.int64()),
                ]
            ),
        )
        flat_schema = [
            ("sim.ve.counter", pa.int64()),
            ("sim.ve.value", pa.float64()),
            ("time", pa.int64()),
        ]
        result = flatten_batch(batch, flat_schema)
        self.assertEqual(result.num_rows, 3)
        self.assertEqual(result.num_columns, 3)
        # Values should be identical since no transformation needed
        self.assertEqual(result.column("sim.ve.counter").to_pylist(), [1, 2, 3])
        self.assertEqual(result.column("sim.ve.value").to_pylist(), [10.0, 20.0, 30.0])
        self.assertEqual(result.column("time").to_pylist(), [100, 200, 300])


class _TempDirCase(unittest.TestCase):
    """Per-test unique temp dir (ctest/pytest may run tests concurrently)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="bcsv_pq_")
        self.tmp = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def path(self, name: str) -> str:
        return os.path.join(self.tmp, name)


def _nullable_table(
    rows: int = 500, metadata: Optional[Dict[str, str]] = None
) -> pa.Table:
    """A table shaped like T13's release: nulls only in float columns."""
    table = pa.table(
        {
            "t": pa.array(range(rows), pa.int32()),
            "pose_x": pa.array(
                [None if i % 7 == 0 else 100.0 + i * 0.01 for i in range(rows)],
                pa.float64(),
            ),
            "temp": pa.array(
                [None if i % 11 == 0 else float(i) for i in range(rows)], pa.float32()
            ),
            "cnt": pa.array([i % 251 for i in range(rows)], pa.uint8()),
        }
    )
    return table.replace_schema_metadata(metadata) if metadata else table


class TestNullPolicyEndToEnd(_TempDirCase):
    """parquet_to_bcsv null_policy, through a real file (R1)."""

    def test_default_still_rejects(self):
        src = self.path("src.parquet")
        pq.write_table(_nullable_table(), src)
        with self.assertRaises(ValueError) as ctx:
            parquet_to_bcsv(src, self.path("out.bcsv"), force=True)
        self.assertIn("BCSV does not support nulls", str(ctx.exception))

    def test_nan_policy_converts_and_reports_count(self):
        src = self.path("src.parquet")
        table = _nullable_table()
        pq.write_table(table, src)
        result = parquet_to_bcsv(
            src, self.path("out.bcsv"), force=True, null_policy="nan"
        )
        expected = (
            table.column("pose_x").null_count + table.column("temp").null_count
        )
        self.assertEqual(result["rows"], table.num_rows)
        self.assertEqual(result["nulls_filled"], expected)

    def test_null_positions_survive_the_round_trip(self):
        """T13's R1 acceptance check: null positions match after parquet->bcsv->parquet."""
        src = self.path("src.parquet")
        bcsv = self.path("out.bcsv")
        back = self.path("back.parquet")
        table = _nullable_table()
        pq.write_table(table, src)

        parquet_to_bcsv(src, bcsv, force=True, null_policy="nan")
        bcsv_to_parquet(bcsv, back, force=True)

        restored = pq.read_table(back)
        self.assertEqual(restored.num_rows, table.num_rows)
        for col in ("pose_x", "temp"):
            self.assertEqual(
                _is_null(table.column(col)),
                _is_nan(restored.column(col)),
                f"null positions diverged for column '{col}'",
            )
        # Non-null values must be untouched.
        self.assertEqual(restored.column("cnt").to_pylist(),
                         table.column("cnt").to_pylist())

    def test_integer_nulls_are_refused_even_under_nan(self):
        src = self.path("src.parquet")
        pq.write_table(
            pa.table({"n": pa.array([1, None, 3], pa.int32())}), src
        )
        with self.assertRaises(ValueError) as ctx:
            parquet_to_bcsv(src, self.path("o.bcsv"), force=True, null_policy="nan")
        self.assertIn("int32", str(ctx.exception))

    def test_zero_policy_converts_integer_nulls_end_to_end(self):
        """The case 'nan' refuses: integer nulls, carried through a real file."""
        src = self.path("src.parquet")
        bcsv = self.path("out.bcsv")
        back = self.path("back.parquet")
        table = pa.table(
            {
                "n": pa.array([1, None, 3], pa.int32()),
                "f": pa.array([1.5, None, 3.5], pa.float64()),
            }
        )
        pq.write_table(table, src)

        result = parquet_to_bcsv(src, bcsv, force=True, null_policy="zero")
        self.assertEqual(result["nulls_filled"], 2)

        bcsv_to_parquet(bcsv, back, force=True)
        restored = pq.read_table(back)
        self.assertEqual(restored.column("n").to_pylist(), [1, 0, 3])
        self.assertEqual(restored.column("f").to_pylist(), [1.5, 0.0, 3.5])

    def test_zero_and_nan_differ_on_float_columns(self):
        src = self.path("src.parquet")
        pq.write_table(
            pa.table({"f": pa.array([1.5, None], pa.float64())}), src
        )
        for policy, expect_nan in (("nan", True), ("zero", False)):
            with self.subTest(policy=policy):
                bcsv = self.path(f"{policy}.bcsv")
                back = self.path(f"{policy}.parquet")
                parquet_to_bcsv(src, bcsv, force=True, null_policy=policy)
                bcsv_to_parquet(bcsv, back, force=True)
                col = pq.read_table(back).column("f")
                self.assertEqual(_is_nan(col)[1], expect_nan)

    def test_invalid_policy_rejected_before_touching_the_output(self):
        src = self.path("src.parquet")
        out = self.path("o.bcsv")
        pq.write_table(_nullable_table(10), src)
        with self.assertRaises(ValueError):
            parquet_to_bcsv(src, out, force=True, null_policy="bogus")
        self.assertFalse(os.path.exists(out))


class TestSeekabilityWarning(_TempDirCase):
    """R4: stream-mode output has no index, and must say so."""

    def _convert(self, file_codec):
        src = self.path("src.parquet")
        # Null-free: this test is about the codec warning, nothing else.
        pq.write_table(
            pa.table({"t": pa.array(range(50), pa.int32())}), src
        )
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            parquet_to_bcsv(
                src, self.path(f"{file_codec}.bcsv"), force=True, file_codec=file_codec
            )
        return caught

    def test_stream_codecs_warn(self):
        for codec in ("stream", "stream_lz4"):
            with self.subTest(codec=codec):
                caught = self._convert(codec)
                messages = [str(w.message) for w in caught]
                self.assertTrue(
                    any("sequential-only" in m for m in messages),
                    f"no seekability warning for {codec}: {messages}",
                )

    def test_packet_codecs_do_not_warn(self):
        for codec in ("packet_lz4_batch", "packet_lz4", "packet"):
            with self.subTest(codec=codec):
                messages = [str(w.message) for w in self._convert(codec)]
                self.assertEqual(
                    [m for m in messages if "sequential-only" in m], []
                )


class TestMetadataJson(_TempDirCase):
    """R2: file-level key/value metadata round-trips via <output>.meta.json."""

    TEN_PAIRS = {
        "rotation_contract": "unit_xyzw_v1",
        "release_id": "rotation-xyzw-v1",
        "producer_commit": "c7671383e9e865e97459ac9bd2e08fa7b6a97cb6",
        "producer_code_sha256": "497e3acf",
        "source_sha256": "aa841aee",
        "calibration_period": "period_1",
        "calibration_sha256": "e4ee24d9",
        "baseline_sha256": "3165192a",
        "time_alignment": "recomputed_from_raw",
        "coordinate_alignment": "period_calibration",
    }

    def _write_source(self, metadata=None):
        src = self.path("src.parquet")
        pq.write_table(_nullable_table(100, metadata), src)
        return src

    def test_arrow_internal_keys_are_excluded(self):
        src = self._write_source(self.TEN_PAIRS)
        kv = _parquet_key_value_metadata(pq.ParquetFile(src))
        self.assertEqual(kv, self.TEN_PAIRS)
        self.assertNotIn("ARROW:schema", kv)

    def test_metadata_json_written_with_hash_and_pairs(self):
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        result = parquet_to_bcsv(src, out, force=True, null_policy="nan")

        self.assertEqual(result["metadata_json"], metadata_json_path(out))
        with open(result["metadata_json"], encoding="utf-8") as fh:
            doc = json.load(fh)
        self.assertEqual(doc["metadata_json_version"], 1)
        self.assertEqual(doc["key_value_metadata"], self.TEN_PAIRS)
        self.assertEqual(doc["source_path"], "src.parquet")
        self.assertEqual(len(doc["source_sha256"]), 64)

    def test_no_source_hash_omits_the_digest(self):
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        parquet_to_bcsv(
            src, out, force=True, null_policy="nan", source_hash=False
        )
        with open(metadata_json_path(out), encoding="utf-8") as fh:
            self.assertIsNone(json.load(fh)["source_sha256"])

    def test_no_metadata_json_when_source_has_no_metadata(self):
        src = self._write_source(None)
        out = self.path("out.bcsv")
        result = parquet_to_bcsv(src, out, force=True, null_policy="nan")
        self.assertIsNone(result["metadata_json"])
        self.assertFalse(os.path.exists(metadata_json_path(out)))

    def test_metadata2json_disabled(self):
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        result = parquet_to_bcsv(
            src, out, force=True, null_policy="nan", metadata2json=False
        )
        self.assertIsNone(result["metadata_json"])
        self.assertFalse(os.path.exists(metadata_json_path(out)))

    def test_ten_pairs_in_ten_pairs_out(self):
        """T13's R2 acceptance check."""
        src = self._write_source(self.TEN_PAIRS)
        bcsv = self.path("out.bcsv")
        back = self.path("back.parquet")
        parquet_to_bcsv(src, bcsv, force=True, null_policy="nan")
        bcsv_to_parquet(bcsv, back, force=True)

        restored = _parquet_key_value_metadata(pq.ParquetFile(back))
        self.assertEqual(restored, self.TEN_PAIRS)

    def test_round_trip_ignores_metadata_json_when_disabled(self):
        src = self._write_source(self.TEN_PAIRS)
        bcsv = self.path("out.bcsv")
        back = self.path("back.parquet")
        parquet_to_bcsv(src, bcsv, force=True, null_policy="nan")
        bcsv_to_parquet(bcsv, back, force=True, json2metadata=False)
        self.assertEqual(_parquet_key_value_metadata(pq.ParquetFile(back)), {})

    def test_explicit_metadata_beats_metadata_json(self):
        src = self._write_source(self.TEN_PAIRS)
        bcsv = self.path("out.bcsv")
        back = self.path("back.parquet")
        parquet_to_bcsv(src, bcsv, force=True, null_policy="nan")
        bcsv_to_parquet(bcsv, back, force=True, metadata={"only": "this"})
        self.assertEqual(
            _parquet_key_value_metadata(pq.ParquetFile(back)), {"only": "this"}
        )

    def test_corrupt_metadata_json_gives_a_clear_error(self):
        src = self._write_source(self.TEN_PAIRS)
        bcsv = self.path("out.bcsv")
        parquet_to_bcsv(src, bcsv, force=True, null_policy="nan")
        with open(metadata_json_path(bcsv), "w", encoding="utf-8") as fh:
            fh.write("{not json")
        with self.assertRaises(ValueError) as ctx:
            bcsv_to_parquet(bcsv, self.path("back.parquet"), force=True)
        self.assertIn("could not be read", str(ctx.exception))

    def test_read_metadata_json_returns_none_when_absent(self):
        self.assertIsNone(read_metadata_json(self.path("nothing.bcsv")))

    def test_suppressing_discards_a_stale_metadata_json(self):
        """A rewritten .bcsv must not inherit the previous file's provenance."""
        first = self.path("first.parquet")
        second = self.path("second.parquet")
        out = self.path("out.bcsv")
        pq.write_table(
            pa.table({"v": pa.array([1.0, 2.0], pa.float64())}).replace_schema_metadata(
                {"release_id": "AAA"}
            ),
            first,
        )
        pq.write_table(
            pa.table(
                {"v": pa.array([9.0, 9.0, 9.0], pa.float64())}
            ).replace_schema_metadata({"release_id": "BBB"}),
            second,
        )

        parquet_to_bcsv(first, out, force=True)
        self.assertTrue(os.path.exists(metadata_json_path(out)))

        parquet_to_bcsv(second, out, force=True, metadata2json=False)
        self.assertFalse(
            os.path.exists(metadata_json_path(out)),
            "stale metadata JSON survived a suppressed re-conversion",
        )

        back = self.path("back.parquet")
        bcsv_to_parquet(out, back, force=True)
        self.assertEqual(_parquet_key_value_metadata(pq.ParquetFile(back)), {})

    def test_source_without_metadata_also_discards_a_stale_json(self):
        first = self.path("first.parquet")
        plain = self.path("plain.parquet")
        out = self.path("out.bcsv")
        pq.write_table(
            _nullable_table(50, {"release_id": "AAA"}), first
        )
        pq.write_table(_nullable_table(50), plain)

        parquet_to_bcsv(first, out, force=True, null_policy="nan")
        self.assertTrue(os.path.exists(metadata_json_path(out)))
        parquet_to_bcsv(plain, out, force=True, null_policy="nan")
        self.assertFalse(os.path.exists(metadata_json_path(out)))

    def test_metadata_json_from_a_different_file_is_refused(self):
        """Path is not identity: a JSON copied in from elsewhere must not apply."""
        src_a = self.path("a.parquet")
        src_b = self.path("b.parquet")
        out_a = self.path("a.bcsv")
        out_b = self.path("b.bcsv")
        pq.write_table(
            _nullable_table(20, self.TEN_PAIRS), src_a
        )
        pq.write_table(_nullable_table(400), src_b)

        parquet_to_bcsv(src_a, out_a, force=True, null_policy="nan")
        parquet_to_bcsv(src_b, out_b, force=True, null_policy="nan")

        # Transplant A's provenance next to B's data.
        with open(metadata_json_path(out_a), encoding="utf-8") as fh:
            stolen = fh.read()
        with open(metadata_json_path(out_b), "w", encoding="utf-8") as fh:
            fh.write(stolen)

        with self.assertRaises(ValueError) as ctx:
            bcsv_to_parquet(out_b, self.path("back.parquet"), force=True)
        self.assertIn("does not describe", str(ctx.exception))

    def test_identical_shape_different_data_is_still_refused(self):
        """Size and row count alone are a heuristic; the digest is the identity check.

        Two fixed-width, uncompressed recordings of the same shape have the same
        byte size and row count, so the cheap pre-checks both pass. Only the
        SHA-256 separates them -- and provenance that can attach to the wrong
        recording is worse than no provenance.
        """
        src_a, src_b = self.path("a.parquet"), self.path("b.parquet")
        out_a, out_b = self.path("a.bcsv"), self.path("b.bcsv")
        # Same schema, same row count, same widths -- different values.
        pq.write_table(
            pa.table({"v": pa.array([1] * 64, pa.int32())}).replace_schema_metadata(
                {"release_id": "AAA"}
            ),
            src_a,
        )
        pq.write_table(
            pa.table({"v": pa.array([2] * 64, pa.int32())}).replace_schema_metadata(
                {"release_id": "BBB"}
            ),
            src_b,
        )
        # Fixed-width, uncompressed: identical shape means identical size.
        for src, out in ((src_a, out_a), (src_b, out_b)):
            parquet_to_bcsv(src, out, force=True, row_codec="flat",
                            file_codec="packet", compression_level=0)

        with open(metadata_json_path(out_a), encoding="utf-8") as fh:
            doc_a = json.load(fh)
        with open(metadata_json_path(out_b), encoding="utf-8") as fh:
            doc_b = json.load(fh)
        # The premise: the cheap checks cannot tell these apart.
        self.assertEqual(doc_a["bcsv_bytes"], doc_b["bcsv_bytes"])
        self.assertEqual(doc_a["bcsv_rows"], doc_b["bcsv_rows"])
        self.assertNotEqual(doc_a["bcsv_sha256"], doc_b["bcsv_sha256"])

        # Transplant A's provenance next to B's data; the digest must refuse it.
        with open(metadata_json_path(out_a), encoding="utf-8") as fh:
            stolen = fh.read()
        with open(metadata_json_path(out_b), "w", encoding="utf-8") as fh:
            fh.write(stolen)
        with self.assertRaises(ValueError) as ctx:
            bcsv_to_parquet(out_b, self.path("back.parquet"), force=True)
        self.assertIn("SHA-256", str(ctx.exception))

    def test_no_bcsv_hash_degrades_to_the_heuristic(self):
        """--no-bcsv-hash is documented as weaker; prove it still writes and reads."""
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        parquet_to_bcsv(src, out, force=True, null_policy="nan", bcsv_hash=False)
        with open(metadata_json_path(out), encoding="utf-8") as fh:
            doc = json.load(fh)
        self.assertIsNone(doc["bcsv_sha256"])
        self.assertIsNotNone(doc["bcsv_bytes"])
        back = self.path("back.parquet")
        bcsv_to_parquet(out, back, force=True)
        self.assertEqual(
            _parquet_key_value_metadata(pq.ParquetFile(back)), self.TEN_PAIRS
        )

    def test_malformed_binding_fields_are_refused(self):
        """A present-but-corrupt binding field must not silently skip its check."""
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        parquet_to_bcsv(src, out, force=True, null_policy="nan")
        for field, bad in [
            ("bcsv_bytes", "invalid"),
            ("bcsv_bytes", -1),
            ("bcsv_bytes", 12.5),
            ("bcsv_bytes", True),
            ("bcsv_rows", "12"),
            ("bcsv_rows", -3),
            ("bcsv_sha256", "nothex"),
            ("bcsv_sha256", 42),
        ]:
            with self.subTest(field=field, value=bad):
                with open(metadata_json_path(out), encoding="utf-8") as fh:
                    doc = json.load(fh)
                doc[field] = bad
                with open(metadata_json_path(out), "w", encoding="utf-8") as fh:
                    json.dump(doc, fh)
                with self.assertRaises(ValueError):
                    read_metadata_json(out, expected_rows=100)

    def test_null_binding_fields_are_treated_as_absent(self):
        """null is how --no-*-hash records 'not computed', and must not throw."""
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        parquet_to_bcsv(src, out, force=True, null_policy="nan")
        with open(metadata_json_path(out), encoding="utf-8") as fh:
            doc = json.load(fh)
        doc["bcsv_sha256"] = None
        doc["bcsv_bytes"] = None
        doc["bcsv_rows"] = None
        with open(metadata_json_path(out), "w", encoding="utf-8") as fh:
            json.dump(doc, fh)
        self.assertEqual(read_metadata_json(out, expected_rows=100), self.TEN_PAIRS)

    def test_binding_fields_are_recorded(self):
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        parquet_to_bcsv(src, out, force=True, null_policy="nan")
        with open(metadata_json_path(out), encoding="utf-8") as fh:
            doc = json.load(fh)
        self.assertEqual(doc["bcsv_bytes"], os.path.getsize(out))
        self.assertEqual(doc["bcsv_rows"], 100)
        self.assertRegex(doc["bcsv_sha256"], r"^[0-9a-f]{64}$")

    def test_json2metadata_false_bypasses_the_binding_check(self):
        """The opt-out must remain usable as the documented escape hatch."""
        src = self._write_source(self.TEN_PAIRS)
        out = self.path("out.bcsv")
        parquet_to_bcsv(src, out, force=True, null_policy="nan")
        with open(metadata_json_path(out), "w", encoding="utf-8") as fh:
            json.dump({"key_value_metadata": {}, "bcsv_bytes": 1}, fh)
        back = self.path("back.parquet")
        bcsv_to_parquet(out, back, force=True, json2metadata=False)
        self.assertEqual(pq.read_table(back).num_rows, 100)


class TestNanNullCollapse(_TempDirCase):
    """The documented lossy case: null and genuine NaN in the same column."""

    def test_genuine_nan_and_null_become_indistinguishable(self):
        """Pins the loss documented for null_policy='nan'.

        A column holding both a real NaN and a null round-trips as two NaNs.
        This is expected, not a defect -- but it is the reason the docs tell
        callers to check their corpus, so changing it must be deliberate.
        """
        src = self.path("src.parquet")
        bcsv = self.path("out.bcsv")
        back = self.path("back.parquet")
        # index 1 is a genuine measured NaN, index 3 is a missing sample.
        pq.write_table(
            pa.table({"v": pa.array([1.0, float("nan"), 3.0, None], pa.float64())}),
            src,
        )

        original = pq.read_table(src).column("v")
        # is_nan propagates null as None, so the two are distinguishable here.
        self.assertEqual(_is_nan(original), [False, True, False, None])
        self.assertEqual(_is_null(original), [False, False, False, True])

        parquet_to_bcsv(src, bcsv, force=True, null_policy="nan")
        bcsv_to_parquet(bcsv, back, force=True)

        restored = pq.read_table(back).column("v")
        # Both are NaN now, and nothing distinguishes which was which.
        self.assertEqual(_is_nan(restored), [False, True, False, True])
        self.assertEqual(restored.null_count, 0)


class TestWireFormatUnchanged(_TempDirCase):
    """The null policy and metadata JSON must not perturb the bytes BCSV writes."""

    def test_null_free_conversion_is_byte_identical_across_policies(self):
        src = self.path("src.parquet")
        pq.write_table(
            pa.table(
                {
                    "t": pa.array(range(300), pa.int32()),
                    "v": pa.array([1.5 * i for i in range(300)], pa.float64()),
                }
            ),
            src,
        )
        a, b = self.path("a.bcsv"), self.path("b.bcsv")
        parquet_to_bcsv(src, a, force=True, null_policy="reject")
        parquet_to_bcsv(src, b, force=True, null_policy="nan")

        raw_a, raw_b = open(a, "rb").read(), open(b, "rb").read()
        # Header bytes 4..12 are the creation timestamp and legitimately differ.
        self.assertEqual(raw_a[:4] + raw_a[12:], raw_b[:4] + raw_b[12:])


if __name__ == "__main__":
    unittest.main()
