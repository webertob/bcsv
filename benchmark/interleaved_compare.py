#!/usr/bin/env python3
"""
Create a markdown comparison report for interleaved baseline/candidate runs.

Expected layout:
    <baseline_root>/<timestamp>/macro_small_results.json
    <candidate_root>/<timestamp>/macro_small_results.json

The comparison matrix is built only from workload instances that exist in both
baseline and candidate runs for the selected scenario.

Typical usage:
  python3 benchmark/interleaved_compare.py \
    --baseline-root benchmark/results/<host>/clean_interleaved \
    --candidate-root benchmark/results/<host>/wip_interleaved
"""

from __future__ import annotations

import argparse
import json
import socket
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lib.constants import mode_base  # noqa: E402


@dataclass(frozen=True)
class MetricSpec:
    label: str
    mode: str
    key: str
    value_kind: str


MACRO_METRICS = [
    MetricSpec("CSV Dense Write Time Δ (WIP vs Baseline)", "CSV", "write_rows_per_sec", "throughput"),
    MetricSpec("Flex Flat Dense Write Time Δ (WIP vs Baseline)", "BCSV Flexible", "write_rows_per_sec", "throughput"),
    MetricSpec("Flex-ZoH Dense Write Time Δ (WIP vs Baseline)", "BCSV Flexible ZoH", "write_rows_per_sec", "throughput"),
    MetricSpec("Static Flat Dense Write Time Δ (WIP vs Baseline)", "BCSV Static", "write_rows_per_sec", "throughput"),
    MetricSpec("Static-ZoH Dense Write Time Δ (WIP vs Baseline)", "BCSV Static ZoH", "write_rows_per_sec", "throughput"),
    MetricSpec("Flex-ZoH Compression Ratio Δ (WIP vs Baseline)", "BCSV Flexible ZoH", "compression_ratio", "relative"),
]


@dataclass(frozen=True)
class MicroMetricSpec:
    label: str
    matcher: Callable[[str], bool]


MICRO_METRICS = [
    MicroMetricSpec("Get Latency Δ (WIP vs Baseline)", lambda name: name.startswith("BM_Get_")),
    MicroMetricSpec("Set Latency Δ (WIP vs Baseline)", lambda name: name.startswith("BM_Set_")),
    MicroMetricSpec("Visit Latency Δ (WIP vs Baseline)", lambda name: "Visit" in name),
    MicroMetricSpec("Serialize Latency Δ (WIP vs Baseline)", lambda name: "Serialize" in name),
]

RUN_TYPE_TO_RESULT_FILE = {
    "MICRO": "micro_results.json",
    "MACRO-SMALL": "macro_small_results.json",
    "MACRO-LARGE": "macro_large_results.json",
}


def _is_ok_row(row: dict) -> bool:
    return str(row.get("status", "ok")).lower() == "ok"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create pair-wise interleaved benchmark comparison report")
    parser.add_argument("--baseline-root", required=True, help="Folder with interleaved baseline run directories")
    parser.add_argument("--candidate-root", required=True, help="Folder with interleaved candidate run directories")
    parser.add_argument("--run-type", default="MACRO-SMALL", help="Run type in condensed_metrics rows_by_type (default: MACRO-SMALL)")
    parser.add_argument("--scenario", default="baseline", help="Scenario id to score (default: baseline)")
    parser.add_argument("--expected-pairs", type=int, default=5, help="Expected count of baseline/candidate pairs (default: 5)")
    parser.add_argument("--output", default=None, help="Output markdown file path (default: benchmark/results/<host>/interleaved_<N>x_comparison.md)")
    return parser.parse_args()


def pct_delta_relative(candidate: float | None, baseline: float | None) -> float | None:
    if candidate is None or baseline is None or baseline == 0:
        return None
    return ((candidate - baseline) / baseline) * 100.0


def pct_delta_time_from_throughput(candidate: float | None, baseline: float | None) -> float | None:
    if candidate is None or baseline is None or candidate == 0:
        return None
    return ((baseline / candidate) - 1.0) * 100.0


def fmt_pct(value: float | None) -> str:
    if value is None:
        return "—"
    return f"{value:+.1f}%"


