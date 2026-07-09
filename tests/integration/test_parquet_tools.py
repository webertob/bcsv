"""Integration tests for parquet2bcsv and bcsv2parquet."""

import json
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq
import pytest

import pybcsv
from pybcsv.parquet_utils import (
    bcsv_to_parquet,
    flatten_parquet_schema,
    parquet_to_bcsv,
)


def _flat_table(num_rows=100):
    return pa.table(
        {
            "id": pa.array(list(range(num_rows)), type=pa.int64()),
            "val": pa.array([i * 1.5 for i in range(num_rows)], type=pa.float64()),
            "name": pa.array([f"r{i}" for i in range(num_rows)], type=pa.string()),
        }
    )


def _struct_table(num_rows=10):
    lats = pa.array([10.0 + i for i in range(num_rows)], type=pa.float32())
    lons = pa.array([20.0 + i for i in range(num_rows)], type=pa.float32())
    structs = pa.StructArray.from_arrays([lats, lons], names=["lat", "lon"])
    return pa.table(
        {
            "id": pa.array(list(range(num_rows)), type=pa.int64()),
            "location": structs,
        }
    )


def _list_table(num_rows=10):
    values = [float(i) for i in range(num_rows * 3)]
    arr = pa.FixedSizeListArray.from_arrays(pa.array(values, type=pa.float64()), 3)
    return pa.table(
        {
            "id": pa.array(list(range(num_rows)), type=pa.int64()),
            "vals": arr,
        }
    )


# ---- Basic round-trips ----


class TestBasic:
    def test_roundtrip_flat(self, tmp_path):
        table = _flat_table(100)
        pq_path = tmp_path / "in.parquet"
        bcsv_path = tmp_path / "out.bcsv"
        rt_path = tmp_path / "rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 100

        rt = pq.read_table(str(rt_path))
        assert rt.num_rows == 100
        assert rt.num_columns == table.num_columns

    def test_roundtrip_large_string(self, tmp_path):
        """large_string Parquet columns must convert without error (regression)."""
        table = pa.table(
            {
                "id": pa.array([1, 2, 3], type=pa.int64()),
                "label": pa.array(["a", "bb", "ccc"], type=pa.large_utf8()),
            }
        )
        pq_path = tmp_path / "ls.parquet"
        bcsv_path = tmp_path / "ls.bcsv"
        pq.write_table(table, str(pq_path))
        result = parquet_to_bcsv(str(pq_path), str(bcsv_path))
        assert result["rows"] == 3


class TestNullRejection:
    def test_null_rejected(self, tmp_path):
        table = pa.table(
            {
                "id": pa.array([1, None, 3], type=pa.int64()),
            }
        )
        pq_path = tmp_path / "null.parquet"
        bcsv_path = tmp_path / "null.bcsv"
        pq.write_table(table, str(pq_path))
        with pytest.raises(ValueError):
            parquet_to_bcsv(str(pq_path), str(bcsv_path))

    def test_null_first_row(self, tmp_path):
        table = pa.table(
            {
                "id": pa.array([None, 2, 3], type=pa.int64()),
            }
        )
        pq_path = tmp_path / "null1.parquet"
        bcsv_path = tmp_path / "null1.bcsv"
        pq.write_table(table, str(pq_path))
        with pytest.raises(ValueError, match="row 0"):
            parquet_to_bcsv(str(pq_path), str(bcsv_path))

    def test_unsupported_timestamp(self, tmp_path):
        table = pa.table(
            {
                "ts": pa.array([1, 2, 3], type=pa.timestamp("us")),
            }
        )
        pq_path = tmp_path / "ts.parquet"
        bcsv_path = tmp_path / "ts.bcsv"
        pq.write_table(table, str(pq_path))
        with pytest.raises(ValueError, match="Unsupported"):
            parquet_to_bcsv(str(pq_path), str(bcsv_path))


