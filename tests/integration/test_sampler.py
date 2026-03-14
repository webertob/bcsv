"""Integration tests for bcsvSampler CLI.

Port of tests/test_bcsvSampler.sh — test vectors, value-level spot checks,
encoding variants, disassembly, and summary output.
"""
import pytest
from pathlib import Path
from helpers import run_tool, csv_rows, csv_cell, float_eq, expect_fail

# ── Canonical dataset (7 rows × 5 columns) ──────────────────────────

CANON_CSV_DATA = """\
timestamp,temperature,status,flags,counter
1.0,20.5,ok,6,0
2.0,21.0,ok,7,1
3.0,21.0,warn,3,2
4.0,55.0,alarm,5,3
5.0,55.0,alarm,5,100
6.0,22.0,ok,7,101
7.0,22.5,ok,6,102
"""


@pytest.fixture(scope="module")
def sampler_data(tmp_path_factory, tools):
    """Create canonical CSV and BCSV for sampler tests."""
    d = tmp_path_factory.mktemp("sampler")
    csv_path = d / "canonical.csv"
    csv_path.write_text(CANON_CSV_DATA)

    bcsv_path = d / "canonical.bcsv"
    run_tool(tools["csv2bcsv"], "--row-codec", "delta",
             csv_path, bcsv_path)

    return {"csv": csv_path, "bcsv": bcsv_path, "dir": d}


def _run_sampler(tools, sampler_data, tmp_path, label, expected_rows,
                 *extra_args):
    """Run bcsvSampler and verify row count. Returns the CSV output path."""
    out_bcsv = tmp_path / f"{label}.bcsv"
    out_csv = tmp_path / f"{label}.csv"
    r = run_tool(tools["bcsvSampler"], *extra_args, "-f",
                 sampler_data["bcsv"], out_bcsv, check=False, timeout=10)

    if out_bcsv.exists():
        run_tool(tools["bcsv2csv"], out_bcsv, out_csv, check=False)
        got = csv_rows(out_csv) if out_csv.exists() else -1
    else:
        got = -1

    assert got == expected_rows, \
        f"{label}: got {got} rows, expected {expected_rows}"
    return out_csv


# ═══════════════════════════════════════════════════════════════════════
# Test Vectors
# ═══════════════════════════════════════════════════════════════════════

class TestSamplerVectors:
    # -- Baseline --
    def test_passthrough(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV01", 7,
                     "-c", "true")

    def test_false(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV02", 0,
                     "-c", "false")

    # -- Threshold & Comparison --
    def test_threshold(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV03", 2,
                     "-c", "X[0][1] > 50.0", "-s", "X[0][0], X[0][1]")

    def test_str_eq(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV06", 2,
                     "-c", 'X[0][2] == "alarm"', "-s", "X[0][0], X[0][2]")

    def test_str_neq(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV07", 3,
                     "-c", 'X[0][2] != "ok"', "-s", "X[0][0], X[0][2]")

    def test_not(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV15", 3,
                     "-c", '!(X[0][2] == "ok")', "-s", "X[0][0], X[0][2]")

    def test_truthiness(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV16", 6,
                     "-c", "X[0][4]", "-s", "X[0][0], X[0][4]")

    # -- Edge Detection & Window --
    def test_edge_trunc(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV04", 4,
                     "-c", "X[0][1] != X[-1][1]",
                     "-s", "X[0][0], X[-1][1], X[0][1]")

    def test_edge_expand(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV05", 4,
                     "-c", "X[0][1] != X[-1][1]",
                     "-s", "X[0][0], X[-1][1], X[0][1]", "-m", "expand")

    def test_lookahead(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV17", 3,
                     "-c", "X[+1][1] > X[0][1]",
                     "-s", "X[0][0], X[0][1], X[+1][1]")

    def test_local_max(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV19", 0,
                     "-c", "X[0][1] > X[-1][1] && X[0][1] > X[+1][1]",
                     "-s", "X[0][0], X[0][1]")

    # -- Boolean & Short-Circuit --
    def test_or(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV09", 3,
                     "-c", 'X[0][1] > 50.0 || X[0][2] == "warn"',
                     "-s", "X[0][0]")

    def test_modulo(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV11", 4,
                     "-c", "X[0][4] % 2 == 0", "-s", "X[0][0], X[0][4]")

    # -- Bitwise --
    def test_bitand(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV12", 6,
                     "-c", "(X[0][3] & 4) != 0",
                     "-s", "X[0][0], X[0][3]")

    # -- Signal Processing --
    def test_gradient(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV10", 6,
                     "-c", "true",
                     "-s", "X[0][0], X[0][1] - X[-1][1]")

    def test_interp(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV30", 6,
                     "-c", "true",
                     "-s", "X[0][0], (X[-1][1] + X[0][1]) / 2.0")

    def test_mavg3(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV32", 5,
                     "-c", "true",
                     "-s", "X[0][0], (X[-1][1] + X[0][1] + X[+1][1]) / 3.0")

    def test_dydx(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV34", 6,
                     "-c", "true",
                     "-s", "X[0][0], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])")

    # -- Precedence --
    def test_precedence(self, tools, sampler_data, tmp_path):
        _run_sampler(tools, sampler_data, tmp_path, "TV29", 3,
                     "-c", "X[0][4] % (2 + 1) == 0",
                     "-s", "X[0][0], X[0][4]")


