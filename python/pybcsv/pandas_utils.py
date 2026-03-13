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

# Import columnar I/O if available (requires numpy headers in bindings)
try:
    from ._bcsv import read_columns as _read_columns, write_columns as _write_columns
    _COLUMNAR_AVAILABLE = True
except ImportError:
    try:
        from _bcsv import read_columns as _read_columns, write_columns as _write_columns
        _COLUMNAR_AVAILABLE = True
    except ImportError:
        _COLUMNAR_AVAILABLE = False

# Import Arrow interop if available
try:
    from ._bcsv import read_to_arrow as _read_to_arrow
    import pyarrow  # noqa: F401
    _ARROW_AVAILABLE = True
except ImportError:
    try:
        from _bcsv import read_to_arrow as _read_to_arrow
        import pyarrow  # noqa: F401
        _ARROW_AVAILABLE = True
    except ImportError:
        _ARROW_AVAILABLE = False


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


def write_dataframe(df, 
                    filename: str, 
                    compression_level: int = 1,
                    row_codec: str = "delta",
                    type_hints: Optional[Dict[str, ColumnType]] = None) -> None:
    """
    Write a pandas DataFrame to a BCSV file via the C++ columnar path.
    
    All data is converted to numpy arrays/string lists in Python, then passed
    to C++ write_columns which runs the entire write loop under GIL release.
    
    Args:
        df: The pandas DataFrame to write
        filename: Output BCSV filename
        compression_level: Compression level (0=no compression, 1-9=LZ4 compression level, default: 1)
        row_codec: Row codec to use ('flat', 'zoh', or 'delta', default: 'delta')
        type_hints: Optional dictionary mapping column names to specific BCSV types
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    if not _COLUMNAR_AVAILABLE:
        raise ImportError("Columnar I/O is not available in this build. "
                          "Rebuild pybcsv with numpy headers available.")

    col_order = [str(c) for c in df.columns]
    col_types = []
    columns: Dict[str, Any] = {}

    for col_name in col_order:
        if type_hints and col_name in type_hints:
            ct = type_hints[col_name]
        else:
            ct = _get_bcsv_type_from_pandas_dtype(df[col_name].dtype)
        col_types.append(ct)

        col = df[col_name]
        if col.isna().any():
            nan_cols = col.isna().sum()
            warnings.warn(
                f"Column '{col_name}' contains {nan_cols} NaN/None values. "
                f"These will be replaced with zero/False/empty-string since BCSV has no null type.",
                UserWarning, stacklevel=2)
            if ct == ColumnType.STRING:
                col = col.fillna("")
            elif ct in (ColumnType.FLOAT, ColumnType.DOUBLE):
                col = col.fillna(0.0)
            elif ct == ColumnType.BOOL:
                col = col.fillna(False)
            else:
                col = col.fillna(0)

        if ct == ColumnType.STRING:
            columns[col_name] = col.astype(str).tolist()
        else:
            columns[col_name] = np.ascontiguousarray(col.values)

    _write_columns(filename, columns, col_order, col_types,
                   row_codec, compression_level)


def read_dataframe(filename: str, 
                  columns: Optional[list] = None,
                  optimize_dtypes: bool = True):
    """
    Read a BCSV file into a pandas DataFrame with optimized memory usage.
    
    Args:
        filename: BCSV file to read
        columns: Optional list of column names to read (default: all columns)
        optimize_dtypes: Whether to optimize pandas dtypes based on BCSV types
        
    Returns:
        pandas DataFrame containing the data
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")

    # Fastest path: Arrow C Data Interface → zero-copy to_pandas
    if _ARROW_AVAILABLE:
        table = _read_to_arrow(filename, columns=columns)
        return table.to_pandas()

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
            
            column_set = set(columns)
            for idx, col_name in enumerate(all_column_names):
                if col_name in column_set:
                    column_indices.append(idx)
                    filtered_names.append(col_name)
                    filtered_types.append(all_column_types[idx])
            
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