class TestSlicing:
    def test_row_slice_forward(self, tmp_path):
        table = _flat_table(10)
        pq_path = tmp_path / "s.parquet"
        bcsv_path = tmp_path / "s.bcsv"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(tmp_path / "s_rt.parquet"),
            row_slice="3:7",
            force=True,
        )
        assert result["rows"] == 4

    def test_row_slice_from_start(self, tmp_path):
        table = _flat_table(10)
        pq_path = tmp_path / "s2.parquet"
        bcsv_path = tmp_path / "s2.bcsv"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(tmp_path / "s2_rt.parquet"),
            row_slice=":5",
            force=True,
        )
        assert result["rows"] == 5


class TestCodecs:
    def test_codec_packet_lz4_batch(self, tmp_path):
        pq_path = tmp_path / "c.parquet"
        bcsv_path = tmp_path / "c.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        result = parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            file_codec="packet_lz4_batch",
        )
        assert result["rows"] == 5

    def test_codec_packet_lz4(self, tmp_path):
        pq_path = tmp_path / "c2.parquet"
        bcsv_path = tmp_path / "c2.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        result = parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            file_codec="packet_lz4",
        )
        assert result["rows"] == 5

    def test_codec_packet(self, tmp_path):
        pq_path = tmp_path / "c3.parquet"
        bcsv_path = tmp_path / "c3.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        result = parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            file_codec="packet",
        )
        assert result["rows"] == 5

    def test_row_codecs(self, tmp_path):
        pq_path = tmp_path / "c4.parquet"
        for codec in ("delta", "zoh", "flat"):
            bcsv_path = tmp_path / f"r_{codec}.bcsv"
            pq.write_table(_flat_table(5), str(pq_path))
            result = parquet_to_bcsv(
                str(pq_path),
                str(bcsv_path),
                row_codec=codec,
            )
            assert result["rows"] == 5

    def test_parquet_compression_snappy(self, tmp_path):
        pq_path = tmp_path / "c5.parquet"
        bcsv_path = tmp_path / "c5.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(tmp_path / "c5_rt.parquet"),
            parquet_compression="snappy",
            force=True,
        )
        assert result["rows"] == 5

    def test_parquet_compression_none(self, tmp_path):
        pq_path = tmp_path / "c6.parquet"
        bcsv_path = tmp_path / "c6.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(tmp_path / "c6_rt.parquet"),
            parquet_compression="none",
            force=True,
        )
        assert result["rows"] == 5


class TestFlags:
    def test_no_unflatten(self, tmp_path):
        pq_path = tmp_path / "f.parquet"
        bcsv_path = tmp_path / "f.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(tmp_path / "f_rt.parquet"),
            unflatten=False,
            force=True,
        )
        assert result["rows"] == 5

    def test_force_overwrite(self, tmp_path):
        pq_path = tmp_path / "f2.parquet"
        bcsv_path = tmp_path / "f2.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(tmp_path / "f2_rt.parquet"),
            force=True,
        )
        assert result["rows"] == 5

    def test_no_force_fails(self, tmp_path):
        pq_path = tmp_path / "f3.parquet"
        bcsv_path = tmp_path / "f3.bcsv"
        rt_path = tmp_path / "f3_rt.parquet"
        pq.write_table(_flat_table(5), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        rt_path.touch()  # pre-create output file
        with pytest.raises(FileExistsError, match="already exists"):
            bcsv_to_parquet(
                str(bcsv_path),
                str(rt_path),
                force=False,
            )


class TestBenchmark:
    def test_benchmark(self, tmp_path):
        pq_path = tmp_path / "b.parquet"
        bcsv_path = tmp_path / "b.bcsv"
        pq.write_table(_flat_table(10), str(pq_path))
        result = parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            benchmark=True,
        )
        assert "rows" in result
        assert "elapsed_s" in result

    def test_benchmark_json(self, tmp_path):
        pq_path = tmp_path / "b2.parquet"
        bcsv_path = tmp_path / "b2.bcsv"
        pq.write_table(_flat_table(10), str(pq_path))
        result = parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            benchmark=True,
            json_output=True,
        )
        data = json.dumps(result)
        parsed = json.loads(data)
        assert "rows" in parsed

    def test_verbose(self, tmp_path, capsys):
        pq_path = tmp_path / "v.parquet"
        bcsv_path = tmp_path / "v.bcsv"
        pq.write_table(_flat_table(10), str(pq_path))
        parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            verbose=True,
        )
        captured = capsys.readouterr()
        assert "Converting" in captured.err


