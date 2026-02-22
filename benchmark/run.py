#!/usr/bin/env python3
"""
Unified BCSV benchmark CLI.

Subcommands
-----------
wip         Run benchmarks against the current working tree.
baseline    Clone a git ref, build, and run benchmarks into a clean baseline
            directory.
compare     Generate a markdown comparison report between two run directories
            (wraps report.py).
interleaved Run head-to-head interleaved pairs and produce a comparison report
            (wraps run_interleaved_pairs.py + interleaved_compare.py).

Examples
--------
    python3 benchmark/run.py wip --type=MICRO,MACRO-SMALL --reps=3
    python3 benchmark/run.py baseline --git-ref=v1.2.0
    python3 benchmark/run.py compare <run_dir> --baseline <baseline_dir>
    python3 benchmark/run.py interleaved --baseline-bin build_clean/bin --candidate-bin build/ninja-release/bin
"""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# --- shared library imports ------------------------------------------------
sys.path.insert(0, str(Path(__file__).resolve().parent))

from lib.aggregation import (
    aggregate_macro_payloads,
    aggregate_micro_payloads,
    merge_macro_payloads,
    read_json,
    write_json,
)
from lib.constants import LANGUAGE_SIZE_BY_TYPE, MACRO_FILE_STEMS, TYPE_ROWS
from lib.discovery import (
    discover_build_dir,
    discover_executables,
    ensure_output_dir,
    latest_clean_run,
    project_root,
    resolve_git_label,
)
from lib.runner import (
    parse_languages,
    parse_pin,
    parse_types,
    pin_cmd,
    run_build,
    run_macro,
    run_micro,
    write_manifest,
    write_platform_json,
)

# Optional: operator_summary lives next to us (not yet refactored into lib/)
try:
    from operator_summary import print_operator_summary
except ImportError:
    def print_operator_summary(*_a, **_k):  # type: ignore[misc]
        pass


# ======================================================================
# Helpers shared by multiple subcommands
# ======================================================================

def _run_report(out_dir: Path, baseline: Path | None, **kwargs) -> None:
    """Invoke ``report.py`` as a subprocess — keeps the report module isolated."""
    report_script = Path(__file__).resolve().parent / "report.py"
    if not report_script.exists():
        print("WARNING: benchmark/report.py not found, skipping report", file=sys.stderr)
        return

    cmd = [sys.executable, str(report_script), str(out_dir)]
    if baseline is not None:
        cmd.extend(["--baseline", str(baseline)])
    for flag, key in (
        ("--python-json", "python_json"),
        ("--csharp-json", "csharp_json"),
        ("--baseline-python-json", "baseline_python_json"),
        ("--baseline-csharp-json", "baseline_csharp_json"),
    ):
        val = kwargs.get(key)
        if val is not None:
            cmd.extend([flag, str(val)])
    if kwargs.get("summary_only", True):
        cmd.append("--summary-only")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        msg = result.stderr.strip() or result.stdout.strip() or "report.py failed"
        print(f"WARNING: report generation failed — {msg}", file=sys.stderr)


def _run_shell(cmd, cwd=None):
    """Run a command, raise on failure, return stdout."""
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(str(c) for c in cmd)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout.strip()


def _discover_language_json(run_dir: Path, language: str) -> Path | None:
    """Locate the most recent language-lane JSON in *run_dir*."""
    prefix = {"python": "py", "csharp": "cs"}.get(language)
    if prefix is None:
        return None
    patterns = [
        f"{prefix}_macro_results.json",
        f"{prefix}_macro_results_*.json",
        f"{language}/{prefix}_macro_results.json",
        f"{language}/{prefix}_macro_results_*.json",
    ]
    candidates = []
    for pattern in patterns:
        candidates.extend(run_dir.glob(pattern))
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)


def _choose_language_size(run_types: list[str]) -> str:
    if "MACRO-LARGE" in run_types:
        return LANGUAGE_SIZE_BY_TYPE["MACRO-LARGE"]
    if "MACRO-SMALL" in run_types:
        return LANGUAGE_SIZE_BY_TYPE["MACRO-SMALL"]
    return "S"


