#!/usr/bin/env python3

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""
Advanced BCSV Usage Example

Demonstrates features beyond basic read/write:
- ReaderDirectAccess (random-access reads)
- Sampler (server-side filtering and column projection)
- CsvWriter / CsvReader (native CSV I/O with typed layouts)
- FileFlags and codec selection
- Columnar I/O (read_columns / write_columns)
- type_to_string utility
"""

import os
import pybcsv
import numpy as np


# ── Helpers ──────────────────────────────────────────────────────────────

def _write_sample_file(filename: str, n: int = 200) -> pybcsv.Layout:
    """Write a sample BCSV file and return its layout."""
    layout = pybcsv.Layout()
    layout.add_column("timestamp", pybcsv.ColumnType.INT64)
    layout.add_column("sensor_id", pybcsv.ColumnType.UINT16)
    layout.add_column("temperature", pybcsv.ColumnType.DOUBLE)
    layout.add_column("label", pybcsv.ColumnType.STRING)
    layout.add_column("valid", pybcsv.ColumnType.BOOL)

    with pybcsv.Writer(layout) as w:
        w.open(filename)
        for i in range(n):
            w.write_row([
                1_700_000_000 + i * 60,    # timestamps 1 min apart
                i % 4,                      # 4 sensors cycling
                20.0 + (i % 30) * 0.1,     # temperature 20.0–22.9
                f"zone_{i % 3}",           # 3 zone labels
                i % 5 != 0,               # 80 % valid
            ])
    return layout


# ── 1. ReaderDirectAccess ────────────────────────────────────────────────

def demo_direct_access(filename: str):
    """Random-access reads by row index — no scanning required."""
    print("\n=== 1. ReaderDirectAccess ===")

    with pybcsv.ReaderDirectAccess() as da:
        da.open(filename)
        total = len(da)
        print(f"  Total rows: {total}")
        print(f"  Version:    {da.version_string()}")
        print(f"  Created:    {da.creation_time()}")

        # Read specific rows by index (O(1) seek)
        for idx in [0, total // 2, total - 1]:
            row = da[idx]
            print(f"  Row[{idx:>3}]: ts={row[0]}, sensor={row[1]}, "
                  f"temp={row[2]:.1f}, label={row[3]}, valid={row[4]}")

        # Also via .read()
        row = da.read(10)
        print(f"  Row[ 10]: {row}")


# ── 2. Sampler ───────────────────────────────────────────────────────────

def demo_sampler(filename: str):
    """Filter rows and project columns with the bytecode VM."""
    print("\n=== 2. Sampler ===")

    reader = pybcsv.Reader()
    reader.open(filename)

    sampler = pybcsv.Sampler(reader)

    # Set a filter: only rows where valid==true AND temperature > 21.0
    result = sampler.set_conditional(
        'X[0]["valid"] == 1 && X[0]["temperature"] > 21.0'
    )
    if not result:
        print(f"  Compile error: {result.error_msg}")
        reader.close()
        return

    # Project to just timestamp and temperature
    result = sampler.set_selection(
        'X[0]["timestamp"], X[0]["temperature"]'
    )
    if not result:
        print(f"  Selection error: {result.error_msg}")
        reader.close()
        return

    # Inspect output layout
    out_layout = sampler.output_layout()
    print(f"  Output columns: {out_layout.column_count()}")
    for i in range(out_layout.column_count()):
        print(f"    {out_layout.column_name(i)}: "
              f"{pybcsv.type_to_string(out_layout.column_type(i))}")

    # Iterate filtered rows
    count = 0
    for row in sampler:
        count += 1
        if count <= 3:
            print(f"  Matched row {count}: ts={row[0]}, temp={row[1]:.1f}")

    print(f"  Total matched: {count} rows")

    # Show bytecode disassembly
    dis = sampler.disassemble()
    print(f"  Bytecode ({len(dis)} chars):")
    for line in dis.strip().split("\n")[:5]:
        print(f"    {line}")
    if dis.count("\n") > 5:
        print(f"    ... ({dis.count(chr(10)) - 5} more lines)")

    reader.close()


# ── 3. CsvWriter / CsvReader ────────────────────────────────────────────

def demo_csv_io(layout: pybcsv.Layout):
    """Native CSV I/O using the same Layout-based schema."""
    print("\n=== 3. CsvWriter / CsvReader ===")
    csv_file = "example_advanced.csv"

    try:
        # Write CSV with typed layout
        with pybcsv.CsvWriter(layout, delimiter=',', decimal_sep='.') as cw:
            cw.open(csv_file, overwrite=True, include_header=True)
            cw.write_row([1_700_000_000, 0, 20.5, "zone_0", True])
            cw.write_row([1_700_000_060, 1, 21.3, "zone_1", False])
            cw.write_row([1_700_000_120, 2, 22.0, "zone_2", True])

        print(f"  Wrote 3 rows to {csv_file}")

        # Read it back
        with open(csv_file) as f:
            print(f"  CSV content:")
            for line in f:
                print(f"    {line.rstrip()}")

        # Read with CsvReader
        cr = pybcsv.CsvReader(layout, delimiter=',', decimal_sep='.')
        cr.open(csv_file, has_header=True)
        rows = []
        for row in cr:
            rows.append(row)
        cr.close()
        print(f"  Read back {len(rows)} rows via CsvReader")
        for r in rows:
            print(f"    {r}")

    finally:
        if os.path.exists(csv_file):
            os.remove(csv_file)


# ── 4. FileFlags & Codec Selection ──────────────────────────────────────

def demo_file_flags():
    """Show different codec combinations via FileFlags."""
    print("\n=== 4. FileFlags & Codec Selection ===")

    layout = pybcsv.Layout()
    layout.add_column("x", pybcsv.ColumnType.DOUBLE)
    layout.add_column("y", pybcsv.ColumnType.DOUBLE)

    rows = [[float(i), float(i) * 2.5] for i in range(500)]

    configs = [
        ("flat + stream (no compression)", "flat", pybcsv.FileFlags.NONE),
        ("delta + batch LZ4 (default)",    "delta", pybcsv.FileFlags.BATCH_COMPRESS),
        ("zoh + stream LZ4",               "zoh",   pybcsv.FileFlags.NONE),
    ]

    for label, codec, flags in configs:
        fname = f"example_flags_{codec}.bcsv"
        try:
            with pybcsv.Writer(layout, row_codec=codec) as w:
                w.open(fname, compression_level=1, flags=flags)
                w.write_rows(rows)
            size = os.path.getsize(fname)
            print(f"  {label:40s}  → {size:>6,} bytes")
        finally:
            if os.path.exists(fname):
                os.remove(fname)


# ── 5. Columnar I/O ─────────────────────────────────────────────────────

def demo_columnar_io():
    """Low-level columnar I/O — write/read numpy arrays directly."""
    print("\n=== 5. Columnar I/O (read_columns / write_columns) ===")
    fname = "example_columnar.bcsv"

    try:
        # Prepare columnar data as numpy arrays
        n = 1000
        columns = {
            "index": np.arange(n, dtype=np.int32),
            "value": np.random.default_rng(42).standard_normal(n).astype(np.float64),
            "tag":   [f"item_{i % 10}" for i in range(n)],
        }
        col_order = ["index", "value", "tag"]
        col_types = [pybcsv.ColumnType.INT32, pybcsv.ColumnType.DOUBLE,
                     pybcsv.ColumnType.STRING]

        # Write columns
        pybcsv.write_columns(fname, columns, col_order, col_types,
                             row_codec="delta", compression_level=1)
        size = os.path.getsize(fname)
        print(f"  Wrote {n} rows (3 columns) → {size:,} bytes")

        # Read columns back
        result = pybcsv.read_columns(fname)
        print(f"  Read back {len(result)} columns: {list(result.keys())}")
        print(f"    index dtype: {result['index'].dtype}, shape: {result['index'].shape}")
        print(f"    value dtype: {result['value'].dtype}, shape: {result['value'].shape}")
        print(f"    tag:   {type(result['tag']).__name__} with {len(result['tag'])} items")

        # Verify round-trip
        assert np.array_equal(result["index"], columns["index"])
        assert np.allclose(result["value"], columns["value"])
        assert result["tag"] == columns["tag"]
        print("  Round-trip verified ✓")

    finally:
        if os.path.exists(fname):
            os.remove(fname)


# ── 6. type_to_string & Layout Inspection ────────────────────────────────

def demo_type_inspection():
    """Inspect layouts and convert types to strings."""
    print("\n=== 6. type_to_string & Layout Inspection ===")

    layout = pybcsv.Layout()
    layout.add_column("a", pybcsv.ColumnType.INT32)
    layout.add_column("b", pybcsv.ColumnType.DOUBLE)
    layout.add_column("c", pybcsv.ColumnType.STRING)

    # Iterate layout with __getitem__ (returns ColumnDefinition)
    for i in range(len(layout)):
        col = layout[i]
        type_str = pybcsv.type_to_string(col.type)
        print(f"  Column {i}: name={col.name!r}, type={type_str}")

    # Bulk accessors
    print(f"  Names: {layout.get_column_names()}")
    print(f"  Types: {[pybcsv.type_to_string(t) for t in layout.get_column_types()]}")

    # Lookup by name
    assert layout.has_column("b")
    assert layout.column_index("b") == 1
    print(f"  has_column('b')={layout.has_column('b')}, "
          f"column_index('b')={layout.column_index('b')}")


# ── Main ─────────────────────────────────────────────────────────────────

def main():
    print("=== Advanced BCSV Usage Examples ===")
    bcsv_file = "example_advanced.bcsv"

    try:
        layout = _write_sample_file(bcsv_file, n=200)

        demo_direct_access(bcsv_file)
        demo_sampler(bcsv_file)
        demo_csv_io(layout)
        demo_file_flags()
        demo_columnar_io()
        demo_type_inspection()

        print("\n=== All examples completed ===")

    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()

    finally:
        if os.path.exists(bcsv_file):
            os.remove(bcsv_file)


if __name__ == "__main__":
    main()
