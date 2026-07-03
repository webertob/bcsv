#!/usr/bin/env python3
"""
Streaming Arrow binding tests for pybcsv.
Tests read_arrow_batch, write_batch, iter_arrow_batches, and streaming round-trips.
"""

import gc
import os
import tempfile
import unittest

import pybcsv

HAS_ARROW = True
try:
    import pyarrow as pa
except ImportError:
    HAS_ARROW = False


def _tmp():
    return tempfile.mktemp(
        suffix=".bcsv",
        dir=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tmp"),
    )


def _DEFAULT_FLAGS():
    """Match DEFAULT_FILE_FLAGS from bindings.cpp."""
    if hasattr(pybcsv.FileFlags, "BATCH_COMPRESS"):
        return pybcsv.FileFlags.BATCH_COMPRESS
    return pybcsv.FileFlags.NONE


ALL_TYPE_DEFS = [
    ("b", pybcsv.BOOL),
    ("i8", pybcsv.INT8),
    ("u8", pybcsv.UINT8),
    ("i16", pybcsv.INT16),
    ("u16", pybcsv.UINT16),
    ("i32", pybcsv.INT32),
    ("u32", pybcsv.UINT32),
    ("i64", pybcsv.INT64),
    ("u64", pybcsv.UINT64),
    ("f32", pybcsv.FLOAT),
    ("f64", pybcsv.DOUBLE),
    ("s", pybcsv.STRING),
]


def _all_types_layout():
    layout = pybcsv.Layout()
    for name, ctype in ALL_TYPE_DEFS:
        layout.add_column(name, ctype)
    return layout


def _generate_table(nrows, offset=0):
    """Generate a pyarrow.Table with all BCSV types.
    offset adds a base to numeric values so consecutive batches have different data."""
    cols = {}
    cols["b"] = pa.array(
        [bool((j + offset) % 2) for j in range(nrows)], type=pa.bool_()
    )
    cols["i8"] = pa.array(
        [((j + offset) % 128) - 64 for j in range(nrows)], type=pa.int8()
    )
    cols["u8"] = pa.array([(j + offset) % 256 for j in range(nrows)], type=pa.uint8())
    cols["i16"] = pa.array(
        [((j + offset) % 65536) - 32768 for j in range(nrows)], type=pa.int16()
    )
    cols["u16"] = pa.array(
        [(j + offset) % 65536 for j in range(nrows)], type=pa.uint16()
    )
    cols["i32"] = pa.array(
        [(j + offset) % (2**31) for j in range(nrows)], type=pa.int32()
    )
    cols["u32"] = pa.array(
        [(j + offset) % (2**32) for j in range(nrows)], type=pa.uint32()
    )
    cols["i64"] = pa.array([j + offset for j in range(nrows)], type=pa.int64())
    cols["u64"] = pa.array([j + offset for j in range(nrows)], type=pa.uint64())
    cols["f32"] = pa.array(
        [float(j + offset) * 0.1 for j in range(nrows)], type=pa.float32()
    )
    cols["f64"] = pa.array(
        [float(j + offset) * 0.01 for j in range(nrows)], type=pa.float64()
    )
    cols["s"] = pa.array([f"row_{j + offset}" for j in range(nrows)], type=pa.utf8())
    return pa.table(cols)


def _arrow_to_bcsv_type(arrow_type):
    t = str(arrow_type)
    mapping = {
        "bool": pybcsv.BOOL,
        "int8": pybcsv.INT8,
        "uint8": pybcsv.UINT8,
        "int16": pybcsv.INT16,
        "uint16": pybcsv.UINT16,
        "int32": pybcsv.INT32,
        "uint32": pybcsv.UINT32,
        "int64": pybcsv.INT64,
        "uint64": pybcsv.UINT64,
        "float": pybcsv.FLOAT,
        "double": pybcsv.DOUBLE,
    }
    for k, v in mapping.items():
        if k in t:
            return v
    if "utf8" in t or "string" in t:
        return pybcsv.STRING
    raise ValueError(f"Unsupported Arrow type: {t}")


