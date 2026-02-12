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
- Bar charts (matplotlib): time, throughput, file size, compression
- Inline PNG chart references in Markdown

Usage:
    python report_generator.py <results_dir>
    python report_generator.py benchmark/results/hostname/2026.02.12_14.10/
"""

import json
import sys
from collections import OrderedDict
from datetime import datetime
from pathlib import Path

# Try to import matplotlib; gracefully degrade if not available
try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("NOTE: matplotlib not available — charts will be skipped")


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

        lines.append("| Mode | Write (ms) | Read (ms) | Total (ms) | Size (MB)"
                      " | W Mrow/s | R Mrow/s | Compression | Valid |")
        lines.append("|------|-----------|----------|-----------|----------"
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

            lines.append(
                f"| {r.get('mode', '?')} | {write_ms:.1f} | {read_ms:.1f}"
                f" | {total_ms:.1f} | {file_mb:.2f}"
                f" | {w_mrows:.3f} | {r_mrows:.3f}"
                f" | {comp_str} | {valid} |"
            )
        lines.append("")


def gen_speedup_table(lines, datasets):
    """Cross-dataset speedup summary: BCSV vs CSV for write/read/total/size."""
    _header(lines, "Speedup Summary (BCSV vs CSV)")

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

    # Macro benchmarks
    datasets = None
    if "macro" in data and "results" in data["macro"]:
        datasets = group_by_dataset(data["macro"]["results"])
        gen_speedup_table(lines, datasets)
        gen_compression_table(lines, datasets)

        # Inline chart references
        if HAS_MATPLOTLIB:
            _header(lines, "Charts")
            lines.append("![Total Time](chart_total_time.png)\n")
            lines.append("![File Size](chart_file_size.png)\n")
            lines.append("![Throughput](chart_throughput.png)\n")
            lines.append("![Compression](chart_compression.png)\n")

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
# Chart generation (matplotlib)
# ============================================================================

# Consistent color palette
MODE_COLORS = {
    "CSV": "#E53935",
    "BCSV Flexible": "#1E88E5",
    "BCSV Flexible ZoH": "#43A047",
    "BCSV CsvReader": "#1E88E5",
    "External csv-parser": "#FB8C00",
}


def _color_for(mode):
    return MODE_COLORS.get(mode, "#757575")


def _short_mode(mode):
    """Shorten mode names for axis labels."""
    return mode.replace("BCSV Flexible ", "BCSV ").replace("BCSV Flexible", "BCSV Flex")


def chart_total_time(datasets, output_dir):
    """Stacked bar chart: write + read time per mode per dataset."""
    ds_names = list(datasets.keys())
    n_ds = len(ds_names)
    fig, axes = plt.subplots(1, n_ds, figsize=(max(4 * n_ds, 12), 5), squeeze=False)
    fig.suptitle("Total Time (Write + Read)", fontsize=14, fontweight="bold")

    for idx, ds_name in enumerate(ds_names):
        ax = axes[0][idx]
        ds_results = datasets[ds_name]
        modes = [_short_mode(r["mode"]) for r in ds_results]
        writes = [r.get("write_time_ms", 0) for r in ds_results]
        reads = [r.get("read_time_ms", 0) for r in ds_results]
        colors = [_color_for(r["mode"]) for r in ds_results]

        x = range(len(modes))
        ax.bar(x, writes, color=colors, alpha=0.85, label="Write")
        ax.bar(x, reads, bottom=writes, color=colors, alpha=0.50, label="Read")

        # Add total time labels on top
        for i, (w, rd) in enumerate(zip(writes, reads)):
            total = w + rd
            ax.text(i, total + total * 0.02, f"{total:.0f}", ha="center",
                    va="bottom", fontsize=7)

        ax.set_xticks(list(x))
        ax.set_xticklabels(modes, rotation=35, ha="right", fontsize=7)
        ax.set_ylabel("Time (ms)")
        ax.set_title(ds_name, fontsize=9)
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{v:.0f}"))

    # Legend for write/read distinction
    from matplotlib.patches import Patch
    legend_elements = [Patch(facecolor="#888", alpha=0.85, label="Write"),
                       Patch(facecolor="#888", alpha=0.50, label="Read")]
    fig.legend(handles=legend_elements, loc="upper right", fontsize=8)

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    path = output_dir / "chart_total_time.png"
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Chart: {path}")


def chart_file_size(datasets, output_dir):
    """Bar chart: file size in MB per mode, per dataset."""
    ds_names = list(datasets.keys())
    n_ds = len(ds_names)
    fig, axes = plt.subplots(1, n_ds, figsize=(max(4 * n_ds, 12), 5), squeeze=False)
    fig.suptitle("File Size Comparison", fontsize=14, fontweight="bold")

    for idx, ds_name in enumerate(ds_names):
        ax = axes[0][idx]
        ds_results = datasets[ds_name]
        modes = [_short_mode(r["mode"]) for r in ds_results]
        sizes = [r.get("file_size", 0) / (1024 * 1024) for r in ds_results]
        colors = [_color_for(r["mode"]) for r in ds_results]

        ax.bar(range(len(modes)), sizes, color=colors, alpha=0.85)
        for i, sz in enumerate(sizes):
            label = f"{sz:.1f}" if sz >= 1 else f"{sz:.2f}"
            ax.text(i, sz + max(sizes) * 0.02, label, ha="center",
                    va="bottom", fontsize=7)

        ax.set_xticks(range(len(modes)))
        ax.set_xticklabels(modes, rotation=35, ha="right", fontsize=7)
        ax.set_ylabel("File Size (MB)")
        ax.set_title(ds_name, fontsize=9)

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    path = output_dir / "chart_file_size.png"
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Chart: {path}")


def chart_throughput(datasets, output_dir):
    """Grouped bar chart: write/read throughput in M rows/sec."""
    ds_names = list(datasets.keys())
    n_ds = len(ds_names)
    fig, axes = plt.subplots(1, n_ds, figsize=(max(4 * n_ds, 12), 5), squeeze=False)
    fig.suptitle("Throughput (M rows/sec)", fontsize=14, fontweight="bold")

    for idx, ds_name in enumerate(ds_names):
        ax = axes[0][idx]
        ds_results = datasets[ds_name]
        modes = [_short_mode(r["mode"]) for r in ds_results]
        w_tput = [r.get("write_rows_per_sec", 0) / 1e6 for r in ds_results]
        r_tput = [r.get("read_rows_per_sec", 0) / 1e6 for r in ds_results]

        x = list(range(len(modes)))
        width = 0.35
        ax.bar([xi - width / 2 for xi in x], w_tput, width, label="Write",
               color="#1565C0", alpha=0.85)
        ax.bar([xi + width / 2 for xi in x], r_tput, width, label="Read",
               color="#EF6C00", alpha=0.85)
        ax.set_xticks(x)
        ax.set_xticklabels(modes, rotation=35, ha="right", fontsize=7)
        ax.set_ylabel("M rows/sec")
        ax.set_title(ds_name, fontsize=9)
        if idx == 0:
            ax.legend(fontsize=7)

    plt.tight_layout(rect=[0, 0, 1, 0.93])
    path = output_dir / "chart_throughput.png"
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Chart: {path}")


def chart_compression(datasets, output_dir):
    """Horizontal bar chart: compression ratio across all datasets."""
    ds_names = []
    flex_pcts = []
    zoh_pcts = []

    for ds_name, ds_results in datasets.items():
        csv_r = next((r for r in ds_results if r.get("mode") == "CSV"), None)
        flex_r = next((r for r in ds_results
                       if "Flexible" in r.get("mode", "") and "ZoH" not in r.get("mode", "")), None)
        zoh_r = next((r for r in ds_results if "ZoH" in r.get("mode", "")), None)
        if not csv_r or csv_r.get("file_size", 0) == 0:
            continue

        csv_sz = csv_r["file_size"]
        ds_names.append(ds_name)
        flex_pcts.append(flex_r["file_size"] / csv_sz * 100 if flex_r else 0)
        zoh_pcts.append(zoh_r["file_size"] / csv_sz * 100 if zoh_r else 0)

    if not ds_names:
        return

    fig, ax = plt.subplots(figsize=(10, max(3, len(ds_names) * 0.6)))
    y = range(len(ds_names))
    height = 0.35

    bars_flex = ax.barh([yi - height / 2 for yi in y], flex_pcts, height,
                         label="BCSV Flexible", color="#1E88E5", alpha=0.85)
    bars_zoh = ax.barh([yi + height / 2 for yi in y], zoh_pcts, height,
                        label="BCSV ZoH", color="#43A047", alpha=0.85)

    for bar, pct in zip(bars_flex, flex_pcts):
        ax.text(bar.get_width() + 1, bar.get_y() + bar.get_height() / 2,
                f"{pct:.1f}%", va="center", fontsize=7)
    for bar, pct in zip(bars_zoh, zoh_pcts):
        ax.text(bar.get_width() + 1, bar.get_y() + bar.get_height() / 2,
                f"{pct:.1f}%", va="center", fontsize=7)

    ax.set_yticks(list(y))
    ax.set_yticklabels(ds_names, fontsize=8)
    ax.set_xlabel("File Size (% of CSV)")
    ax.set_title("Compression: File Size Relative to CSV",
                 fontsize=12, fontweight="bold")
    ax.legend(fontsize=8)
    ax.set_xlim(0, max(max(flex_pcts, default=100), 100) * 1.15)

    plt.tight_layout()
    path = output_dir / "chart_compression.png"
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Chart: {path}")


def generate_charts(data, output_dir):
    """Generate all charts from benchmark data."""
    if not HAS_MATPLOTLIB:
        return

    output_dir = Path(output_dir)

    if "macro" not in data or "results" not in data["macro"]:
        return

    datasets = group_by_dataset(data["macro"]["results"])

    print("Generating charts...")
    chart_total_time(datasets, output_dir)
    chart_file_size(datasets, output_dir)
    chart_throughput(datasets, output_dir)
    chart_compression(datasets, output_dir)


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

    # Generate charts
    generate_charts(data, results_dir)

    print(f"\nReport complete. See: {results_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
