"""Integration tests for BCSV CLI tools.

Port of tests/test_cli_tools.sh — CSV round-trip, codec variants, head/tail,
header display, generator, pipeline, and edge cases.
"""
import pytest
from pathlib import Path
from helpers import (
    run_tool, csv_rows, csv_cell, csv_header_col1,
    csv_equal, expect_fail, line_count,
)

# ── Canonical dataset (20 rows × 4 columns) ─────────────────────────

CANON_CSV_DATA = """\
id,name,temperature,active
1,alpha,23.5,true
2,bravo,18.2,false
3,charlie,31.0,true
4,delta,27.8,true
5,echo,15.6,false
6,foxtrot,22.1,true
7,golf,29.4,false
8,hotel,33.7,true
9,india,20.3,true
10,juliet,25.9,false
11,kilo,19.1,true
12,lima,28.6,false
13,mike,24.0,true
14,november,21.5,true
15,oscar,30.2,false
16,papa,17.8,true
17,quebec,26.3,false
18,romeo,32.1,true
19,sierra,23.9,true
20,tango,16.4,false
"""

CANON_ROWS = 20


@pytest.fixture(scope="module")
def canon_csv(tmp_path_factory, tools):
    """Create canonical CSV and default BCSV files."""
    d = tmp_path_factory.mktemp("cli")
    csv_path = d / "canon.csv"
    csv_path.write_text(CANON_CSV_DATA)

    bcsv_path = d / "default.bcsv"
    run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)

    return {"csv": csv_path, "bcsv": bcsv_path, "dir": d}


# ═══════════════════════════════════════════════════════════════════════
# csv2bcsv tests
# ═══════════════════════════════════════════════════════════════════════

class TestCsv2Bcsv:
    def test_default_creates_file(self, canon_csv):
        assert canon_csv["bcsv"].stat().st_size > 0

    def test_roundtrip_row_count(self, tools, canon_csv, tmp_path):
        out = tmp_path / "rt.csv"
        run_tool(tools["bcsv2csv"], canon_csv["bcsv"], "-o", out)
        assert csv_rows(out) == CANON_ROWS

    def test_roundtrip_data_parity(self, tools, canon_csv, tmp_path):
        out = tmp_path / "rt.csv"
        run_tool(tools["bcsv2csv"], canon_csv["bcsv"], "-o", out)
        assert csv_equal(canon_csv["csv"], out)

    def test_roundtrip_header(self, tools, canon_csv, tmp_path):
        out = tmp_path / "rt.csv"
        run_tool(tools["bcsv2csv"], canon_csv["bcsv"], "-o", out)
        assert csv_header_col1(out) == "id"

    @pytest.mark.parametrize("rcodec", ["flat", "zoh", "delta"])
    def test_row_codec_roundtrip(self, tools, canon_csv, tmp_path, rcodec):
        bcsv = tmp_path / f"rc_{rcodec}.bcsv"
        out = tmp_path / f"rt_{rcodec}.csv"
        run_tool(tools["csv2bcsv"], "--row-codec", rcodec,
                 "--file-codec", "packet_lz4", "-f", canon_csv["csv"], bcsv)
        run_tool(tools["bcsv2csv"], bcsv, "-o", out)
        assert csv_rows(out) == CANON_ROWS

    @pytest.mark.parametrize("fcodec", [
        "stream", "stream_lz4", "packet", "packet_lz4", "packet_lz4_batch",
    ])
    def test_file_codec_roundtrip(self, tools, canon_csv, tmp_path, fcodec):
        bcsv = tmp_path / f"fc_{fcodec}.bcsv"
        out = tmp_path / f"rt_{fcodec}.csv"
        run_tool(tools["csv2bcsv"], "--file-codec", fcodec, "-f", canon_csv["csv"], bcsv)
        run_tool(tools["bcsv2csv"], bcsv, "-o", out)
        assert csv_rows(out) == CANON_ROWS

    @pytest.mark.parametrize("rcodec", ["flat", "zoh", "delta"])
    @pytest.mark.parametrize("fcodec", [
        "stream", "stream_lz4", "packet", "packet_lz4", "packet_lz4_batch",
    ])
    def test_codec_matrix_parity(self, tools, canon_csv, tmp_path, rcodec, fcodec):
        bcsv = tmp_path / f"matrix_{rcodec}_{fcodec}.bcsv"
        out = tmp_path / f"rt_matrix_{rcodec}_{fcodec}.csv"
        run_tool(tools["csv2bcsv"], "--row-codec", rcodec, "--file-codec", fcodec,
                 "-f", canon_csv["csv"], bcsv)
        run_tool(tools["bcsv2csv"], bcsv, "-o", out)
        assert csv_equal(canon_csv["csv"], out)

    def test_invalid_row_codec(self, tools, canon_csv, tmp_path):
        assert expect_fail(tools["csv2bcsv"], "--row-codec", "bogus",
                           "-f", canon_csv["csv"], tmp_path / "bad.bcsv")

    def test_invalid_file_codec(self, tools, canon_csv, tmp_path):
        assert expect_fail(tools["csv2bcsv"], "--file-codec", "bogus",
                           "-f", canon_csv["csv"], tmp_path / "bad.bcsv")

    def test_help_exits_zero(self, tools):
        run_tool(tools["csv2bcsv"], "--help")

    def test_missing_input(self, tools):
        assert expect_fail(tools["csv2bcsv"])


