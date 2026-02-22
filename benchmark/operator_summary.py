#!/usr/bin/env python3

import json
import statistics
import sys
from pathlib import Path

# Import canonical constants from the shared library
sys.path.insert(0, str(Path(__file__).resolve().parent))
from lib.constants import MODE_ALIASES, mode_base, pick_mode_rows  # noqa: E402


def _median_or_none(values: list[float]) -> float | None:
    if not values:
        return None
    return float(statistics.median(values))


def _stdev_or_none(values: list[float]) -> float | None:
    if len(values) < 2:
        return None
    return float(statistics.stdev(values))


def _cv_flag(median: float | None, stdev: float | None) -> str:
    """Return a noise marker when coefficient of variation exceeds 1.0."""
    if median is None or stdev is None or median == 0:
        return ""
    return " ~" if stdev / abs(median) > 1.0 else ""


def _filter_ok(rows: list[dict], aliases: list[str]) -> list[dict]:
    """Pick rows matching *aliases*, excluding skipped results."""
    return [r for r in pick_mode_rows(rows, aliases) if r.get("status") != "skipped"]


def compute_macro_operator_summary(rows: list[dict]) -> list[dict]:
    if not rows:
        return []

    csv_sizes: dict[tuple[str, str], float] = {}
    for row in rows:
        if mode_base(str(row.get("mode", ""))) == "CSV":
            key = (str(row.get("dataset", "?")), str(row.get("scenario_id", "baseline")))
            value = row.get("file_size")
            if isinstance(value, (int, float)) and value > 0:
                csv_sizes[key] = float(value)

    # Count total profiles in the baseline scenario (for coverage annotation)
    total_profiles = len({
        r.get("dataset") for r in rows
        if r.get("scenario_id", "baseline") == "baseline"
        and mode_base(str(r.get("mode", ""))) == "CSV"
    })

    summary = []
    for display_mode, aliases in MODE_ALIASES.items():
        mode_rows = _filter_ok(rows, aliases)
        dense_rows = [row for row in mode_rows if row.get("scenario_id", "baseline") == "baseline"]

        write_vals = [float(row["write_rows_per_sec"]) for row in dense_rows if isinstance(row.get("write_rows_per_sec"), (int, float)) and row["write_rows_per_sec"] > 0]
        read_vals = [float(row["read_rows_per_sec"]) for row in dense_rows if isinstance(row.get("read_rows_per_sec"), (int, float)) and row["read_rows_per_sec"] > 0]

        comp_vals = []
        for row in mode_rows:
            if mode_base(str(row.get("mode", ""))) == "CSV":
                comp_vals.append(1.0)
                continue
            key = (str(row.get("dataset", "?")), str(row.get("scenario_id", "baseline")))
            csv_size = csv_sizes.get(key)
            mode_size = row.get("file_size")
            if csv_size and isinstance(mode_size, (int, float)) and mode_size > 0:
                comp_vals.append(float(mode_size) / csv_size)

        # Distinct profiles that contributed to the dense (baseline) stats
        profile_count = len({r.get("dataset") for r in dense_rows})

        summary.append({
            "mode": display_mode,
            "compression": _median_or_none(comp_vals),
            "dense_write": _median_or_none(write_vals),
            "dense_read": _median_or_none(read_vals),
            "write_stdev": _stdev_or_none(write_vals),
            "read_stdev": _stdev_or_none(read_vals),
            "profile_count": profile_count,
            "profile_total": total_profiles,
        })

    return summary


def compute_profile_breakdown(rows: list[dict]) -> list[dict]:
    """Per-profile baseline breakdown for all modes.

    Returns a list of dicts: {profile, mode, compression, write, read}.
    Skipped rows are excluded.
    """
    if not rows:
        return []

    csv_sizes: dict[str, float] = {}
    for row in rows:
        if (mode_base(str(row.get("mode", ""))) == "CSV"
                and row.get("scenario_id", "baseline") == "baseline"):
            ds = str(row.get("dataset", "?"))
            sz = row.get("file_size")
            if isinstance(sz, (int, float)) and sz > 0:
                csv_sizes[ds] = float(sz)

    out: list[dict] = []
    for display_mode, aliases in MODE_ALIASES.items():
        mode_rows = _filter_ok(rows, aliases)
        baselines = [r for r in mode_rows if r.get("scenario_id", "baseline") == "baseline"]
        for r in baselines:
            ds = str(r.get("dataset", "?"))
            w = r.get("write_rows_per_sec")
            rd = r.get("read_rows_per_sec")
            sz = r.get("file_size")
            csv_sz = csv_sizes.get(ds)
            comp = float(sz) / csv_sz if csv_sz and isinstance(sz, (int, float)) and sz > 0 else None
            if display_mode == "CSV":
                comp = 1.0
            out.append({
                "profile": ds,
                "mode": display_mode,
                "compression": comp,
                "write": float(w) if isinstance(w, (int, float)) and w > 0 else None,
                "read": float(rd) if isinstance(rd, (int, float)) and rd > 0 else None,
            })
    return out


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


def print_operator_summary(out_dir: Path, run_types: list[str], macro_payloads: dict[str, dict],
                           *, detail: bool = False) -> None:
    print("\nOperator Summary")
    print(f"  Run directory: {out_dir}")

    for run_type in ["MACRO-SMALL", "MACRO-LARGE"]:
        payload = macro_payloads.get(run_type)
        if not payload:
            continue

        rows = payload.get("results", []) if isinstance(payload, dict) else []
        summary = compute_macro_operator_summary(rows)

        # Wall-clock timing
        total_sec = payload.get("total_time_sec")
        timing_note = f" — {total_sec:.1f}s" if isinstance(total_sec, (int, float)) else ""
        print(f"  {run_type} (median, baseline-scenario){timing_note}:")
        print("    Mode                 Comp(vsCSV)   Write(rows/s)   Read(rows/s)  Profiles")
        for row in summary:
            comp = f"{row['compression']:.3f}" if row["compression"] is not None else "-"
            write_str = f"{row['dense_write']:.0f}" if row["dense_write"] is not None else "-"
            read_str = f"{row['dense_read']:.0f}" if row["dense_read"] is not None else "-"
            # Noise flag: append ~ when stdev > median
            write_str += _cv_flag(row["dense_write"], row.get("write_stdev"))
            read_str += _cv_flag(row["dense_read"], row.get("read_stdev"))
            coverage = f"{row['profile_count']}/{row['profile_total']}"
            print(f"    {row['mode']:<20} {comp:>11} {write_str:>15} {read_str:>14}  {coverage:>8}")

        if detail:
            breakdown = compute_profile_breakdown(rows)
            if breakdown:
                profiles = sorted({r["profile"] for r in breakdown})
                modes = list(MODE_ALIASES.keys())
                print(f"\n    Per-profile breakdown ({run_type}, baseline, rows/s):")
                # Header
                hdr = f"    {'Profile':<28}"
                for m in modes:
                    hdr += f" {m:>16}"
                print(hdr)
                # Rows
                for profile in profiles:
                    line = f"    {profile:<28}"
                    for m in modes:
                        match = [r for r in breakdown if r["profile"] == profile and r["mode"] == m]
                        if match and match[0]["write"] is not None:
                            line += f" {match[0]['write']:>16,.0f}"
                        else:
                            line += f" {'—':>16}"
                    print(line)

        # Note for partial coverage
        partial = [r for r in summary if r["profile_count"] < r["profile_total"]]
        if partial:
            names = ", ".join(r["mode"] for r in partial)
            print(f"    Note: {names} — partial coverage (not all profiles support static mode)")

        print()  # blank line between sections

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
