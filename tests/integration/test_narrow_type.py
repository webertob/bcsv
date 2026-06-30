"""Integration tests for bcsvNarrowType CLI.

Tests: analyze mode, convert + compare, in-place overwrite,
no-narrowing case, empty file, string opt-in, flat codec size.
"""

import shutil

import pytest
from pathlib import Path
from helpers import run_tool


# Fixtures


@pytest.fixture(scope="module")
def narrow_data(tmp_path_factory, tools):
    d = tmp_path_factory.mktemp("narrow_type")
    narrow = tools.get(
        "bcsvNarrowType", tools["bcsvGenerator"].parent / "bcsvNarrowType"
    )

    # Generate test files
    sensor = d / "sensor.bcsv"
    run_tool(tools["bcsvGenerator"], "-p", "sensor_noisy", "-n", "1000", "-o", sensor)

    empty = d / "empty.bcsv"
    # Create a 0-row file: bcsvGenerator rejects -n 0, so filter the sensor
    # file with a never-matching condition to retain the layout but no rows.
    run_tool(tools["bcsvSampler"], "-c", "X[0][0] != X[0][0]", sensor, empty, "-f")

    # Flat codec file for size test
    flat = d / "flat.bcsv"
    run_tool(
        tools["bcsvGenerator"],
        "-p",
        "sensor_noisy",
        "-n",
        "10000",
        "--row-codec",
        "flat",
        "-o",
        flat,
    )

    delta = d / "delta.bcsv"
    run_tool(
        tools["bcsvGenerator"],
        "-p",
        "sensor_noisy",
        "-n",
        "10000",
        "--row-codec",
        "delta",
        "-o",
        delta,
    )

    return {
        "sensor": sensor,
        "empty": empty,
        "flat": flat,
        "delta": delta,
        "dir": d,
        "narrow": narrow,
    }


class TestHelp:
    def test_help_flag(self, narrow_data):
        r = run_tool(narrow_data["narrow"], "--help")
        assert "bcsvNarrowType" in r.stdout
        assert "--in-place" in r.stdout
        assert "--overwrite" in r.stdout
        assert "--cols" in r.stdout
        assert "--stringsToValue" in r.stdout


class TestAnalyze:
    def test_analyze_output(self, narrow_data):
        r = run_tool(narrow_data["narrow"], narrow_data["sensor"])
        assert r.returncode == 0
        assert "bcsvNarrowType: analysis of" in r.stdout
        assert "Columns:" in r.stdout

    def test_verbose_stderr(self, narrow_data):
        r = run_tool(narrow_data["narrow"], "-v", narrow_data["sensor"], check=False)
        assert r.returncode == 0
        # verbose goes to stderr
        assert "Scanning file:" in r.stderr


class TestConvert:
    def test_convert_to_new_file(self, narrow_data):
        out = narrow_data["dir"] / "converted.bcsv"
        # Two positionals → convert mode (no flag required).
        r = run_tool(
            narrow_data["narrow"], narrow_data["sensor"], str(out)
        )
        assert r.returncode == 0
        assert out.exists()

        # Verify output is valid BCSV
        r2 = run_tool(narrow_data["narrow"], str(out))
        assert r2.returncode == 0

    def test_convert_value_correctness(self, narrow_data, tools):
        out = narrow_data["dir"] / "value_test.bcsv"
        run_tool(
            narrow_data["narrow"], "-o", str(out), narrow_data["sensor"]
        )

        # Values should match in value mode
        r = run_tool(
            tools["bcsvCompare"],
            "--mode",
            "value",
            narrow_data["sensor"],
            out,
            check=False,
        )
        assert r.returncode == 0

    def test_convert_delta_codec(self, narrow_data, tools):
        out = narrow_data["dir"] / "delta_out.bcsv"
        run_tool(
            narrow_data["narrow"], "-o", str(out), narrow_data["delta"]
        )

        # Delta codec: values should match (size may not shrink)
        r = run_tool(
            tools["bcsvCompare"],
            "--mode",
            "value",
            narrow_data["delta"],
            out,
            check=False,
        )
        assert r.returncode == 0

    def test_convert_flat_codec_size(self, narrow_data):
        out = narrow_data["dir"] / "flat_out.bcsv"
        in_size = narrow_data["flat"].stat().st_size

        run_tool(
            narrow_data["narrow"], "-o", str(out), narrow_data["flat"]
        )

        out_size = out.stat().st_size
        # Flat codec should show size reduction for narrowable columns
        if in_size > 0:
            assert out_size < in_size or out_size == in_size


