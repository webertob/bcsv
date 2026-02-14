# BCSV Benchmark Suite — AI Skills Reference

> Quick-reference for AI agents to build, run, and interpret the benchmark suite.
> For humans, start with: benchmark/OPERATOR_GUIDE.md

## Build

```bash
# Release build (required for meaningful benchmarks)
cmake -S . -B build_release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=native" \
  -DBCSV_ENABLE_BENCHMARKS=ON \
  -DBCSV_ENABLE_MICRO_BENCHMARKS=ON
cmake --build build_release -j$(nproc)

# Optional: external CSV library comparison
cmake -S . -B build_release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=native" \
  -DBCSV_ENABLE_BENCHMARKS=ON \
  -DBCSV_ENABLE_MICRO_BENCHMARKS=ON \
  -DBCSV_ENABLE_EXTERNAL_CSV_BENCH=ON
cmake --build build_release -j$(nproc)
```

Executables land in `build_release/bin/`:
- `bench_macro_datasets` — macro write/read/validate benchmarks
- `bench_micro_types` — Google Benchmark per-type latency
- `bench_generate_csv` — CSV file generator utility
- `bench_external_csv` — BCSV CsvReader vs csv-parser (if enabled)
- `csv2bcsv`, `bcsv2csv` — CLI tools (also benchmarked)

## Run (One Command — Full 360)

```bash
# Default: clean rebuild + all benchmarks (pinned to CPU 0) + report + leaderboard
python3 benchmark/run_benchmarks.py --size=S          # Quick smoke test (~1 min)
python3 benchmark/run_benchmarks.py --size=M          # Standard run (~3 min)
python3 benchmark/run_benchmarks.py --size=L          # Full run (~10 min)
python3 benchmark/run_benchmarks.py                   # Profile defaults (no --size)

# Skip rebuild (reuse existing binaries)
python3 benchmark/run_benchmarks.py --size=M --no-build

# Skip report/leaderboard
python3 benchmark/run_benchmarks.py --size=S --no-report

# Disable CPU pinning
python3 benchmark/run_benchmarks.py --size=S --no-pin

# Repeat runs and aggregate medians (canonical JSONs are medians)
python3 benchmark/run_benchmarks.py --size=S --repeat=5

# Benchmark current suite against older include/bcsv from git revision
python3 benchmark/run_benchmarks.py --size=S --git-commit=v1.1.0
python3 benchmark/run_benchmarks.py --size=M --git-commit=HEAD~3 --repeat=3

# Use a specific harness ref for overlay (default is HEAD)
python3 benchmark/run_benchmarks.py --size=S --git-commit=v1.0.0 --bench-ref=main

# Keep worktree for manual debugging/backport commits
python3 benchmark/run_benchmarks.py --size=S --git-commit=v1.0.0 --keep-worktree

# Keep worktree only if run fails
python3 benchmark/run_benchmarks.py --size=S --git-commit=v1.0.0 --keep-worktree-on-fail

# Print prepared worktree path and exit (for manual backport workflows)
python3 benchmark/run_benchmarks.py --git-commit=v1.0.0 --print-worktree-path-only

# Machine-friendly: print only the path
python3 benchmark/run_benchmarks.py --git-commit=v1.0.0 --print-worktree-path-only --quiet

# Script-minimal output: keep run artifacts but suppress final summary table/listing
python3 benchmark/run_benchmarks.py --size=S --no-report --quiet --quiet-summary
```

The orchestrator auto-discovers executables in `build_release/bin/`, clean-rebuilds all
targets, runs macro/micro/CLI/external benchmarks with CPU pinning, generates a Markdown
report with 4 charts, updates the leaderboard, and prints a compressed summary.

When `--repeat > 1`, per-run raw outputs are kept under `repeats/run_XXX/` and top-level
`*_results.json` files contain medians for stable comparisons/reporting.

When `--git-commit` is used, benchmarking runs in an isolated git worktree under system `/tmp`
(`bcsv_bench_worktrees`) and never mutates your active workspace. Worktrees are auto-pruned,
keeping the most recent 5 by default (`--sandbox-keep`).

