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


def _get_bcsv_type_from_pandas_dtype(dtype: 'pd.Series.dtype') -> ColumnType:
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


def write_dataframe(df: 'pd.DataFrame', 
                   filename: str, 
                   compression: bool = True,
                   type_hints: Optional[Dict[str, ColumnType]] = None) -> None:
    """
    Write a pandas DataFrame to a BCSV file.
    
    Args:
        df: The pandas DataFrame to write
        filename: Output BCSV filename
        compression: Whether to use compression (default: True)
        type_hints: Optional dictionary mapping column names to specific BCSV types
    """
    if not PANDAS_AVAILABLE:
        raise ImportError("pandas is not available. Please install pandas to use this function.")
    
    # Create layout
    layout = Layout()
    
    for col_name in df.columns:
        if type_hints and col_name in type_hints:
            col_type = type_hints[col_name]
        else:
            col_type = _get_bcsv_type_from_pandas_dtype(df[col_name].dtype)
        
        layout.add_column(str(col_name), col_type)
    
    # Set compression flag
    flags = FileFlags.COMPRESSED if compression else FileFlags.NONE
    
    # Write data
    writer = Writer(layout)
    try:
        if not writer.open(filename):
            raise RuntimeError(f"Failed to open file for writing: {filename}")
        
        for _, row in df.iterrows():
            row_values = []
            for i, (col_name, col_def) in enumerate(zip(df.columns, layout.get_column_types())):
                value = row.iloc[i]
                
                # Handle NaN values
                if pd.isna(value):
                    if col_def == ColumnType.STRING:
                        value = ""
                    elif col_def in [ColumnType.FLOAT, ColumnType.DOUBLE]:
                        value = 0.0
                    elif col_def in [ColumnType.INT8, ColumnType.INT16, ColumnType.INT32, ColumnType.INT64,
                                   ColumnType.UINT8, ColumnType.UINT16, ColumnType.UINT32, ColumnType.UINT64]:
                        value = 0
                    elif col_def == ColumnType.BOOL:
                        value = False
                    else:
                        value = ""
                
                # Convert to appropriate Python type
                if col_def == ColumnType.BOOL:
                    row_values.append(bool(value))
                elif col_def in [ColumnType.INT8, ColumnType.INT16, ColumnType.INT32, ColumnType.INT64,
                               ColumnType.UINT8, ColumnType.UINT16, ColumnType.UINT32, ColumnType.UINT64]:
                    row_values.append(int(value))
                elif col_def in [ColumnType.FLOAT, ColumnType.DOUBLE]:
                    row_values.append(float(value))
                else:
                    row_values.append(str(value))
            
            writer.write_row(row_values)
    finally:
        writer.close()


def read_dataframe(filename: str, 
                  columns: Optional[list] = None,
                  optimize_dtypes: bool = True) -> 'pd.DataFrame':
    """
    Read a BCSV file into a pandas DataFrame.
    
    Args:
        filename: BCSV file to read
        columns: Optional list of column names to read (default: all columns)
        optimize_dtypes: Whether to optimize pandas dtypes based on BCSV types
        
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
            
            for col_name in columns:
                if col_name in all_column_names:
                    idx = all_column_names.index(col_name)
                    column_indices.append(idx)
                    filtered_names.append(col_name)
                    filtered_types.append(all_column_types[idx])
                else:
                    warnings.warn(f"Column '{col_name}' not found in BCSV file", UserWarning)
            
            column_names = filtered_names
            column_types = filtered_types
        else:
            column_indices = list(range(len(all_column_names)))
            column_names = all_column_names
            column_types = all_column_types
        
        # Read all data
        data = reader.read_all()
        
        # Filter columns if needed
        if columns is not None and column_indices:
            filtered_data = []
            for row in data:
                filtered_row = [row[i] for i in column_indices]
                filtered_data.append(filtered_row)
            data = filtered_data
        
        # Create DataFrame
        if not data:
            # Empty DataFrame with correct column structure
            df = pd.DataFrame(columns=column_names)
        else:
            df = pd.DataFrame(data, columns=column_names)
        
        # Optimize dtypes if requested
        if optimize_dtypes:
            for col_name, col_type in zip(column_names, column_types):
                target_dtype = _get_pandas_dtype_from_bcsv_type(col_type)
                try:
                    df[col_name] = df[col_name].astype(target_dtype)
                except (ValueError, TypeError) as e:
                    warnings.warn(f"Could not convert column '{col_name}' to {target_dtype}: {e}", UserWarning)
        
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
            compression: bool = True,
            type_hints: Optional[Dict[str, ColumnType]] = None,
            **csv_kwargs) -> None:
    """
    Convert a CSV file to BCSV format.
    
    Args:
        csv_filename: Input CSV file
        bcsv_filename: Output BCSV file
        compression: Whether to use compression (default: True)
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
    write_dataframe(df, bcsv_filename, compression=compression, type_hints=type_hints)