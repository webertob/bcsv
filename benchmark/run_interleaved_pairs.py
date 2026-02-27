#!/usr/bin/env python3
"""
Run true interleaved benchmark pairs head-to-head:
for repetition in N:
    for run_type in selected_types:
        run baseline and candidate in parallel

Outputs pair directories compatible with benchmark/interleaved_compare.py.
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lib.constants import TYPE_ROWS  # noqa: E402
from lib.runner import parse_types   # noqa: E402


def _run_parallel_pair(
    baseline_cmd: list[str],
    candidate_cmd: list[str],
    baseline_stdout: Path,
    baseline_stderr: Path,
    candidate_stdout: Path,
    candidate_stderr: Path,
) -> tuple[int, int]:
    baseline_stdout.parent.mkdir(parents=True, exist_ok=True)
    candidate_stdout.parent.mkdir(parents=True, exist_ok=True)

    with baseline_stdout.open("w", encoding="utf-8") as b_out, \
         baseline_stderr.open("w", encoding="utf-8") as b_err, \
         candidate_stdout.open("w", encoding="utf-8") as c_out, \
         candidate_stderr.open("w", encoding="utf-8") as c_err:

        p_base = subprocess.Popen(baseline_cmd, stdout=b_out, stderr=b_err)
        p_cand = subprocess.Popen(candidate_cmd, stdout=c_out, stderr=c_err)

        base_rc = p_base.wait()
        cand_rc = p_cand.wait()

    return base_rc, cand_rc


def _require_output_or_success(code: int, output_file: Path, label: str) -> None:
    if output_file.exists() and output_file.stat().st_size > 0:
        if code != 0:
            print(f"  WARNING: {label} exited with code {code} but produced output; continuing.",
                  file=sys.stderr)
        return
    raise RuntimeError(f"{label} failed with code {code} and no usable output file: {output_file}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run interleaved benchmark pairs head-to-head")
    parser.add_argument("--baseline-bin", required=True, help="Baseline bin directory containing bench executables")
    parser.add_argument("--candidate-bin", required=True, help="Candidate/WIP bin directory containing bench executables")
    parser.add_argument("--baseline-label", required=True, help="Baseline label (e.g. git-46662e9-clean)")
    parser.add_argument("--candidate-label", default="WIP", help="Candidate label (default: WIP)")
    parser.add_argument("--types", default="MICRO,MACRO-SMALL", help="Comma-separated types")
    parser.add_argument("--repetitions", type=int, default=5, help="Interleaved pair count")
    parser.add_argument("--results-root", default=None, help="Base result root (default: benchmark/results/<host>/interleaved_h2h)")
    parser.add_argument("--build-type", default="Release", help="Build type for macro benchmark arg")
    parser.add_argument("--baseline-compression", type=int, default=None,
                        help="LZ4 compression level 1-9 for baseline (omit to use binary default)")
    parser.add_argument("--candidate-compression", type=int, default=None,
                        help="LZ4 compression level 1-9 for candidate (omit to use binary default)")
    args = parser.parse_args()

    if args.repetitions < 1:
        raise ValueError("--repetitions must be >= 1")

    run_types = parse_types(args.types)

    repo_root = Path(__file__).resolve().parent.parent
    host = socket.gethostname()
    root = Path(args.results_root) if args.results_root else (repo_root / "benchmark" / "results" / host / "interleaved_h2h")
    if not root.is_absolute():
        root = repo_root / root

    baseline_root = root / args.baseline_label
    candidate_root = root / args.candidate_label

    if root.exists():
        shutil.rmtree(root)
    baseline_root.mkdir(parents=True, exist_ok=True)
    candidate_root.mkdir(parents=True, exist_ok=True)

    baseline_bin = Path(args.baseline_bin)
    candidate_bin = Path(args.candidate_bin)
    baseline_macro = baseline_bin / "bench_macro_datasets"
    baseline_micro = baseline_bin / "bench_micro_types"
    candidate_macro = candidate_bin / "bench_macro_datasets"
    candidate_micro = candidate_bin / "bench_micro_types"

    for exe in [baseline_macro, baseline_micro, candidate_macro, candidate_micro]:
        if not exe.exists() or not os.access(exe, os.X_OK):
            raise FileNotFoundError(f"Executable not found or not executable: {exe}")

    print(f"Interleaved head-to-head run: baseline={args.baseline_label}, candidate={args.candidate_label}")
    print(f"Run types: {', '.join(run_types)}; repetitions={args.repetitions}")

    for rep in range(1, args.repetitions + 1):
        pair_name = f"pair{rep:02d}"
        baseline_pair = baseline_root / pair_name
        candidate_pair = candidate_root / pair_name
        baseline_pair.mkdir(parents=True, exist_ok=True)
        candidate_pair.mkdir(parents=True, exist_ok=True)

        print(f"=== Pair {rep}/{args.repetitions}: {pair_name} ===")

        for run_type in run_types:
            if run_type == "MICRO":
                print("  - MICRO (parallel)")
                base_out = baseline_pair / "micro_results.json"
                cand_out = candidate_pair / "micro_results.json"
                base_cmd = [str(baseline_micro), "--benchmark_format=json", f"--benchmark_out={base_out}"]
                cand_cmd = [str(candidate_micro), "--benchmark_format=json", f"--benchmark_out={cand_out}"]
                base_rc, cand_rc = _run_parallel_pair(
                    base_cmd,
                    cand_cmd,
                    baseline_pair / "micro_stdout.log",
                    baseline_pair / "micro_stderr.log",
                    candidate_pair / "micro_stdout.log",
                    candidate_pair / "micro_stderr.log",
                )
                _require_output_or_success(base_rc, base_out, f"baseline MICRO {pair_name}")
                _require_output_or_success(cand_rc, cand_out, f"candidate MICRO {pair_name}")
                continue

            stem = "macro_small" if run_type == "MACRO-SMALL" else "macro_large"
            rows = TYPE_ROWS[run_type]
            print(f"  - {run_type} (parallel)")
            base_out = baseline_pair / f"{stem}_results.json"
            cand_out = candidate_pair / f"{stem}_results.json"
            base_cmd = [
                str(baseline_macro),
                f"--output={base_out}",
                f"--build-type={args.build_type}",
                f"--rows={rows}",
            ]
            if args.baseline_compression is not None:
                base_cmd.append(f"--compression={args.baseline_compression}")
            cand_cmd = [
                str(candidate_macro),
                f"--output={cand_out}",
                f"--build-type={args.build_type}",
                f"--rows={rows}",
            ]
            if args.candidate_compression is not None:
                cand_cmd.append(f"--compression={args.candidate_compression}")
            base_rc, cand_rc = _run_parallel_pair(
                base_cmd,
                cand_cmd,
                baseline_pair / f"{stem}_stdout.log",
                baseline_pair / f"{stem}_stderr.log",
                candidate_pair / f"{stem}_stdout.log",
                candidate_pair / f"{stem}_stderr.log",
            )
            _require_output_or_success(base_rc, base_out, f"baseline {run_type} {pair_name}")
            _require_output_or_success(cand_rc, cand_out, f"candidate {run_type} {pair_name}")

    print("Completed interleaved head-to-head benchmark pairs")
    print(f"Baseline root: {baseline_root}")
    print(f"Candidate root: {candidate_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