# ═══════════════════════════════════════════════════════════════════════
# bcsv2csv tests
# ═══════════════════════════════════════════════════════════════════════

class TestBcsv2Csv:
    def test_basic_output(self, tools, canon_csv, tmp_path):
        out = tmp_path / "basic.csv"
        run_tool(tools["bcsv2csv"], canon_csv["bcsv"], "-o", out)
        assert csv_rows(out) == CANON_ROWS

    def test_stdout_mode(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsv2csv"], canon_csv["bcsv"], "-o", "-")
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == CANON_ROWS  # subtract header

    def test_stdout_verbose_clean(self, tools, canon_csv, tmp_path):
        """B1 fix: verbose must NOT corrupt stdout data."""
        r = run_tool(tools["bcsv2csv"], "-v", canon_csv["bcsv"], "-o", "-")
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == CANON_ROWS

    def test_stdout_benchmark_json_no_leak(self, tools, canon_csv, tmp_path):
        """B3 fix: JSON must NOT appear in stdout."""
        r = run_tool(tools["bcsv2csv"], "--benchmark", "--json",
                     canon_csv["bcsv"], "-o", "-", check=False)
        assert '"tool":"bcsv2csv"' not in r.stdout

    def test_stdout_benchmark_row_count(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsv2csv"], "--benchmark", "--json",
                     canon_csv["bcsv"], "-o", "-", check=False)
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == CANON_ROWS

    def test_no_header(self, tools, canon_csv, tmp_path):
        out = tmp_path / "noheader.csv"
        run_tool(tools["bcsv2csv"], "--no-header", canon_csv["bcsv"], "-o", out)
        assert line_count(out) == CANON_ROWS

    def test_slice(self, tools, canon_csv, tmp_path):
        out = tmp_path / "slice.csv"
        run_tool(tools["bcsv2csv"], "--slice", "5:10", canon_csv["bcsv"], "-o", out)
        assert csv_rows(out) == 5

    def test_first_last_row(self, tools, canon_csv, tmp_path):
        out = tmp_path / "range.csv"
        run_tool(tools["bcsv2csv"], "--firstRow", "2", "--lastRow", "4",
                 canon_csv["bcsv"], "-o", out)
        assert csv_rows(out) == 3

    def test_semicolon_delimiter(self, tools, canon_csv, tmp_path):
        out = tmp_path / "semi.csv"
        run_tool(tools["bcsv2csv"], "-d", ";", canon_csv["bcsv"], "-o", out)
        header = out.read_text().splitlines()[0]
        assert ";" in header

    def test_help_exits_zero(self, tools):
        run_tool(tools["bcsv2csv"], "--help")

    def test_missing_input(self, tools):
        assert expect_fail(tools["bcsv2csv"])

    def test_nonexistent_file(self, tools, tmp_path):
        assert expect_fail(tools["bcsv2csv"], "nonexistent_file_xyz.bcsv",
                           "-o", tmp_path / "nope.csv")


# ═══════════════════════════════════════════════════════════════════════
# bcsvHead tests
# ═══════════════════════════════════════════════════════════════════════

class TestBcsvHead:
    def test_default_10_rows(self, tools, canon_csv, tmp_path):
        out = tmp_path / "head.csv"
        run_tool(tools["bcsvHead"], canon_csv["bcsv"], check=False,
                 timeout=10)  # stdout mode
        r = run_tool(tools["bcsvHead"], canon_csv["bcsv"])
        (tmp_path / "head.csv").write_text(r.stdout)
        assert csv_rows(tmp_path / "head.csv") == 10

    def test_header_preserved(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvHead"], canon_csv["bcsv"])
        (tmp_path / "head.csv").write_text(r.stdout)
        assert csv_header_col1(tmp_path / "head.csv") == "id"

    def test_n5(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvHead"], "-n", "5", canon_csv["bcsv"])
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == 5

    def test_n_larger_than_file(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvHead"], "-n", "100", canon_csv["bcsv"])
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == CANON_ROWS

    def test_no_header(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvHead"], "--no-header", "-n", "5", canon_csv["bcsv"])
        lines = r.stdout.strip().splitlines()
        assert len(lines) == 5

    def test_first_row_value(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvHead"], canon_csv["bcsv"])
        f = tmp_path / "head.csv"
        f.write_text(r.stdout)
        assert csv_cell(f, 1, 1) == "1"

    def test_help_exits_zero(self, tools):
        run_tool(tools["bcsvHead"], "--help")

    def test_unknown_option(self, tools, canon_csv):
        assert expect_fail(tools["bcsvHead"], "--bogus", canon_csv["bcsv"])


# ═══════════════════════════════════════════════════════════════════════
# bcsvTail tests
# ═══════════════════════════════════════════════════════════════════════

class TestBcsvTail:
    def test_default_10_rows(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], canon_csv["bcsv"])
        f = tmp_path / "tail.csv"
        f.write_text(r.stdout)
        assert csv_rows(f) == 10

    def test_header_preserved(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], canon_csv["bcsv"])
        f = tmp_path / "tail.csv"
        f.write_text(r.stdout)
        assert csv_header_col1(f) == "id"

    def test_last_row_id(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], canon_csv["bcsv"])
        f = tmp_path / "tail.csv"
        f.write_text(r.stdout)
        assert csv_cell(f, 10, 1) == "20"

    def test_first_displayed_row_id(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], canon_csv["bcsv"])
        f = tmp_path / "tail.csv"
        f.write_text(r.stdout)
        assert csv_cell(f, 1, 1) == "11"

    def test_n3(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], "-n", "3", canon_csv["bcsv"])
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == 3

    def test_sequential_fallback(self, tools, canon_csv, tmp_path):
        """Stream-mode file has no footer → sequential path."""
        stream = tmp_path / "stream.bcsv"
        run_tool(tools["csv2bcsv"], "--file-codec", "stream", "-f",
                 canon_csv["csv"], stream)
        r = run_tool(tools["bcsvTail"], "-n", "5", stream)
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == 5

    def test_sequential_last_row(self, tools, canon_csv, tmp_path):
        stream = tmp_path / "stream.bcsv"
        run_tool(tools["csv2bcsv"], "--file-codec", "stream", "-f",
                 canon_csv["csv"], stream)
        r = run_tool(tools["bcsvTail"], "-n", "5", stream)
        f = tmp_path / "tail_seq.csv"
        f.write_text(r.stdout)
        assert csv_cell(f, 5, 1) == "20"

    def test_sequential_header_b4(self, tools, canon_csv, tmp_path):
        """B4 fix: sequential path must preserve header."""
        stream = tmp_path / "stream.bcsv"
        run_tool(tools["csv2bcsv"], "--file-codec", "stream", "-f",
                 canon_csv["csv"], stream)
        r = run_tool(tools["bcsvTail"], "-n", "5", stream)
        f = tmp_path / "tail_seq.csv"
        f.write_text(r.stdout)
        assert csv_header_col1(f) == "id"

    def test_n_larger_than_file(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], "-n", "999", canon_csv["bcsv"])
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == CANON_ROWS

    def test_no_header(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvTail"], "--no-header", "-n", "5", canon_csv["bcsv"])
        lines = r.stdout.strip().splitlines()
        assert len(lines) == 5

    def test_help_exits_zero(self, tools):
        run_tool(tools["bcsvTail"], "--help")

    def test_unknown_option(self, tools, canon_csv):
        assert expect_fail(tools["bcsvTail"], "--bogus", canon_csv["bcsv"])

    def test_direct_access_delta(self, tools, canon_csv, tmp_path):
        bcsv = tmp_path / "da_delta.bcsv"
        run_tool(tools["csv2bcsv"], "--row-codec", "delta", "--file-codec",
                 "packet_lz4", "-f", canon_csv["csv"], bcsv)
        r = run_tool(tools["bcsvTail"], "-n", "1", bcsv)
        f = tmp_path / "tail_da.csv"
        f.write_text(r.stdout)
        assert csv_cell(f, 1, 1) == "20"

    def test_direct_access_zoh(self, tools, canon_csv, tmp_path):
        bcsv = tmp_path / "da_zoh.bcsv"
        run_tool(tools["csv2bcsv"], "--row-codec", "zoh", "--file-codec",
                 "packet_lz4", "-f", canon_csv["csv"], bcsv)
        r = run_tool(tools["bcsvTail"], "-n", "1", bcsv)
        f = tmp_path / "tail_zoh.csv"
        f.write_text(r.stdout)
        assert csv_cell(f, 1, 1) == "20"


# ═══════════════════════════════════════════════════════════════════════
# bcsvHeader tests
# ═══════════════════════════════════════════════════════════════════════

class TestBcsvHeader:
    def test_column_names(self, tools, canon_csv):
        r = run_tool(tools["bcsvHeader"], canon_csv["bcsv"])
        assert "id" in r.stdout and "temperature" in r.stdout

    def test_column_count(self, tools, canon_csv):
        r = run_tool(tools["bcsvHeader"], canon_csv["bcsv"])
        assert "4" in r.stdout

    def test_verbose_row_info(self, tools, canon_csv):
        r = run_tool(tools["bcsvHeader"], "-v", canon_csv["bcsv"], check=False)
        combined = r.stdout + r.stderr
        assert "row" in combined.lower()

    def test_verbose_codec_names(self, tools, canon_csv):
        r = run_tool(tools["bcsvHeader"], "-v", canon_csv["bcsv"], check=False)
        combined = r.stdout + r.stderr
        assert "Row codec: delta" in combined
        assert "File codec: packet_lz4_batch" in combined

    def test_verbose_flat_stream(self, tools, canon_csv, tmp_path):
        bcsv = tmp_path / "flat_stream.bcsv"
        run_tool(tools["csv2bcsv"], "--row-codec", "flat", "--file-codec",
                 "stream", "-f", canon_csv["csv"], bcsv)
        r = run_tool(tools["bcsvHeader"], "-v", bcsv, check=False)
        combined = r.stdout + r.stderr
        assert "Row codec: flat" in combined
        assert "File codec: stream" in combined

    def test_help_exits_zero(self, tools):
        run_tool(tools["bcsvHeader"], "--help")

    def test_unknown_option(self, tools, canon_csv):
        assert expect_fail(tools["bcsvHeader"], "--bogus", canon_csv["bcsv"])

    def test_nonexistent_file(self, tools):
        assert expect_fail(tools["bcsvHeader"], "nonexistent_xyz.bcsv")


# ═══════════════════════════════════════════════════════════════════════
# bcsvGenerator tests
# ═══════════════════════════════════════════════════════════════════════

class TestBcsvGenerator:
    def test_basic_generation(self, tools, tmp_path):
        out = tmp_path / "gen.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", out, "-f", "-n", "100")
        assert out.stat().st_size > 0

    def test_roundtrip_100_rows(self, tools, tmp_path):
        bcsv = tmp_path / "gen.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", bcsv, "-f", "-n", "100")
        csv_out = tmp_path / "gen.csv"
        run_tool(tools["bcsv2csv"], bcsv, "-o", csv_out)
        assert csv_rows(csv_out) == 100

    def test_flat_codec(self, tools, tmp_path):
        bcsv = tmp_path / "gen_flat.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", bcsv, "-f", "-n", "50",
                 "--row-codec", "flat")
        csv_out = tmp_path / "gen_flat.csv"
        run_tool(tools["bcsv2csv"], bcsv, "-o", csv_out)
        assert csv_rows(csv_out) == 50

    def test_stream_file_codec(self, tools, tmp_path):
        bcsv = tmp_path / "gen_stream.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", bcsv, "-f", "-n", "50",
                 "--file-codec", "stream")
        csv_out = tmp_path / "gen_stream.csv"
        run_tool(tools["bcsv2csv"], bcsv, "-o", csv_out)
        assert csv_rows(csv_out) == 50

    def test_list_profiles(self, tools):
        r = run_tool(tools["bcsvGenerator"], "--list")
        assert "mixed_generic" in r.stdout

    def test_random_mode(self, tools, tmp_path):
        bcsv = tmp_path / "gen_random.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", bcsv, "-f", "-n", "30",
                 "-d", "random")
        csv_out = tmp_path / "gen_random.csv"
        run_tool(tools["bcsv2csv"], bcsv, "-o", csv_out)
        assert csv_rows(csv_out) == 30

    def test_invalid_profile(self, tools, tmp_path):
        assert expect_fail(tools["bcsvGenerator"], "-o",
                           tmp_path / "bad.bcsv", "-p", "nonexistent_profile")

    def test_invalid_codec(self, tools, tmp_path):
        assert expect_fail(tools["bcsvGenerator"], "-o",
                           tmp_path / "bad.bcsv", "--row-codec", "bogus")

    def test_help_exits_zero(self, tools):
        run_tool(tools["bcsvGenerator"], "--help")

    def test_overwrite_protection(self, tools, tmp_path):
        out = tmp_path / "prot.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", out, "-f", "-n", "10")
        assert expect_fail(tools["bcsvGenerator"], "-o", out, "-n", "10")