Use `--keep-worktree` to preserve the prepared worktree after the run for investigation
or manual backport commits; default behavior remains auto-cleanup.
Use `--keep-worktree-on-fail` to keep worktrees only for failed runs.
Use `--print-worktree-path-only` to only prepare and print the worktree path, then exit.
Use `--quiet` with print-only mode for path-only stdout suitable for scripts.
Use `--quiet-summary` to suppress the final summary and file listing for script-oriented runs.

## Operator Protocol (Reproducible Comparison)

Use this exact flow when comparing versions:

```bash
# 1) Start clean: remove stale/misleading run data
rm -rf benchmark/results/<hostname>/*

# 2) Run current worktree (small + large)
python3 benchmark/run_benchmarks.py --size=S --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_current_S
python3 benchmark/run_benchmarks.py --size=L --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_current_L

# 3) Run historical commits using current harness (small + large)
python3 benchmark/run_benchmarks.py --git-commit=<sha1> --bench-ref=HEAD --size=S --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha1>_S
python3 benchmark/run_benchmarks.py --git-commit=<sha1> --bench-ref=HEAD --size=L --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha1>_L

python3 benchmark/run_benchmarks.py --git-commit=<sha2> --bench-ref=HEAD --size=S --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha2>_S
python3 benchmark/run_benchmarks.py --git-commit=<sha2> --bench-ref=HEAD --size=L --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha2>_L
```

Notes:
- `--repeat=3` writes median top-level JSONs and preserves raw repeats under `repeats/`.
- `--size=S` and `--size=L` cover both small and large row counts; profile set covers small and large column counts.
- `--bench-ref=HEAD` ensures one consistent benchmark harness across compared commits.

## Run (Individual Executables)

```bash
# Macro benchmark: single profile, custom rows
build_release/bin/bench_macro_datasets --rows=100000 --profile=mixed_generic \
  --output=results.json

# Macro benchmark: all 9 profiles
build_release/bin/bench_macro_datasets --rows=100000 --output=results.json

# Micro benchmark (Google Benchmark JSON output)
build_release/bin/bench_micro_types --benchmark_format=json --benchmark_out=micro.json

# External CSV comparison
build_release/bin/bench_external_csv --rows=100000 --profile=mixed_generic
```

## Size Presets

| Flag | Rows | Duration (all 9 profiles) | Use Case |
|------|------|--------------------------|----------|
| `--size=S` | 10,000 | ~30 sec | CI, smoke test |
| `--size=M` | 100,000 | ~2 min | Development |
| `--size=L` | 500,000 | ~90 sec at -O3 | Release benchmarks |
| `--size=XL` | 2,000,000 | ~5 min | Stress test |

## Dataset Profiles

| Name | Cols | Types | Character |
|------|------|-------|-----------|
| `mixed_generic` | 72 | 6 per type × 12 types | General-purpose, random |
| `sparse_events` | 100 | Mixed + many empty | Burst data, ZoH-friendly |
| `sensor_noisy` | 50 | float/double heavy | High-entropy, ZoH-unfriendly |
| `string_heavy` | 30 | 20 string + 10 scalar | String-dominated |
| `bool_heavy` | 128+ | Mostly bool | Bitset performance |
| `arithmetic_wide` | 200 | Pure numeric | Wide rows, worst-case ZoH |
| `simulation_smooth` | 100 | float/double, linear drift | ZoH-optimal |
| `weather_timeseries` | 40 | Mixed realistic | Weather telemetry |
| `high_cardinality_string` | 50 | UUID strings | Worst-case compression |
| `realistic_measurement` | 38 | Mixed: float/double/int/bool/string | DAQ session with phases + multi-rate sensors |
| `rtl_waveform` | 290 | 256 bool + uint8/16/32/64 | RTL simulation waveform capture |

Each profile runs 3 modes: **CSV**, **BCSV Flexible**, **BCSV Flexible ZoH** = 33 benchmarks.

## Output Files

Default location: `benchmark/results/<hostname>/<timestamp>/` (override with `--output-dir`):

