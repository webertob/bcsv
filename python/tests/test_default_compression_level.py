"""Every writer entry point must default to the same compression level.

These assertions read the level back out of the written file's header
(``Reader.compression_level``) rather than inspecting source literals, because
the defect they guard against was exactly a source literal that had drifted:
``Writer.open`` moved to level 6 while ``parquet2bcsv --compression-level`` and
``BcsvColumns.WriteColumns`` kept writing level 1, so identical data compressed
differently depending on which API you called. Only the file tells the truth.
"""

import os
import subprocess
import sys
import tempfile
import unittest

import pybcsv

DEFAULT = pybcsv.DEFAULT_COMPRESSION_LEVEL


def _level_of(path: str) -> int:
    reader = pybcsv.Reader()
    reader.open(path)
    try:
        return reader.compression_level()
    finally:
        reader.close()


class _TempDirCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="bcsv_level_")
        self.tmp = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def path(self, name: str) -> str:
        return os.path.join(self.tmp, name)


def _layout():
    layout = pybcsv.Layout()
    layout.add_column("id", pybcsv.ColumnType.INT32)
    layout.add_column("val", pybcsv.ColumnType.DOUBLE)
    return layout


class TestDefaultIsSix(_TempDirCase):
    """The constant itself, and the plain Writer path."""

    def test_constant_matches_native_header(self):
        self.assertEqual(DEFAULT, 6)

    def test_writer_open_default(self):
        p = self.path("w.bcsv")
        w = pybcsv.Writer(_layout())
        self.assertTrue(w.open(p, True))
        # The writer reports the level it is actually using, before any file is
        # closed; the header check below then confirms it reached the file.
        self.assertEqual(w.compression_level(), DEFAULT)
        for i in range(100):
            w.write_row([i, i * 1.5])
        w.close()
        self.assertEqual(_level_of(p), DEFAULT)

    def test_explicit_level_still_wins(self):
        """Defaulting must not override an explicit request."""
        for level in (0, 1, 9):
            with self.subTest(level=level):
                p = self.path(f"w{level}.bcsv")
                w = pybcsv.Writer(_layout())
                self.assertTrue(w.open(p, True, level))
                w.write_row([1, 1.0])
                w.close()
                self.assertEqual(_level_of(p), level)


class TestColumnarAndArrowDefaults(_TempDirCase):
    """The bulk-write entry points bound in bindings.cpp."""

    def test_write_columns_default(self):
        p = self.path("cols.bcsv")
        pybcsv.write_columns(
            p,
            {"id": list(range(100)), "val": [float(i) for i in range(100)]},
            ["id", "val"],
            [pybcsv.ColumnType.INT32, pybcsv.ColumnType.DOUBLE],
        )
        self.assertEqual(_level_of(p), DEFAULT)

    def test_write_from_arrow_default(self):
        try:
            import pyarrow as pa
        except ImportError:
            self.skipTest("pyarrow not installed")
        p = self.path("arrow.bcsv")
        table = pa.table(
            {
                "id": pa.array(range(100), pa.int32()),
                "val": pa.array([float(i) for i in range(100)], pa.float64()),
            }
        )
        pybcsv.write_from_arrow(p, table)
        self.assertEqual(_level_of(p), DEFAULT)


class TestPandasAndPolarsDefaults(_TempDirCase):
    def test_write_dataframe_default(self):
        try:
            import pandas as pd
        except ImportError:
            self.skipTest("pandas not installed")
        p = self.path("pd.bcsv")
        pybcsv.write_dataframe(pd.DataFrame({"id": range(100)}), p)
        self.assertEqual(_level_of(p), DEFAULT)

    def test_write_polars_default(self):
        try:
            import polars as pl
        except ImportError:
            self.skipTest("polars not installed")
        p = self.path("pl.bcsv")
        pybcsv.write_polars(pl.DataFrame({"id": list(range(100))}), p)
        self.assertEqual(_level_of(p), DEFAULT)


class TestParquetEntryPointsAgree(_TempDirCase):
    """The Python API and the parquet2bcsv CLI must produce the same level.

    They disagreed: the function signature said 6, argparse said 1.
    """

    def _src_parquet(self) -> str:
        try:
            import pyarrow as pa
            import pyarrow.parquet as pq
        except ImportError:
            self.skipTest("pyarrow not installed")
        p = self.path("src.parquet")
        pq.write_table(
            pa.table(
                {
                    "id": pa.array(range(500), pa.int32()),
                    "val": pa.array([float(i) for i in range(500)], pa.float64()),
                }
            ),
            p,
        )
        return p

    def test_parquet_to_bcsv_api_default(self):
        out = self.path("api.bcsv")
        pybcsv.parquet_to_bcsv(
            self._src_parquet(), out, force=True, metadata2json=False,
            source_hash=False, bcsv_hash=False,
        )
        self.assertEqual(_level_of(out), DEFAULT)

    def test_parquet2bcsv_cli_default_matches_api(self):
        src = self._src_parquet()
        api, cli = self.path("api.bcsv"), self.path("cli.bcsv")
        pybcsv.parquet_to_bcsv(
            src, api, force=True, metadata2json=False, source_hash=False,
            bcsv_hash=False,
        )
        result = subprocess.run(
            [sys.executable, "-c",
             "from pybcsv.parquet_utils import parquet2bcsv_cli; parquet2bcsv_cli()",
             src, "-o", cli, "-f", "--no-metadata2json"],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(_level_of(cli), DEFAULT)
        self.assertEqual(_level_of(cli), _level_of(api))


class TestBundledCliDefaults(_TempDirCase):
    """The bundled C++ CLI tools read DEFAULT_COMPRESSION_LEVEL from the header."""

    def test_csv2bcsv_default(self):
        import shutil

        exe = shutil.which("csv2bcsv")
        if exe is None:
            self.skipTest("csv2bcsv not on PATH")
        csv = self.path("in.csv")
        with open(csv, "w") as fh:
            fh.write("id,val\n")
            for i in range(500):
                fh.write(f"{i},{i * 1.5}\n")
        out = self.path("out.bcsv")
        result = subprocess.run([exe, csv, out, "--overwrite"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)
        self.assertEqual(_level_of(out), DEFAULT)


if __name__ == "__main__":
    unittest.main()