# ═══════════════════════════════════════════════════════════════════════
# Pipeline tests
# ═══════════════════════════════════════════════════════════════════════

class TestPipeline:
    def test_generator_head(self, tools, tmp_path):
        bcsv = tmp_path / "pipe1.bcsv"
        run_tool(tools["bcsvGenerator"], "-o", bcsv, "-f", "-n", "50",
                 "--file-codec", "packet_lz4")
        r = run_tool(tools["bcsvHead"], "-n", "5", bcsv)
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == 5

    def test_csv2bcsv_stdout_pipe(self, tools, canon_csv, tmp_path):
        bcsv = tmp_path / "pipe2.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--file-codec", "packet_lz4",
                 canon_csv["csv"], bcsv)
        r = run_tool(tools["bcsv2csv"], bcsv, "-o", "-")
        lines = r.stdout.strip().splitlines()
        assert len(lines) == CANON_ROWS + 1  # rows + header

    def test_head_tail_coverage(self, tools, canon_csv, tmp_path):
        r_head = run_tool(tools["bcsvHead"], "-n", "5", "--no-header",
                          canon_csv["bcsv"])
        r_tail = run_tool(tools["bcsvTail"], "-n", "15", "--no-header",
                          canon_csv["bcsv"])
        head_lines = len(r_head.stdout.strip().splitlines())
        tail_lines = len(r_tail.stdout.strip().splitlines())
        assert head_lines + tail_lines == CANON_ROWS

    def test_header_column_count(self, tools, canon_csv, tmp_path):
        r = run_tool(tools["bcsvHeader"], canon_csv["bcsv"])
        out = tmp_path / "rt.csv"
        run_tool(tools["bcsv2csv"], canon_csv["bcsv"], "-o", out)
        header = out.read_text().splitlines()[0]
        rt_cols = len(header.split(","))
        assert str(rt_cols) in r.stdout


