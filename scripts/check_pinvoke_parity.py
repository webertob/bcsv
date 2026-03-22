#!/usr/bin/env python3
"""
Check P/Invoke parity between the NuGet and Unity C# bindings.

Extracts all bcsv_* function names from each NativeMethods / BcsvNative source
file and reports any differences. Intended for CI use.

Usage:
    python scripts/check_pinvoke_parity.py          # returns 0 if in sync
    python scripts/check_pinvoke_parity.py --verbose
"""

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

NUGET_FILE = REPO_ROOT / "csharp" / "src" / "Bcsv" / "NativeMethods.cs"
UNITY_FILE = REPO_ROOT / "unity" / "Runtime" / "Scripts" / "BcsvNative.cs"

# Matches P/Invoke declarations like:  static extern ... bcsv_foo_bar(
FUNC_RE = re.compile(r"\bbcsv_\w+(?=\s*\()")

# Functions intentionally excluded from Unity (e.g. row visitor — out of scope)
EXCLUDED = {
    "bcsv_row_visit_const",
}


def extract_functions(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    return set(FUNC_RE.findall(text))


def main() -> int:
    parser = argparse.ArgumentParser(description="Check P/Invoke parity")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args()

    if not NUGET_FILE.exists():
        print(f"ERROR: NuGet file not found: {NUGET_FILE}", file=sys.stderr)
        return 1
    if not UNITY_FILE.exists():
        print(f"ERROR: Unity file not found: {UNITY_FILE}", file=sys.stderr)
        return 1

    nuget = extract_functions(NUGET_FILE) - EXCLUDED
    unity = extract_functions(UNITY_FILE) - EXCLUDED

    only_nuget = sorted(nuget - unity)
    only_unity = sorted(unity - nuget)

    if args.verbose:
        common = sorted(nuget & unity)
        print(f"NuGet functions : {len(nuget)}")
        print(f"Unity functions : {len(unity)}")
        print(f"Common          : {len(common)}")
        if only_nuget:
            print(f"\nOnly in NuGet ({len(only_nuget)}):")
            for f in only_nuget:
                print(f"  - {f}")
        if only_unity:
            print(f"\nOnly in Unity ({len(only_unity)}):")
            for f in only_unity:
                print(f"  + {f}")
        if not only_nuget and not only_unity:
            print("\n✓ Perfect parity")

    if only_nuget or only_unity:
        if not args.verbose:
            parts = []
            if only_nuget:
                parts.append(f"{len(only_nuget)} only in NuGet: {', '.join(only_nuget[:5])}")
            if only_unity:
                parts.append(f"{len(only_unity)} only in Unity: {', '.join(only_unity[:5])}")
            print("P/Invoke drift detected — " + "; ".join(parts))
        return 1

    if not args.verbose:
        print(f"P/Invoke parity OK ({len(nuget)} functions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
