"""Type stubs for pybcsv — re-exports from the C++ extension and Python utils."""

from typing import Optional, Dict

from ._bcsv import (
    ColumnType as ColumnType,
    ColumnDefinition as ColumnDefinition,
    Layout as Layout,
    Writer as Writer,
    Reader as Reader,
    ReaderDirectAccess as ReaderDirectAccess,
    Sampler as Sampler,
    CsvWriter as CsvWriter,
    CsvReader as CsvReader,
    FileFlags as FileFlags,
    SamplerMode as SamplerMode,
    SamplerErrorPolicy as SamplerErrorPolicy,
    SamplerCompileResult as SamplerCompileResult,
    read_columns as read_columns,
    write_columns as write_columns,
    read_to_arrow as read_to_arrow,
    write_from_arrow as write_from_arrow,
    type_to_string as type_to_string,
    BOOL as BOOL,
    UINT8 as UINT8,
    UINT16 as UINT16,
    UINT32 as UINT32,
    UINT64 as UINT64,
    INT8 as INT8,
    INT16 as INT16,
    INT32 as INT32,
    INT64 as INT64,
    FLOAT as FLOAT,
    DOUBLE as DOUBLE,
    STRING as STRING,
)

__version__: str
_BINDINGS_AVAILABLE: bool
_PANDAS_UTILS_AVAILABLE: bool
_POLARS_UTILS_AVAILABLE: bool

def write_dataframe(
    df: object,
    filename: str,
    compression_level: int = 1,
    row_codec: str = "delta",
    type_hints: Optional[Dict[str, ColumnType]] = None,
    strict: bool = False,
) -> None: ...

def read_dataframe(
    filename: str,
    columns: Optional[list] = None,
    optimize_dtypes: bool = True,
) -> object: ...

def to_csv(bcsv_filename: str, csv_filename: str, **csv_kwargs: object) -> None: ...

def from_csv(
    csv_filename: str,
    bcsv_filename: str,
    compression_level: int = 1,
    type_hints: Optional[Dict[str, ColumnType]] = None,
    **csv_kwargs: object,
) -> None: ...

def read_polars(
    filename: str,
    columns: object = None,
    chunk_size: int = 0,
) -> object: ...

def write_polars(
    df: object,
    filename: str,
    row_codec: str = "delta",
    compression_level: int = 1,
) -> None: ...
