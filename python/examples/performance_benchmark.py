#!/usr/bin/env python3
"""
Performance Benchmark Example

This example demonstrates BCSV performance characteristics:
- Large dataset handling
- Compression effectiveness
- Memory usage
- Read/write speed comparisons
"""

import pybcsv
import time
import os
import sys
import numpy as np

try:
    import pandas as pd
    import psutil
    ADVANCED_FEATURES = True
except ImportError:
    ADVANCED_FEATURES = False

def format_bytes(bytes_val):
    """Format bytes in human readable format."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.1f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.1f} TB"

def get_memory_usage():
    """Get current memory usage if psutil is available."""
    if ADVANCED_FEATURES:
        process = psutil.Process(os.getpid())
        return process.memory_info().rss
    return None

def create_large_dataset(num_rows):
    """Create a large dataset for performance testing."""
    print(f"Creating dataset with {num_rows:,} rows...")
    
    # Use numpy for efficient data generation
    np.random.seed(42)
    
    # Create layout
    layout = pybcsv.Layout()
    layout.add_column("timestamp", pybcsv.ColumnType.INT64)
    layout.add_column("sensor_id", pybcsv.ColumnType.INT16)
    layout.add_column("temperature", pybcsv.ColumnType.FLOAT)
    layout.add_column("humidity", pybcsv.ColumnType.FLOAT)
    layout.add_column("pressure", pybcsv.ColumnType.DOUBLE)
    layout.add_column("location", pybcsv.ColumnType.STRING)
    layout.add_column("is_valid", pybcsv.ColumnType.BOOL)
    layout.add_column("error_code", pybcsv.ColumnType.UINT8)
    
    return layout

def benchmark_write_performance(layout, num_rows):
    """Benchmark write performance with different configurations."""
    print(f"\n=== Write Performance Benchmark ({num_rows:,} rows) ===")
    
    # Generate data in batches to manage memory
    batch_size = 10000
    
    # Test 1: Compressed BCSV
    print("Writing compressed BCSV...")
    start_time = time.time()
    start_memory = get_memory_usage()
    
    with pybcsv.Writer("benchmark_compressed.bcsv", layout, pybcsv.FileFlags.COMPRESSED) as writer:
        for batch_start in range(0, num_rows, batch_size):
            batch_end = min(batch_start + batch_size, num_rows)
            current_batch_size = batch_end - batch_start
            
            # Generate batch data
            timestamps = np.arange(batch_start, batch_end, dtype=np.int64)
            sensor_ids = np.random.randint(1, 100, current_batch_size, dtype=np.int16)
            temperatures = np.random.uniform(15.0, 35.0, current_batch_size).astype(np.float32)
            humidities = np.random.uniform(30.0, 90.0, current_batch_size).astype(np.float32)
            pressures = np.random.uniform(980.0, 1030.0, current_batch_size)
            locations = [f"Site_{sid % 10}" for sid in sensor_ids]
            is_valid = np.random.choice([True, False], current_batch_size, p=[0.95, 0.05])
            error_codes = np.random.choice([0, 1, 2, 255], current_batch_size, p=[0.9, 0.05, 0.03, 0.02]).astype(np.uint8)
            
            # Write batch
            for i in range(current_batch_size):
                row = [
                    int(timestamps[i]),
                    int(sensor_ids[i]),
                    float(temperatures[i]),
                    float(humidities[i]),
                    float(pressures[i]),
                    locations[i],
                    bool(is_valid[i]),
                    int(error_codes[i])
                ]
                writer.write_row(row)
    
    compressed_time = time.time() - start_time
    compressed_size = os.path.getsize("benchmark_compressed.bcsv")
    end_memory = get_memory_usage()
    memory_used = (end_memory - start_memory) if start_memory and end_memory else None
    
    print(f"Compressed BCSV: {compressed_time:.2f}s, {format_bytes(compressed_size)}")
    if memory_used:
        print(f"Memory used: {format_bytes(memory_used)}")
    
    # Test 2: Uncompressed BCSV
    print("Writing uncompressed BCSV...")
    start_time = time.time()
    
    with pybcsv.Writer("benchmark_uncompressed.bcsv", layout, pybcsv.FileFlags.NONE) as writer:
        for batch_start in range(0, num_rows, batch_size):
            batch_end = min(batch_start + batch_size, num_rows)
            current_batch_size = batch_end - batch_start
            
            # Generate batch data (same as above)
            timestamps = np.arange(batch_start, batch_end, dtype=np.int64)
            sensor_ids = np.random.randint(1, 100, current_batch_size, dtype=np.int16)
            temperatures = np.random.uniform(15.0, 35.0, current_batch_size).astype(np.float32)
            humidities = np.random.uniform(30.0, 90.0, current_batch_size).astype(np.float32)
            pressures = np.random.uniform(980.0, 1030.0, current_batch_size)
            locations = [f"Site_{sid % 10}" for sid in sensor_ids]
            is_valid = np.random.choice([True, False], current_batch_size, p=[0.95, 0.05])
            error_codes = np.random.choice([0, 1, 2, 255], current_batch_size, p=[0.9, 0.05, 0.03, 0.02]).astype(np.uint8)
            
            # Write batch
            for i in range(current_batch_size):
                row = [
                    int(timestamps[i]),
                    int(sensor_ids[i]),
                    float(temperatures[i]),
                    float(humidities[i]),
                    float(pressures[i]),
                    locations[i],
                    bool(is_valid[i]),
                    int(error_codes[i])
                ]
                writer.write_row(row)
    
    uncompressed_time = time.time() - start_time
    uncompressed_size = os.path.getsize("benchmark_uncompressed.bcsv")
    
    print(f"Uncompressed BCSV: {uncompressed_time:.2f}s, {format_bytes(uncompressed_size)}")
    
    # Compare compression
    compression_ratio = uncompressed_size / compressed_size
    print(f"Compression ratio: {compression_ratio:.1f}x")
    print(f"Write speed: {num_rows / compressed_time:.0f} rows/second (compressed)")

def benchmark_read_performance(num_rows):
    """Benchmark read performance."""
    print(f"\n=== Read Performance Benchmark ===")
    
    # Test 1: Read compressed file
    print("Reading compressed BCSV...")
    start_time = time.time()
    start_memory = get_memory_usage()
    
    row_count = 0
    with pybcsv.Reader("benchmark_compressed.bcsv") as reader:
        for row in reader:
            row_count += 1
            # Process every 10000th row to avoid too much overhead
            if row_count % 10000 == 0:
                pass  # Could do processing here
    
    compressed_read_time = time.time() - start_time
    end_memory = get_memory_usage()
    memory_used = (end_memory - start_memory) if start_memory and end_memory else None
    
    print(f"Compressed read: {compressed_read_time:.2f}s, {row_count:,} rows")
    if memory_used:
        print(f"Memory used: {format_bytes(memory_used)}")
    
    # Test 2: Read uncompressed file
    print("Reading uncompressed BCSV...")
    start_time = time.time()
    
    row_count = 0
    with pybcsv.Reader("benchmark_uncompressed.bcsv") as reader:
        for row in reader:
            row_count += 1
    
    uncompressed_read_time = time.time() - start_time
    print(f"Uncompressed read: {uncompressed_read_time:.2f}s, {row_count:,} rows")
    
    print(f"Read speed: {num_rows / compressed_read_time:.0f} rows/second (compressed)")
    print(f"Read speed: {num_rows / uncompressed_read_time:.0f} rows/second (uncompressed)")

def demonstrate_streaming():
    """Demonstrate streaming capabilities for large files."""
    print(f"\n=== Streaming Demonstration ===")
    
    print("Processing data in streaming fashion...")
    start_time = time.time()
    
    # Calculate statistics while streaming
    temperature_sum = 0.0
    temperature_count = 0
    valid_readings = 0
    error_count = 0
    
    with pybcsv.Reader("benchmark_compressed.bcsv") as reader:
        layout = reader.get_layout()
        print(f"File layout: {layout}")
        
        for row in reader:
            # Extract values (indices based on our layout)
            temperature = row[2]  # temperature column
            is_valid = row[6]     # is_valid column
            error_code = row[7]   # error_code column
            
            if is_valid:
                temperature_sum += temperature
                temperature_count += 1
                valid_readings += 1
            
            if error_code != 0:
                error_count += 1
    
    processing_time = time.time() - start_time
    
    print(f"Streaming processing: {processing_time:.2f}s")
    print(f"Average temperature: {temperature_sum / temperature_count:.1f}°C")
    print(f"Valid readings: {valid_readings:,}")
    print(f"Error readings: {error_count:,}")

def main():
    print("=== BCSV Performance Benchmark ===")
    
    if len(sys.argv) > 1:
        try:
            num_rows = int(sys.argv[1])
        except ValueError:
            print("Usage: python performance_benchmark.py [num_rows]")
            return
    else:
        num_rows = 100000  # Default
    
    print(f"Benchmarking with {num_rows:,} rows")
    
    if ADVANCED_FEATURES:
        print("✓ Advanced features available (pandas, psutil)")
    else:
        print("⚠ Limited features (install pandas and psutil for full benchmarks)")
    
    try:
        # Create layout
        layout = create_large_dataset(num_rows)
        print(f"Layout: {layout}")
        
        # Benchmark writing
        benchmark_write_performance(layout, num_rows)
        
        # Benchmark reading
        benchmark_read_performance(num_rows)
        
        # Demonstrate streaming
        demonstrate_streaming()
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
    
    finally:
        # Clean up
        test_files = ["benchmark_compressed.bcsv", "benchmark_uncompressed.bcsv"]
        
        for filename in test_files:
            if os.path.exists(filename):
                os.remove(filename)
                print(f"Cleaned up: {filename}")

if __name__ == "__main__":
    main()