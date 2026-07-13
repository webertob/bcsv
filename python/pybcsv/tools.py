# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""Access to the BCSV CLI tools bundled with pybcsv.

The wheel installs a curated set of native command-line tools into the
environment's scripts directory (the same place console entry points go, so
they are on PATH in an activated environment)::

    csv2bcsv data.csv data.bcsv          # from a shell

For programmatic use, :func:`run` wraps :mod:`subprocess` with the resolved
tool path::

    import pybcsv.tools as tools
    tools.run("csv2bcsv", "data.csv", "data.bcsv", "--overwrite")
    print(tools.run("bcsvHeader", "data.bcsv").stdout)

The bundled tools are always the same version as the pybcsv library, so file
format and behavior are guaranteed to match.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sysconfig

#: Tools shipped in the wheel — keep in sync with PYBCSV_TOOLS in CMakeLists.txt.
TOOLS = (
    "csv2bcsv", "bcsv2csv",                      # converters
    "bcsvHeader", "bcsvHead", "bcsvTail",        # inspection
    "bcsvCast", "bcsvSampler",                   # transform / filter
    "bcsvValidate", "bcsvRepair", "bcsvCompare", # integrity & ops
    "bcsvGenerator",                             # synthetic test data
)

__all__ = ["TOOLS", "path", "run"]


def path(name: str) -> str:
    """Return the absolute path of a bundled CLI tool.

    Raises ValueError for names not in :data:`TOOLS` and FileNotFoundError
    when the tool is not present (e.g. a source build with
    ``-DPYBCSV_BUILD_TOOLS=OFF``).
    """
    if name not in TOOLS:
        raise ValueError(f"unknown tool {name!r}; bundled tools: {', '.join(TOOLS)}")
    exe = name + (".exe" if os.name == "nt" else "")

    # The wheel installs tools into the scripts dir of the running environment.
    candidate = os.path.join(sysconfig.get_path("scripts"), exe)
    if os.path.isfile(candidate):
        return candidate

    # Fallback (user-site installs, unusual layouts): whatever PATH resolves.
    found = shutil.which(exe)
    if found:
        return found

    raise FileNotFoundError(
        f"{name} is not installed with this pybcsv build "
        "(built with -DPYBCSV_BUILD_TOOLS=OFF?)"
    )


def run(name: str, *args: object, check: bool = True, capture_output: bool = True,
        text: bool = True, **kwargs) -> subprocess.CompletedProcess:
    """Run a bundled CLI tool and return the CompletedProcess.

    Arguments are stringified (so Path objects and numbers work directly).
    By default output is captured as text and a non-zero exit raises
    CalledProcessError; pass ``check=False`` / ``capture_output=False`` to
    override. Remaining keyword arguments go to :func:`subprocess.run`.
    """
    cmd = [path(name), *(str(a) for a in args)]
    return subprocess.run(cmd, check=check, capture_output=capture_output,
                          text=text, **kwargs)
