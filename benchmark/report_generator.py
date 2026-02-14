#!/usr/bin/env python3
"""
BCSV Benchmark Report Generator

Reads JSON result files from a benchmark run directory and generates:
- Markdown report with all tables
- Speedup summary table (BCSV vs CSV: write/read/total/size)
- Compression ratio table
- Micro-benchmark summary
- CLI tool timing summary
- External CSV comparison table (if available)

Usage:
    python report_generator.py <results_dir>
    python report_generator.py benchmark/results/hostname/2026.02.12_14.10/
"""

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


def _fmt_med_std_pair(median_val, stdev_val, decimals=2):
    if median_val is None:
        return "—"
    stdev = 0.0 if stdev_val is None else stdev_val
    return f"{median_val:.{decimals}f} ± {stdev:.{decimals}f}"


# ============================================================================
# Data loading
# ============================================================================

def load_results(results_dir: Path) -> dict:
    """Load all JSON result files from a run directory."""
    data = {}

    for name, key in [("platform.json", "platform"),
                       ("macro_results.json", "macro"),
                       ("micro_results.json", "micro"),
                       ("cli_results.json", "cli"),
                       ("external_results.json", "external")]:
        path = results_dir / name
        if path.exists():
            try:
                data[key] = json.loads(path.read_text())
            except json.JSONDecodeError as e:
                print(f"WARNING: Malformed JSON in {path}: {e}")
                continue

    return data


def group_by_dataset(results: list) -> OrderedDict:
    """Group a list of result dicts by dataset name, preserving order."""
    groups = OrderedDict()
    for r in results:
        ds = r.get("dataset", "unknown")
        groups.setdefault(ds, []).append(r)
    return groups


# ============================================================================
# Markdown generation
# ============================================================================

def _header(lines, title, level=2):
    lines.append(f"\n{'#' * level} {title}\n")


def gen_platform_section(lines, platform):
    _header(lines, "Platform")
    lines.append("| Property | Value |")
    lines.append("|----------|-------|")
    for key, label in [("hostname", "Hostname"), ("os", "OS"),
                        ("cpu_model", "CPU"), ("build_type", "Build"),
                        ("compiler", "Compiler"), ("bcsv_version", "BCSV version"),
                        ("git_describe", "Git"), ("timestamp", "Timestamp")]:
        lines.append(f"| {label} | {platform.get(key, 'N/A')} |")
    lines.append("")


def gen_macro_detail_tables(lines, datasets):
    """Per-dataset detail tables."""
    _header(lines, "Macro Benchmark — Per-Dataset Detail")

    for ds_name, ds_results in datasets.items():
        _header(lines, f"Dataset: `{ds_name}`", level=3)

        lines.append("| Mode | Scenario | Access | Proc Rows | Sel Cols | Write (ms) | Read (ms) | Total (ms) | Size (MB)"
                  " | W Mrow/s | R Mrow/s | Compression | Valid |")
        lines.append("|------|----------|--------|----------:|---------:|-----------|----------|-----------|----------"
                  "|---------|---------|-------------|-------|")

        for r in ds_results:
            write_ms = r.get("write_time_ms", 0)
            read_ms = r.get("read_time_ms", 0)
            total_ms = write_ms + read_ms
            file_mb = r.get("file_size", 0) / (1024 * 1024)
            w_mrows = r.get("write_rows_per_sec", 0) / 1e6
            r_mrows = r.get("read_rows_per_sec", 0) / 1e6
            comp = r.get("compression_ratio", 0)
            valid = "PASS" if r.get("validation_passed", False) else "FAIL"
            comp_str = f"{comp:.2%}" if comp > 0 else "—"
            scenario = r.get("scenario_id", "baseline")
            access = r.get("access_path", "-")
            processed_ratio = r.get("processed_row_ratio", 1.0) * 100.0
            selected_cols = r.get("selected_columns", r.get("num_columns", 0))

            lines.append(
                f"| {r.get('mode', '?')} | {scenario} | {access}"
                f" | {processed_ratio:.1f}% | {selected_cols} | {write_ms:.1f} | {read_ms:.1f}"
                f" | {total_ms:.1f} | {file_mb:.2f}"
                f" | {w_mrows:.3f} | {r_mrows:.3f}"
                f" | {comp_str} | {valid} |"
            )
        lines.append("")


