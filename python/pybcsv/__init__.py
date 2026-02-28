# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""pybcsv - Python bindings for the BCSV library."""

from .__version__ import __version__

# Try to import the compiled extension module
try:
    from _bcsv import *
    _BINDINGS_AVAILABLE = True
except ImportError:
    _BINDINGS_AVAILABLE = False
    
    # Create stub classes that raise ImportError
    class Layout:
        def __init__(self, *args, **kwargs):
            raise ImportError("BCSV bindings are not available. Please compile the extension module.")
    
    class Writer:
        def __init__(self, *args, **kwargs):
            raise ImportError("BCSV bindings are not available. Please compile the extension module.")
    
    class Reader:
        def __init__(self, *args, **kwargs):
            raise ImportError("BCSV bindings are not available. Please compile the extension module.")
    
    class CsvWriter:
        def __init__(self, *args, **kwargs):
            raise ImportError("BCSV bindings are not available. Please compile the extension module.")
    
    class CsvReader:
        def __init__(self, *args, **kwargs):
            raise ImportError("BCSV bindings are not available. Please compile the extension module.")

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

__all__ = [
    "__version__",
    # Core classes
    "Layout",
    "Writer",
    "Reader",
    "CsvWriter",
    "CsvReader",
    # Utility functions
    "write_dataframe",
    "read_dataframe", 
    "to_csv",
    "from_csv",
]