@unittest.skipUnless(HAS_ARROW, "pyarrow not installed")
class TestReadArrowBatch(unittest.TestCase):
    """Tests for ReaderDirectAccess.read_arrow_batch() and iter_arrow_batches()."""

    def test_yields_correct_count(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(1000))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(pybcsv.iter_arrow_batches(r, batch_size=300))
            total = sum(b.num_rows for b in batches)
            self.assertEqual(total, 1000)
            self.assertEqual(len(batches), 4)
            self.assertEqual(batches[0].num_rows, 300)
            self.assertEqual(batches[3].num_rows, 100)
        finally:
            os.unlink(path)

    def test_batch_boundary_integrity(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(300))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(pybcsv.iter_arrow_batches(r, batch_size=100))
            self.assertEqual(len(batches), 3)
            vals = batches[0].column("i32").to_pylist()
            self.assertEqual(vals[-1], 99)
            vals0 = batches[1].column("i32").to_pylist()
            self.assertEqual(vals0[0], 100)
            self.assertEqual(len(vals0), 100)
        finally:
            os.unlink(path)

    def test_all_12_types(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(50))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(pybcsv.iter_arrow_batches(r, batch_size=20))
            result = pa.Table.from_batches(batches)
            self.assertEqual(result.num_rows, 50)
            self.assertEqual(result.num_columns, 12)
        finally:
            os.unlink(path)

    def test_column_filter(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(50))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(
                    pybcsv.iter_arrow_batches(r, columns=["i32", "s"], batch_size=20)
                )
            for b in batches:
                self.assertEqual(b.num_columns, 2)
                self.assertEqual(b.column_names, ["i32", "s"])
        finally:
            os.unlink(path)

    def test_start_row_seek(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(100))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(
                    pybcsv.iter_arrow_batches(r, start_row=50, batch_size=50)
                )
            total = sum(b.num_rows for b in batches)
            self.assertEqual(total, 50)
            i32_vals = batches[0].column("i32").to_pylist()
            self.assertEqual(i32_vals[0], 50)
        finally:
            os.unlink(path)

    def test_empty_file(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(0))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(pybcsv.iter_arrow_batches(r))
            self.assertEqual(len(batches), 0)
        finally:
            os.unlink(path)

    def test_direct_read_arrow_batch(self):
        """Test read_arrow_batch directly without iter_arrow_batches helper."""
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(100))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                b1 = r.read_arrow_batch(0, 30)
                self.assertEqual(b1.num_rows, 30)
                b2 = r.read_arrow_batch(30, 30)
                self.assertEqual(b2.num_rows, 30)
                b3 = r.read_arrow_batch(70, 30)
                self.assertEqual(b3.num_rows, 30)
                b4 = r.read_arrow_batch(95, 30)
                self.assertEqual(b4.num_rows, 5)
                b5 = r.read_arrow_batch(100, 30)
                self.assertIsNone(b5)
        finally:
            os.unlink(path)

    def test_file_row_count(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(256))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                self.assertEqual(r.row_count(), 256)
        finally:
            os.unlink(path)

    def test_early_gc_cleanup(self):
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(1000))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(pybcsv.iter_arrow_batches(r, batch_size=100))
            total = sum(b.num_rows for b in batches)
            self.assertEqual(total, 1000)
            gc.collect()
            # Can re-open the same file
            with pybcsv.ReaderDirectAccess() as r2:
                r2.open(path, rebuild_footer=True)
                batches2 = list(pybcsv.iter_arrow_batches(r2, batch_size=100))
            total2 = sum(b.num_rows for b in batches2)
            self.assertEqual(total2, 1000)
        finally:
            os.unlink(path)

    def test_iter_returns_generator(self):
        """Verify iter_arrow_batches returns a generator."""
        path = _tmp()
        try:
            pybcsv.write_from_arrow(path, _generate_table(10))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                gen = pybcsv.iter_arrow_batches(r, batch_size=10)
                self.assertTrue(hasattr(gen, "__iter__"))
                self.assertTrue(hasattr(gen, "__next__"))
        finally:
            os.unlink(path)