# ---- Regression tests for critical bugs ----


class TestStructRoundtrip:
    """Regression: struct fields must survive Parquet->BCSV->Parquet."""

    def test_struct_roundtrip(self, tmp_path):
        lats = pa.array([10.0, 20.0, 30.0], type=pa.float32())
        lons = pa.array([40.0, 50.0, 60.0], type=pa.float32())
        structs = pa.StructArray.from_arrays([lats, lons], names=["lat", "lon"])
        table = pa.table(
            {"id": pa.array([1, 2, 3], type=pa.int64()), "location": structs}
        )
        pq_path = tmp_path / "loc.parquet"
        bcsv_path = tmp_path / "loc.bcsv"
        rt_path = tmp_path / "loc_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
        )
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 3

        rt = pq.read_table(str(rt_path))
        assert rt.num_rows == 3
        assert rt.num_columns == 2
        assert rt.column("id").to_pylist() == [1, 2, 3]
        orig_lats = table.column("location").combine_chunks().field("lat").to_pylist()
        rt_lats = rt.column("location").combine_chunks().field("lat").to_pylist()
        assert rt_lats == orig_lats
        orig_lons = table.column("location").combine_chunks().field("lon").to_pylist()
        rt_lons = rt.column("location").combine_chunks().field("lon").to_pylist()
        assert rt_lons == orig_lons


class TestFixedSizeListRoundtrip:
    """FixedSizeList element `i` must map to element-i-of-each-row.

    Regression for a transposition bug: element extraction used a contiguous
    slice (child[i*N:(i+1)*N]) instead of a strided gather, which scrambled the
    flat BCSV columns.  A `.values`-only assertion is invariant under that
    (symmetric) scramble and does NOT catch it — assert per-element and per-row.
    """

    def test_list_element_semantics(self, tmp_path):
        rows = [[0, 1, 2], [10, 11, 12], [20, 21, 22], [30, 31, 32]]
        child = pa.array([v for r in rows for v in r], type=pa.int64())
        arr = pa.FixedSizeListArray.from_arrays(child, 3)
        table = pa.table({"vals": arr})
        pq_path = tmp_path / "sem.parquet"
        bcsv_path = tmp_path / "sem.bcsv"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)
        t = pybcsv.read_to_arrow(str(bcsv_path))
        # column vals[i] must hold element i of every row, not a contiguous block
        assert t.column("vals[0]").to_pylist() == [r[0] for r in rows]
        assert t.column("vals[1]").to_pylist() == [r[1] for r in rows]
        assert t.column("vals[2]").to_pylist() == [r[2] for r in rows]

    def test_list_roundtrip(self, tmp_path):
        rows = [[0.0, 1.0, 2.0], [3.0, 4.0, 5.0], [6.0, 7.0, 8.0]]
        child = pa.array([v for r in rows for v in r], type=pa.float64())
        arr = pa.FixedSizeListArray.from_arrays(child, 3)
        table = pa.table({"id": pa.array([1, 2, 3], type=pa.int64()), "vals": arr})
        pq_path = tmp_path / "list.parquet"
        bcsv_path = tmp_path / "list.bcsv"
        rt_path = tmp_path / "list_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 3
        rt = pq.read_table(str(rt_path))
        # Row-level fidelity — not just .values, which survives a transposition.
        assert rt.column("vals").to_pylist() == rows

    def test_list_asymmetric_batch_roundtrip(self, tmp_path):
        """Streaming with different write/read batch sizes — the realistic case
        a transposition bug corrupts even when single-batch round-trips look ok."""
        rows = [[i * 10, i * 10 + 1, i * 10 + 2] for i in range(7)]
        child = pa.array([v for r in rows for v in r], type=pa.int64())
        arr = pa.FixedSizeListArray.from_arrays(child, 3)
        table = pa.table({"vals": arr})
        pq_path = tmp_path / "asym.parquet"
        bcsv_path = tmp_path / "asym.bcsv"
        rt_path = tmp_path / "asym_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True, chunk_size=3)
        bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True, chunk_size=2)
        rt = pq.read_table(str(rt_path))
        assert rt.column("vals").to_pylist() == rows


