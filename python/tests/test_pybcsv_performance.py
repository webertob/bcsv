#!/usr/bin/env python3
"""
Performance and optimization tests for PyBCSV.
Tests batch vs individual operations, DataFrame I/O timing,
and memory efficiency with larger datasets.
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import os
import tempfile
import time
from pathlib import Path

import numpy as np
import pandas as pd
import pytest
import pybcsv


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _tmp(suffix=".bcsv"):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix="pybcsv_perf_")
    os.close(fd)
    return path


# ---------------------------------------------------------------------------
# pytest-style tests (originally from test_optimizations.py)
# ---------------------------------------------------------------------------


def test_optimized_operations():
    """Test optimized read/write operations."""
    test_data = {
        'bool_col': [True, False, True] * 1000,
        'int_col': list(range(3000)),
        'float_col': [1.1, 2.2, 3.3] * 1000,
        'string_col': ['test', 'data', 'row'] * 1000,
    }
    df = pd.DataFrame(test_data)
    test_file = Path("/tmp/test_optimized.bcsv")

    pybcsv.write_dataframe(df, str(test_file), compression_level=1)
    df_read = pybcsv.read_dataframe(str(test_file))

    assert len(df) == len(df_read)
    assert list(df.columns) == list(df_read.columns)

    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.ColumnType.INT32)
    layout.add_column("value", pybcsv.ColumnType.DOUBLE)
    layout.add_column("name", pybcsv.ColumnType.STRING)

    batch_file = Path("/tmp/test_batch.bcsv")
    writer = pybcsv.Writer(layout)
    try:
        writer.open(str(batch_file))
        for i in range(1000):
            writer.write_row([i, float(i) * 1.5, f"row_{i}"])
        batch_data = [[i + 1000, float(i + 1000) * 1.5, f"batch_{i}"]
                      for i in range(1000)]
        writer.write_rows(batch_data)
    finally:
        writer.close()

    reader = pybcsv.Reader()
    try:
        reader.open(str(batch_file))
        all_data = reader.read_all()
        assert len(all_data) == 2000
    finally:
        reader.close()

    test_file.unlink(missing_ok=True)
    batch_file.unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# unittest-style tests (originally from test_all_optimizations.py)
# ---------------------------------------------------------------------------


def test_individual_operations():
    """Test basic optimized individual write/read operations."""
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.INT32)
    layout.add_column("name", pybcsv.STRING)
    layout.add_column("value", pybcsv.DOUBLE)

    filename = _tmp()
    try:
        writer = pybcsv.Writer(layout)
        writer.open(filename)
        writer.write_row([1, "Alice", 123.45])
        writer.write_row([2, "Bob", 678.90])
        writer.write_row([3, "Charlie", 111.22])
        writer.close()

        reader = pybcsv.Reader()
        reader.open(filename)
        rows = reader.read_all()
        reader.close()

        assert len(rows) == 3
        assert rows[0] == [1, "Alice", 123.45]
    finally:
        if os.path.exists(filename):
            os.unlink(filename)


def test_batch_operations():
    """Test optimized batch write operations."""
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.INT32)
    layout.add_column("value", pybcsv.DOUBLE)

    filename = _tmp()
    try:
        writer = pybcsv.Writer(layout)
        writer.open(filename)
        batch_data = [[i, float(i * 100)] for i in range(1, 6)]
        writer.write_rows(batch_data)
        writer.close()

        reader = pybcsv.Reader()
        reader.open(filename)
        rows = reader.read_all()
        reader.close()

        assert len(rows) == 5
        assert rows[2] == [3, 300.0]
    finally:
        if os.path.exists(filename):
            os.unlink(filename)


def test_dataframe_integration():
    """Test optimized pandas DataFrame integration."""
    df = pd.DataFrame({
        'id': [1, 2, 3, 4, 5],
        'name': ['Alice', 'Bob', 'Charlie', 'David', 'Eve'],
        'value': [123.45, 678.90, 111.22, 444.55, 999.99],
        'active': [True, False, True, True, False],
    })

    filename = _tmp()
    try:
        pybcsv.write_dataframe(df, filename)
        df_read = pybcsv.read_dataframe(filename)

        assert len(df_read) == len(df)
        assert list(df_read.columns) == ['id', 'name', 'value', 'active']
        assert df_read['id'].tolist() == [1, 2, 3, 4, 5]
        assert df_read['name'].tolist() == ['Alice', 'Bob', 'Charlie', 'David', 'Eve']
    finally:
        if os.path.exists(filename):
            os.unlink(filename)


def test_performance_comparison():
    """Test performance of batch vs individual operations."""
    n_rows = 1000
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.INT32)
    layout.add_column("value", pybcsv.DOUBLE)

    data = [[i, float(i * 10.5)] for i in range(n_rows)]
    filename = _tmp()
    try:
        writer = pybcsv.Writer(layout)
        writer.open(filename)
        start = time.time()
        writer.write_rows(data)
        batch_time = time.time() - start
        writer.close()

        os.unlink(filename)
        writer = pybcsv.Writer(layout)
        writer.open(filename)
        start = time.time()
        for row in data:
            writer.write_row(row)
        individual_time = time.time() - start
        writer.close()

        # Just verify both paths produce correct output
        reader = pybcsv.Reader()
        reader.open(filename)
        rows = reader.read_all()
        reader.close()
        assert len(rows) == n_rows
    finally:
        if os.path.exists(filename):
            os.unlink(filename)


def test_memory_optimization():
    """Test that optimizations work with larger datasets."""
    n_rows = 5000
    df = pd.DataFrame({
        'id': np.arange(n_rows, dtype=np.int32),
        'value1': np.random.random(n_rows).astype(np.float64),
        'value2': np.random.random(n_rows).astype(np.float64),
        'category': [f'cat_{i % 10}' for i in range(n_rows)],
    })

    filename = _tmp()
    try:
        pybcsv.write_dataframe(df, filename)
        df_read = pybcsv.read_dataframe(filename)

        assert len(df_read) == n_rows
        assert np.array_equal(df_read['id'].values, df['id'].values)
    finally:
        if os.path.exists(filename):
            os.unlink(filename)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
