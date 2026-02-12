#!/usr/bin/env python3
"""
BCSV Benchmark Orchestrator — full 360

By default performs the complete benchmark cycle:
  1. Clean rebuild of all benchmark targets (all cores)
  2. Run full benchmark with warmup and CPU pinning (single core)
  3. Generate Markdown + chart report
  4. Update the leaderboard
  5. Print compressed summary with file paths

Usage:
    python run_benchmarks.py                       # full 360 (default)
    python run_benchmarks.py --size=S              # quick smoke test
    python run_benchmarks.py --no-build --no-report  # just run
    python run_benchmarks.py --profile=mixed_generic --size=L
    python run_benchmarks.py --list                # show available profiles

Options:
    --mode=sweep|full     sweep: reduced rows; full: profile defaults (default: full)
    --build-type=TYPE     CMake build type (default: Release)
    --size=S|M|L|XL       Row count preset: S=10K M=100K L=500K XL=2M
    --rows=N              Override row count directly
    --profile=NAME        Run only this dataset profile
    --output-dir=PATH     Custom output directory
    --no-build            Skip rebuild step
    --no-report           Skip report generation + leaderboard
    --no-cleanup          Keep temporary data files after benchmarking
    --no-pin              Disable CPU pinning (default: pin to one core)
    --list                List available benchmark executables and profiles
    --help                Show this help

Result storage:
    benchmark/results/<hostname>/<YYYY.MM.DD_HH.MM>/
"""

import argparse
import json
import multiprocessing
import os
import platform
import shutil
import socket
import subprocess
import sys
from datetime import datetime
from pathlib import Path


def get_project_root():
    """Find project root (directory containing CMakeLists.txt)."""
    p = Path(__file__).resolve().parent.parent
    if (p / "CMakeLists.txt").exists():
        return p
    raise RuntimeError(f"Cannot find project root from {__file__}")


def get_build_dir(project_root, build_type="Release"):
    """Determine the build directory."""
    # Try common build directories
    candidates = [
        project_root / f"build_{build_type.lower()}",
        project_root / "build_release",
        project_root / "build",
        project_root / "cmake-build-release",
    ]
    for d in candidates:
        if (d / "bin").exists():
            return d
    return project_root / "build"


def discover_executables(build_dir):
    """Find benchmark executables in the build directory."""
    bin_dir = build_dir / "bin"
    if not bin_dir.exists():
        return {}
    
    executables = {}
    for name in ["bench_macro_datasets", "bench_micro_types", "bench_generate_csv",
                 "bench_external_csv"]:
        path = bin_dir / name
        if path.exists() and os.access(path, os.X_OK):
            executables[name] = path
    
    # Also look for CLI tools in examples build dir or bin
    for name in ["csv2bcsv", "bcsv2csv"]:
        for search_dir in [bin_dir, build_dir / "examples"]:
            path = search_dir / name
            if path.exists() and os.access(path, os.X_OK):
                executables[name] = path
                break
    
    return executables


def create_output_dir(project_root, custom_path=None):
    """Create timestamped output directory."""
    if custom_path:
        out = Path(custom_path)
    else:
        hostname = socket.gethostname()
        timestamp = datetime.now().strftime("%Y.%m.%d_%H.%M")
        out = project_root / "benchmark" / "results" / hostname / timestamp
    
    out.mkdir(parents=True, exist_ok=True)
    return out


def write_platform_json(output_dir, build_type):
    """Write platform information to JSON."""
    info = {
        "hostname": socket.gethostname(),
        "os": f"{platform.system()} {platform.release()}",
        "architecture": platform.machine(),
        "python_version": platform.python_version(),
        "build_type": build_type,
        "timestamp": datetime.now().isoformat(),
    }
    
    # Try to get CPU info from /proc/cpuinfo on Linux
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if "model name" in line:
                    info["cpu_model"] = line.split(":")[1].strip()
                    break
    except (FileNotFoundError, PermissionError):
        info["cpu_model"] = platform.processor() or "unknown"
    
    # Try to get git version
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            info["git_describe"] = result.stdout.strip()
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    
    path = output_dir / "platform.json"
    with open(path, "w") as f:
        json.dump(info, f, indent=2)
    
    return info