# ═══════════════════════════════════════════════════════════════════════
# Compile Errors (expected failures)
# ═══════════════════════════════════════════════════════════════════════

class TestSamplerCompileErrors:
    def test_str_arith(self, tools, sampler_data, tmp_path):
        assert expect_fail(tools["bcsvSampler"], "-c", "X[0][2] + 1 > 0",
                           sampler_data["bcsv"], tmp_path / "bad.bcsv")

    def test_str_order(self, tools, sampler_data, tmp_path):
        assert expect_fail(tools["bcsvSampler"], "-c", 'X[0][2] > "ok"',
                           sampler_data["bcsv"], tmp_path / "bad.bcsv")

    def test_bad_col_idx(self, tools, sampler_data, tmp_path):
        assert expect_fail(tools["bcsvSampler"], "-c", "X[0][99] > 0",
                           sampler_data["bcsv"], tmp_path / "bad.bcsv")

    def test_bad_col_name(self, tools, sampler_data, tmp_path):
        assert expect_fail(tools["bcsvSampler"], "-c",
                           'X[0]["nonexistent"] > 0',
                           sampler_data["bcsv"], tmp_path / "bad.bcsv")


# ═══════════════════════════════════════════════════════════════════════
# Value-level spot checks
# ═══════════════════════════════════════════════════════════════════════