# ═══════════════════════════════════════════════════════════════════════
# Edge cases
# ═══════════════════════════════════════════════════════════════════════

class TestEdgeCases:
    def test_special_column_name_head(self, tools, tmp_path):
        csv_path = tmp_path / "special.csv"
        csv_path.write_text('"col,one","col""two",normal\n1,2,3\n4,5,6\n7,8,9\n')
        bcsv = tmp_path / "special.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--file-codec", "packet_lz4",
                 csv_path, bcsv)
        r = run_tool(tools["bcsvHead"], "-n", "1", bcsv)
        assert '"col,one"' in r.stdout

    def test_special_column_name_tail_sequential(self, tools, tmp_path):
        """B4 fix: sequential path quotes column name with comma."""
        csv_path = tmp_path / "special.csv"
        csv_path.write_text('"col,one","col""two",normal\n1,2,3\n4,5,6\n7,8,9\n')
        bcsv = tmp_path / "special_stream.bcsv"
        run_tool(tools["csv2bcsv"], "--file-codec", "stream", "-f", csv_path, bcsv)
        r = run_tool(tools["bcsvTail"], "-n", "1", bcsv)
        assert '"col,one"' in r.stdout

    def test_single_row_head(self, tools, tmp_path):
        csv_path = tmp_path / "single.csv"
        csv_path.write_text("x,y\n42,99\n")
        bcsv = tmp_path / "single.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--file-codec", "packet_lz4",
                 csv_path, bcsv)
        r = run_tool(tools["bcsvHead"], "-n", "10", bcsv)
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == 1

    def test_single_row_tail(self, tools, tmp_path):
        csv_path = tmp_path / "single.csv"
        csv_path.write_text("x,y\n42,99\n")
        bcsv = tmp_path / "single.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--file-codec", "packet_lz4",
                 csv_path, bcsv)
        r = run_tool(tools["bcsvTail"], "-n", "10", bcsv)
        lines = r.stdout.strip().splitlines()
        assert len(lines) - 1 == 1

    def test_empty_file_rejected(self, tools, tmp_path):
        csv_path = tmp_path / "empty.csv"
        csv_path.write_text("a,b,c\n")
        assert expect_fail(tools["csv2bcsv"], "-f", csv_path,
                           tmp_path / "empty.bcsv")

    def test_precision_roundtrip(self, tools, tmp_path):
        csv_path = tmp_path / "precision.csv"
        csv_path.write_text("val\n-42\n-3.14\n0.001\n12345678\n0.1\n0.3\n")
        bcsv = tmp_path / "precision.bcsv"
        rt = tmp_path / "precision_rt.csv"
        run_tool(tools["csv2bcsv"], "-f", "--file-codec", "packet_lz4",
                 csv_path, bcsv)
        run_tool(tools["bcsv2csv"], bcsv, "-o", rt)
        assert csv_equal(csv_path, rt)