@unittest.skipUnless(HAS_ARROW, "pyarrow not installed")
class TestWriteBatch(unittest.TestCase):
    """Tests for Writer.write_batch()."""

    def test_basic_write(self):
        path = _tmp()
        try:
            table = _generate_table(100)
            layout = _all_types_layout()
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                w.write_batch(table)
                self.assertEqual(w.row_count(), 100)
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.num_rows, 100)
        finally:
            os.unlink(path)

    def test_multi_batch(self):
        path = _tmp()
        try:
            layout = _all_types_layout()
            cumulative = 0
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                for _ in range(10):
                    w.write_batch(_generate_table(100, offset=cumulative))
                    cumulative += 100
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.num_rows, 1000)
        finally:
            os.unlink(path)

    def test_all_12_types(self):
        path = _tmp()
        try:
            layout = _all_types_layout()
            table = _generate_table(50)
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                w.write_batch(table)
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.num_rows, 50)
            self.assertEqual(t2.num_columns, 12)
        finally:
            os.unlink(path)

    def test_empty_batch(self):
        path = _tmp()
        try:
            layout = _all_types_layout()
            empty = _generate_table(0)
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                w.write_batch(empty)
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.num_rows, 0)
        finally:
            os.unlink(path)

    def test_context_manager(self):
        path = _tmp()
        try:
            layout = _all_types_layout()
            table = _generate_table(20)
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                w.write_batch(table)
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.num_rows, 20)
        finally:
            os.unlink(path)

    def test_exception_writes_footer(self):
        path = _tmp()
        try:
            layout = _all_types_layout()
            try:
                with pybcsv.Writer(layout) as w:
                    w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                    w.write_batch(_generate_table(50))
                    w.write_batch(_generate_table(30))
                    raise ValueError("simulated failure")
            except ValueError:
                pass
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.num_rows, 80)
        finally:
            if os.path.exists(path):
                os.unlink(path)

    def test_codec_variants(self):
        layout = _all_types_layout()
        for codec in ("flat", "zoh", "delta"):
            with self.subTest(codec=codec):
                path = _tmp()
                try:
                    table = _generate_table(30)
                    with pybcsv.Writer(layout, row_codec=codec) as w:
                        w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                        w.write_batch(table)
                    t2 = pybcsv.read_to_arrow(path)
                    self.assertEqual(t2.num_rows, 30)
                finally:
                    os.unlink(path)

    def test_row_count_tracking(self):
        path = _tmp()
        try:
            layout = _all_types_layout()
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                self.assertEqual(w.row_count(), 0)
                w.write_batch(_generate_table(50))
                self.assertEqual(w.row_count(), 50)
                w.write_batch(_generate_table(30))
                self.assertEqual(w.row_count(), 80)
        finally:
            os.unlink(path)

    def test_wrong_column_count(self):
        path = _tmp()
        try:
            layout = pybcsv.Layout()
            layout.add_column("a", pybcsv.INT32)
            table = pa.table({"a": [1, 2], "b": [3.0, 4.0]})
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                with self.assertRaises(RuntimeError):
                    w.write_batch(table)
        finally:
            if os.path.exists(path):
                os.unlink(path)

    def test_null_strings(self):
        path = _tmp()
        try:
            table = pa.table(
                {
                    "a": pa.array([1, 2, 3], type=pa.int32()),
                    "s": pa.array(["hello", None, "world"], type=pa.utf8()),
                }
            )
            layout = pybcsv.Layout()
            layout.add_column("a", pybcsv.INT32)
            layout.add_column("s", pybcsv.STRING)
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                w.write_batch(table)
            t2 = pybcsv.read_to_arrow(path)
            self.assertEqual(t2.column("s").to_pylist(), ["hello", "", "world"])
        finally:
            os.unlink(path)


@unittest.skipUnless(HAS_ARROW, "pyarrow not installed")
class TestStreamingRoundTrip(unittest.TestCase):
    def test_100_rows(self):
        path = _tmp()
        try:
            table = _generate_table(100)
            layout = _all_types_layout()
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                w.write_batch(table)
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                batches = list(pybcsv.iter_arrow_batches(r, batch_size=30))
            result = pa.Table.from_batches(batches)
            self.assertEqual(result.num_rows, 100)
            for col_name in table.column_names:
                self.assertEqual(
                    table.column(col_name).to_pylist(),
                    result.column(col_name).to_pylist(),
                    f"Mismatch in column {col_name}",
                )
        finally:
            os.unlink(path)

    def test_10m_rows_count_and_samples(self):
        path = _tmp()
        total_rows = 10_000_000
        batch_size = 500_000
        try:
            layout = _all_types_layout()
            num_batches = (total_rows + batch_size - 1) // batch_size
            offset = 0
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                for _ in range(num_batches):
                    w.write_batch(_generate_table(batch_size, offset=offset))
                    offset += batch_size
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                read_batches = list(pybcsv.iter_arrow_batches(r, batch_size=batch_size))
            total_read = sum(b.num_rows for b in read_batches)
            self.assertEqual(total_read, total_rows)
            full_table = pa.Table.from_batches(read_batches)
            i32_np = full_table.column("i32").to_numpy()
            self.assertEqual(i32_np[0], 0)
            self.assertEqual(i32_np[1_000_000], 1_000_000)
            self.assertEqual(i32_np[5_000_000], 5_000_000)
            self.assertEqual(i32_np[9_000_000], 9_000_000)
            self.assertEqual(i32_np[total_rows - 1], total_rows - 1)
        finally:
            if os.path.exists(path):
                os.unlink(path)

    def test_10m_rows_all_types(self):
        total_rows = 10_000_000
        batch_size = 500_000
        path = _tmp()
        try:
            layout = _all_types_layout()
            num_batches = (total_rows + batch_size - 1) // batch_size
            with pybcsv.Writer(layout) as w:
                w.open(path, overwrite=True, flags=_DEFAULT_FLAGS())
                for _ in range(num_batches):
                    w.write_batch(_generate_table(batch_size))
            with pybcsv.ReaderDirectAccess() as r:
                r.open(path, rebuild_footer=True)
                read_batches = list(pybcsv.iter_arrow_batches(r, batch_size=batch_size))
            total_read = sum(b.num_rows for b in read_batches)
            self.assertEqual(total_read, total_rows)
        finally:
            if os.path.exists(path):
                os.unlink(path)


if __name__ == "__main__":
    unittest.main()
