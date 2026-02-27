#!/usr/bin/env python3

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""
Basic BCSV Usage Example

This example demonstrates the basic functionality of the pybcsv library:
- Creating a layout
- Writing data to a BCSV file
- Reading data back from the file
"""

import pybcsv
import os

def main():
    print("=== Basic BCSV Usage Example ===")
    
    # Define the file path
    filename = "example_basic.bcsv"
    
    try:
        # Step 1: Create a layout definition
        print("\n1. Creating layout...")
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.ColumnType.INT32)
        layout.add_column("name", pybcsv.ColumnType.STRING)
        layout.add_column("score", pybcsv.ColumnType.DOUBLE)
        layout.add_column("active", pybcsv.ColumnType.BOOL)
        
        print(f"Layout created with {len(layout)} columns:")
        print(layout)
        
        # Step 2: Write data to BCSV file
        print("\n2. Writing data...")
        sample_data = [
            [1, "Alice", 95.5, True],
            [2, "Bob", 87.2, True],
            [3, "Charlie", 92.1, False],
            [4, "Diana", 88.9, True],
            [5, "Eve", 91.7, False]
        ]
        
        with pybcsv.Writer(layout) as writer:
            writer.open(filename)
            for row in sample_data:
                writer.write_row(row)
        
        print(f"Successfully wrote {len(sample_data)} rows to {filename}")
        
        # Step 3: Read data back
        print("\n3. Reading data back...")
        with pybcsv.Reader() as reader:
            reader.open(filename)
            read_layout = reader.get_layout()
            print(f"File layout: {read_layout}")
            
            print("\nData rows:")
            row_count = 0
            for row in reader:
                print(f"  Row {row_count + 1}: {row}")
                row_count += 1
            
            print(f"Successfully read {row_count} rows")
        
        # Step 4: Alternative reading method - read all at once
        print("\n4. Reading all data at once...")
        with pybcsv.Reader() as reader:
            reader.open(filename)
            all_rows = reader.read_all()
            print(f"Read {len(all_rows)} rows in one call:")
            for i, row in enumerate(all_rows):
                print(f"  Row {i + 1}: {row}")
        
        # Step 5: Show file information
        file_size = os.path.getsize(filename)
        print(f"\n5. File information:")
        print(f"  File size: {file_size} bytes")
        print(f"  Average bytes per row: {file_size / len(sample_data):.1f}")
        
    except Exception as e:
        print(f"Error: {e}")
    
    finally:
        # Clean up
        if os.path.exists(filename):
            os.remove(filename)
            print(f"\nCleaned up: removed {filename}")

if __name__ == "__main__":
    main()