# ═══════════════════════════════════════════════════════════════════════
# csv2bcsv: inference widening, header/name/type flags, row/col selection
# ═══════════════════════════════════════════════════════════════════════

class TestCsv2BcsvInference:
    def test_widening_after_sample(self, tools, tmp_dir):
        """Headline regression: a value past the inference sample that overflows
        the sampled type must widen the column, not silently write 0."""
        csv_path = tmp_dir / "widen.csv"
        lines = ["id,val"] + [f"{i},{i % 60000}" for i in range(1400)]
        lines.append("1400,70000")  # past the 1000-row sample; overflows uint16
        lines += [f"{i},{i % 100}" for i in range(1401, 1500)]
        csv_path.write_text("\n".join(lines) + "\n")

        bcsv_path = tmp_dir / "widen.bcsv"
        run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)

        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "uint32" in header  # widened from uint16

        out_csv = tmp_dir / "widen_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_cell(out_csv, 1401, 2) == "70000"

    def test_overflow_to_string_with_warning(self, tools, tmp_dir):
        """A number beyond uint64/double keeps the column STRING + warning."""
        csv_path = tmp_dir / "huge.csv"
        lines = ["id,big"] + [f"{i},{i}" for i in range(50)]
        lines.append("50,99999999999999999999")
        csv_path.write_text("\n".join(lines) + "\n")

        bcsv_path = tmp_dir / "huge.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--sample", "10", csv_path, bcsv_path)
        assert "Warning" in result.stderr and "STRING" in result.stderr

        out_csv = tmp_dir / "huge_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_cell(out_csv, 51, 2) == "99999999999999999999"  # preserved verbatim

    def test_late_string_no_warning(self, tools, tmp_dir):
        """A clearly non-numeric late cell widens to STRING silently."""
        csv_path = tmp_dir / "latestr.csv"
        lines = ["id,v"] + [f"{i},{i}" for i in range(50)]
        lines.append("50,hello")
        csv_path.write_text("\n".join(lines) + "\n")

        bcsv_path = tmp_dir / "latestr.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--sample", "10", csv_path, bcsv_path)
        assert "Warning" not in result.stderr

        out_csv = tmp_dir / "latestr_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_cell(out_csv, 51, 2) == "hello"

    def test_sample_zero_full_scan(self, tools, tmp_dir):
        """--sample 0 scans everything up front — single write pass, exact types."""
        csv_path = tmp_dir / "full.csv"
        lines = ["v"] + ["1"] * 100 + ["70000"]
        csv_path.write_text("\n".join(lines) + "\n")

        bcsv_path = tmp_dir / "full.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--sample", "0", "--benchmark", "--json",
                          csv_path, bcsv_path)
        assert '"passes":2' in result.stderr  # no rescue needed
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "uint32" in header

    def test_uint64_precision_preserved(self, tools, tmp_dir):
        """Integers in (int64max, uint64max] must infer UINT64, not DOUBLE."""
        csv_path = tmp_dir / "u64.csv"
        csv_path.write_text("v\n18446744073709551615\n1\n")
        bcsv_path = tmp_dir / "u64.bcsv"
        run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "uint64" in header
        out_csv = tmp_dir / "u64_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_cell(out_csv, 1, 1) == "18446744073709551615"


