#!/usr/bin/env python3
"""
Validate that Python-vendored C API files stay byte-identical to root C API files.

Fails with non-zero exit code when drift is detected.
"""

from __future__ import annotations

import difflib
from pathlib import Path


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent

    file_pairs = [
        (
            repo_root / "include" / "bcsv" / "bcsv_c_api.h",
            repo_root / "python" / "include" / "bcsv" / "bcsv_c_api.h",
        ),
        (
            repo_root / "include" / "bcsv" / "bcsv_c_api.cpp",
            repo_root / "python" / "include" / "bcsv" / "bcsv_c_api.cpp",
        ),
    ]

    has_mismatch = False
    for source, vendored in file_pairs:
        if not source.exists() or not vendored.exists():
            print(f"[ERROR] Missing file in sync check pair: {source} <-> {vendored}")
            has_mismatch = True
            continue

        src = _read_text(source)
        dst = _read_text(vendored)
        if src == dst:
            print(f"[OK] {vendored.relative_to(repo_root)} is in sync")
            continue

        has_mismatch = True
        print(f"[MISMATCH] {vendored.relative_to(repo_root)} differs from {source.relative_to(repo_root)}")
        diff = difflib.unified_diff(
            src.splitlines(),
            dst.splitlines(),
            fromfile=str(source.relative_to(repo_root)),
            tofile=str(vendored.relative_to(repo_root)),
            lineterm="",
        )
        for idx, line in enumerate(diff):
            if idx >= 200:
                print("... diff truncated ...")
                break
            print(line)

    if has_mismatch:
        print("\nSync check failed. Run: cd python && python sync_headers.py --force --verbose")
        return 1

    print("\nAll Python-vendored C API files are synchronized.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
