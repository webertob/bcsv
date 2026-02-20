#!/usr/bin/env python3
"""
BCSV Benchmark Orchestrator (simplified)

Run types:
- MICRO:       Google Benchmark micro suite
- MACRO-SMALL: Macro dataset benchmark with 10k rows
- MACRO-LARGE: Macro dataset benchmark with 500k rows

Examples:
    python3 benchmark/run_benchmarks.py
    python3 benchmark/run_benchmarks.py --type=MICRO --pin=CPU2
    python3 benchmark/run_benchmarks.py --type=MICRO,MACRO-SMALL,MACRO-LARGE
"""

import argparse
import json
import os
import platform
import shutil
import socket
import statistics
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from operator_summary import print_operator_summary


TYPE_ROWS = {
    "MACRO-SMALL": 10_000,
    "MACRO-LARGE": 500_000,
}

MACRO_FILE_STEMS = {
    "MACRO-SMALL": "macro_small",
    "MACRO-LARGE": "macro_large",
}

def project_root() -> Path:
    root = Path(__file__).resolve().parent.parent
    if not (root / "CMakeLists.txt").exists():
        raise RuntimeError(f"Cannot find project root from {__file__}")
    return root


def discover_build_dir(root: Path) -> Path:
    candidates = [
        root / "build" / "ninja-release",
        root / "build" / "ninja-debug",
        root / "build",
        root / "build_release",
    ]
    for candidate in candidates:
        if (candidate / "bin").exists():
            return candidate
    return root / "build" / "ninja-release"


def discover_executables(build_dir: Path) -> dict:
    bin_dir = build_dir / "bin"
    executables = {}
    for name in ["bench_macro_datasets", "bench_micro_types"]:
        path = bin_dir / name
        if path.exists() and os.access(path, os.X_OK):
            executables[name] = path
    return executables


def parse_types(value: str) -> list[str]:
    items = [item.strip().upper() for item in value.split(",") if item.strip()]
    if not items:
        raise ValueError("--type must include at least one type")
    allowed = {"MICRO", "MACRO-SMALL", "MACRO-LARGE"}
    invalid = [item for item in items if item not in allowed]
    if invalid:
        raise ValueError(f"Unsupported type(s): {', '.join(invalid)}")
    seen = set()
    ordered = []
    for item in items:
        if item not in seen:
            ordered.append(item)
            seen.add(item)
    return ordered


def parse_pin(value: str):
    token = value.strip().upper()
    if token == "NONE":
        return False, 0
    if token.startswith("CPU"):
        cpu_token = token[3:]
        if cpu_token.isdigit():
            return True, int(cpu_token)
    raise ValueError("--pin must be NONE or CPU<id> (e.g. CPU2)")


def pin_cmd(cmd: list[str], pin_enabled: bool, pin_cpu: int) -> list[str]:
    if not pin_enabled:
        return cmd
    if shutil.which("taskset"):
        return ["taskset", "-c", str(pin_cpu)] + cmd
    return cmd


def ensure_output_dir(root: Path, results_arg: str, git_label: str) -> Path:
    default_base = root / "benchmark" / "results" / socket.gethostname() / git_label
    base = Path(results_arg) if results_arg else default_base
    if not base.is_absolute():
        base = root / "benchmark" / base

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = base / timestamp
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir


def resolve_git_label(root: Path, git_arg: str) -> str:
    arg = (git_arg or "WIP").strip()
    if not arg:
        arg = "WIP"

    if arg.upper() != "WIP":
        return arg.lower()

    try:
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=str(root),
            capture_output=True,
            text=True,
            timeout=5,
        )
        if status.returncode == 0 and status.stdout.strip():
            return "wip"

        rev = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(root),
            capture_output=True,
            text=True,
            timeout=5,
        )
        if rev.returncode == 0:
            value = rev.stdout.strip().lower()
            if value:
                return value
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    return "wip"


