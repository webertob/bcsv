#!/usr/bin/env python3

import json
import statistics
from pathlib import Path


MODE_ALIASES = {
    "CSV": ["CSV"],
    "BCSV Flex Flat": ["BCSV Flexible", "BCSV Flat", "BCSV Standard", "BCSV Flexible Flat"],
    "BCSV Flex-ZoH": ["BCSV Flexible ZoH", "BCSV ZoH", "BCSV Flexible-ZoH"],
    "BCSV Static Flat": ["BCSV Static", "BCSV Static Flat", "BCSV Static Standard"],
    "BCSV Static-ZoH": ["BCSV Static ZoH", "BCSV Static-ZoH"],
}


def _median_or_none(values: list[float]) -> float | None:
    if not values:
        return None
    return float(statistics.median(values))


def compute_macro_operator_summary(rows: list[dict]) -> list[dict]:
    if not rows:
        return []

    csv_sizes: dict[tuple[str, str], float] = {}
    for row in rows:
        if row.get("mode") == "CSV":
            key = (str(row.get("dataset", "?")), str(row.get("scenario_id", "baseline")))
            value = row.get("file_size")
            if isinstance(value, (int, float)) and value > 0:
                csv_sizes[key] = float(value)

    summary = []
    for display_mode, aliases in MODE_ALIASES.items():
        mode_rows = [row for row in rows if row.get("mode") in aliases]
        dense_rows = [row for row in mode_rows if row.get("scenario_id", "baseline") == "baseline"]

        write_vals = [float(row["write_rows_per_sec"]) for row in dense_rows if isinstance(row.get("write_rows_per_sec"), (int, float))]
        read_vals = [float(row["read_rows_per_sec"]) for row in dense_rows if isinstance(row.get("read_rows_per_sec"), (int, float))]

        comp_vals = []
        for row in mode_rows:
            if row.get("mode") == "CSV":
                comp_vals.append(1.0)
                continue
            key = (str(row.get("dataset", "?")), str(row.get("scenario_id", "baseline")))
            csv_size = csv_sizes.get(key)
            mode_size = row.get("file_size")
            if csv_size and isinstance(mode_size, (int, float)) and mode_size > 0:
                comp_vals.append(float(mode_size) / csv_size)

        summary.append({
            "mode": display_mode,
            "compression": _median_or_none(comp_vals),
            "dense_write": _median_or_none(write_vals),
            "dense_read": _median_or_none(read_vals),
        })

    return summary


def compute_micro_operator_summary(micro_payload: dict) -> dict[str, float]:
    benchmarks = micro_payload.get("benchmarks", []) if isinstance(micro_payload, dict) else []
    groups = {
        "Get": [b.get("real_time") for b in benchmarks if str(b.get("name", "")).startswith("BM_Get_") and isinstance(b.get("real_time"), (int, float))],
        "Set": [b.get("real_time") for b in benchmarks if str(b.get("name", "")).startswith("BM_Set_") and isinstance(b.get("real_time"), (int, float))],
        "Visit": [b.get("real_time") for b in benchmarks if "Visit" in str(b.get("name", "")) and isinstance(b.get("real_time"), (int, float))],
        "Serialize": [b.get("real_time") for b in benchmarks if "Serialize" in str(b.get("name", "")) and isinstance(b.get("real_time"), (int, float))],
    }
    out: dict[str, float] = {}
    for key, values in groups.items():
        median = _median_or_none([float(value) for value in values])
        if median is not None:
            out[key] = median
    return out


def print_operator_summary(out_dir: Path, run_types: list[str], macro_payloads: dict[str, dict]) -> None:
    print("\nOperator Summary")
    print(f"  Run directory: {out_dir}")

    for run_type in ["MACRO-SMALL", "MACRO-LARGE"]:
        payload = macro_payloads.get(run_type)
        if not payload:
            continue

        rows = payload.get("results", []) if isinstance(payload, dict) else []
        summary = compute_macro_operator_summary(rows)
        print(f"  {run_type} (median, baseline-scenario):")
        print("    Mode                 Comp(vsCSV)   Write(rows/s)   Read(rows/s)")
        for row in summary:
            comp = f"{row['compression']:.3f}" if row["compression"] is not None else "-"
            write = f"{row['dense_write']:.0f}" if row["dense_write"] is not None else "-"
            read = f"{row['dense_read']:.0f}" if row["dense_read"] is not None else "-"
            print(f"    {row['mode']:<20} {comp:>11} {write:>15} {read:>14}")

    if "MICRO" in run_types:
        micro_path = out_dir / "micro_results.json"
        if micro_path.exists():
            micro_summary = compute_micro_operator_summary(json.loads(micro_path.read_text()))
            if micro_summary:
                print("  MICRO (median real_time, ns):")
                for key in ["Get", "Set", "Visit", "Serialize"]:
                    if key in micro_summary:
                        print(f"    {key:<9} {micro_summary[key]:.1f}")

    report_path = out_dir / "report.md"
    if report_path.exists():
        print(f"  Report: {report_path}")
    condensed_path = out_dir / "condensed_metrics.json"
    if condensed_path.exists():
        print(f"  Condensed metrics: {condensed_path}")
