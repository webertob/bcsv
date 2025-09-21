#!/usr/bin/env python3
"""Simple test to verify basic functionality works."""

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

import pytest
import pandas as pd
from pathlib import Path

def test_basic_operations():
    """Test basic operations first."""
    print("🧪 Testing Basic BCSV Operations...")
    
    # Test 1: Simple write/read
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.ColumnType.INT32)
    layout.add_column("name", pybcsv.ColumnType.STRING)
    
    test_file = Path("/tmp/test_basic.bcsv")
    
    # Test individual writes
    writer = pybcsv.Writer(layout)
    try:
        writer.open(str(test_file))
        
        # Write individual rows
        writer.write_row([1, "test1"])
        writer.write_row([2, "test2"])
        writer.write_row([3, "test3"])
        
    finally:
        writer.close()
    
    # Read back
    reader = pybcsv.Reader()
    try:
        reader.open(str(test_file))
        data = reader.read_all()
        print(f"✅ Read {len(data)} rows successfully")
        print(f"   First row: {data[0]}")
        
    finally:
        reader.close()
    
    # Test 2: DataFrame operations
    df = pd.DataFrame({
        'id': [1, 2, 3, 4, 5],
        'value': [1.1, 2.2, 3.3, 4.4, 5.5],
        'name': ['a', 'b', 'c', 'd', 'e']
    })
    
    df_file = Path("/tmp/test_df.bcsv")
    pybcsv.write_dataframe(df, str(df_file))
    df_read = pybcsv.read_dataframe(str(df_file))
    
    print(f"✅ DataFrame test: wrote {len(df)} rows, read {len(df_read)} rows")
    
    # Clean up
    test_file.unlink(missing_ok=True)
    df_file.unlink(missing_ok=True)
    
    print("🎉 Basic tests passed!")

if __name__ == "__main__":
    test_basic_operations()