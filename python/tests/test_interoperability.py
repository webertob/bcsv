#!/usr/bin/env python3
"""
Interoperability tests between Python and C++ BCSV implementations.
Tests that Python can read C++ generated files and vice versa.
"""

import unittest
import tempfile
import os
import subprocess
import sys
import pybcsv
from typing import List, Any


class TestInteroperability(unittest.TestCase):
    """Test interoperability between Python and C++ BCSV implementations."""

    def setUp(self):
        """Set up test fixtures."""
        self.temp_files = []
        
        # Check if C++ examples are built
        self.cpp_examples_dir = os.path.join(os.path.dirname(__file__), '../../build/examples')
        self.bcsv2csv_exe = os.path.join(self.cpp_examples_dir, 'bcsv2csv')
        self.csv2bcsv_exe = os.path.join(self.cpp_examples_dir, 'csv2bcsv')
        
        # Check if executables exist
        self.cpp_available = (
            os.path.exists(self.bcsv2csv_exe) and 
            os.path.exists(self.csv2bcsv_exe)
        )
        
        if not self.cpp_available:
            print(f"Warning: C++ examples not found at {self.cpp_examples_dir}")
            print("Skipping C++ interoperability tests")

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

    def test_python_write_cpp_read(self):
        """Test that C++ can read files written by Python."""
        if not self.cpp_available:
            self.skipTest("C++ examples not available")
        
        bcsv_file = self._create_temp_file('.bcsv')
        csv_file = self._create_temp_file('.csv')
        
        # Create test data with Python
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        layout.add_column("value", pybcsv.DOUBLE)
        
        test_data = [
            [1, "Alice", 123.45],
            [2, "Bob", 678.90],
            [3, "Charlie", 111.22]
        ]
        
        # Write with Python
        writer = pybcsv.Writer(layout)
        self.assertTrue(writer.open(bcsv_file))
        writer.write_rows(test_data)
        writer.close()
        
        # Read with C++ (convert to CSV)
        result = subprocess.run(
            [self.bcsv2csv_exe, bcsv_file, csv_file],
            capture_output=True,
            text=True
        )
        
        self.assertEqual(result.returncode, 0, f"C++ reader failed: {result.stderr}")
        self.assertTrue(os.path.exists(csv_file))
        
        # Verify CSV content
        with open(csv_file, 'r') as f:
            csv_content = f.read().strip()
        
        expected_lines = [
            "id,name,value",  # Header
            "1,Alice,123.45",
            "2,Bob,678.9",
            "3,Charlie,111.22"
        ]
        
        csv_lines = csv_content.split('\n')
        self.assertEqual(len(csv_lines), len(expected_lines))
        
        # Check header
        self.assertEqual(csv_lines[0], expected_lines[0])
        
        # Check data rows (allowing for floating point precision differences)
        for i, (expected, actual) in enumerate(zip(expected_lines[1:], csv_lines[1:]), 1):
            expected_parts = expected.split(',')
            actual_parts = actual.split(',')
            
            self.assertEqual(len(actual_parts), 3, f"Row {i} has wrong number of columns")
            self.assertEqual(actual_parts[0], expected_parts[0], f"Row {i} ID mismatch")
            self.assertEqual(actual_parts[1], expected_parts[1], f"Row {i} name mismatch")
            
            # Allow floating point precision differences
            expected_value = float(expected_parts[2])
            actual_value = float(actual_parts[2])
            self.assertAlmostEqual(actual_value, expected_value, places=5, 
                                 msg=f"Row {i} value mismatch")

    def test_cpp_write_python_read(self):
        """Test that Python can read files written by C++."""
        if not self.cpp_available:
            self.skipTest("C++ examples not available")
        
        csv_file = self._create_temp_file('.csv')
        bcsv_file = self._create_temp_file('.bcsv')
        
        # Create CSV file for C++ to convert
        csv_content = """id,name,score,active
1,Alice,95.5,true
2,Bob,87.2,false
3,Charlie,92.1,true
4,Diana,98.7,false"""
        
        with open(csv_file, 'w') as f:
            f.write(csv_content)
        
        # Convert CSV to BCSV with C++
        result = subprocess.run(
            [self.csv2bcsv_exe, csv_file, bcsv_file],
            capture_output=True,
            text=True
        )
        
        self.assertEqual(result.returncode, 0, f"C++ writer failed: {result.stderr}")
        self.assertTrue(os.path.exists(bcsv_file))
        
        # Read with Python
        reader = pybcsv.Reader()
        self.assertTrue(reader.open(bcsv_file))
        
        # Check layout
        layout = reader.layout()
        self.assertEqual(layout.column_count(), 4)
        
        expected_columns = ["id", "name", "score", "active"]
        for i, expected_name in enumerate(expected_columns):
            self.assertEqual(layout.column_name(i), expected_name)
        
        # Read data
        data = reader.read_all()
        reader.close()
        
        # Verify data
        expected_data = [
            [1, "Alice", 95.5, True],
            [2, "Bob", 87.2, False],
            [3, "Charlie", 92.1, True],
            [4, "Diana", 98.7, False]
        ]
        
        self.assertEqual(len(data), len(expected_data))
        for i, (expected_row, actual_row) in enumerate(zip(expected_data, data)):
            self.assertEqual(len(actual_row), len(expected_row), f"Row {i} length mismatch")
            for j, (expected_val, actual_val) in enumerate(zip(expected_row, actual_row)):
                if isinstance(expected_val, float):
                    self.assertAlmostEqual(actual_val, expected_val, places=5,
                                         msg=f"Row {i}, col {j} value mismatch")
                else:
                    self.assertEqual(actual_val, expected_val,
                                   f"Row {i}, col {j}: {actual_val} != {expected_val}")

    def test_roundtrip_compatibility(self):
        """Test complete roundtrip: Python -> C++ -> Python."""
        if not self.cpp_available:
            self.skipTest("C++ examples not available")
        
        original_bcsv = self._create_temp_file('.original.bcsv')
        csv_file = self._create_temp_file('.csv')
        roundtrip_bcsv = self._create_temp_file('.roundtrip.bcsv')
        
        # Create original data with Python
        layout = pybcsv.Layout()
        layout.add_column("int_val", pybcsv.INT32)
        layout.add_column("str_val", pybcsv.STRING)
        layout.add_column("float_val", pybcsv.DOUBLE)
        layout.add_column("bool_val", pybcsv.BOOL)
        
        original_data = [
            [1, "test1", 1.1, True],
            [2, "test2", 2.2, False],
            [3, "test3", 3.3, True],
            [-1, "negative", -4.4, False],
            [0, "", 0.0, True]
        ]
        
        # Step 1: Write with Python
        writer = pybcsv.Writer(layout)
        writer.open(original_bcsv)
        writer.write_rows(original_data)
        writer.close()
        
        # Step 2: Convert to CSV with C++
        result = subprocess.run(
            [self.bcsv2csv_exe, original_bcsv, csv_file],
            capture_output=True,
            text=True
        )
        self.assertEqual(result.returncode, 0, f"BCSV to CSV failed: {result.stderr}")
        
        # Step 3: Convert back to BCSV with C++
        result = subprocess.run(
            [self.csv2bcsv_exe, csv_file, roundtrip_bcsv],
            capture_output=True,
            text=True
        )
        self.assertEqual(result.returncode, 0, f"CSV to BCSV failed: {result.stderr}")
        
        # Step 4: Read with Python
        reader = pybcsv.Reader()
        reader.open(roundtrip_bcsv)
        roundtrip_data = reader.read_all()
        reader.close()
        
        # Verify data integrity
        self.assertEqual(len(roundtrip_data), len(original_data))
        for i, (original_row, roundtrip_row) in enumerate(zip(original_data, roundtrip_data)):
            self.assertEqual(len(roundtrip_row), len(original_row), f"Row {i} length mismatch")
            for j, (orig_val, round_val) in enumerate(zip(original_row, roundtrip_row)):
                if isinstance(orig_val, float):
                    self.assertAlmostEqual(round_val, orig_val, places=5,
                                         msg=f"Row {i}, col {j} float value mismatch")
                else:
                    self.assertEqual(round_val, orig_val,
                                   f"Row {i}, col {j}: {round_val} != {orig_val}")

    def test_large_file_compatibility(self):
        """Test compatibility with larger files."""
        if not self.cpp_available:
            self.skipTest("C++ examples not available")
        
        bcsv_file = self._create_temp_file('.large.bcsv')
        csv_file = self._create_temp_file('.large.csv')
        
        # Create larger dataset
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("data", pybcsv.STRING)
        layout.add_column("value", pybcsv.DOUBLE)
        
        # Generate 1000 rows
        large_data = []
        for i in range(1000):
            large_data.append([i, f"data_row_{i}", i * 1.5])
        
        # Write with Python
        writer = pybcsv.Writer(layout)
        writer.open(bcsv_file)
        writer.write_rows(large_data)
        writer.close()
        
        # Convert to CSV with C++
        result = subprocess.run(
            [self.bcsv2csv_exe, bcsv_file, csv_file],
            capture_output=True,
            text=True
        )
        
        self.assertEqual(result.returncode, 0, f"Large file conversion failed: {result.stderr}")
        
        # Verify CSV has correct number of lines (header + data)
        with open(csv_file, 'r') as f:
            csv_lines = f.readlines()
        
        self.assertEqual(len(csv_lines), 1001)  # 1 header + 1000 data rows
        
        # Verify a few sample rows
        header = csv_lines[0].strip()
        self.assertEqual(header, "id,data,value")
        
        # Check first row
        first_row = csv_lines[1].strip()
        self.assertEqual(first_row, "0,data_row_0,0")
        
        # Check last row
        last_row = csv_lines[-1].strip()
        self.assertEqual(last_row, "999,data_row_999,1498.5")

    def test_unicode_compatibility(self):
        """Test Unicode string handling between Python and C++."""
        if not self.cpp_available:
            self.skipTest("C++ examples not available")
        
        bcsv_file = self._create_temp_file('.unicode.bcsv')
        csv_file = self._create_temp_file('.unicode.csv')
        
        # Create data with Unicode strings
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("unicode_text", pybcsv.STRING)
        
        unicode_data = [
            [1, "Hello World"],
            [2, "Testing 🚀 emoji"],
            [3, "中文测试"],
            [4, "Español: niño"],
            [5, "Français: café"],
            [6, "Русский: привет"],
            [7, "日本語: こんにちは"],
            [8, "العربية: مرحبا"]
        ]
        
        # Write with Python
        writer = pybcsv.Writer(layout)
        writer.open(bcsv_file)
        writer.write_rows(unicode_data)
        writer.close()
        
        # Convert to CSV with C++
        result = subprocess.run(
            [self.bcsv2csv_exe, bcsv_file, csv_file],
            capture_output=True,
            text=True
        )
        
        self.assertEqual(result.returncode, 0, f"Unicode conversion failed: {result.stderr}")
        
        # Read CSV and verify Unicode preservation
        with open(csv_file, 'r', encoding='utf-8') as f:
            csv_content = f.read()
        
        # Check that Unicode characters are preserved
        self.assertIn("🚀", csv_content)
        self.assertIn("中文", csv_content)
        self.assertIn("niño", csv_content)
        self.assertIn("café", csv_content)
        self.assertIn("привет", csv_content)


if __name__ == '__main__':
    # First, try to build C++ examples if they don't exist
    cpp_build_dir = os.path.join(os.path.dirname(__file__), '../../build')
    if not os.path.exists(os.path.join(cpp_build_dir, 'examples')):
        print("C++ examples not found. Please build them first:")
        print(f"cd {os.path.dirname(__file__)}/../../build && make")
    
    unittest.main()