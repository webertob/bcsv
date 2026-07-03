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


def _flat_layout():
    return "id:int64,val:double,name:string"


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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 100

        rt = pq.read_table(str(rt_path))
        assert rt.num_rows == 100
        assert rt.num_columns == table.num_columns


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
            parquet_to_bcsv(str(pq_path), str(bcsv_path), "id:int64")

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
            parquet_to_bcsv(str(pq_path), str(bcsv_path), "id:int64")

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
            parquet_to_bcsv(str(pq_path), str(bcsv_path), "ts:int64")


class TestLayoutValidation:
    def test_missing_layout(self, tmp_path):
        pq_path = tmp_path / "m.parquet"
        bcsv_path = tmp_path / "m.bcsv"
        pq.write_table(pa.table({"id": pa.array([1])}), str(pq_path))
        with pytest.raises(ValueError, match="cannot be empty"):
            parquet_to_bcsv(str(pq_path), str(bcsv_path), "")

    def test_type_mismatch(self, tmp_path):
        pq_path = tmp_path / "m2.parquet"
        bcsv_path = tmp_path / "m2.bcsv"
        pq.write_table(pa.table({"id": pa.array([1], type=pa.int64())}), str(pq_path))
        with pytest.raises(ValueError, match="mismatch"):
            parquet_to_bcsv(str(pq_path), str(bcsv_path), "id:string")

    def test_missing_columns(self, tmp_path):
        pq_path = tmp_path / "m3.parquet"
        bcsv_path = tmp_path / "m3.bcsv"
        table = pa.table(
            {
                "id": pa.array([1], type=pa.int64()),
                "val": pa.array([1.0], type=pa.float64()),
            }
        )
        pq.write_table(table, str(pq_path))
        with pytest.raises(ValueError, match="Missing"):
            parquet_to_bcsv(str(pq_path), str(bcsv_path), "id:int64")


class TestSlicing:
    def test_row_slice_forward(self, tmp_path):
        table = _flat_table(10)
        pq_path = tmp_path / "s.parquet"
        bcsv_path = tmp_path / "s.bcsv"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
            _flat_layout(),
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
            _flat_layout(),
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
            _flat_layout(),
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
                _flat_layout(),
                row_codec=codec,
            )
            assert result["rows"] == 5

    def test_parquet_compression_snappy(self, tmp_path):
        pq_path = tmp_path / "c5.parquet"
        bcsv_path = tmp_path / "c5.bcsv"
        pq.write_table(_flat_table(5), str(pq_path))
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), _flat_layout())
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
            _flat_layout(),
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
            _flat_layout(),
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
            _flat_layout(),
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
            "id:int64,location.lat:float,location.lon:float",
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
    """Regression: FixedSizeList extraction must use .values slice, not row index."""

    def test_list_roundtrip(self, tmp_path):
        values = [float(i) for i in range(9)]
        arr = pa.FixedSizeListArray.from_arrays(pa.array(values, type=pa.float64()), 3)
        table = pa.table({"id": pa.array([1, 2, 3], type=pa.int64()), "vals": arr})
        pq_path = tmp_path / "list.parquet"
        bcsv_path = tmp_path / "list.bcsv"
        rt_path = tmp_path / "list_rt.parquet"
        pq.write_table(table, str(pq_path))
        parquet_to_bcsv(
            str(pq_path),
            str(bcsv_path),
            "id:int64,vals[0]:double,vals[1]:double,vals[2]:double",
        )
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 3

        rt = pq.read_table(str(rt_path))
        orig_values = table.column("vals").combine_chunks().values.to_pylist()
        rt_values = rt.column("vals").combine_chunks().values.to_pylist()
        assert rt_values == orig_values


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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), "id:int64,val:double")
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
            "id:int64,location.lat:float,location.lon:float",
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
        parquet_to_bcsv(str(pq_path), str(bcsv_path), "x:float")
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
            _flat_layout(),
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

    def test_trailing_underscore_rejected_layout(self, tmp_path):
        table = pa.table({"id": pa.array([1])})
        pq.write_table(table, str(tmp_path / "u.parquet"))
        with pytest.raises(ValueError, match="ends with '_'"):
            parquet_to_bcsv(
                str(tmp_path / "u.parquet"),
                str(tmp_path / "u.bcsv"),
                "id_:int64",
            )

    def test_trailing_underscore_rejected_parquet(self, tmp_path):
        # Column name ending in _ in the original Parquet schema
        table = pa.table({"data_": pa.array([1.0], type=pa.float64())})
        pq.write_table(table, str(tmp_path / "u2.parquet"))
        with pytest.raises(ValueError, match="ends with '_'"):
            parquet_to_bcsv(
                str(tmp_path / "u2.parquet"),
                str(tmp_path / "u2.bcsv"),
                "data_:double",
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
            "id:int64,vals[0]:float,vals[1]:float,vals[2]:float",
        )
        result = bcsv_to_parquet(str(bcsv_path), str(rt_path), force=True)
        assert result["rows"] == 3

        rt = pq.read_table(str(rt_path))
        orig = table.column("vals").combine_chunks().values.to_pylist()
        rtv = rt.column("vals").combine_chunks().values.to_pylist()
        assert rtv == orig


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
