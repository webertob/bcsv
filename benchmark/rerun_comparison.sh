#!/usr/bin/env bash
# Sequential re-run: baseline (committed HEAD) then WIP, both on CPU 2, size=L, 5 reps.
# Run from: cd /home/tobias/ws/bcsv/benchmark && bash rerun_comparison.sh
set -euo pipefail
cd "$(dirname "$0")"

echo "=== Baseline (HEAD commit) — CPU 2, 5 reps, size=L ==="
python run_benchmarks.py --git-commit=HEAD --size=L --repeat=5 --pin-cpu=2 \
  --output-dir=results/WorkhorseLNX/clean_8faec05_L_v2

echo ""
echo "=== WIP (uncommitted) — CPU 2, 5 reps, size=L ==="
python run_benchmarks.py --size=L --repeat=5 --pin-cpu=2 \
  --output-dir=results/WorkhorseLNX/wipcmp_8faec05_L_v2

echo ""
echo "=== Comparison ==="
python compare_runs.py \
  results/WorkhorseLNX/clean_8faec05_L_v2 \
  results/WorkhorseLNX/wipcmp_8faec05_L_v2 \
  --output=results/WorkhorseLNX/wip_vs_clean_v2.md

echo "Done. Report: results/WorkhorseLNX/wip_vs_clean_v2.md"