def gen_speedup_table(lines, datasets):
    """Cross-dataset speedup summary: BCSV vs CSV for write/read/total/size."""
    _header(lines, "BCSV vs CSV Comparison")

    lines.append("| Dataset | Mode | Write | Read | Total | File size |")
    lines.append("|---------|------|------:|-----:|------:|----------:|")

    for ds_name, ds_results in datasets.items():
        csv_r = next((r for r in ds_results if r.get("mode") == "CSV"), None)
        if not csv_r:
            continue

        csv_w = max(csv_r.get("write_time_ms", 1), 0.001)
        csv_rd = max(csv_r.get("read_time_ms", 1), 0.001)
        csv_sz = max(csv_r.get("file_size", 1), 1)

        for r in ds_results:
            if r.get("mode") == "CSV":
                continue
            mode = r.get("mode", "?")
            w_sp = csv_w / max(r.get("write_time_ms", 1), 0.001)
            r_sp = csv_rd / max(r.get("read_time_ms", 1), 0.001)
            t_sp = (csv_w + csv_rd) / max(r.get("write_time_ms", 0) + r.get("read_time_ms", 0), 0.001)
            sz = r.get("file_size", 0) / csv_sz * 100

            lines.append(
                f"| {ds_name} | {mode} | {w_sp:.2f}x | {r_sp:.2f}x"
                f" | {t_sp:.2f}x | {sz:.1f}% |"
            )
    lines.append("")


def gen_compression_table(lines, datasets):
    """Compression ratio comparison."""
    _header(lines, "Compression Ratios")

    lines.append("| Dataset | CSV (MB) | BCSV Flex (MB) | Flex ratio"
                  " | BCSV ZoH (MB) | ZoH ratio |")
    lines.append("|---------|---------|---------------|----------"
                  "|--------------|----------|")

    for ds_name, ds_results in datasets.items():
        csv_r = next((r for r in ds_results if r.get("mode") == "CSV"), None)
        flex_r = next((r for r in ds_results
                       if "Flexible" in r.get("mode", "") and "ZoH" not in r.get("mode", "")), None)
        zoh_r = next((r for r in ds_results if "ZoH" in r.get("mode", "")), None)

        if not csv_r:
            continue

        csv_mb = csv_r.get("file_size", 0) / (1024 * 1024)
        flex_mb = flex_r.get("file_size", 0) / (1024 * 1024) if flex_r else 0
        zoh_mb = zoh_r.get("file_size", 0) / (1024 * 1024) if zoh_r else 0
        flex_pct = (flex_r["file_size"] / max(csv_r["file_size"], 1) * 100) if flex_r else 0
        zoh_pct = (zoh_r["file_size"] / max(csv_r["file_size"], 1) * 100) if zoh_r else 0

        lines.append(
            f"| {ds_name} | {csv_mb:.1f} | {flex_mb:.1f} | {flex_pct:.1f}%"
            f" | {zoh_mb:.1f} | {zoh_pct:.1f}% |"
        )
    lines.append("")


