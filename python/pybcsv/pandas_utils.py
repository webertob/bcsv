# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""Pandas integration utilities for pybcsv."""

import numpy as np
from typing import Optional, Union, Dict, Any
import warnings

try:
    import pandas as pd
    PANDAS_AVAILABLE = True
except ImportError:
    PANDAS_AVAILABLE = False
    pd = None

from . import Layout, ColumnType, Writer, Reader, FileFlags


def _get_bcsv_type_from_pandas_dtype(dtype) -> ColumnType:
    """Convert pandas dtype to BCSV ColumnType."""
    if pd.api.types.is_bool_dtype(dtype):
        return ColumnType.BOOL
    elif pd.api.types.is_integer_dtype(dtype):
        if dtype == np.int8:
            return ColumnType.INT8
        elif dtype == np.int16:
            return ColumnType.INT16
        elif dtype == np.int32:
            return ColumnType.INT32
        elif dtype == np.int64:
            return ColumnType.INT64
        elif dtype == np.uint8:
            return ColumnType.UINT8
        elif dtype == np.uint16:
            return ColumnType.UINT16
        elif dtype == np.uint32:
            return ColumnType.UINT32
        elif dtype == np.uint64:
            return ColumnType.UINT64
        else:
            # Default to INT64 for other integer types
            return ColumnType.INT64
    elif pd.api.types.is_float_dtype(dtype):
        if dtype == np.float32:
            return ColumnType.FLOAT
        else:
            return ColumnType.DOUBLE
    elif pd.api.types.is_string_dtype(dtype) or pd.api.types.is_object_dtype(dtype):
        return ColumnType.STRING
    else:
        # Default to STRING for unknown types
        warnings.warn(f"Unknown dtype {dtype}, defaulting to STRING", UserWarning)
        return ColumnType.STRING


def _get_pandas_dtype_from_bcsv_type(col_type: ColumnType) -> Union[str, np.dtype]:
    """Convert BCSV ColumnType to pandas dtype."""
    type_mapping = {
        ColumnType.BOOL: bool,
        ColumnType.INT8: np.int8,
        ColumnType.INT16: np.int16,
        ColumnType.INT32: np.int32,
        ColumnType.INT64: np.int64,
        ColumnType.UINT8: np.uint8,
        ColumnType.UINT16: np.uint16,
        ColumnType.UINT32: np.uint32,
        ColumnType.UINT64: np.uint64,
        ColumnType.FLOAT: np.float32,
        ColumnType.DOUBLE: np.float64,
        ColumnType.STRING: str,
    }
    return type_mapping.get(col_type, str)


