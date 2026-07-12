# Benchmark Noise-Floor Evaluation — 2026-07-12

Machine: Ryzen 9 9950X3D (16C/32T, 2 CCDs). Sweep: full micro + macro suite,
all 15 profiles, all 9 scenarios, three row counts (S=10 k, M=100 k, L=500 k),
**5 repetitions, interleaved, 8 parallel workers** pinned to CPUs 0–7 (one CCD,
uniform V-cache L3, SMT siblings idle, one core per worker — main + batch-codec
BG thread share the core, matching the single-core pinning of the baseline
campaign). Each track ran in its own working directory (bench temp files are
CWD-relative). After the parallel phase: **solo anchors** (1 rep of full
MACRO-S, MACRO-L, micro on CPU2, idle machine) to separate code effects from
regime effects. Driver: `tmp/noisefloor/run_sweep.py`; analysis:
`tmp/noisefloor/analyze_noise.py`; raw data: `tmp/noisefloor/results/`.

## 1. Noise floor (per-metric CV across repetitions, parallel regime)

| Group | median CV | p75 | p90 | max |
|---|---|---|---|---|
| macro L read | 5.6 % | 7.2 % | 8.7 % | 13 % |
| macro L write | 6.7 % | 8.9 % | 12.5 % | 42 % |
| macro M read | 6.0 % | 8.3 % | 10.8 % | 22 % |
| macro M write | 7.7 % | 11.3 % | 15.8 % | 49 % |
| macro S read | 8.5 % | 15.2 % | 20.2 % | 42 % |
| macro S write | 9.1 % | 16.1 % | 22.5 % | 53 % |
| micro real_time | 6.6 % | 8.4 % | 10.6 % | 13 % |

**With warm-up excluded (reps 2–5 only): virtually unchanged** (all-macro read
6.0 → 5.6 %, write 7.4 → 7.0 %). The parallel-regime noise is *not*
warm-up-dominated — it is cross-worker interference (shared L3/memory
bandwidth, all-core boost budget).

## 2. Warm-up effect (rep 1 vs median of reps 2–5)

| Size | median dev | p10 (worst tail) | rep1 slower in |
|---|---|---|---|
| L | +0.5…+0.8 % | −9…−11 % | ~47 % of metrics |
| M | −2.7…−2.9 % | −16…−18 % | ~65 % |
| S | +0.5 % | −22…−23 % | ~43 % |

Interpretation: there is **no strong systematic warm-up** at repetition
granularity — the median first-rep penalty is ≤ 3 %. But a *tail* of
metrics (~10–15 %) shows a 10–23 % slower first repetition (cold page cache,
first file creation, frequency ramp). Recommendation: run ≥ 3 reps and use
the median (robust to the tail), or discard rep 1 when only means are used.

## 3. Parallel-regime offset (same binary: solo anchor vs parallel warm median)

| | read | write |
|---|---|---|
| size S | solo **+7.5 %** faster (p10 +4.4, p90 +19) | **+8.1 %** (p90 +22) |
| size L | solo **+11.1 %** faster (p10 +4.2, p90 +18) | **+14.4 %** (p90 +23) |

8-way parallel execution costs 7–14 % median throughput vs a solo pinned run
and adds spread. **Numbers from parallel sweeps must never be compared
against solo-run numbers without regime-matched anchors.**

## 4. Comparison vs the pre-1.5.10 baseline (2026-07-11 campaign)

Regime-matched: solo CPU2 anchors (current code, cycles A+B+C applied) vs the
3-rep solo CPU2 baseline medians. Median delta across all profiles ×
scenarios per mode family:

| Mode | S write | S read | L write | L read |
|---|---|---|---|---|
| CSV | −0.7 % | −0.3 % | +1.0 % | +0.1 % |
| Flexible Dense | −1.0 % | +0.9 % | −0.3 % | +0.3 % |
| Flexible ZoH | +0.1 % | −0.8 % | +0.2 % | −0.5 % |
| Flexible Delta | −0.0 % | +1.3 % | −0.3 % | **+1.7 %** |
| Static Dense | +0.5 % | −0.8 % | +1.1 % | −0.8 % |
| Static ZoH | +0.2 % | −0.5 % | +1.6 % | −0.6 % |
| Static Delta | +1.7 % | −1.2 % | +1.7 % | −0.6 % |
| ZoH [generic] | −2.3 % | −2.8 % | −1.7 % | −2.5 % |

**Verdict: no regression from the 1.5.10 work (cycles A + B + C).** Every
family is within ±1.7 % except ZoH-generic (−2.5 %, n=36, single-anchor
uncertainty; worth one eye at the release gate). Flexible Delta reads are
+1.3…+1.7 % — consistent with the B2 investigation's unrolling win.

Micro anchors (solo): aggregate benchmarks (CsvWriteRow, VisitConst,
Serialize/Deserialize, RowConstruct) within ±5 % — Deserialize 12-col
**−18.5 % (faster)**, matching the B2 clamp win. The sub-nanosecond
`Get_*`/`Set_*` benchmarks (0.4–0.6 ns ≈ 2–3 cycles) swing ±30 % between any
two runs and are **not usable as regression gates** at single-run
granularity; the parallel-regime micro numbers are uniformly inflated
(+12…+60 %) and should be ignored entirely.

## 5. Methodology recommendations (feeding D2 / release gate)

1. **Parallel sweeps** (8 workers): good for coverage smoke tests and
   catching regressions > ~15–20 %; unusable for verdicts in the ±10 % band.
2. **A/B verdicts** need solo, pinned, *interleaved* runs; reads are the
   trustworthy metric (solo interleaved noise ≈ ±1–2 % read vs ±5–7 % write
   from binary-layout luck — see B2_VALIDATION_COST_INVESTIGATION.md).
3. **Aggregate before judging**: a per-mode median across 135
   profile×scenario metrics is far tighter than any single metric; single
   metrics need > p90-CV deltas (≈ 9–22 % depending on size) to be signal.
4. **Size matters**: S (10 k rows, ~ms runs) is 1.5–2× noisier than L —
   prefer M/L for gating, S for smoke.
5. **Warm-up**: median-of-≥3-reps is sufficient; drop rep 1 if using means.
6. **Micro suite**: gate only on the aggregate ops (≥ ~10 ns); treat sub-ns
   accessor benchmarks as informational.