def gen_sparse_summary(lines, results):
    """Compact sparse scenario summary aggregated across datasets."""
    if not results:
        return

    by_scenario = OrderedDict()
    for r in results:
        scenario = r.get("scenario_id", "baseline")
        by_scenario.setdefault(scenario, []).append(r)

    # Only show section when sparse metadata is actually present.
    if len(by_scenario) <= 1 and "baseline" in by_scenario:
        return

    _header(lines, "Sparse Summary")
    lines.append("| Scenario | Avg Proc Rows | Avg Sel Cols | Flex Speedup | ZoH Speedup | Flex Size | ZoH Size |")
    lines.append("|----------|--------------:|-------------:|-------------:|------------:|----------:|---------:|")

    def median_or_none(values):
        return statistics.median(values) if values else None

    def scenario_sort_key(name):
        return (0 if name == "baseline" else 1, name)

    for scenario in sorted(by_scenario.keys(), key=scenario_sort_key):
        rows = by_scenario[scenario]
        proc_ratios = [r.get("processed_row_ratio", 1.0) for r in rows]
        sel_cols = [r.get("selected_columns", r.get("num_columns", 0)) for r in rows]

        csv_totals = [r.get("write_time_ms", 0) + r.get("read_time_ms", 0) for r in rows if r.get("mode") == "CSV"]
        flex_totals = [r.get("write_time_ms", 0) + r.get("read_time_ms", 0) for r in rows if r.get("mode") == "BCSV Flexible"]
        zoh_totals = [r.get("write_time_ms", 0) + r.get("read_time_ms", 0) for r in rows if r.get("mode") == "BCSV Flexible ZoH"]

        csv_sizes = [r.get("file_size", 0) for r in rows if r.get("mode") == "CSV"]
        flex_sizes = [r.get("file_size", 0) for r in rows if r.get("mode") == "BCSV Flexible"]
        zoh_sizes = [r.get("file_size", 0) for r in rows if r.get("mode") == "BCSV Flexible ZoH"]

        csv_total = median_or_none(csv_totals)
        flex_total = median_or_none(flex_totals)
        zoh_total = median_or_none(zoh_totals)

        csv_size = median_or_none(csv_sizes)
        flex_size = median_or_none(flex_sizes)
        zoh_size = median_or_none(zoh_sizes)

        flex_speedup = f"{(csv_total / max(flex_total, 0.001)):.2f}x" if csv_total and flex_total else "—"
        zoh_speedup = f"{(csv_total / max(zoh_total, 0.001)):.2f}x" if csv_total and zoh_total else "—"
        flex_size_pct = f"{(flex_size / max(csv_size, 1) * 100):.1f}%" if csv_size and flex_size is not None else "—"
        zoh_size_pct = f"{(zoh_size / max(csv_size, 1) * 100):.1f}%" if csv_size and zoh_size is not None else "—"

        lines.append(
            f"| {scenario} | {statistics.mean(proc_ratios) * 100:.1f}%"
            f" | {statistics.mean(sel_cols):.1f}"
            f" | {flex_speedup} | {zoh_speedup} | {flex_size_pct} | {zoh_size_pct} |"
        )

    lines.append("")


def gen_condensed_progress_table(lines, results):
    """Condensed development KPI table across all macro tests."""
    if not results:
        return

    _header(lines, "Condensed Performance Matrix")

    rows = compute_condensed_progress_metrics(results)
    if not rows:
        lines.append("No macro KPI data available.\n")
        return

    metrics_by_mode = {row["mode"]: row for row in rows}

    lines.append("| Mode | Compression Ratio vs CSV | Dense Write (rows/s) | Dense Read (rows/s) | Sparse Write (cells/s) | Sparse Read (cells/s) |")
    lines.append("|------|---------------------------:|---------------------:|--------------------:|-----------------------:|----------------------:|")

    for display_mode in MODE_ALIASES.keys():
        row = metrics_by_mode.get(display_mode, {})

        lines.append(
            f"| {display_mode}"
            f" | {_fmt_med_std_pair(row.get('compression_ratio_median'), row.get('compression_ratio_stdev'), decimals=3)}"
            f" | {_fmt_med_std_pair(row.get('dense_write_rows_per_sec_median'), row.get('dense_write_rows_per_sec_stdev'), decimals=0)}"
            f" | {_fmt_med_std_pair(row.get('dense_read_rows_per_sec_median'), row.get('dense_read_rows_per_sec_stdev'), decimals=0)}"
            f" | {_fmt_med_std_pair(row.get('sparse_write_cells_per_sec_median'), row.get('sparse_write_cells_per_sec_stdev'), decimals=0)}"
            f" | {_fmt_med_std_pair(row.get('sparse_read_cells_per_sec_median'), row.get('sparse_read_cells_per_sec_stdev'), decimals=0)} |"
        )

    lines.append("")


def load_previous_macro_results(results_dir: Path, max_runs: int = 3):
    """Load up to N previous sibling run macro results (newest first)."""
    parent = results_dir.parent
    if not parent.exists():
        return []

    candidates = []
    for child in parent.iterdir():
        if child == results_dir or not child.is_dir():
            continue
        macro_path = child / "macro_results.json"
        if macro_path.exists():
            candidates.append((child.stat().st_mtime, child, macro_path))

    candidates.sort(key=lambda x: x[0], reverse=True)

    previous = []
    for _, run_dir, macro_path in candidates[:max_runs]:
        try:
            payload = json.loads(macro_path.read_text())
            rows = payload.get("results", [])
            if rows:
                previous.append((run_dir.name, rows))
        except json.JSONDecodeError:
            continue
    return previous