def workload_key(row: dict) -> tuple:
    return (
        str(row.get("dataset", "?")),
        str(row.get("scenario_id", "baseline")),
        str(row.get("access_path", "-")),
        int(row.get("selected_columns", row.get("num_columns", 0))),
        int(row.get("num_columns", 0)),
        str(row.get("mode", "?")),
    )


def profile_name(dataset_name: str) -> str:
    return dataset_name.split("::", 1)[0]


def load_macro_rows(run_dir: Path, run_type: str) -> list[dict]:
    result_file = RUN_TYPE_TO_RESULT_FILE.get(run_type.upper())
    if result_file is None:
        raise ValueError(f"Unsupported --run-type '{run_type}'. Supported: {', '.join(sorted(RUN_TYPE_TO_RESULT_FILE.keys()))}")
    payload_path = run_dir / result_file
    if not payload_path.exists():
        raise ValueError(f"Missing macro results file: {payload_path}")
    payload = json.loads(payload_path.read_text())
    rows = payload.get("results", [])
    if not isinstance(rows, list):
        raise ValueError(f"Invalid results payload in: {payload_path}")
    return [row for row in rows if isinstance(row, dict) and _is_ok_row(row)]


def load_micro_map(run_dir: Path) -> dict[str, float]:
    payload_path = run_dir / RUN_TYPE_TO_RESULT_FILE["MICRO"]
    if not payload_path.exists():
        raise ValueError(f"Missing micro results file: {payload_path}")
    payload = json.loads(payload_path.read_text())
    rows = payload.get("benchmarks", [])
    if not isinstance(rows, list):
        raise ValueError(f"Invalid micro payload in: {payload_path}")

    out: dict[str, float] = {}
    for row in rows:
        name = row.get("name")
        value = row.get("real_time")
        if isinstance(name, str) and isinstance(value, (int, float)):
            out[name] = float(value)
    return out


def rows_to_map(rows: list[dict]) -> dict[tuple, dict]:
    return {workload_key(row): row for row in rows}


def profile_sets(rows: list[dict], scenario: str) -> set[str]:
    names = set()
    for row in rows:
        if str(row.get("scenario_id", "baseline")) != scenario:
            continue
        names.add(profile_name(str(row.get("dataset", "?"))))
    return names


def metric_delta_from_common(
    baseline_map: dict[tuple, dict],
    candidate_map: dict[tuple, dict],
    spec: MetricSpec,
    scenario: str,
) -> tuple[float | None, int]:
    baseline_keys = {
        key for key, row in baseline_map.items()
        if key[1] == scenario and mode_base(str(row.get("mode", "?"))) == spec.mode and isinstance(row.get(spec.key), (int, float))
    }
    candidate_keys = {
        key for key, row in candidate_map.items()
        if key[1] == scenario and mode_base(str(row.get("mode", "?"))) == spec.mode and isinstance(row.get(spec.key), (int, float))
    }
    common_keys = sorted(baseline_keys & candidate_keys)
    if not common_keys:
        return None, 0

    baseline_values = [float(baseline_map[key][spec.key]) for key in common_keys]
    candidate_values = [float(candidate_map[key][spec.key]) for key in common_keys]
    baseline_med = float(statistics.median(baseline_values))
    candidate_med = float(statistics.median(candidate_values))
    if spec.value_kind == "throughput":
        delta = pct_delta_time_from_throughput(candidate_med, baseline_med)
    else:
        delta = pct_delta_relative(candidate_med, baseline_med)
    return delta, len(common_keys)


def micro_metric_delta_from_common(
    baseline_map: dict[str, float],
    candidate_map: dict[str, float],
    spec: MicroMetricSpec,
) -> tuple[float | None, int]:
    baseline_keys = {name for name in baseline_map if spec.matcher(name)}
    candidate_keys = {name for name in candidate_map if spec.matcher(name)}
    common_keys = sorted(baseline_keys & candidate_keys)
    if not common_keys:
        return None, 0

    baseline_values = [baseline_map[name] for name in common_keys]
    candidate_values = [candidate_map[name] for name in common_keys]
    baseline_med = float(statistics.median(baseline_values))
    candidate_med = float(statistics.median(candidate_values))
    return pct_delta_relative(candidate_med, baseline_med), len(common_keys)


