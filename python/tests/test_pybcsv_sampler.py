#!/usr/bin/env python3
"""
Sampler tests for PyBCSV.
Tests filter expressions, projection/selection, iterator protocol,
compile results, disassemble, and passthrough checks.
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


class TestSampler(unittest.TestCase):

    def _write_and_open(self, path, n=100):
        layout = _make_layout()
        rows = _sample_rows(n)
        writer = pybcsv.Writer(layout)
        writer.open(path)
        writer.write_rows(rows)
        writer.close()

        reader = pybcsv.Reader()
        reader.open(path)
        return reader, rows

    def test_sampler_no_filter(self):
        """Sampler with no filter/selection should pass all rows."""
        path = _tmp()
        try:
            reader, rows = self._write_and_open(path, 20)
            sampler = pybcsv.Sampler(reader)
            result = sampler.bulk()
            self.assertEqual(len(result), 20)
            reader.close()
        finally:
            os.unlink(path)

    def test_sampler_conditional_filter(self):
        """Filter rows where i32 >= 5."""
        path = _tmp()
        try:
            reader, rows = self._write_and_open(path, 20)
            sampler = pybcsv.Sampler(reader)
            result = sampler.set_conditional('X[0]["i32"] >= 5')
            self.assertTrue(result)

            filtered = sampler.bulk()
            self.assertEqual(len(filtered), 15)
            for row in filtered:
                self.assertGreaterEqual(row[0], 5)
            reader.close()
        finally:
            os.unlink(path)

    def test_sampler_selection(self):
        """Project only first two columns."""
        path = _tmp()
        try:
            reader, _ = self._write_and_open(path, 10)
            sampler = pybcsv.Sampler(reader)
            result = sampler.set_selection('X[0]["i32"], X[0]["f64"]')
            self.assertTrue(result)

            out_layout = sampler.output_layout()
            self.assertEqual(out_layout.column_count(), 2)

            all_rows = sampler.bulk()
            self.assertEqual(len(all_rows), 10)
            self.assertEqual(len(all_rows[0]), 2)
            reader.close()
        finally:
            os.unlink(path)

    def test_sampler_iterator(self):
        """Use sampler as iterator."""
        path = _tmp()
        try:
            reader, _ = self._write_and_open(path, 10)
            sampler = pybcsv.Sampler(reader)
            count = 0
            for row in sampler:
                count += 1
            self.assertEqual(count, 10)
            reader.close()
        finally:
            os.unlink(path)

    def test_sampler_mode(self):
        """Test SamplerMode enum."""
        self.assertIsNotNone(pybcsv.SamplerMode.TRUNCATE)
        self.assertIsNotNone(pybcsv.SamplerMode.EXPAND)

    def test_sampler_error_policy(self):
        """Test SamplerErrorPolicy enum."""
        self.assertIsNotNone(pybcsv.SamplerErrorPolicy.THROW)
        self.assertIsNotNone(pybcsv.SamplerErrorPolicy.SKIP_ROW)
        self.assertIsNotNone(pybcsv.SamplerErrorPolicy.SATURATE)

    def test_sampler_compile_result(self):
        """Test SamplerCompileResult truthiness."""
        path = _tmp()
        try:
            reader, _ = self._write_and_open(path, 5)
            sampler = pybcsv.Sampler(reader)

            good = sampler.set_conditional('X[0]["i32"] > 0')
            self.assertTrue(good)
            self.assertTrue(good.success)

            bad = sampler.set_conditional('nonexistent_column > 0')
            self.assertFalse(bad)
            self.assertFalse(bad.success)
            self.assertTrue(len(bad.error_msg) > 0)

            reader.close()
        finally:
            os.unlink(path)

    def test_sampler_disassemble(self):
        """Test disassemble returns a string."""
        path = _tmp()
        try:
            reader, _ = self._write_and_open(path, 5)
            sampler = pybcsv.Sampler(reader)
            sampler.set_conditional('X[0]["i32"] > 2')
            dis = sampler.disassemble()
            self.assertIsInstance(dis, str)
            self.assertTrue(len(dis) > 0)
            reader.close()
        finally:
            os.unlink(path)

    def test_sampler_passthrough(self):
        """Test passthrough checks."""
        path = _tmp()
        try:
            reader, _ = self._write_and_open(path, 5)
            sampler = pybcsv.Sampler(reader)
            self.assertFalse(sampler.is_conditional_passthrough())
            self.assertFalse(sampler.is_selection_passthrough())

            sampler.set_conditional('X[0]["i32"] > 0')
            self.assertFalse(sampler.is_conditional_passthrough())

            reader.close()
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