def gen_condensed_performance_comparison(lines, current_results, previous_runs):
    """Comparison matrix: delta current vs best of previous runs."""
    _header(lines, "Condensed Performance Matrix Comparison")
    lines.append("Interpretation: for throughput metrics (rows/s, cells/s), **positive % is better**; for compression ratio, **positive % is better** (smaller ratio than previous best).")
    lines.append("")

    current_rows = compute_condensed_progress_metrics(current_results)
    if not current_rows:
        lines.append("No current macro data available for comparison.\n")
        return
    if not previous_runs:
        lines.append("No previous run data found (need at least one sibling run with `macro_results.json`).\n")
        return

    previous_condensed = []
    for _, rows in previous_runs:
        previous_condensed.append(compute_condensed_progress_metrics(rows))

    by_mode_prev = {}
    for table in previous_condensed:
        for row in table:
            by_mode_prev.setdefault(row["mode"], []).append(row)

    lines.append("| Mode | Compression Ratio vs CSV | Dense Write (rows/s) | Dense Read (rows/s) | Sparse Write (cells/s) | Sparse Read (cells/s) |")
    lines.append("|------|---------------------------:|---------------------:|--------------------:|-----------------------:|----------------------:|")

    def best_prev(rows, key, higher_is_better):
        vals = [r.get(key) for r in rows if r.get(key) is not None]
        if not vals:
            return None
        return max(vals) if higher_is_better else min(vals)

    def fmt_delta(current, prev_best, higher_is_better):
        if current is None or prev_best is None or prev_best == 0:
            return "—"
        if higher_is_better:
            delta = ((current - prev_best) / prev_best) * 100.0
        else:
            delta = ((prev_best - current) / prev_best) * 100.0
        return f"{delta:+.1f}%"

    for cur in current_rows:
        mode = cur["mode"]
        prev_rows = by_mode_prev.get(mode, [])

        lines.append(
            f"| {mode}"
            f" | {fmt_delta(cur.get('compression_ratio_median'), best_prev(prev_rows, 'compression_ratio_median', False), False)}"
            f" | {fmt_delta(cur.get('dense_write_rows_per_sec_median'), best_prev(prev_rows, 'dense_write_rows_per_sec_median', True), True)}"
            f" | {fmt_delta(cur.get('dense_read_rows_per_sec_median'), best_prev(prev_rows, 'dense_read_rows_per_sec_median', True), True)}"
            f" | {fmt_delta(cur.get('sparse_write_cells_per_sec_median'), best_prev(prev_rows, 'sparse_write_cells_per_sec_median', True), True)}"
            f" | {fmt_delta(cur.get('sparse_read_cells_per_sec_median'), best_prev(prev_rows, 'sparse_read_cells_per_sec_median', True), True)} |"
        )

    labels = ", ".join(name for name, _ in previous_runs)
    lines.append("")
    lines.append(f"Compared against best value from previous runs: {labels}.")
    lines.append("")