def write_json(path: Path, payload) -> None:
    path.write_text(json.dumps(payload, indent=2))


def read_json(path: Path):
    return json.loads(path.read_text())


def median_value(values):
    med = statistics.median(values)
    if all(isinstance(value, int) and not isinstance(value, bool) for value in values):
        return int(round(med))
    return float(med)


def aggregate_dicts(samples: list[dict]) -> dict:
    if not samples:
        return {}

    merged = dict(samples[0])
    all_keys = set()
    for sample in samples:
        all_keys.update(sample.keys())

    for key in all_keys:
        values = [sample.get(key) for sample in samples if key in sample]
        if len(values) != len(samples):
            continue

        if all(isinstance(value, bool) for value in values):
            merged[key] = all(values)
        elif all(isinstance(value, (int, float)) and not isinstance(value, bool) for value in values):
            merged[key] = median_value(values)
        else:
            merged[key] = values[0]

    return merged


def aggregate_macro_payloads(payloads: list[dict], run_type: str) -> dict | None:
    if not payloads:
        return None

    result_map: dict[tuple, list[dict]] = {}
    for payload in payloads:
        for row in payload.get("results", []):
            key = (
                row.get("dataset", "?"),
                row.get("mode", "?"),
                row.get("scenario_id", "baseline"),
                row.get("access_path", "-"),
                row.get("selected_columns", row.get("num_columns", 0)),
            )
            result_map.setdefault(key, []).append(row)

    merged_rows = []
    for key in sorted(result_map.keys()):
        merged_rows.append(aggregate_dicts(result_map[key]))

    out = dict(payloads[0])
    totals = [payload.get("total_time_sec") for payload in payloads if isinstance(payload.get("total_time_sec"), (int, float))]
    if totals:
        out["total_time_sec"] = float(statistics.median(totals))

    out["run_type"] = run_type
    out["results"] = merged_rows
    out["aggregation"] = {
        "method": "median",
        "repetitions": len(payloads),
    }
    return out


def aggregate_micro_payloads(payloads: list[dict]) -> dict | None:
    if not payloads:
        return None

    benchmark_map: dict[str, list[dict]] = {}
    for payload in payloads:
        for benchmark in payload.get("benchmarks", []):
            key = benchmark.get("name", "?")
            benchmark_map.setdefault(key, []).append(benchmark)

    merged_benchmarks = []
    for key in sorted(benchmark_map.keys()):
        merged_benchmarks.append(aggregate_dicts(benchmark_map[key]))

    out = dict(payloads[0])
    out["benchmarks"] = merged_benchmarks
    out["aggregation"] = {
        "method": "median",
        "repetitions": len(payloads),
    }
    return out


def merge_macro_payloads(payloads_by_type: dict[str, dict]) -> dict | None:
    if not payloads_by_type:
        return None

    merged_results = []
    for run_type in ["MACRO-SMALL", "MACRO-LARGE"]:
        payload = payloads_by_type.get(run_type)
        if not payload:
            continue
        for row in payload.get("results", []):
            row_with_type = dict(row)
            row_with_type["run_type"] = run_type
            merged_results.append(row_with_type)

    base_payload = dict(next(iter(payloads_by_type.values())))
    totals = [payload.get("total_time_sec") for payload in payloads_by_type.values() if isinstance(payload.get("total_time_sec"), (int, float))]
    if totals:
        base_payload["total_time_sec"] = float(sum(totals))

    base_payload["run_types"] = [run_type for run_type in ["MACRO-SMALL", "MACRO-LARGE"] if run_type in payloads_by_type]
    base_payload["results"] = merged_results
    return base_payload


