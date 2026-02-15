#!/usr/bin/env python3
"""
BCSV Benchmark Orchestrator — full 360

By default performs the complete benchmark cycle:
  1. Clean rebuild of all benchmark targets (all cores)
  2. Run full benchmark with warmup and CPU pinning (single core)
    3. Generate Markdown report
  4. Update the leaderboard
  5. Print compressed summary with file paths

Usage:
    python run_benchmarks.py                       # full 360 (default)
    python run_benchmarks.py --size=S              # quick smoke test
    python run_benchmarks.py --size=S --repeat=5   # 5 runs + median outputs
    python run_benchmarks.py --git-commit=v1.1.0   # run current suite on a historical git worktree
    python run_benchmarks.py --git-commit=v1.1.0 --keep-worktree  # keep sandbox for debugging
    python run_benchmarks.py --git-commit=v1.1.0 --keep-worktree-on-fail  # keep sandbox only on failures
    python run_benchmarks.py --git-commit=v1.1.0 --print-worktree-path-only  # print prepared worktree path and exit
    python run_benchmarks.py --git-commit=v1.1.0 --print-worktree-path-only --quiet  # path-only output
    python run_benchmarks.py --size=S --no-report --quiet --quiet-summary  # minimal script output
    python run_benchmarks.py --no-build --no-report  # just run
    python run_benchmarks.py --profile=mixed_generic --size=L
    python run_benchmarks.py --list                # show available profiles

Options:
    --mode=sweep|full     sweep: reduced rows; full: profile defaults (default: full)
    --build-type=TYPE     CMake build type (default: Release)
    --size=S|M|L|XL       Row count preset: S=10K M=100K L=500K XL=2M
    --rows=N              Override row count directly
    --repeat=N            Repeat benchmark execution N times, emit median results
    --profile=NAME        Run only this dataset profile
    --git-commit=REV      Benchmark historical revision in isolated git worktree
    --bench-ref=REF       Ref providing benchmark harness overlay (default: HEAD)
    --sandbox-keep=N      Keep last N temp worktrees (default: 5)
    --keep-worktree       Keep prepared worktree after run (debugging/backporting)
    --keep-worktree-on-fail
                           Keep prepared worktree only if run fails
    --print-worktree-path-only
                           Print prepared worktree path and exit (implies keeping it)
    --quiet               Reduce console output (path-only output with --print-worktree-path-only)
    --quiet-summary       Suppress final summary and output file listing
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
import statistics
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


def create_output_dir(project_root, custom_path=None, suffix=None):
    """Create timestamped output directory."""
    if custom_path:
        out = Path(custom_path)
    else:
        hostname = socket.gethostname()
        timestamp = datetime.now().strftime("%Y.%m.%d_%H.%M")
        if suffix:
            timestamp = f"{timestamp}_{suffix}"
        out = project_root / "benchmark" / "results" / hostname / timestamp
    
    out.mkdir(parents=True, exist_ok=True)
    return out


def write_json_file(path, data):
    with open(path, "w") as f:
        json.dump(data, f, indent=2)


def write_platform_json(output_dir, build_type, git_cwd=None):
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
            cwd=str(git_cwd) if git_cwd else None,
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            info["git_describe"] = result.stdout.strip()
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    
    path = output_dir / "platform.json"
    write_json_file(path, info)
    
    return info


def _coerce_median(values):
    med = statistics.median(values)
    if all(isinstance(v, int) and not isinstance(v, bool) for v in values):
        return int(round(med))
    return float(med)


def _aggregate_dicts_by_median(samples):
    if not samples:
        return {}

    out = dict(samples[0])
    all_keys = set()
    for s in samples:
        all_keys.update(s.keys())

    for key in all_keys:
        values = [s.get(key) for s in samples if key in s]
        if len(values) != len(samples):
            continue
        if all(isinstance(v, bool) for v in values):
            out[key] = all(values)
        elif all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in values):
            out[key] = _coerce_median(values)
        else:
            out[key] = values[0]

    return out


def aggregate_macro_like_runs(run_payloads):
    if not run_payloads:
        return None

    template = dict(run_payloads[0])
    result_map = {}
    for payload in run_payloads:
        for row in payload.get("results", []):
            key = (row.get("dataset", "?"), row.get("mode", "?"))
            result_map.setdefault(key, []).append(row)

    merged_rows = []
    for key in sorted(result_map.keys()):
        merged_rows.append(_aggregate_dicts_by_median(result_map[key]))

    totals = [p.get("total_time_sec") for p in run_payloads
              if isinstance(p.get("total_time_sec"), (int, float))]
    if totals:
        template["total_time_sec"] = _coerce_median(totals)

    template["results"] = merged_rows
    template["aggregation"] = {
        "method": "median",
        "repeat": len(run_payloads),
    }
    return template


def aggregate_micro_runs(run_payloads):
    if not run_payloads:
        return None

    template = dict(run_payloads[0])
    bench_map = {}
    for payload in run_payloads:
        for bench in payload.get("benchmarks", []):
            key = bench.get("name", "?")
            bench_map.setdefault(key, []).append(bench)

    merged_bench = []
    for key in sorted(bench_map.keys()):
        merged_bench.append(_aggregate_dicts_by_median(bench_map[key]))

    template["benchmarks"] = merged_bench
    template["aggregation"] = {
        "method": "median",
        "repeat": len(run_payloads),
    }
    return template


def aggregate_cli_runs(run_payloads):
    if not run_payloads:
        return None

    template = dict(run_payloads[0])
    merged_tools = {}
    tool_names = set()
    for payload in run_payloads:
        tool_names.update((payload.get("tools") or {}).keys())

    for tool in sorted(tool_names):
        tool_samples = []
        for payload in run_payloads:
            td = (payload.get("tools") or {}).get(tool)
            if isinstance(td, dict):
                tool_samples.append(td)
        if tool_samples:
            merged_tools[tool] = _aggregate_dicts_by_median(tool_samples)

    template["tools"] = merged_tools
    bool_fields = ["row_count_match"]
    for field in bool_fields:
        vals = [p.get(field) for p in run_payloads if isinstance(p.get(field), bool)]
        if vals:
            template[field] = all(vals)
    template["aggregation"] = {
        "method": "median",
        "repeat": len(run_payloads),
    }
    return template


def prepare_git_worktree(project_root, git_commit, bench_ref, keep_last):
    script = Path(__file__).resolve().parent / "prepare_benchmark_worktree.py"
    if not script.exists():
        raise RuntimeError(f"Missing helper script: {script}")

    cmd = [
        sys.executable,
        str(script),
        "--project-root", str(project_root),
        "--git-commit", git_commit,
        "--bench-ref", bench_ref,
        "--keep", str(keep_last),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip() or "worktree preparation failed")
    return json.loads(proc.stdout.strip())


def cleanup_git_worktree(project_root, worktree_path, keep_last):
    if not worktree_path:
        return
    subprocess.run(
        ["git", "-C", str(project_root), "worktree", "remove", "--force", str(worktree_path)],
        capture_output=True,
        text=True,
    )
    if Path(worktree_path).exists():
        shutil.rmtree(worktree_path, ignore_errors=True)
    subprocess.run(
        ["git", "-C", str(project_root), "worktree", "prune"],
        capture_output=True,
        text=True,
    )


def pin_cmd(cmd, pin=True, cpu=0):
    """Wrap a command list with taskset to pin to a CPU core (Linux only)."""
    if not pin:
        return cmd
    if shutil.which("taskset"):
        return ["taskset", "-c", str(cpu)] + cmd
    return cmd


def run_macro_benchmark(executable, output_dir, mode="full", profile=None, rows=None,
                         build_type="Release", no_cleanup=False, pin=True, pin_cpu=0, quiet=False):
    """Run the macro benchmark executable."""
    output_file = output_dir / "macro_results.json"
    stdout_log = output_dir / "macro_stdout.log"
    stderr_log = output_dir / "macro_stderr.log"
    
    cmd = [str(executable), f"--output={output_file}", f"--build-type={build_type}"]
    
    if mode == "sweep":
        cmd.append("--rows=50000")
    elif rows:
        cmd.append(f"--rows={rows}")
    
    if profile:
        cmd.append(f"--profile={profile}")
    
    if no_cleanup:
        cmd.append("--no-cleanup")
    
    cmd = pin_cmd(cmd, pin, cpu=pin_cpu)
    
    if not quiet:
        print(f"\n{'='*60}")
        print(f"Running macro benchmark: {' '.join(cmd)}")
        print(f"{'='*60}\n")
    
    result = subprocess.run(
        cmd,
        timeout=1800,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    stdout_log.write_text(result.stdout or "")
    stderr_log.write_text(result.stderr or "")
    
    if result.returncode != 0:
        print(f"WARNING: Macro benchmark exited with code {result.returncode}")
        if result.stderr:
            print(f"  stderr: {result.stderr[:1000]}")
    
    if output_file.exists():
        data = json.loads(output_file.read_text())
        if not quiet:
            try:
                rows_out = data.get("results", [])
                mode_stats = {}
                for row in rows_out:
                    mode = row.get("mode", "?")
                    total = float(row.get("write_time_ms", 0)) + float(row.get("read_time_ms", 0))
                    mode_stats.setdefault(mode, []).append(total)
                summary = ", ".join(
                    f"{m}: med {statistics.median(v):.1f} ms"
                    for m, v in sorted(mode_stats.items()) if v
                )
                print(f"  Macro summary ({len(rows_out)} rows): {summary}")
                print(f"  Raw logs: {stdout_log.name}, {stderr_log.name}")
            except Exception:
                print(f"  Macro summary unavailable; see {stdout_log.name}/{stderr_log.name}")
        return data
    return None


def run_micro_benchmark(executable, output_dir, pin=True, pin_cpu=0, quiet=False):
    """Run the Google Benchmark micro-benchmark executable."""
    output_file = output_dir / "micro_results.json"
    stdout_log = output_dir / "micro_stdout.log"
    stderr_log = output_dir / "micro_stderr.log"
    
    cmd = [
        str(executable),
        f"--benchmark_format=json",
        f"--benchmark_out={output_file}",
    ]
    
    cmd = pin_cmd(cmd, pin, cpu=pin_cpu)
    
    if not quiet:
        print(f"\n{'='*60}")
        print(f"Running micro benchmark: {' '.join(cmd)}")
        print(f"{'='*60}\n")
    
    result = subprocess.run(
        cmd,
        timeout=600,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    stdout_log.write_text(result.stdout or "")
    stderr_log.write_text(result.stderr or "")
    
    if result.returncode != 0:
        print(f"WARNING: Micro benchmark exited with code {result.returncode}")
        if result.stderr:
            print(f"  stderr: {result.stderr[:1000]}")
    
    if output_file.exists():
        data = json.loads(output_file.read_text())
        if not quiet:
            benches = data.get("benchmarks", [])
            print(f"  Micro summary: {len(benches)} benchmark entries")
            print(f"  Raw logs: {stdout_log.name}, {stderr_log.name}")
        return data
    return None


def build_targets(project_root, build_dir, build_type, clean=True, quiet=False):
    """Clean-rebuild benchmark targets using all available cores."""
    nproc = str(multiprocessing.cpu_count())
    
    if clean and build_dir.exists():
        if not quiet:
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
    if not quiet:
        print(f"  Configuring: {' '.join(cmd)}")
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    
    # Build all benchmark targets + CLI tools in one pass
    targets = ["bench_macro_datasets", "bench_micro_types", "bench_generate_csv",
                "bench_external_csv", "csv2bcsv", "bcsv2csv"]
    cmd = ["cmake", "--build", str(build_dir), "-j", nproc, "--"]
    cmd.extend(targets)
    if not quiet:
        print(f"  Building {len(targets)} targets with -j{nproc} ...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    # Report per-target status
    if not quiet:
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


def run_cli_benchmark(executables, output_dir, profile="mixed_generic", rows=50000, quiet=False):
    """Run CLI-tool round-trip benchmark: generate CSV → csv2bcsv → bcsv2csv → validate.
    
    Requires: bench_generate_csv, csv2bcsv, bcsv2csv in executables dict.
    """
    required = ["bench_generate_csv", "csv2bcsv", "bcsv2csv"]
    missing = [k for k in required if k not in executables]
    if missing:
        if not quiet:
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
        if not quiet:
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
        if not quiet:
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
        if not quiet:
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
        rows = macro_data["results"]
        if len(rows) > 30:
            print(f"{'Mode':<24} {'Rows':>6} {'Total med(ms)':>14} {'Write med(ms)':>14} {'Read med(ms)':>13} {'Size med(MB)':>13}")
            print("-" * 92)
            by_mode = {}
            for r in rows:
                by_mode.setdefault(r.get("mode", "?"), []).append(r)

            for mode, mode_rows in sorted(by_mode.items()):
                totals = [float(x.get("write_time_ms", 0)) + float(x.get("read_time_ms", 0)) for x in mode_rows]
                writes = [float(x.get("write_time_ms", 0)) for x in mode_rows]
                reads = [float(x.get("read_time_ms", 0)) for x in mode_rows]
                sizes = [float(x.get("file_size", 0)) / (1024 * 1024) for x in mode_rows]
                print(f"{mode:<24} {len(mode_rows):>6} {statistics.median(totals):>14.1f}"
                      f" {statistics.median(writes):>14.1f} {statistics.median(reads):>13.1f} {statistics.median(sizes):>13.2f}")
            print("  (Condensed view shown; full per-scenario rows are in macro_results.json/report.md)")
        else:
            print(f"{'Dataset/Mode':<40} {'Total(ms)':>10} {'Write':>8} {'Read':>8} {'Size':>8} {'Ok':>4}")
            print("-" * 80)
            for r in rows:
                size_mb = r.get("file_size", 0) / (1024 * 1024)
                valid = "OK" if r.get("validation_passed", False) else "FAIL"
                label = f"{r.get('dataset', '?')}/{r.get('mode', '?')}"
                total = r.get("write_time_ms", 0) + r.get("read_time_ms", 0)
                print(f"{label:<40} {total:>10.1f} {r.get('write_time_ms', 0):>8.1f} "
                      f"{r.get('read_time_ms', 0):>8.1f} {size_mb:>7.1f}M {valid:>4}")

        # Compute average BCSV vs CSV speedup
        csv_totals = []
        bcsv_totals = []
        for r in rows:
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


def run_repeated_benchmarks(args, executables, output_dir, pin, pin_cpu=0, quiet=False):
    macro_runs = []
    micro_runs = []
    cli_runs = []
    external_runs = []

    cli_profile = args.profile or "mixed_generic"
    cli_rows = args.rows or (10000 if args.mode == "sweep" else 50000)

    for run_idx in range(1, args.repeat + 1):
        if args.repeat > 1:
            run_dir = output_dir / "repeats" / f"run_{run_idx:03d}"
            run_dir.mkdir(parents=True, exist_ok=True)
            if not quiet:
                print(f"\n  Repeat {run_idx}/{args.repeat}: {run_dir}")
        else:
            run_dir = output_dir

        if "bench_macro_datasets" in executables:
            macro_data = run_macro_benchmark(
                executables["bench_macro_datasets"],
                run_dir,
                mode=args.mode,
                profile=args.profile,
                rows=args.rows,
                build_type=args.build_type,
                no_cleanup=args.no_cleanup,
                pin=pin,
                pin_cpu=pin_cpu,
                quiet=quiet,
            )
            if macro_data:
                macro_runs.append(macro_data)

        if "bench_micro_types" in executables:
            micro_data = run_micro_benchmark(
                executables["bench_micro_types"],
                run_dir,
                pin=pin,
                pin_cpu=pin_cpu,
                quiet=quiet,
            )
            if micro_data:
                micro_runs.append(micro_data)

        if all(k in executables for k in ["bench_generate_csv", "csv2bcsv", "bcsv2csv"]):
            if not quiet:
                print(f"\n  CLI tool round-trip ({cli_profile}, {cli_rows} rows)")
            cli_data = run_cli_benchmark(
                executables, run_dir,
                profile=cli_profile, rows=cli_rows,
                quiet=quiet,
            )
            if cli_data:
                cli_runs.append(cli_data)

        if "bench_external_csv" in executables:
            external_output = run_dir / "external_results.json"
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
            ext_cmd = pin_cmd(ext_cmd, pin, cpu=pin_cpu)

            if not quiet:
                print(f"\n  External CSV comparison")
            result = subprocess.run(ext_cmd, timeout=1800, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"  WARNING: External CSV benchmark exited with code {result.returncode}")
            if external_output.exists():
                try:
                    external_runs.append(json.loads(external_output.read_text()))
                except json.JSONDecodeError:
                    print(f"  WARNING: Failed to parse external results JSON")

    return macro_runs, micro_runs, cli_runs, external_runs


def finalize_run_payloads(output_dir, repeat, macro_runs, micro_runs, cli_runs, external_runs):
    macro_data = None
    micro_data = None
    cli_data = None
    external_data = None

    if repeat == 1:
        macro_data = macro_runs[0] if macro_runs else None
        micro_data = micro_runs[0] if micro_runs else None
        cli_data = cli_runs[0] if cli_runs else None
        external_data = external_runs[0] if external_runs else None
    else:
        macro_data = aggregate_macro_like_runs(macro_runs)
        micro_data = aggregate_micro_runs(micro_runs)
        cli_data = aggregate_cli_runs(cli_runs)
        external_data = aggregate_macro_like_runs(external_runs)

        if macro_data:
            write_json_file(output_dir / "macro_results.json", macro_data)
        if micro_data:
            write_json_file(output_dir / "micro_results.json", micro_data)
        if cli_data:
            write_json_file(output_dir / "cli_results.json", cli_data)
        if external_data:
            write_json_file(output_dir / "external_results.json", external_data)

    return macro_data, micro_data, cli_data, external_data


def write_manifest_file(
    output_dir,
    args,
    platform_info,
    macro_data,
    micro_data,
    cli_data,
    external_data,
    git_commit_resolved,
    bench_ref_resolved,
    sandbox_root,
    sandbox_dir,
):
    manifest = {
        "timestamp": datetime.now().isoformat(),
        "mode": args.mode,
        "profile_filter": args.profile,
        "row_override": args.rows,
        "repeat": args.repeat,
        "cpu_pinning": platform_info.get("cpu_pinning", False),
        "git_commit_requested": args.git_commit,
        "git_commit_resolved": git_commit_resolved,
        "bench_ref": args.bench_ref,
        "bench_ref_resolved": bench_ref_resolved,
        "sandbox_root": str(sandbox_root) if sandbox_root else None,
        "sandbox_dir": str(sandbox_dir) if sandbox_dir else None,
        "keep_worktree": args.keep_worktree,
        "keep_worktree_on_fail": args.keep_worktree_on_fail,
        "files": sorted(f.name for f in output_dir.iterdir() if f.is_file()),
        "has_macro": macro_data is not None,
        "has_micro": micro_data is not None,
        "has_cli": cli_data is not None,
        "has_external": external_data is not None,
    }
    write_json_file(output_dir / "manifest.json", manifest)


def print_output_files(output_dir, quiet=False):
    if quiet:
        return
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
    parser.add_argument("--repeat", type=int, default=1,
                        help="Repeat benchmark execution N times and emit medians")
    parser.add_argument("--size", choices=["S", "M", "L", "XL"], default=None,
                        help="Dataset size preset: S=10K, M=100K, L=500K, XL=2M rows")
    parser.add_argument("--git-commit", default=None,
                        help="Benchmark historical revision in isolated git worktree")
    parser.add_argument("--bench-ref", default="HEAD",
                        help="Ref providing benchmark harness overlay (default: HEAD)")
    parser.add_argument("--sandbox-keep", type=int, default=5,
                        help="Keep last N temporary worktrees (default: 5)")
    parser.add_argument("--keep-worktree", action="store_true",
                        help="Keep prepared worktree after run for debugging/backporting")
    parser.add_argument("--keep-worktree-on-fail", action="store_true",
                        help="Keep prepared worktree only when benchmark run fails")
    parser.add_argument("--print-worktree-path-only", action="store_true",
                        help="Print prepared worktree path and exit (implies keeping it)")
    parser.add_argument("--quiet", action="store_true",
                        help="Reduce console output")
    parser.add_argument("--quiet-summary", action="store_true",
                        help="Suppress final summary and output file listing")
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
    parser.add_argument("--pin-cpu", type=int, default=0,
                        help="CPU core to pin to (default: 0)")
    parser.add_argument("--list", action="store_true",
                        help="List available executables and profiles")
    
    args = parser.parse_args()

    if args.repeat < 1:
        print("ERROR: --repeat must be >= 1")
        return 1
    if args.sandbox_keep < 1:
        print("ERROR: --sandbox-keep must be >= 1")
        return 1

    if args.keep_worktree and args.keep_worktree_on_fail:
        if not args.quiet:
            print("NOTE: --keep-worktree overrides --keep-worktree-on-fail")
    if args.print_worktree_path_only and not args.git_commit:
        print("ERROR: --print-worktree-path-only requires --git-commit")
        return 1

    keep_for_print_only = args.print_worktree_path_only

    # --size is a convenience alias for --rows
    SIZE_MAP = {"S": 10_000, "M": 100_000, "L": 500_000, "XL": 2_000_000}
    if args.size and not args.rows:
        args.rows = SIZE_MAP[args.size]
    
    project_root = get_project_root()
    run_project_root = project_root
    git_commit_resolved = None
    sandbox_dir = None
    sandbox_root = None
    bench_ref_resolved = None

    if args.git_commit:
        try:
            worktree_info = prepare_git_worktree(
                project_root,
                args.git_commit,
                args.bench_ref,
                args.sandbox_keep,
            )
            git_commit_resolved = worktree_info["target_sha"]
            bench_ref_resolved = worktree_info.get("bench_sha")
            sandbox_dir = worktree_info["sandbox_dir"]
            sandbox_root = worktree_info["sandbox_root"]
            run_project_root = Path(sandbox_dir)
        except Exception as e:
            print(f"ERROR: Failed to prepare git worktree: {e}")
            return 1

    build_dir = get_build_dir(run_project_root, args.build_type)
    pin = not args.no_pin
    pin_cpu = args.pin_cpu
    
    # ── Step 1: Build ─────────────────────────────────────────────
    if args.git_commit:
        if not args.quiet:
            print(f"\nWorktree mode: target {args.git_commit} ({git_commit_resolved[:12]})")
            if bench_ref_resolved:
                print(f"  Benchmark harness from {args.bench_ref} ({bench_ref_resolved[:12]})")
            print(f"  Sandbox root: {sandbox_root}")
            print(f"  Active worktree: {sandbox_dir}")

    run_succeeded = False

    try:
        if args.print_worktree_path_only:
            print(str(sandbox_dir))
            run_succeeded = True
            return 0

        if not args.no_build:
            print(f"\n[1/5] Clean rebuild ({args.build_type}, -j{multiprocessing.cpu_count()})")
            build_targets(run_project_root, build_dir, args.build_type, clean=True, quiet=args.quiet)
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
        suffix = f"g{git_commit_resolved[:7]}" if git_commit_resolved else None
        output_dir = create_output_dir(project_root, args.output_dir, suffix=suffix)
    
        # Write platform info
        platform_info = write_platform_json(output_dir, args.build_type, git_cwd=run_project_root)
        platform_info["cpu_pinning"] = pin and shutil.which("taskset") is not None
        if pin and shutil.which("taskset"):
            platform_info["cpu_pinned_to"] = pin_cpu
        if git_commit_resolved:
            platform_info["bench_source"] = "current-suite+historic-worktree"
            platform_info["bench_source_git_commit"] = git_commit_resolved
            platform_info["bench_source_git_commit_requested"] = args.git_commit
            platform_info["bench_source_bench_ref"] = args.bench_ref
            platform_info["bench_source_bench_ref_resolved"] = bench_ref_resolved
            write_json_file(output_dir / "platform.json", platform_info)
    
        # ── Step 2: Run benchmarks ────────────────────────────────────
        repeat_suffix = f", repeat={args.repeat}" if args.repeat > 1 else ""
        print(f"\n[2/5] Running benchmarks {'(pinned to CPU ' + str(pin_cpu) + ')' if pin else '(no pinning)'}{repeat_suffix}")
        print(f"      Output: {output_dir}")

        macro_runs, micro_runs, cli_runs, external_runs = run_repeated_benchmarks(
            args, executables, output_dir, pin, pin_cpu=pin_cpu, quiet=args.quiet
        )

        macro_data, micro_data, cli_data, external_data = finalize_run_payloads(
            output_dir,
            args.repeat,
            macro_runs,
            micro_runs,
            cli_runs,
            external_runs,
        )

        write_manifest_file(
            output_dir,
            args,
            platform_info,
            macro_data,
            micro_data,
            cli_data,
            external_data,
            git_commit_resolved,
            bench_ref_resolved,
            sandbox_root,
            sandbox_dir,
        )
    
        # ── Step 3: Generate report ───────────────────────────────────
        if not args.no_report:
            print(f"\n[3/5] Generating report")
            if generate_report(output_dir):
                print(f"      Report: {output_dir / 'report.md'}")

            # ── Step 4: Update leaderboard ────────────────────────────
            print(f"\n[4/5] Updating leaderboard")
            update_leaderboard_from(output_dir)
        else:
            print(f"\n[3/5] Report: skipped (--no-report)")
            print(f"[4/5] Leaderboard: skipped (--no-report)")
    
        # ── Step 5: Summary ───────────────────────────────────────────
        if not args.quiet_summary:
            print(f"\n[5/5] Summary")
            print_summary(macro_data, micro_data, platform_info, output_dir, cli_data)
        elif not args.quiet:
            print(f"\n[5/5] Summary: skipped (--quiet-summary)")
    
        # File listing
        print_output_files(output_dir, quiet=(args.quiet or args.quiet_summary))
    
        report_file = output_dir / "report.md"
        if report_file.exists():
            print(f"\n  Open report:  {report_file}")

        run_succeeded = True
        return 0
    finally:
        if sandbox_dir:
            keep_effective = keep_for_print_only or args.keep_worktree or (args.keep_worktree_on_fail and not run_succeeded)
            if keep_effective:
                if keep_for_print_only:
                    reason = "print-only requested"
                elif args.keep_worktree:
                    reason = "requested"
                else:
                    reason = "run failed"
                if not args.quiet:
                    print(f"\nKeeping worktree for inspection ({reason}): {sandbox_dir}")
            else:
                cleanup_git_worktree(project_root, sandbox_dir, args.sandbox_keep)


if __name__ == "__main__":
    sys.exit(main())