def compute_condensed_progress_metrics(results):
    """Compute condensed KPI metrics and return structured rows for sidecar export."""
    if not results:
        return []

    def pick_mode_rows(rows, aliases):
        return [r for r in rows if r.get("mode") in aliases]

    def med_std(values):
        if not values:
            return None, None
        med = statistics.median(values)
        stdev = statistics.stdev(values) if len(values) > 1 else 0.0
        return med, stdev

    csv_size_by_key = {}
    for r in results:
        if r.get("mode") == "CSV":
            key = (r.get("dataset"), r.get("scenario_id", "baseline"))
            csv_size_by_key[key] = r.get("file_size", 0)

    rows_out = []
    for display_mode, aliases in MODE_ALIASES.items():
        mode_rows = pick_mode_rows(results, aliases)
        dense_rows = [r for r in mode_rows if r.get("scenario_id", "baseline") == "baseline"]
        sparse_rows = [r for r in mode_rows if r.get("scenario_id", "baseline") != "baseline"]

        dense_write_rows_s = [r.get("write_rows_per_sec", 0.0) for r in dense_rows if r.get("write_rows_per_sec")]
        dense_read_rows_s = [r.get("read_rows_per_sec", 0.0) for r in dense_rows if r.get("read_rows_per_sec")]

        sparse_write_cells_s = []
        sparse_read_cells_s = []
        for r in sparse_rows:
            num_rows = float(r.get("num_rows", 0) or 0)
            proc_ratio = float(r.get("processed_row_ratio", 1.0) or 0)
            sel_cols = float(r.get("selected_columns", r.get("num_columns", 0)) or 0)
            eff_cells = num_rows * proc_ratio * sel_cols
            w_ms = float(r.get("write_time_ms", 0) or 0)
            rd_ms = float(r.get("read_time_ms", 0) or 0)
            if w_ms > 0 and eff_cells > 0:
                sparse_write_cells_s.append(eff_cells / (w_ms / 1000.0))
            if rd_ms > 0 and eff_cells > 0:
                sparse_read_cells_s.append(eff_cells / (rd_ms / 1000.0))

        comp_ratios = []
        for r in mode_rows:
            if r.get("mode") == "CSV":
                comp_ratios.append(1.0)
                continue
            key = (r.get("dataset"), r.get("scenario_id", "baseline"))
            csv_size = csv_size_by_key.get(key, 0)
            mode_size = r.get("file_size", 0)
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


def write_condensed_progress_artifacts(output_dir, results):
    """Write condensed KPI sidecars (JSON + CSV)."""
    rows = compute_condensed_progress_metrics(results)
    if not rows:
        return

    out_dir = Path(output_dir)
    json_path = out_dir / "condensed_metrics.json"
    csv_path = out_dir / "condensed_metrics.csv"

    json_path.write_text(json.dumps({"rows": rows}, indent=2))

    headers = [
        "mode",
        "compression_ratio_median", "compression_ratio_stdev",
        "dense_write_rows_per_sec_median", "dense_write_rows_per_sec_stdev",
        "dense_read_rows_per_sec_median", "dense_read_rows_per_sec_stdev",
        "sparse_write_cells_per_sec_median", "sparse_write_cells_per_sec_stdev",
        "sparse_read_cells_per_sec_median", "sparse_read_cells_per_sec_stdev",
    ]

    def fmt(v):
        return "" if v is None else f"{v:.6f}" if isinstance(v, float) else str(v)

    lines = [",".join(headers)]
    for row in rows:
        lines.append(",".join(fmt(row.get(h)) for h in headers))
    csv_path.write_text("\n".join(lines) + "\n")

    print(f"Condensed metrics written to: {json_path}")
    print(f"Condensed metrics written to: {csv_path}")


def gen_micro_section(lines, micro_data):
    """Micro benchmark summary."""
    if not micro_data or "benchmarks" not in micro_data:
        return

    _header(lines, "Micro Benchmarks (per-operation latency)")

    benchmarks = micro_data["benchmarks"]
    lines.append("| Benchmark | Time (ns) | CPU (ns) | Iterations |")
    lines.append("|-----------|-----------|---------|-----------|")

    for b in benchmarks:
        name = b.get("name", "?")
        real_t = b.get("real_time", 0)
        cpu_t = b.get("cpu_time", 0)
        iters = b.get("iterations", 0)
        lines.append(f"| {name} | {real_t:.2f} | {cpu_t:.2f} | {iters:,} |")
    lines.append("")


def gen_cli_section(lines, cli_data):
    """CLI tool benchmark summary."""
    if not cli_data or "tools" not in cli_data:
        return

    _header(lines, "CLI Tool Benchmarks")

    profile = cli_data.get("profile", "?")
    rows = cli_data.get("rows", "?")
    lines.append(f"Profile: `{profile}`, Rows: {rows}\n")

    lines.append("| Tool | Wall (ms) | MB/s | Rows/s |")
    lines.append("|------|----------|------|--------|")

    for tool_name, tool_data in cli_data["tools"].items():
        if isinstance(tool_data, dict) and "wall_ms" in tool_data:
            wall = tool_data.get("wall_ms", 0)
            mb_s = tool_data.get("throughput_mb_s", "—")
            r_s = tool_data.get("rows_per_sec", "—")
            lines.append(f"| {tool_name} | {wall} | {mb_s} | {r_s} |")
        elif isinstance(tool_data, dict) and "error" in tool_data:
            lines.append(f"| {tool_name} | ERROR | — | — |")

    match = cli_data.get("row_count_match", False)
    lines.append(f"\nRound-trip validation: **{'PASS' if match else 'FAIL'}**\n")


