#!/usr/bin/env python3
"""
Columnar I/O tests for PyBCSV.
Tests read_columns / write_columns roundtrips, numeric types,
different codecs, and empty files via the columnar path.
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import os
import tempfile
import unittest

import numpy as np
import pybcsv


def _tmp(suffix=".bcsv"):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix="pybcsv_test_")
    os.close(fd)
    os.unlink(path)
    return path


class TestColumnarIO(unittest.TestCase):

    def test_write_read_columns_roundtrip(self):
        path = _tmp()
        try:
            columns = {
                "x": np.array([1, 2, 3, 4, 5], dtype=np.int32),
                "y": np.array([1.1, 2.2, 3.3, 4.4, 5.5], dtype=np.float64),
                "z": ["a", "b", "c", "d", "e"],
            }
            col_order = ["x", "y", "z"]
            col_types = [
                pybcsv.ColumnType.INT32,
                pybcsv.ColumnType.DOUBLE,
                pybcsv.ColumnType.STRING,
            ]

            pybcsv.write_columns(path, columns, col_order, col_types)

            result = pybcsv.read_columns(path)
            np.testing.assert_array_equal(result["x"], columns["x"])
            np.testing.assert_array_almost_equal(result["y"], columns["y"])
            self.assertEqual(list(result["z"]), columns["z"])
        finally:
            os.unlink(path)

    def test_read_columns_numeric_types(self):
        """Test all numeric types through columnar I/O."""
        path = _tmp()
        try:
            layout = pybcsv.Layout()
            layout.add_column("i8", pybcsv.ColumnType.INT8)
            layout.add_column("u16", pybcsv.ColumnType.UINT16)
            layout.add_column("f32", pybcsv.ColumnType.FLOAT)

            writer = pybcsv.Writer(layout)
            writer.open(path)
            writer.write_rows([
                [10, 100, 1.5],
                [20, 200, 2.5],
                [30, 300, 3.5],
            ])
            writer.close()

            result = pybcsv.read_columns(path)
            self.assertEqual(result["i8"].dtype, np.int8)
            self.assertEqual(result["u16"].dtype, np.uint16)
            self.assertEqual(result["f32"].dtype, np.float32)
            np.testing.assert_array_equal(result["i8"], [10, 20, 30])
            np.testing.assert_array_equal(result["u16"], [100, 200, 300])
            np.testing.assert_array_almost_equal(result["f32"], [1.5, 2.5, 3.5])
        finally:
            os.unlink(path)

    def test_write_columns_with_codecs(self):
        """Test write_columns with different row codecs."""
        for codec in ["flat", "zoh", "delta"]:
            path = _tmp()
            try:
                columns = {
                    "val": np.array([1, 2, 3], dtype=np.int32),
                }
                pybcsv.write_columns(
                    path, columns, ["val"],
                    [pybcsv.ColumnType.INT32],
                    row_codec=codec,
                )
                result = pybcsv.read_columns(path)
                np.testing.assert_array_equal(result["val"], [1, 2, 3])
            finally:
                os.unlink(path)

    def test_empty_file(self):
        """Write and read zero rows."""
        path = _tmp()
        try:
            columns = {
                "x": np.array([], dtype=np.int32),
            }
            pybcsv.write_columns(
                path, columns, ["x"],
                [pybcsv.ColumnType.INT32],
            )
            result = pybcsv.read_columns(path)
            self.assertEqual(len(result["x"]), 0)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
