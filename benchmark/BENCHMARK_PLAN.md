# BCSV Benchmark Suite — Implementation Plan

> Agreed plan for ToDo item 9. This file is the authoritative reference.
> Supersedes the raw bullet list in ToDo.txt for item 9.

## Architecture Decision

**Concept B + C elements** — Modular executables + data-driven parameterization:
- Multiple focused benchmark executables (not one monolith)
- Python orchestrator for build → run → collect → report workflow
- Google Benchmark for micro-benchmarks, hand-rolled chrono for macro-benchmarks
- Runtime parameterization via CLI args (--rows, --profile, --output, etc.)
- JSON structured output from all C++ executables
- Results stored in `benchmark/results/<hostname>/<YYYY.MM.DD_HH.MM>/`

---

## Phase 1 — MVP: Fair CSV + Key Datasets + JSON Output + Basic Report
**Status: DONE**

### Deliverables
- [x] **Fair CSV baseline** — `CsvWriter` using `Row::visitConst()` + `std::to_chars()` buffer,
  `CsvReader` using `std::from_chars()` for all types (zero heap alloc per numeric cell)
- [x] **4 dataset profiles** — mixed_generic (72 col), sparse_events (100 col),
  sensor_noisy (50 col), string_heavy (30 col)
- [x] **Macro benchmark** (`bench_macro_datasets`) — CSV / BCSV Flexible / BCSV ZoH per profile,
  full write→read→validate, JSON output, CLI args
- [x] **Micro benchmark** (`bench_micro_types`) — Google Benchmark, per-type Get/Set,
  VisitConst, CsvWriteRow, Serialize/Deserialize
- [x] **CMake integration** — `BCSV_ENABLE_BENCHMARKS`, `BCSV_ENABLE_MICRO_BENCHMARKS`,
  Google Benchmark v1.8.3 via FetchContent
- [x] **Python orchestrator** (`benchmark/run_benchmarks.py`) — build, run, collect, summarize
- [x] **Python report generator** (`benchmark/report_generator.py`) — Markdown + Matplotlib

### Files
| File | Purpose |
|------|---------|
| `tests/bench_common.hpp` | Shared infra: Timer, PlatformInfo, CsvWriter, CsvReader, RoundTripValidator, JSON output, CLI args |
| `tests/bench_datasets.hpp` | Dataset profiles + deterministic data generators |
| `tests/bench_macro_datasets.cpp` | Macro benchmark executable |
| `tests/bench_micro_types.cpp` | Google Benchmark micro-benchmarks |
| `tests/CMakeLists.txt` | Build targets (modified) |
| `benchmark/run_benchmarks.py` | Python orchestrator |
| `benchmark/report_generator.py` | Matplotlib + Markdown report |

---

## Phase 2 — Full Dataset Coverage + CLI Tool Benchmarks + Persistence
**Status: DONE**

### 2.1 — Additional dataset profiles
Add profiles requested in ToDo.txt that aren't yet covered:
- [x] `bool_heavy` — 128 bool columns + few scalars, tests bitset performance
- [x] `arithmetic_wide` — 200 numeric columns (int + float mix), no strings, worst-case for ZoH (volatile)
- [x] `simulation_smooth` — 100 float/double columns, slow linear drift (first-order-hold friendly)
- [x] `weather_timeseries` — 40 columns, realistic weather pattern: temp, humidity, wind, pressure + string station IDs
- [x] `high_cardinality_string` — 50 string columns, unique UUIDs (worst case for string compression)

### 2.2 — Size variants
Add `--size=S|M|L|XL` CLI flag to `bench_macro_datasets`:
- [x] S = 10K rows (quick smoke test, ~1 sec)
- [x] M = 100K rows (default, ~10 sec)
- [x] L = 500K rows (the original benchmark_large scale)
- [x] XL = 2M rows (stress test, file > RAM-cache)

### 2.3 — CLI tool benchmarks (`--benchmark` flag)
- [x] `--benchmark` flag prints timing (wall clock, rows/sec, MB/sec) to stderr
- [x] `--benchmark --json` emits JSON timing blob to stdout
- [x] Added to both `bcsv2csv` and `csv2bcsv`

### 2.4 — CLI tool benchmark integration
- [x] `bench_generate_csv` utility: generates reference CSV from any profile
- [x] Python orchestrator drives: generate CSV → csv2bcsv → bcsv2csv → validate row count
- [x] JSON results stored in `cli_results.json`
- Note: Uses `--no-zoh` because `bcsv2csv` uses `ReaderDirectAccess` which doesn't support ZoH files

