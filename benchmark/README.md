# Benchmark Suite

Single source of truth for benchmark operation (human + agent).

## Scope

The benchmark workflow is intentionally reduced to three run types:
- `MICRO` (Google Benchmark micro suite)
- `MACRO-SMALL` (macro suite at 10k rows)
- `MACRO-LARGE` (macro suite at 500k rows)

Legacy compare/leaderboard/report-latest flows are removed. Reporting and comparison are handled by `benchmark/report.py`.

## Build

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release-build -j$(nproc) --target bench_macro_datasets bench_micro_types
```

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

## Standard Runs

```bash
# Default developer run
python3 benchmark/run_benchmarks.py

# Micro benchmark pinned to CPU2 (target <5 min)
python3 benchmark/run_benchmarks.py --type=MICRO --pin=CPU2

# Full campaign (all three types)
python3 benchmark/run_benchmarks.py --type=MICRO,MACRO-SMALL,MACRO-LARGE

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