class TestColumnSubsetUnflatten:
    """Regression: --columns subset must not crash or drop data during unflatten."""

    def test_subset_flat_columns(self, tmp_path):
        table = pa.table(
            {
                "id": pa.array([1, 2, 3], type=pa.int64()),
                "val": pa.array([10.0, 20.0, 30.0], type=pa.float64()),
            }
        )
        pq_path = tmp_path / "s.parquet"
        bcsv_path = tmp_path / "s.bcsv"
        rq_path = tmp_path / "s.sub.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(
            str(bcsv_path), str(rq_path), columns=["id"], force=True
        )
        assert result["rows"] == 3
        rt = pq.read_table(str(rq_path))
        assert rt.num_columns == 1
        assert rt.column("id").to_pylist() == [1, 2, 3]

    def test_subset_struct_columns(self, tmp_path):
        lats = pa.array([10.0, 20.0, 30.0], type=pa.float32())
        lons = pa.array([40.0, 50.0, 60.0], type=pa.float32())
        structs = pa.StructArray.from_arrays([lats, lons], names=["lat", "lon"])
        table = pa.table(
            {"id": pa.array([1, 2, 3], type=pa.int64()), "location": structs}
        )
        pq_path = tmp_path / "s2.parquet"
        bcsv_path = tmp_path / "s2.bcsv"
        rq_path = tmp_path / "s2.sub.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
        )
        result = bcsv_to_parquet(
            str(bcsv_path),
            str(rq_path),
            columns=["id", "location.lat"],
            force=True,
        )
        assert result["rows"] == 3
        rt = pq.read_table(str(rq_path))
        assert "id" in [rt.schema.field(i).name for i in range(len(rt.schema))]
        assert "location" in [rt.schema.field(i).name for i in range(len(rt.schema))]


class TestFP16Widening:
    """Regression: float16 columns must be widened to float32 without data loss."""

    def test_fp16_roundtrip_values(self, tmp_path):
        original = [1.5, 2.5, 3.75]
        arr = pa.array(original, type=pa.float16())
        table = pa.table({"x": arr})
        pq_path = tmp_path / "fp16.parquet"
        bcsv_path = tmp_path / "fp16.bcsv"
        rq_path = tmp_path / "fp16_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        result = bcsv_to_parquet(str(bcsv_path), str(rq_path), force=True)
        assert result["rows"] == 3
        rt = pq.read_table(str(rq_path))
        rt_vals = rt.column("x").to_pylist()
        assert rt_vals == original


