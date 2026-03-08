"""Integration tests for bcsvValidate CLI.

Port of tests/test_bcsvValidate.sh — structure validation (Mode 1),
pattern validation (Mode 2), file comparison (Mode 3), and error handling.
"""
import pytest
from pathlib import Path
from helpers import run_tool, expect_fail

# ═══════════════════════════════════════════════════════════════════════
# Fixtures
# ═══════════════════════════════════════════════════════════════════════


@pytest.fixture(scope="module")
def validate_data(tmp_path_factory, tools):
    """Prepare test files for validation tests."""
    d = tmp_path_factory.mktemp("validate")

    # Workaround [LIB-4]: use packet_lz4 instead of default packet_lz4_batch
    # to avoid intermittent SIGSEGV in background decompression thread.
    # Revert to default codec once LIB-4 is fixed.
    FC = ["--file-codec", "packet_lz4"]

    # Generate sensor_noisy (500 rows)
    sensor = d / "sensor.bcsv"
    run_tool(tools["bcsvGenerator"], "-p", "sensor_noisy", "-n", "500",
             *FC, "-o", sensor)

    # Copy for identical comparison
    sensor_copy = d / "sensor_copy.bcsv"
    run_tool(tools["bcsvGenerator"], "-p", "sensor_noisy", "-n", "500",
             *FC, "-o", sensor_copy)

    # Different profile for mismatch tests
    mixed = d / "mixed.bcsv"
    run_tool(tools["bcsvGenerator"], "-p", "mixed_generic", "-n", "200",
             *FC, "-o", mixed)

    # Export sensor to CSV
    sensor_csv = d / "sensor.csv"
    run_tool(tools["bcsv2csv"], sensor, sensor_csv)

    # Small CSV → BCSV
    small_csv = d / "small.csv"
    small_csv.write_text("time,value,label\n1.0,10.5,foo\n2.0,20.1,bar\n3.0,30.9,baz\n")
    small_bcsv = d / "small.bcsv"
    run_tool(tools["csv2bcsv"], "--file-codec", "packet_lz4",
             small_csv, small_bcsv)

    return {
        "sensor": sensor, "sensor_copy": sensor_copy,
        "mixed": mixed, "sensor_csv": sensor_csv,
        "small_csv": small_csv, "small_bcsv": small_bcsv, "dir": d,
    }


# ═══════════════════════════════════════════════════════════════════════
# Help and list
# ═══════════════════════════════════════════════════════════════════════

class TestValidateHelp:
    def test_help(self, tools):
        r = run_tool(tools["bcsvValidate"], "--help")
        assert "Validate BCSV files" in r.stdout or "Validate" in r.stdout

    def test_list(self, tools):
        r = run_tool(tools["bcsvValidate"], "--list")
        assert "sensor_noisy" in r.stdout


# ═══════════════════════════════════════════════════════════════════════
# Mode 1 — Structure validation
# ═══════════════════════════════════════════════════════════════════════

