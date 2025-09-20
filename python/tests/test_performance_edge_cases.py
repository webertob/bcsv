#!/usr/bin/env python3
"""
Performance and edge case tests for PyBCSV.
Tests boundary conditions, memory usage, performance characteristics, and edge cases.
"""

import unittest
import tempfile
import os
import time
import gc
import sys
import numpy as np
import pandas as pd
import pybcsv
from typing import List, Any
import tracemalloc


class TestPerformanceEdgeCases(unittest.TestCase):
    """Test performance characteristics and edge cases."""

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

    def test_boundary_values(self):
        """Test boundary values for all numeric types."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
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
        
        # Test boundary values
        boundary_data = [
            # Min values
            [-128, -32768, -2147483648, -9223372036854775808,
             0, 0, 0, 0, 
             -3.4028235e+38, -1.7976931348623157e+308],
            
            # Max values  
            [127, 32767, 2147483647, 9223372036854775807,
             255, 65535, 4294967295, 18446744073709551615,
             3.4028235e+38, 1.7976931348623157e+308],
            
            # Zero values
            [0, 0, 0, 0, 0, 0, 0, 0, 0.0, 0.0],
            
            # Small values
            [1, 1, 1, 1, 1, 1, 1, 1, 1e-38, 1e-308],
            
            # Negative values (where applicable)
            [-1, -1, -1, -1, 128, 32768, 2147483648, 9223372036854775808,
             -1e-38, -1e-308]
        ]
        
        # Write boundary data
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_rows(boundary_data)
        writer.close()
        
        # Read back and verify
        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        
        self.assertEqual(len(read_data), len(boundary_data))
        
        # Verify each row (with tolerance for floating point)
        for i, (original, read) in enumerate(zip(boundary_data, read_data)):
            for j, (orig_val, read_val) in enumerate(zip(original, read)):
                if j >= 8:  # Float/double columns
                    if abs(orig_val) > 1e30:  # Very large numbers
                        # Allow relative error for very large numbers
                        self.assertAlmostEqual(read_val / orig_val, 1.0, places=3)
                    else:
                        self.assertAlmostEqual(read_val, orig_val, places=5)
                else:
                    self.assertEqual(read_val, orig_val, 
                                   f"Row {i}, col {j}: {read_val} != {orig_val}")

    def test_very_large_dataset(self):
        """Test handling of very large datasets."""
        filepath = self._create_temp_file()
        
        # Create layout for performance test
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("data", pybcsv.STRING)
        layout.add_column("value", pybcsv.DOUBLE)
        
        # Test with large dataset (50,000 rows)
        n_rows = 50000
        print(f"Testing with {n_rows} rows...")
        
        # Generate data in chunks to avoid memory issues
        chunk_size = 1000
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        start_time = time.time()
        
        for chunk_start in range(0, n_rows, chunk_size):
            chunk_end = min(chunk_start + chunk_size, n_rows)
            chunk_data = []
            
            for i in range(chunk_start, chunk_end):
                chunk_data.append([i, f"data_row_{i}", i * 0.5])
            
            writer.write_rows(chunk_data)
        
        writer.close()
        write_time = time.time() - start_time
        
        # Read back data
        start_time = time.time()
        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        read_time = time.time() - start_time
        
        # Verify data
        self.assertEqual(len(read_data), n_rows)
        
        # Check file size
        file_size = os.path.getsize(filepath)
        
        print(f"Write time: {write_time:.2f}s ({n_rows/write_time:.0f} rows/sec)")
        print(f"Read time: {read_time:.2f}s ({n_rows/read_time:.0f} rows/sec)")
        print(f"File size: {file_size / (1024*1024):.2f} MB")
        
        # Performance assertions
        self.assertLess(write_time, 30.0, "Write should complete within 30 seconds")
        self.assertLess(read_time, 10.0, "Read should complete within 10 seconds")
        self.assertGreater(n_rows/write_time, 1000, "Should write at least 1000 rows/sec")

    def test_memory_usage(self):
        """Test memory usage patterns."""
        if not hasattr(tracemalloc, 'start'):
            self.skipTest("tracemalloc not available")
        
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("data", pybcsv.STRING)
        
        # Start memory tracing
        tracemalloc.start()
        
        # Create moderate dataset
        n_rows = 5000
        test_data = [[i, f"test_string_{i}"] for i in range(n_rows)]
        
        # Measure memory during write
        snapshot1 = tracemalloc.take_snapshot()
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_rows(test_data)
        writer.close()
        
        snapshot2 = tracemalloc.take_snapshot()
        
        # Measure memory during read
        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        
        snapshot3 = tracemalloc.take_snapshot()
        
        # Clean up data
        del test_data
        del read_data
        gc.collect()
        
        snapshot4 = tracemalloc.take_snapshot()
        
        tracemalloc.stop()
        
        # Check memory stats
        write_stats = snapshot2.compare_to(snapshot1, 'lineno')
        read_stats = snapshot3.compare_to(snapshot2, 'lineno')
        cleanup_stats = snapshot4.compare_to(snapshot3, 'lineno')
        
        print("Memory usage during operations:")
        print(f"Write phase: {sum(stat.size_diff for stat in write_stats[:10]) / 1024:.2f} KB")
        print(f"Read phase: {sum(stat.size_diff for stat in read_stats[:10]) / 1024:.2f} KB")
        print(f"Cleanup: {sum(stat.size_diff for stat in cleanup_stats[:10]) / 1024:.2f} KB")

    def test_string_edge_cases(self):
        """Test edge cases with string handling."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("test_string", pybcsv.STRING)
        
        # Test various string edge cases
        edge_case_strings = [
            "",  # Empty string
            " ",  # Single space
            "\n",  # Newline
            "\t",  # Tab
            "\r\n",  # Windows line ending
            "\"",  # Quote
            "'",  # Single quote
            "\\",  # Backslash
            "\x00",  # Null character (may not work)
            "a" * 1000,  # Long string
            "🚀" * 100,  # Unicode emoji repeated
            "\U0001F600\U0001F601\U0001F602",  # Multiple Unicode
            "Mixed: ASCII + 中文 + 🚀 + español",  # Mixed character sets
            "\u0000\u0001\u0002",  # Control characters
            "line1\nline2\nline3",  # Multi-line
        ]
        
        test_data = [[s] for s in edge_case_strings]
        
        # Write edge cases
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        for i, row in enumerate(test_data):
            try:
                writer.write_row(row)
            except (ValueError, RuntimeError) as e:
                print(f"String edge case {i} failed (acceptable): {repr(edge_case_strings[i])}")
                # Some edge cases may legitimately fail
        
        writer.close()
        
        # Read back and verify what was written
        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        
        # At least some strings should have been written successfully
        self.assertGreater(len(read_data), 0)

    def test_concurrent_readers(self):
        """Test multiple concurrent readers on the same file."""
        filepath = self._create_temp_file()
        
        # Create test file
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("value", pybcsv.DOUBLE)
        
        test_data = [[i, i * 1.5] for i in range(100)]
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_rows(test_data)
        writer.close()
        
        # Open multiple readers simultaneously
        readers = []
        for i in range(5):
            reader = pybcsv.Reader()
            self.assertTrue(reader.open(filepath))
            readers.append(reader)
        
        # Read from all readers
        all_data = []
        for reader in readers:
            data = reader.read_all()
            all_data.append(data)
        
        # Close all readers
        for reader in readers:
            reader.close()
        
        # Verify all readers got the same data
        for data in all_data:
            self.assertEqual(data, test_data)

    def test_performance_batch_vs_individual(self):
        """Test performance difference between batch and individual operations."""
        filepath_batch = self._create_temp_file('.batch.bcsv')
        filepath_individual = self._create_temp_file('.individual.bcsv')
        
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("value", pybcsv.DOUBLE)
        
        # Test data
        n_rows = 2000
        test_data = [[i, i * 1.5] for i in range(n_rows)]
        
        # Test batch write
        start_time = time.time()
        writer_batch = pybcsv.Writer(layout)
        writer_batch.open(filepath_batch)
        writer_batch.write_rows(test_data)
        writer_batch.close()
        batch_time = time.time() - start_time
        
        # Test individual write
        start_time = time.time()
        writer_individual = pybcsv.Writer(layout)
        writer_individual.open(filepath_individual)
        for row in test_data:
            writer_individual.write_row(row)
        writer_individual.close()
        individual_time = time.time() - start_time
        
        # Verify both files have same data
        reader1 = pybcsv.Reader()
        reader1.open(filepath_batch)
        data1 = reader1.read_all()
        reader1.close()
        
        reader2 = pybcsv.Reader()
        reader2.open(filepath_individual)
        data2 = reader2.read_all()
        reader2.close()
        
        self.assertEqual(data1, data2)
        
        # Batch should be faster
        speedup = individual_time / batch_time
        print(f"Batch time: {batch_time:.4f}s")
        print(f"Individual time: {individual_time:.4f}s")
        print(f"Speedup: {speedup:.2f}x")
        
        self.assertGreater(speedup, 1.0, "Batch operations should be faster")

    def test_compression_effectiveness(self):
        """Test compression effectiveness with different data patterns."""
        
        # Test highly repetitive data (should compress well)
        repetitive_data = [["same_string"] * 1000 for _ in range(100)]
        
        # Test random data (should compress poorly)
        import random
        random_data = [[f"random_{random.randint(0, 999999)}"] for _ in range(1000)]
        
        # Test mixed data
        mixed_data = []
        for i in range(500):
            if i % 2 == 0:
                mixed_data.append(["repetitive"])
            else:
                mixed_data.append([f"unique_{i}"])
        
        layout = pybcsv.Layout()
        layout.add_column("data", pybcsv.STRING)
        
        test_cases = [
            ("repetitive", repetitive_data),
            ("random", random_data),
            ("mixed", mixed_data)
        ]
        
        for name, data in test_cases:
            for compression_level in [1, 9]:
                filepath = self._create_temp_file(f'.{name}_comp{compression_level}.bcsv')
                
                writer = pybcsv.Writer(layout)
                writer.open(filepath, compression_level=compression_level)
                writer.write_rows(data)
                writer.close()
                
                file_size = os.path.getsize(filepath)
                print(f"{name} data, compression {compression_level}: {file_size} bytes")
                
                # Verify data integrity
                reader = pybcsv.Reader()
                reader.open(filepath)
                read_data = reader.read_all()
                reader.close()
                
                self.assertEqual(len(read_data), len(data))

    def test_zero_length_fields(self):
        """Test handling of zero-length and minimal data."""
        filepath = self._create_temp_file()
        
        # Test with minimal layout and data
        layout = pybcsv.Layout()
        layout.add_column("empty", pybcsv.STRING)
        
        # Test various minimal cases
        minimal_data = [
            [""],  # Empty string
            ["a"],  # Single character
            [" "],  # Single space
        ]
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_rows(minimal_data)
        writer.close()
        
        reader = pybcsv.Reader()
        reader.open(filepath)
        read_data = reader.read_all()
        reader.close()
        
        self.assertEqual(read_data, minimal_data)

    def test_file_size_limits(self):
        """Test behavior near file size limits."""
        filepath = self._create_temp_file()
        
        layout = pybcsv.Layout()
        layout.add_column("data", pybcsv.STRING)
        
        # Create moderately large strings to test file growth
        large_string = "x" * 10000  # 10KB string
        
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        
        # Write until we have a reasonably large file (limit to avoid test timeouts)
        max_rows = 1000  # This should create ~10MB file
        for i in range(max_rows):
            writer.write_row([f"{large_string}_{i}"])
            
            # Check file size periodically
            if i % 100 == 0:
                writer.flush()
                current_size = os.path.getsize(filepath)
                if current_size > 50 * 1024 * 1024:  # Stop at 50MB
                    break
        
        writer.close()
        
        final_size = os.path.getsize(filepath)
        print(f"Final file size: {final_size / (1024*1024):.2f} MB")
        
        # Verify we can still read the file
        reader = pybcsv.Reader()
        self.assertTrue(reader.open(filepath))
        
        # Read just first few rows to verify integrity
        row_count = 0
        try:
            data = reader.read_all()
            row_count = len(data)
        finally:
            reader.close()
        
        self.assertGreater(row_count, 0)
        print(f"Successfully read {row_count} rows from large file")


if __name__ == '__main__':
    unittest.main()