class TestCLIVersion:
    """Regression: --version flag must be available on both CLIs."""

    def test_parquet2bcsv_version(self, tmp_path):
        import subprocess, sys

        r = subprocess.run(
            [
                sys.executable,
                "-c",
                "import sys; sys.argv=['parquet2bcsv','--version']; "
                "from pybcsv.parquet_utils import parquet2bcsv_cli; parquet2bcsv_cli()",
            ],
            capture_output=True,
            text=True,
        )
        assert r.returncode == 0
        assert pybcsv.__version__ in r.stdout

    def test_bcvs2parquet_version(self, tmp_path):
        import subprocess, sys

        r = subprocess.run(
            [
                sys.executable,
                "-c",
                "import sys; sys.argv=['bcsv2parquet','--version']; "
                "from pybcsv.parquet_utils import bcsv2parquet_cli; bcsv2parquet_cli()",
            ],
            capture_output=True,
            text=True,
        )
        assert r.returncode == 0
        assert pybcsv.__version__ in r.stdout


class TestBenchmarkOutputConsistency:
    """Regression: text benchmark → stderr, JSON benchmark → stdout."""

    def test_benchmark_json_to_stdout(self, tmp_path, capsys):
        table = _flat_table(10)
        pq_path = tmp_path / "b3.parquet"
        bcsv_path = tmp_path / "b3.bcsv"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            benchmark=True,
            json_output=True,
        )
        captured = capsys.readouterr()
        assert captured.out.strip()  # JSON benchmark goes to stdout
        data = json.loads(captured.out.strip())
        assert data["rows"] == 10


class TestUnderscoreRejection:
    """Regression: column names ending in '_' are rejected to prevent
    silent corruption during escape-suffix stripping on roundtrip."""

    def test_trailing_underscore_rejected_parquet(self, tmp_path):
        # Column name ending in _ in the original Parquet schema
        table = pa.table({"data_": pa.array([1.0], type=pa.float64())})
        pq.write_table(table, str(tmp_path / "u2.parquet"))
        with pytest.raises(ValueError, match="ends with '_'"):
            parquet_to_bcsv(
                str(tmp_path / "u2.parquet"),
                str(tmp_path / "u2.bcsv"),
            )


class TestFP16FixedSizeListRoundtrip:
    """Regression: FixedSizeList<halffloat, N> widens to float32."""

    def test_fp16_list_roundtrip(self, tmp_path):
        values = [float(i) for i in range(9)]
        arr = pa.FixedSizeListArray.from_arrays(pa.array(values, type=pa.float16()), 3)
        table = pa.table({"id": pa.array([1, 2, 3], type=pa.int64()), "vals": arr})
        pq_path = tmp_path / "fp16_list.parquet"
        bcsv_path = tmp_path / "fp16_list.bcsv"
        rt_path = tmp_path / "fp16_list_rt.parquet"
        pq.write_table(table, str(pq_path))

        parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
        )
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 3

        rt = pq.read_table(str(rt_path))
        # Row-level fidelity (a .values-only check misses element transposition).
        assert rt.column("vals").to_pylist() == [
            [float(v) for v in r]
            for r in [values[i : i + 3] for i in range(0, 9, 3)]
        ]


class TestRowGroupSize:
    """Regression: --row-group-size must produce multiple row groups."""

    def test_row_group_size_produces_groups(self, tmp_path):
        table = _flat_table(10)
        pq_path = tmp_path / "rg.parquet"
        bcsv_path = tmp_path / "rg.bcsv"
        rt_path = tmp_path / "rg_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        bcsv_to_parquet(str(bcsv_path), str(rt_path), row_group_size=3, force=True)
        rt_meta = pq.read_metadata(str(rt_path))
        assert rt_meta.num_row_groups == 4  # 10 rows / 3 = 4 groups (3,3,3,1)


class TestNestedUnderscore:
    """Regression: nested struct field names ending in '_' must be rejected."""

    def test_nested_underscore_rejected_parquet(self, tmp_path):
        lats = pa.array([10.0, 20.0], type=pa.float32())
        structs = pa.StructArray.from_arrays([lats], names=["lat"])
        table = pa.table({"loc_": structs})
        pq_path = tmp_path / "nu.parquet"
        bcsv_path = tmp_path / "nu.bcsv"
        pq.write_table(table, str(pq_path))
        with pytest.raises(ValueError, match="ends with '_'"):
            parquet_to_bcsv(str(pq_path), str(bcsv_path))