def run_build(root: Path, build_dir: Path, build_type: str) -> None:
    preset = "ninja-release" if build_type.lower() == "release" else "ninja-debug"
    build_preset = f"{preset}-build"

    try:
        subprocess.run(
            ["cmake", "--preset", preset],
            cwd=str(root),
            check=True,
            capture_output=True,
            text=True,
        )
        subprocess.run(
            [
                "cmake", "--build", "--preset", build_preset,
                "-j", str(os.cpu_count() or 1),
                "--target", "bench_macro_datasets", "bench_micro_types",
            ],
            cwd=str(root),
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError:
        cxx_flags = "-O3 -march=native" if build_type == "Release" else ""
        configure_cmd = [
            "cmake", "-S", str(root), "-B", str(build_dir),
            "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={build_type}",
            f"-DCMAKE_CXX_FLAGS={cxx_flags}",
        ]
        subprocess.run(configure_cmd, check=True, capture_output=True, text=True)

        build_cmd = [
            "cmake", "--build", str(build_dir), "-j", str(os.cpu_count() or 1),
            "--target", "bench_macro_datasets", "bench_micro_types",
        ]
        subprocess.run(build_cmd, check=True, capture_output=True, text=True)


def write_platform_json(out_dir: Path, build_type: str, git_label: str,
                        run_types: list[str], repetitions: int, cpus: int,
                        pin_value: str, pin_effective: bool, pin_cpu: int | None):
    info = {
        "hostname": socket.gethostname(),
        "os": f"{platform.system()} {platform.release()}",
        "architecture": platform.machine(),
        "python_version": platform.python_version(),
        "build_type": build_type,
        "timestamp": datetime.now().isoformat(),
        "git_label": git_label,
        "run_types": run_types,
        "repetitions": repetitions,
        "cpus": cpus,
        "pin": pin_value,
        "pin_effective": pin_effective,
        "pin_cpu": pin_cpu,
    }

    try:
        with open("/proc/cpuinfo", "r", encoding="utf-8") as handle:
            for line in handle:
                if "model name" in line:
                    info["cpu_model"] = line.split(":", 1)[1].strip()
                    break
    except (FileNotFoundError, PermissionError):
        info["cpu_model"] = platform.processor() or "unknown"

    write_json(out_dir / "platform.json", info)


def run_macro(executable: Path, out_dir: Path, run_type: str, rows: int,
              build_type: str, pin_enabled: bool, pin_cpu: int,
              timeout: int = 3600):
    stem = MACRO_FILE_STEMS[run_type]
    output_file = out_dir / f"{stem}_results.json"
    stdout_log = out_dir / f"{stem}_stdout.log"
    stderr_log = out_dir / f"{stem}_stderr.log"

    cmd = [
        str(executable),
        f"--output={output_file}",
        f"--build-type={build_type}",
        f"--rows={rows}",
    ]
    cmd = pin_cmd(cmd, pin_enabled, pin_cpu)

    result = subprocess.run(cmd, timeout=timeout, capture_output=True, text=True)
    stdout_log.write_text(result.stdout or "")
    stderr_log.write_text(result.stderr or "")

    if result.returncode != 0:
        raise RuntimeError(f"Macro benchmark failed with exit code {result.returncode}")

    if not output_file.exists():
        raise RuntimeError(f"{run_type} benchmark completed without {output_file.name}")

    return read_json(output_file)


def run_micro(executable: Path, out_dir: Path,
              pin_enabled: bool, pin_cpu: int, timeout: int = 900):
    output_file = out_dir / "micro_results.json"
    stdout_log = out_dir / "micro_stdout.log"
    stderr_log = out_dir / "micro_stderr.log"

    cmd = [
        str(executable),
        "--benchmark_format=json",
        f"--benchmark_out={output_file}",
    ]
    cmd = pin_cmd(cmd, pin_enabled, pin_cpu)

    result = subprocess.run(cmd, timeout=timeout, capture_output=True, text=True)
    stdout_log.write_text(result.stdout or "")
    stderr_log.write_text(result.stderr or "")

    if result.returncode != 0:
        raise RuntimeError(f"Micro benchmark failed with exit code {result.returncode}")

    if not output_file.exists():
        raise RuntimeError("Micro benchmark completed without micro_results.json")

    return read_json(output_file)


def latest_clean_run(current_run: Path) -> Path | None:
    host_root = current_run.parent.parent
    if not host_root.exists():
        return None

    candidates = []
    for git_bucket in host_root.iterdir():
        if not git_bucket.is_dir():
            continue
        label = git_bucket.name.lower()
        if "wip" in label:
            continue
        for run_dir in git_bucket.iterdir():
            if not run_dir.is_dir() or run_dir == current_run:
                continue
            platform_json = run_dir / "platform.json"
            macro_json = run_dir / "macro_results.json"
            micro_json = run_dir / "micro_results.json"
            if platform_json.exists() and (macro_json.exists() or micro_json.exists()):
                candidates.append((run_dir.stat().st_mtime, run_dir))

    if not candidates:
        return None

    candidates.sort(key=lambda pair: pair[0], reverse=True)
    return candidates[0][1]


def run_report(out_dir: Path, baseline: Path | None, summary_only: bool = True):
    report_script = Path(__file__).resolve().parent / "report.py"
    if not report_script.exists():
        raise RuntimeError("Missing benchmark/report.py")

    cmd = [sys.executable, str(report_script), str(out_dir)]
    if baseline is not None:
        cmd.extend(["--baseline", str(baseline)])
    if summary_only:
        cmd.append("--summary-only")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "report.py failed")


