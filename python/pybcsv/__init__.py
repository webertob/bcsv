# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""pybcsv - Python bindings for the BCSV library."""

from .__version__ import __version__

# Import the compiled extension module — fail immediately if not available
try:
    from ._bcsv import *
    _BINDINGS_AVAILABLE = True
except ImportError:
    try:
        from _bcsv import *   # fallback for legacy in-tree builds
        _BINDINGS_AVAILABLE = True
    except ImportError as _exc:
        raise ImportError(
            "pybcsv native extension (_bcsv) is not available. "
            "Install a pre-built wheel (pip install pybcsv) or build from source. "
            "See https://github.com/webertob/bcsv/tree/main/python"
        ) from _exc

# Try to import pandas utilities if pandas is available
try:
    from .pandas_utils import write_dataframe, read_dataframe, to_csv, from_csv
    _PANDAS_UTILS_AVAILABLE = True
except ImportError:
    _PANDAS_UTILS_AVAILABLE = False
    
    # Create stub functions that raise ImportError
    def write_dataframe(*args, **kwargs):
        raise ImportError("pandas is not available. Please install pandas to use DataFrame functions.")
    
    def read_dataframe(*args, **kwargs):
        raise ImportError("pandas is not available. Please install pandas to use DataFrame functions.")
    
    def to_csv(*args, **kwargs):
        raise ImportError("pandas is not available. Please install pandas to use CSV conversion functions.")
    
    def from_csv(*args, **kwargs):
        raise ImportError("pandas is not available. Please install pandas to use CSV conversion functions.")

# Try to import Polars utilities if polars + pyarrow are available
try:
    from .polars_utils import read_polars, write_polars
    _POLARS_UTILS_AVAILABLE = True
except ImportError:
    _POLARS_UTILS_AVAILABLE = False

    def read_polars(*args, **kwargs):
        raise ImportError("polars and pyarrow are required. Install with: pip install pybcsv[polars,arrow]")

    def write_polars(*args, **kwargs):
        raise ImportError("polars and pyarrow are required. Install with: pip install pybcsv[polars,arrow]")

__all__ = [
    "__version__",
    # Core classes
    "Layout",
    "Writer",
    "Reader",
    "ReaderDirectAccess",
    "Sampler",
    "CsvWriter",
    "CsvReader",
    # Enums and types
    "ColumnType",
    "ColumnDefinition",
    "FileFlags",
    "SamplerMode",
    "SamplerErrorPolicy",
    "SamplerCompileResult",
    # Columnar I/O
    "read_columns",
    "write_columns",
    # Arrow interop
    "read_to_arrow",
    "write_from_arrow",
    # Polars interop
    "read_polars",
    "write_polars",
    # Utility functions
    "type_to_string",
    "write_dataframe",
    "read_dataframe",
    "to_csv",
    "from_csv",
]