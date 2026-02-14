#!/usr/bin/env python3
"""
Prepare an isolated git worktree for historical benchmark runs.

Workflow:
  1. Create a detached worktree at target commit under /tmp
  2. Overlay benchmark harness paths from a reference ref (default: HEAD)
  3. Return metadata as JSON for orchestration scripts

This script never mutates the active workspace checkout.
"""

import argparse
import json
import shutil
import subprocess
import tempfile
from datetime import datetime
from pathlib import Path


DEFAULT_OVERLAY_PATHS = [
    "benchmark",
    "tests/bench_common.hpp",
    "tests/bench_datasets.hpp",
    "tests/bench_macro_datasets.cpp",
    "tests/bench_micro_types.cpp",
    "tests/bench_generate_csv.cpp",
    "tests/bench_external_csv.cpp",
    "tests/CMakeLists.txt",
    "examples/csv2bcsv.cpp",
    "examples/bcsv2csv.cpp",
    "examples/CMakeLists.txt",
]


def run_git(project_root: Path, args: list[str], check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(project_root), *args],
        capture_output=True,
        text=True,
        check=check,
    )


def resolve_ref(project_root: Path, ref: str) -> str:
    check = run_git(project_root, ["cat-file", "-e", f"{ref}^{{commit}}"], check=False)
    if check.returncode != 0:
        raise RuntimeError(f"Unknown git ref: {ref}")
    rev = run_git(project_root, ["rev-parse", ref]).stdout.strip()
    if not rev:
        raise RuntimeError(f"Unable to resolve ref: {ref}")
    return rev


def list_existing_worktrees(project_root: Path) -> set[Path]:
    proc = run_git(project_root, ["worktree", "list", "--porcelain"])
    paths: set[Path] = set()
    for line in proc.stdout.splitlines():
        if line.startswith("worktree "):
            p = Path(line[len("worktree "):].strip())
            paths.add(p)
    return paths


def remove_worktree(project_root: Path, worktree_path: Path) -> None:
    run_git(project_root, ["worktree", "remove", "--force", str(worktree_path)], check=False)
    if worktree_path.exists():
        shutil.rmtree(worktree_path, ignore_errors=True)


def prune_old_worktrees(project_root: Path, sandbox_root: Path, keep: int) -> None:
    sandbox_root.mkdir(parents=True, exist_ok=True)
    tracked = list_existing_worktrees(project_root)
    sandboxes = sorted(
        [d for d in sandbox_root.iterdir() if d.is_dir() and d in tracked],
        key=lambda d: d.stat().st_mtime,
        reverse=True,
    )
    for old in sandboxes[keep:]:
        remove_worktree(project_root, old)
    run_git(project_root, ["worktree", "prune"], check=False)


def create_worktree(project_root: Path, sandbox_root: Path, commit_sha: str) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    short = commit_sha[:12]
    worktree_dir = sandbox_root / f"{stamp}_{short}"
    run_git(project_root, ["worktree", "add", "--detach", str(worktree_dir), commit_sha])
    return worktree_dir


def overlay_paths_from_ref(worktree_dir: Path, bench_ref: str, paths: list[str]) -> list[str]:
    existing_paths = list(paths)
    if not existing_paths:
        return []

    cmd = ["git", "-C", str(worktree_dir), "checkout", bench_ref, "--", *existing_paths]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            "Failed to overlay benchmark harness paths from ref "
            f"{bench_ref}: {proc.stderr.strip()}"
        )
    return existing_paths


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare isolated benchmark worktree")
    parser.add_argument("--project-root", default=None,
                        help="Project root containing .git and CMakeLists.txt")
    parser.add_argument("--git-commit", required=True,
                        help="Target commit/tag/branch to benchmark")
    parser.add_argument("--bench-ref", default="HEAD",
                        help="Ref to source benchmark harness files from (default: HEAD)")
    parser.add_argument("--sandbox-root", default=str(Path(tempfile.gettempdir()) / "bcsv_bench_worktrees"),
                        help="Directory for temporary benchmark worktrees")
    parser.add_argument("--keep", type=int, default=5,
                        help="Keep last N worktrees in sandbox root")
    parser.add_argument("--overlay-path", action="append", default=[],
                        help="Additional path to overlay from --bench-ref")

    args = parser.parse_args()

    if args.keep < 1:
        raise RuntimeError("--keep must be >= 1")

    if args.project_root:
        project_root = Path(args.project_root).resolve()
    else:
        project_root = Path(__file__).resolve().parent.parent

    if not (project_root / ".git").exists() and not (project_root / "CMakeLists.txt").exists():
        raise RuntimeError(f"Not a project root: {project_root}")

    sandbox_root = Path(args.sandbox_root).resolve()
    sandbox_root.mkdir(parents=True, exist_ok=True)

    target_sha = resolve_ref(project_root, args.git_commit)
    bench_sha = resolve_ref(project_root, args.bench_ref)

    prune_old_worktrees(project_root, sandbox_root, args.keep)
    worktree_dir = create_worktree(project_root, sandbox_root, target_sha)

    overlay_paths = list(dict.fromkeys(DEFAULT_OVERLAY_PATHS + args.overlay_path))
    applied_overlay_paths = overlay_paths_from_ref(worktree_dir, bench_sha, overlay_paths)

    payload = {
        "project_root": str(project_root),
        "sandbox_root": str(sandbox_root),
        "sandbox_dir": str(worktree_dir),
        "target_ref": args.git_commit,
        "target_sha": target_sha,
        "bench_ref": args.bench_ref,
        "bench_sha": bench_sha,
        "overlay_paths": applied_overlay_paths,
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
