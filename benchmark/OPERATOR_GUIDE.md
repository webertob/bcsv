# Benchmark Operator Guide

This guide is for human operators running reproducible performance comparisons across commits.

## Goal

Produce fair, repeatable benchmark results for:
- current workspace commit
- previous functional commits

with:
- small and large row counts (`--size=S` and `--size=L`)
- repeated runs and median aggregation (`--repeat=3`)

## Prerequisites

- Build toolchain available (`cmake`, `ninja`, C++ compiler)
- Python 3 available
- Clean enough disk space for build artifacts and results

## Commit Selection Rule

When selecting “previous two functional commits”:
- include commits that change behavior/performance-relevant code
- skip metadata-only or naming-only commits unless explicitly requested

## Recommended Clean Run Procedure

### 1) Clean old results

```bash
rm -rf benchmark/results/<hostname>/*
```

This prevents accidental comparison with stale or non-comparable runs.

### 2) Run current commit (S + L)

```bash
python3 benchmark/run_benchmarks.py --size=S --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_current_S
python3 benchmark/run_benchmarks.py --size=L --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_current_L
```

### 3) Run historical commits using current harness

Use `--bench-ref=HEAD` so all compared versions use the same benchmark scripts.

```bash
python3 benchmark/run_benchmarks.py --git-commit=<sha1> --bench-ref=HEAD --size=S --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha1>_S
python3 benchmark/run_benchmarks.py --git-commit=<sha1> --bench-ref=HEAD --size=L --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha1>_L

python3 benchmark/run_benchmarks.py --git-commit=<sha2> --bench-ref=HEAD --size=S --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha2>_S
python3 benchmark/run_benchmarks.py --git-commit=<sha2> --bench-ref=HEAD --size=L --repeat=3 --no-report --output-dir benchmark/results/<hostname>/clean_<sha2>_L
```

For sparse-focused macro runs (single profile) you can run the executable directly:

```bash
build/bin/bench_macro_datasets --profile=mixed_generic --rows=10000 \
  --scenario=baseline,sample_every_n10,predicate_selectivity_10 \
  --output=benchmark/results/<hostname>/sparse_focus/macro_results.json
```

Notes:
- `mixed_generic` currently emits additional static modes (`BCSV Static`, `BCSV Static ZoH`).
- Orchestrator output is intentionally condensed; raw process output is written to `macro_stdout.log`, `macro_stderr.log`, `micro_stdout.log`, `micro_stderr.log`.

## Optional Debug Flags

- `--keep-worktree`: keep temporary historical worktree for inspection
- `--keep-worktree-on-fail`: keep only when run fails
- `--print-worktree-path-only --quiet`: print path only (script-friendly)
- `--quiet-summary`: suppress final summary and file listing (useful in CI/script logs)

## Result Semantics

- Top-level JSON files are median aggregates when `--repeat > 1`
- Per-repeat raw outputs are under `repeats/`
- Comparisons require matching workload shape (same rows and columns); tooling now guards against row-count mismatch by default

## Reporting

You can generate a report after the campaign:

```bash
python3 benchmark/report_generator.py benchmark/results/<hostname>/<run_dir>
```

The generated report includes:
- `Condensed Performance Matrix`
- `Condensed Performance Matrix Comparison` (delta vs best of previous 3 sibling runs)

Sidecar KPI artifacts are also written:
- `condensed_metrics.json`
- `condensed_metrics.csv`

## Troubleshooting

- Build fails in historical run:
  - re-run with `--keep-worktree-on-fail`
  - inspect the preserved worktree and build logs
- Noisy results:
  - increase `--repeat` (for example, 5)
  - reduce system background load
- Suspicious regressions:
  - verify row/column comparability first
  - verify benchmark profile and `--size` match
