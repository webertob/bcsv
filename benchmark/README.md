# Benchmark Suite

Single source of truth for benchmark operation (human + agent).

## Scope

The benchmark workflow is intentionally reduced to three run types:
- `MICRO` (Google Benchmark micro suite)
- `MACRO-SMALL` (macro suite at 10k rows)
- `MACRO-LARGE` (macro suite at 500k rows)

Legacy compare/leaderboard/report-latest flows are removed. Reporting and comparison are handled by `benchmark/report.py`.

Current macro dataset catalog contains 14 profiles, including string-heavy reference workloads:
`event_log`, `iot_fleet`, and `financial_orders`.

Macro runs execute the following **5 modes**:
- `CSV`
- `BCSV Flexible`
- `BCSV Flexible ZoH`
- `BCSV Static`
- `BCSV Static ZoH`

## Build

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release-build -j$(nproc) --target bench_macro_datasets bench_micro_types
```

## Direct Macro CLI Quick Start

Use the macro executable directly when you want fast local iteration on profile/scenario selection.

```bash
# Show CLI overview, options, and examples
build/ninja-release/bin/bench_macro_datasets --help

# Run one profile with selected scenarios
build/ninja-release/bin/bench_macro_datasets \
	--profile=rtl_waveform \
	--rows=10000 \
	--scenario=baseline,sparse_columns_k1

# Force narrow-terminal summary output
build/ninja-release/bin/bench_macro_datasets \
	--size=S \
	--summary=compact \
	--output=macro_small_results.json
```

Summary modes:
- `--summary=full` (default): full metrics table
- `--summary=compact`: terminal-friendly compact table (fits narrow terminals)

Discovery helpers:
- `--list` prints all profile names
- `--list-scenarios` prints all scenario IDs

## Streamlined CLI

```bash
python3 benchmark/run_benchmarks.py \
	--type=MACRO-SMALL \
	--repetitions=1 \
	--cpus=1 \
	--pin=NONE \
	--git=WIP
```

### Flags

- `--type=MICRO,MACRO-SMALL,MACRO-LARGE` (comma-separated, default `MACRO-SMALL`)
- `--repetitions=<N>` (default `1`)
- `--cpus=<N>` (default `1`, reserved for future parallel workers)
- `--pin=NONE|CPU<id>` (default `NONE`, example `CPU2`)
- `--git=<label>` (default `WIP`, controls results bucket naming)
- `--results=<path>` (default `benchmark/results/<hostname>/<git>`)
- `--no-build` / `--no-report`
- `--languages=python,csharp` (optional language lanes for Item 11.B)

## Standard Runs

```bash
# Default developer run
python3 benchmark/run_benchmarks.py

# Micro benchmark pinned to CPU2 (target <5 min)
python3 benchmark/run_benchmarks.py --type=MICRO --pin=CPU2

# Full campaign (all three types)
python3 benchmark/run_benchmarks.py --type=MICRO,MACRO-SMALL,MACRO-LARGE

# Include Python language lane in the same run directory/report
python3 benchmark/run_benchmarks.py --type=MACRO-SMALL --languages=python

# Clean baseline run bucket
python3 benchmark/run_benchmarks.py --type=MACRO-LARGE --git=clean
```

## Baseline + WIP Recipe (Recommended)

Use this flow for reproducible comparison between a clean commit baseline and current workspace changes.

```bash
# 1) Purge old results
rm -rf benchmark/results && mkdir -p benchmark/results

# 2) Clean baseline from a fresh clone at the selected git ref
python3 benchmark/build_and_run.py \
	--git-ref HEAD \
	--types MICRO,MACRO-SMALL,MACRO-LARGE \
	--repetitions 5 \
	--temp-root /tmp/bcsv \
	--results-root benchmark/results/$(hostname) \
	--pin NONE

# 3) Current workspace run (WIP) compared against latest clean baseline
python3 benchmark/run_benchmarks.py \
	--type=MICRO,MACRO-SMALL,MACRO-LARGE \
	--repetitions=5 \
	--cpus=1 \
	--pin=NONE \
	--git=WIP \
	--results=benchmark/results/$(hostname)/WIP \
	--no-build
```

### Interleaved Pair Comparison (Operator Utility)

For alternating baseline/candidate campaigns (for example 5 interleaved pairs),
generate a single pair-wise + aggregate markdown summary:

```bash
python3 benchmark/interleaved_compare.py \
	--baseline-root benchmark/results/$(hostname)/clean_interleaved \
	--candidate-root benchmark/results/$(hostname)/wip_interleaved \
	--expected-pairs 5 \
	--run-type MACRO-SMALL