def _run_python_lane(root: Path, out_dir: Path, size_token: str) -> Path:
    script = root / "python" / "benchmarks" / "run_pybcsv_benchmarks.py"
    if not script.exists():
        raise RuntimeError("Missing python/benchmarks/run_pybcsv_benchmarks.py")
    python_exe = root / ".venv" / "bin" / "python"
    interpreter = str(python_exe) if python_exe.exists() else sys.executable
    target = out_dir / "py_macro_results.json"
    cmd = [interpreter, str(script), f"--size={size_token}", f"--output={target}"]
    result = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True)
    (out_dir / "python_bench_stdout.log").write_text(result.stdout or "")
    (out_dir / "python_bench_stderr.log").write_text(result.stderr or "")
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "Python benchmark lane failed")
    if not target.exists():
        raise RuntimeError("Python benchmark lane completed without py_macro_results.json")
    return target


def _run_csharp_lane(root: Path, out_dir: Path, size_token: str) -> Path:
    project = root / "csharp" / "benchmarks" / "Bcsv.Benchmarks.csproj"
    if not project.exists():
        raise RuntimeError("Missing csharp/benchmarks/Bcsv.Benchmarks.csproj")
    if shutil.which("dotnet") is None:
        raise RuntimeError("dotnet not found in PATH")
    target = out_dir / "cs_macro_results.json"

    framework = None
    try:
        info = subprocess.run(["dotnet", "--list-runtimes"], capture_output=True, text=True, check=False)
        text = info.stdout or ""
        if "Microsoft.NETCore.App 8." in text:
            framework = "net8.0"
        elif "Microsoft.NETCore.App 10." in text:
            framework = "net10.0"
    except Exception:
        pass

    cmd = ["dotnet", "run", "--project", str(project)]
    if framework:
        cmd.extend(["-f", framework])
    cmd.extend(["--", f"--size={size_token}", "--flags=none", f"--output={target}"])

    env = dict(os.environ)
    lib_dirs = [root / "build" / "ninja-release", root / "build" / "ninja-release" / "lib"]
    lib_dir = next((d for d in lib_dirs if d.exists()), None)
    if lib_dir:
        existing = env.get("LD_LIBRARY_PATH", "")
        env["LD_LIBRARY_PATH"] = f"{lib_dir}:{existing}" if existing else str(lib_dir)

    result = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, env=env)
    (out_dir / "csharp_bench_stdout.log").write_text(result.stdout or "")
    (out_dir / "csharp_bench_stderr.log").write_text(result.stderr or "")
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "C# benchmark lane failed")
    if not target.exists():
        raise RuntimeError("C# lane completed without cs_macro_results.json")
    return target


def _create_macro_compat_payload(out_dir, run_types, payloads_by_type):
    selected = [rt for rt in run_types if rt in MACRO_FILE_STEMS]
    if len(selected) == 1:
        p = payloads_by_type.get(selected[0])
        if p:
            write_json(out_dir / "macro_results.json", p)
    elif len(selected) > 1:
        merged = merge_macro_payloads({rt: payloads_by_type[rt] for rt in selected if rt in payloads_by_type})
        if merged:
            write_json(out_dir / "macro_results.json", merged)


# ======================================================================
# Sub-command: wip
# ======================================================================

