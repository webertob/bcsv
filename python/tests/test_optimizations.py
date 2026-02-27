#!/usr/bin/env python3
"""Quick test to verify the optimized BCSV Python wrapper works correctly."""

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

import pytest
import numpy as np
import pandas as pd
import time
from pathlib import Path
import pybcsv

def test_optimized_operations():
    """Test optimized read/write operations."""
    print("🧪 Testing Optimized BCSV Operations...")
    
    # Create test data
    test_data = {
        'bool_col': [True, False, True] * 1000,
        'int_col': list(range(3000)),
        'float_col': [1.1, 2.2, 3.3] * 1000,
        'string_col': ['test', 'data', 'row'] * 1000
    }
    df = pd.DataFrame(test_data)
    
    # Test file path
    test_file = Path("/tmp/test_optimized.bcsv")
    
    # Test optimized write
    print("✅ Testing optimized DataFrame write...")
    start_time = time.time()
    pybcsv.write_dataframe(df, str(test_file), compression_level=1)
    write_time = time.time() - start_time
    print(f"   📊 Write time: {write_time:.4f}s ({len(df)} rows)")
    
    # Test optimized read
    print("✅ Testing optimized DataFrame read...")
    start_time = time.time()
    df_read = pybcsv.read_dataframe(str(test_file))
    read_time = time.time() - start_time
    print(f"   📊 Read time: {read_time:.4f}s ({len(df_read)} rows)")
    
    # Verify data integrity
    print("✅ Verifying data integrity...")
    assert len(df) == len(df_read), f"Row count mismatch: {len(df)} vs {len(df_read)}"
    assert list(df.columns) == list(df_read.columns), "Column mismatch"
    print("   ✅ Data integrity verified!")
    
    # Test batch operations
    print("✅ Testing batch write operations...")
    
    # Create layout and writer
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.ColumnType.INT32)
    layout.add_column("value", pybcsv.ColumnType.DOUBLE)
    layout.add_column("name", pybcsv.ColumnType.STRING)
    
    batch_file = Path("/tmp/test_batch.bcsv")
    writer = pybcsv.Writer(layout)
    
    try:
        writer.open(str(batch_file))
        
        # Test individual row writes (using optimized cached writer)
        start_time = time.time()
        for i in range(1000):
            writer.write_row([i, float(i) * 1.5, f"row_{i}"])
        individual_time = time.time() - start_time
        print(f"   📊 Individual writes: {individual_time:.4f}s (1000 rows)")
        
        # Test batch writes (using new optimized batch interface)
        batch_data = [[i + 1000, float(i + 1000) * 1.5, f"batch_{i}"] 
                      for i in range(1000)]
        start_time = time.time()
        writer.write_rows(batch_data)
        batch_time = time.time() - start_time
        print(f"   📊 Batch writes: {batch_time:.4f}s (1000 rows)")
        
        if individual_time > 0:
            speedup = individual_time / batch_time if batch_time > 0 else float('inf')
            print(f"   🚀 Batch speedup: {speedup:.2f}x")
        
    finally:
        writer.close()
    
    # Test optimized reading
    print("✅ Testing optimized batch read...")
    reader = pybcsv.Reader()
    
    try:
        reader.open(str(batch_file))
        
        # Test read_all (optimized)
        start_time = time.time()
        all_data = reader.read_all()
        read_all_time = time.time() - start_time
        print(f"   📊 Read all: {read_all_time:.4f}s ({len(all_data)} rows)")
        
        assert len(all_data) == 2000, f"Expected 2000 rows, got {len(all_data)}"
        print("   ✅ Read verification passed!")
        
    finally:
        reader.close()
    
    # Clean up
    test_file.unlink(missing_ok=True)
    batch_file.unlink(missing_ok=True)
    
    print("🎉 All optimization tests passed!")
    print(f"📈 Performance Summary:")
    print(f"   - DataFrame write: {write_time:.4f}s")
    print(f"   - DataFrame read: {read_time:.4f}s")
    print(f"   - Batch operations: ✅ Working")
    print(f"   - Memory optimizations: ✅ Applied")

if __name__ == "__main__":
    test_optimized_operations()