def pin_cmd(cmd, pin=True):
    """Wrap a command list with taskset to pin to CPU core 0 (Linux only)."""
    if not pin:
        return cmd
    if shutil.which("taskset"):
        return ["taskset", "-c", "0"] + cmd
    return cmd


def run_macro_benchmark(executable, output_dir, mode="full", profile=None, rows=None,
                         build_type="Release", no_cleanup=False, pin=True):
    """Run the macro benchmark executable."""
    output_file = output_dir / "macro_results.json"
    
    cmd = [str(executable), f"--output={output_file}", f"--build-type={build_type}"]
    
    if mode == "sweep":
        cmd.append("--rows=50000")
    elif rows:
        cmd.append(f"--rows={rows}")
    
    if profile:
        cmd.append(f"--profile={profile}")
    
    if no_cleanup:
        cmd.append("--no-cleanup")
    
    cmd = pin_cmd(cmd, pin)
    
    print(f"\n{'='*60}")
    print(f"Running macro benchmark: {' '.join(cmd)}")
    print(f"{'='*60}\n")
    
    result = subprocess.run(cmd, timeout=1800, stderr=subprocess.PIPE, text=True)
    
    if result.returncode != 0:
        print(f"WARNING: Macro benchmark exited with code {result.returncode}")
        if result.stderr:
            print(f"  stderr: {result.stderr[:1000]}")
        # Save stderr log
        (output_dir / "macro_stderr.log").write_text(result.stderr or "")
    
    if output_file.exists():
        return json.loads(output_file.read_text())
    return None


def run_micro_benchmark(executable, output_dir, pin=True):
    """Run the Google Benchmark micro-benchmark executable."""
    output_file = output_dir / "micro_results.json"
    
    cmd = [
        str(executable),
        f"--benchmark_format=json",
        f"--benchmark_out={output_file}",
    ]
    
    cmd = pin_cmd(cmd, pin)
    
    print(f"\n{'='*60}")
    print(f"Running micro benchmark: {' '.join(cmd)}")
    print(f"{'='*60}\n")
    
    result = subprocess.run(cmd, timeout=600, stderr=subprocess.PIPE, text=True)
    
    if result.returncode != 0:
        print(f"WARNING: Micro benchmark exited with code {result.returncode}")
        if result.stderr:
            print(f"  stderr: {result.stderr[:1000]}")
        (output_dir / "micro_stderr.log").write_text(result.stderr or "")
    
    if output_file.exists():
        return json.loads(output_file.read_text())
    return None