def cmd_wip(args) -> int:
    """Run benchmarks against the current working-tree build."""
    try:
        run_types = parse_types(args.type)
        pin_enabled, pin_cpu = parse_pin(args.pin)
        language_lanes = parse_languages(args.languages)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    root = project_root()
    build_dir = discover_build_dir(root)

    # ---- build ----
    if not args.no_build:
        print("[1/5] Configure + build benchmark targets")
        run_build(root, build_dir, args.build_type)
    else:
        print("[1/5] Build skipped (--no-build)")

    executables = discover_executables(build_dir)
    if "MICRO" in run_types and "bench_micro_types" not in executables:
        print("ERROR: bench_micro_types not found. Build first.", file=sys.stderr)
        return 1
    if any(t.startswith("MACRO") for t in run_types) and "bench_macro_datasets" not in executables:
        print("ERROR: bench_macro_datasets not found. Build first.", file=sys.stderr)
        return 1

    git_label = resolve_git_label(root, args.git)
    out_dir = ensure_output_dir(root, args.results, git_label)
    pin_effective = pin_enabled and shutil.which("taskset") is not None

    write_platform_json(
        out_dir, args.build_type, git_label, run_types,
        args.repetitions, args.pin, pin_effective,
        pin_cpu if pin_effective else None,
    )

    # ---- benchmark runs ----
    print(f"[2/5] Run selected types: {', '.join(run_types)}")
    run_payloads: dict[str, list[dict]] = {rt: [] for rt in run_types}

    for rep_idx in range(args.repetitions):
        run_out = out_dir if args.repetitions == 1 else out_dir / "repeats" / f"run_{rep_idx+1:03d}"
        run_out.mkdir(parents=True, exist_ok=True)

        for rt in run_types:
            suffix = "" if args.repetitions == 1 else f" (rep {rep_idx+1}/{args.repetitions})"
            print(f"  - {rt}{suffix}")
            if rt == "MICRO":
                payload = run_micro(executables["bench_micro_types"], run_out, pin_enabled, pin_cpu)
            else:
                payload = run_macro(
                    executables["bench_macro_datasets"], run_out, rt,
                    TYPE_ROWS[rt], args.build_type, pin_enabled, pin_cpu,
                    args.macro_profile, args.macro_scenario,
                    args.macro_tracking, args.macro_storage, args.macro_codec,
                )
            run_payloads[rt].append(payload)

    # ---- aggregate ----
    agg_macro: dict[str, dict] = {}
    for rt in run_types:
        payloads = run_payloads[rt]
        if not payloads:
            continue
        if rt == "MICRO":
            agg = aggregate_micro_payloads(payloads) if args.repetitions > 1 else payloads[0]
            if agg:
                write_json(out_dir / "micro_results.json", agg)
        else:
            agg = aggregate_macro_payloads(payloads, rt) if args.repetitions > 1 else payloads[0]
            if agg:
                agg_macro[rt] = agg
                write_json(out_dir / f"{MACRO_FILE_STEMS[rt]}_results.json", agg)

    _create_macro_compat_payload(out_dir, run_types, agg_macro)

    # ---- language lanes ----
    lang_outputs: dict[str, Path] = {}
    if language_lanes:
        print("[3/5] Run language benchmark lanes")
        size_token = _choose_language_size(run_types)
        for lane in language_lanes:
            print(f"  - {lane} (size={size_token})")
            if lane == "python":
                lang_outputs["python"] = _run_python_lane(root, out_dir, size_token)
            elif lane == "csharp":
                lang_outputs["csharp"] = _run_csharp_lane(root, out_dir, size_token)
    else:
        print("[3/5] Language lanes skipped")

    write_manifest(out_dir, args_dict={
        "type": run_types, "repetitions": args.repetitions,
        "pin": args.pin, "pin_effective": pin_effective,
        "pin_cpu": pin_cpu if pin_effective else None,
        "git": args.git, "build_type": args.build_type,
        "languages": args.languages,
        "macro_profile": args.macro_profile,
        "macro_scenario": args.macro_scenario,
        "macro_tracking": args.macro_tracking,
        "macro_storage": args.macro_storage,
        "macro_codec": args.macro_codec,
    })

    # ---- report ----
    print("[4/5] Generate summary report")
    if not args.no_report:
        baseline = latest_clean_run(out_dir)
        bpy = _discover_language_json(baseline, "python") if baseline else None
        bcs = _discover_language_json(baseline, "csharp") if baseline else None
        _run_report(
            out_dir, baseline,
            python_json=lang_outputs.get("python"),
            csharp_json=lang_outputs.get("csharp"),
            baseline_python_json=bpy,
            baseline_csharp_json=bcs,
        )
    else:
        print("  skipped (--no-report)")

    print_operator_summary(out_dir, run_types, agg_macro, detail=args.detail)
    print(f"[5/5] Done — {out_dir}")
    return 0


