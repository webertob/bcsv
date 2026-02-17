#!/usr/bin/env python3
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


TYPE_ROWS = {
    "MACRO-SMALL": 10_000,
    "MACRO-LARGE": 500_000,
}

MACRO_FILE_STEMS = {
    "MACRO-SMALL": "macro_small",
    "MACRO-LARGE": "macro_large",
}


def run(cmd, cwd=None):
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result.stdout.strip()


def parse_types(value: str) -> list[str]:
    items = [item.strip().upper() for item in value.split(",") if item.strip()]
    allowed = {"MICRO", "MACRO-SMALL", "MACRO-LARGE"}
    invalid = [item for item in items if item not in allowed]
    if invalid:
        raise ValueError(f"Unsupported type(s): {', '.join(invalid)}")
    if not items:
        raise ValueError("At least one type is required")
    deduped = []
    seen = set()
    for item in items:
        if item not in seen:
            deduped.append(item)
            seen.add(item)
    return deduped


def default_temp_root() -> Path:
    if platform.system().lower().startswith("win"):
        return Path("C:/temp/bcsv")
    return Path("/tmp/bcsv")


def write_json(path: Path, payload):
    path.write_text(json.dumps(payload, indent=2))


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Clone/build/run benchmark at specific git ref")
    parser.add_argument("--git-ref", default="HEAD", help="Git ref to benchmark (default: HEAD)")
    parser.add_argument("--types", default="MICRO,MACRO-SMALL", help="Comma-separated benchmark types")
    parser.add_argument("--temp-root", default=None, help="Temp root (default: /tmp/bcsv or C:/temp/bcsv)")
    parser.add_argument("--results-root", default=None,
                        help="Results root (default: benchmark/results/<hostname>)")
    parser.add_argument("--build-type", default="Release", help="CMake build type")
    parser.add_argument("--pin", default="NONE", help="NONE or CPU<id>")
    parser.add_argument("--repetitions", type=int, default=1, help="Repetitions per selected type (default: 1)")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    host = socket.gethostname()

    types = parse_types(args.types)
    if args.repetitions < 1:
        raise ValueError("--repetitions must be >= 1")
    temp_root = Path(args.temp_root) if args.temp_root else default_temp_root()
    results_root = Path(args.results_root) if args.results_root else (repo_root / "benchmark" / "results" / host)
    if not results_root.is_absolute():
        results_root = repo_root / results_root

    resolved_sha = run(["git", "rev-parse", "--short", args.git_ref], cwd=str(repo_root)).lower()
    clone_dir = temp_root / resolved_sha

    if clone_dir.exists():
        shutil.rmtree(clone_dir)
    clone_dir.parent.mkdir(parents=True, exist_ok=True)

    print(f"[1/5] Clone ref {args.git_ref} -> {clone_dir}")
    run(["git", "clone", "--quiet", "--no-hardlinks", str(repo_root), str(clone_dir)])
    run(["git", "checkout", "--quiet", resolved_sha], cwd=str(clone_dir))

    print("[2/5] Configure + build")
    preset = "ninja-release" if args.build_type.lower() == "release" else "ninja-debug"
    build_preset = f"{preset}-build"
    build_dir = clone_dir / "build" / ("ninja-release" if args.build_type.lower() == "release" else "ninja-debug")

    try:
        run(["cmake", "--preset", preset], cwd=str(clone_dir))
        run([
            "cmake", "--build", "--preset", build_preset,
            "-j", str(os.cpu_count() or 1),
            "--target", "bench_macro_datasets", "bench_micro_types",
        ], cwd=str(clone_dir))
    except RuntimeError:
        cxx_flags = "-O3 -march=native" if args.build_type == "Release" else ""
        run([
            "cmake", "-S", str(clone_dir), "-B", str(build_dir),
            "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={args.build_type}", f"-DCMAKE_CXX_FLAGS={cxx_flags}",
        ])
        run([
            "cmake", "--build", str(build_dir), "-j", str(os.cpu_count() or 1),
            "--target", "bench_macro_datasets", "bench_micro_types",
        ])

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = results_root / resolved_sha / timestamp
    out_dir.mkdir(parents=True, exist_ok=True)

    bin_dir = build_dir / "bin"
    macro_exe = bin_dir / "bench_macro_datasets"
    micro_exe = bin_dir / "bench_micro_types"

    pin_enabled = args.pin.upper() != "NONE"
    pin_cpu = 0
    if pin_enabled:
        token = args.pin.upper()
        if not token.startswith("CPU") or not token[3:].isdigit():
            raise ValueError("--pin must be NONE or CPU<id>")
        pin_cpu = int(token[3:])

    def with_pin(cmd):
        if pin_enabled and shutil.which("taskset"):
            return ["taskset", "-c", str(pin_cpu)] + cmd
        return cmd

    print(f"[3/5] Run benchmarks: {', '.join(types)}")
    run_payloads: dict[str, list[dict]] = {run_type: [] for run_type in types}

    for repetition_idx in range(args.repetitions):
        rep_suffix = "" if args.repetitions == 1 else f" (rep {repetition_idx + 1}/{args.repetitions})"
        print(f"  repetition{rep_suffix}")
        run_output_dir = out_dir if args.repetitions == 1 else out_dir / "repeats" / f"run_{repetition_idx + 1:03d}"
        run_output_dir.mkdir(parents=True, exist_ok=True)

        for run_type in types:
            if run_type == "MICRO":
                micro_cmd = with_pin([
                    str(micro_exe),
                    "--benchmark_format=json",
                    f"--benchmark_out={run_output_dir / 'micro_results.json'}",
                ])
                result = subprocess.run(micro_cmd, capture_output=True, text=True)
                (run_output_dir / "micro_stdout.log").write_text(result.stdout or "")
                (run_output_dir / "micro_stderr.log").write_text(result.stderr or "")
                if result.returncode != 0:
                    raise RuntimeError("MICRO execution failed")
                run_payloads[run_type].append(json.loads((run_output_dir / "micro_results.json").read_text()))
                continue

            stem = MACRO_FILE_STEMS[run_type]
            macro_cmd = with_pin([
                str(macro_exe),
                f"--output={run_output_dir / f'{stem}_results.json'}",
                f"--build-type={args.build_type}",
                f"--rows={TYPE_ROWS[run_type]}",
            ])
            result = subprocess.run(macro_cmd, capture_output=True, text=True)
            (run_output_dir / f"{stem}_stdout.log").write_text(result.stdout or "")
            (run_output_dir / f"{stem}_stderr.log").write_text(result.stderr or "")
            if result.returncode != 0:
                raise RuntimeError(f"{run_type} execution failed")
            run_payloads[run_type].append(json.loads((run_output_dir / f"{stem}_results.json").read_text()))

    aggregated_macro_payloads: dict[str, dict] = {}
    for run_type in types:
        if run_type == "MICRO":
            payload = aggregate_micro_payloads(run_payloads[run_type]) if args.repetitions > 1 else run_payloads[run_type][0]
            write_json(out_dir / "micro_results.json", payload)
            continue

        payload = aggregate_macro_payloads(run_payloads[run_type], run_type) if args.repetitions > 1 else run_payloads[run_type][0]
        aggregated_macro_payloads[run_type] = payload
        stem = MACRO_FILE_STEMS[run_type]
        write_json(out_dir / f"{stem}_results.json", payload)

    selected_macro_types = [run_type for run_type in types if run_type in MACRO_FILE_STEMS]
    if len(selected_macro_types) == 1:
        write_json(out_dir / "macro_results.json", aggregated_macro_payloads[selected_macro_types[0]])
    elif len(selected_macro_types) > 1:
        merged = merge_macro_payloads({run_type: aggregated_macro_payloads[run_type] for run_type in selected_macro_types})
        if merged is not None:
            write_json(out_dir / "macro_results.json", merged)

    print("[4/5] Write platform/manifest")
    platform_payload = {
        "hostname": host,
        "os": f"{platform.system()} {platform.release()}",
        "architecture": platform.machine(),
        "python_version": platform.python_version(),
        "build_type": args.build_type,
        "timestamp": datetime.now().isoformat(),
        "git_label": resolved_sha,
        "run_types": types,
        "repetitions": args.repetitions,
        "cpus": 1,
        "pin": args.pin,
        "pin_effective": pin_enabled and shutil.which("taskset") is not None,
        "pin_cpu": pin_cpu if pin_enabled else None,
    }
    write_json(out_dir / "platform.json", platform_payload)

    manifest_payload = {
        "git_ref": args.git_ref,
        "resolved_sha": resolved_sha,
        "clone_dir": str(clone_dir),
        "types": types,
        "repetitions": args.repetitions,
        "output_dir": str(out_dir),
    }
    write_json(out_dir / "manifest.json", manifest_payload)

    print("[5/5] Generate summary report")
    report_script = repo_root / "benchmark" / "report.py"
    run([sys.executable, str(report_script), str(out_dir), "--summary-only"], cwd=str(repo_root))

    print(f"Output: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())