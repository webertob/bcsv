#!/usr/bin/env python3
"""
Pandas Integration Example

This example demonstrates how to use pybcsv with pandas DataFrames:
- Converting DataFrames to BCSV files
- Reading BCSV files as DataFrames
- Performance comparison with CSV
"""

import pybcsv
import pandas as pd
import numpy as np
import time
import os

def create_sample_dataframe(num_rows=10000):
    """Create a sample DataFrame for testing."""
    print(f"Creating sample DataFrame with {num_rows} rows...")
    
    np.random.seed(42)  # For reproducible results
    
    data = {
        'id': range(1, num_rows + 1),
        'name': [f'Person_{i}' for i in range(1, num_rows + 1)],
        'age': np.random.randint(18, 80, num_rows),
        'salary': np.random.normal(50000, 15000, num_rows).round(2),
        'score': np.random.uniform(0, 100, num_rows).round(1),
        'active': np.random.choice([True, False], num_rows),
        'department': np.random.choice(['Engineering', 'Sales', 'Marketing', 'HR'], num_rows),
        'rating': np.random.choice([1, 2, 3, 4, 5], num_rows),
    }
    
    return pd.DataFrame(data)

def benchmark_write_performance(df):
    """Compare write performance between BCSV and CSV."""
    print("\n=== Write Performance Comparison ===")
    
    # BCSV write (compressed)
    start_time = time.time()
    pybcsv.write_dataframe(df, "test_compressed.bcsv", compression=True)
    bcsv_compressed_time = time.time() - start_time
    bcsv_compressed_size = os.path.getsize("test_compressed.bcsv")
    
    # BCSV write (uncompressed)
    start_time = time.time()
    pybcsv.write_dataframe(df, "test_uncompressed.bcsv", compression=False)
    bcsv_uncompressed_time = time.time() - start_time
    bcsv_uncompressed_size = os.path.getsize("test_uncompressed.bcsv")
    
    # CSV write
    start_time = time.time()
    df.to_csv("test.csv", index=False)
    csv_time = time.time() - start_time
    csv_size = os.path.getsize("test.csv")
    
    print(f"BCSV (compressed):   {bcsv_compressed_time:.3f}s, {bcsv_compressed_size:,} bytes")
    print(f"BCSV (uncompressed): {bcsv_uncompressed_time:.3f}s, {bcsv_uncompressed_size:,} bytes")
    print(f"CSV:                 {csv_time:.3f}s, {csv_size:,} bytes")
    print(f"Compression ratio:   {(csv_size / bcsv_compressed_size):.1f}x smaller than CSV")
    print(f"Speed improvement:   {(csv_time / bcsv_compressed_time):.1f}x faster than CSV")

def benchmark_read_performance(num_rows):
    """Compare read performance between BCSV and CSV."""
    print("\n=== Read Performance Comparison ===")
    
    # BCSV read (compressed)
    start_time = time.time()
    df_bcsv_compressed = pybcsv.read_dataframe("test_compressed.bcsv")
    bcsv_compressed_time = time.time() - start_time
    
    # BCSV read (uncompressed)
    start_time = time.time()
    df_bcsv_uncompressed = pybcsv.read_dataframe("test_uncompressed.bcsv")
    bcsv_uncompressed_time = time.time() - start_time
    
    # CSV read
    start_time = time.time()
    df_csv = pd.read_csv("test.csv")
    csv_time = time.time() - start_time
    
    print(f"BCSV (compressed):   {bcsv_compressed_time:.3f}s")
    print(f"BCSV (uncompressed): {bcsv_uncompressed_time:.3f}s")
    print(f"CSV:                 {csv_time:.3f}s")
    print(f"Speed improvement:   {(csv_time / bcsv_compressed_time):.1f}x faster than CSV")
    
    # Verify data integrity
    assert len(df_bcsv_compressed) == num_rows
    assert len(df_bcsv_uncompressed) == num_rows
    assert len(df_csv) == num_rows
    print("✓ Data integrity verified")

