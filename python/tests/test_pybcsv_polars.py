#!/usr/bin/env python3
"""
Polars integration tests for pybcsv.
Tests read_polars and write_polars via Arrow C Data Interface.
"""

import unittest
import os

import pybcsv

try:
    import polars as pl
    HAS_POLARS = True
except ImportError:
    HAS_POLARS = False

try:
    import pyarrow as pa
    HAS_ARROW = True
except ImportError:
    HAS_ARROW = False

TMP = os.path.join(os.path.dirname(__file__), '..', '..', 'tmp')


def _write_test_file(path, n=50):
    layout = pybcsv.Layout()
    layout.add_column('i', pybcsv.INT32)
    layout.add_column('x', pybcsv.DOUBLE)
    layout.add_column('name', pybcsv.STRING)
    w = pybcsv.Writer(layout)
    w.open(path, overwrite=True)
    for i in range(n):
        w.write_row([i, i * 1.5, f'row_{i}'])
    w.close()


@unittest.skipUnless(HAS_POLARS and HAS_ARROW, "polars and/or pyarrow not installed")
class TestPolarsIntegration(unittest.TestCase):

    def setUp(self):
        self.path = os.path.join(TMP, 'test_polars.bcsv')
        _write_test_file(self.path, n=20)

    def tearDown(self):
        if os.path.exists(self.path):
            os.unlink(self.path)

    def test_read_polars(self):
        df = pybcsv.read_polars(self.path)
        self.assertIsInstance(df, pl.DataFrame)
        self.assertEqual(len(df), 20)
        self.assertEqual(df.columns, ['i', 'x', 'name'])
        self.assertEqual(df['i'][0], 0)
        self.assertAlmostEqual(df['x'][1], 1.5)
        self.assertEqual(df['name'][2], 'row_2')

    def test_read_polars_column_selection(self):
        df = pybcsv.read_polars(self.path, columns=['i', 'name'])
        self.assertEqual(df.columns, ['i', 'name'])
        self.assertEqual(len(df), 20)

    def test_write_polars_roundtrip(self):
        out = os.path.join(TMP, 'test_polars_write.bcsv')
        try:
            df = pl.DataFrame({
                'a': [1, 2, 3],
                'b': [1.1, 2.2, 3.3],
                'c': ['x', 'y', 'z'],
            })
            pybcsv.write_polars(df, out)
            df2 = pybcsv.read_polars(out)
            self.assertEqual(df2.columns, ['a', 'b', 'c'])
            self.assertEqual(len(df2), 3)
            self.assertEqual(df2['c'].to_list(), ['x', 'y', 'z'])
        finally:
            if os.path.exists(out):
                os.unlink(out)


class TestPolarsStubs(unittest.TestCase):
    """Verify stubs raise ImportError when polars is missing."""

    @unittest.skipIf(HAS_POLARS and HAS_ARROW, "polars+pyarrow installed, stubs not active")
    def test_read_polars_stub(self):
        with self.assertRaises(ImportError):
            pybcsv.read_polars("dummy.bcsv")

    @unittest.skipIf(HAS_POLARS and HAS_ARROW, "polars+pyarrow installed, stubs not active")
    def test_write_polars_stub(self):
        with self.assertRaises(ImportError):
            pybcsv.write_polars(None, "dummy.bcsv")


if __name__ == '__main__':
    unittest.main()
