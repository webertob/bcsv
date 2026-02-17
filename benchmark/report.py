#!/usr/bin/env python3
"""
BCSV benchmark reporting tool.

Modes:
1) Single-run summary report (with optional baseline comparison)
   python3 benchmark/report.py <run_dir>

2) Explicit run-vs-run comparison report
   python3 benchmark/report.py <run_dir> --baseline <baseline_dir>
"""

import argparse
import json
import statistics
import sys
from collections import OrderedDict
from datetime import datetime
from pathlib import Path


MODE_ALIASES = OrderedDict([
    ("CSV", ["CSV"]),
    ("BCSV Flex Flat (Standard)", ["BCSV Flexible", "BCSV Flat", "BCSV Standard", "BCSV Flexible Flat"]),
    ("BCSV Flex-ZoH", ["BCSV Flexible ZoH", "BCSV ZoH", "BCSV Flexible-ZoH"]),
    ("BCSV Static Flat (Standard)", ["BCSV Static", "BCSV Static Flat", "BCSV Static Standard"]),
    ("BCSV Static-ZoH", ["BCSV Static ZoH", "BCSV Static-ZoH"]),
])


def load_json(path: Path):
    return json.loads(path.read_text())


def load_macro_rows(run_dir: Path):
    path = run_dir / "macro_results.json"
    if not path.exists():
        return []
    payload = load_json(path)
    return payload.get("results", [])


def load_macro_rows_by_type(run_dir: Path):
    typed_files = {
        "MACRO-SMALL": run_dir / "macro_small_results.json",
        "MACRO-LARGE": run_dir / "macro_large_results.json",
    }

    out = {}
    for run_type, path in typed_files.items():
        if not path.exists():
            continue
        payload = load_json(path)
        out[run_type] = payload.get("results", [])

    if out:
        return out

    legacy_rows = load_macro_rows(run_dir)
    if legacy_rows:
        return {"MACRO": legacy_rows}
    return {}


def load_platform(run_dir: Path):
    path = run_dir / "platform.json"
    if not path.exists():
        return {}
    return load_json(path)


def load_micro(run_dir: Path):
    path = run_dir / "micro_results.json"
    if not path.exists():
        return {}
    return load_json(path)


def compute_condensed_progress_metrics(results):
    if not results:
        return []

    def pick_mode_rows(rows, aliases):
        return [row for row in rows if row.get("mode") in aliases]

    def med_std(values):
        if not values:
            return None, None
        med = statistics.median(values)
        stdev = statistics.stdev(values) if len(values) > 1 else 0.0
        return med, stdev

    csv_size_by_key = {}
    for row in results:
        if row.get("mode") == "CSV":
            key = (row.get("dataset"), row.get("scenario_id", "baseline"))
            csv_size_by_key[key] = row.get("file_size", 0)

    rows_out = []
    for display_mode, aliases in MODE_ALIASES.items():
        mode_rows = pick_mode_rows(results, aliases)
        dense_rows = [row for row in mode_rows if row.get("scenario_id", "baseline") == "baseline"]
        sparse_rows = [row for row in mode_rows if row.get("scenario_id", "baseline") != "baseline"]

        dense_write_rows_s = [row.get("write_rows_per_sec", 0.0) for row in dense_rows if row.get("write_rows_per_sec")]
        dense_read_rows_s = [row.get("read_rows_per_sec", 0.0) for row in dense_rows if row.get("read_rows_per_sec")]

        sparse_write_cells_s = []
        sparse_read_cells_s = []
        for row in sparse_rows:
            num_rows = float(row.get("num_rows", 0) or 0)
            proc_ratio = float(row.get("processed_row_ratio", 1.0) or 0)
            selected_cols = float(row.get("selected_columns", row.get("num_columns", 0)) or 0)
            effective_cells = num_rows * proc_ratio * selected_cols
            write_ms = float(row.get("write_time_ms", 0) or 0)
            read_ms = float(row.get("read_time_ms", 0) or 0)
            if write_ms > 0 and effective_cells > 0:
                sparse_write_cells_s.append(effective_cells / (write_ms / 1000.0))
            if read_ms > 0 and effective_cells > 0:
                sparse_read_cells_s.append(effective_cells / (read_ms / 1000.0))

        comp_ratios = []
        for row in mode_rows:
            if row.get("mode") == "CSV":
                comp_ratios.append(1.0)
                continue
            key = (row.get("dataset"), row.get("scenario_id", "baseline"))
            csv_size = csv_size_by_key.get(key, 0)
            mode_size = row.get("file_size", 0)
            if csv_size and mode_size is not None:
                comp_ratios.append(float(mode_size) / float(csv_size))

        comp_med, comp_std = med_std(comp_ratios)
        dw_med, dw_std = med_std(dense_write_rows_s)
        dr_med, dr_std = med_std(dense_read_rows_s)
        sw_med, sw_std = med_std(sparse_write_cells_s)
        sr_med, sr_std = med_std(sparse_read_cells_s)

        rows_out.append({
            "mode": display_mode,
            "compression_ratio_median": comp_med,
            "compression_ratio_stdev": comp_std,
            "dense_write_rows_per_sec_median": dw_med,
            "dense_write_rows_per_sec_stdev": dw_std,
            "dense_read_rows_per_sec_median": dr_med,
            "dense_read_rows_per_sec_stdev": dr_std,
            "sparse_write_cells_per_sec_median": sw_med,
            "sparse_write_cells_per_sec_stdev": sw_std,
            "sparse_read_cells_per_sec_median": sr_med,
            "sparse_read_cells_per_sec_stdev": sr_std,
        })

    return rows_out