### 2.5 — Persistence and cleanup
- [x] Orchestrator stores results in `benchmark/results/<hostname>/<timestamp>/`
- [x] CLI tool benchmark cleans temp files after collecting results
- [x] `benchmark/.gitignore` excludes `runs/`
- [x] Manifest includes all result types (macro, micro, cli)

---

## Phase 3 — External Comparison, Reporting, Regression Detection
**Status: DONE**

### 3.1 — External CSV library comparison
- [x] Optional CMake flag: `BCSV_ENABLE_EXTERNAL_CSV_BENCH`
- [x] FetchContent download of [vincentlaucsb/csv-parser](https://github.com/vincentlaucsb/csv-parser) v2.3.0
- [x] New benchmark executable: `bench_external_csv` — read-only benchmark
  (external lib reads BCSV-generated CSV files, measures parse time)
- [x] Uses `get_sv()` + `std::from_chars()` for numeric types (fairest comparison:
  matches BCSV CsvReader's approach, avoids csv-parser overflow checks)
- [x] Results included in JSON output alongside BCSV numbers
- [x] Orchestrator discovers and runs `bench_external_csv`, stores `external_results.json`

### 3.2 — Enhanced reporting
- [x] Python report generator produces:
  - Bar charts: total_time (stacked), file_size, throughput (grouped), compression (horizontal)
  - Speedup table: BCSV vs CSV (write/read/total/size)
  - Compression ratio table
  - Micro-benchmark summary
  - CLI tool timing summary
  - External CSV comparison table
- [x] Markdown output embeds chart PNGs inline
- [x] Consistent color palette across all chart types

### 3.3 — Regression detection
- [x] `benchmark/compare_runs.py` script:
  - Loads two run directories (or `--latest` for most recent pair)
  - Computes deltas: % change in write/read/total time, file size
  - Flags regressions > `--threshold` (default 5%)
  - Produces diff report (Markdown or `--json`)
  - Saves report to file via `--output`
- [x] Persistent leaderboard: saves alongside candidate run directory
  - Best-ever per (profile × mode) with git version tag and timestamp
  - `--leaderboard` display mode, `--update-leaderboard` to record new bests

---

## Phase 4 — Polish, CI Integration, Documentation
**Status: DONE**

### 4.1 — CI integration
- [x] GitHub Actions workflow: `.github/workflows/benchmark.yml`
- [x] Runs `sweep` mode on every PR (S size, ~30 sec)
- [x] Runs `sweep` mode on release tags (L size) with external CSV comparison
- [x] Manual dispatch with configurable `--size` and `--mode`
- [x] Uploads JSON results + report + charts as artifacts
- [x] Posts summary to PR via `$GITHUB_STEP_SUMMARY`

### 4.2 — Documentation
- [x] Updated `tests/README.md` with benchmark suite architecture, profiles, CMake options,
  running instructions, output file descriptions
- [x] Updated project `README.md` with benchmark section + quick-start commands
- [x] Marked completed items in tests/README.md planned improvements

### 4.3 — Cleanup
- [x] Legacy `benchmark_large.cpp` and `benchmark_performance.cpp` behind
  `BCSV_ENABLE_LEGACY_BENCHMARKS` flag (OFF by default) with deprecation notices
- [x] Added deprecation README to `tmp/` directory
- [x] Version-stamped JSON output format (`format_version: 2`)

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-02-11 | Concept B + C elements | Modular = maintainable, data-driven = flexible |
| 2026-02-11 | Google Benchmark for micro only | GB adds value for ns-precision; overkill for multi-second macro runs |
| 2026-02-11 | visit()-based CSV | Fair comparison: same type-dispatch path as real CSV libs |
| 2026-02-12 | Phase 1 complete | All 4 profiles pass round-trip validation, both executables build |
| 2026-02-12 | CsvWriter → to_chars buffer | 2.3-3.4x faster CSV write; eliminates ostream locale/vtable overhead |
| 2026-02-12 | CsvReader → from_chars for float/double | Zero heap alloc per numeric cell; C++20 GCC 11+ supported |
| 2026-02-13 | csv-parser v2.3.0 via FetchContent | Popular C++ CSV lib with mmap I/O; used get_sv+from_chars for fairness |
| 2026-02-13 | Phase 3 complete | External comparison, enhanced charts, regression detection all validated |
| 2026-02-13 | Phase 4 complete | CI workflow, docs, legacy deprecation, JSON version stamp |
| 2026-02-13 | Full-360 orchestrator | `run_benchmarks.py` default: clean rebuild → run (pinned) → report → leaderboard → summary. Flags inverted: `--no-build`, `--no-report`, `--no-pin` |
