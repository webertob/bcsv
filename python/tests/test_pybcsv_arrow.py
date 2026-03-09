#!/usr/bin/env python3
"""
Arrow C Data Interface tests for pybcsv.
Tests read_to_arrow and write_from_arrow interop via pyarrow.
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import unittest
import tempfile
import os

import pybcsv

try:
    import pyarrow as pa
    HAS_ARROW = True
except ImportError:
    HAS_ARROW = False


def _tmp():
    return tempfile.mktemp(suffix='.bcsv', dir=os.path.join(
        os.path.dirname(__file__), '..', '..', 'tmp'))


def _write_test_file(path, n=50):
    """Write a test BCSV file with int32, double, and string columns."""
    layout = pybcsv.Layout()
    layout.add_column('i', pybcsv.INT32)
    layout.add_column('x', pybcsv.DOUBLE)
    layout.add_column('name', pybcsv.STRING)
    w = pybcsv.Writer(layout)
    w.open(path)
    for i in range(n):
        w.write_row([i, i * 1.5, f'row_{i}'])
    w.close()
    return layout


@unittest.skipUnless(HAS_ARROW, "pyarrow not installed")
class TestReadToArrow(unittest.TestCase):
    """Tests for pybcsv.read_to_arrow()."""

    def test_full_read(self):
        """Read entire file into Arrow Table."""
        path = _tmp()
        try:
            _write_test_file(path, 50)
            table = pybcsv.read_to_arrow(path)
            self.assertIsInstance(table, pa.Table)
            self.assertEqual(table.num_rows, 50)
            self.assertEqual(table.num_columns, 3)
            self.assertEqual(table.column_names, ['i', 'x', 'name'])
        finally:
            os.unlink(path)

    def test_column_filter(self):
        """Read only selected columns."""
        path = _tmp()
        try:
            _write_test_file(path, 30)
            table = pybcsv.read_to_arrow(path, columns=['i', 'name'])
            self.assertEqual(table.num_columns, 2)
            self.assertEqual(table.column_names, ['i', 'name'])
            self.assertEqual(table.num_rows, 30)
        finally:
            os.unlink(path)

    def test_single_column(self):
        """Read a single column."""
        path = _tmp()
        try:
            _write_test_file(path, 10)
            table = pybcsv.read_to_arrow(path, columns=['x'])
            self.assertEqual(table.num_columns, 1)
            self.assertEqual(table.column_names, ['x'])
            self.assertEqual(table.num_rows, 10)
        finally:
            os.unlink(path)

    def test_chunked_read(self):
        """Chunked reading produces correct total row count."""
        path = _tmp()
        try:
            _write_test_file(path, 100)
            table = pybcsv.read_to_arrow(path, chunk_size=30)
            self.assertEqual(table.num_rows, 100)
            self.assertEqual(table.num_columns, 3)
        finally:
            os.unlink(path)

    def test_data_values(self):
        """Verify actual data values round-trip correctly."""
        path = _tmp()
        try:
            _write_test_file(path, 10)
            table = pybcsv.read_to_arrow(path)
            col_i = table.column('i').to_pylist()
            col_x = table.column('x').to_pylist()
            col_name = table.column('name').to_pylist()
            self.assertEqual(col_i, list(range(10)))
            for i in range(10):
                self.assertAlmostEqual(col_x[i], i * 1.5)
            self.assertEqual(col_name, [f'row_{i}' for i in range(10)])
        finally:
            os.unlink(path)

    def test_arrow_types(self):
        """Verify Arrow schema types match BCSV types."""
        path = _tmp()
        try:
            _write_test_file(path, 5)
            table = pybcsv.read_to_arrow(path)
            self.assertEqual(table.schema.field('i').type, pa.int32())
            self.assertEqual(table.schema.field('x').type, pa.float64())
            self.assertEqual(table.schema.field('name').type, pa.utf8())
        finally:
            os.unlink(path)

    def test_empty_file(self):
        """Read a file with zero rows."""
        path = _tmp()
        try:
            _write_test_file(path, 0)
            table = pybcsv.read_to_arrow(path)
            self.assertEqual(table.num_rows, 0)
            self.assertEqual(table.num_columns, 3)
        finally:
            os.unlink(path)

    def test_all_numeric_types(self):
        """Test all numeric BCSV column types map to correct Arrow types."""
        path = _tmp()
        try:
            layout = pybcsv.Layout()
            layout.add_column('b', pybcsv.BOOL)
            layout.add_column('i8', pybcsv.INT8)
            layout.add_column('u8', pybcsv.UINT8)
            layout.add_column('i16', pybcsv.INT16)
            layout.add_column('u16', pybcsv.UINT16)
            layout.add_column('i32', pybcsv.INT32)
            layout.add_column('u32', pybcsv.UINT32)
            layout.add_column('i64', pybcsv.INT64)
            layout.add_column('u64', pybcsv.UINT64)
            layout.add_column('f32', pybcsv.FLOAT)
            layout.add_column('f64', pybcsv.DOUBLE)

            w = pybcsv.Writer(layout)
            w.open(path)
            w.write_row([True, -1, 2, -300, 400, -50000, 60000, -7000000, 8000000, 1.5, 2.5])
            w.close()

            table = pybcsv.read_to_arrow(path)
            self.assertEqual(table.schema.field('b').type, pa.bool_())
            self.assertEqual(table.schema.field('i8').type, pa.int8())
            self.assertEqual(table.schema.field('u8').type, pa.uint8())
            self.assertEqual(table.schema.field('i16').type, pa.int16())
            self.assertEqual(table.schema.field('u16').type, pa.uint16())
            self.assertEqual(table.schema.field('i32').type, pa.int32())
            self.assertEqual(table.schema.field('u32').type, pa.uint32())
            self.assertEqual(table.schema.field('i64').type, pa.int64())
            self.assertEqual(table.schema.field('u64').type, pa.uint64())
            self.assertEqual(table.schema.field('f32').type, pa.float32())
            self.assertEqual(table.schema.field('f64').type, pa.float64())
        finally:
            os.unlink(path)


@unittest.skipUnless(HAS_ARROW, "pyarrow not installed")
class TestWriteFromArrow(unittest.TestCase):
    """Tests for pybcsv.write_from_arrow()."""

    def test_basic_write(self):
        """Write an Arrow Table to BCSV and read back."""
        path = _tmp()
        try:
            table = pa.table({
                'a': pa.array([1, 2, 3], type=pa.int32()),
                'b': pa.array([1.0, 2.0, 3.0], type=pa.float64()),
            })
            pybcsv.write_from_arrow(path, table)

            # Verify by reading back
            with pybcsv.Reader() as r:
                r.open(path)
                rows = r.read_all()
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[0][0], 1)
            self.assertAlmostEqual(rows[1][1], 2.0)
        finally:
            os.unlink(path)

    def test_write_with_strings(self):
        """Write Arrow Table with string column."""
        path = _tmp()
        try:
            table = pa.table({
                'id': pa.array([10, 20], type=pa.int32()),
                'label': pa.array(['hello', 'world']),
            })
            pybcsv.write_from_arrow(path, table)

            with pybcsv.Reader() as r:
                r.open(path)
                rows = r.read_all()
            self.assertEqual(rows[0], [10, 'hello'])
            self.assertEqual(rows[1], [20, 'world'])
        finally:
            os.unlink(path)


@unittest.skipUnless(HAS_ARROW, "pyarrow not installed")
class TestArrowRoundTrip(unittest.TestCase):
    """Round-trip tests: BCSV -> Arrow -> BCSV -> Arrow."""

    def test_roundtrip_equality(self):
        """Write BCSV, read to Arrow, write back, read again — values match."""
        path1 = _tmp()
        path2 = _tmp()
        try:
            _write_test_file(path1, 25)
            t1 = pybcsv.read_to_arrow(path1)
            pybcsv.write_from_arrow(path2, t1)
            t2 = pybcsv.read_to_arrow(path2)
            self.assertTrue(t1.equals(t2))
        finally:
            for p in (path1, path2):
                if os.path.exists(p):
                    os.unlink(p)

    def test_roundtrip_large(self):
        """Round-trip with 10000 rows."""
        path1 = _tmp()
        path2 = _tmp()
        try:
            _write_test_file(path1, 10000)
            t1 = pybcsv.read_to_arrow(path1)
            self.assertEqual(t1.num_rows, 10000)
            pybcsv.write_from_arrow(path2, t1)
            t2 = pybcsv.read_to_arrow(path2)
            self.assertEqual(t2.num_rows, 10000)
            self.assertEqual(t1.column('i').to_pylist(), t2.column('i').to_pylist())
        finally:
            for p in (path1, path2):
                if os.path.exists(p):
                    os.unlink(p)

    def test_arrow_to_pandas(self):
        """Read to Arrow, convert to pandas DataFrame."""
        path = _tmp()
        try:
            _write_test_file(path, 20)
            table = pybcsv.read_to_arrow(path)
            df = table.to_pandas()
            self.assertEqual(len(df), 20)
            self.assertListEqual(list(df.columns), ['i', 'x', 'name'])
            self.assertEqual(df['i'].iloc[0], 0)
            self.assertEqual(df['name'].iloc[5], 'row_5')
        finally:
            os.unlink(path)


if __name__ == '__main__':
    unittest.main()
