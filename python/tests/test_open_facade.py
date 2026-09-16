#!/usr/bin/env python3
"""
Tests for the pybcsv.open() auto-detecting façade (ADR-0009).

Covers:
- Round-trip write → read through pybcsv.open ONLY for every format:
  .bcsv (binary), .csv (plain text), .csv.lz4 (LZ4 frame by extension)
- Explicit rt/wt and rb/wb mode overrides
- Missing-layout errors (CSV read and any write)
"""

# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

import pytest

import pybcsv


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_layout(*cols):
    """Build a Layout from (name, type) pairs."""
    layout = pybcsv.Layout()
    for name, ctype in cols:
        layout.add_column(name, ctype)
    return layout


LAYOUT_COLS = (("a", pybcsv.ColumnType.INT32), ("b", pybcsv.ColumnType.DOUBLE))
ROWS = [[1, 1.5], [2, 2.5], [3, 3.5]]


def _write_all(obj, rows):
    for r in rows:
        obj.write_row(r)


def _csv_tuples(reader):
    # read_all() rows are positional lists in layout-column order
    return [(row[0], row[1]) for row in reader.read_all()]


# ---------------------------------------------------------------------------
# Round-trips — one façade, three formats
# ---------------------------------------------------------------------------


class TestOpenFacade:
    def test_bcsv_round_trip(self, tmp_path):
        path = str(tmp_path / "data.bcsv")
        layout = _make_layout(*LAYOUT_COLS)

        w = pybcsv.open(path, layout, mode="w", overwrite=True)
        assert isinstance(w, pybcsv.Writer)      # extension decides: .bcsv
        _write_all(w, ROWS)
        w.close()

        # Binary reads need no layout — the file carries its schema.
        r = pybcsv.open(path)
        assert isinstance(r, pybcsv.ReaderDirectAccess)
        assert r.row_count() == len(ROWS)
        assert [list(r[i]) for i in range(r.row_count())] == [list(x) for x in ROWS]
        r.close()

    def test_csv_round_trip(self, tmp_path):
        path = str(tmp_path / "data.csv")
        layout = _make_layout(*LAYOUT_COLS)

        w = pybcsv.open(path, layout, mode="w", overwrite=True)
        assert isinstance(w, pybcsv.CsvWriter)
        _write_all(w, ROWS)
        w.close()

        r = pybcsv.open(path, layout)
        assert isinstance(r, pybcsv.CsvReader)
        assert _csv_tuples(r) == [tuple(x) for x in ROWS]
        r.close()

    def test_csv_lz4_round_trip_and_magic(self, tmp_path):
        path = str(tmp_path / "data.csv.lz4")
        layout = _make_layout(*LAYOUT_COLS)

        # The .lz4 extension selects the frame writer; knobs travel to set_lz4.
        w = pybcsv.open(path, layout, mode="w", overwrite=True,
                        lz4_block_max_size_id=5, lz4_level=9)   # 256KB, HC
        assert isinstance(w, pybcsv.CsvWriter)
        _write_all(w, ROWS)
        w.close()

        with open(path, "rb") as f:   # this file's open is still builtins.open
            assert f.read(4) == b"\x04\x22\x4d\x18"   # LZ4 frame magic

        r = pybcsv.open(path, layout)  # sniffed by magic, not extension
        assert isinstance(r, pybcsv.CsvReader)
        assert _csv_tuples(r) == [tuple(x) for x in ROWS]
        r.close()

    # -----------------------------------------------------------------------
    # Explicit modes
    # -----------------------------------------------------------------------

    def test_forced_text_mode_beats_bcsv_extension(self, tmp_path):
        path = str(tmp_path / "forced.bcsv")
        layout = _make_layout(*LAYOUT_COLS)

        w = pybcsv.open(path, layout, mode="wt", overwrite=True)
        assert isinstance(w, pybcsv.CsvWriter)   # wt overrides .bcsv name
        _write_all(w, ROWS)
        w.close()

        with open(path, "rb") as f:   # plain builtin open — pybcsv.open never shadows it
            assert f.read(4) != b"BCSV"          # it is text, not binary

        r = pybcsv.open(path, layout, mode="rt")
        assert isinstance(r, pybcsv.CsvReader)
        assert len(r.read_all()) == len(ROWS)
        r.close()

    def test_forced_binary_modes(self, tmp_path):
        path = str(tmp_path / "forced.bin")      # no .bcsv extension
        layout = _make_layout(*LAYOUT_COLS)

        w = pybcsv.open(path, layout, mode="wb", overwrite=True)
        assert isinstance(w, pybcsv.Writer)      # wb overrides the extension
        _write_all(w, ROWS)
        w.close()

        with open(path, "rb") as f:
            assert f.read(4) == b"BCSV"

        r = pybcsv.open(path, mode="rb")
        assert isinstance(r, pybcsv.ReaderDirectAccess)
        assert r.row_count() == len(ROWS)
        r.close()

    def test_unknown_mode_rejected(self, tmp_path):
        with pytest.raises(ValueError):
            pybcsv.open(str(tmp_path / "x.bcsv"), _make_layout(*LAYOUT_COLS),
                        mode="rw")

    # -----------------------------------------------------------------------
    # Guard rails (round-2 review): pinned modes are loud, never garbage
    # -----------------------------------------------------------------------

    def test_rt_refuses_binary_magic(self, tmp_path):
        # ADR-0009 hazard 1 on the Python side: 'rt' must not feed binary
        # bytes to CsvReader (has_header=False would yield garbage rows).
        path = str(tmp_path / "data.bcsv")
        layout = _make_layout(*LAYOUT_COLS)
        w = pybcsv.open(path, layout, mode="w", overwrite=True)
        _write_all(w, ROWS)
        w.close()

        with pytest.raises(ValueError):
            pybcsv.open(path, layout, mode="rt")
        # ...and the same file opens fine when the mode allows binary:
        r = pybcsv.open(path, mode="rb")
        assert r.row_count() == len(ROWS)
        r.close()

    def test_lz4_knobs_on_binary_write_rejected(self, tmp_path):
        # Silently dropping compression knobs is a silent-surprise hazard.
        layout = _make_layout(*LAYOUT_COLS)
        with pytest.raises(ValueError):
            pybcsv.open(str(tmp_path / "x.bcsv"), layout, mode="w",
                        overwrite=True, lz4_level=9)
        with pytest.raises(ValueError):
            pybcsv.open(str(tmp_path / "x.bcsv"), layout, mode="wb",
                        overwrite=True, lz4_block_max_size_id=6)
        # Same knobs are accepted for CSV output (only the frame applies).
        w = pybcsv.open(str(tmp_path / "x.csv"), layout, mode="wt",
                        overwrite=True, lz4_level=9)   # plain .csv: inert
        _write_all(w, ROWS)
        w.close()

    def test_extension_lie_on_plain_text_read(self, tmp_path):
        # Plain bytes named .csv.lz4: magic decides (no frame) -> plain CSV,
        # matching native AUTO. Locks the fallback semantics.
        path = tmp_path / "lied.csv.lz4"
        with open(path, "w") as f:
            f.write("a,b\n1,1.5\n2,2.5\n")
        r = pybcsv.open(str(path), _make_layout(*LAYOUT_COLS))
        assert isinstance(r, pybcsv.CsvReader)
        assert _csv_tuples(r) == [(1, 1.5), (2, 2.5)]
        r.close()

    def test_skippable_frame_magic_matches_cpp_sniff(self, tmp_path):
        # lz4_frame_sniff() also accepts skippable frames (0x184D2A50..5F);
        # the façade must classify the same way, even under a .bcsv name.
        layout = _make_layout(*LAYOUT_COLS)
        frame_path = tmp_path / "inner.csv.lz4"
        w = pybcsv.open(str(frame_path), layout, mode="w", overwrite=True,
                        lz4_block_max_size_id=5)
        _write_all(w, ROWS)
        w.close()
        with open(frame_path, "rb") as f:
            frame = f.read()
        skippable = b"\x50\x2a\x4d\x18" + (0).to_bytes(4, "little")
        out = tmp_path / "masked.bcsv"          # name lies, magic wins
        with open(out, "wb") as f:
            f.write(skippable + frame)

        r = pybcsv.open(str(out), layout)
        assert isinstance(r, pybcsv.CsvReader)
        assert _csv_tuples(r) == [tuple(x) for x in ROWS]
        r.close()

    # -----------------------------------------------------------------------
    # Missing layout
    # -----------------------------------------------------------------------

    def test_missing_layout_errors(self, tmp_path):
        # Write: layout always required (schema must come from somewhere).
        with pytest.raises(TypeError):
            pybcsv.open(str(tmp_path / "x.csv"), mode="w")
        with pytest.raises(TypeError):
            pybcsv.open(str(tmp_path / "y.bcsv"), mode="wb")

        # Read: a text file carries no schema — loud TypeError, not garbage.
        csv_path = tmp_path / "z.csv"
        with open(csv_path, "w") as f:
            f.write("a,b\n1,1.5\n")
        with pytest.raises(TypeError):
            pybcsv.open(str(csv_path))