class TestCsv2BcsvHeaderFlags:
    CSV = "colA,colB\n1,2.5\n3,4.5\n"

    def test_skip_header_auto_names(self, tools, tmp_dir):
        csv_path = tmp_dir / "sh.csv"
        csv_path.write_text(self.CSV)
        bcsv_path = tmp_dir / "sh.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--skip-header", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "column_1" in header and "colA" not in header
        out_csv = tmp_dir / "sh_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_rows(out_csv) == 2  # header row consumed, not data

    def test_skip_header_with_names(self, tools, tmp_dir):
        csv_path = tmp_dir / "shn.csv"
        csv_path.write_text(self.CSV)
        bcsv_path = tmp_dir / "shn.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--skip-header", "--names", "x,y",
                 csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "x" in header and "y" in header

    def test_names_map_override(self, tools, tmp_dir):
        csv_path = tmp_dir / "nm.csv"
        csv_path.write_text(self.CSV)
        bcsv_path = tmp_dir / "nm.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--names", "colB=renamed", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "renamed" in header and "colA" in header

    def test_names_list_wrong_count_fails(self, tools, tmp_dir):
        csv_path = tmp_dir / "nc.csv"
        csv_path.write_text(self.CSV)
        result = run_tool(tools["csv2bcsv"], "-f", "--names", "onlyone",
                          csv_path, tmp_dir / "nc.bcsv", check=False)
        assert result.returncode == 2

    def test_duplicate_names_fail(self, tools, tmp_dir):
        csv_path = tmp_dir / "dup.csv"
        csv_path.write_text(self.CSV)
        result = run_tool(tools["csv2bcsv"], "-f", "--names", "same,same",
                          csv_path, tmp_dir / "dup.bcsv", check=False)
        assert result.returncode == 2

    def test_no_header_and_skip_header_conflict(self, tools, tmp_dir):
        csv_path = tmp_dir / "conf.csv"
        csv_path.write_text(self.CSV)
        result = run_tool(tools["csv2bcsv"], "-f", "--no-header", "--skip-header",
                          csv_path, tmp_dir / "conf.bcsv", check=False)
        assert result.returncode == 2


class TestCsv2BcsvForcedTypes:
    def test_types_map_by_name(self, tools, tmp_dir):
        csv_path = tmp_dir / "tm.csv"
        csv_path.write_text("a,b\n1,2\n3,4\n")
        bcsv_path = tmp_dir / "tm.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--types", "b=double", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "double" in header and "uint8" in header

    def test_types_list_with_auto(self, tools, tmp_dir):
        csv_path = tmp_dir / "tl.csv"
        csv_path.write_text("a,b\n1,2\n")
        bcsv_path = tmp_dir / "tl.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--types", "int32,auto", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "int32" in header and "uint8" in header

    def test_forced_misfit_clamps_with_warning(self, tools, tmp_dir):
        csv_path = tmp_dir / "cl.csv"
        csv_path.write_text("x\n300\n")
        bcsv_path = tmp_dir / "cl.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--types", "0=uint8", csv_path, bcsv_path)
        assert result.returncode == 0
        assert "clamped" in result.stderr
        out_csv = tmp_dir / "cl_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_cell(out_csv, 1, 1) == "255"  # saturated, bcsvCast --static semantics

    def test_strict_aborts_on_forced_misfit(self, tools, tmp_dir):
        csv_path = tmp_dir / "st.csv"
        csv_path.write_text("x\n300\n")
        result = run_tool(tools["csv2bcsv"], "-f", "--strict", "--types", "0=uint8",
                          csv_path, tmp_dir / "st.bcsv", check=False)
        assert result.returncode == 1
        assert not (tmp_dir / "st.bcsv").exists()  # temp cleaned up, no partial output

    def test_bad_spec_fails(self, tools, tmp_dir):
        csv_path = tmp_dir / "bs.csv"
        csv_path.write_text("a,b\n1,2\n")
        result = run_tool(tools["csv2bcsv"], "-f", "--types", "0=notatype",
                          csv_path, tmp_dir / "bs.bcsv", check=False)
        assert result.returncode == 2


