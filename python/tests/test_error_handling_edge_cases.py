#!/usr/bin/env python3
"""
Error handling and edge case tests for PyBCSV.
Tests file not found, corrupt files, layout mismatches, wrong types, wrong data sizes, etc.
"""

import unittest
import tempfile
import os
import pybcsv
from typing import List, Any


class TestErrorHandling(unittest.TestCase):
    """Test error handling and validation in PyBCSV."""

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

    def _create_valid_file(self) -> str:
        """Create a valid BCSV file for testing."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([1, "test"])
        writer.close()
        
        return filepath

    def test_file_not_found_reader(self):
        """Test reader behavior with non-existent files."""
        non_existent_file = "/path/that/does/not/exist/file.bcsv"
        
        reader = pybcsv.Reader()
        
        # Should return False when trying to open non-existent file
        self.assertFalse(reader.open(non_existent_file))
        self.assertFalse(reader.is_open())

    def test_file_not_found_writer(self):
        """Test writer behavior with invalid paths."""
        # Try to write to a directory that doesn't exist
        invalid_path = "/path/that/does/not/exist/file.bcsv"
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        writer = pybcsv.Writer(layout)
        
        # Should return False when trying to open invalid path
        self.assertFalse(writer.open(invalid_path))
        self.assertFalse(writer.is_open())

    def test_permission_denied(self):
        """Test handling of permission denied errors."""
        # Try to write to root directory (should fail with permission denied)
        if os.name != 'posix':
            self.skipTest("Permission test only valid on POSIX systems")
        
        restricted_path = "/root/test.bcsv"
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        writer = pybcsv.Writer(layout)
        
        # Should return False due to permission denied
        self.assertFalse(writer.open(restricted_path))

    def test_corrupted_file_header(self):
        """Test reading files with corrupted headers."""
        filepath = self._create_temp_file()
        
        # Create a file with invalid content
        with open(filepath, 'wb') as f:
            f.write(b"This is not a valid BCSV file header")
        
        reader = pybcsv.Reader()
        
        # Should fail to open corrupted file
        self.assertFalse(reader.open(filepath))

    def test_truncated_file(self):
        """Test reading truncated files."""
        # Create a valid file first
        valid_filepath = self._create_valid_file()
        
        # Read the valid file content
        with open(valid_filepath, 'rb') as f:
            valid_content = f.read()
        
        # Create truncated version
        truncated_filepath = self._create_temp_file()
        with open(truncated_filepath, 'wb') as f:
            # Write only half the content
            f.write(valid_content[:len(valid_content)//2])
        
        reader = pybcsv.Reader()
        
        # May open successfully but fail during reading
        if reader.open(truncated_filepath):
            # Should handle truncated data gracefully
            try:
                data = reader.read_all()
                # If it succeeds, data should be empty or partial
                self.assertIsInstance(data, list)
            except Exception:
                # It's acceptable to throw an exception for corrupted data
                pass
            finally:
                reader.close()

    def test_row_length_mismatch(self):
        """Test writing rows with wrong number of columns."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("col1", pybcsv.INT32)
        layout.add_column("col2", pybcsv.STRING)
        layout.add_column("col3", pybcsv.DOUBLE)
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        # Test row with too few columns
        with self.assertRaises((RuntimeError, ValueError)):
            writer.write_row([1, "test"])  # Missing third column
        
        # Test row with too many columns
        with self.assertRaises((RuntimeError, ValueError)):
            writer.write_row([1, "test", 3.14, "extra"])  # Extra column
        
        # Test empty row
        with self.assertRaises((RuntimeError, ValueError)):
            writer.write_row([])
        
        writer.close()

    def test_wrong_data_types(self):
        """Test writing data with incorrect types."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("int_col", pybcsv.INT32)
        layout.add_column("string_col", pybcsv.STRING)
        layout.add_column("double_col", pybcsv.DOUBLE)
        layout.add_column("bool_col", pybcsv.BOOL)
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        # Test string where int expected
        with self.assertRaises((TypeError, ValueError, RuntimeError)):
            writer.write_row(["not_a_number", "valid_string", 3.14, True])
        
        # Test None values (should fail)
        with self.assertRaises((TypeError, ValueError, RuntimeError)):
            writer.write_row([None, "valid_string", 3.14, True])
        
        # Test complex object where simple type expected
        with self.assertRaises((TypeError, ValueError, RuntimeError)):
            writer.write_row([{"complex": "object"}, "valid_string", 3.14, True])
        
        writer.close()

    def test_numeric_overflow(self):
        """Test numeric values that exceed type limits."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("int8_col", pybcsv.INT8)
        layout.add_column("uint8_col", pybcsv.UINT8)
        layout.add_column("int32_col", pybcsv.INT32)
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        # Test INT8 overflow (valid range: -128 to 127)
        with self.assertRaises((OverflowError, ValueError, RuntimeError)):
            writer.write_row([128, 0, 0])  # 128 > 127
        
        with self.assertRaises((OverflowError, ValueError, RuntimeError)):
            writer.write_row([-129, 0, 0])  # -129 < -128
        
        # Test UINT8 overflow (valid range: 0 to 255)
        with self.assertRaises((OverflowError, ValueError, RuntimeError)):
            writer.write_row([0, 256, 0])  # 256 > 255
        
        with self.assertRaises((OverflowError, ValueError, RuntimeError)):
            writer.write_row([0, -1, 0])  # -1 < 0 for unsigned
        
        # Test very large number for INT32
        with self.assertRaises((OverflowError, ValueError, RuntimeError)):
            writer.write_row([0, 0, 2**32])  # Larger than INT32_MAX
        
        writer.close()

    def test_invalid_layout(self):
        """Test operations with invalid layouts."""
        # Test empty layout
        empty_layout = pybcsv.Layout()
        
        filepath = self._create_temp_file()
        writer = pybcsv.Writer(empty_layout)
        
        # Should handle empty layout gracefully
        self.assertTrue(writer.open(filepath))
        
        # Writing to empty layout should work (empty rows)
        writer.write_row([])
        writer.close()

    def test_batch_operation_errors(self):
        """Test error handling in batch operations."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        # Test batch with inconsistent row lengths
        invalid_batch = [
            [1, "valid"],
            [2],  # Missing column
            [3, "valid", "extra"]  # Extra column
        ]
        
        # Should detect the error and report which row is problematic
        with self.assertRaises((RuntimeError, ValueError)) as context:
            writer.write_rows(invalid_batch)
        
        # Error message should indicate which row failed
        error_message = str(context.exception).lower()
        self.assertTrue("row" in error_message or "length" in error_message or "mismatch" in error_message)
        
        writer.close()

    def test_file_operations_when_closed(self):
        """Test operations on closed files."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        # Test writer operations when closed
        writer = pybcsv.Writer(layout)
        
        # Operations on unopened writer should fail gracefully
        with self.assertRaises((RuntimeError, ValueError)):
            writer.write_row([1])
        
        with self.assertRaises((RuntimeError, ValueError)):
            writer.write_rows([[1], [2]])
        
        # Test reader operations when closed
        reader = pybcsv.Reader()
        
        # Operations on unopened reader should fail gracefully
        with self.assertRaises((RuntimeError, ValueError, AttributeError)):
            reader.read_all()

    def test_double_close(self):
        """Test closing files multiple times."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        # Test writer double close
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([1])
        
        writer.close()
        self.assertFalse(writer.is_open())
        
        # Second close should be safe
        writer.close()  # Should not throw
        self.assertFalse(writer.is_open())
        
        # Test reader double close
        reader = pybcsv.Reader()
        reader.open(filepath)
        data = reader.read_all()
        
        reader.close()
        self.assertFalse(reader.is_open())
        
        # Second close should be safe
        reader.close()  # Should not throw
        self.assertFalse(reader.is_open())

    def test_concurrent_file_access(self):
        """Test concurrent access to the same file."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        # Create a file first
        writer1 = pybcsv.Writer(layout)
        writer1.open(filepath)
        writer1.write_row([1])
        writer1.close()
        
        # Test multiple readers (should be allowed)
        reader1 = pybcsv.Reader()
        reader2 = pybcsv.Reader()
        
        self.assertTrue(reader1.open(filepath))
        self.assertTrue(reader2.open(filepath))
        
        data1 = reader1.read_all()
        data2 = reader2.read_all()
        
        self.assertEqual(data1, data2)
        
        reader1.close()
        reader2.close()

    def test_large_string_errors(self):
        """Test handling of extremely large strings."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("large_string", pybcsv.STRING)
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        # Test extremely large string (may succeed or fail gracefully)
        try:
            # Create a very large string (100MB)
            huge_string = "x" * (100 * 1024 * 1024)
            writer.write_row([huge_string])
            
            # If it succeeds, verify we can read it back
            writer.close()
            
            reader = pybcsv.Reader()
            reader.open(filepath)
            data = reader.read_all()
            reader.close()
            
            self.assertEqual(len(data[0][0]), 100 * 1024 * 1024)
            
        except (MemoryError, OverflowError, RuntimeError):
            # It's acceptable to fail with very large strings
            pass
        finally:
            if writer.is_open():
                writer.close()

    def test_invalid_compression_level(self):
        """Test invalid compression levels."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        
        writer = pybcsv.Writer(layout)
        
        # Test invalid compression levels (should either clamp or fail gracefully)
        try:
            # Very high compression level
            writer.open(filepath, compression_level=100)
            writer.write_row([1])
            writer.close()
        except (ValueError, RuntimeError):
            # It's acceptable to reject invalid compression levels
            pass
        
        try:
            # Negative compression level
            writer.open(filepath, compression_level=-1)
            writer.write_row([1])
            writer.close()
        except (ValueError, RuntimeError):
            # It's acceptable to reject invalid compression levels
            pass


if __name__ == '__main__':
    unittest.main()