class TestSamplerValues:
    def test_tv03_timestamps(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV03", 2,
                           "-c", "X[0][1] > 50.0", "-s", "X[0][0], X[0][1]")
        assert float_eq(csv_cell(csv, 1, 1), 4.0)
        assert float_eq(csv_cell(csv, 2, 1), 5.0)

    def test_tv04_edge_timestamps(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV04", 4,
                           "-c", "X[0][1] != X[-1][1]",
                           "-s", "X[0][0], X[-1][1], X[0][1]")
        assert float_eq(csv_cell(csv, 1, 1), 2.0)
        assert float_eq(csv_cell(csv, 2, 1), 4.0)
        assert float_eq(csv_cell(csv, 3, 1), 6.0)
        assert float_eq(csv_cell(csv, 4, 1), 7.0)

    def test_tv06_status_alarm(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV06", 2,
                           "-c", 'X[0][2] == "alarm"',
                           "-s", "X[0][0], X[0][2]")
        assert csv_cell(csv, 1, 2) == "alarm"
        assert csv_cell(csv, 2, 2) == "alarm"

    def test_tv10_gradient(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV10", 6,
                           "-c", "true",
                           "-s", "X[0][0], X[0][1] - X[-1][1]")
        assert float_eq(csv_cell(csv, 1, 2), 0.5)
        assert float_eq(csv_cell(csv, 3, 2), 34.0)
        assert float_eq(csv_cell(csv, 5, 2), -33.0)

    def test_tv11_modulo_counters(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV11", 4,
                           "-c", "X[0][4] % 2 == 0",
                           "-s", "X[0][0], X[0][4]")
        assert csv_cell(csv, 1, 2) == "0"
        assert csv_cell(csv, 2, 2) == "2"
        assert csv_cell(csv, 3, 2) == "100"
        assert csv_cell(csv, 4, 2) == "102"

    def test_tv30_interpolation(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV30", 6,
                           "-c", "true",
                           "-s", "X[0][0], (X[-1][1] + X[0][1]) / 2.0")
        assert float_eq(csv_cell(csv, 1, 2), 20.75)
        assert float_eq(csv_cell(csv, 3, 2), 38.0)
        assert float_eq(csv_cell(csv, 6, 2), 22.25)

    def test_tv32_moving_avg(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV32", 5,
                           "-c", "true",
                           "-s", "X[0][0], (X[-1][1] + X[0][1] + X[+1][1]) / 3.0")
        assert float_eq(csv_cell(csv, 1, 2), 20.833, 0.1)
        assert float_eq(csv_cell(csv, 2, 2), 32.333, 0.1)
        assert float_eq(csv_cell(csv, 5, 2), 33.167, 0.1)

    def test_tv29_precedence_counters(self, tools, sampler_data, tmp_path):
        csv = _run_sampler(tools, sampler_data, tmp_path, "val_TV29", 3,
                           "-c", "X[0][4] % (2 + 1) == 0",
                           "-s", "X[0][0], X[0][4]")
        assert csv_cell(csv, 1, 2) == "0"
        assert csv_cell(csv, 2, 2) == "3"
        assert csv_cell(csv, 3, 2) == "102"


# ═══════════════════════════════════════════════════════════════════════
# Encoding variants
# ═══════════════════════════════════════════════════════════════════════

class TestSamplerEncodings:
    @pytest.mark.parametrize("variant,extra_args", [
        ("default", []),
        ("flat-row", ["--row-codec", "flat"]),
        ("packet-file", ["--file-codec", "packet"]),
        ("packet-lz4-file", ["--file-codec", "packet_lz4"]),
        ("flat-packet", ["--row-codec", "flat", "--file-codec", "packet"]),
    ])
    def test_encoding_variant(self, tools, sampler_data, tmp_path,
                              variant, extra_args):
        out_bcsv = tmp_path / f"enc_{variant}.bcsv"
        out_csv = tmp_path / f"enc_{variant}.csv"
        run_tool(tools["bcsvSampler"], "-c", "X[0][1] > 50.0",
                 *extra_args, "--file-codec", "packet_lz4", "-f", sampler_data["bcsv"], out_bcsv,
                 timeout=10, check=False)
        run_tool(tools["bcsv2csv"], out_bcsv, out_csv, check=False)
        assert csv_rows(out_csv) == 2


# ═══════════════════════════════════════════════════════════════════════
# Disassembly & Summary smoke tests
# ═══════════════════════════════════════════════════════════════════════

class TestSamplerMisc:
    def test_disassembly(self, tools, sampler_data):
        r = run_tool(tools["bcsvSampler"], "--disassemble",
                     "-c", "X[0][1] > 50.0", "-s", "X[0][0], X[0][1]",
                     sampler_data["bcsv"], check=False, timeout=10)
        assert "HALT_COND" in r.stdout
        assert "HALT_SEL" in r.stdout

    def test_verbose_summary(self, tools, sampler_data, tmp_path):
        out = tmp_path / "summary.bcsv"
        r = run_tool(tools["bcsvSampler"], "-v", "-c", "X[0][1] > 50.0",
                     "-f", sampler_data["bcsv"], out,
                     check=False, timeout=10)
        combined = r.stdout + r.stderr
        assert "Pass rate" in combined
        assert "Rows written" in combined
        assert "Encoding" in combined