class TestCsv2BcsvSelection:
    CSV = "a,b,c\n0,10,x0\n1,11,x1\n2,12,x2\n3,13,x3\n4,14,x4\n"

    def _write(self, tmp_dir):
        p = tmp_dir / "sel.csv"
        p.write_text(self.CSV)
        return p

    def test_rows_and_cols(self, tools, tmp_dir):
        csv_path = self._write(tmp_dir)
        bcsv_path = tmp_dir / "sel.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--rows", "1:3", "--cols", "a,c",
                 csv_path, bcsv_path)
        out_csv = tmp_dir / "sel_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_rows(out_csv) == 3
        assert csv_cell(out_csv, 1, 1) == "1"
        assert csv_cell(out_csv, 1, 2) == "x1"

    def test_cols_merged_ranges(self, tools, tmp_dir):
        csv_path = self._write(tmp_dir)
        bcsv_path = tmp_dir / "selm.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--cols", "0:1,1:2", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "a" in header and "b" in header and "c" in header

    def test_negative_rows_rejected(self, tools, tmp_dir):
        csv_path = self._write(tmp_dir)
        result = run_tool(tools["csv2bcsv"], "-f", "--rows", "-5:",
                          csv_path, tmp_dir / "neg.bcsv", check=False)
        assert result.returncode == 2

    def test_types_key_outside_cols_fails(self, tools, tmp_dir):
        csv_path = self._write(tmp_dir)
        result = run_tool(tools["csv2bcsv"], "-f", "--cols", "a", "--types", "b=int32",
                          csv_path, tmp_dir / "out.bcsv", check=False)
        assert result.returncode == 2

    def test_open_ended_rows(self, tools, tmp_dir):
        csv_path = self._write(tmp_dir)
        bcsv_path = tmp_dir / "open.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--rows", "3:", csv_path, bcsv_path)
        out_csv = tmp_dir / "open_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out_csv)
        assert csv_rows(out_csv) == 2


class TestBcsv2CsvSelection:
    @pytest.fixture()
    def sel_bcsv(self, tools, tmp_dir):
        csv_path = tmp_dir / "sel.csv"
        csv_path.write_text("a,b,c\n0,10,x0\n1,11,x1\n2,12,x2\n3,13,x3\n4,14,x4\n")
        bcsv_path = tmp_dir / "sel.bcsv"
        run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)
        return bcsv_path

    def test_rows_ranges(self, tools, tmp_dir, sel_bcsv):
        out = tmp_dir / "rows.csv"
        run_tool(tools["bcsv2csv"], "--rows", "1:2,4", sel_bcsv, out)
        assert csv_rows(out) == 3
        assert csv_cell(out, 1, 1) == "1"
        assert csv_cell(out, 3, 1) == "4"

    def test_rows_negative(self, tools, tmp_dir, sel_bcsv):
        out = tmp_dir / "neg.csv"
        run_tool(tools["bcsv2csv"], "--rows", "-2:", sel_bcsv, out)
        assert csv_rows(out) == 2
        assert csv_cell(out, 1, 1) == "3"

    def test_cols_by_name_and_index(self, tools, tmp_dir, sel_bcsv):
        out = tmp_dir / "cols.csv"
        run_tool(tools["bcsv2csv"], "--cols", "a,2", sel_bcsv, out)
        first_line = out.read_text().splitlines()[0]
        assert "a" in first_line and "c" in first_line and "b" not in first_line

    def test_rows_conflicts_with_legacy(self, tools, tmp_dir, sel_bcsv):
        result = run_tool(tools["bcsv2csv"], "--rows", "0:1", "--slice", "::2",
                          sel_bcsv, tmp_dir / "x.csv", check=False)
        assert result.returncode == 2
        result = run_tool(tools["bcsv2csv"], "--rows", "0:1", "--firstRow", "0",
                          sel_bcsv, tmp_dir / "y.csv", check=False)
        assert result.returncode == 2

    def test_unknown_col_name_fails(self, tools, tmp_dir, sel_bcsv):
        result = run_tool(tools["bcsv2csv"], "--cols", "nope",
                          sel_bcsv, tmp_dir / "z.csv", check=False)
        assert result.returncode == 2

    def test_legacy_slice_unchanged(self, tools, tmp_dir, sel_bcsv):
        out = tmp_dir / "step.csv"
        run_tool(tools["bcsv2csv"], "--slice", "::2", sel_bcsv, out)
        assert csv_rows(out) == 3  # rows 0, 2, 4


