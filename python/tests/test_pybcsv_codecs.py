#!/usr/bin/env python3
"""
Codec and metadata tests for PyBCSV.
Tests multi-codec Writer (flat/zoh/delta), FileFlags enum,
writer/reader metadata accessors, and all-type codec roundtrips.
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
    os.unlink(path)
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


# ─── Codec selection ─────────────────────────────────────────────────────


class TestCodecSelection(unittest.TestCase):

    def _roundtrip(self, codec, compression=1):
        path = _tmp()
        try:
            layout = _make_layout()
            rows = _sample_rows(20)

            writer = pybcsv.Writer(layout, row_codec=codec)
            self.assertEqual(writer.row_codec(), codec)
            writer.open(path, True, compression)
            writer.write_rows(rows)
            writer.close()

            reader = pybcsv.Reader()
            reader.open(path)
            read_data = reader.read_all()
            reader.close()

            self.assertEqual(len(read_data), 20)
            for orig, read in zip(rows, read_data):
                self.assertEqual(orig[0], read[0])
                self.assertAlmostEqual(orig[1], read[1], places=5)
                self.assertEqual(orig[2], read[2])
                self.assertEqual(orig[3], read[3])
        finally:
            os.unlink(path)

    def test_flat_codec(self):
        self._roundtrip("flat")

    def test_zoh_codec(self):
        self._roundtrip("zoh")

    def test_delta_codec(self):
        self._roundtrip("delta")

    def test_flat_uncompressed(self):
        self._roundtrip("flat", compression=0)

    def test_delta_high_compression(self):
        self._roundtrip("delta", compression=5)

    def test_invalid_codec_raises(self):
        layout = _make_layout()
        with self.assertRaises(RuntimeError):
            pybcsv.Writer(layout, row_codec="nonexistent")

    def test_default_codec_is_delta(self):
        layout = _make_layout()
        writer = pybcsv.Writer(layout)
        self.assertEqual(writer.row_codec(), "delta")


# ─── FileFlags ────────────────────────────────────────────────────────────


class TestFileFlags(unittest.TestCase):

    def test_flags_exist(self):
        self.assertIsNotNone(pybcsv.FileFlags.NONE)
        self.assertIsNotNone(pybcsv.FileFlags.ZERO_ORDER_HOLD)
        self.assertIsNotNone(pybcsv.FileFlags.NO_FILE_INDEX)
        self.assertIsNotNone(pybcsv.FileFlags.STREAM_MODE)
        self.assertIsNotNone(pybcsv.FileFlags.BATCH_COMPRESS)
        self.assertIsNotNone(pybcsv.FileFlags.DELTA_ENCODING)

    def test_flags_bitwise_or(self):
        combined = pybcsv.FileFlags.ZERO_ORDER_HOLD | pybcsv.FileFlags.STREAM_MODE
        self.assertNotEqual(combined, pybcsv.FileFlags.NONE)

    def test_flags_bitwise_and(self):
        combined = pybcsv.FileFlags.ZERO_ORDER_HOLD | pybcsv.FileFlags.STREAM_MODE
        result = combined & pybcsv.FileFlags.ZERO_ORDER_HOLD
        self.assertNotEqual(int(result), 0)


# ─── Writer metadata ─────────────────────────────────────────────────────


class TestWriterMetadata(unittest.TestCase):

    def test_row_count(self):
        path = _tmp()
        try:
            layout = _make_layout()
            writer = pybcsv.Writer(layout)
            writer.open(path)
            self.assertEqual(writer.row_count(), 0)
            writer.write_row([1, 2.0, "a", True])
            self.assertEqual(writer.row_count(), 1)
            writer.write_rows([[2, 3.0, "b", False]] * 5)
            self.assertEqual(writer.row_count(), 6)
            writer.close()
        finally:
            os.unlink(path)

    def test_compression_level(self):
        path = _tmp()
        try:
            layout = _make_layout()
            writer = pybcsv.Writer(layout)
            writer.open(path, True, 3)
            self.assertEqual(writer.compression_level(), 3)
            writer.close()
        finally:
            os.unlink(path)


# ─── Reader metadata ─────────────────────────────────────────────────────


class TestReaderMetadata(unittest.TestCase):

    def test_file_flags(self):
        path = _tmp()
        try:
            layout = _make_layout()
            writer = pybcsv.Writer(layout)
            writer.open(path)
            writer.write_row([1, 2.0, "hello", True])
            writer.close()

            reader = pybcsv.Reader()
            reader.open(path)
            flags = reader.file_flags()
            self.assertIsNotNone(flags)
            reader.close()
        finally:
            os.unlink(path)

    def test_version_string(self):
        path = _tmp()
        try:
            layout = _make_layout()
            writer = pybcsv.Writer(layout)
            writer.open(path)
            writer.write_row([1, 2.0, "hello", True])
            writer.close()

            reader = pybcsv.Reader()
            reader.open(path)
            version = reader.version_string()
            self.assertIsInstance(version, str)
            self.assertTrue(len(version) > 0)
            reader.close()
        finally:
            os.unlink(path)


# ─── All-type codec roundtrip ────────────────────────────────────────────


class TestAllTypeRoundtrip(unittest.TestCase):
    """Ensure every ColumnType survives write -> read for every codec."""

    ALL_TYPES = [
        ("bool_col",   pybcsv.ColumnType.BOOL,   True),
        ("int8_col",   pybcsv.ColumnType.INT8,   -42),
        ("int16_col",  pybcsv.ColumnType.INT16,  -1234),
        ("int32_col",  pybcsv.ColumnType.INT32,  100000),
        ("int64_col",  pybcsv.ColumnType.INT64,  2**40),
        ("uint8_col",  pybcsv.ColumnType.UINT8,  200),
        ("uint16_col", pybcsv.ColumnType.UINT16, 50000),
        ("uint32_col", pybcsv.ColumnType.UINT32, 3_000_000_000),
        ("uint64_col", pybcsv.ColumnType.UINT64, 2**50),
        ("float_col",  pybcsv.ColumnType.FLOAT,  3.14),
        ("double_col", pybcsv.ColumnType.DOUBLE, 2.718281828),
        ("str_col",    pybcsv.ColumnType.STRING, "hello world"),
    ]

    def _test_codec(self, codec):
        path = _tmp()
        try:
            layout = pybcsv.Layout()
            for name, ctype, _ in self.ALL_TYPES:
                layout.add_column(name, ctype)

            values = [v for _, _, v in self.ALL_TYPES]

            writer = pybcsv.Writer(layout, row_codec=codec)
            writer.open(path)
            writer.write_rows([values] * 5)
            writer.close()

            reader = pybcsv.Reader()
            reader.open(path)
            data = reader.read_all()
            reader.close()

            self.assertEqual(len(data), 5)
            for row in data:
                for i, (_, ctype, expected) in enumerate(self.ALL_TYPES):
                    if ctype == pybcsv.ColumnType.FLOAT:
                        self.assertAlmostEqual(row[i], expected, places=2)
                    elif ctype == pybcsv.ColumnType.DOUBLE:
                        self.assertAlmostEqual(row[i], expected, places=6)
                    else:
                        self.assertEqual(row[i], expected, f"Mismatch at col {i}")
        finally:
            os.unlink(path)

    def test_flat_all_types(self):
        self._test_codec("flat")

    def test_zoh_all_types(self):
        self._test_codec("zoh")

    def test_delta_all_types(self):
        self._test_codec("delta")


if __name__ == "__main__":
    unittest.main()