class TestNameCollisionData:
    """Struct path colliding with a literal dotted column.

    parquet2bcsv must not crash and must keep both columns distinct (forward);
    the unflatten direction must fail loudly rather than silently merge them.
    """

    def _colliding_table(self):
        struct = pa.StructArray.from_arrays(
            [pa.array([1, 2, 3], type=pa.int32())], names=["b"]
        )
        schema = pa.schema(
            [pa.field("a", struct.type), pa.field("a.b", pa.float64())]
        )
        return pa.Table.from_arrays(
            [struct, pa.array([1.5, 2.5, 3.5], type=pa.float64())], schema=schema
        )

    def test_collision_forward_distinct_columns(self, tmp_path):
        pq_path = tmp_path / "c.parquet"
        bcsv_path = tmp_path / "c.bcsv"
        pq.write_table(self._colliding_table(), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)  # must not raise
        t = pybcsv.read_to_arrow(str(bcsv_path))
        assert t.column("a_.b").to_pylist() == [1, 2, 3]  # struct a.b (escaped)
        assert t.column("a.b").to_pylist() == [1.5, 2.5, 3.5]  # literal flat column

    def test_collision_unflatten_raises(self, tmp_path):
        pq_path = tmp_path / "c2.parquet"
        bcsv_path = tmp_path / "c2.bcsv"
        rt_path = tmp_path / "c2_rt.parquet"
        pq.write_table(self._colliding_table(), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)
        with pytest.raises(ValueError, match="map to nested path"):
            bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)

    def test_collision_no_unflatten_ok(self, tmp_path):
        pq_path = tmp_path / "c3.parquet"
        bcsv_path = tmp_path / "c3.bcsv"
        rt_path = tmp_path / "c3_rt.parquet"
        pq.write_table(self._colliding_table(), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)
        bcsv_to_parquet(str(bcsv_path), str(rt_path), unflatten=False, force=True)
        rt = pq.read_table(str(rt_path))
        names = {rt.schema.field(i).name for i in range(len(rt.schema))}
        assert names == {"a_.b", "a.b"}


class TestColumnOrder:
    """--columns must honor the requested order consistently across the
    unflatten and no-unflatten paths."""

    def _make_bcsv(self, tmp_path):
        table = pa.table(
            {
                "a": pa.array([1, 2, 3], type=pa.int64()),
                "b": pa.array([4, 5, 6], type=pa.int64()),
                "c": pa.array([7, 8, 9], type=pa.int64()),
            }
        )
        pq_path = tmp_path / "o.parquet"
        bcsv_path = tmp_path / "o.bcsv"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)
        return bcsv_path

    def test_reorder_unflatten_honored(self, tmp_path):
        bcsv_path = self._make_bcsv(tmp_path)
        out = tmp_path / "u.parquet"
        bcsv_to_parquet(str(bcsv_path), str(out), columns=["c", "a"], force=True)
        rt = pq.read_table(str(out))
        assert [rt.schema.field(i).name for i in range(len(rt.schema))] == ["c", "a"]
        assert rt.column("c").to_pylist() == [7, 8, 9]
        assert rt.column("a").to_pylist() == [1, 2, 3]

    def test_reorder_matches_across_paths(self, tmp_path):
        bcsv_path = self._make_bcsv(tmp_path)
        u = tmp_path / "u2.parquet"
        f = tmp_path / "f2.parquet"
        bcsv_to_parquet(str(bcsv_path), str(u), columns=["c", "a"], force=True)
        bcsv_to_parquet(
            str(bcsv_path), str(f), columns=["c", "a"], unflatten=False, force=True
        )
        un = [pq.read_table(str(u)).schema.field(i).name for i in range(2)]
        fn = [pq.read_table(str(f)).schema.field(i).name for i in range(2)]
        assert un == fn == ["c", "a"]