```

### True Interleaved Pair Runner (Head-to-Head)

For strict interleaving semantics (`for repetition -> for benchmark type -> run A/B pair`),
use the dedicated runner below. It executes each pair **in parallel** (no pinning by default)
to expose both candidates to similar thermal/power conditions.

```bash
python3 benchmark/run_interleaved_pairs.py \
	--baseline-bin /tmp/bcsv/<git-hash>/build/ninja-release/bin \
	--candidate-bin build/ninja-release/bin \
	--baseline-label git-<git-hash>-clean \
	--candidate-label WIP \
	--types MICRO,MACRO-SMALL \
	--repetitions 5 \
	--results-root benchmark/results/$(hostname)/interleaved_h2h
```

Then compare the pair roots:

```bash
python3 benchmark/interleaved_compare.py \
	--baseline-root benchmark/results/$(hostname)/interleaved_h2h/git-<git-hash>-clean \
	--candidate-root benchmark/results/$(hostname)/interleaved_h2h/WIP \
	--expected-pairs 5 \
	--run-type MACRO-SMALL
```

Default output:
- `benchmark/results/<hostname>/interleaved_5x_comparison.md`

The script also prints a concise terminal summary (median/min/max deltas per metric)
so operators get immediate feedback without opening the markdown file.
Scores and comparison matrices are computed only from workloads present in both
baseline and candidate runs; excluded workloads are listed explicitly in the report.

Delta conventions used by comparison reports:
- Macro write/read deltas are expressed as **execution-time change** (derived from throughput).
	Positive means WIP is slower; negative means WIP is faster.
- Micro deltas are expressed as latency change (`real_time` ns).
	Positive means WIP is slower; negative means WIP is faster.
- Compression/file-size deltas use `(WIP - baseline) / baseline`.
	Positive means WIP output is larger.

Supported run types for `--run-type`:
- `MACRO-SMALL`
- `MACRO-LARGE`
- `MICRO` (latency deltas, using `micro_results.json`)

Temp clone location defaults:
- Linux: `/tmp/bcsv/<git-hash>`
- Windows: `C:/temp/bcsv/<git-hash>`

## Runtime Targets

- `MACRO-SMALL` should complete in < 3 minutes.
- `MACRO-LARGE` should complete in < 60 minutes.
- `MICRO` should run pinned to `CPU2` and complete in < 5 minutes.

## Reporting

`run_benchmarks.py` calls `benchmark/report.py` automatically (unless `--no-report`).

Manual usage:

```bash
# Auto-compare against latest git-clean run if available
python3 benchmark/report.py benchmark/results/<hostname>/WIP/<run_timestamp>

# Explicit baseline comparison
python3 benchmark/report.py <candidate_run_dir> --baseline <baseline_run_dir>
```

Generated summary report name:
- `report_<git_label>_<timestamp>.md`

Report content is intentionally concise:
- Host information
- Condensed Performance Matrix
- Condensed Performance Matrix Comparison

## Outputs

Each run directory contains:
- `platform.json`
- `manifest.json`
- `macro_small_results.json` (when `MACRO-SMALL` selected)
- `macro_large_results.json` (when `MACRO-LARGE` selected)
- `macro_results.json` (compatibility merged macro view)
- `micro_results.json` (when micro selected)
- `macro_small_stdout.log`, `macro_small_stderr.log`
- `macro_large_stdout.log`, `macro_large_stderr.log`
- `micro_stdout.log`, `micro_stderr.log` (when micro selected)
- `condensed_metrics.json`
- `report.md` and timestamped `report_<label>_<timestamp>.md`

For `--repetitions > 1`, raw per-repetition artifacts are stored under `repeats/run_<N>/` and top-level
`macro_*_results.json` / `micro_results.json` contain median-aggregated results across repetitions.

## External CSV Benchmark Policy

3rd-party CSV benchmarks are treated as one-time documented reference measurements and are not part of the default orchestrator flow.

## Item 11.B Additions

### Reference Time-Series Workloads

Open-data reference mapping and cache/download workflow:
- `benchmark/REFERENCE_WORKLOADS.md`
- `benchmark/reference_workloads.json`
- `benchmark/fetch_reference_datasets.py`

Fetch datasets to non-versioned cache (`tmp/reference_datasets`):

```bash
python3 benchmark/fetch_reference_datasets.py
```

### Dedicated Python Benchmarks

Python lane is intentionally separate from the C++ orchestrator and writes schema-compatible macro rows:

```bash
python3 python/benchmarks/run_pybcsv_benchmarks.py --size=S
```

Output default:
- `benchmark/results/<hostname>/python/py_macro_results_<timestamp>.json`

### Dedicated C# Benchmarks (Standalone .NET)

Build native C API target, then run the .NET benchmark harness:

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release-build -j$(nproc) --target bcsv_c_api
export LD_LIBRARY_PATH="$PWD/build/ninja-release/lib:$LD_LIBRARY_PATH"
dotnet run --project csharp/benchmarks/Bcsv.Benchmarks.csproj -- --size=S
```

Output default:
- `benchmark/results/<hostname>/csharp/cs_macro_results_<timestamp>.json`