# ======================================================================
# Sub-command: baseline
# ======================================================================

def cmd_baseline(args) -> int:
    """Clone a git ref into a temp directory, build, and run benchmarks."""
    import platform as plat

    try:
        types = parse_types(args.types)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    if args.repetitions < 1:
        print("ERROR: --repetitions must be >= 1", file=sys.stderr)
        return 1

    repo_root = project_root()
    host = socket.gethostname()

    if plat.system().lower().startswith("win"):
        temp_root = Path(args.temp_root) if args.temp_root else Path("C:/temp/bcsv")
    else:
        temp_root = Path(args.temp_root) if args.temp_root else Path("/tmp/bcsv")

    results_root = Path(args.results_root) if args.results_root else (repo_root / "benchmark" / "results" / host)
    if not results_root.is_absolute():
        results_root = repo_root / results_root

    resolved_sha = _run_shell(["git", "rev-parse", "--short", args.git_ref], cwd=str(repo_root)).lower()
    clone_dir = temp_root / resolved_sha

    if clone_dir.exists():
        shutil.rmtree(clone_dir)
    clone_dir.parent.mkdir(parents=True, exist_ok=True)

    print(f"[1/5] Clone ref {args.git_ref} → {clone_dir}")
    _run_shell(["git", "clone", "--quiet", "--no-hardlinks", str(repo_root), str(clone_dir)])
    _run_shell(["git", "checkout", "--quiet", resolved_sha], cwd=str(clone_dir))

    print("[2/5] Configure + build")
    preset = "ninja-release" if args.build_type.lower() == "release" else "ninja-debug"
    build_preset = f"{preset}-build"
    build_dir = clone_dir / "build" / ("ninja-release" if args.build_type.lower() == "release" else "ninja-debug")

    try:
        _run_shell(["cmake", "--preset", preset], cwd=str(clone_dir))
        _run_shell([
            "cmake", "--build", "--preset", build_preset,
            "-j", str(os.cpu_count() or 1),
            "--target", "bench_macro_datasets", "bench_micro_types",
        ], cwd=str(clone_dir))
    except RuntimeError:
        cxx_flags = "-O3 -march=native" if args.build_type == "Release" else ""
        _run_shell([
            "cmake", "-S", str(clone_dir), "-B", str(build_dir),
            "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={args.build_type}",
            f"-DCMAKE_CXX_FLAGS={cxx_flags}",
        ])
        _run_shell([
            "cmake", "--build", str(build_dir),
            "-j", str(os.cpu_count() or 1),
            "--target", "bench_macro_datasets", "bench_micro_types",
        ])

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = results_root / resolved_sha / timestamp
    out_dir.mkdir(parents=True, exist_ok=True)

    bin_dir = build_dir / "bin"
    macro_exe = bin_dir / "bench_macro_datasets"
    micro_exe = bin_dir / "bench_micro_types"

    pin_enabled, pin_cpu = parse_pin(args.pin)

    print(f"[3/5] Run benchmarks: {', '.join(types)}")
    run_payloads: dict[str, list[dict]] = {rt: [] for rt in types}

    for rep_idx in range(args.repetitions):
        suffix = "" if args.repetitions == 1 else f" (rep {rep_idx+1}/{args.repetitions})"
        print(f"  repetition{suffix}")
        run_out = out_dir if args.repetitions == 1 else out_dir / "repeats" / f"run_{rep_idx+1:03d}"
        run_out.mkdir(parents=True, exist_ok=True)

        for rt in types:
            if rt == "MICRO":
                payload = run_micro(micro_exe, run_out, pin_enabled, pin_cpu)
            else:
                payload = run_macro(
                    macro_exe, run_out, rt, TYPE_ROWS[rt],
                    args.build_type, pin_enabled, pin_cpu,
                )
            run_payloads[rt].append(payload)

    agg_macro: dict[str, dict] = {}
    for rt in types:
        payloads = run_payloads[rt]
        if not payloads:
            continue
        if rt == "MICRO":
            agg = aggregate_micro_payloads(payloads) if args.repetitions > 1 else payloads[0]
            write_json(out_dir / "micro_results.json", agg)
        else:
            agg = aggregate_macro_payloads(payloads, rt) if args.repetitions > 1 else payloads[0]
            agg_macro[rt] = agg
            write_json(out_dir / f"{MACRO_FILE_STEMS[rt]}_results.json", agg)

    _create_macro_compat_payload(out_dir, types, agg_macro)

    print("[4/5] Write platform / manifest")
    import platform as _p
    plat_payload = {
        "hostname": host,
        "os": f"{_p.system()} {_p.release()}",
        "architecture": _p.machine(),
        "python_version": _p.python_version(),
        "build_type": args.build_type,
        "timestamp": datetime.now().isoformat(),
        "git_label": resolved_sha,
        "run_types": types,
        "repetitions": args.repetitions,
        "pin": args.pin,
        "pin_effective": pin_enabled and shutil.which("taskset") is not None,
        "pin_cpu": pin_cpu if pin_enabled else None,
    }
    try:
        with open("/proc/cpuinfo", encoding="utf-8") as f:
            for line in f:
                if "model name" in line:
                    plat_payload["cpu_model"] = line.split(":", 1)[1].strip()
                    break
    except (FileNotFoundError, PermissionError):
        plat_payload["cpu_model"] = _p.processor() or "unknown"

    write_json(out_dir / "platform.json", plat_payload)
    write_json(out_dir / "manifest.json", {
        "git_ref": args.git_ref, "resolved_sha": resolved_sha,
        "clone_dir": str(clone_dir), "types": types,
        "repetitions": args.repetitions, "output_dir": str(out_dir),
    })

    print("[5/5] Generate summary report")
    _run_report(out_dir, baseline=None)
    print_operator_summary(out_dir, types, agg_macro)
    print(f"Output: {out_dir}")
    return 0


