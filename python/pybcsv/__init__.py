# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""pybcsv - Python bindings for the BCSV library."""

import builtins as _builtins
import os as _os

from .__version__ import __version__

# Import the compiled extension module — fail immediately if not available
try:
    from ._bcsv import *

    _BINDINGS_AVAILABLE = True
except ImportError:
    try:
        from _bcsv import *  # fallback for legacy in-tree builds

        _BINDINGS_AVAILABLE = True
    except ImportError as _exc:
        raise ImportError(
            "pybcsv native extension (_bcsv) is not available. "
            "Install a pre-built wheel (pip install pybcsv) or build from source. "
            "See https://github.com/webertob/bcsv/tree/main/python"
        ) from _exc

# Bundled CLI tools (csv2bcsv, bcsv2csv, ...) — see pybcsv.tools.run()/path()
from . import tools

# Try to import pandas utilities if pandas is available
try:
    from .pandas_utils import write_dataframe, read_dataframe, to_csv, from_csv

    _PANDAS_UTILS_AVAILABLE = True
except ImportError:
    _PANDAS_UTILS_AVAILABLE = False

    # Create stub functions that raise ImportError
    def write_dataframe(*args, **kwargs):
        raise ImportError(
            "pandas is not available. Please install pandas to use DataFrame functions."
        )

    def read_dataframe(*args, **kwargs):
        raise ImportError(
            "pandas is not available. Please install pandas to use DataFrame functions."
        )

    def to_csv(*args, **kwargs):
        raise ImportError(
            "pandas is not available. Please install pandas to use CSV conversion functions."
        )

    def from_csv(*args, **kwargs):
        raise ImportError(
            "pandas is not available. Please install pandas to use CSV conversion functions."
        )


# Try to import Polars utilities if polars + pyarrow are available
try:
    from .polars_utils import read_polars, write_polars

    _POLARS_UTILS_AVAILABLE = True
except ImportError:
    _POLARS_UTILS_AVAILABLE = False

    def read_polars(*args, **kwargs):
        raise ImportError(
            "polars and pyarrow are required. Install with: pip install pybcsv[polars,arrow]"
        )

    def write_polars(*args, **kwargs):
        raise ImportError(
            "polars and pyarrow are required. Install with: pip install pybcsv[polars,arrow]"
        )


# Try to import Parquet utilities if pyarrow is available
try:
    from .parquet_utils import parquet_to_bcsv, bcsv_to_parquet

    _PARQUET_AVAILABLE = True
except ImportError:
    _PARQUET_AVAILABLE = False

    def parquet_to_bcsv(*args, **kwargs):
        raise ImportError(
            "pyarrow is required for Parquet conversion. "
            "Install with: pip install pybcsv[arrow]"
        )

    def bcsv_to_parquet(*args, **kwargs):
        raise ImportError(
            "pyarrow is required for Parquet conversion. "
            "Install with: pip install pybcsv[arrow]"
        )


# ---------------------------------------------------------------------------
# pybcsv.open() — auto-detecting façade over .bcsv / .csv / .csv.lz4 (ADR-0009)
# ---------------------------------------------------------------------------

_BCSV_MAGIC = b"BCSV"              # binary magic bytes, big-endian B,C,S,V
_LZ4_MAGIC = b"\x04\x22\x4d\x18"   # standard LZ4 frame magic, little-endian


def _lz4_frame_sniff(head):
    """Mirror of C++ lz4_frame_sniff: frame magic or skippable frame magic."""
    if head[:4] == _LZ4_MAGIC:
        return True
    # Skippable frames: 0x184D2A50..0x184D2A5F (LE on the wire).
    b0, b1, b2, b3 = head[0], head[1], head[2], head[3]
    return b3 == 0x18 and b2 == 0x4D and b1 == 0x2A and 0x50 <= b0 <= 0x5F


