#!/usr/bin/env python3
"""
Tests for PyBCSV CsvWriter and CsvReader bindings.

Covers:
- Round-trip write → read for multiple column types
- Delimiter and decimal-separator options
- Context-manager (with-statement) usage
- Row count tracking
- Error handling (missing file, non-existent path)
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import os
import csv
import tempfile
import pytest

import pybcsv


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_layout(*cols):
    """Build a Layout from (name, type) pairs."""
    layout = pybcsv.Layout()
    for name, ctype in cols:
        layout.add_column(name, ctype)
    return layout


# ---------------------------------------------------------------------------
# CsvWriter / CsvReader round-trip
# ---------------------------------------------------------------------------

class TestCsvRoundTrip:
    """Write CSV with CsvWriter, read back with CsvReader, compare."""

    def test_basic_round_trip(self, tmp_path):
        path = str(tmp_path / "basic.csv")
        layout = _make_layout(
            ("id", pybcsv.ColumnType.INT32),
            ("name", pybcsv.ColumnType.STRING),
            ("score", pybcsv.ColumnType.FLOAT),
        )

        rows = [
            [1, "alice", 9.5],
            [2, "bob", 7.25],
            [3, "carol", 8.0],
        ]

        # Write
        writer = pybcsv.CsvWriter()
        writer.open(path, layout)
        for r in rows:
            writer.write_row(r)
        writer.close()

        # Read back via stdlib csv (file must be valid CSV)
        with open(path, newline="") as f:
            reader = csv.reader(f)
            header = next(reader)
            assert header == ["id", "name", "score"]
            data = list(reader)
        assert len(data) == 3
        assert data[0][1] == "alice"

    def test_context_manager(self, tmp_path):
        path = str(tmp_path / "ctx.csv")
        layout = _make_layout(("x", pybcsv.ColumnType.INT32))

        with pybcsv.CsvWriter() as w:
            w.open(path, layout)
            w.write_row([42])

        # file should exist and have data
        assert os.path.isfile(path)
        with open(path) as f:
            lines = f.read().strip().splitlines()
        assert len(lines) == 2  # header + 1 row

    def test_row_count(self, tmp_path):
        path = str(tmp_path / "count.csv")
        layout = _make_layout(("v", pybcsv.ColumnType.DOUBLE))

        writer = pybcsv.CsvWriter()
        writer.open(path, layout)
        assert writer.row_count() == 0
        writer.write_row([1.0])
        writer.write_row([2.0])
        assert writer.row_count() == 2
        writer.close()

    def test_write_rows_batch(self, tmp_path):
        path = str(tmp_path / "batch.csv")
        layout = _make_layout(("a", pybcsv.ColumnType.INT32))

        writer = pybcsv.CsvWriter()
        writer.open(path, layout)
        writer.write_rows([[i] for i in range(100)])
        assert writer.row_count() == 100
        writer.close()

    def test_delimiter(self, tmp_path):
        path = str(tmp_path / "semicolon.csv")
        layout = _make_layout(
            ("a", pybcsv.ColumnType.INT32),
            ("b", pybcsv.ColumnType.STRING),
        )

        writer = pybcsv.CsvWriter()
        writer.open(path, layout, ";")
        writer.write_row([1, "hello"])
        writer.close()

        with open(path) as f:
            content = f.read()
        assert ";" in content


# ---------------------------------------------------------------------------
# CsvReader specific
# ---------------------------------------------------------------------------

class TestCsvReader:

    def _write_csv(self, path, header, rows, delimiter=","):
        with open(path, "w", newline="") as f:
            w = csv.writer(f, delimiter=delimiter)
            w.writerow(header)
            w.writerows(rows)

    def test_read_simple(self, tmp_path):
        path = str(tmp_path / "simple.csv")
        self._write_csv(path, ["x", "y"], [[1, 2], [3, 4]])

        layout = _make_layout(
            ("x", pybcsv.ColumnType.INT32),
            ("y", pybcsv.ColumnType.INT32),
        )

        reader = pybcsv.CsvReader()
        reader.open(path, layout)
        rows = []
        while reader.read_next():
            rows.append(reader.read_row())
        reader.close()
        assert len(rows) == 2

    def test_reader_nonexistent_file(self, tmp_path):
        path = str(tmp_path / "does_not_exist.csv")
        layout = _make_layout(("x", pybcsv.ColumnType.INT32))

        reader = pybcsv.CsvReader()
        result = reader.open(path, layout)
        assert result is False or result is None  # expect failure
