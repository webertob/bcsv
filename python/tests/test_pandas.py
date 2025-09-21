#!/usr/bin/env python3
"""
Test script for pybcsv pandas integration
"""

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

import pytest
import pandas as pd
import tempfile
import os
import numpy as np

def test_pandas_integration():
    """Test pandas DataFrame integration"""
    print("Testing PyBCSV pandas integration...")
    
    # Create a sample DataFrame
    df = pd.DataFrame({
        'id': [1, 2, 3, 4, 5],
        'name': ['Alice', 'Bob', 'Charlie', 'Diana', 'Eve'],
        'age': [25, 30, 35, 28, 32],
        'salary': [50000.0, 60000.0, 70000.0, 55000.0, 65000.0],
        'active': [True, False, True, True, False]
    })
    
    print("Original DataFrame:")
    print(df)
    print(f"DataFrame shape: {df.shape}")
    print(f"DataFrame dtypes:\n{df.dtypes}")
    
    # Create a temporary file
    with tempfile.NamedTemporaryFile(suffix=".bcsv", delete=False) as tmp:
        filename = tmp.name
    
    try:
        # Test writing DataFrame to BCSV
        pybcsv.write_dataframe(df, filename)
        print(f"\nSuccessfully wrote DataFrame to {filename}")
        
        # Get file size
        file_size = os.path.getsize(filename)
        print(f"File size: {file_size} bytes")
        
        # Test reading back from BCSV
        df_read = pybcsv.read_dataframe(filename)
        print(f"\nRead DataFrame back:")
        print(df_read)
        print(f"Read DataFrame shape: {df_read.shape}")
        print(f"Read DataFrame dtypes:\n{df_read.dtypes}")
        
        # Compare the DataFrames
        print("\nComparing original vs read DataFrame:")
        print(f"DataFrames equal: {df.equals(df_read)}")
        
        # Check column by column
        for col in df.columns:
            if col in df_read.columns:
                equal = df[col].equals(df_read[col])
                print(f"  Column '{col}' equal: {equal}")
                if not equal:
                    print(f"    Original: {df[col].tolist()}")
                    print(f"    Read:     {df_read[col].tolist()}")
            else:
                print(f"  Column '{col}' missing in read DataFrame")
        
    except Exception as e:
        print(f"Error during pandas test: {e}")
        import traceback
        traceback.print_exc()
        
    finally:
        # Clean up
        if os.path.exists(filename):
            os.unlink(filename)
    
    print("\nPandas integration test completed!")

def test_csv_conversion():
    """Test CSV to BCSV and back conversion"""
    print("\n" + "="*50)
    print("Testing CSV conversion functionality...")
    
    # Create a sample CSV
    csv_data = """id,name,value
1,Alice,123.45
2,Bob,678.90
3,Charlie,111.22"""
    
    with tempfile.NamedTemporaryFile(mode='w', suffix=".csv", delete=False) as csv_tmp:
        csv_filename = csv_tmp.name
        csv_tmp.write(csv_data)
    
    with tempfile.NamedTemporaryFile(suffix=".bcsv", delete=False) as bcsv_tmp:
        bcsv_filename = bcsv_tmp.name
    
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as csv_out_tmp:
        csv_out_filename = csv_out_tmp.name
    
    try:
        # Convert CSV to BCSV
        print(f"Converting {csv_filename} to {bcsv_filename}")
        pybcsv.from_csv(csv_filename, bcsv_filename)
        
        csv_size = os.path.getsize(csv_filename)
        bcsv_size = os.path.getsize(bcsv_filename)
        print(f"CSV size: {csv_size} bytes")
        print(f"BCSV size: {bcsv_size} bytes")
        print(f"Compression ratio: {csv_size/bcsv_size:.2f}x")
        
        # Convert back to CSV
        print(f"Converting {bcsv_filename} back to {csv_out_filename}")
        pybcsv.to_csv(bcsv_filename, csv_out_filename)
        
        # Read and compare
        with open(csv_filename, 'r') as f:
            original_csv = f.read().strip()
        
        with open(csv_out_filename, 'r') as f:
            converted_csv = f.read().strip()
        
        print(f"Original CSV:\n{original_csv}")
        print(f"Converted CSV:\n{converted_csv}")
        print(f"CSV conversion successful: {original_csv == converted_csv}")
        
    except Exception as e:
        print(f"Error during CSV conversion test: {e}")
        import traceback
        traceback.print_exc()
        
    finally:
        # Clean up
        for filename in [csv_filename, bcsv_filename, csv_out_filename]:
            if os.path.exists(filename):
                os.unlink(filename)
    
    print("CSV conversion test completed!")

if __name__ == "__main__":
    test_pandas_integration()
    test_csv_conversion()