def write_manifest(out_dir: Path, args, run_types: list[str], pin_effective: bool, pin_cpu: int):
    payload = {
        "timestamp": datetime.now().isoformat(),
        "type": run_types,
        "repetitions": args.repetitions,
        "cpus": args.cpus,
        "pin": args.pin,
        "pin_effective": pin_effective,
        "pin_cpu": pin_cpu if pin_effective else None,
        "git": args.git,
        "results_root_arg": args.results,
        "no_build": args.no_build,
        "no_report": args.no_report,
    }
    write_json(out_dir / "manifest.json", payload)


def create_macro_compat_payload(out_dir: Path, run_types: list[str], payloads_by_type: dict[str, dict]) -> None:
    selected_macro_types = [run_type for run_type in run_types if run_type in MACRO_FILE_STEMS]
    if not selected_macro_types:
        return

    if len(selected_macro_types) == 1:
        run_type = selected_macro_types[0]
        payload = payloads_by_type.get(run_type)
        if payload is not None:
            write_json(out_dir / "macro_results.json", payload)
        return

    merged = merge_macro_payloads({
        run_type: payloads_by_type[run_type]
        for run_type in selected_macro_types
        if run_type in payloads_by_type
    })
    if merged is not None:
        write_json(out_dir / "macro_results.json", merged)


