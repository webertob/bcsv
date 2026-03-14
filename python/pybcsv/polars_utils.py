# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

"""Polars integration utilities for pybcsv (via Arrow C Data Interface)."""

import os

try:
    from ._bcsv import read_to_arrow as _read_to_arrow, write_from_arrow as _write_from_arrow
except ImportError:
    from _bcsv import read_to_arrow as _read_to_arrow, write_from_arrow as _write_from_arrow

import polars as pl


def read_polars(filename: str, columns=None, chunk_size: int = 0):
    """Read a BCSV file into a Polars DataFrame via Arrow zero-copy.

    Args:
        filename: Path to the BCSV file (str or pathlib.Path).
        columns: Optional list of column names to read (default: all).
        chunk_size: If > 0, read in chunks of this many rows.

    Returns:
        polars.DataFrame
    """
    filename = os.fspath(filename)
    table = _read_to_arrow(filename, columns=columns, chunk_size=chunk_size)
    return pl.from_arrow(table)


def write_polars(df, filename: str, row_codec: str = "delta",
                 compression_level: int = 1):
    """Write a Polars DataFrame to a BCSV file via Arrow zero-copy.

    Args:
        df: polars.DataFrame to write.
        filename: Output BCSV file path (str or pathlib.Path).
        row_codec: Row codec ('flat', 'zoh', or 'delta').
        compression_level: LZ4 compression level (0 = none).
    """
    filename = os.fspath(filename)
    table = df.to_arrow()
    _write_from_arrow(filename, table, row_codec=row_codec,
                      compression_level=compression_level)
