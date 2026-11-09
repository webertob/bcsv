# Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""Writer.flush(wait=False) (1.5.23): the non-blocking flush."""

import math
import os

import pytest

import pybcsv

N_ROWS = 12000


def make_layout():
    layout = pybcsv.Layout()
    layout.add_column("t", pybcsv.ColumnType.UINT64)
    for k in range(20):
        layout.add_column(f"f{k}", pybcsv.ColumnType.FLOAT)
    layout.add_column("s", pybcsv.ColumnType.STRING)
    return layout


def row(i):
    return [1000 * i] + [math.sin(i * 0.01 + k) for k in range(20)] + [f"mode-{i // 2500}"]


def write(path, wait, codec="delta", level=6):
    w = pybcsv.Writer(make_layout(), codec)
    w.open(str(path), overwrite=True, compression_level=level, block_size_kb=64)
    for i in range(N_ROWS):
        w.write_row(row(i))
        if (i + 1) % 1000 == 0:
            w.flush(wait=wait)
    w.flush(wait=wait)               # empty packet
    w.close()


def masked(path):
    b = bytearray(open(path, "rb").read())
    b[4:12] = bytes(8)               # creation time
    return bytes(b)


@pytest.mark.parametrize("level", [1, 6])
@pytest.mark.parametrize("codec", ["delta", "zoh", "flat"])
def test_same_bytes_as_blocking_flush(tmp_path, codec, level):
    write(tmp_path / "wait.bcsv", True, codec, level)
    write(tmp_path / "nowait.bcsv", False, codec, level)
    assert masked(tmp_path / "nowait.bcsv") == masked(tmp_path / "wait.bcsv")
    r = pybcsv.Reader()
    r.open(str(tmp_path / "nowait.bcsv"))
    assert r.count_rows() == N_ROWS
    r.close()


def test_default_is_blocking_and_rows_are_readable(tmp_path):
    p = tmp_path / "durable.bcsv"
    w = pybcsv.Writer(make_layout())
    w.open(str(p), overwrite=True, compression_level=1)
    for i in range(3000):
        w.write_row(row(i))
    w.flush(wait=False)
    for i in range(3000, 4000):
        w.write_row(row(i))
    w.flush()                        # wait=True: everything above is in the OS
    r = pybcsv.Reader()
    r.open(str(p))
    n = 0
    while r.read_next():
        n += 1
    r.close()
    assert n == 4000
    w.close()


@pytest.mark.skipif(not os.path.exists("/dev/full"), reason="needs /dev/full")
def test_background_write_failure_is_raised():
    w = pybcsv.Writer(make_layout())
    w.open("/dev/full", overwrite=True, compression_level=1, block_size_kb=64)
    with pytest.raises(RuntimeError):
        for i in range(2000):
            w.write_row(row(i))
        w.flush(wait=False)          # hands off; the write fails in the background
        w.flush()                    # waits for that packet and reports it
    try:
        w.close()
    except RuntimeError:
        pass
    assert not w.is_open()