def main() -> int:
    parser = argparse.ArgumentParser(description="BCSV benchmark orchestrator (simplified)")
    parser.add_argument(
        "--type",
        default="MACRO-SMALL",
        help="Comma-separated: MICRO,MACRO-SMALL,MACRO-LARGE (default: MACRO-SMALL)",
    )
    parser.add_argument("--repetitions", type=int, default=1,
                        help="Repetitions per selected type (default: 1)")
    parser.add_argument("--cpus", type=int, default=1,
                        help="Reserved for future parallel workers (default: 1)")
    parser.add_argument("--pin", default="NONE",
                        help="NONE or CPU<id> (example: CPU2)")
    parser.add_argument("--git", default="WIP",
                        help="Logical run label used for output folder naming (default: WIP)")
    parser.add_argument("--results", default=None,
                        help="Result base directory (default: benchmark/results/<host>/<git>)")
    parser.add_argument("--build-type", default="Release",
                        help="CMake build type (default: Release)")
    parser.add_argument("--no-build", action="store_true",
                        help="Skip CMake configure/build")
    parser.add_argument("--no-report", action="store_true",
                        help="Skip report generation")

    args = parser.parse_args()

    if args.repetitions < 1:
        print("ERROR: --repetitions must be >= 1")
        return 1
    if args.cpus < 1:
        print("ERROR: --cpus must be >= 1")
        return 1

    try:
        run_types = parse_types(args.type)
        pin_enabled, pin_cpu = parse_pin(args.pin)
    except ValueError as error:
        print(f"ERROR: {error}")
        return 1

    if args.cpus != 1:
        print("NOTE: --cpus is accepted but execution is currently single-process. Using cpus=1 behavior.")

    root = project_root()
    build_dir = discover_build_dir(root)

    if not args.no_build:
        print("[1/4] Configure + build benchmark targets")
        run_build(root, build_dir, args.build_type)
    else:
        print("[1/4] Build skipped (--no-build)")

    executables = discover_executables(build_dir)
    if "MICRO" in run_types and "bench_micro_types" not in executables:
        print("ERROR: bench_micro_types not found. Build benchmark targets first.")
        return 1
    if any(item.startswith("MACRO") for item in run_types) and "bench_macro_datasets" not in executables:
        print("ERROR: bench_macro_datasets not found. Build benchmark targets first.")
        return 1

    git_label = resolve_git_label(root, args.git)
    out_dir = ensure_output_dir(root, args.results, git_label)
    pin_effective = pin_enabled and shutil.which("taskset") is not None
    write_platform_json(
        out_dir,
        args.build_type,
        git_label,
        run_types,
        args.repetitions,
        args.cpus,
        args.pin,
        pin_effective,
        pin_cpu if pin_effective else None,
    )

    print(f"[2/4] Run selected benchmark types: {', '.join(run_types)}")
    run_payloads: dict[str, list[dict]] = {run_type: [] for run_type in run_types}

    for run_type in run_types:
        for repetition_idx in range(args.repetitions):
            rep_suffix = "" if args.repetitions == 1 else f" (rep {repetition_idx + 1}/{args.repetitions})"
            print(f"  - {run_type}{rep_suffix}")

            run_output_dir = out_dir if args.repetitions == 1 else out_dir / "repeats" / f"run_{repetition_idx + 1:03d}"
            run_output_dir.mkdir(parents=True, exist_ok=True)

            if run_type == "MICRO":
                payload = run_micro(executables["bench_micro_types"], run_output_dir, pin_enabled, pin_cpu)
                run_payloads[run_type].append(payload)

            else:
                rows = TYPE_ROWS[run_type]
                payload = run_macro(
                    executables["bench_macro_datasets"],
                    run_output_dir,
                    run_type,
                    rows,
                    args.build_type,
                    pin_enabled,
                    pin_cpu,
                )
                run_payloads[run_type].append(payload)

    aggregated_macro_payloads: dict[str, dict] = {}

    for run_type in run_types:
        if run_type == "MICRO":
            payloads = run_payloads[run_type]
            if not payloads:
                continue
            aggregated = aggregate_micro_payloads(payloads) if args.repetitions > 1 else payloads[0]
            if aggregated is not None:
                write_json(out_dir / "micro_results.json", aggregated)
            continue

        payloads = run_payloads[run_type]
        if not payloads:
            continue
        aggregated = aggregate_macro_payloads(payloads, run_type) if args.repetitions > 1 else payloads[0]
        if aggregated is not None:
            aggregated_macro_payloads[run_type] = aggregated
            stem = MACRO_FILE_STEMS[run_type]
            write_json(out_dir / f"{stem}_results.json", aggregated)

    create_macro_compat_payload(out_dir, run_types, aggregated_macro_payloads)

    write_manifest(out_dir, args, run_types, pin_effective, pin_cpu)

    print("[3/4] Generate summary report")
    if not args.no_report:
        baseline = latest_clean_run(out_dir)
        run_report(out_dir, baseline, summary_only=True)
    else:
        print("  - skipped (--no-report)")

    print_operator_summary(out_dir, run_types, aggregated_macro_payloads)

    print("[4/4] Completed")
    print(f"Output: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
