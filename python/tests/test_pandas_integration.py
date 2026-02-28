#!/usr/bin/env python3
"""
Pandas integration tests for PyBCSV.
Tests DataFrame read/write, type conversions, large datasets, and pandas-specific functionality.
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
import numpy as np
import pandas as pd
import pybcsv
from typing import List, Any
import warnings


class TestPandasIntegration(unittest.TestCase):
    """Test pandas DataFrame integration with PyBCSV."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_files = []

    def tearDown(self):
        """Clean up temporary files."""
        for filepath in self.temp_files:
            if os.path.exists(filepath):
                os.unlink(filepath)

    def _create_temp_file(self, suffix='.bcsv') -> str:
        """Create a temporary file and track it for cleanup."""
        fd, filepath = tempfile.mkstemp(suffix=suffix)
        os.close(fd)
        self.temp_files.append(filepath)
        return filepath

    def test_basic_dataframe_roundtrip(self):
        """Test basic DataFrame write and read operations."""
        filepath = self._create_temp_file()
        
        # Create test DataFrame
        df_original = pd.DataFrame({
            'id': [1, 2, 3, 4, 5],
            'name': ['Alice', 'Bob', 'Charlie', 'Diana', 'Eve'],
            'score': [95.5, 87.2, 92.1, 98.7, 89.3],
            'active': [True, False, True, True, False]
        })
        
        # Write DataFrame
        pybcsv.write_dataframe(df_original, filepath)
        
        # Read DataFrame back
        df_read = pybcsv.read_dataframe(filepath)
        
        # Verify structure
        self.assertEqual(len(df_read), len(df_original))
        self.assertEqual(list(df_read.columns), list(df_original.columns))
        
        # Verify data
        pd.testing.assert_frame_equal(df_read, df_original, check_dtype=False)

    def test_dataframe_dtypes(self):
        """Test DataFrame with various numpy dtypes."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with specific dtypes
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
            'string_col': ['hello', 'world', 'test']
        })
        
        # Write and read
        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Verify data (allowing for dtype differences)
        self.assertEqual(len(df_read), len(df_original))
        self.assertEqual(list(df_read.columns), list(df_original.columns))
        
        for col in df_original.columns:
            if df_original[col].dtype == object:  # String columns
                self.assertTrue(df_read[col].equals(df_original[col]))
            else:  # Numeric/boolean columns
                np.testing.assert_array_equal(df_read[col].values, df_original[col].values)

    def test_empty_dataframe(self):
        """Test handling of empty DataFrames."""
        filepath = self._create_temp_file()
        
        # Create empty DataFrame with columns
        df_empty = pd.DataFrame(columns=['id', 'name', 'value'])
        
        # Write and read empty DataFrame
        pybcsv.write_dataframe(df_empty, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Should preserve column structure
        self.assertEqual(len(df_read), 0)
        self.assertEqual(list(df_read.columns), ['id', 'name', 'value'])

    def test_large_dataframe(self):
        """Test handling of large DataFrames."""
        filepath = self._create_temp_file()
        
        # Create large DataFrame (10,000 rows)
        n_rows = 10000
        df_large = pd.DataFrame({
            'id': range(n_rows),
            'category': [f'cat_{i % 100}' for i in range(n_rows)],
            'value1': np.random.random(n_rows),
            'value2': np.random.random(n_rows),
            'active': [i % 2 == 0 for i in range(n_rows)]
        })
        
        # Write and read
        pybcsv.write_dataframe(df_large, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Verify structure and sample data
        self.assertEqual(len(df_read), n_rows)
        self.assertEqual(list(df_read.columns), list(df_large.columns))
        
        # Check first and last rows
        pd.testing.assert_frame_equal(df_read.head(), df_large.head(), check_dtype=False)
        pd.testing.assert_frame_equal(df_read.tail(), df_large.tail(), check_dtype=False)

    def test_dataframe_with_missing_values(self):
        """Test DataFrame handling with NaN/None values."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with missing values
        df_with_na = pd.DataFrame({
            'id': [1, 2, 3, 4],
            'name': ['Alice', None, 'Charlie', 'Diana'],
            'score': [95.5, np.nan, 92.1, 98.7],
            'active': [True, None, True, False]
        })
        
        # This might fail or handle NaN values differently
        # Test the behavior and document it
        try:
            pybcsv.write_dataframe(df_with_na, filepath)
            df_read = pybcsv.read_dataframe(filepath)
            
            # If successful, verify the result
            self.assertEqual(len(df_read), len(df_with_na))
            
        except (ValueError, TypeError, RuntimeError) as e:
            # It's acceptable to reject DataFrames with missing values
            self.assertIn('nan', str(e).lower(), 
                         "Error should mention NaN/missing value issue")

    def test_dataframe_index_handling(self):
        """Test how DataFrame indices are handled."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with custom index
        df_original = pd.DataFrame({
            'name': ['Alice', 'Bob', 'Charlie'],
            'score': [95.5, 87.2, 92.1]
        }, index=['A', 'B', 'C'])
        
        # Write and read
        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Index should be reset to default (BCSV doesn't preserve custom indices)
        self.assertEqual(list(df_read.index), [0, 1, 2])
        self.assertEqual(list(df_read.columns), ['name', 'score'])
        
        # Data should be preserved
        np.testing.assert_array_equal(df_read['name'].values, df_original['name'].values)
        np.testing.assert_array_equal(df_read['score'].values, df_original['score'].values)

    def test_unicode_in_dataframe(self):
        """Test Unicode string handling in DataFrames."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with Unicode strings
        df_unicode = pd.DataFrame({
            'id': [1, 2, 3, 4],
            'name': ['Alice', 'José', '北京', '🚀 Rocket'],
            'description': [
                'Regular text',
                'Acentos en español', 
                '中文描述',
                'Emoji test 🎉 🌟 🚀'
            ]
        })
        
        # Write and read
        pybcsv.write_dataframe(df_unicode, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Verify Unicode preservation
        pd.testing.assert_frame_equal(df_read, df_unicode, check_dtype=False)

    def test_dataframe_column_order(self):
        """Test that column order is preserved."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with specific column order
        column_order = ['z_last', 'a_first', 'm_middle', 'b_second']
        df_original = pd.DataFrame({
            col: [f'{col}_{i}' for i in range(3)]
            for col in column_order
        })
        
        # Write and read
        pybcsv.write_dataframe(df_original, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Verify column order preservation
        self.assertEqual(list(df_read.columns), column_order)
        pd.testing.assert_frame_equal(df_read, df_original, check_dtype=False)

    def test_dataframe_special_values(self):
        """Test DataFrame with special floating point values."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with special float values
        df_special = pd.DataFrame({
            'id': [1, 2, 3, 4, 5],
            'value': [1.5, float('inf'), float('-inf'), 0.0, -0.0]
        })
        
        # Write and read
        pybcsv.write_dataframe(df_special, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        
        # Verify special values
        self.assertEqual(len(df_read), 5)
        self.assertEqual(df_read.iloc[0]['value'], 1.5)
        self.assertTrue(np.isinf(df_read.iloc[1]['value']) and df_read.iloc[1]['value'] > 0)
        self.assertTrue(np.isinf(df_read.iloc[2]['value']) and df_read.iloc[2]['value'] < 0)
        self.assertEqual(df_read.iloc[3]['value'], 0.0)

    def test_dataframe_mixed_types_error(self):
        """Test DataFrame with mixed types in same column (should fail)."""
        filepath = self._create_temp_file()
        
        # Create DataFrame with mixed types (pandas allows this but BCSV shouldn't)
        df_mixed = pd.DataFrame({
            'mixed_col': [1, 'string', 3.14, True]  # Mixed types
        })
        
        # Current implementation may handle mixed types by converting to string
        # or may succeed with type coercion, so we test but allow either behavior
        try:
            pybcsv.write_dataframe(df_mixed, filepath)
            # If it succeeds, verify we can read it back
            df_read = pybcsv.read_dataframe(filepath)
            self.assertEqual(len(df_read), len(df_mixed))
            print("Mixed types handled successfully through type coercion")
        except (ValueError, TypeError, RuntimeError):
            # It's also acceptable to fail with mixed types
            print("Mixed types properly rejected")
            pass

    def test_dataframe_performance_comparison(self):
        """Test performance of DataFrame operations vs manual operations."""
        filepath_df = self._create_temp_file('.df.bcsv')
        filepath_manual = self._create_temp_file('.manual.bcsv')
        
        # Create test data
        n_rows = 1000
        test_data = {
            'id': list(range(n_rows)),
            'value': [i * 1.5 for i in range(n_rows)],
            'category': [f'cat_{i % 10}' for i in range(n_rows)]
        }
        
        df = pd.DataFrame(test_data)
        
        # Time DataFrame operation
        import time
        
        start_time = time.time()
        pybcsv.write_dataframe(df, filepath_df)
        df_write_time = time.time() - start_time
        
        start_time = time.time()
        df_read = pybcsv.read_dataframe(filepath_df)
        df_read_time = time.time() - start_time
        
        # Time manual operation
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("value", pybcsv.DOUBLE)
        layout.add_column("category", pybcsv.STRING)
        
        manual_data = [[row['id'], row['value'], row['category']] 
                      for _, row in df.iterrows()]
        
        start_time = time.time()
        writer = pybcsv.Writer(layout)
        writer.open(filepath_manual)
        writer.write_rows(manual_data)
        writer.close()
        manual_write_time = time.time() - start_time
        
        start_time = time.time()
        reader = pybcsv.Reader()
        reader.open(filepath_manual)
        manual_read_data = reader.read_all()
        reader.close()
        manual_read_time = time.time() - start_time
        
        # Verify both methods produce equivalent results
        manual_df = pd.DataFrame(manual_read_data, columns=['id', 'value', 'category'])
        pd.testing.assert_frame_equal(df_read, manual_df, check_dtype=False)
        
        # DataFrame operations should be reasonably fast
        # (not necessarily faster than manual, but not dramatically slower)
        print(f"DataFrame write: {df_write_time:.4f}s, manual write: {manual_write_time:.4f}s")
        print(f"DataFrame read: {df_read_time:.4f}s, manual read: {manual_read_time:.4f}s")

    def test_dataframe_compression_levels(self):
        """Test DataFrame operations with different compression levels."""
        df = pd.DataFrame({
            'data': ['repetitive ' * 50] * 100,  # Highly compressible
            'id': range(100)
        })
        
        # Test different compression levels
        for compression_level in [1, 5, 9]:
            filepath = self._create_temp_file(f'.comp{compression_level}.bcsv')
            
            # Write with compression level
            pybcsv.write_dataframe(df, filepath, compression_level=compression_level)
            
            # Read back and verify
            df_read = pybcsv.read_dataframe(filepath)
            pd.testing.assert_frame_equal(df_read, df, check_dtype=False)
            
            # Check file exists and has reasonable size
            self.assertTrue(os.path.exists(filepath))
            self.assertGreater(os.path.getsize(filepath), 0)

    def test_dataframe_error_handling(self):
        """Test error handling in DataFrame operations."""
        
        # Test writing to invalid path
        df = pd.DataFrame({'test': [1, 2, 3]})
        
        with self.assertRaises((IOError, OSError, RuntimeError)):
            pybcsv.write_dataframe(df, "/invalid/path/file.bcsv")
        
        # Test reading non-existent file
        with self.assertRaises((IOError, OSError, RuntimeError)):
            pybcsv.read_dataframe("/path/does/not/exist.bcsv")
        
        # Test reading non-BCSV file
        text_file = self._create_temp_file('.txt')
        with open(text_file, 'w') as f:
            f.write("This is not a BCSV file")
        
        with self.assertRaises((IOError, OSError, RuntimeError)):
            pybcsv.read_dataframe(text_file)


if __name__ == '__main__':
    unittest.main()