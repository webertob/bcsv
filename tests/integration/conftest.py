"""Pytest configuration and fixtures for BCSV integration tests."""
import os
import shutil
import tempfile
from pathlib import Path
import pytest


def _find_bin_dir() -> Path:
    """Locate the directory containing BCSV CLI binaries."""
    tests_dir = Path(__file__).resolve().parent
    repo_root = tests_dir.parent.parent  # tests/integration -> tests -> repo

    # Check common build directories (preset builds first, then plain cmake)
    candidates = [
        repo_root / "build" / "ninja-msvc-debug" / "bin",
        repo_root / "build" / "ninja-msvc-release" / "bin",
        repo_root / "build" / "ninja-debug" / "bin",
        repo_root / "build" / "ninja-release" / "bin",
        repo_root / "build" / "clang-debug" / "bin",
        repo_root / "build" / "clang-release" / "bin",
        repo_root / "build" / "gcc-debug" / "bin",
        repo_root / "build" / "gcc-release" / "bin",
        repo_root / "build" / "bin",
    ]

    # Also check BUILD_DIR environment variable
    env_build = os.environ.get("BCSV_BUILD_DIR")
    if env_build:
        candidates.insert(0, Path(env_build) / "bin")

    for d in candidates:
        if d.is_dir() and (d / "csv2bcsv").exists():
            return d
        # Windows: look for .exe
        if d.is_dir() and (d / "csv2bcsv.exe").exists():
            return d

    # For multi-config generators (MSVC), binaries may be under bin/Debug or bin/Release
    for config in ("Debug", "Release"):
        for base_dir in ("build/msvc-debug", "build/msvc-release", "build"):
            d = repo_root / base_dir / "bin" / config
            if d.is_dir() and ((d / "csv2bcsv.exe").exists() or (d / "csv2bcsv").exists()):
                return d
            # Also check directly under the build dir (non-bin layout)
            d = repo_root / base_dir / config
            if d.is_dir() and ((d / "csv2bcsv.exe").exists() or (d / "csv2bcsv").exists()):
                return d

    pytest.skip("BCSV binaries not found — build project first")


@pytest.fixture(scope="session")
def bin_dir() -> Path:
    """Path to directory containing BCSV CLI tools."""
    return _find_bin_dir()


@pytest.fixture(scope="session")
def tools(bin_dir):
    """Dict mapping tool name -> full Path to executable."""
    names = [
        "csv2bcsv", "bcsv2csv", "bcsvHead", "bcsvTail",
        "bcsvHeader", "bcsvGenerator", "bcsvSampler", "bcsvValidate",
    ]
    result = {}
    for name in names:
        p = bin_dir / name
        if not p.exists():
            p = bin_dir / (name + ".exe")
        result[name] = p
    return result


@pytest.fixture
def tmp_dir(tmp_path):
    """Provide a temporary directory that's cleaned up automatically."""
    return tmp_path