def open(path, layout=None, mode="r", delimiter=None, decimal_separator=None,
         has_header=True, include_header=True, lz4_block_max_size_id=4,
         lz4_level=0, **kw):
    """Open a BCSV file, auto-detecting .bcsv / .csv / .csv.lz4.

    Like ``textio.open``, ``mode`` picks the flavour: ``r``/``w`` auto-detect
    (readers by content magic, writers by extension), ``rb``/``wb`` force
    binary, ``rt``/``wt`` force CSV text. ``layout`` is required whenever a
    CSV file is involved (text carries no schema) or a file is written.

    Read: ``ReaderDirectAccess`` for binary, ``CsvReader`` for text (plain
    or LZ4 — the frame magic is sniffed by the reader). Write: ``Writer``
    or ``CsvWriter``; with text output the LZ4 frame activates when the
    destination ends in ``.lz4``, tuned by ``lz4_block_max_size_id``
    (4=64KB/5=256KB/6=1MB/7=4MB) and ``lz4_level`` (0/1 fast, 2..12 HC).
    Returns the open object — use it as a context manager; ``**kw`` go to
    the object's ``open()`` (e.g. ``overwrite=True``).

    Access it as ``pybcsv.open``; a ``from pybcsv import *`` does rebind
    the name ``open`` in the importing namespace.
    """
    binary = {"rb": True, "wb": True}.get(mode)
    text = {"rt": True, "wt": True}.get(mode)
    if binary is None and text is None and mode not in ("r", "w"):
        raise ValueError(f"pybcsv.open: unsupported mode {mode!r} "
                         "(use r, w, rb, wb, rt or wt)")
    p = _os.fspath(path)
    writing = mode[:1] == "w"    # w, wb or wt
    if writing and layout is None:
        raise TypeError("pybcsv.open: 'layout' is required when writing")

    if writing:
        as_csv = text or (binary is None and p.endswith((".csv", ".lz4")))
        if not as_csv and (lz4_level or lz4_block_max_size_id != 4):
            raise ValueError(
                "pybcsv.open: lz4_block_max_size_id/lz4_level apply only to "
                f"CSV output; {p!r} opens as binary BCSV (use mode='wt' or a "
                ".csv/.csv.lz4 name, or drop the knobs)")
    else:
        head = _peek_head(p)
        if head[:4] == _BCSV_MAGIC:
            as_csv = False
        elif _lz4_frame_sniff(head[:4]):
            as_csv = True
        else:  # unreadable/short/new: the extension decides, like native AUTO
            as_csv = not p.endswith(".bcsv")
        as_csv = binary is False or (binary is None and (text or as_csv))
        if text and head[:4] == _BCSV_MAGIC:
            # Same hazard ADR-0009 closed on the C side: pinned text must
            # never decode binary bytes (garbage rows with has_header=False).
            raise ValueError(
                f"pybcsv.open: {p!r} is a binary BCSV file "
                "(B,C,S,V magic) — mode 'rt' cannot read it; use mode='r' "
                "or 'rb' (ReaderDirectAccess)")
        if as_csv and layout is None:
            raise TypeError(
                f"pybcsv.open: 'layout' is required to read text file {p!r} "
                "(CSV carries no schema — pass a pybcsv.Layout)")

    if as_csv:
        cs = {} if delimiter is None else {"delimiter": delimiter}
        if decimal_separator is not None:
            cs["decimal_sep"] = decimal_separator
        if writing:
            w = CsvWriter(layout, **cs)
            w.set_lz4(lz4_block_max_size_id, lz4_level)  # pre-open; inert for plain .csv
            w.open(p, include_header=include_header, **kw)
            return w
        r = CsvReader(layout, **cs)
        r.open(p, has_header=has_header, **kw)
        return r
    if writing:
        w = Writer(layout, **({"row_codec": kw.pop("row_codec")}
                               if "row_codec" in kw else {}))
        w.open(p, **kw)
        return w
    r = ReaderDirectAccess()
    r.open(p, **kw)
    return r


def _peek_head(path):
    """First 4 bytes of path for magic sniffing; b'' when unreadable."""
    try:
        with _builtins.open(path, "rb") as f:
            return f.read(4)
    except OSError:
        return b""


def iter_arrow_batches(reader, *, batch_size=512000, columns=None, start_row=0):
    """Yield pa.RecordBatch objects from an already-open ReaderDirectAccess.

    Memory-bounded: exactly one batch is held at a time. `reader` must be open.
    """
    pos = start_row
    while True:
        batch = reader.read_arrow_batch(pos, batch_size, columns)
        if batch is None:
            break
        yield batch
        pos += batch.num_rows


__all__ = [
    "__version__",
    # Bundled CLI tools
    "tools",
    # Auto-detecting façade
    "open",
    # Core classes
    "Layout",
    "Writer",
    "Reader",
    "ReaderDirectAccess",
    "Sampler",
    "CsvWriter",
    "CsvReader",
    # Native defaults
    "DEFAULT_COMPRESSION_LEVEL",
    "DEFAULT_PACKET_SIZE_KB",
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
    "iter_arrow_batches",
    # Parquet interop
    "parquet_to_bcsv",
    "bcsv_to_parquet",
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
