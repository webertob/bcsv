# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

"""
Pytest configuration and shared fixtures for BCSV Python tests.

Both unittest.TestCase subclasses and plain pytest functions are discovered
automatically by ``pytest``.  Run the full suite with::

    pytest python/tests/ -v
"""

import os
import sys
import tempfile
import pytest

# ---------------------------------------------------------------------------
# Ensure pybcsv is importable regardless of working directory
# ---------------------------------------------------------------------------
_project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _project_root not in sys.path:
    sys.path.insert(0, _project_root)


# ---------------------------------------------------------------------------
# Shared fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def tmp_bcsv(tmp_path):
    """Return a temporary .bcsv file path that is cleaned up automatically."""
    return str(tmp_path / "test_output.bcsv")


@pytest.fixture
def tmp_dir(tmp_path):
    """Return a temporary directory path."""
    return str(tmp_path)
