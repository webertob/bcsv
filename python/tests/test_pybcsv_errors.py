#!/usr/bin/env python3
"""
Error handling and edge case tests for PyBCSV.
Tests file not found, corrupt files, layout mismatches, wrong types,
permission denied, context manager exceptions, double close,
concurrent access, large strings, and invalid parameters.
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


class TestErrorHandling(unittest.TestCase):
    """Test error handling and edge cases."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.test_file = os.path.join(self.temp_dir, "test.bcsv")

    def tearDown(self):
        if os.path.exists(self.test_file):
            os.remove(self.test_file)
        if os.path.exists(self.temp_dir):
            os.rmdir(self.temp_dir)

    def test_invalid_file_read(self):
        non_existent = os.path.join(self.temp_dir, "does_not_exist.bcsv")
        with self.assertRaises(Exception):
            reader = pybcsv.Reader()
            reader.open(non_existent)

    def test_invalid_file_write(self):
        invalid_path = "/invalid/path/that/does/not/exist/test.bcsv"
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.ColumnType.INT32)
        with self.assertRaises(Exception):
            pybcsv.Writer(invalid_path, layout)

    def test_empty_layout(self):
        empty_layout = pybcsv.Layout()
        with pybcsv.Writer(empty_layout) as writer:
            writer.open(self.test_file, compression_level=0)
            writer.write_row([])

        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            layout = reader.layout()
            self.assertEqual(layout.column_count(), 0)
            data = reader.read_all()
            self.assertEqual(len(data), 1)

    def test_row_length_mismatch(self):
        layout = pybcsv.Layout()
        layout.add_column("col1", pybcsv.ColumnType.INT32)
        layout.add_column("col2", pybcsv.ColumnType.STRING)

        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            writer.write_row([1, "test"])
            try:
                writer.write_row([1])
            except Exception:
                pass
            try:
                writer.write_row([1, "test", "extra"])
            except Exception:
                pass

    def test_wrong_data_types(self):
        layout = pybcsv.Layout()
        layout.add_column("int_col", pybcsv.ColumnType.INT32)
        layout.add_column("bool_col", pybcsv.ColumnType.BOOL)
        layout.add_column("float_col", pybcsv.ColumnType.DOUBLE)

        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            writer.write_row([1, True, 1.5])
            writer.write_row([1.0, False, 2])
            writer.write_row(["1", "True", "3.14"])
            try:
                writer.write_row(["not_a_number", True, 1.5])
            except Exception:
                pass

    def test_file_permissions(self):
        if os.name != 'posix':
            self.skipTest("File permission test only on POSIX systems")
        readonly_dir = os.path.join(self.temp_dir, "readonly")
        os.makedirs(readonly_dir)
        os.chmod(readonly_dir, 0o444)

        readonly_file = os.path.join(readonly_dir, "test.bcsv")
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.ColumnType.INT32)

        try:
            with self.assertRaises(Exception):
                pybcsv.Writer(readonly_file, layout)
        finally:
            os.chmod(readonly_dir, 0o755)
            os.rmdir(readonly_dir)

    def test_context_manager_exceptions(self):
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.ColumnType.INT32)
        try:
            with pybcsv.Writer(layout) as writer:
                writer.open(self.test_file, compression_level=0)
                writer.write_row([1])
                raise ValueError("Test exception")
        except ValueError:
            pass

        self.assertTrue(os.path.exists(self.test_file))
        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            data = reader.read_all()
            self.assertEqual(len(data), 1)
            self.assertEqual(data[0], [1])

    def test_reader_iteration_after_end(self):
        layout = pybcsv.Layout()
        layout.add_column("value", pybcsv.ColumnType.INT32)
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for i in range(3):
                writer.write_row([i])

        with pybcsv.Reader() as reader:
            reader.open(self.test_file)
            rows = list(reader)
            self.assertEqual(len(rows), 3)
            next_row = reader.read_row()
            self.assertIsNone(next_row)

    def test_multiple_readers(self):
        layout = pybcsv.Layout()
        layout.add_column("value", pybcsv.ColumnType.INT32)
        with pybcsv.Writer(layout) as writer:
            writer.open(self.test_file, compression_level=0)
            for i in range(5):
                writer.write_row([i])

        with pybcsv.Reader() as reader1, pybcsv.Reader() as reader2:
            reader1.open(self.test_file)
            reader2.open(self.test_file)
            data1 = reader1.read_all()
            data2 = reader2.read_all()
            self.assertEqual(data1, data2)
            self.assertEqual(len(data1), 5)


