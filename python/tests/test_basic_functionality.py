#!/usr/bin/env python3
"""
Basic functionality unit tests for PyBCSV.
Tests core read/write operations, layout creation, and data integrity.
"""

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

import unittest
import tempfile
import os
import numpy as np
import pybcsv
from typing import List, Any


class TestBasicFunctionality(unittest.TestCase):
    """Test basic BCSV functionality and API."""

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
        os.close(fd)  # Close the file descriptor
        self.temp_files.append(filepath)
        return filepath

    def test_layout_creation(self):
        """Test layout creation with different column types."""
        layout = pybcsv.Layout()
        
        # Add various column types
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
        
        # Verify layout properties
        self.assertEqual(layout.column_count(), 12)
        
        # Check column names and types
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

    def test_simple_write_read(self):
        """Test basic write and read operations."""
        filepath = self._create_temp_file()
        
        # Create layout
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        layout.add_column("value", pybcsv.DOUBLE)
        
        # Test data
        test_data = [
            [1, "Alice", 123.45],
            [2, "Bob", 678.90],
            [3, "Charlie", 111.22],
            [4, "Diana", 999.99]
        ]
        
        # Write data
        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(filepath))
        
        for row in test_data:
            writer.write_row(row)
        
        writer.close()
        
        # Verify file exists and has content
        self.assertTrue(os.path.exists(filepath))
        self.assertGreater(os.path.getsize(filepath), 0)
        
        # Read data back
        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        
        read_data = reader.read_all()
        reader.close()
        
        # Verify data integrity
        self.assertEqual(len(read_data), len(test_data))
        for i, (original, read) in enumerate(zip(test_data, read_data)):
            self.assertEqual(original, read, f"Row {i} mismatch")

    def test_all_column_types(self):
        """Test reading and writing all supported column types."""
        filepath = self._create_temp_file()
        
        # Create layout with all types
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
        
        # Test data with boundary values
        test_data = [
            [True, -128, -32768, -2147483648, -9223372036854775808, 
             0, 0, 0, 0, 1.5, 2.5, "hello"],
            [False, 127, 32767, 2147483647, 9223372036854775807,
             255, 65535, 4294967295, 18446744073709551615, -1.5, -2.5, "world"],
            [True, 0, 0, 0, 0, 128, 32768, 2147483648, 9223372036854775808,
             0.0, 0.0, ""],
            [False, -1, -1, -1, -1, 1, 1, 1, 1, 
             float('inf'), float('-inf'), "unicode: 🚀 测试"]
        ]
        
        # Write data
        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(filepath))
        
        for row in test_data:
            writer.write_row(row)
        
        writer.close()
        
        # Read data back
        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        
        read_data = reader.read_all()
        reader.close()
        
        # Verify data (with special handling for inf values)
        self.assertEqual(len(read_data), len(test_data))
        for i, (original, read) in enumerate(zip(test_data, read_data)):
            for j, (orig_val, read_val) in enumerate(zip(original, read)):
                if isinstance(orig_val, float) and np.isinf(orig_val):
                    self.assertTrue(np.isinf(read_val), f"Row {i}, col {j}: expected inf, got {read_val}")
                    self.assertEqual(np.sign(orig_val), np.sign(read_val), f"Row {i}, col {j}: inf sign mismatch")
                else:
                    self.assertEqual(orig_val, read_val, f"Row {i}, col {j}: {orig_val} != {read_val}")

    def test_batch_operations(self):
        """Test batch write operations."""
        filepath = self._create_temp_file()
        
        # Create layout
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("value", pybcsv.DOUBLE)
        
        # Generate test data
        batch_size = 100
        test_data = [[i, i * 1.5] for i in range(batch_size)]
        
        # Write using batch operation
        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(filepath))
        
        writer.write_rows(test_data)
        writer.close()
        
        # Read data back
        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        
        read_data = reader.read_all()
        reader.close()
        
        # Verify data
        self.assertEqual(len(read_data), batch_size)
        for i, (original, read) in enumerate(zip(test_data, read_data)):
            self.assertEqual(original, read, f"Row {i} mismatch")

    def test_context_managers(self):
        """Test that writers and readers work as context managers."""
        filepath = self._create_temp_file()
        
        # Create layout
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.STRING)
        
        test_data = [["hello"], ["world"]]
        
        # Test writer context manager
        with pybcsv.Writer(layout) as writer:
            writer.open(filepath)
            for row in test_data:
                writer.write_row(row)
        
        # Test reader context manager
        with pybcsv.Reader() as reader:
            reader.open(filepath)
            read_data = reader.read_all()
        
        # Verify data
        self.assertEqual(read_data, test_data)

    def test_file_operations(self):
        """Test file opening, closing, and status checking."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        # Test writer file operations
        writer = pybcsv.Writer(layout)
        self.assertFalse(writer.is_open())
        
        self.assertTrue(writer.open(filepath))
        self.assertTrue(writer.is_open())
        
        writer.write_row([42])
        writer.flush()  # Test flush
        
        writer.close()
        self.assertFalse(writer.is_open())
        
        # Test reader file operations
        reader = pybcsv.Reader()
        self.assertFalse(reader.is_open())
        
        self.assertTrue(reader.open(filepath))
        self.assertTrue(reader.is_open())
        
        data = reader.read_all()
        self.assertEqual(data, [[42]])
        
        reader.close()
        self.assertFalse(reader.is_open())

    def test_empty_file(self):
        """Test handling of empty files (no rows)."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        # Write empty file
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.close()
        
        # Read empty file
        reader = pybcsv.Reader()
        reader.open(filepath)
        data = reader.read_all()
        reader.close()
        
        self.assertEqual(data, [])

    def test_large_strings(self):
        """Test handling of large strings with size limits."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("large_string", pybcsv.STRING)
        
        # Test with maximum string size that fits in a row (65527 bytes + 8 byte address = 65535 total)
        max_string = "x" * 65527  # Max that fits within BCSV row size limit
        test_data = [[max_string]]
        
        # Write and read
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row(test_data[0])
        writer.close()
        
        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        
        self.assertEqual(read_data, test_data)
        self.assertEqual(len(read_data[0][0]), 65527)
        
        # Test string that would exceed BCSV row size limit (should be rejected)
        with self.assertRaises(RuntimeError) as context:
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            oversized_string = "x" * 65534  # Would result in 65542 byte row (too large)
            writer.write_row([oversized_string])
            writer.close()
        
        self.assertIn("Total row size too large", str(context.exception))
        
        # Test string that exceeds BCSV string format limit (should be rejected)
        with self.assertRaises(RuntimeError) as context:
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            huge_string = "x" * 100000  # Exceeds 65534 byte string limit
            writer.write_row([huge_string])
            writer.close()
        
        self.assertIn("exceeds BCSV format limit", str(context.exception))
        
        # Test that excessively large strings are handled gracefully by our Python bindings
        with self.assertRaises(RuntimeError) as context:
            writer = pybcsv.Writer(layout)
            writer.open(filepath)
            # Try to write a 200MB string (should fail at Python level)
            huge_string = "x" * (200 * 1024 * 1024)
            writer.write_row([huge_string])
            writer.close()
        
        self.assertIn("too large", str(context.exception))

    def test_compression_levels(self):
        """Test different compression levels."""
        filepath_low = self._create_temp_file('.low.bcsv')
        filepath_high = self._create_temp_file('.high.bcsv')
        
        layout = pybcsv.Layout()
        layout.add_column("data", pybcsv.STRING)
        
        # Create repetitive data (compresses well)
        test_data = [["repetitive data " * 100] for _ in range(50)]
        
        # Write with low compression
        writer_low = pybcsv.Writer(layout)
        writer_low.open(filepath_low, compression_level=1)
        writer_low.write_rows(test_data)
        writer_low.close()
        
        # Write with high compression
        writer_high = pybcsv.Writer(layout)
        writer_high.open(filepath_high, compression_level=9)
        writer_high.write_rows(test_data)
        writer_high.close()
        
        # Check file sizes (high compression should be smaller)
        size_low = os.path.getsize(filepath_low)
        size_high = os.path.getsize(filepath_high)
        
        self.assertGreater(size_low, 0)
        self.assertGreater(size_high, 0)
        self.assertLessEqual(size_high, size_low)  # High compression should be smaller or equal
        
        # Verify both files read correctly
        for filepath in [filepath_low, filepath_high]:
            reader = pybcsv.Reader()
            reader.open(filepath)
            read_data = reader.read_all()
            reader.close()
            self.assertEqual(read_data, test_data)


if __name__ == '__main__':
    unittest.main()