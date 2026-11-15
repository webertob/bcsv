# Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""Files whose writer never reached close(), and footers that disagree with the packets (1.5.25).

Up to 1.5.24:
- ``ReaderDirectAccess.open(path, rebuild_footer=True)`` and ``Reader.count_rows()``
  never returned on a file without a footer and grew without bound;
- ``bcsvRepair`` wrote a footer one batch packet short;
- ``read_to_arrow``, ``read_columns`` and ``read_dataframe`` sized their buffers by the
  footer's row count and wrote past them when the packets held more rows (heap
  corruption), and returned uninitialised rows when they held fewer.

The rule since 1.5.25: the bulk readers return the rows the packets hold - what the
sequential ``Reader`` delivers; the footer's row count only sizes their first
allocation. ``ReaderDirectAccess`` and ``iter_arrow_batches`` address rows through the
footer's index and raise when a packet disagrees with it.
"""

import json
import os
import struct
import subprocess
import sys

import pytest

import pybcsv

N_ROWS = 20000         # flat: ~ten 64 kB packets
_M64 = (1 << 64) - 1


# ── a footer with any row count / index: XXH64 in pure Python ────────────────

_P1, _P2, _P3, _P4, _P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                           0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _M64


def _round(acc, lane):
    return (_rotl((acc + lane * _P2) & _M64, 31) * _P1) & _M64


def _xxh64(data, seed=0):
    n, i = len(data), 0
    if n >= 32:
        v = [(seed + _P1 + _P2) & _M64, (seed + _P2) & _M64, seed, (seed - _P1) & _M64]
        while i + 32 <= n:
            for k in range(4):
                v[k] = _round(v[k], struct.unpack_from('<Q', data, i + 8 * k)[0])
            i += 32
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & _M64
        for k in range(4):
            h = ((h ^ _round(0, v[k])) * _P1 + _P4) & _M64
    else:
        h = (seed + _P5) & _M64
    h = (h + n) & _M64
    while i + 8 <= n:
        h = (_rotl(h ^ _round(0, struct.unpack_from('<Q', data, i)[0]), 27) * _P1 + _P4) & _M64
        i += 8
    if i + 4 <= n:
        h = (_rotl(h ^ (struct.unpack_from('<I', data, i)[0] * _P1 & _M64), 23) * _P2 + _P3) & _M64
        i += 4
    while i < n:
        h = (_rotl(h ^ (data[i] * _P5 & _M64), 11) * _P1) & _M64
        i += 1
    h ^= h >> 33
    h = h * _P2 & _M64
    h ^= h >> 29
    h = h * _P3 & _M64
    return h ^ (h >> 32)


def read_footer(path):
    """(packet index [(offset, first_row)], row count, footer start) of a closed file."""
    data = open(path, 'rb').read()
    magic, start, rows, checksum = struct.unpack_from('<4sIQQ', data, len(data) - 24)
    assert magic == b'EIDX'
    begin = len(data) - start
    assert data[begin:begin + 4] == b'BIDX'
    assert _xxh64(data[begin:len(data) - 8]) == checksum      # checks _xxh64 itself
    entries = [struct.unpack_from('<QQ', data, begin + 4 + 16 * k)
               for k in range((start - 28) // 16)]
    return entries, rows, begin


def replace_footer(path, out, index, rows):
    _, _, begin = read_footer(path)
    body = b'BIDX' + b''.join(struct.pack('<QQ', o, r) for o, r in index)
    head = body + struct.pack('<4sIQ', b'EIDX', len(body) + 24, rows)
    with open(path, 'rb') as f, open(out, 'wb') as g:
        g.write(f.read(begin) + head + struct.pack('<Q', _xxh64(head)))


# ── test files ───────────────────────────────────────────────────────────────

def make_layout():
    layout = pybcsv.Layout()
    layout.add_column('i', pybcsv.ColumnType.INT64)
    layout.add_column('x', pybcsv.ColumnType.DOUBLE)
    layout.add_column('s', pybcsv.ColumnType.STRING)
    layout.add_column('h', pybcsv.ColumnType.UINT64)   # incompressible
    return layout


def write(path, codec='flat', level=1, flags=pybcsv.FileFlags.BATCH_COMPRESS):
    w = pybcsv.Writer(make_layout(), codec)
    w.open(str(path), overwrite=True, compression_level=level, block_size_kb=64, flags=flags)
    for i in range(N_ROWS):
        w.write_row([i, i * 0.5, f's{i // 100}', (i * 0x9E3779B97F4A7C15) & _M64])
    w.close()
    return str(path)


def cut(src, dst, nbytes):
    with open(src, 'rb') as f, open(dst, 'wb') as g:
        g.write(f.read(nbytes))
    return str(dst)


def _cap_address_space():
    import resource                      # POSIX only
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))


def capped(code, timeout=60):
    """Run code in a child process with a time bound and, on Linux, an address-space
    cap: a regression to the unbounded rebuild must fail the test, not exhaust the
    machine. (Windows has no resource module; macOS does not enforce RLIMIT_AS.)"""
    preexec = _cap_address_space if sys.platform.startswith('linux') else None
    p = subprocess.run([sys.executable, '-c', 'import pybcsv\n' + code], capture_output=True,
                       text=True, timeout=timeout, preexec_fn=preexec)
    assert p.returncode == 0, p.stderr
    return p.stdout.strip()


def check_rows(values, n):
    assert list(values) == list(range(n))


@pytest.fixture(scope='module')
def batch_file(tmp_path_factory):
    path = write(tmp_path_factory.mktemp('unclosed') / 'full.bcsv')
    index, rows, _ = read_footer(path)
    assert len(index) >= 4 and rows == N_ROWS
    return path


# ── 1. footer rebuild and count_rows terminate with the complete packets ─────

@pytest.mark.parametrize('where', ['inside_payload', 'packet_boundary', 'inside_header',
                                   'inside_footer', 'no_footer'])
def test_rebuild_and_count_rows_terminate(batch_file, tmp_path, where):
    index, _, footer_start = read_footer(batch_file)
    off2, first2 = index[2]
    nbytes, expected = {
        'inside_payload':  ((off2 + index[3][0]) // 2, first2),
        'packet_boundary': (off2, first2),
        'inside_header':   (off2 + 7, first2),
        'inside_footer':   (footer_start + 10, N_ROWS),
        'no_footer':       (footer_start, N_ROWS),
    }[where]
    path = cut(batch_file, tmp_path / 'cut.bcsv', nbytes)
    assert capped(f"da=pybcsv.ReaderDirectAccess(); da.open({path!r}, rebuild_footer=True); "
                  f"print(da.row_count())") == str(expected)
    assert capped(f"r=pybcsv.Reader(); r.open({path!r}); print(r.count_rows())") == str(expected)

    # The sequential Reader delivers the rows of the complete packets, and
    # raises at a cut packet.
    r = pybcsv.Reader()
    r.open(path)
    got = []
    try:
        for row in r:
            got.append(row[0])
    except RuntimeError:
        assert where == 'inside_payload'
    check_rows(got, expected)


# ── 2. bcsvRepair: footer rows == sequential rows == dry-run rows ────────────

@pytest.mark.parametrize('mode', ['-o', '--in-place'])
def test_repair_footer_matches_sequential_and_dry_run(batch_file, tmp_path, mode):
    index, _, _ = read_footer(batch_file)
    path = cut(batch_file, tmp_path / 'cut.bcsv', (index[3][0] + index[4][0]) // 2)
    dry = json.loads(pybcsv.tools.run('bcsvRepair', path, '--dry-run', '--json').stdout)
    out = str(tmp_path / 'repaired.bcsv')
    args = ['-o', out] if mode == '-o' else ['--in-place']
    pybcsv.tools.run('bcsvRepair', path, *args)
    if mode == '--in-place':
        out = path

    da = pybcsv.ReaderDirectAccess()
    da.open(out)
    r = pybcsv.Reader()
    r.open(out)
    seq = [row[0] for row in r]
    assert da.row_count() == len(seq) == dry['rows_recovered'] == index[3][1]
    check_rows(seq, index[3][1])


# ── 3. bulk readers follow the packets, whatever the footer says ─────────────

def _bulk_reads(path):
    pa = pytest.importorskip('pyarrow')
    pd = pytest.importorskip('pandas')
    assert pa and pd
    return {
        'read_to_arrow':         pybcsv.read_to_arrow(path)['i'].to_pylist(),
        'read_to_arrow_chunked': pybcsv.read_to_arrow(path, chunk_size=1000)['i'].to_pylist(),
        'read_columns':          pybcsv.read_columns(path)['i'].tolist(),
        'read_dataframe':        pybcsv.read_dataframe(path, columns=['i'])['i'].tolist(),
    }


@pytest.mark.parametrize('variant', ['one_packet_short', 'rows_short', 'rows_long',
                                     'last_packet_unindexed'])
def test_bulk_readers_on_inconsistent_footer(batch_file, tmp_path, variant):
    index, rows, _ = read_footer(batch_file)
    last_first = index[-1][1]
    new_index, new_rows = {
        'one_packet_short':      (index, last_first),     # what 1.5.24's bcsvRepair wrote
        'rows_short':            (index, rows - 5),
        'rows_long':             (index, rows + 5),
        'last_packet_unindexed': (index[:-1], last_first),
    }[variant]
    path = str(tmp_path / 'inconsistent.bcsv')
    replace_footer(batch_file, path, new_index, new_rows)

    for name, values in _bulk_reads(path).items():
        assert len(values) == N_ROWS, name
        check_rows(values, N_ROWS)

    # Direct access addresses rows through the index. A packet the index
    # assigns rows to but that holds others is an error, never wrong rows; a
    # packet the index assigns no rows to is not read at all (a closed file may
    # legitimately end in an empty packet), so those rows are simply absent.
    da = pybcsv.ReaderDirectAccess()
    da.open(path)
    if new_rows > last_first:
        with pytest.raises(RuntimeError, match='footer inconsistent'):
            list(pybcsv.iter_arrow_batches(da, batch_size=2000, columns=['i']))
        with pytest.raises(RuntimeError, match='footer inconsistent'):
            da.read(last_first)
    else:
        got = []
        for b in pybcsv.iter_arrow_batches(da, batch_size=2000, columns=['i']):
            got.extend(b['i'].to_pylist())
        check_rows(got, new_rows)


def test_bulk_readers_on_consistent_file(batch_file):
    for name, values in _bulk_reads(batch_file).items():
        check_rows(values, N_ROWS)
    da = pybcsv.ReaderDirectAccess()
    da.open(batch_file)
    got = []
    for b in pybcsv.iter_arrow_batches(da, batch_size=2000, columns=['i']):
        got.extend(b['i'].to_pylist())
    check_rows(got, N_ROWS)


@pytest.mark.parametrize('level', [0, 1])
def test_bulk_readers_on_stream_file(tmp_path, level):
    # No footer at all: 1.5.24 sized the buffers 0 rows and wrote past them.
    path = write(tmp_path / 'stream.bcsv', level=level, flags=pybcsv.FileFlags.STREAM_MODE)
    for name, values in _bulk_reads(path).items():
        assert len(values) == N_ROWS, name
        check_rows(values, N_ROWS)


def test_bulk_readers_on_cut_file_raise(batch_file, tmp_path):
    # Like the sequential Reader: a cut packet is an error, not a short result.
    index, _, _ = read_footer(batch_file)
    path = cut(batch_file, tmp_path / 'cut.bcsv', (index[2][0] + index[3][0]) // 2)
    for read in (lambda: pybcsv.read_to_arrow(path), lambda: pybcsv.read_columns(path),
                 lambda: pybcsv.read_to_arrow(path, chunk_size=1000)):
        with pytest.raises(RuntimeError):
            read()
    da = pybcsv.ReaderDirectAccess()
    da.open(path, rebuild_footer=True)
    got = []
    for b in pybcsv.iter_arrow_batches(da, batch_size=1000, columns=['i']):
        got.extend(b['i'].to_pylist())
    check_rows(got, index[2][1])
