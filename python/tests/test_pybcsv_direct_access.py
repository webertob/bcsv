#!/usr/bin/env python3
"""
ReaderDirectAccess tests for PyBCSV.
Tests random access reads, len/__getitem__, context managers,
out-of-range handling, and metadata accessors.
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import os
import tempfile
import unittest

import pybcsv


def _tmp(suffix=".bcsv"):
    fd, path = tempfile.mkstemp(suffix=suffix, prefix="pybcsv_test_")
    os.close(fd)
    return path


def _make_layout():
    """Standard 4-column layout for tests."""
    layout = pybcsv.Layout()
    layout.add_column("i32", pybcsv.ColumnType.INT32)
    layout.add_column("f64", pybcsv.ColumnType.DOUBLE)
    layout.add_column("txt", pybcsv.ColumnType.STRING)
    layout.add_column("flag", pybcsv.ColumnType.BOOL)
    return layout


def _sample_rows(n=10):
    return [[i, float(i) * 1.5, f"row_{i}", i % 2 == 0] for i in range(n)]


class TestReaderDirectAccess(unittest.TestCase):

    def _write_test_file(self, path, n=50):
        layout = _make_layout()
        rows = _sample_rows(n)
        writer = pybcsv.Writer(layout)
        writer.open(path)
        writer.write_rows(rows)
        writer.close()
        return rows

    def test_basic_random_access(self):
        path = _tmp()
        try:
            rows = self._write_test_file(path, 50)
            da = pybcsv.ReaderDirectAccess()
            da.open(path)
            self.assertEqual(da.row_count(), 50)

            row0 = da.read(0)
            self.assertEqual(row0[0], 0)
            self.assertEqual(row0[2], "row_0")

            row49 = da.read(49)
            self.assertEqual(row49[0], 49)
            self.assertEqual(row49[2], "row_49")

            row10 = da.read(10)
            self.assertEqual(row10[0], 10)

            da.close()
        finally:
            os.unlink(path)

    def test_len_and_getitem(self):
        path = _tmp()
        try:
            self._write_test_file(path, 20)
            da = pybcsv.ReaderDirectAccess()
            da.open(path)

            self.assertEqual(len(da), 20)
            row5 = da[5]
            self.assertEqual(row5[0], 5)

            da.close()
        finally:
            os.unlink(path)

    def test_context_manager(self):
        path = _tmp()
        try:
            self._write_test_file(path, 10)
            with pybcsv.ReaderDirectAccess() as da:
                da.open(path)
                self.assertEqual(da.row_count(), 10)
                row = da.read(3)
                self.assertEqual(row[0], 3)
        finally:
            os.unlink(path)

    def test_out_of_range(self):
        path = _tmp()
        try:
            self._write_test_file(path, 5)
            da = pybcsv.ReaderDirectAccess()
            da.open(path)
            with self.assertRaises(IndexError):
                da.read(100)
            da.close()
        finally:
            os.unlink(path)

    def test_metadata(self):
        path = _tmp()
        try:
            self._write_test_file(path, 10)
            da = pybcsv.ReaderDirectAccess()
            da.open(path)
            self.assertTrue(da.is_open())
            version = da.version_string()
            self.assertIsInstance(version, str)
            da.close()
            self.assertFalse(da.is_open())
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