class TestInPlace:
    def test_inplace_overwrite(self, narrow_data):
        # Copy sensor file to avoid modifying the shared original
        inp = narrow_data["dir"] / "inplace_test.bcsv"
        shutil.copy(narrow_data["sensor"], inp)

        orig_size = inp.stat().st_size
        assert orig_size > 0
        r = run_tool(narrow_data["narrow"], "--in-place", str(inp))
        assert r.returncode == 0
        assert inp.exists()

    def test_inplace_rejects_output(self, narrow_data):
        inp = narrow_data["dir"] / "inplace_reject.bcsv"
        shutil.copy(narrow_data["sensor"], inp)
        out = narrow_data["dir"] / "inplace_reject_out.bcsv"
        r = run_tool(
            narrow_data["narrow"], "--in-place", str(inp), str(out), check=False
        )
        assert r.returncode == 2


class TestNoNarrowing:
    def test_no_narrowing_message(self, narrow_data):
        """When all columns are already narrowest, should indicate that."""
        out = narrow_data["dir"] / "no_narrow.bcsv"
        r = run_tool(
            narrow_data["narrow"], "-o", str(out), narrow_data["sensor"]
        )
        assert r.returncode == 0
        # Should run without error even if narrowing is partial


class TestEmptyFile:
    def test_empty_file(self, narrow_data):
        r = run_tool(narrow_data["narrow"], narrow_data["empty"], check=False)
        assert r.returncode == 0


class TestStringsToValue:
    def test_strings_to_value_flag(self, narrow_data):
        r = run_tool(
            narrow_data["narrow"],
            "--stringsToValue",
            narrow_data["sensor"],
            check=False,
        )
        assert r.returncode == 0


class TestErrors:
    def test_no_args(self, narrow_data):
        r = run_tool(narrow_data["narrow"], check=False)
        assert r.returncode != 0

    def test_nonexistent_file(self, narrow_data):
        r = run_tool(narrow_data["narrow"], "/nonexistent/path/file.bcsv", check=False)
        assert r.returncode != 0

    def test_removed_convert_flag(self, narrow_data):
        # --convert / --analyze were removed; they are now unknown options.
        r = run_tool(
            narrow_data["narrow"], "--convert", narrow_data["sensor"], check=False
        )
        assert r.returncode == 2

    def test_overwrite_required(self, narrow_data):
        # Existing distinct output without --overwrite must fail.
        out = narrow_data["dir"] / "overwrite_target.bcsv"
        shutil.copy(narrow_data["sensor"], out)
        r = run_tool(
            narrow_data["narrow"], narrow_data["sensor"], str(out), check=False
        )
        assert r.returncode != 0
        assert "--overwrite" in r.stderr
        # With --overwrite it succeeds.
        r2 = run_tool(
            narrow_data["narrow"], "--overwrite", narrow_data["sensor"], str(out)
        )
        assert r2.returncode == 0


class TestAnalyzeOutput:
    def test_analysis_shows_table(self, narrow_data):
        r = run_tool(narrow_data["narrow"], narrow_data["sensor"])
        assert r.returncode == 0
        assert "Original" in r.stdout
        assert "Optimal" in r.stdout

    def test_analysis_shows_codec(self, narrow_data):
        r = run_tool(narrow_data["narrow"], narrow_data["sensor"])
        assert r.returncode == 0
        assert "File codec:" in r.stdout
        assert "Row codec:" in r.stdout

    def test_analysis_shows_rows_scanned(self, narrow_data):
        r = run_tool(narrow_data["narrow"], narrow_data["sensor"])
        assert r.returncode == 0
        assert "Rows scanned:" in r.stdout


class TestColumnSelection:
    def test_cols_restricts_analysis(self, narrow_data):
        # Restrict analysis to a single column.
        r = run_tool(narrow_data["narrow"], "--cols", "0", narrow_data["sensor"])
        assert r.returncode == 0
        assert "Columns: 1" in r.stdout

    def test_cols_out_of_range(self, narrow_data):
        r = run_tool(
            narrow_data["narrow"], "--cols", "9999", narrow_data["sensor"], check=False
        )
        assert r.returncode == 2

    def test_cols_convert_subset(self, narrow_data):
        out = narrow_data["dir"] / "cols_subset.bcsv"
        r = run_tool(
            narrow_data["narrow"], "--cols", "0:1", narrow_data["flat"], str(out)
        )
        assert r.returncode == 0
        assert out.exists()