# ======================================================================
# Sub-command: compare
# ======================================================================

def cmd_compare(args) -> int:
    """Generate a markdown comparison report between two run directories."""
    run_dir = Path(args.run_dir)
    if not run_dir.exists():
        print(f"ERROR: run directory not found: {run_dir}", file=sys.stderr)
        return 1

    baseline_dir = Path(args.baseline) if args.baseline else None
    if baseline_dir and not baseline_dir.exists():
        print(f"ERROR: baseline not found: {baseline_dir}", file=sys.stderr)
        return 1

    report_script = Path(__file__).resolve().parent / "report.py"
    cmd = [sys.executable, str(report_script), str(run_dir)]
    if baseline_dir:
        cmd.extend(["--baseline", str(baseline_dir)])
    if not args.full:
        cmd.append("--summary-only")

    result = subprocess.run(cmd)
    return result.returncode


# ======================================================================
# Sub-command: interleaved
# ======================================================================

def cmd_interleaved(args) -> int:
    """Run interleaved head-to-head pairs, then produce a comparison report."""
    scripts_dir = Path(__file__).resolve().parent

    # 1. run_interleaved_pairs.py
    pairs_script = scripts_dir / "run_interleaved_pairs.py"
    if not pairs_script.exists():
        print("ERROR: run_interleaved_pairs.py not found", file=sys.stderr)
        return 1

    pairs_cmd = [
        sys.executable, str(pairs_script),
        "--baseline-bin", args.baseline_bin,
        "--candidate-bin", args.candidate_bin,
        "--baseline-label", args.baseline_label,
        "--candidate-label", args.candidate_label,
        "--types", args.types,
        "--repetitions", str(args.repetitions),
        "--build-type", args.build_type,
    ]
    if args.results_root:
        pairs_cmd.extend(["--results-root", args.results_root])

    result = subprocess.run(pairs_cmd)
    if result.returncode != 0:
        return result.returncode

    # 2. interleaved_compare.py
    compare_script = scripts_dir / "interleaved_compare.py"
    if not compare_script.exists():
        print("WARNING: interleaved_compare.py not found, skipping report", file=sys.stderr)
        return 0

    root = project_root()
    host = socket.gethostname()
    default_root = root / "benchmark" / "results" / host / "interleaved_h2h"
    base_root = Path(args.results_root) if args.results_root else default_root

    cmp_cmd = [
        sys.executable, str(compare_script),
        "--baseline-root", str(base_root / args.baseline_label),
        "--candidate-root", str(base_root / args.candidate_label),
        "--run-type", args.types.split(",")[0].strip(),
        "--expected-pairs", str(args.repetitions),
    ]
    result = subprocess.run(cmp_cmd)
    return result.returncode