def write_dataframe_optimized(df, 
                            filename: str, 
                            compression_level: int = 1,
                            type_hints: Optional[Dict[str, ColumnType]] = None,
                            batch_size: int = 10000) -> None:
    """
    Write a pandas DataFrame to a BCSV file using optimized vectorized operations.
    
    Args:
        df: The pandas DataFrame to write
        filename: Output BCSV filename
        compression_level: Compression level (0=no compression, 1-9=LZ4 compression level, default: 1)
        type_hints: Optional dictionary mapping column names to specific BCSV types
        batch_size: Number of rows to process in each batch (default: 10000)
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    
    # Create layout
    layout = Layout()
    column_types = []
    
    for col_name in df.columns:
        if type_hints and col_name in type_hints:
            col_type = type_hints[col_name]
        else:
            col_type = _get_bcsv_type_from_pandas_dtype(df[col_name].dtype)
        
        layout.add_column(str(col_name), col_type)
        column_types.append(col_type)
    
    # Write data
    writer = Writer(layout)
    try:
        if not writer.open(filename, True, compression_level):
            raise RuntimeError(f"Failed to open file for writing: {filename}")
        
        # Process data in batches for memory efficiency
        total_rows = len(df)
        
        for start_idx in range(0, total_rows, batch_size):
            end_idx = min(start_idx + batch_size, total_rows)
            batch_df = df.iloc[start_idx:end_idx]
            
            # Convert entire batch to numpy arrays for vectorized processing
            batch_data = []
            
            for i, (col_name, col_type) in enumerate(zip(df.columns, column_types)):
                col_data = batch_df.iloc[:, i]
                
                # Handle NaN values vectorized
                if col_data.isna().any():
                    if col_type == ColumnType.STRING:
                        col_data = col_data.fillna("")
                    elif col_type in [ColumnType.FLOAT, ColumnType.DOUBLE]:
                        col_data = col_data.fillna(0.0)
                    elif col_type in [ColumnType.INT8, ColumnType.INT16, ColumnType.INT32, ColumnType.INT64,
                                    ColumnType.UINT8, ColumnType.UINT16, ColumnType.UINT32, ColumnType.UINT64]:
                        col_data = col_data.fillna(0)
                    elif col_type == ColumnType.BOOL:
                        col_data = col_data.fillna(False)
                    else:
                        col_data = col_data.fillna("")
                
                # Convert to appropriate numpy array with proper dtype
                if col_type == ColumnType.BOOL:
                    array_data = col_data.astype(bool).values
                elif col_type in [ColumnType.INT8, ColumnType.INT16, ColumnType.INT32, ColumnType.INT64,
                                ColumnType.UINT8, ColumnType.UINT16, ColumnType.UINT32, ColumnType.UINT64]:
                    array_data = col_data.astype(int).values
                elif col_type in [ColumnType.FLOAT, ColumnType.DOUBLE]:
                    array_data = col_data.astype(float).values
                else:
                    array_data = col_data.astype(str).values
                
                batch_data.append(array_data)
            
            # Write batch using vectorized operations - convert to row-major format
            batch_rows = len(batch_df)
            for row_idx in range(batch_rows):
                row_values = [batch_data[col_idx][row_idx] for col_idx in range(len(batch_data))]
                writer.write_row(row_values)
    
    finally:
        writer.close()


def write_dataframe_ultra_optimized(df, 
                                   filename: str, 
                                   compression_level: int = 1,
                                   type_hints: Optional[Dict[str, ColumnType]] = None) -> None:
    """
    Write a pandas DataFrame to a BCSV file using ultra-optimized operations.
    
    This version minimizes Python/C++ boundary crossings and eliminates
    temporary object creation by preparing all data in numpy arrays.
    
    Args:
        df: The pandas DataFrame to write
        filename: Output BCSV filename
        compression_level: Compression level (0=no compression, 1-9=LZ4 compression level, default: 1)
        type_hints: Optional dictionary mapping column names to specific BCSV types
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    
    # Create layout
    layout = Layout()
    column_types = []
    
    for col_name in df.columns:
        if type_hints and col_name in type_hints:
            col_type = type_hints[col_name]
        else:
            col_type = _get_bcsv_type_from_pandas_dtype(df[col_name].dtype)
        
        layout.add_column(str(col_name), col_type)
        column_types.append(col_type)
    
    # Pre-process all data into optimized numpy arrays with exact dtypes
    # This eliminates type conversion overhead during writing
    processed_columns = []
    
    for i, (col_name, col_type) in enumerate(zip(df.columns, column_types)):
        col_data = df.iloc[:, i]
        
        # Handle NaN values vectorized - use fillna with optimal values
        if col_data.isna().any():
            if col_type == ColumnType.STRING:
                col_data = col_data.fillna("")
            elif col_type in [ColumnType.FLOAT, ColumnType.DOUBLE]:
                col_data = col_data.fillna(0.0)
            elif col_type in [ColumnType.INT8, ColumnType.INT16, ColumnType.INT32, ColumnType.INT64,
                            ColumnType.UINT8, ColumnType.UINT16, ColumnType.UINT32, ColumnType.UINT64]:
                col_data = col_data.fillna(0)
            elif col_type == ColumnType.BOOL:
                col_data = col_data.fillna(False)
            else:
                col_data = col_data.fillna("")
        
        # Convert to numpy array with exact target dtype to avoid repeated conversions
        if col_type == ColumnType.BOOL:
            array_data = col_data.astype(bool, copy=False)  # copy=False avoids unnecessary copying
        elif col_type == ColumnType.INT8:
            array_data = col_data.astype(np.int8, copy=False)
        elif col_type == ColumnType.INT16:
            array_data = col_data.astype(np.int16, copy=False)
        elif col_type == ColumnType.INT32:
            array_data = col_data.astype(np.int32, copy=False)
        elif col_type == ColumnType.INT64:
            array_data = col_data.astype(np.int64, copy=False)
        elif col_type == ColumnType.UINT8:
            array_data = col_data.astype(np.uint8, copy=False)
        elif col_type == ColumnType.UINT16:
            array_data = col_data.astype(np.uint16, copy=False)
        elif col_type == ColumnType.UINT32:
            array_data = col_data.astype(np.uint32, copy=False)
        elif col_type == ColumnType.UINT64:
            array_data = col_data.astype(np.uint64, copy=False)
        elif col_type == ColumnType.FLOAT:
            array_data = col_data.astype(np.float32, copy=False)
        elif col_type == ColumnType.DOUBLE:
            array_data = col_data.astype(np.float64, copy=False)
        else:  # STRING
            array_data = col_data.astype(str, copy=False)
        
        # Store raw numpy array values for direct indexing (eliminates pandas Series overhead)
        processed_columns.append(array_data.values)
    
    # Write data using C++ optimized batch writer
    writer = Writer(layout)
    try:
        if not writer.open(filename, True, compression_level):
            raise RuntimeError(f"Failed to open file for writing: {filename}")
        
        # Convert to list of lists for batch writing (eliminates row-by-row overhead)
        num_rows = len(df)
        num_cols = len(processed_columns)
        
        # Pre-allocate list with exact size to avoid growth overhead
        all_rows = [None] * num_rows
        
        # Vectorized row creation using numpy advanced indexing
        for row_idx in range(num_rows):
            # Use list comprehension with direct numpy array access (fastest Python approach)
            all_rows[row_idx] = [processed_columns[col_idx][row_idx] for col_idx in range(num_cols)]
        
        # Use optimized batch writer
        writer.write_rows(all_rows)
    
    finally:
        writer.close()


