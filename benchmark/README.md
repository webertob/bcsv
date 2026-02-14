# Benchmark Suite

Human-facing entry point for running and comparing BCSV benchmarks.

For full procedures, see:
- `benchmark/OPERATOR_GUIDE.md` (campaign workflow)
- `benchmark/SKILLS.md` (detailed commands/options)

## Quick Commands

```bash
# Smoke run
python3 benchmark/run_benchmarks.py --size=S

# Script-friendly low-noise run (keeps files, suppresses final summary/listing)
python3 benchmark/run_benchmarks.py --size=S --no-report --quiet --quiet-summary

# Print only prepared worktree path for historical benchmark setup
python3 benchmark/run_benchmarks.py --git-commit=<sha> --print-worktree-path-only --quiet
```

## Output Location

Results are written under:

`benchmark/results/<hostname>/<timestamp>/`

with canonical artifacts like `macro_results.json`, `micro_results.json`, `cli_results.json`, `external_results.json`, `platform.json`, and `manifest.json`.
