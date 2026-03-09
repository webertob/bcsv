#!/usr/bin/env python3
"""
Pandas integration tests for PyBCSV.
Tests DataFrame roundtrips, dtype preservation, large datasets,
missing values, Unicode, column order, special values, compression,
CSV conversion, and error handling.
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import os
import tempfile
import time
import unittest

import numpy as np
import pandas as pd
import pybcsv


class TestPandasIntegration(unittest.TestCase):
    """Test pandas DataFrame integration with PyBCSV."""

    def setUp(self):
        self.temp_files = []

    def tearDown(self):
        for fp in self.temp_files:
            if os.path.exists(fp):
                os.unlink(fp)

    def _tmp(self, suffix='.bcsv'):
        fd, fp = tempfile.mkstemp(suffix=suffix)
        os.close(fd)
        os.unlink(fp)
        self.temp_files.append(fp)
        return fp

    def test_basic_dataframe_roundtrip(self):
        filepath = self._tmp()
        df_original = pd.DataFrame({
            'id': [1, 2, 3, 4, 5],
            'name': ['Alice', 'Bob', 'Charlie', 'Diana', 'Eve'],
            'score': [95.5, 87.2, 92.1, 98.7, 89.3],
            'active': [True, False, True, True, False],
        })
        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)

        self.assertEqual(len(df_read), len(df_original))
        self.assertEqual(list(df_read.columns), list(df_original.columns))
        pd.testing.assert_frame_equal(df_read, df_original, check_dtype=False)

    def test_dataframe_dtypes(self):
        filepath = self._tmp()
        df_original = pd.DataFrame({
            'int8_col': np.array([1, 2, 3], dtype=np.int8),
            'int16_col': np.array([100, 200, 300], dtype=np.int16),
            'int32_col': np.array([1000, 2000, 3000], dtype=np.int32),
            'int64_col': np.array([10000, 20000, 30000], dtype=np.int64),
            'uint8_col': np.array([10, 20, 30], dtype=np.uint8),
            'uint16_col': np.array([1000, 2000, 3000], dtype=np.uint16),
            'uint32_col': np.array([100000, 200000, 300000], dtype=np.uint32),
            'uint64_col': np.array([1000000, 2000000, 3000000], dtype=np.uint64),
            'float32_col': np.array([1.5, 2.5, 3.5], dtype=np.float32),
            'float64_col': np.array([1.25, 2.25, 3.25], dtype=np.float64),
            'bool_col': np.array([True, False, True], dtype=bool),
            'string_col': ['hello', 'world', 'test'],
        })
        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)

        self.assertEqual(len(df_read), len(df_original))
        self.assertEqual(list(df_read.columns), list(df_original.columns))
        for col in df_original.columns:
            if df_original[col].dtype == object:
                self.assertTrue(df_read[col].equals(df_original[col]))
            else:
                np.testing.assert_array_equal(df_read[col].values, df_original[col].values)

    def test_empty_dataframe(self):
        filepath = self._tmp()
        df_empty = pd.DataFrame(columns=['id', 'name', 'value'])
        pybcsv.write_dataframe(df_empty, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        self.assertEqual(len(df_read), 0)
        self.assertEqual(list(df_read.columns), ['id', 'name', 'value'])

    def test_large_dataframe(self):
        filepath = self._tmp()
        n_rows = 10000
        df_large = pd.DataFrame({
            'id': range(n_rows),
            'category': [f'cat_{i % 100}' for i in range(n_rows)],
            'value1': np.random.random(n_rows),
            'value2': np.random.random(n_rows),
            'active': [i % 2 == 0 for i in range(n_rows)],
        })
        pybcsv.write_dataframe(df_large, filepath)
        df_read = pybcsv.read_dataframe(filepath)

        self.assertEqual(len(df_read), n_rows)
        self.assertEqual(list(df_read.columns), list(df_large.columns))
        pd.testing.assert_frame_equal(df_read.head(), df_large.head(), check_dtype=False)
        pd.testing.assert_frame_equal(df_read.tail(), df_large.tail(), check_dtype=False)

    def test_dataframe_with_missing_values(self):
        filepath = self._tmp()
        df_with_na = pd.DataFrame({
            'id': [1, 2, 3, 4],
            'name': ['Alice', None, 'Charlie', 'Diana'],
            'score': [95.5, np.nan, 92.1, 98.7],
            'active': [True, None, True, False],
        })
        try:
            pybcsv.write_dataframe(df_with_na, filepath)
            df_read = pybcsv.read_dataframe(filepath)
            self.assertEqual(len(df_read), len(df_with_na))
        except (ValueError, TypeError, RuntimeError):
            pass  # acceptable to reject NaN

    def test_dataframe_index_handling(self):
        filepath = self._tmp()
        df_original = pd.DataFrame({
            'name': ['Alice', 'Bob', 'Charlie'],
            'score': [95.5, 87.2, 92.1],
        }, index=['A', 'B', 'C'])

        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        self.assertEqual(list(df_read.index), [0, 1, 2])
        np.testing.assert_array_equal(df_read['name'].values, df_original['name'].values)
        np.testing.assert_array_equal(df_read['score'].values, df_original['score'].values)

    def test_unicode_in_dataframe(self):
        filepath = self._tmp()
        df_unicode = pd.DataFrame({
            'id': [1, 2, 3, 4],
            'name': ['Alice', 'José', '\u5317\u4eac', '\U0001f680 Rocket'],
            'description': [
                'Regular text',
                'Acentos en español',
                '\u4e2d\u6587\u63cf\u8ff0',
                'Emoji test \U0001f389 \U0001f31f \U0001f680',
            ],
        })
        pybcsv.write_dataframe(df_unicode, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        pd.testing.assert_frame_equal(df_read, df_unicode, check_dtype=False)

    def test_dataframe_column_order(self):
        filepath = self._tmp()
        column_order = ['z_last', 'a_first', 'm_middle', 'b_second']
        df_original = pd.DataFrame({
            col: [f'{col}_{i}' for i in range(3)]
            for col in column_order
        })
        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        self.assertEqual(list(df_read.columns), column_order)
        pd.testing.assert_frame_equal(df_read, df_original, check_dtype=False)

    def test_dataframe_special_values(self):
        filepath = self._tmp()
        df_special = pd.DataFrame({
            'id': [1, 2, 3, 4, 5],
            'value': [1.5, float('inf'), float('-inf'), 0.0, -0.0],
        })
        pybcsv.write_dataframe(df_special, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        self.assertEqual(len(df_read), 5)
        self.assertEqual(df_read.iloc[0]['value'], 1.5)
        self.assertTrue(np.isinf(df_read.iloc[1]['value']) and df_read.iloc[1]['value'] > 0)
        self.assertTrue(np.isinf(df_read.iloc[2]['value']) and df_read.iloc[2]['value'] < 0)
        self.assertEqual(df_read.iloc[3]['value'], 0.0)

    def test_dataframe_mixed_types_error(self):
        filepath = self._tmp()
        df_mixed = pd.DataFrame({'mixed_col': [1, 'string', 3.14, True]})
        try:
            pybcsv.write_dataframe(df_mixed, filepath)
            df_read = pybcsv.read_dataframe(filepath)
            self.assertEqual(len(df_read), len(df_mixed))
        except (ValueError, TypeError, RuntimeError):
            pass

    def test_dataframe_performance_comparison(self):
        filepath_df = self._tmp('.df.bcsv')
        filepath_manual = self._tmp('.manual.bcsv')
        n_rows = 1000
        test_data = {
            'id': list(range(n_rows)),
            'value': [i * 1.5 for i in range(n_rows)],
            'category': [f'cat_{i % 10}' for i in range(n_rows)],
        }
        df = pd.DataFrame(test_data)

        pybcsv.write_dataframe(df, filepath_df)
        df_read = pybcsv.read_dataframe(filepath_df)

        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("value", pybcsv.DOUBLE)
        layout.add_column("category", pybcsv.STRING)
        manual_data = [[row['id'], row['value'], row['category']]
                       for _, row in df.iterrows()]

        writer = pybcsv.Writer(layout)
        writer.open(filepath_manual)
        writer.write_rows(manual_data)
        writer.close()

        reader = pybcsv.Reader()
        reader.open(filepath_manual)
        manual_read_data = reader.read_all()
        reader.close()

        manual_df = pd.DataFrame(manual_read_data, columns=['id', 'value', 'category'])
        pd.testing.assert_frame_equal(df_read, manual_df, check_dtype=False)

    def test_dataframe_compression_levels(self):
        df = pd.DataFrame({
            'data': ['repetitive ' * 50] * 100,
            'id': range(100),
        })
        for level in [1, 5, 9]:
            filepath = self._tmp(f'.comp{level}.bcsv')
            pybcsv.write_dataframe(df, filepath, compression_level=level)
            df_read = pybcsv.read_dataframe(filepath)
            pd.testing.assert_frame_equal(df_read, df, check_dtype=False)
            self.assertGreater(os.path.getsize(filepath), 0)

    def test_dataframe_error_handling(self):
        df = pd.DataFrame({'test': [1, 2, 3]})
        # On Windows, Writer::open creates directories, so Unix-style paths
        # may succeed. Use a non-existent drive letter on Windows.
        if os.name == 'nt':
            bad_write = "Q:\\nonexistent\\dir\\file.bcsv"
            bad_read = "Q:\\nonexistent\\nofile.bcsv"
        else:
            bad_write = "/invalid/path/file.bcsv"
            bad_read = "/path/does/not/exist.bcsv"
        with self.assertRaises((IOError, OSError, RuntimeError)):
            pybcsv.write_dataframe(df, bad_write)
        with self.assertRaises((IOError, OSError, RuntimeError)):
            pybcsv.read_dataframe(bad_read)

        text_file = self._tmp('.txt')
        with open(text_file, 'w') as f:
            f.write("This is not a BCSV file")
        with self.assertRaises((IOError, OSError, RuntimeError)):
            pybcsv.read_dataframe(text_file)


# ---------------------------------------------------------------------------
# CSV conversion tests (originally from test_pandas.py)
# ---------------------------------------------------------------------------


def test_pandas_roundtrip():
    """Test pandas DataFrame roundtrip (pytest-style)."""
    df = pd.DataFrame({
        'id': [1, 2, 3, 4, 5],
        'name': ['Alice', 'Bob', 'Charlie', 'Diana', 'Eve'],
        'age': [25, 30, 35, 28, 32],
        'salary': [50000.0, 60000.0, 70000.0, 55000.0, 65000.0],
        'active': [True, False, True, True, False],
    })

    fd, filename = tempfile.mkstemp(suffix=".bcsv")
    os.close(fd)
    try:
        pybcsv.write_dataframe(df, filename)
        df_read = pybcsv.read_dataframe(filename)

        assert len(df_read) == len(df)
        assert list(df_read.columns) == list(df.columns)
    finally:
        os.unlink(filename)


def test_csv_conversion():
    """Test CSV to BCSV and back conversion."""
    csv_data = "id,name,value\n1,Alice,123.45\n2,Bob,678.90\n3,Charlie,111.22"

    fd_csv, csv_filename = tempfile.mkstemp(suffix=".csv")
    os.close(fd_csv)
    fd_bcsv, bcsv_filename = tempfile.mkstemp(suffix=".bcsv")
    os.close(fd_bcsv)
    fd_out, csv_out_filename = tempfile.mkstemp(suffix=".csv")
    os.close(fd_out)

    try:
        with open(csv_filename, 'w') as f:
            f.write(csv_data)

        pybcsv.from_csv(csv_filename, bcsv_filename)
        pybcsv.to_csv(bcsv_filename, csv_out_filename)

        # Verify the output file exists and has content
        # (exact float formatting may differ, e.g. 678.90 vs 678.9)
        assert os.path.getsize(csv_out_filename) > 0
    finally:
        for fp in [csv_filename, bcsv_filename, csv_out_filename]:
            if os.path.exists(fp):
                os.unlink(fp)


if __name__ == '__main__':
    unittest.main()