# Keep the original function for compatibility
write_dataframe = write_dataframe_ultra_optimized


def read_dataframe(filename: str, 
                  columns: Optional[list] = None,
                  optimize_dtypes: bool = True,
                  chunk_size: Optional[int] = None):
    """
    Read a BCSV file into a pandas DataFrame with optimized memory usage.
    
    Args:
        filename: BCSV file to read
        columns: Optional list of column names to read (default: all columns)
        optimize_dtypes: Whether to optimize pandas dtypes based on BCSV types
        chunk_size: If specified, read data in chunks (for large files)
        
    Returns:
        pandas DataFrame containing the data
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    
    reader = Reader()
    try:
        if not reader.open(filename):
            raise RuntimeError(f"Failed to open file for reading: {filename}")
            
        layout = reader.layout()
        
        # Get column information
        all_column_names = layout.get_column_names()
        all_column_types = layout.get_column_types()
        
        # Filter columns if specified
        if columns is not None:
            column_indices = []
            filtered_names = []
            filtered_types = []
            
            # Use set for faster lookup
            column_set = set(columns)
            for idx, col_name in enumerate(all_column_names):
                if col_name in column_set:
                    column_indices.append(idx)
                    filtered_names.append(col_name)
                    filtered_types.append(all_column_types[idx])
            
            # Warn about missing columns
            missing_cols = column_set - set(filtered_names)
            if missing_cols:
                warnings.warn(f"Columns not found in BCSV file: {missing_cols}", UserWarning)
            
            column_names = filtered_names
            column_types = filtered_types
        else:
            column_indices = None
            column_names = all_column_names
            column_types = all_column_types
        
        # Read all data using optimized reader
        data = reader.read_all()
        
        if not data:
            # Empty DataFrame with correct column structure and optimized dtypes
            dtype_dict = {}
            if optimize_dtypes:
                for col_name, col_type in zip(column_names, column_types):
                    dtype_dict[col_name] = _get_pandas_dtype_from_bcsv_type(col_type)
            return pd.DataFrame(columns=column_names).astype(dtype_dict)
        
        # Filter columns efficiently using numpy-style indexing if needed
        if column_indices is not None:
            # Pre-allocate filtered data list
            filtered_data = [None] * len(data)
            for i, row in enumerate(data):
                filtered_data[i] = [row[idx] for idx in column_indices]
            data = filtered_data
        
        # Create DataFrame with pre-computed dtypes for optimal memory usage
        if optimize_dtypes:
            # Create DataFrame with object dtype first (fastest creation)
            df = pd.DataFrame(data, columns=column_names)
            
            # Convert columns to optimal dtypes in batch
            dtype_conversions = {}
            for col_name, col_type in zip(column_names, column_types):
                target_dtype = _get_pandas_dtype_from_bcsv_type(col_type)
                dtype_conversions[col_name] = target_dtype
            
            # Use astype with dictionary for vectorized conversion
            try:
                df = df.astype(dtype_conversions, copy=False)  # copy=False avoids unnecessary copying
            except (ValueError, TypeError) as e:
                warnings.warn(f"Could not convert dtypes in batch: {e}. Falling back to individual column conversion.", UserWarning)
                # Fallback to individual column conversion
                for col_name, target_dtype in dtype_conversions.items():
                    try:
                        df[col_name] = df[col_name].astype(target_dtype, copy=False)
                    except (ValueError, TypeError) as e:
                        warnings.warn(f"Could not convert column '{col_name}' to {target_dtype}: {e}", UserWarning)
        else:
            df = pd.DataFrame(data, columns=column_names)
        
        return df
    finally:
        reader.close()


def to_csv(bcsv_filename: str, csv_filename: str, **csv_kwargs) -> None:
    """
    Convert a BCSV file to CSV format.
    
    Args:
        bcsv_filename: Input BCSV file
        csv_filename: Output CSV file
        **csv_kwargs: Additional arguments passed to pandas.DataFrame.to_csv()
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    
    df = read_dataframe(bcsv_filename)
    
    # Set default CSV parameters
    csv_params = {
        'index': False,
        'encoding': 'utf-8'
    }
    csv_params.update(csv_kwargs)
    
    df.to_csv(csv_filename, **csv_params)


def from_csv(csv_filename: str, 
            bcsv_filename: str,
            compression_level: int = 1,
            type_hints: Optional[Dict[str, ColumnType]] = None,
            **csv_kwargs) -> None:
    """
    Convert a CSV file to BCSV format.
    
    Args:
        csv_filename: Input CSV file
        bcsv_filename: Output BCSV file
        compression_level: Compression level (0=no compression, 1-9=LZ4 compression level, default: 1)
        type_hints: Optional dictionary mapping column names to specific BCSV types
        **csv_kwargs: Additional arguments passed to pandas.read_csv()
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    
    # Set default CSV reading parameters
    csv_params = {
        'encoding': 'utf-8'
    }
    csv_params.update(csv_kwargs)
    
    df = pd.read_csv(csv_filename, **csv_params)
    write_dataframe(df, bcsv_filename, compression_level=compression_level, type_hints=type_hints)