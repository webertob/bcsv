# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

import unittest
import os
import tempfile
import pybcsv

class TestErrorHandling(unittest.TestCase):
    """Test error handling and edge cases."""
    
    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.mkdtemp()
        self.test_file = os.path.join(self.temp_dir, "test.bcsv")
    
    def tearDown(self):
        """Clean up test fixtures."""
        if os.path.exists(self.test_file):
            os.remove(self.test_file)
        os.rmdir(self.temp_dir)
    
    def test_invalid_file_read(self):
        """Test reading from non-existent file."""
        non_existent_file = os.path.join(self.temp_dir, "does_not_exist.bcsv")
        
        with self.assertRaises(Exception):  # Should raise some kind of exception
            reader = pybcsv.Reader()
            reader.open(non_existent_file)
    
    def test_invalid_file_write(self):
        """Test writing to invalid path."""
        invalid_path = "/invalid/path/that/does/not/exist/test.bcsv"
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.ColumnType.INT32)
        
        with self.assertRaises(Exception):  # Should raise some kind of exception
            pybcsv.Writer(invalid_path, layout)
    
    def test_empty_layout(self):
        """Test operations with empty layout."""
        empty_layout = pybcsv.Layout()
        
        # Should be able to create writer with empty layout
        with pybcsv.Writer(empty_layout) as writer:
            writer.open(self.test_file, compression_level=0)
            # But writing rows should be okay (empty rows)
            writer.write_row([])
        
        # Should be able to read back
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            layout = reader.get_layout()
            self.assertEqual(layout.get_column_count(), 0)
            
            data = reader.read_all()
            # Empty layout with no columns: write_row([]) writes a valid zero-column row
            # The row is stored and can be read back (1 empty row)
            self.assertEqual(len(data), 1)
    
    def test_row_length_mismatch(self):
        """Test writing rows with wrong number of columns."""
        layout = pybcsv.Layout()
        layout.add_column("col1", pybcsv.ColumnType.INT32)
        layout.add_column("col2", pybcsv.ColumnType.STRING)
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            # This should work
            writer.write_row([1, "test"])
            
            # These might raise exceptions or handle gracefully
            try:
                writer.write_row([1])  # Too few columns
            except Exception:
                pass  # Expected
                
            try:
                writer.write_row([1, "test", "extra"])  # Too many columns
            except Exception:
                pass  # Expected
    
    def test_wrong_data_types(self):
        """Test writing wrong data types."""
        layout = pybcsv.Layout()
        layout.add_column("int_col", pybcsv.ColumnType.INT32)
        layout.add_column("bool_col", pybcsv.ColumnType.BOOL)
        layout.add_column("float_col", pybcsv.ColumnType.DOUBLE)
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            # These should work (automatic conversion)
            writer.write_row([1, True, 1.5])
            writer.write_row([1.0, False, 2])  # float to int, int to float
            writer.write_row(["1", "True", "3.14"])  # strings that can be converted
            
            # These might cause issues
            try:
                writer.write_row(["not_a_number", True, 1.5])
            except Exception:
                pass  # Expected for invalid string to number conversion
    
    def test_file_permissions(self):
        """Test handling of file permission issues."""
        if os.name != 'posix':
            self.skipTest("File permission test only on POSIX systems")
        
        # Create a read-only directory
        readonly_dir = os.path.join(self.temp_dir, "readonly")
        os.makedirs(readonly_dir)
        os.chmod(readonly_dir, 0o444)  # Read-only
        
        readonly_file = os.path.join(readonly_dir, "test.bcsv")
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.ColumnType.INT32)
        
        try:
            with self.assertRaises(Exception):
                pybcsv.Writer(readonly_file, layout)
        finally:
            # Clean up - restore permissions
            os.chmod(readonly_dir, 0o755)
            os.rmdir(readonly_dir)
    
    def test_context_manager_exceptions(self):
        """Test that context managers handle exceptions properly."""
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.ColumnType.INT32)
        
        # Test writer context manager with exception
        try:
            with pybcsv.Writer(layout) as writer:
                writer.open(self.test_file, compression_level=0)
                writer.write_row([1])
                raise ValueError("Test exception")
        except ValueError:
            pass  # Expected
        
        # File should still be created and readable
        self.assertTrue(os.path.exists(self.test_file))
        
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            data = reader.read_all()
            self.assertEqual(len(data), 1)
            self.assertEqual(data[0], [1])
    
    def test_reader_iteration_after_end(self):
        """Test reader behavior after reaching end of file."""
        layout = pybcsv.Layout()
        layout.add_column("value", pybcsv.ColumnType.INT32)
        
        # Write test data
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for i in range(3):
                writer.write_row([i])
        
        # Read and exhaust the iterator
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            rows = list(reader)  # This should read all rows
            self.assertEqual(len(rows), 3)
            
            # Try to read more - should return None
            next_row = reader.read_row()
            self.assertIsNone(next_row)
    
    def test_multiple_readers(self):
        """Test multiple simultaneous readers on the same file."""
        layout = pybcsv.Layout()
        layout.add_column("value", pybcsv.ColumnType.INT32)
        
        # Write test data
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for i in range(5):
                writer.write_row([i])
        
        # Open multiple readers
        with pybcsv.Reader() as reader1, \
             pybcsv.Reader() as reader2:
            
            reader1.open(self.test_file)
            reader2.open(self.test_file)
            
            # Both should be able to read independently
            data1 = reader1.read_all()
            data2 = reader2.read_all()
            
            self.assertEqual(data1, data2)
            self.assertEqual(len(data1), 5)

if __name__ == '__main__':
    unittest.main()