def resolve_output_path(output_arg: str | None, expected_pairs: int, run_type: str) -> Path:
    if output_arg:
        return Path(output_arg)
    host = socket.gethostname()
    run_type_slug = run_type.strip().lower().replace("-", "_")
    return Path(f"benchmark/results/{host}/interleaved_{expected_pairs}x_{run_type_slug}_comparison.md")


def main() -> int:
    args = parse_args()

    baseline_root = Path(args.baseline_root)
    candidate_root = Path(args.candidate_root)
    output_path = resolve_output_path(args.output, args.expected_pairs, args.run_type)

    if not baseline_root.exists() or not baseline_root.is_dir():
        raise SystemExit(f"Baseline root not found: {baseline_root}")
    if not candidate_root.exists() or not candidate_root.is_dir():
        raise SystemExit(f"Candidate root not found: {candidate_root}")

    baseline_runs = sorted([path for path in baseline_root.iterdir() if path.is_dir()])
    candidate_runs = sorted([path for path in candidate_root.iterdir() if path.is_dir()])

    if len(baseline_runs) != args.expected_pairs or len(candidate_runs) != args.expected_pairs:
        raise SystemExit(
            f"Expected {args.expected_pairs} pairs, got baseline={len(baseline_runs)} candidate={len(candidate_runs)}"
        )

    is_micro = args.run_type.upper() == "MICRO"
    metric_labels = [metric.label for metric in (MICRO_METRICS if is_micro else MACRO_METRICS)]

    pair_rows = []
    baseline_profiles_all = set()
    candidate_profiles_all = set()
    for idx, (baseline, candidate) in enumerate(zip(baseline_runs, candidate_runs), start=1):
        baseline_profiles: set[str] = set()
        candidate_profiles: set[str] = set()

        deltas = {}
        coverage = {}
        if is_micro:
            baseline_map = load_micro_map(baseline)
            candidate_map = load_micro_map(candidate)
            for spec in MICRO_METRICS:
                delta, common_count = micro_metric_delta_from_common(
                    baseline_map=baseline_map,
                    candidate_map=candidate_map,
                    spec=spec,
                )
                deltas[spec.label] = delta
                coverage[spec.label] = common_count
        else:
            baseline_rows = load_macro_rows(baseline, args.run_type.upper())
            candidate_rows = load_macro_rows(candidate, args.run_type.upper())
            baseline_map = rows_to_map(baseline_rows)
            candidate_map = rows_to_map(candidate_rows)

            baseline_profiles = profile_sets(baseline_rows, args.scenario)
            candidate_profiles = profile_sets(candidate_rows, args.scenario)
            baseline_profiles_all.update(baseline_profiles)
            candidate_profiles_all.update(candidate_profiles)

            for spec in MACRO_METRICS:
                delta, common_count = metric_delta_from_common(
                    baseline_map=baseline_map,
                    candidate_map=candidate_map,
                    spec=spec,
                    scenario=args.scenario,
                )
                deltas[spec.label] = delta
                coverage[spec.label] = common_count

        pair_rows.append({
            "pair": idx,
            "baseline": baseline,
            "candidate": candidate,
            "deltas": deltas,
            "coverage": coverage,
            "baseline_only_profiles": sorted(baseline_profiles - candidate_profiles),
            "candidate_only_profiles": sorted(candidate_profiles - baseline_profiles),
        })

    common_profiles_all = sorted(baseline_profiles_all & candidate_profiles_all)
    baseline_only_profiles_all = sorted(baseline_profiles_all - candidate_profiles_all)
    candidate_only_profiles_all = sorted(candidate_profiles_all - baseline_profiles_all)

    output_path.parent.mkdir(parents=True, exist_ok=True)

    lines = []
    lines.append(f"# Interleaved {args.expected_pairs}x Benchmark Comparison ({args.run_type.upper()})")
    lines.append("")
    if is_micro:
        lines.append("Delta is WIP vs Baseline.")
        lines.append("Positive latency delta means WIP is slower; negative means WIP is faster.")
        lines.append("Aggregate score/comparison matrix is computed only from benchmark names present in both baseline and candidate runs.")
    else:
        lines.append("Delta is WIP vs Baseline.")
        lines.append("For write-time metrics (derived from rows/s), positive means WIP is slower; negative means WIP is faster.")
        lines.append("For compression ratio, positive means WIP files are larger; negative means smaller.")
        lines.append(f"Scoring scenario: `{args.scenario}`")
        lines.append("Aggregate score/comparison matrix is computed **only** from workload instances present in both baseline and candidate runs.")
    lines.append("")

    lines.append("| Pair | Baseline Run | Candidate Run | " + " | ".join(metric_labels) + " |")
    lines.append("|------|--------------|---------------|" + "|".join(["------------------:" for _ in metric_labels]) + "|")
    for row in pair_rows:
        deltas = [fmt_pct(row["deltas"].get(label)) for label in metric_labels]
        lines.append(
            f"| {row['pair']} | {row['baseline'].name} | {row['candidate'].name} | " + " | ".join(deltas) + " |"
        )

    lines.append("")
    if not is_micro:
        lines.append("## Workload Coverage")
        lines.append("")
        lines.append(f"- Common workloads included in score ({len(common_profiles_all)}): " + (", ".join(common_profiles_all) if common_profiles_all else "none"))
        lines.append(f"- Baseline-only workloads excluded ({len(baseline_only_profiles_all)}): " + (", ".join(baseline_only_profiles_all) if baseline_only_profiles_all else "none"))
        lines.append(f"- Candidate-only workloads excluded ({len(candidate_only_profiles_all)}): " + (", ".join(candidate_only_profiles_all) if candidate_only_profiles_all else "none"))

    lines.append("")
    lines.append("## Common Item Count by Metric")
    lines.append("")
    lines.append("| Metric | Median Common Items | Min | Max |")
    lines.append("|--------|------------------------:|----:|----:|")
    for label in metric_labels:
        values = [int(row["coverage"].get(label, 0)) for row in pair_rows]
        if values:
            lines.append(f"| {label} | {int(statistics.median(values))} | {min(values)} | {max(values)} |")
        else:
            lines.append(f"| {label} | 0 | 0 | 0 |")

    lines.append("")
    lines.append(f"## Aggregate Across {args.expected_pairs} Pairs")
    lines.append("")
    lines.append("| Metric | Median Δ | Min Δ | Max Δ |")
    lines.append("|--------|---------:|------:|------:|")
    for label in metric_labels:
        values = [row["deltas"].get(label) for row in pair_rows]
        values = [value for value in values if value is not None]
        if values:
            lines.append(
                f"| {label} | {fmt_pct(statistics.median(values))} | {fmt_pct(min(values))} | {fmt_pct(max(values))} |"
            )
        else:
            lines.append(f"| {label} | — | — | — |")

    lines.append("")
    lines.append("## Run Directories")
    for row in pair_rows:
        lines.append(f"- baseline: {row['baseline']}")
        lines.append(f"- candidate: {row['candidate']}")

    output_path.write_text("\n".join(lines) + "\n")

    print("Interleaved comparison summary")
    print(f"  Pairs: {args.expected_pairs}")
    print(f"  Run type: {args.run_type.upper()}")
    if not is_micro:
        print(f"  Scenario: {args.scenario}")
    print(f"  Baseline root: {baseline_root}")
    print(f"  Candidate root: {candidate_root}")
    print(f"  Output: {output_path}")
    if not is_micro:
        print(f"  Common workloads included ({len(common_profiles_all)}): {', '.join(common_profiles_all) if common_profiles_all else 'none'}")
        print(f"  Baseline-only workloads excluded ({len(baseline_only_profiles_all)}): {', '.join(baseline_only_profiles_all) if baseline_only_profiles_all else 'none'}")
        print(f"  Candidate-only workloads excluded ({len(candidate_only_profiles_all)}): {', '.join(candidate_only_profiles_all) if candidate_only_profiles_all else 'none'}")
    print("  Aggregate deltas (WIP vs Baseline):")
    for label in metric_labels:
        values = [row["deltas"].get(label) for row in pair_rows]
        values = [value for value in values if value is not None]
        if not values:
            print(f"    {label:<30} median=— min=— max=—")
            continue
        print(
            f"    {label:<30} median={fmt_pct(statistics.median(values)):>7} "
            f"min={fmt_pct(min(values)):>7} max={fmt_pct(max(values)):>7}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