def gen_external_section(lines, external_data):
    """External CSV comparison summary."""
    if not external_data or "results" not in external_data:
        return

    _header(lines, "External CSV Library Comparison")
    lines.append("*BCSV CsvReader vs vincentlaucsb/csv-parser (memory-mapped)*\n")

    datasets = group_by_dataset(external_data["results"])

    lines.append("| Dataset | BCSV Read (ms) | External Read (ms) | BCSV MB/s"
                  " | External MB/s | Speedup |")
    lines.append("|---------|---------------|-------------------|----------"
                  "|--------------|---------|")

    for ds_name, ds_results in datasets.items():
        bcsv_r = next((r for r in ds_results if "BCSV" in r.get("mode", "")), None)
        ext_r = next((r for r in ds_results if "External" in r.get("mode", "")), None)
        if not bcsv_r or not ext_r:
            continue

        bcsv_ms = bcsv_r.get("read_time_ms", 0)
        ext_ms = ext_r.get("read_time_ms", 0)
        bcsv_mbs = bcsv_r.get("read_mb_per_sec", 0)
        ext_mbs = ext_r.get("read_mb_per_sec", 0)
        speedup = ext_ms / max(bcsv_ms, 0.001) if bcsv_ms > 0 else 0

        lines.append(
            f"| {ds_name} | {bcsv_ms:.1f} | {ext_ms:.1f}"
            f" | {bcsv_mbs:.0f} | {ext_mbs:.0f} | {speedup:.2f}x |"
        )
    lines.append("")
    lines.append("*Speedup > 1.0 means BCSV CsvReader is faster*\n")


def generate_markdown(data, output_path):
    """Generate the complete Markdown report."""
    lines = []

    lines.append("# BCSV Benchmark Report\n")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M')}\n")

    # Platform
    if "platform" in data:
        gen_platform_section(lines, data["platform"])

    # Condensed matrices directly below platform
    if "macro" in data and "results" in data["macro"]:
        results_dir = Path(output_path).parent
        previous_runs = load_previous_macro_results(results_dir, max_runs=3)
        gen_condensed_progress_table(lines, data["macro"]["results"])
        gen_condensed_performance_comparison(lines, data["macro"]["results"], previous_runs)

    # Macro benchmarks
    datasets = None
    if "macro" in data and "results" in data["macro"]:
        datasets = group_by_dataset(data["macro"]["results"])
        gen_speedup_table(lines, datasets)
        gen_compression_table(lines, datasets)
        gen_sparse_summary(lines, data["macro"]["results"])

        gen_macro_detail_tables(lines, datasets)

    # Micro benchmarks
    if "micro" in data:
        gen_micro_section(lines, data["micro"])

    # CLI tools
    if "cli" in data:
        gen_cli_section(lines, data["cli"])

    # External comparison
    if "external" in data:
        gen_external_section(lines, data["external"])

    # Total time
    if "macro" in data:
        total = data["macro"].get("total_time_sec", 0)
        lines.append(f"\n---\n**Total macro benchmark time: {total:.1f} seconds**\n")

    report_text = "\n".join(lines)
    Path(output_path).write_text(report_text)
    print(f"Markdown report written to: {output_path}")
    return report_text


# ============================================================================
# Main entry point
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    results_dir = Path(sys.argv[1])
    if not results_dir.exists():
        print(f"ERROR: Directory not found: {results_dir}")
        return 1

    print(f"Loading results from: {results_dir}")
    data = load_results(results_dir)

    if not data:
        print("ERROR: No result files found")
        return 1

    # Generate Markdown report
    report_path = results_dir / "report.md"
    generate_markdown(data, report_path)

    if "macro" in data and "results" in data["macro"]:
        write_condensed_progress_artifacts(results_dir, data["macro"]["results"])

    print(f"\nReport complete. See: {results_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
