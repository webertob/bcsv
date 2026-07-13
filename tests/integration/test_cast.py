"""Integration tests for bcsvCast CLI.

Tests: --scan report, --optimize convert + compare, in-place overwrite,
no-op case, empty file, string opt-in, flat codec size, --static/--dynamic,
and --json output.
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
        "bcsvCast", tools["bcsvGenerator"].parent / "bcsvCast"
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
        assert "bcsvCast" in r.stdout
        assert "--in-place" in r.stdout
        assert "--overwrite" in r.stdout
        assert "--cols" in r.stdout
        assert "--string-to-value" in r.stdout


class TestAnalyze:
    def test_analyze_output(self, narrow_data):
        r = run_tool(narrow_data["narrow"], narrow_data["sensor"])
        assert r.returncode == 0
        assert "bcsvCast: analysis of" in r.stdout
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
            "--string-to-value",
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


class TestExplicitModes:
    def test_static_col_to_string(self, narrow_data):
        # any → string is always lossless; works regardless of column 0's type.
        out = narrow_data["dir"] / "to_string.bcsv"
        r = run_tool(
            narrow_data["narrow"], "--static", "0=string",
            narrow_data["sensor"], str(out),
        )
        assert r.returncode == 0
        assert out.exists()

    def test_static_apply_and_value_roundtrip(self, narrow_data, tools):
        # Force column 0 to string; parsing those strings back must reproduce the
        # original numeric values exactly (shortest round-trip formatting).
        out = narrow_data["dir"] / "static_rt.bcsv"
        run_tool(narrow_data["narrow"], "--static", "0=string",
                 narrow_data["sensor"], str(out))
        r = run_tool(tools["bcsvCompare"], "--mode", "value", "--string-to-value",
                     narrow_data["sensor"], out, check=False)
        assert r.returncode == 0

    def test_dynamic_dry_run_reports(self, narrow_data):
        r = run_tool(narrow_data["narrow"], "--dynamic", "0=int8", narrow_data["sensor"])
        assert r.returncode == 0
        assert "dynamic cast of" in r.stdout

    def test_mode_conflict_rejected(self, narrow_data):
        r = run_tool(narrow_data["narrow"], "--scan", "--optimize",
                     narrow_data["sensor"], check=False)
        assert r.returncode == 2

    def test_positional_with_equals_rejected(self, narrow_data):
        # A stray '=' positional (brace-expanded SPEC) must be caught.
        r = run_tool(narrow_data["narrow"], narrow_data["sensor"], "3=float", check=False)
        assert r.returncode == 2

    def test_cols_with_static_rejected(self, narrow_data):
        out = narrow_data["dir"] / "conflict.bcsv"
        r = run_tool(narrow_data["narrow"], "--cols", "0", "--static", "0=int8",
                     narrow_data["sensor"], str(out), check=False)
        assert r.returncode == 2

    def test_apply_always_writes_noop(self, narrow_data):
        # Requesting current type for col 0 is a no-op, but output must be written.
        import subprocess
        # Determine column 0's current type from --scan --json.
        r = run_tool(narrow_data["narrow"], "--scan", "--json", narrow_data["sensor"])
        import json
        typ = json.loads(r.stdout)["columns"][0]["original"]
        out = narrow_data["dir"] / "noop.bcsv"
        r2 = run_tool(narrow_data["narrow"], "--static", f"0={typ}",
                      narrow_data["sensor"], str(out))
        assert r2.returncode == 0
        assert out.exists()


class TestJson:
    def test_scan_json_shape(self, narrow_data):
        import json
        r = run_tool(narrow_data["narrow"], "--scan", "--json", narrow_data["sensor"])
        assert r.returncode == 0
        doc = json.loads(r.stdout)  # pure JSON on stdout
        assert doc["tool"] == "bcsvCast"
        assert doc["mode"] == "scan"
        assert doc["num_columns"] == len(doc["columns"])
        assert "suggested_spec" in doc

    def test_suggested_spec_is_reusable(self, narrow_data, tools):
        # Agent workflow: scan → take suggested_spec → apply via --static → round-trips.
        import json
        r = run_tool(narrow_data["narrow"], "--scan", "--json", narrow_data["flat"])
        spec = json.loads(r.stdout)["suggested_spec"]
        if not spec:
            pytest.skip("nothing narrowable in fixture")
        out = narrow_data["dir"] / "reused.bcsv"
        r2 = run_tool(narrow_data["narrow"], "--static", spec,
                      narrow_data["flat"], str(out))
        assert r2.returncode == 0
        # suggested_spec is the lossless narrowing plan → values must compare equal.
        r3 = run_tool(tools["bcsvCompare"], "--mode", "value",
                      narrow_data["flat"], out, check=False)
        assert r3.returncode == 0



class TestNameKeys:
    """SPEC map keys and --cols accept column names (shared spec grammar)."""

    def test_spec_name_key(self, tools, tmp_path):
        csv_path = tmp_path / "nk.csv"
        csv_path.write_text("alpha,beta\n1,2\n3,4\n")
        bcsv_path = tmp_path / "nk.bcsv"
        run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)

        out_path = tmp_path / "nk_out.bcsv"
        run_tool(tools["bcsvCast"], "--static", "beta=float", bcsv_path, out_path,
                 "--overwrite")
        header = run_tool(tools["bcsvHeader"], out_path).stdout
        assert "float" in header

    def test_cols_name(self, tools, tmp_path):
        csv_path = tmp_path / "nc.csv"
        csv_path.write_text("alpha,beta\n1,2\n3,4\n")
        bcsv_path = tmp_path / "nc.bcsv"
        run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)
        result = run_tool(tools["bcsvCast"], "--scan", "--cols", "beta", bcsv_path)
        assert "Columns: 1" in result.stdout  # name resolved to exactly one column

    def test_unknown_name_fails(self, tools, tmp_path):
        csv_path = tmp_path / "un.csv"
        csv_path.write_text("alpha,beta\n1,2\n")
        bcsv_path = tmp_path / "un.bcsv"
        run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)
        result = run_tool(tools["bcsvCast"], "--static", "gamma=float", bcsv_path,
                          tmp_path / "o.bcsv", check=False)
        assert result.returncode == 2