| File | Content |
|------|---------|
| `platform.json` | CPU, RAM, OS, git version, timestamp |
| `macro_results.json` | Array of 27 results with write_ms, read_ms, file_size, validation |
| `micro_results.json` | Google Benchmark JSON (per-type ns/op) |
| `cli_results.json` | CLI tool round-trip timing |
| `external_results.json` | BCSV CsvReader vs csv-parser (if available) |
| `report.md` | Markdown report with inline chart references |
| `chart_*.png` | 4 chart images (total_time, file_size, throughput, compression) |

## JSON Schema (macro_results.json)

```json
{
  "format_version": 2,
  "platform": { "hostname": "...", "cpu_model": "...", "os": "...", "git_describe": "..." },
  "total_time_sec": 84.2,
  "results": [
    {
      "dataset": "mixed_generic",
      "mode": "CSV",
      "write_time_ms": 1596.0,
      "read_time_ms": 1607.0,
      "file_size": 390856704,
      "rows": 500000,
      "columns": 72,
      "validation_passed": true,
      "write_throughput_mrows_sec": 0.313,
      "read_throughput_mrows_sec": 0.311
    }
  ]
}
```

## Regression Detection

```bash
# Compare two runs (5% threshold)
python3 benchmark/compare_runs.py <baseline_dir> <candidate_dir>

# Auto-detect regressions, exit code 1 if any found
python3 benchmark/compare_runs.py <baseline> <candidate> --threshold=5

# Update leaderboard with new best times
python3 benchmark/compare_runs.py <baseline> <candidate> --update-leaderboard
```

## Interpreting Results

- **Speedup > 1.0**: BCSV is faster than CSV for that metric
- **Typical BCSV vs CSV speedup**: 1.0x–1.7x (profile-dependent)
- **ZoH compression**: 0.1%–98% of BCSV size (data-dependent)
- **String-heavy data**: BCSV shows little speed advantage (CSV parsing is competitive)
- **Numeric-heavy data**: BCSV shows 1.3x–1.7x advantage
- **ZoH excels on**: slowly-drifting data (simulation_smooth, sparse_events)
- **ZoH struggles on**: high-entropy data (sensor_noisy, arithmetic_wide)

## CMake Options

| Option | Default | Purpose |
|--------|---------|---------|
| `BCSV_ENABLE_BENCHMARKS` | ON | Macro + micro + generator |
| `BCSV_ENABLE_MICRO_BENCHMARKS` | ON | Google Benchmark micro-benchmarks |
| `BCSV_ENABLE_EXTERNAL_CSV_BENCH` | OFF | csv-parser comparison (downloads ~30MB) |
| `BCSV_ENABLE_LEGACY_BENCHMARKS` | OFF | Old benchmark_large/benchmark_performance |

## Source Files

| File | Lines | Purpose |
|------|-------|---------|
| `tests/bench_common.hpp` | ~750 | Timer, PlatformInfo, CsvWriter, CsvReader, JSON output, CLI args |
| `tests/bench_datasets.hpp` | ~900 | 9 dataset profiles + deterministic data generators |
| `tests/bench_macro_datasets.cpp` | ~530 | Macro benchmark: CSV/BCSV/ZoH write→read→validate |
| `tests/bench_micro_types.cpp` | ~450 | 32 Google Benchmark micro-benchmarks |
| `tests/bench_external_csv.cpp` | ~450 | External csv-parser read comparison |
| `tests/bench_generate_csv.cpp` | ~100 | CSV file generator from profiles |
| `benchmark/run_benchmarks.py` | ~650 | Python orchestrator (full 360) |
| `benchmark/prepare_benchmark_worktree.py` | ~220 | Git worktree setup for historical benchmark runs |
| `benchmark/report_generator.py` | ~560 | Markdown + Matplotlib report generator |
| `benchmark/compare_runs.py` | ~440 | Regression detector + leaderboard |
| `benchmark/BENCHMARK_PLAN.md` | ~160 | Architecture plan + decision log |

## Known Limitations

- `CsvReader::parseLine()` doesn't check `from_chars` error codes
- External benchmark I/O subsystem differs (std::getline vs mmap)
