# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""Python -> column conversion of Writer.write_row(), pinned value by value.

write_row() converts the four exact builtin types (float, int, bool, str) on a
fast path and everything else on the generic path. These cases fix what each
input stores (or which error it raises), so the fast path can never drift from
the generic semantics. Every expectation here was recorded with pybcsv 1.5.22,
before the fast path existed.
"""

import math

import numpy as np
import pytest

import pybcsv

CT = pybcsv.ColumnType
ERR = object()          # marker: write_row must raise RuntimeError

CASES = {
    CT.BOOL: [(True, True), (False, False), (1, True), (0, False), (2, True), (-1, True),
              ('true', True), ('False', False), ('1', True), (np.bool_(True), ERR),
              (1.5, True), (0.0, False)],
    CT.UINT8: [(0, 0), (255, 255), (256, 0), (-1, 255), (3.7, 3), (np.uint8(7), 7),
               ('12', 12), (True, 1), (2**64, ERR), (2**70, ERR)],
    CT.UINT16: [(65535, 65535), (65536, 0), (-1, 65535), (np.int64(9), 9)],
    CT.UINT32: [(2**32 - 1, 2**32 - 1), (2**32, 0), (-1, 2**32 - 1)],
    CT.UINT64: [(2**64 - 1, 2**64 - 1), (2**63, 2**63), (2**63 - 1, 2**63 - 1),
                (-1, 2**64 - 1), (2**64, ERR), (-2**63, 2**63), (-2**63 - 1, ERR)],
    CT.INT8: [(127, 127), (-128, -128), (128, -128), (-129, 127), (np.int8(-3), -3)],
    CT.INT16: [(32767, 32767), (-32768, -32768), (40000, -25536), (-40000, 25536),
               (2.9, 2), (-2.9, -2)],
    CT.INT32: [(2**31 - 1, 2**31 - 1), (-2**31, -2**31), (2**31, -2**31)],
    CT.INT64: [(2**63 - 1, 2**63 - 1), (-2**63, -2**63), (2**63, ERR), (-2**63 - 1, ERR)],
    CT.FLOAT: [(1.5, 1.5), (0.1, float(np.float32(0.1))), (1e40, math.inf), (-1e40, -math.inf),
               (3, 3.0), (2**70, float(np.float32(2.0**70))), (10**400, ERR),
               (math.inf, math.inf), (np.float32(1.25), 1.25),
               (np.float64(0.1), float(np.float32(0.1))), ('2.5', 2.5), (True, 1.0), (None, ERR)],
    CT.DOUBLE: [(0.1, 0.1), (2**53 + 1, float(2**53)), (10**400, ERR),
                (np.float64(1e-300), 1e-300), ('1e5', 1e5), ('x', ERR), (False, 0.0)],
    CT.STRING: [('abc', 'abc'), ('', ''), ('ü€', 'ü€'), (42, '42'), (None, 'None'),
                (1.5, '1.5'), ('\ud800', ERR), ('x' * 65535, 'x' * 65535), ('x' * 65536, ERR)],
}


def _roundtrip(path, ctype, value, codec):
    lay = pybcsv.Layout()
    lay.add_column('c', ctype)
    w = pybcsv.Writer(lay, codec)
    w.open(path, overwrite=True)
    try:
        w.write_row([value])
    finally:
        w.close()
    r = pybcsv.Reader()
    r.open(path)
    rows = r.read_all()
    r.close()
    return rows[0][0]


@pytest.mark.parametrize('codec', ['delta', 'flat'])
@pytest.mark.parametrize('ctype', list(CASES), ids=lambda t: str(t).split('.')[-1])
def test_write_row_converts_like_1_5_22(tmp_bcsv, ctype, codec):
    for value, want in CASES[ctype]:
        if want is ERR:
            with pytest.raises(RuntimeError):
                _roundtrip(tmp_bcsv, ctype, value, codec)
            continue
        got = _roundtrip(tmp_bcsv, ctype, value, codec)
        assert type(got) is type(want) and (got == want or (got != got and want != want)), \
            f'{ctype} {value!r}: got {got!r}, want {want!r}'


def test_nan_float_column(tmp_bcsv):
    assert math.isnan(_roundtrip(tmp_bcsv, CT.FLOAT, float('nan'), 'delta'))
    assert math.isnan(_roundtrip(tmp_bcsv, CT.DOUBLE, float('nan'), 'delta'))


@pytest.mark.parametrize('codec', ['delta', 'zoh', 'flat'])
def test_string_column_unchanged_and_changed_values(tmp_bcsv, codec):
    """A STRING column holding the same object row after row (a constant meta
    column) and one that changes, across a flush (packet boundary)."""
    lay = pybcsv.Layout()
    lay.add_column('i', CT.INT32)
    lay.add_column('const', CT.STRING)
    lay.add_column('var', CT.STRING)
    const = 'pat9136:12,icm42688:12,iim46234:3'
    var = ['a', 'a', 'bb', 'a', '', '', 'long string beyond the SSO limit', 'a']
    w = pybcsv.Writer(lay, codec)
    w.open(tmp_bcsv, overwrite=True)
    for i, v in enumerate(var):
        w.write_row([i, const, v])
        if i == 3:
            w.flush()
    w.close()
    r = pybcsv.Reader()
    r.open(tmp_bcsv)
    rows = r.read_all()
    r.close()
    assert rows == [[i, const, v] for i, v in enumerate(var)]


def test_reused_list_filled_in_place(tmp_bcsv):
    """The recommended hot-loop pattern: one list, meta tail set once, the
    value part slice-assigned each row. Must store exactly what fresh lists do."""
    lay = pybcsv.Layout()
    for name, t in [('f', CT.FLOAT), ('u', CT.UINT8), ('m', CT.STRING)]:
        lay.add_column(name, t)
    buf = [None, None, 'meta']
    w = pybcsv.Writer(lay)
    w.open(tmp_bcsv, overwrite=True)
    for i in range(100):
        buf[:2] = [i * 0.5, i]
        w.write_row(buf)
    w.close()
    d = pybcsv.read_columns(tmp_bcsv)
    assert d['f'].tolist() == [i * 0.5 for i in range(100)]
    assert d['u'].tolist() == list(range(100))
    assert d['m'] == ['meta'] * 100


def test_list_mutated_by_conversion_raises(tmp_bcsv):
    """A value whose conversion runs Python code that shrinks the row list
    must raise, not read past the end of the list."""
    lay = pybcsv.Layout()
    lay.add_column('a', CT.INT32)
    lay.add_column('b', CT.INT32)
    lay.add_column('c', CT.INT32)
    row = []

    class Shrinker:
        def __index__(self):
            row.clear()
            return 1

    row.extend([Shrinker(), 2, 3])
    w = pybcsv.Writer(lay)
    w.open(tmp_bcsv, overwrite=True)
    with pytest.raises(RuntimeError):
        w.write_row(row)
    w.close()