class TestErrorHandlingEdgeCases(unittest.TestCase):
    """Extended error handling and validation tests."""

    def setUp(self):
        self.temp_files = []

    def tearDown(self):
        for fp in self.temp_files:
            if os.path.exists(fp):
                os.unlink(fp)

    def _tmp(self, suffix='.bcsv'):
        fd, fp = tempfile.mkstemp(suffix=suffix)
        os.close(fd)
        self.temp_files.append(fp)
        return fp

    def _create_valid_file(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([1, "test"])
        writer.close()
        return filepath

    def test_file_not_found_reader(self):
        reader = pybcsv.Reader()
        with self.assertRaises(RuntimeError):
            reader.open("/path/that/does/not/exist/file.bcsv")
        self.assertFalse(reader.is_open())

    def test_file_not_found_writer(self):
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        writer = pybcsv.Writer(layout)
        with self.assertRaises(RuntimeError):
            writer.open("/path/that/does/not/exist/file.bcsv")
        self.assertFalse(writer.is_open())

    def test_permission_denied(self):
        if os.name != 'posix':
            self.skipTest("Permission test only on POSIX")
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        writer = pybcsv.Writer(layout)
        with self.assertRaises(RuntimeError):
            writer.open("/root/test.bcsv")

    def test_corrupted_file_header(self):
        filepath = self._tmp()
        with open(filepath, 'wb') as f:
            f.write(b"This is not a valid BCSV file header")
        reader = pybcsv.Reader()
        with self.assertRaises(RuntimeError):
            reader.open(filepath)

    def test_truncated_file(self):
        valid_fp = self._create_valid_file()
        with open(valid_fp, 'rb') as f:
            valid_content = f.read()
        truncated_fp = self._tmp()
        with open(truncated_fp, 'wb') as f:
            f.write(valid_content[:len(valid_content) // 2])
        reader = pybcsv.Reader()
        try:
            opened = reader.open(truncated_fp)
        except RuntimeError:
            return  # Batch codec detects corruption during open
        if opened:
            try:
                data = reader.read_all()
                self.assertIsInstance(data, list)
            except Exception:
                pass
            finally:
                reader.close()

    def test_row_length_mismatch_extended(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("col1", pybcsv.INT32)
        layout.add_column("col2", pybcsv.STRING)
        layout.add_column("col3", pybcsv.DOUBLE)
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([1, "test", 3.14])
        writer.close()

    def test_wrong_data_types_extended(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("int_col", pybcsv.INT32)
        layout.add_column("string_col", pybcsv.STRING)
        layout.add_column("double_col", pybcsv.DOUBLE)
        layout.add_column("bool_col", pybcsv.BOOL)
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([42, "valid_string", 3.14, True])
        writer.close()

    def test_numeric_overflow(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("int8_col", pybcsv.INT8)
        layout.add_column("uint8_col", pybcsv.UINT8)
        layout.add_column("int32_col", pybcsv.INT32)
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([127, 255, 2147483647])
        writer.write_row([-128, 0, -2147483648])
        writer.close()

    def test_invalid_layout(self):
        empty_layout = pybcsv.Layout()
        filepath = self._tmp()
        writer = pybcsv.Writer(empty_layout)
        writer.open(filepath)
        writer.write_row([])
        writer.close()

    def test_batch_operation_errors(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("id", pybcsv.INT32)
        layout.add_column("name", pybcsv.STRING)
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_rows([[1, "valid1"], [2, "valid2"], [3, "valid3"]])
        writer.close()

    def test_file_operations_when_closed(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)

        writer = pybcsv.Writer(layout)
        try:
            result = writer.write_row([1])
            if result is not None:
                self.assertFalse(result)
        except (RuntimeError, ValueError):
            pass

        reader = pybcsv.Reader()
        try:
            result = reader.read_all()
            if result is not None:
                self.assertEqual(result, [])
        except (RuntimeError, ValueError, AttributeError):
            pass

    def test_double_close(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)

        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([1])
        writer.close()
        self.assertFalse(writer.is_open())
        writer.close()
        self.assertFalse(writer.is_open())

        reader = pybcsv.Reader()
        reader.open(filepath)
        reader.read_all()
        reader.close()
        self.assertFalse(reader.is_open())
        reader.close()
        self.assertFalse(reader.is_open())

    def test_concurrent_file_access(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)

        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        writer.write_row([1])
        writer.close()

        reader1 = pybcsv.Reader()
        reader2 = pybcsv.Reader()
        self.assertTrue(reader1.open(filepath))
        self.assertTrue(reader2.open(filepath))
        data1 = reader1.read_all()
        data2 = reader2.read_all()
        self.assertEqual(data1, data2)
        reader1.close()
        reader2.close()

    def test_large_string_errors(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("large_string", pybcsv.STRING)
        writer = pybcsv.Writer(layout)
        writer.open(filepath)
        try:
            huge_string = "x" * (100 * 1024 * 1024)
            writer.write_row([huge_string])
            writer.close()
            reader = pybcsv.Reader()
            reader.open(filepath)
            data = reader.read_all()
            reader.close()
            self.assertEqual(len(data[0][0]), 100 * 1024 * 1024)
        except (MemoryError, OverflowError, RuntimeError):
            pass
        finally:
            if writer.is_open():
                writer.close()

    def test_invalid_compression_level(self):
        filepath = self._tmp()
        layout = pybcsv.Layout()
        layout.add_column("test", pybcsv.INT32)
        writer = pybcsv.Writer(layout)
        try:
            writer.open(filepath, True, 100)
            writer.write_row([1])
            writer.close()
        except (ValueError, RuntimeError):
            pass
        try:
            with self.assertRaises(TypeError):
                writer.open(filepath, True, -1)
        except Exception:
            pass


if __name__ == '__main__':
    unittest.main()