# ======================================================================
# Argument parser
# ======================================================================

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="benchmark/run.py",
        description="Unified BCSV benchmark CLI",
    )
    sub = parser.add_subparsers(dest="command")

    # ---- wip ----
    p_wip = sub.add_parser("wip", help="Run benchmarks against the current working-tree build")
    p_wip.add_argument("--type", default="MACRO-SMALL",
                        help="Comma-separated: MICRO,MACRO-SMALL,MACRO-LARGE (default: MACRO-SMALL)")
    p_wip.add_argument("--repetitions", type=int, default=1)
    p_wip.add_argument("--pin", default="NONE", help="NONE or CPU<id>")
    p_wip.add_argument("--git", default="WIP", help="Logical run label")
    p_wip.add_argument("--results", default=None, help="Result base directory")
    p_wip.add_argument("--build-type", default="Release")
    p_wip.add_argument("--no-build", action="store_true")
    p_wip.add_argument("--no-report", action="store_true")
    p_wip.add_argument("--languages", default="", help="python,csharp")
    p_wip.add_argument("--macro-profile", default="")
    p_wip.add_argument("--macro-scenario", default="")
    p_wip.add_argument("--macro-tracking", default="both")
    p_wip.add_argument("--macro-storage", default="both")
    p_wip.add_argument("--macro-codec", default="both")
    p_wip.add_argument("--detail", action="store_true",
                        help="Show per-profile breakdown in operator summary")
    p_wip.set_defaults(func=cmd_wip)

    # ---- baseline ----
    p_base = sub.add_parser("baseline", help="Clone a git ref, build, and benchmark")
    p_base.add_argument("--git-ref", default="HEAD", help="Git ref to benchmark")
    p_base.add_argument("--types", default="MICRO,MACRO-SMALL", help="Comma-separated types")
    p_base.add_argument("--temp-root", default=None)
    p_base.add_argument("--results-root", default=None)
    p_base.add_argument("--build-type", default="Release")
    p_base.add_argument("--pin", default="NONE")
    p_base.add_argument("--repetitions", type=int, default=1)
    p_base.set_defaults(func=cmd_baseline)

    # ---- compare ----
    p_cmp = sub.add_parser("compare", help="Generate comparison report between two run dirs")
    p_cmp.add_argument("run_dir", help="Run directory")
    p_cmp.add_argument("--baseline", default=None, help="Baseline run directory")
    p_cmp.add_argument("--full", action="store_true", help="Include detail tables")
    p_cmp.set_defaults(func=cmd_compare)

    # ---- interleaved ----
    p_ilv = sub.add_parser("interleaved", help="Run interleaved head-to-head pairs")
    p_ilv.add_argument("--baseline-bin", required=True)
    p_ilv.add_argument("--candidate-bin", required=True)
    p_ilv.add_argument("--baseline-label", required=True)
    p_ilv.add_argument("--candidate-label", default="WIP")
    p_ilv.add_argument("--types", default="MICRO,MACRO-SMALL")
    p_ilv.add_argument("--repetitions", type=int, default=5)
    p_ilv.add_argument("--results-root", default=None)
    p_ilv.add_argument("--build-type", default="Release")
    p_ilv.set_defaults(func=cmd_interleaved)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.command is None:
        parser.print_help()
        return 0
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