def fmt_med_std(median_val, stdev_val, decimals=2):
    if median_val is None:
        return "—"
    if stdev_val is None:
        stdev_val = 0.0
    return f"{median_val:.{decimals}f} ± {stdev_val:.{decimals}f}"


def fmt_delta(current, baseline, higher_is_better):
    if current is None or baseline is None or baseline == 0:
        return "—"
    if higher_is_better:
        delta = ((current - baseline) / baseline) * 100.0
    else:
        delta = ((baseline - current) / baseline) * 100.0
    return f"{delta:+.1f}%"


def micro_group_medians(micro_payload):
    if not micro_payload:
        return {}
    benchmarks = micro_payload.get("benchmarks", [])
    groups = {
        "Get": [b.get("real_time") for b in benchmarks if str(b.get("name", "")).startswith("BM_Get_") and isinstance(b.get("real_time"), (int, float))],
        "Set": [b.get("real_time") for b in benchmarks if str(b.get("name", "")).startswith("BM_Set_") and isinstance(b.get("real_time"), (int, float))],
        "Visit": [b.get("real_time") for b in benchmarks if "Visit" in str(b.get("name", "")) and isinstance(b.get("real_time"), (int, float))],
        "Serialize": [b.get("real_time") for b in benchmarks if "Serialize" in str(b.get("name", "")) and isinstance(b.get("real_time"), (int, float))],
    }
    out = {}
    for label, values in groups.items():
        if values:
            out[label] = statistics.median(values)
    return out


