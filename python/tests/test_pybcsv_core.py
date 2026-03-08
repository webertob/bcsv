#!/usr/bin/env python3
"""
Core BCSV functionality tests.
Tests layout creation, read/write operations, data integrity, context managers,
batch operations, compression, and string limits.
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
from pathlib import Path

import numpy as np
import pandas as pd
import pybcsv


class TestLayoutCreation(unittest.TestCase):
    """Test layout creation with different column types."""

    def test_layout_all_column_types(self):
        layout = pybcsv.Layout()
        layout.add_column("bool_col", pybcsv.BOOL)
        layout.add_column("int8_col", pybcsv.INT8)
        layout.add_column("int16_col", pybcsv.INT16)
        layout.add_column("int32_col", pybcsv.INT32)
        layout.add_column("int64_col", pybcsv.INT64)
        layout.add_column("uint8_col", pybcsv.UINT8)
        layout.add_column("uint16_col", pybcsv.UINT16)
        layout.add_column("uint32_col", pybcsv.UINT32)
        layout.add_column("uint64_col", pybcsv.UINT64)
        layout.add_column("float_col", pybcsv.FLOAT)
        layout.add_column("double_col", pybcsv.DOUBLE)
        layout.add_column("string_col", pybcsv.STRING)

        self.assertEqual(layout.column_count(), 12)

        expected_columns = [
            ("bool_col", pybcsv.BOOL),
            ("int8_col", pybcsv.INT8),
            ("int16_col", pybcsv.INT16),
            ("int32_col", pybcsv.INT32),
            ("int64_col", pybcsv.INT64),
            ("uint8_col", pybcsv.UINT8),
            ("uint16_col", pybcsv.UINT16),
            ("uint32_col", pybcsv.UINT32),
            ("uint64_col", pybcsv.UINT64),
            ("float_col", pybcsv.FLOAT),
            ("double_col", pybcsv.DOUBLE),
            ("string_col", pybcsv.STRING),
        ]

        for i, (expected_name, expected_type) in enumerate(expected_columns):
            self.assertEqual(layout.column_name(i), expected_name)
            self.assertEqual(layout.column_type(i), expected_type)


class TestSimpleWriteRead(unittest.TestCase):
    """Test basic write/read operations."""

    def setUp(self):
        self.temp_files = []

    def tearDown(self):
        for fp in self.temp_files:
            if os.path.exists(fp):
                os.unlink(fp)

    def _tmp(self, suffix='.bcsv'):
        fd, fp = tempfile.mkstemp(suffix=suffix)
        os.close(fd)
        self.temp_files.append(fp)
        return fp

    def test_simple_write_read(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        layout.add_column("value", pybcsv.DOUBLE)

        test_data = [
            [1, "Alice", 123.45],
            [2, "Bob", 678.90],
            [3, "Charlie", 111.22],
            [4, "Diana", 999.99],
        ]

        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(filepath))
        for row in test_data:
            writer.write_row(row)
        writer.close()

        self.assertTrue(os.path.exists(filepath))
        self.assertGreater(os.path.getsize(filepath), 0)

        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        read_data = reader.read_all()
        reader.close()

        self.assertEqual(len(read_data), len(test_data))
        for i, (original, read) in enumerate(zip(test_data, read_data)):
            self.assertEqual(original, read, f"Row {i} mismatch")

    def test_all_column_types(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("bool_val", pybcsv.BOOL)
        layout.add_column("int8_val", pybcsv.INT8)
        layout.add_column("int16_val", pybcsv.INT16)
        layout.add_column("int32_val", pybcsv.INT32)
        layout.add_column("int64_val", pybcsv.INT64)
        layout.add_column("uint8_val", pybcsv.UINT8)
        layout.add_column("uint16_val", pybcsv.UINT16)
        layout.add_column("uint32_val", pybcsv.UINT32)
        layout.add_column("uint64_val", pybcsv.UINT64)
        layout.add_column("float_val", pybcsv.FLOAT)
        layout.add_column("double_val", pybcsv.DOUBLE)
        layout.add_column("string_val", pybcsv.STRING)

        test_data = [
            [True, -128, -32768, -2147483648, -9223372036854775808,
             0, 0, 0, 0, 1.5, 2.5, "hello"],
            [False, 127, 32767, 2147483647, 9223372036854775807,
             255, 65535, 4294967295, 18446744073709551615, -1.5, -2.5, "world"],
            [True, 0, 0, 0, 0, 128, 32768, 2147483648, 9223372036854775808,
             0.0, 0.0, ""],
            [False, -1, -1, -1, -1, 1, 1, 1, 1,
             float('inf'), float('-inf'), "unicode: \U0001f680 \u6d4b\u8bd5"],
        ]

        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(filepath))
        for row in test_data:
            writer.write_row(row)
        writer.close()

        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        read_data = reader.read_all()
        reader.close()

        self.assertEqual(len(read_data), len(test_data))
        for i, (original, read) in enumerate(zip(test_data, read_data)):
            for j, (orig_val, read_val) in enumerate(zip(original, read)):
                if isinstance(orig_val, float) and np.isinf(orig_val):
                    self.assertTrue(np.isinf(read_val))
                    self.assertEqual(np.sign(orig_val), np.sign(read_val))
                else:
                    self.assertEqual(orig_val, read_val, f"Row {i}, col {j}")

    def test_batch_operations(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("value", pybcsv.DOUBLE)

        batch_size = 100
        test_data = [[i, i * 1.5] for i in range(batch_size)]

        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(filepath))
        writer.write_rows(test_data)
        writer.close()

        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        read_data = reader.read_all()
        reader.close()

        self.assertEqual(len(read_data), batch_size)
        for i, (original, read) in enumerate(zip(test_data, read_data)):
            self.assertEqual(original, read, f"Row {i} mismatch")

    def test_context_managers(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.STRING)
        test_data = [["hello"], ["world"]]

        with pybcsv.Writer(layout) as writer:
            writer.open(filepath)
            for row in test_data:
                writer.write_row(row)

        with pybcsv.Reader() as reader:
            reader.open(filepath)
            read_data = reader.read_all()

        self.assertEqual(read_data, test_data)

    def test_file_operations(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)

        writer = pybcsv.Writer(layout)
        self.assertFalse(writer.is_open())
        self.assertTrue(writer.open(filepath))
        self.assertTrue(writer.is_open())
        writer.write_row([42])
        writer.flush()
        writer.close()
        self.assertFalse(writer.is_open())

        reader = pybcsv.Reader()
        self.assertFalse(reader.is_open())
        self.assertTrue(reader.open(filepath))
        self.assertTrue(reader.is_open())
        data = reader.read_all()
        self.assertEqual(data, [[42]])
        reader.close()
        self.assertFalse(reader.is_open())

    def test_empty_file(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)

        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.close()

        reader = pybcsv.Reader()
        reader.open(filepath)
        try:
            data = reader.read_all()
        except RuntimeError:
            data = []
        reader.close()
        self.assertEqual(data, [])

    def test_large_strings(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("large_string", pybcsv.STRING)

        max_string = "x" * 65527
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([max_string])
        writer.close()

        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        self.assertEqual(read_data, [[max_string]])
        self.assertEqual(len(read_data[0][0]), 65527)

        max_limit_string = "x" * 65535
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([max_limit_string])
        writer.close()

        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        self.assertEqual(len(read_data[0][0]), 65535)

        with self.assertRaises(RuntimeError) as context:
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            writer.write_row(["x" * 65536])
            writer.close()
        self.assertIn("exceeds maximum length", str(context.exception))

        with self.assertRaises(RuntimeError) as context:
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            writer.write_row(["x" * 100000])
            writer.close()
        self.assertIn("exceeds maximum length", str(context.exception))

        with self.assertRaises(RuntimeError) as context:
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            writer.write_row(["x" * (200 * 1024 * 1024)])
            writer.close()
        self.assertIn("exceeds maximum length", str(context.exception))

    def test_compression_levels(self):
        filepath_low = self._tmp('.low.bcsv')
        filepath_high = self._tmp('.high.bcsv')

        layout = pybcsv.Layout()
        layout.add_column("data", pybcsv.STRING)

        test_data = [["repetitive data " * 100] for _ in range(50)]

        writer_low = pybcsv.Writer(layout)
        writer_low.open(filepath_low, compression_level=1)
        writer_low.write_rows(test_data)
        writer_low.close()

        writer_high = pybcsv.Writer(layout)
        writer_high.open(filepath_high, compression_level=9)
        writer_high.write_rows(test_data)
        writer_high.close()

        size_low = os.path.getsize(filepath_low)
        size_high = os.path.getsize(filepath_high)

        self.assertGreater(size_low, 0)
        self.assertGreater(size_high, 0)
        self.assertLessEqual(size_high, size_low)

        for fp in [filepath_low, filepath_high]:
            reader = pybcsv.Reader()
            reader.open(fp)
            read_data = reader.read_all()
            reader.close()
            self.assertEqual(read_data, test_data)


def test_basic_operations():
    """Test basic operations (pytest-style function from original test_basic.py)."""
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.ColumnType.INT32)
    layout.add_column("name", pybcsv.ColumnType.STRING)

    test_file = Path("/tmp/test_basic.bcsv")

    writer = pybcsv.Writer(layout)
    try:
        writer.open(str(test_file))
        writer.write_row([1, "test1"])
        writer.write_row([2, "test2"])
        writer.write_row([3, "test3"])
    finally:
        writer.close()

    reader = pybcsv.Reader()
    try:
        reader.open(str(test_file))
        data = reader.read_all()
        assert len(data) == 3
    finally:
        reader.close()

    df = pd.DataFrame({
        'id': [1, 2, 3, 4, 5],
        'value': [1.1, 2.2, 3.3, 4.4, 5.5],
        'name': ['a', 'b', 'c', 'd', 'e'],
    })
    df_file = Path("/tmp/test_df.bcsv")
    pybcsv.write_dataframe(df, str(df_file))
    df_read = pybcsv.read_dataframe(str(df_file))
    assert len(df_read) == len(df)

    test_file.unlink(missing_ok=True)
    df_file.unlink(missing_ok=True)


if __name__ == '__main__':
    unittest.main()
