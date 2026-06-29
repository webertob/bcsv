"""Integration tests for bcsvCompare CLI.

Tests: help, 3 modes, tolerance, ranges, verbose, errors.
Exit code: 0 = identical, 1 = different, 2 = error.
Verbose output goes to stdout, silent otherwise.
"""

import pytest
from pathlib import Path
from helpers import run_tool


# Fixtures


@pytest.fixture(scope="module")
def compare_data(tmp_path_factory, tools):
    d = tmp_path_factory.mktemp("compare")

    sensor_a = d / "sensor_a.bcsv"
    sensor_b = d / "sensor_b.bcsv"
    run_tool(tools["bcsvCompare"], "--help")  # smoke
    run_tool(tools["bcsvGenerator"], "-p", "sensor_noisy", "-n", "100", "-o", sensor_a)
    run_tool(tools["bcsvGenerator"], "-p", "sensor_noisy", "-n", "100", "-o", sensor_b)

    mixed = d / "mixed.bcsv"
    run_tool(tools["bcsvGenerator"], "-p", "mixed_generic", "-n", "100", "-o", mixed)

    sensor50 = d / "sensor50.bcsv"
    run_tool(tools["bcsvGenerator"], "-p", "sensor_noisy", "-n", "50", "-o", sensor50)

    return {
        "sensor_a": sensor_a,
        "sensor_b": sensor_b,
        "mixed": mixed,
        "sensor50": sensor50,
        "dir": d,
    }


# Help & modes


class TestCompareHelp:
    def test_help_flag(self, tools):
        r = run_tool(tools["bcsvCompare"], "--help")
        assert "bcsvCompare" in r.stdout
        assert "strict" in r.stdout
        assert "compatible" in r.stdout
        assert "value" in r.stdout


class TestModes:
    def test_strict_identical(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 0

    def test_strict_identical_silent(self, tools, compare_data):
        """No verbose => no stdout."""
        r = run_tool(
            tools["bcsvCompare"], compare_data["sensor_a"], compare_data["sensor_b"]
        )
        assert r.stdout.strip() == ""

    def test_stict_different_column_count(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            compare_data["sensor_a"],
            compare_data["mixed"],
            check=False,
        )
        assert r.returncode == 1

    def test_strict_different_row_count(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            compare_data["sensor_a"],
            compare_data["sensor50"],
            check=False,
        )
        assert r.returncode == 1

    def test_compatible_identical(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--mode",
            "compatible",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 0

    def test_value_identical(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--mode",
            "value",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 0


# Tolerance


class TestTolerance:
    def test_tolerance_same_data(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--tolerance",
            "1e-6",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 0


# Ranges


class TestRanges:
    def test_rows_range(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--rows",
            "0:9",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0

    def test_cols_range(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--cols",
            "0:1",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0

    def test_negative_row_range(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--rows",
            "-10:",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0

    def test_single_row(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--rows",
            "0",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0

    def test_single_col(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--cols",
            "0",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0

    def test_disjoint_ranges(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--rows",
            "0:9,50:59",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0

    def test_rows_range_out_of_bounds(self, tools, compare_data):
        """Rows out of range should fail with exit 2."""
        r = run_tool(
            tools["bcsvCompare"],
            "--rows",
            "200:300",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 2

    def test_cols_range_out_of_bounds(self, tools, compare_data):
        """Columns out of range should fail with exit 2."""
        r = run_tool(
            tools["bcsvCompare"],
            "--cols",
            "200:300",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 2


# Verbose


class TestVerbose:
    def test_verbose_identical_stdout(self, tools, compare_data):
        """Verbose output for identical files goes to stdout with 'Result: identical'."""
        r = run_tool(
            tools["bcsvCompare"],
            "-v",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
        )
        assert r.returncode == 0
        assert "Result: identical" in r.stdout

    def test_verbose_different_column_count(self, tools, compare_data):
        """Verbose output for different files goes to stdout with 'Result: different'."""
        r = run_tool(
            tools["bcsvCompare"],
            "-v",
            compare_data["sensor_a"],
            compare_data["mixed"],
            check=False,
        )
        assert r.returncode == 1
        assert "Result: different" in r.stdout

    def test_verbose_mismatch_detail(self, tools, compare_data):
        """Verbose output shows mismatch details."""
        r = run_tool(
            tools["bcsvCompare"],
            "-v",
            compare_data["sensor_a"],
            compare_data["mixed"],
            check=False,
        )
        assert r.returncode == 1
        assert "mismatch" in r.stdout.lower()


# Errors


class TestErrors:
    def test_no_args(self, tools):
        r = run_tool(tools["bcsvCompare"], check=False)
        assert r.returncode == 2

    def test_one_file_only(self, tools, compare_data):
        r = run_tool(tools["bcsvCompare"], compare_data["sensor_a"], check=False)
        assert r.returncode == 2

    def test_missing_file_a(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "/tmp/nonexistent_x.bcsv",
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 2

    def test_missing_file_b(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            compare_data["sensor_a"],
            "/tmp/nonexistent_x.bcsv",
            check=False,
        )
        assert r.returncode == 2

    def test_invalid_mode(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--mode",
            "foo",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 2

    def test_unknown_option(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            "--badopt",
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            check=False,
        )
        assert r.returncode == 2

    def test_too_many_args(self, tools, compare_data):
        r = run_tool(
            tools["bcsvCompare"],
            compare_data["sensor_a"],
            compare_data["sensor_b"],
            compare_data["mixed"],
            check=False,
        )
        assert r.returncode == 2