def demonstrate_type_hints():
    """Demonstrate using type hints for better control over data types."""
    print("\n=== Type Hints Example ===")
    
    # Create a DataFrame with ambiguous types
    data = {
        'category_id': [1, 2, 3, 4, 5],  # Could be INT8 instead of default INT64
        'percentage': [0.1, 0.2, 0.3, 0.4, 0.5],  # Could be FLOAT instead of DOUBLE
        'status_code': [200, 404, 500, 200, 301],  # Could be UINT16
    }
    df = pd.DataFrame(data)
    
    print("Original DataFrame dtypes:")
    print(df.dtypes)
    
    # Write with type hints for more efficient storage
    type_hints = {
        'category_id': pybcsv.ColumnType.INT8,
        'percentage': pybcsv.ColumnType.FLOAT,
        'status_code': pybcsv.ColumnType.UINT16,
    }
    
    pybcsv.write_dataframe(df, "typed_data.bcsv", type_hints=type_hints)
    
    # Read back and verify types
    df_read = pybcsv.read_dataframe("typed_data.bcsv")
    print("\nDataFrame after BCSV round-trip:")
    print(df_read.dtypes)
    print(df_read)

def demonstrate_csv_conversion():
    """Demonstrate CSV to BCSV conversion utilities."""
    print("\n=== CSV Conversion Utilities ===")
    
    # Create a sample CSV
    sample_data = {
        'product_id': [1, 2, 3],
        'product_name': ['Widget A', 'Widget B', 'Widget C'],
        'price': [19.99, 29.99, 39.99],
        'in_stock': [True, False, True]
    }
    df = pd.DataFrame(sample_data)
    df.to_csv("sample.csv", index=False)
    
    # Convert CSV to BCSV
    print("Converting CSV to BCSV...")
    pybcsv.from_csv("sample.csv", "converted.bcsv", compression=True)
    
    # Convert BCSV back to CSV
    print("Converting BCSV back to CSV...")
    pybcsv.to_csv("converted.bcsv", "roundtrip.csv")
    
    # Verify the round trip
    original_df = pd.read_csv("sample.csv")
    roundtrip_df = pd.read_csv("roundtrip.csv")
    
    print("Original CSV:")
    print(original_df)
    print("\nAfter BCSV round-trip:")
    print(roundtrip_df)
    
    # Check file sizes
    csv_size = os.path.getsize("sample.csv")
    bcsv_size = os.path.getsize("converted.bcsv")
    print(f"\nFile sizes:")
    print(f"Original CSV: {csv_size} bytes")
    print(f"BCSV:         {bcsv_size} bytes")
    print(f"Compression:  {(csv_size / bcsv_size):.1f}x")

def main():
    print("=== Pandas Integration Example ===")
    
    try:
        # Check if pandas is available
        import pandas as pd
        print("✓ pandas is available")
        
        # Create sample data
        num_rows = 50000
        df = create_sample_dataframe(num_rows)
        print(f"Created DataFrame: {df.shape}")
        print("\nDataFrame info:")
        print(df.dtypes)
        print(f"\nFirst 5 rows:")
        print(df.head())
        
        # Benchmark performance
        benchmark_write_performance(df)
        benchmark_read_performance(num_rows)
        
        # Demonstrate type hints
        demonstrate_type_hints()
        
        # Demonstrate CSV conversion
        demonstrate_csv_conversion()
        
    except ImportError:
        print("❌ pandas is not available")
        print("Please install pandas to run this example: pip install pandas")
        return
    
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
    
    finally:
        # Clean up
        test_files = [
            "test_compressed.bcsv", "test_uncompressed.bcsv", "test.csv",
            "typed_data.bcsv", "sample.csv", "converted.bcsv", "roundtrip.csv"
        ]
        
        for filename in test_files:
            if os.path.exists(filename):
                os.remove(filename)
                print(f"Cleaned up: {filename}")

if __name__ == "__main__":
    main()