def latest_clean_run(current_run: Path):
    host_root = current_run.parent.parent
    if not host_root.exists():
        return None

    candidates = []
    for git_bucket in host_root.iterdir():
        if not git_bucket.is_dir():
            continue
        if "wip" in git_bucket.name.lower():
            continue
        for run_dir in git_bucket.iterdir():
            if not run_dir.is_dir() or run_dir == current_run:
                continue
            has_macro = (run_dir / "macro_results.json").exists()
            has_micro = (run_dir / "micro_results.json").exists()
            if (run_dir / "platform.json").exists() and (has_macro or has_micro):
                candidates.append((run_dir.stat().st_mtime, run_dir))

    if not candidates:
        return None

    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def generate_summary_markdown(run_dir: Path, baseline_dir: Path | None, summary_only: bool):
    platform = load_platform(run_dir)
    candidate_label = str(platform.get("git_label", run_dir.name))
    current_macro_by_type = load_macro_rows_by_type(run_dir)
    micro_data = load_micro(run_dir)
    baseline_macro_by_type = {}
    baseline_micro = {}
    if baseline_dir is not None:
        baseline_macro_by_type = load_macro_rows_by_type(baseline_dir)
        baseline_micro = load_micro(baseline_dir)
    baseline_label = baseline_dir.name if baseline_dir is not None else None

    lines = []
    lines.append("# BCSV Benchmark Summary Report")
    lines.append("")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append("")

    lines.append("## Host")
    lines.append("")
    lines.append("| Property | Value |")
    lines.append("|----------|-------|")
    lines.append(f"| Hostname | {platform.get('hostname', 'N/A')} |")
    lines.append(f"| OS | {platform.get('os', 'N/A')} |")
    lines.append(f"| CPU | {platform.get('cpu_model', 'N/A')} |")
    lines.append(f"| Build | {platform.get('build_type', 'N/A')} |")
    lines.append(f"| Git Label | {platform.get('git_label', platform.get('git_describe', 'N/A'))} |")
    lines.append(f"| Run Types | {', '.join(platform.get('run_types', [])) if platform.get('run_types') else 'N/A'} |")
    lines.append(f"| Repetitions | {platform.get('repetitions', 'N/A')} |")
    lines.append(f"| CPU Pinning | {platform.get('pin', 'N/A')} |")
    lines.append("")

    condensed_sidecar = {}
    for macro_type in ["MACRO-SMALL", "MACRO-LARGE", "MACRO"]:
        current_macro_rows = current_macro_by_type.get(macro_type, [])
        if not current_macro_rows:
            continue

        current_condensed = compute_condensed_progress_metrics(current_macro_rows)
        condensed_sidecar[macro_type] = current_condensed

        lines.append(f"## Condensed Performance Matrix ({macro_type})")
        lines.append("")
        lines.append("| Mode | Compression Ratio vs CSV | Dense Write (rows/s) | Dense Read (rows/s) | Sparse Write (cells/s) | Sparse Read (cells/s) |")
        lines.append("|------|---------------------------:|---------------------:|--------------------:|-----------------------:|----------------------:|")

        current_by_mode = {row["mode"]: row for row in current_condensed}
        for mode in MODE_ALIASES.keys():
            row = current_by_mode.get(mode, {})
            lines.append(
                f"| {mode}"
                f" | {fmt_med_std(row.get('compression_ratio_median'), row.get('compression_ratio_stdev'), decimals=3)}"
                f" | {fmt_med_std(row.get('dense_write_rows_per_sec_median'), row.get('dense_write_rows_per_sec_stdev'), decimals=0)}"
                f" | {fmt_med_std(row.get('dense_read_rows_per_sec_median'), row.get('dense_read_rows_per_sec_stdev'), decimals=0)}"
                f" | {fmt_med_std(row.get('sparse_write_cells_per_sec_median'), row.get('sparse_write_cells_per_sec_stdev'), decimals=0)}"
                f" | {fmt_med_std(row.get('sparse_read_cells_per_sec_median'), row.get('sparse_read_cells_per_sec_stdev'), decimals=0)} |"
            )
        lines.append("")

        lines.append(f"## Condensed Performance Matrix Comparison ({macro_type})")
        lines.append("")
        baseline_macro_rows = baseline_macro_by_type.get(macro_type, [])
        if baseline_dir is None:
            lines.append("No baseline run available (latest git-clean run not found).")
            lines.append("")
        elif not baseline_macro_rows:
            lines.append(f"Baseline `{baseline_label}` has no {macro_type} data.")
            lines.append("")
        else:
            baseline_condensed = compute_condensed_progress_metrics(baseline_macro_rows)
            baseline_by_mode = {row["mode"]: row for row in baseline_condensed}
            lines.append(f"Candidate: `{candidate_label}`")
            lines.append(f"Baseline: `{baseline_label}`")
            lines.append("Delta is candidate vs baseline. Positive % means candidate is better (faster for throughput columns; smaller is better for compression ratio).")
            lines.append("")
            lines.append("| Mode | Compression Ratio vs CSV | Dense Write (rows/s) | Dense Read (rows/s) | Sparse Write (cells/s) | Sparse Read (cells/s) |")
            lines.append("|------|---------------------------:|---------------------:|--------------------:|-----------------------:|----------------------:|")

            for mode in MODE_ALIASES.keys():
                current = current_by_mode.get(mode, {})
                baseline = baseline_by_mode.get(mode, {})
                lines.append(
                    f"| {mode}"
                    f" | {fmt_delta(current.get('compression_ratio_median'), baseline.get('compression_ratio_median'), higher_is_better=False)}"
                    f" | {fmt_delta(current.get('dense_write_rows_per_sec_median'), baseline.get('dense_write_rows_per_sec_median'), higher_is_better=True)}"
                    f" | {fmt_delta(current.get('dense_read_rows_per_sec_median'), baseline.get('dense_read_rows_per_sec_median'), higher_is_better=True)}"
                    f" | {fmt_delta(current.get('sparse_write_cells_per_sec_median'), baseline.get('sparse_write_cells_per_sec_median'), higher_is_better=True)}"
                    f" | {fmt_delta(current.get('sparse_read_cells_per_sec_median'), baseline.get('sparse_read_cells_per_sec_median'), higher_is_better=True)} |"
                )
            lines.append("")

    if micro_data and micro_data.get("benchmarks"):
        lines.append("## Micro Benchmark Summary")
        lines.append("")
        benchmarks = micro_data.get("benchmarks", [])
        lines.append(f"Entries: {len(benchmarks)}")
        lines.append("")
        lines.append("| Group | Median Real Time (ns) | Count |")
        lines.append("|-------|------------------------:|------:|")

        groups = {
            "Get": [b.get("real_time") for b in benchmarks if str(b.get("name", "")).startswith("BM_Get_") and isinstance(b.get("real_time"), (int, float))],
            "Set": [b.get("real_time") for b in benchmarks if str(b.get("name", "")).startswith("BM_Set_") and isinstance(b.get("real_time"), (int, float))],
            "Visit": [b.get("real_time") for b in benchmarks if "Visit" in str(b.get("name", "")) and isinstance(b.get("real_time"), (int, float))],
            "Serialize": [b.get("real_time") for b in benchmarks if "Serialize" in str(b.get("name", "")) and isinstance(b.get("real_time"), (int, float))],
        }

        for label, values in groups.items():
            if values:
                lines.append(f"| {label} | {statistics.median(values):.2f} | {len(values)} |")

        lines.append("")

    lines.append("## Micro Benchmark Comparison")
    lines.append("")
    current_micro_medians = micro_group_medians(micro_data)
    baseline_micro_medians = micro_group_medians(baseline_micro)

    if not current_micro_medians:
        lines.append("No micro benchmark data in current run.")
        lines.append("")
    elif baseline_dir is None:
        lines.append("No baseline run available (latest git-clean run not found).")
        lines.append("")
    elif not baseline_micro_medians:
        lines.append(f"Baseline `{baseline_label}` has no micro benchmark data.")
        lines.append("")
    else:
        lines.append(f"Candidate: `{candidate_label}`")
        lines.append(f"Baseline: `{baseline_label}`")
        lines.append("Delta is candidate vs baseline. Positive % indicates candidate is faster (lower ns).")
        lines.append("")
        lines.append("| Group | Current Median (ns) | Baseline Median (ns) | Delta |")
        lines.append("|-------|--------------------:|---------------------:|------:|")
        for label in ["Get", "Set", "Visit", "Serialize"]:
            cur = current_micro_medians.get(label)
            base = baseline_micro_medians.get(label)
            if cur is None and base is None:
                continue
            cur_text = f"{cur:.2f}" if cur is not None else "—"
            base_text = f"{base:.2f}" if base is not None else "—"
            delta = fmt_delta(cur, base, higher_is_better=False)
            lines.append(f"| {label} | {cur_text} | {base_text} | {delta} |")
        lines.append("")

    if not summary_only:
        for macro_type in ["MACRO-SMALL", "MACRO-LARGE", "MACRO"]:
            current_macro_rows = current_macro_by_type.get(macro_type, [])
            if not current_macro_rows:
                continue
            lines.append(f"## Macro Detail ({macro_type})")
            lines.append("")
            lines.append("| Dataset | Mode | Write (ms) | Read (ms) | Total (ms) | Size (MB) |")
            lines.append("|---------|------|-----------:|----------:|-----------:|----------:|")
            for row in current_macro_rows:
                lines.append(
                    f"| {row.get('dataset', '?')} | {row.get('mode', '?')}"
                    f" | {row.get('write_time_ms', 0):.1f}"
                    f" | {row.get('read_time_ms', 0):.1f}"
                    f" | {(row.get('write_time_ms', 0) + row.get('read_time_ms', 0)):.1f}"
                    f" | {(row.get('file_size', 0) / (1024 * 1024)):.2f} |"
                )
            lines.append("")

    git_label = str(platform.get("git_label", "wip")).lower()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_name = f"report_{git_label}_{timestamp}.md"
    report_path = run_dir / report_name
    report_path.write_text("\n".join(lines) + "\n")

    # stable pointer file for tooling
    (run_dir / "report.md").write_text("\n".join(lines) + "\n")

    # sidecar condensed metrics JSON for tooling
    if condensed_sidecar:
        (run_dir / "condensed_metrics.json").write_text(json.dumps({"rows_by_type": condensed_sidecar}, indent=2))

    print(f"Report written: {report_path}")
    return report_path


def main():
    parser = argparse.ArgumentParser(description="BCSV benchmark reporting tool")
    parser.add_argument("run_dir", help="Run directory containing platform.json/macro_results.json")
    parser.add_argument("--baseline", default=None,
                        help="Optional baseline run directory for comparison")
    parser.add_argument("--summary-only", action="store_true",
                        help="Only include host + condensed matrix + condensed comparison")

    args = parser.parse_args()

    run_dir = Path(args.run_dir)
    if not run_dir.exists():
        print(f"ERROR: Run directory not found: {run_dir}")
        return 1

    baseline_dir = Path(args.baseline) if args.baseline else latest_clean_run(run_dir)
    if baseline_dir is not None and not baseline_dir.exists():
        baseline_dir = None

    generate_summary_markdown(run_dir, baseline_dir, args.summary_only)
    return 0


if __name__ == "__main__":
    sys.exit(main())