class TestFlatRoundtripFidelity:
    """Flat round-trip must preserve names, types, AND values (incl. strings) —
    not just row/column counts."""

    def test_values_types_names(self, tmp_path):
        table = pa.table(
            {
                "id": pa.array([1, 2, 3], type=pa.int64()),
                "val": pa.array([1.5, 2.5, 3.5], type=pa.float64()),
                "name": pa.array(["a", "bb", "ccc"], type=pa.string()),
            }
        )
        pq_path = tmp_path / "f.parquet"
        bcsv_path = tmp_path / "f.bcsv"
        rt_path = tmp_path / "f_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)
        bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        rt = pq.read_table(str(rt_path))
        assert [rt.schema.field(i).name for i in range(len(rt.schema))] == [
            "id",
            "val",
            "name",
        ]
        assert rt.schema.field("id").type == pa.int64()
        assert rt.schema.field("val").type == pa.float64()
        assert rt.schema.field("name").type == pa.string()
        assert rt.column("id").to_pylist() == [1, 2, 3]
        assert rt.column("val").to_pylist() == [1.5, 2.5, 3.5]
        assert rt.column("name").to_pylist() == ["a", "bb", "ccc"]


class TestEmptyFile:
    """0-row files must round-trip with schema preserved."""

    def test_empty_roundtrip(self, tmp_path):
        table = pa.table(
            {
                "id": pa.array([], type=pa.int64()),
                "val": pa.array([], type=pa.float64()),
            }
        )
        pq_path = tmp_path / "e.parquet"
        bcsv_path = tmp_path / "e.bcsv"
        rt_path = tmp_path / "e_rt.parquet"
        pq.write_table(table, str(pq_path))
        assert parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)["rows"] == 0
        assert bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)["rows"] == 0
        rt = pq.read_table(str(rt_path))
        assert rt.num_rows == 0
        assert [rt.schema.field(i).name for i in range(len(rt.schema))] == ["id", "val"]


class TestOverwriteGuard:
    """parquet_to_bcsv must refuse to overwrite an existing output without force."""

    def test_parquet_to_bcsv_no_overwrite(self, tmp_path):
        pq_path = tmp_path / "g.parquet"
        bcsv_path = tmp_path / "g.bcsv"
        pq.write_table(_flat_table(3), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path))
        with pytest.raises(FileExistsError):
            parquet_to_bcsv(str(pq_path), str(bcsv_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), force=True)  # force overrides


class TestBcsvFirstRoundtrip:
    """A BCSV file authored directly with flat dotted names must round-trip out
    to Parquet and back (also exercises the literal-dotted-name fast path)."""

    def test_bcsv_parquet_bcsv(self, tmp_path):
        layout = pybcsv.Layout()
        layout.add_column("sim.ve.counter", pybcsv.ColumnType.INT64)
        layout.add_column("sim.ve.value", pybcsv.ColumnType.DOUBLE)
        bcsv_in = tmp_path / "src.bcsv"
        w = pybcsv.Writer(layout)
        w.open(str(bcsv_in), overwrite=True)
        for i in range(5):
            w.write_row([i, i * 1.5])
        w.close()
        pq_path = tmp_path / "mid.parquet"
        bcsv_out = tmp_path / "back.bcsv"
        # Keep flat for a faithful round-trip of literal dotted column names.
        bcsv_to_parquet(str(bcsv_in), str(pq_path), unflatten=False, force=True)
        parquet_to_bcsv(str(pq_path), str(bcsv_out), force=True)
        t = pybcsv.read_to_arrow(str(bcsv_out))
        assert t.column("sim.ve.counter").to_pylist() == [0, 1, 2, 3, 4]
        assert t.column("sim.ve.value").to_pylist() == [i * 1.5 for i in range(5)]


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
