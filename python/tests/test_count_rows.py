#!/usr/bin/env python3
"""Test count_rows functionality in pybcsv."""

import tempfile
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pybcsv


def test_count_rows():
    """Test the count_rows method."""
    print("🧪 Testing count_rows functionality...")
    
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.ColumnType.INT32)
    layout.add_column("name", pybcsv.ColumnType.STRING)
    layout.add_column("value", pybcsv.ColumnType.DOUBLE)
    
    test_cases = [0, 1, 10, 100]
    
    for expected_rows in test_cases:
        with tempfile.NamedTemporaryFile(suffix='.bcsv', delete=False) as f:
            temp_file = f.name
        
        try:
            # Write test data
            writer = pybcsv.Writer(layout)
            writer.open(temp_file, overwrite=True)
            
            for i in range(expected_rows):
                writer.write_row([i, f'item_{i}', i * 2.5])
            
            writer.close()
            
            # Test count_rows
            reader = pybcsv.Reader()
            reader.open(temp_file)
            
            counted_rows = reader.count_rows()
            
            # Verify by reading all and counting
            all_rows = list(reader.read_all())
            actual_rows = len(all_rows)
            
            reader.close()
            
            # Assertions
            assert counted_rows == expected_rows, f"count_rows() returned {counted_rows}, expected {expected_rows}"
            assert actual_rows == expected_rows, f"read_all() returned {actual_rows} rows, expected {expected_rows}"
            assert counted_rows == actual_rows, f"count_rows() and read_all() mismatch: {counted_rows} vs {actual_rows}"
            
            print(f"✅ {expected_rows} rows: count_rows() = {counted_rows}, read_all() = {actual_rows}")
            
        finally:
            if os.path.exists(temp_file):
                os.unlink(temp_file)
    
    print("🎉 count_rows tests passed!")


if __name__ == "__main__":
    test_count_rows()