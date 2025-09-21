# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

import unittest
import os
import tempfile
import pybcsv

class TestDataTypes(unittest.TestCase):
    """Test various data type handling in BCSV."""
    
    def setUp(self):
        """Set up test fixtures."""
        self.temp_dir = tempfile.mkdtemp()
        self.test_file = os.path.join(self.temp_dir, "test.bcsv")
    
    def tearDown(self):
        """Clean up test fixtures."""
        if os.path.exists(self.test_file):
            os.remove(self.test_file)
        os.rmdir(self.temp_dir)
    
    def test_integer_types(self):
        """Test all integer types."""
        layout = pybcsv.Layout()
        layout.add_column("int8_col", pybcsv.ColumnType.INT8)
        layout.add_column("int16_col", pybcsv.ColumnType.INT16)
        layout.add_column("int32_col", pybcsv.ColumnType.INT32)
        layout.add_column("int64_col", pybcsv.ColumnType.INT64)
        layout.add_column("uint8_col", pybcsv.ColumnType.UINT8)
        layout.add_column("uint16_col", pybcsv.ColumnType.UINT16)
        layout.add_column("uint32_col", pybcsv.ColumnType.UINT32)
        layout.add_column("uint64_col", pybcsv.ColumnType.UINT64)
        
        test_data = [
            [-128, -32768, -2147483648, -9223372036854775808,
             0, 0, 0, 0],
            [127, 32767, 2147483647, 9223372036854775807,
             255, 65535, 4294967295, 18446744073709551615],
            [0, 0, 0, 0, 128, 32768, 2147483648, 9223372036854775808]
        ]
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for row in test_data:
                writer.write_row(row)
        
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            read_data = reader.read_all()
        
        self.assertEqual(len(read_data), len(test_data))
        for original, read_back in zip(test_data, read_data):
            self.assertEqual(original, read_back)
    
    def test_float_types(self):
        """Test float and double types."""
        layout = pybcsv.Layout()
        layout.add_column("float_col", pybcsv.ColumnType.FLOAT)
        layout.add_column("double_col", pybcsv.ColumnType.DOUBLE)
        
        test_data = [
            [1.5, 1.5],
            [-3.14159, -3.141592653589793],
            [0.0, 0.0],
            [float('inf'), float('inf')],
            [float('-inf'), float('-inf')],
        ]
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for row in test_data:
                writer.write_row(row)
        
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            read_data = reader.read_all()
        
        self.assertEqual(len(read_data), len(test_data))
        for i, (original, read_back) in enumerate(zip(test_data, read_data)):
            # For regular numbers, check equality
            if i < 3:
                self.assertAlmostEqual(original[0], read_back[0], places=6)
                self.assertAlmostEqual(original[1], read_back[1], places=15)
            # For infinity values, check special cases
            else:
                self.assertEqual(str(original[0]), str(read_back[0]))
                self.assertEqual(str(original[1]), str(read_back[1]))
    
    def test_boolean_type(self):
        """Test boolean type."""
        layout = pybcsv.Layout()
        layout.add_column("bool_col", pybcsv.ColumnType.BOOL)
        
        test_data = [
            [True],
            [False],
            [True],
            [False]
        ]
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for row in test_data:
                writer.write_row(row)
        
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            read_data = reader.read_all()
        
        self.assertEqual(len(read_data), len(test_data))
        for original, read_back in zip(test_data, read_data):
            self.assertEqual(original, read_back)
    
    def test_string_type(self):
        """Test string type with various content."""
        layout = pybcsv.Layout()
        layout.add_column("string_col", pybcsv.ColumnType.STRING)
        
        test_data = [
            ["Hello, World!"],
            [""],  # Empty string
            ["Line1\nLine2"],  # Newlines
            ["Tabs\t\there"],  # Tabs
            ["Unicode: 你好世界 🌍"],  # Unicode
            ["Special chars: !@#$%^&*()"],
            ["Very long string " * 100],  # Long string
        ]
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for row in test_data:
                writer.write_row(row)
        
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            read_data = reader.read_all()
        
        self.assertEqual(len(read_data), len(test_data))
        for original, read_back in zip(test_data, read_data):
            self.assertEqual(original, read_back)
    
    def test_mixed_types(self):
        """Test a layout with mixed data types."""
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.ColumnType.INT32)
        layout.add_column("name", pybcsv.ColumnType.STRING)
        layout.add_column("score", pybcsv.ColumnType.DOUBLE)
        layout.add_column("active", pybcsv.ColumnType.BOOL)
        layout.add_column("count", pybcsv.ColumnType.UINT16)
        layout.add_column("rating", pybcsv.ColumnType.FLOAT)
        
        test_data = [
            [1, "Alice", 95.5, True, 100, 4.5],
            [2, "Bob", 87.25, False, 50, 3.8],
            [3, "Charlie", 92.125, True, 75, 4.2],
            [-1, "", 0.0, False, 0, 0.0],  # Edge cases
            [2147483647, "Very long name " * 10, 
             999999.999999, True, 65535, 5.0],  # Max values
        ]
        
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for row in test_data:
                writer.write_row(row)
        
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            read_data = reader.read_all()
        
        self.assertEqual(len(read_data), len(test_data))
        for i, (original, read_back) in enumerate(zip(test_data, read_data)):
            self.assertEqual(len(original), len(read_back))
            # Check each field
            self.assertEqual(original[0], read_back[0])  # id
            self.assertEqual(original[1], read_back[1])  # name
            self.assertAlmostEqual(original[2], read_back[2], places=10)  # score
            self.assertEqual(original[3], read_back[3])  # active
            self.assertEqual(original[4], read_back[4])  # count
            self.assertAlmostEqual(original[5], read_back[5], places=6)  # rating
    
    def test_type_to_string(self):
        """Test the type_to_string utility function."""
        type_mappings = {
            pybcsv.ColumnType.FLOAT: "float",
            pybcsv.ColumnType.DOUBLE: "double",
            pybcsv.ColumnType.INT8: "int8",
            pybcsv.ColumnType.INT16: "int16",
            pybcsv.ColumnType.INT32: "int32",
            pybcsv.ColumnType.INT64: "int64",
            pybcsv.ColumnType.UINT8: "uint8",
            pybcsv.ColumnType.UINT16: "uint16",
            pybcsv.ColumnType.UINT32: "uint32",
            pybcsv.ColumnType.UINT64: "uint64",
            pybcsv.ColumnType.BOOL: "bool",
            pybcsv.ColumnType.STRING: "string",
        }
        
        for col_type, expected_string in type_mappings.items():
            result = pybcsv.type_to_string(col_type)
            self.assertEqual(result, expected_string)

if __name__ == '__main__':
    unittest.main()