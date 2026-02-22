"""Project and run-directory discovery utilities."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path


def project_root(anchor: Path | None = None) -> Path:
    """Walk upward from *anchor* (default: this file) to find the repo root."""
    start = (anchor or Path(__file__)).resolve()
    for parent in [start] + list(start.parents):
        if (parent / "CMakeLists.txt").exists() and (parent / "include").is_dir():
            return parent
    raise RuntimeError(f"Cannot find project root from {start}")


# ------------------------------------------------------------------
# Build-dir discovery
# ------------------------------------------------------------------

_BUILD_DIR_CANDIDATES = [
    "build/ninja-release",
    "build/ninja-debug",
    "build",
    "build_release",
]


def discover_build_dir(root: Path) -> Path:
    """Return the first build directory that contains a ``bin/`` folder."""
    for rel in _BUILD_DIR_CANDIDATES:
        candidate = root / rel
        if (candidate / "bin").exists():
            return candidate
    return root / "build" / "ninja-release"


def discover_executables(build_dir: Path) -> dict[str, Path]:
    """Map executable basenames → absolute paths inside *build_dir*/bin."""
    bin_dir = build_dir / "bin"
    out: dict[str, Path] = {}
    for name in ("bench_macro_datasets", "bench_micro_types"):
        path = bin_dir / name
        if path.exists() and os.access(path, os.X_OK):
            out[name] = path
    return out


# ------------------------------------------------------------------
# Git helpers
# ------------------------------------------------------------------

def resolve_git_label(root: Path, git_arg: str = "WIP") -> str:
    """Derive a short git label suitable for directory naming.

    * Explicit non-WIP arg → pass through (lower-cased).
    * WIP + dirty tree     → ``"wip"``.
    * WIP + clean tree     → short SHA of HEAD.
    """
    arg = (git_arg or "WIP").strip()
    if arg.upper() != "WIP":
        return arg.lower()

    try:
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=str(root), capture_output=True, text=True, timeout=5,
        )
        if status.returncode == 0 and status.stdout.strip():
            return "wip"

        rev = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(root), capture_output=True, text=True, timeout=5,
        )
        if rev.returncode == 0 and rev.stdout.strip():
            return rev.stdout.strip().lower()
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    return "wip"


# ------------------------------------------------------------------
# Output-dir helpers
# ------------------------------------------------------------------

def ensure_output_dir(root: Path, results_arg: str | None, git_label: str) -> Path:
    """Create and return a timestamped run directory under *results_arg*."""
    import socket
    from datetime import datetime

    if results_arg:
        base = Path(results_arg)
        if not base.is_absolute():
            parts = base.parts
            if parts and parts[0] == "benchmark":
                base = root / base
            else:
                base = root / "benchmark" / base
    else:
        base = root / "benchmark" / "results" / socket.gethostname() / git_label

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = base / timestamp
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


# ------------------------------------------------------------------
# Baseline discovery
# ------------------------------------------------------------------

def latest_clean_run(
    current_run: Path,
    required_macro_types: set[str] | None = None,
    require_micro: bool = False,
) -> Path | None:
    """Find the most recent non-WIP run under the same host root.

    The search prefers exact type-coverage matches, then falls back to
    best-overlap by modification time.
    """
    host_root = current_run.parent.parent
    if not host_root.exists():
        return None

    required_macro_types = required_macro_types or set()

    candidates: list[dict] = []
    for git_bucket in host_root.iterdir():
        if not git_bucket.is_dir():
            continue
        if "wip" in git_bucket.name.lower():
            continue
        for run_dir in git_bucket.iterdir():
            if not run_dir.is_dir() or run_dir == current_run:
                continue
            if not (run_dir / "platform.json").exists():
                continue

            # Probe available data without importing report-layer loaders
            macro_types_found: set[str] = set()
            for stem, rtype in (
                ("macro_small_results.json", "MACRO-SMALL"),
                ("macro_large_results.json", "MACRO-LARGE"),
                ("macro_results.json", "MACRO"),
            ):
                if (run_dir / stem).exists():
                    macro_types_found.add(rtype)

            has_micro = (run_dir / "micro_results.json").exists()

            if not macro_types_found and not has_micro:
                continue

            macro_overlap = len(required_macro_types & macro_types_found)
            missing_macro = len(required_macro_types - macro_types_found)
            missing_micro = 1 if (require_micro and not has_micro) else 0
            exact = missing_macro == 0 and missing_micro == 0

            candidates.append({
                "run_dir": run_dir,
                "mtime": run_dir.stat().st_mtime,
                "exact_match": exact,
                "missing_total": missing_macro + missing_micro,
                "macro_overlap": macro_overlap,
            })

    if not candidates:
        return None

    candidates.sort(
        key=lambda c: (c["exact_match"], -c["missing_total"], c["macro_overlap"], c["mtime"]),
        reverse=True,
    )
    return candidates[0]["run_dir"]