class TestMode1Structure:
    def test_basic_exit_0(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     check=False)
        assert r.returncode == 0

    def test_passed(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        assert "PASSED" in (r.stdout + r.stderr)

    def test_row_count(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        assert "500" in (r.stdout + r.stderr)

    def test_format_version(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        combined = r.stdout + r.stderr
        assert "Format version:" in combined

    def test_packet_size(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        assert "Packet size:" in (r.stdout + r.stderr)

    def test_total_packets(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        assert "Total packets:" in (r.stdout + r.stderr)

    def test_avg_rows_per_packet(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        assert "Avg rows/packet:" in (r.stdout + r.stderr)

    def test_footer_present(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"])
        assert "Footer present:" in (r.stdout + r.stderr)

    def test_deep_exit_0(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--deep", check=False)
        assert r.returncode == 0

    def test_deep_passed(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--deep")
        assert "Deep check:" in (r.stdout + r.stderr)
        assert "PASSED" in (r.stdout + r.stderr)

    def test_json_exit_0(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json", check=False)
        assert r.returncode == 0

    def test_json_valid(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json")
        assert '"valid": true' in r.stdout

    def test_json_row_count(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json")
        assert '"row_count": 500' in r.stdout

    def test_json_packet_size(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json")
        assert '"packet_size":' in r.stdout

    def test_json_total_packets(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json")
        assert '"total_packets":' in r.stdout

    def test_json_avg_rows(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json")
        assert '"avg_rows_per_packet":' in r.stdout

    def test_json_columns(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--json")
        assert '"columns":' in r.stdout


# ═══════════════════════════════════════════════════════════════════════
# Mode 2 — Pattern validation
# ═══════════════════════════════════════════════════════════════════════

class TestMode2Pattern:
    def test_match_exit_0(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "sensor_noisy", "-n", "500", check=False)
        assert r.returncode == 0

    def test_match_passed(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "sensor_noisy", "-n", "500")
        assert "PASSED" in (r.stdout + r.stderr)

    def test_match_json(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "sensor_noisy", "-n", "500", "--json",
                     check=False)
        assert r.returncode == 0
        assert '"valid": true' in r.stdout

    def test_wrong_rowcount(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "sensor_noisy", "-n", "999", check=False)
        assert r.returncode == 1
        assert "MISMATCH" in (r.stdout + r.stderr) or \
               "FAILED" in (r.stdout + r.stderr)

    def test_wrong_profile(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "mixed_generic", check=False)
        assert r.returncode != 0

    def test_unknown_profile(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "nonexistent_profile", check=False)
        assert r.returncode == 2

    def test_unknown_profile_message(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "-p", "nonexistent_profile", check=False)
        combined = r.stdout + r.stderr
        assert "Unknown profile" in combined or "--list" in combined


# ═══════════════════════════════════════════════════════════════════════
# Mode 3 — BCSV vs BCSV comparison
# ═══════════════════════════════════════════════════════════════════════

class TestMode3BcsvVsBcsv:
    def test_identical_exit_0(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["sensor_copy"], check=False)
        assert r.returncode == 0

    def test_identical_passed(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["sensor_copy"])
        assert "PASSED" in (r.stdout + r.stderr)

    def test_layout_mismatch(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["mixed"], check=False)
        assert r.returncode == 1
        combined = r.stdout + r.stderr
        assert "MISMATCH" in combined or "FAILED" in combined

    def test_identical_json(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["sensor_copy"], "--json",
                     check=False)
        assert r.returncode == 0
        assert '"valid": true' in r.stdout


# ═══════════════════════════════════════════════════════════════════════
# Mode 3 — BCSV vs CSV comparison
# ═══════════════════════════════════════════════════════════════════════

class TestMode3BcsvVsCsv:
    def test_roundtrip_exit_0(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["sensor_csv"], check=False)
        assert r.returncode == 0

    def test_roundtrip_passed(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["sensor_csv"])
        assert "PASSED" in (r.stdout + r.stderr)

    def test_roundtrip_json(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", validate_data["sensor_csv"], "--json",
                     check=False)
        assert r.returncode == 0

    def test_small_dataset(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["small_bcsv"],
                     "--compare", validate_data["small_csv"], check=False)
        assert r.returncode == 0
        assert "PASSED" in (r.stdout + r.stderr)


# ═══════════════════════════════════════════════════════════════════════
# Error handling
# ═══════════════════════════════════════════════════════════════════════

class TestValidateErrors:
    def test_missing_file(self, tools):
        r = run_tool(tools["bcsvValidate"], "-i",
                     "/tmp/nonexistent_file_12345.bcsv", check=False)
        assert r.returncode == 2

    def test_no_input(self, tools):
        r = run_tool(tools["bcsvValidate"], check=False)
        assert r.returncode == 2

    def test_unknown_option(self, tools):
        r = run_tool(tools["bcsvValidate"], "--unknown-option", check=False)
        assert r.returncode == 2

    def test_compare_missing_file_b(self, tools, validate_data):
        r = run_tool(tools["bcsvValidate"], "-i", validate_data["sensor"],
                     "--compare", "/tmp/nonexistent_12345.bcsv", check=False)
        assert r.returncode == 2


# ═══════════════════════════════════════════════════════════════════════
# Pipeline: generate → validate
# ═══════════════════════════════════════════════════════════════════════

class TestValidatePipeline:
    # Workaround [LIB-4]: --file-codec packet_lz4 avoids default
    # packet_lz4_batch SIGSEGV. Revert once LIB-4 is fixed.

    def test_structure_deep(self, tools, tmp_path):
        bcsv = tmp_path / "pipeline.bcsv"
        run_tool(tools["bcsvGenerator"], "-p", "mixed_generic", "-n", "1000",
                 "--file-codec", "packet_lz4", "-o", bcsv)
        r = run_tool(tools["bcsvValidate"], "-i", bcsv, "--deep")
        assert "PASSED" in (r.stdout + r.stderr)

    def test_pattern_match(self, tools, tmp_path):
        bcsv = tmp_path / "pipeline.bcsv"
        run_tool(tools["bcsvGenerator"], "-p", "mixed_generic", "-n", "1000",
                 "--file-codec", "packet_lz4", "-o", bcsv)
        r = run_tool(tools["bcsvValidate"], "-i", bcsv, "-p", "mixed_generic",
                     "-n", "1000")
        assert "PASSED" in (r.stdout + r.stderr)

    def test_bcsv_vs_csv_roundtrip(self, tools, tmp_path):
        bcsv = tmp_path / "pipeline.bcsv"
        run_tool(tools["bcsvGenerator"], "-p", "mixed_generic", "-n", "1000",
                 "--file-codec", "packet_lz4", "-o", bcsv)
        csv = tmp_path / "pipeline.csv"
        run_tool(tools["bcsv2csv"], bcsv, csv)
        r = run_tool(tools["bcsvValidate"], "-i", bcsv, "--compare", csv)
        assert "PASSED" in (r.stdout + r.stderr)
