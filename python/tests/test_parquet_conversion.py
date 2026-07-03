"""Unit tests for pybcsv Parquet conversion utilities.

Tests flatten/unflatten logic, name decomposition, validation,
type mapping, and NULL detection.
"""

import unittest
import warnings
from typing import List, Tuple

import pyarrow as pa
import pyarrow.parquet as pq

import pybcsv
from pybcsv.parquet_utils import (
    _check_nulls,
    _decompose_name,
    _flat_arrow_schema,
    _resolve_codec_flags,
    _strip_escape_suffixes,
    bcsv_to_parquet,
    flatten_batch,
    flatten_parquet_schema,
    parse_layout,
    parquet_to_bcsv,
    unflatten_batch,
    unflatten_schema_to_arrow,
    validate_layout_against_parquet,
    validate_parquet_schema,
    _MAX_FLATTEN_DEPTH,
    _MAX_FIXED_LIST_SIZE,
    _FP16_TYPES,
)


def _make_parquet(tmp: str, table: pa.Table) -> None:
    pq.write_table(table, tmp)


def _make_layout(*cols: str):
    return ",".join(cols)


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


class TestParseLayout(unittest.TestCase):
    """Test parse_layout CLI layout string parser."""

    def test_parse_layout_valid(self):
        result = parse_layout("a:int64,b:float")
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0], ("a", pybcsv.ColumnType.INT64))
        self.assertEqual(result[1], ("b", pybcsv.ColumnType.FLOAT))

    def test_parse_layout_invalid_type(self):
        with self.assertRaises(ValueError) as ctx:
            parse_layout("a:bigint")
        self.assertIn("Unknown type", str(ctx.exception))

    def test_parse_layout_empty(self):
        with self.assertRaises(ValueError):
            parse_layout("")

    def test_parse_layout_duplicate_columns(self):
        with self.assertRaises(ValueError) as ctx:
            parse_layout("a:int64,b:float,a:string")
        self.assertIn("Duplicate", str(ctx.exception))

    def test_parse_layout_with_brackets(self):
        result = parse_layout("vals[0]:double,vals[1]:double")
        names = [n for n, _ in result]
        self.assertIn("vals[0]", names)
        self.assertIn("vals[1]", names)

    def test_parse_layout_with_dots(self):
        result = parse_layout("loc.lat:float,loc.lon:float")
        self.assertEqual(len(result), 2)

    def test_parse_layout_all_types(self):
        valid_types = {
            "bool",
            "int8",
            "int16",
            "int32",
            "int64",
            "uint8",
            "uint16",
            "uint32",
            "uint64",
            "float",
            "double",
            "string",
        }
        for tname in valid_types:
            result = parse_layout(f"x:{tname}")
            self.assertEqual(len(result), 1)


class TestLayoutValidation(unittest.TestCase):
    """Test validate_layout_against_parquet."""

    def test_validate_matching(self):
        layout = [("a", pybcsv.ColumnType.INT64), ("b", pybcsv.ColumnType.FLOAT)]
        flat = [("a", pa.int64()), ("b", pa.float32())]
        validate_layout_against_parquet(layout, flat)  # should not raise

    def test_validate_type_mismatch(self):
        layout = [("a", pybcsv.ColumnType.INT64)]
        flat = [("a", pa.float64())]
        with self.assertRaises(ValueError) as ctx:
            validate_layout_against_parquet(layout, flat)
        self.assertIn("mismatch", str(ctx.exception))

    def test_validate_missing_column(self):
        layout = [("a", pybcsv.ColumnType.INT64)]
        flat = [("a", pa.int64()), ("b", pa.float64())]
        with self.assertRaises(ValueError) as ctx:
            validate_layout_against_parquet(layout, flat)
        self.assertIn("Missing", str(ctx.exception))

    def test_validate_extra_column(self):
        layout = [("a", pybcsv.ColumnType.INT64), ("b", pybcsv.ColumnType.FLOAT)]
        flat = [("a", pa.int64())]
        with self.assertRaises(ValueError) as ctx:
            validate_layout_against_parquet(layout, flat)
        self.assertIn("Extra", str(ctx.exception))

    def test_type_float16_widened(self):
        layout = [("x", pybcsv.ColumnType.FLOAT)]
        flat = [("x", pa.float32())]
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            validate_layout_against_parquet(layout, flat)
        # Should warn about float16 widening
        self.assertTrue(len(w) == 0)

    def test_type_float16_bfloat16(self):
        layout = [("x", pybcsv.ColumnType.FLOAT)]
        flat = [("x", pa.float32())]
        # BFloat16 is treated same as float16
        validate_layout_against_parquet(layout, flat)


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

    def test_parse_layout_rejects_trailing_underscore(self):
        with self.assertRaises(ValueError) as ctx:
            parse_layout("data_:int64")
        self.assertIn("ends with '_'", str(ctx.exception))

    def test_parse_layout_allows_internal_underscore(self):
        # Loc_.lat is OK — only trailing underscore on the FULL name is rejected
        result = parse_layout("loc_.lat:float")
        self.assertEqual(len(result), 1)

    def test_flatten_schema_rejects_trailing_underscore(self):
        schema = pa.schema([pa.field("data_", pa.int64())])
        with self.assertRaises(ValueError) as ctx:
            flatten_parquet_schema(schema)
        self.assertIn("ends with '_'", str(ctx.exception))

    def test_flatten_schema_rejects_nested_trailing_underscore(self):
        # Regression: struct field name ending with '_' must be caught
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


if __name__ == "__main__":
    unittest.main()
