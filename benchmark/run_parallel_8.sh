#!/usr/bin/env bash
# Parallel benchmark launcher: 8 jobs (4 configs × 2 builds), 5 reps, no pinning.
#
# Configs: sweep+S, sweep+L, full+S, full+L
# Builds:  WIP (current), clean (8faec05)
#
# Usage: bash benchmark/run_parallel_8.sh

set -euo pipefail

WIP_ROOT="/home/tobias/ws/bcsv"
CLEAN_ROOT="/tmp/bcsv_bench_worktrees/20260215_204132_8faec0568d0b"
RESULTS_BASE="${WIP_ROOT}/benchmark/results/WorkhorseLNX"
STAMP=$(date +%Y.%m.%d_%H.%M)

REPEAT=5
COMMON="--no-build --no-pin --no-report --quiet --quiet-summary --repeat=${REPEAT}"

echo "=== Parallel benchmark launch: ${STAMP} ==="
echo "  WIP root:   ${WIP_ROOT}"
echo "  Clean root:  ${CLEAN_ROOT}"
echo "  Repeat:      ${REPEAT}"
echo "  Jobs:        8"
echo ""

PIDS=()
LABELS=()

launch() {
  local label="$1"; shift
  local root="$1"; shift
  local outdir="$1"; shift
  mkdir -p "${outdir}"
  echo "  Launching: ${label} → ${outdir##*/}"
  python3 "${root}/benchmark/run_benchmarks.py" ${COMMON} "$@" \
    --output-dir="${outdir}" \
    > "${outdir}/launch.log" 2>&1 &
  PIDS+=($!)
  LABELS+=("${label}")
}

# 4 WIP jobs
launch "wip_sweep_S"  "${WIP_ROOT}"   "${RESULTS_BASE}/wip_perf_sweep_S"   --mode=sweep --size=S
launch "wip_sweep_L"  "${WIP_ROOT}"   "${RESULTS_BASE}/wip_perf_sweep_L"   --mode=sweep --size=L
launch "wip_full_S"   "${WIP_ROOT}"   "${RESULTS_BASE}/wip_perf_full_S"    --mode=full  --size=S
launch "wip_full_L"   "${WIP_ROOT}"   "${RESULTS_BASE}/wip_perf_full_L"    --mode=full  --size=L

# 4 Clean jobs (run orchestrator from the worktree so it discovers its own build_release/bin/)
launch "clean_sweep_S" "${CLEAN_ROOT}" "${RESULTS_BASE}/clean_perf_sweep_S" --mode=sweep --size=S
launch "clean_sweep_L" "${CLEAN_ROOT}" "${RESULTS_BASE}/clean_perf_sweep_L" --mode=sweep --size=L
launch "clean_full_S"  "${CLEAN_ROOT}" "${RESULTS_BASE}/clean_perf_full_S"  --mode=full  --size=S
launch "clean_full_L"  "${CLEAN_ROOT}" "${RESULTS_BASE}/clean_perf_full_L"  --mode=full  --size=L

echo ""
echo "  All 8 jobs launched.  Waiting..."
echo ""

FAIL=0
for i in "${!PIDS[@]}"; do
  pid="${PIDS[$i]}"
  label="${LABELS[$i]}"
  if wait "${pid}"; then
    echo "  ✓ ${label} (PID ${pid})"
  else
    echo "  ✗ ${label} (PID ${pid}) — FAILED"
    FAIL=$((FAIL + 1))
  fi
done

echo ""
if [ "${FAIL}" -eq 0 ]; then
  echo "=== All 8 jobs completed successfully ==="
else
  echo "=== ${FAIL} job(s) failed — check launch.log files ==="
fi

echo ""
echo "Results:"
for d in "${RESULTS_BASE}"/wip_perf_* "${RESULTS_BASE}"/clean_perf_*; do
  if [ -d "$d" ]; then
    macro="${d}/macro_results.json"
    if [ -f "$macro" ]; then
      echo "  $(basename "$d"): $(wc -c < "$macro") bytes"
    else
      echo "  $(basename "$d"): NO macro_results.json"
    fi
  fi
done