class TestCsv2BcsvPartialRows:
    """Field-count mismatches abort by default (likely corrupt CSV);
    --skip-partial-rows / --pad-partial-rows opt into tolerant behavior."""

    SHORT = "a,b,c\n1,2,3\n4,5\n6,7,8\n"
    LONG = "a,b\n1,2\n3,4,5\n"

    def test_short_row_aborts_by_default(self, tools, tmp_dir):
        csv_path = tmp_dir / "short.csv"
        csv_path.write_text(self.SHORT)
        result = run_tool(tools["csv2bcsv"], "-f", csv_path, tmp_dir / "s.bcsv", check=False)
        assert result.returncode == 1
        assert "partial" in result.stderr and "row 1" in result.stderr
        assert not (tmp_dir / "s.bcsv").exists()

    def test_long_row_aborts_by_default(self, tools, tmp_dir):
        csv_path = tmp_dir / "long.csv"
        csv_path.write_text(self.LONG)
        result = run_tool(tools["csv2bcsv"], "-f", csv_path, tmp_dir / "l.bcsv", check=False)
        assert result.returncode == 1

    def test_skip_partial_rows(self, tools, tmp_dir):
        csv_path = tmp_dir / "skip.csv"
        csv_path.write_text(self.SHORT)
        bcsv_path = tmp_dir / "skip.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--skip-partial-rows", csv_path, bcsv_path)
        assert "skipped 1 partial row" in result.stderr  # reported even without -v
        out = tmp_dir / "skip_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out)
        assert csv_rows(out) == 2

    def test_pad_partial_rows(self, tools, tmp_dir):
        csv_path = tmp_dir / "pad.csv"
        csv_path.write_text(self.SHORT)
        bcsv_path = tmp_dir / "pad.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--pad-partial-rows", csv_path, bcsv_path)
        assert "padded 1 short row" in result.stderr
        out = tmp_dir / "pad_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out)
        assert csv_rows(out) == 3
        assert csv_cell(out, 2, 3) == "0"  # padded cell stored as default

    def test_pad_does_not_cover_long_rows(self, tools, tmp_dir):
        csv_path = tmp_dir / "padlong.csv"
        csv_path.write_text(self.LONG)
        result = run_tool(tools["csv2bcsv"], "-f", "--pad-partial-rows",
                          csv_path, tmp_dir / "pl.bcsv", check=False)
        assert result.returncode == 1

    def test_pad_and_skip_combine(self, tools, tmp_dir):
        csv_path = tmp_dir / "mixed.csv"
        csv_path.write_text("a,b\n1,2\n3\n4,5,6\n7,8\n")
        bcsv_path = tmp_dir / "mixed.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--pad-partial-rows",
                          "--skip-partial-rows", csv_path, bcsv_path)
        assert "padded 1" in result.stderr and "skipped 1" in result.stderr
        out = tmp_dir / "mixed_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out)
        assert csv_rows(out) == 3  # 1,2 / 3,<pad> / 7,8 — 4,5,6 skipped

    def test_trailing_delimiter_still_tolerated(self, tools, tmp_dir):
        csv_path = tmp_dir / "trail.csv"
        csv_path.write_text("a,b\n1,2,\n3,4\n")
        bcsv_path = tmp_dir / "trail.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", csv_path, bcsv_path)
        assert "Warning" not in result.stderr
        out = tmp_dir / "trail_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out)
        assert csv_rows(out) == 2

    def test_headerless_trailing_delimiter_column_count(self, tools, tmp_dir):
        """Regression: --no-header with trailing delimiters must not grow a
        phantom all-empty trailing column."""
        csv_path = tmp_dir / "hless.csv"
        csv_path.write_text("1,2,3,\n4,5,6,\n")
        bcsv_path = tmp_dir / "hless.bcsv"
        run_tool(tools["csv2bcsv"], "-f", "--no-header", csv_path, bcsv_path)
        header = run_tool(tools["bcsvHeader"], bcsv_path).stdout
        assert "Columns: 3" in header


class TestReviewRegressions:
    """Regressions from the 2026-07-13 critical review."""

    def test_textual_bool_column_widens_to_string(self, tools, tmp_dir):
        """A bool-looking column with a later non-bool value must widen to
        STRING, not abort the conversion (settled() regression)."""
        csv_path = tmp_dir / "tbool.csv"
        lines = ["flag"] + ["true", "false"] * 30 + ["N/A"]
        csv_path.write_text("\n".join(lines) + "\n")
        bcsv_path = tmp_dir / "tbool.bcsv"
        result = run_tool(tools["csv2bcsv"], "-f", "--sample", "10", csv_path, bcsv_path)
        assert result.returncode == 0
        out = tmp_dir / "tbool_out.csv"
        run_tool(tools["bcsv2csv"], bcsv_path, out)
        assert csv_rows(out) == 61
        assert csv_cell(out, 61, 1) == "N/A"

    def test_malformed_spec_key_rejected(self, tools, tmp_dir):
        """'1+2' must not silently resolve to column 1 (strict index parse)."""
        csv_path = tmp_dir / "key.csv"
        csv_path.write_text("a,b,c\n1,2,3\n")
        for bad in ("1+2=double", "5-=double"):
            result = run_tool(tools["csv2bcsv"], "-f", "--types", bad,
                              csv_path, tmp_dir / "key.bcsv", check=False)
            assert result.returncode == 2, bad
        result = run_tool(tools["csv2bcsv"], "-f", csv_path, tmp_dir / "key.bcsv")
        result = run_tool(tools["bcsv2csv"], "--cols", "1+2",
                          tmp_dir / "key.bcsv", tmp_dir / "key_out.csv", check=False)
        assert result.returncode == 2

    def test_rows_dash_typo_message_mentions_colon(self, tools, tmp_dir):
        csv_path = tmp_dir / "typo.csv"
        csv_path.write_text("a\n1\n")
        result = run_tool(tools["csv2bcsv"], "-f", "--rows", "5-10",
                          csv_path, tmp_dir / "typo.bcsv", check=False)
        assert result.returncode == 2
        assert "5:10" in result.stderr  # points at the ':' syntax
