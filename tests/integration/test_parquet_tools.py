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


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