def build_targets(project_root, build_dir, build_type, clean=True):
    """Clean-rebuild benchmark targets using all available cores."""
    nproc = str(multiprocessing.cpu_count())
    
    if clean and build_dir.exists():
        print(f"  Cleaning {build_dir} ...")
        shutil.rmtree(build_dir, ignore_errors=True)
    
    # Configure
    cxx_flags = "-O3 -march=native" if build_type == "Release" else ""
    cmd = [
        "cmake", "-S", str(project_root), "-B", str(build_dir),
        "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_CXX_FLAGS={cxx_flags}",
        "-DBCSV_ENABLE_EXTERNAL_CSV_BENCH=ON",
    ]
    print(f"  Configuring: {' '.join(cmd)}")
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    
    # Build all benchmark targets + CLI tools in one pass
    targets = ["bench_macro_datasets", "bench_micro_types", "bench_generate_csv",
                "bench_external_csv", "csv2bcsv", "bcsv2csv"]
    cmd = ["cmake", "--build", str(build_dir), "-j", nproc, "--"]
    cmd.extend(targets)
    print(f"  Building {len(targets)} targets with -j{nproc} ...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    # Report per-target status
    for target in targets:
        exe = build_dir / "bin" / target
        if exe.exists():
            print(f"    {target:<28} OK")
        else:
            print(f"    {target:<28} FAILED")
    if result.returncode != 0:
        # Print last 10 lines of error
        stderr_lines = (result.stderr or "").strip().split("\n")
        for line in stderr_lines[-10:]:
            print(f"    {line}")
        print(f"  WARNING: Build returned {result.returncode} (some targets may have failed)")


def run_cli_benchmark(executables, output_dir, profile="mixed_generic", rows=50000):
    """Run CLI-tool round-trip benchmark: generate CSV → csv2bcsv → bcsv2csv → validate.
    
    Requires: bench_generate_csv, csv2bcsv, bcsv2csv in executables dict.
    """
    required = ["bench_generate_csv", "csv2bcsv", "bcsv2csv"]
    missing = [k for k in required if k not in executables]
    if missing:
        print(f"  Skipping CLI benchmark (missing: {', '.join(missing)})")
        return None

    import tempfile
    tmpdir = tempfile.mkdtemp(prefix="bcsv_cli_bench_")
    ref_csv = os.path.join(tmpdir, "ref.csv")
    bcsv_file = os.path.join(tmpdir, "ref.bcsv")
    rt_csv = os.path.join(tmpdir, "roundtrip.csv")

    results = {"profile": profile, "rows": rows, "tools": {}}

    try:
        # 1. Generate reference CSV
        print(f"  Generating reference CSV ({profile}, {rows} rows)...")
        cmd = [str(executables["bench_generate_csv"]),
               f"--profile={profile}", f"--rows={rows}", f"--output={ref_csv}"]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if r.returncode != 0:
            results["error"] = f"bench_generate_csv failed: {r.stderr.strip()}"
            return results
        ref_size = os.path.getsize(ref_csv)
        results["ref_csv_bytes"] = ref_size

        # 2. csv2bcsv --benchmark --json (without ZoH, since bcsv2csv uses
        #    ReaderDirectAccess which doesn't support ZoH files yet)
        print(f"  Running csv2bcsv --benchmark --json ...")
        cmd = [str(executables["csv2bcsv"]), "--benchmark", "--json",
               "--no-zoh", ref_csv, bcsv_file]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if r.returncode != 0:
            results["tools"]["csv2bcsv"] = {"error": r.stderr.strip()}
        else:
            # Parse JSON from stdout (may be mixed with normal output; take last JSON line)
            for line in reversed(r.stdout.strip().split("\n")):
                line = line.strip()
                if line.startswith("{"):
                    try:
                        results["tools"]["csv2bcsv"] = json.loads(line)
                    except json.JSONDecodeError:
                        results["tools"]["csv2bcsv"] = {"raw": line}
                    break

        # 3. bcsv2csv --benchmark --json
        print(f"  Running bcsv2csv --benchmark --json ...")
        cmd = [str(executables["bcsv2csv"]), "--benchmark", "--json", bcsv_file, rt_csv]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if r.returncode != 0:
            results["tools"]["bcsv2csv"] = {"error": r.stderr.strip()}
        else:
            for line in reversed(r.stdout.strip().split("\n")):
                line = line.strip()
                if line.startswith("{"):
                    try:
                        results["tools"]["bcsv2csv"] = json.loads(line)
                    except json.JSONDecodeError:
                        results["tools"]["bcsv2csv"] = {"raw": line}
                    break

        # 4. Simple round-trip validation: compare file sizes and spot-check
        if os.path.exists(rt_csv):
            rt_size = os.path.getsize(rt_csv)
            results["roundtrip_csv_bytes"] = rt_size
            # Count lines (excluding header)
            with open(ref_csv) as f:
                ref_lines = sum(1 for _ in f) - 1  # minus header
            with open(rt_csv) as f:
                rt_lines = sum(1 for _ in f) - 1
            results["ref_rows"] = ref_lines
            results["roundtrip_rows"] = rt_lines
            results["row_count_match"] = ref_lines == rt_lines
        else:
            results["roundtrip_csv_bytes"] = 0
            results["row_count_match"] = False

    except subprocess.TimeoutExpired:
        results["error"] = "CLI benchmark timed out"
    except Exception as e:
        results["error"] = str(e)
    finally:
        # Cleanup temp files
        shutil.rmtree(tmpdir, ignore_errors=True)

    # Persist results
    cli_output = output_dir / "cli_results.json"
    with open(cli_output, "w") as f:
        json.dump(results, f, indent=2)

    return results


def print_summary(macro_data, micro_data, platform_info, output_dir, cli_data=None):
    """Print compressed human-readable summary."""
    print(f"\n{'='*60}")
    print("BCSV Benchmark Results Summary")
    print(f"{'='*60}")
    print(f"Host:    {platform_info.get('hostname', 'unknown')}")
    print(f"CPU:     {platform_info.get('cpu_model', 'unknown')}")
    print(f"Build:   {platform_info.get('build_type', 'unknown')}")
    print(f"Git:     {platform_info.get('git_describe', 'unknown')}")
    print()
    
    if macro_data and "results" in macro_data:
        print(f"{'Dataset/Mode':<40} {'Total(ms)':>10} {'Write':>8} {'Read':>8} {'Size':>8} {'Ok':>4}")
        print("-" * 80)
        for r in macro_data["results"]:
            size_mb = r.get("file_size", 0) / (1024 * 1024)
            valid = "OK" if r.get("validation_passed", False) else "FAIL"
            label = f"{r.get('dataset', '?')}/{r.get('mode', '?')}"
            total = r.get("write_time_ms", 0) + r.get("read_time_ms", 0)
            print(f"{label:<40} {total:>10.1f} {r.get('write_time_ms', 0):>8.1f} "
                  f"{r.get('read_time_ms', 0):>8.1f} {size_mb:>7.1f}M {valid:>4}")

        # Compute average BCSV vs CSV speedup
        csv_totals = []
        bcsv_totals = []
        for r in macro_data["results"]:
            total = r.get("write_time_ms", 0) + r.get("read_time_ms", 0)
            mode = r.get("mode", "")
            if mode == "CSV":
                csv_totals.append(total)
            elif mode == "BCSV Flexible":
                bcsv_totals.append(total)
        if csv_totals and bcsv_totals and len(csv_totals) == len(bcsv_totals):
            avg_speedup = sum(csv_totals) / sum(bcsv_totals)
            print(f"\n  Average BCSV Flexible speedup: {avg_speedup:.2f}x over CSV")
    
    if micro_data and "benchmarks" in micro_data:
        # Just top-level stats
        benchmarks = micro_data["benchmarks"]
        get_ns = [b["real_time"] for b in benchmarks if b["name"].startswith("BM_Get_")]
        set_ns = [b["real_time"] for b in benchmarks if b["name"].startswith("BM_Set_")]
        if get_ns:
            print(f"  Micro: get<T> avg {sum(get_ns)/len(get_ns):.1f} ns, "
                  f"set<T> avg {sum(set_ns)/len(set_ns):.1f} ns "
                  f"({len(benchmarks)} benchmarks)")
    
    if cli_data and "tools" in cli_data:
        match = cli_data.get("row_count_match", False)
        tools = cli_data["tools"]
        parts = []
        for tool_name, tool_data in tools.items():
            if isinstance(tool_data, dict) and "wall_ms" in tool_data:
                parts.append(f"{tool_name} {tool_data['wall_ms']}ms")
        if parts:
            print(f"  CLI:   {', '.join(parts)} — round-trip {'OK' if match else 'FAIL'}")
    
    print()


def generate_report(output_dir):
    """Run report_generator.py on the output directory."""
    script = Path(__file__).resolve().parent / "report_generator.py"
    if not script.exists():
        print(f"  WARNING: {script} not found, skipping report generation")
        return False
    result = subprocess.run(
        [sys.executable, str(script), str(output_dir)],
        capture_output=True, text=True, timeout=120,
    )
    if result.returncode != 0:
        print(f"  WARNING: Report generation failed: {result.stderr[:500]}")
        return False
    return True


def update_leaderboard_from(output_dir):
    """Run compare_runs.py to update the leaderboard for this run."""
    script = Path(__file__).resolve().parent / "compare_runs.py"
    if not script.exists():
        print(f"  WARNING: {script} not found, skipping leaderboard update")
        return
    # Self-compare to register bests (delta will be 0%)
    result = subprocess.run(
        [sys.executable, str(script), str(output_dir), str(output_dir),
         "--update-leaderboard"],
        capture_output=True, text=True, timeout=60,
    )
    if result.returncode != 0:
        # Non-zero is also returned for regressions, which is fine for self-compare
        pass
    # Print any leaderboard update messages
    for line in result.stdout.strip().split("\n"):
        if "leaderboard" in line.lower() or "best" in line.lower():
            print(f"  {line}")


def main():
    parser = argparse.ArgumentParser(
        description="BCSV Benchmark Orchestrator — full 360",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--mode", choices=["sweep", "full"], default="full",
                        help="sweep: reduced rows; full: profile defaults (default: full)")
    parser.add_argument("--build-type", default="Release",
                        help="CMake build type (default: Release)")
    parser.add_argument("--profile", default=None,
                        help="Run only this dataset profile")
    parser.add_argument("--rows", type=int, default=None,
                        help="Override row count")
    parser.add_argument("--size", choices=["S", "M", "L", "XL"], default=None,
                        help="Dataset size preset: S=10K, M=100K, L=500K, XL=2M rows")
    parser.add_argument("--output-dir", default=None,
                        help="Custom output directory")
    parser.add_argument("--no-build", action="store_true",
                        help="Skip rebuild step")
    parser.add_argument("--no-report", action="store_true",
                        help="Skip report generation and leaderboard update")
    parser.add_argument("--no-cleanup", action="store_true",
                        help="Keep temporary data files")
    parser.add_argument("--no-pin", action="store_true",
                        help="Disable CPU pinning")
    parser.add_argument("--list", action="store_true",
                        help="List available executables and profiles")
    
    args = parser.parse_args()

    # --size is a convenience alias for --rows
    SIZE_MAP = {"S": 10_000, "M": 100_000, "L": 500_000, "XL": 2_000_000}
    if args.size and not args.rows:
        args.rows = SIZE_MAP[args.size]
    
    project_root = get_project_root()
    build_dir = get_build_dir(project_root, args.build_type)
    pin = not args.no_pin
    
    # ── Step 1: Build ─────────────────────────────────────────────
    if not args.no_build:
        print(f"\n[1/5] Clean rebuild ({args.build_type}, -j{multiprocessing.cpu_count()})")
        build_targets(project_root, build_dir, args.build_type, clean=True)
    else:
        print(f"\n[1/5] Build: skipped (--no-build)")
    
    executables = discover_executables(build_dir)
    
    if args.list:
        print("Available executables:")
        for name, path in executables.items():
            print(f"  {name}: {path}")
        if "bench_macro_datasets" in executables:
            result = subprocess.run(
                [str(executables["bench_macro_datasets"]), "--list"],
                capture_output=True, text=True
            )
            print(f"\nAvailable profiles:")
            print(f"  {result.stdout.strip()}")
        return 0
    
    if not executables:
        print("ERROR: No benchmark executables found. Build first (remove --no-build).")
        return 1
    
    # Create output directory
    output_dir = create_output_dir(project_root, args.output_dir)
    
    # Write platform info
    platform_info = write_platform_json(output_dir, args.build_type)
    platform_info["cpu_pinning"] = pin and shutil.which("taskset") is not None
    
    # ── Step 2: Run benchmarks ────────────────────────────────────
    print(f"\n[2/5] Running benchmarks {'(pinned to CPU 0)' if pin else '(no pinning)'}")
    print(f"      Output: {output_dir}")
    
    macro_data = None
    if "bench_macro_datasets" in executables:
        macro_data = run_macro_benchmark(
            executables["bench_macro_datasets"],
            output_dir,
            mode=args.mode,
            profile=args.profile,
            rows=args.rows,
            build_type=args.build_type,
            no_cleanup=args.no_cleanup,
            pin=pin,
        )
    
    micro_data = None
    if "bench_micro_types" in executables:
        micro_data = run_micro_benchmark(
            executables["bench_micro_types"],
            output_dir,
            pin=pin,
        )
    
    # CLI tool round-trip
    cli_data = None
    cli_profile = args.profile or "mixed_generic"
    cli_rows = args.rows or (10000 if args.mode == "sweep" else 50000)
    if all(k in executables for k in ["bench_generate_csv", "csv2bcsv", "bcsv2csv"]):
        print(f"\n  CLI tool round-trip ({cli_profile}, {cli_rows} rows)")
        cli_data = run_cli_benchmark(
            executables, output_dir,
            profile=cli_profile, rows=cli_rows,
        )
    
    # External CSV comparison
    external_data = None
    if "bench_external_csv" in executables:
        external_output = output_dir / "external_results.json"
        ext_cmd = [
            str(executables["bench_external_csv"]),
            f"--output={external_output}",
            f"--build-type={args.build_type}",
        ]
        if args.mode == "sweep":
            ext_cmd.append("--rows=10000")
        elif args.rows:
            ext_cmd.append(f"--rows={args.rows}")
        if args.profile:
            ext_cmd.append(f"--profile={args.profile}")
        ext_cmd = pin_cmd(ext_cmd, pin)

        print(f"\n  External CSV comparison")
        result = subprocess.run(ext_cmd, timeout=1800, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  WARNING: External CSV benchmark exited with code {result.returncode}")
        if external_output.exists():
            try:
                external_data = json.loads(external_output.read_text())
            except json.JSONDecodeError:
                print(f"  WARNING: Failed to parse external results JSON")

    # Write manifest
    manifest = {
        "timestamp": datetime.now().isoformat(),
        "mode": args.mode,
        "profile_filter": args.profile,
        "row_override": args.rows,
        "cpu_pinning": platform_info.get("cpu_pinning", False),
        "files": sorted(f.name for f in output_dir.iterdir() if f.is_file()),
        "has_macro": macro_data is not None,
        "has_micro": micro_data is not None,
        "has_cli": cli_data is not None,
        "has_external": external_data is not None,
    }
    with open(output_dir / "manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)
    
    # ── Step 3: Generate report ───────────────────────────────────
    if not args.no_report:
        print(f"\n[3/5] Generating report + charts")
        if generate_report(output_dir):
            print(f"      Report: {output_dir / 'report.md'}")
        
        # ── Step 4: Update leaderboard ────────────────────────────
        print(f"\n[4/5] Updating leaderboard")
        update_leaderboard_from(output_dir)
    else:
        print(f"\n[3/5] Report: skipped (--no-report)")
        print(f"[4/5] Leaderboard: skipped (--no-report)")
    
    # ── Step 5: Summary ───────────────────────────────────────────
    print(f"\n[5/5] Summary")
    print_summary(macro_data, micro_data, platform_info, output_dir, cli_data)
    
    # File listing
    print(f"  Output directory: {output_dir}")
    print(f"  Files:")
    for f in sorted(output_dir.iterdir()):
        if f.is_file():
            size = f.stat().st_size
            if size > 1024 * 1024:
                print(f"    {f.name:<30} {size / (1024*1024):.1f} MB")
            elif size > 1024:
                print(f"    {f.name:<30} {size / 1024:.1f} KB")
            else:
                print(f"    {f.name:<30} {size} B")
    
    report_file = output_dir / "report.md"
    if report_file.exists():
        print(f"\n  Open report:  {report_file}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
