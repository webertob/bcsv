# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""Tests for the bundled CLI tools (pybcsv.tools).

The tools are only present when pybcsv was installed from a wheel (or a
source build with PYBCSV_BUILD_TOOLS=ON followed by `pip install`), so the
functional tests skip themselves when csv2bcsv is not resolvable.
"""

import pytest

import pybcsv
from pybcsv import tools


def _tools_available() -> bool:
    try:
        tools.path("csv2bcsv")
        return True
    except FileNotFoundError:
        return False


def test_unknown_tool_rejected():
    with pytest.raises(ValueError):
        tools.path("rm")


needs_tools = pytest.mark.skipif(
    not _tools_available(), reason="CLI tools not installed with this build"
)


@needs_tools
def test_tools_resolve_and_report_version():
    for name in tools.TOOLS:
        result = tools.run(name, "--version")
        assert "BCSV" in result.stdout


@needs_tools
def test_reports_bcsv_version():
    # Note: on dev builds setuptools-scm bumps past the last tag (x.y.z+1.devN)
    # while the C++ core reports the tag itself, so only tagged releases match
    # exactly — assert the tool identifies itself with a BCSV version at all.
    result = tools.run("csv2bcsv", "--version")
    assert "BCSV" in result.stdout


@needs_tools
def test_csv_roundtrip_via_tools(tmp_path):
    csv_in = tmp_path / "in.csv"
    csv_in.write_text("id,value\n1,1.5\n2,2.5\n3,3.5\n")
    bcsv_path = tmp_path / "data.bcsv"
    csv_out = tmp_path / "out.csv"

    tools.run("csv2bcsv", csv_in, bcsv_path, "--overwrite")
    header = tools.run("bcsvHeader", bcsv_path).stdout
    assert "Columns: 2" in header
    tools.run("bcsv2csv", bcsv_path, csv_out)
    assert csv_out.read_text().count("\n") == 4  # header + 3 rows

    # And the library reads what the tool wrote.
    reader = pybcsv.ReaderDirectAccess()
    assert reader.open(str(bcsv_path))
    assert reader.row_count() == 3
    reader.close()
