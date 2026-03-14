#!/usr/bin/env python3
"""
Tests for Cycle 4: Python Hardening.

Covers: strict NaN mode, pathlib.Path support, __repr__ methods.
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import os
import tempfile
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

import pybcsv


# ---------------------------------------------------------------------------
# Strict NaN mode tests
# ---------------------------------------------------------------------------

class TestStrictNaN:
    """Tests for the strict= parameter in write_dataframe."""

    def test_strict_nan_float_raises(self, tmp_path):
        """strict=True should raise ValueError when float column has NaN."""
        filepath = str(tmp_path / "strict_nan.bcsv")
        df = pd.DataFrame({
            'id': [1, 2, 3],
            'value': [1.0, float('nan'), 3.0],
        })
        with pytest.raises(ValueError, match="NaN/None"):
            pybcsv.write_dataframe(df, filepath, strict=True)

    def test_strict_nan_string_raises(self, tmp_path):
        """strict=True should raise ValueError when string column has None."""
        filepath = str(tmp_path / "strict_nan_str.bcsv")
        df = pd.DataFrame({
            'id': [1, 2, 3],
            'name': ['Alice', None, 'Charlie'],
        })
        with pytest.raises(ValueError, match="NaN/None"):
            pybcsv.write_dataframe(df, filepath, strict=True)

    def test_strict_nan_reports_columns(self, tmp_path):
        """strict=True error message should list affected column names."""
        filepath = str(tmp_path / "strict_cols.bcsv")
        df = pd.DataFrame({
            'good': [1, 2, 3],
            'bad_float': [1.0, np.nan, 3.0],
            'bad_str': ['a', None, 'c'],
        })
        with pytest.raises(ValueError, match="bad_float") as exc_info:
            pybcsv.write_dataframe(df, filepath, strict=True)
        assert "bad_str" in str(exc_info.value)

    def test_strict_false_default_coerces(self, tmp_path):
        """Default strict=False should coerce NaN to zero (existing behavior)."""
        filepath = str(tmp_path / "default_nan.bcsv")
        df = pd.DataFrame({
            'id': [1, 2, 3],
            'value': [1.0, float('nan'), 3.0],
        })
        # Should succeed without error (with warning)
        with pytest.warns(UserWarning, match="NaN/None"):
            pybcsv.write_dataframe(df, filepath, strict=False)

        df_read = pybcsv.read_dataframe(filepath)
        assert len(df_read) == 3
        # NaN was coerced to 0.0
        assert df_read.iloc[1]['value'] == 0.0

    def test_strict_clean_data_passes(self, tmp_path):
        """strict=True should pass with clean data (no NaN)."""
        filepath = str(tmp_path / "clean.bcsv")
        df = pd.DataFrame({
            'id': [1, 2, 3],
            'value': [1.0, 2.0, 3.0],
            'name': ['a', 'b', 'c'],
        })
        pybcsv.write_dataframe(df, filepath, strict=True)
        df_read = pybcsv.read_dataframe(filepath)
        assert len(df_read) == 3
        pd.testing.assert_frame_equal(df_read, df, check_dtype=False)


# ---------------------------------------------------------------------------
# pathlib.Path support tests
# ---------------------------------------------------------------------------

class TestPathlibSupport:
    """Tests for pathlib.Path acceptance in file path parameters."""

    def test_write_read_dataframe_pathlib(self, tmp_path):
        """write_dataframe and read_dataframe should accept pathlib.Path."""
        filepath = tmp_path / "pathlib_test.bcsv"
        df = pd.DataFrame({
            'id': [1, 2, 3],
            'value': [10.0, 20.0, 30.0],
        })
        pybcsv.write_dataframe(df, filepath)
        df_read = pybcsv.read_dataframe(filepath)
        assert len(df_read) == 3
        pd.testing.assert_frame_equal(df_read, df, check_dtype=False)

    def test_to_from_csv_pathlib(self, tmp_path):
        """to_csv and from_csv should accept pathlib.Path."""
        bcsv_path = tmp_path / "pathlib.bcsv"
        csv_path = tmp_path / "pathlib.csv"
        csv_back_path = tmp_path / "pathlib_back.bcsv"

        df = pd.DataFrame({'x': [1, 2, 3], 'y': ['a', 'b', 'c']})
        pybcsv.write_dataframe(df, bcsv_path)

        # BCSV → CSV with pathlib
        pybcsv.to_csv(bcsv_path, csv_path)
        assert csv_path.exists()

        # CSV → BCSV with pathlib
        pybcsv.from_csv(csv_path, csv_back_path)
        assert csv_back_path.exists()

        df_back = pybcsv.read_dataframe(csv_back_path)
        assert len(df_back) == 3


# ---------------------------------------------------------------------------
# __repr__ tests
# ---------------------------------------------------------------------------

class TestRepr:
    """Tests for __repr__ methods on Writer, Reader, ReaderDirectAccess."""

    def test_writer_repr_closed(self):
        """Writer __repr__ should show codec, open=False when not opened."""
        layout = pybcsv.Layout()
        layout.add_column("x", pybcsv.ColumnType.INT32)
        writer = pybcsv.Writer(layout, "delta")
        r = repr(writer)
        assert "Writer" in r
        assert "delta" in r
        assert "open=False" in r

    def test_writer_repr_open(self, tmp_path):
        """Writer __repr__ should show open=True and row count when open."""
        filepath = str(tmp_path / "repr_writer.bcsv")
        layout = pybcsv.Layout()
        layout.add_column("x", pybcsv.ColumnType.INT32)
        writer = pybcsv.Writer(layout)
        writer.open(filepath, compression_level=0)
        writer.write_row([42])
        r = repr(writer)
        assert "open=True" in r
        assert "rows=1" in r
        writer.close()

    def test_reader_repr_closed(self):
        """Reader __repr__ should show open=False when not opened."""
        reader = pybcsv.Reader()
        r = repr(reader)
        assert "Reader" in r
        assert "open=False" in r

    def test_reader_repr_open(self, tmp_path):
        """Reader __repr__ should show open=True and row_pos."""
        filepath = str(tmp_path / "repr_reader.bcsv")
        layout = pybcsv.Layout()
        layout.add_column("x", pybcsv.ColumnType.INT32)
        with pybcsv.Writer(layout) as w:
            w.open(filepath, compression_level=0)
            w.write_row([1])
            w.write_row([2])

        reader = pybcsv.Reader()
        reader.open(filepath)
        r = repr(reader)
        assert "open=True" in r
        reader.close()

    def test_direct_access_repr_closed(self):
        """ReaderDirectAccess __repr__ shows open=False when closed."""
        da = pybcsv.ReaderDirectAccess()
        r = repr(da)
        assert "ReaderDirectAccess" in r
        assert "open=False" in r

    def test_direct_access_repr_open(self, tmp_path):
        """ReaderDirectAccess __repr__ shows open=True and row count."""
        filepath = str(tmp_path / "repr_da.bcsv")
        layout = pybcsv.Layout()
        layout.add_column("x", pybcsv.ColumnType.INT32)
        with pybcsv.Writer(layout) as w:
            w.open(filepath, compression_level=0)
            for i in range(5):
                w.write_row([i])

        da = pybcsv.ReaderDirectAccess()
        da.open(filepath)
        r = repr(da)
        assert "open=True" in r
        assert "rows=5" in r
        da.close()


# ---------------------------------------------------------------------------
# Stub / .pyi validation
# ---------------------------------------------------------------------------

class TestTypeStubs:
    """Verify .pyi type stubs exist in the package."""

    def test_pyi_stub_exists(self):
        """_bcsv.pyi should exist alongside the compiled extension."""
        import importlib.resources
        pkg = importlib.resources.files("pybcsv")
        # Check that the stub file exists
        stub = pkg / "_bcsv.pyi"
        assert stub.is_file(), f"Missing type stub: {stub}"

    def test_py_typed_marker_exists(self):
        """py.typed marker must exist for PEP 561 compliance."""
        import importlib.resources
        pkg = importlib.resources.files("pybcsv")
        assert (pkg / "py.typed").is_file()
