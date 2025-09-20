#!/usr/bin/env python3
"""
Comprehensive demonstration of PyBCSV functionality
"""

import pybcsv
import pandas as pd
import numpy as np
import tempfile
import os
import time

def demo_comprehensive():
    """Comprehensive demonstration of all PyBCSV features"""
    print("🚀 PyBCSV Comprehensive Demo")
    print("="*50)
    
    # 1. Basic BCSV Operations
    print("\n1. Basic BCSV Operations")
    print("-" * 30)
    
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.INT64)
    layout.add_column("name", pybcsv.STRING)
    layout.add_column("score", pybcsv.DOUBLE)
    layout.add_column("active", pybcsv.BOOL)
    
    print(f"Created layout with {layout.column_count()} columns:")
    for i in range(layout.column_count()):
        print(f"  {i}: {layout.column_name(i)} ({pybcsv.type_to_string(layout.column_type(i))})")
    
    # 2. Writing Data
    print("\n2. Writing Data")
    print("-" * 20)
    
    with tempfile.NamedTemporaryFile(suffix=".bcsv", delete=False) as tmp:
        filename = tmp.name
    
    try:
        writer = pybcsv.Writer(layout)
        writer.open(filename)
        
        test_data = [
            [1, "Alice", 95.5, True],
            [2, "Bob", 87.2, False],
            [3, "Charlie", 92.8, True],
            [4, "Diana", 98.1, True],
            [5, "Eve", 89.7, False]
        ]
        
        for row in test_data:
            writer.write_row(row)
        
        writer.close()
        
        file_size = os.path.getsize(filename)
        print(f"Written {len(test_data)} rows to {filename}")
        print(f"File size: {file_size} bytes")
        
        # 3. Reading Data
        print("\n3. Reading Data")
        print("-" * 20)
        
        reader = pybcsv.Reader()
        reader.open(filename)
        
        rows = reader.read_all()
        reader.close()
        
        print(f"Read {len(rows)} rows:")
        for i, row in enumerate(rows, 1):
            print(f"  Row {i}: {row}")
        
        # 4. Pandas Integration
        print("\n4. Pandas Integration")
        print("-" * 25)
        
        # Create a more complex DataFrame
        np.random.seed(42)
        df = pd.DataFrame({
            'user_id': range(1, 101),
            'username': [f"user_{i}" for i in range(1, 101)],
            'age': np.random.randint(18, 65, 100),
            'salary': np.random.normal(50000, 15000, 100),
            'is_premium': np.random.choice([True, False], 100, p=[0.3, 0.7]),
            'rating': np.random.uniform(1.0, 5.0, 100)
        })
        
        print(f"Created DataFrame with shape {df.shape}")
        print(f"DataFrame memory usage: {df.memory_usage(deep=True).sum()} bytes")
        
        # Write DataFrame to BCSV
        start_time = time.time()
        pybcsv.write_dataframe(df, filename)
        write_time = time.time() - start_time
        
        bcsv_size = os.path.getsize(filename)
        print(f"BCSV write time: {write_time:.4f} seconds")
        print(f"BCSV file size: {bcsv_size} bytes")
        
        # Read DataFrame back
        start_time = time.time()
        df_read = pybcsv.read_dataframe(filename)
        read_time = time.time() - start_time
        
        print(f"BCSV read time: {read_time:.4f} seconds")
        print(f"Data integrity check: {df.shape == df_read.shape}")
        
        # Check data types
        print("\nData type preservation:")
        for col in df.columns:
            orig_dtype = df[col].dtype
            read_dtype = df_read[col].dtype
            preserved = str(orig_dtype) == str(read_dtype)
            print(f"  {col}: {orig_dtype} -> {read_dtype} {'✓' if preserved else '✗'}")
        
        # 5. CSV Comparison
        print("\n5. CSV vs BCSV Comparison")
        print("-" * 30)
        
        # Write to CSV for comparison
        csv_filename = filename.replace('.bcsv', '.csv')
        
        start_time = time.time()
        df.to_csv(csv_filename, index=False)
        csv_write_time = time.time() - start_time
        
        csv_size = os.path.getsize(csv_filename)
        
        start_time = time.time()
        df_csv = pd.read_csv(csv_filename)
        csv_read_time = time.time() - start_time
        
        print(f"CSV file size: {csv_size} bytes")
        print(f"CSV write time: {csv_write_time:.4f} seconds")
        print(f"CSV read time: {csv_read_time:.4f} seconds")
        
        print(f"\nSize comparison:")
        print(f"  BCSV: {bcsv_size} bytes")
        print(f"  CSV:  {csv_size} bytes")
        print(f"  Compression ratio: {csv_size/bcsv_size:.2f}x")
        
        print(f"\nPerformance comparison:")
        print(f"  BCSV write: {write_time:.4f}s")
        print(f"  CSV write:  {csv_write_time:.4f}s")
        print(f"  BCSV read:  {read_time:.4f}s")
        print(f"  CSV read:   {csv_read_time:.4f}s")
        
        # 6. Type System Demo
        print("\n6. Type System Demo")
        print("-" * 22)
        
        type_layout = pybcsv.Layout()
        type_layout.add_column("int8_col", pybcsv.INT8)
        type_layout.add_column("uint16_col", pybcsv.UINT16)
        type_layout.add_column("int32_col", pybcsv.INT32)
        type_layout.add_column("uint64_col", pybcsv.UINT64)
        type_layout.add_column("float_col", pybcsv.FLOAT)
        type_layout.add_column("double_col", pybcsv.DOUBLE)
        type_layout.add_column("bool_col", pybcsv.BOOL)
        type_layout.add_column("string_col", pybcsv.STRING)
        
        print("Available BCSV types:")
        for i in range(type_layout.column_count()):
            col_name = type_layout.column_name(i)
            col_type = type_layout.column_type(i)
            type_str = pybcsv.type_to_string(col_type)
            print(f"  {col_name}: {type_str}")
        
        # Clean up
        os.unlink(csv_filename)
        
    finally:
        if os.path.exists(filename):
            os.unlink(filename)
    
    print(f"\n✅ Demo completed successfully!")
    print(f"🎉 PyBCSV is ready for use in your Python data projects!")

if __name__ == "__main__":
    demo_comprehensive()