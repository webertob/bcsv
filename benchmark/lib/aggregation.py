"""Multi-repetition aggregation utilities.

Canonical implementations of median-based aggregation for macro and micro
benchmark payloads.  Imported by every orchestrator — no copy-paste.
"""

from __future__ import annotations

import json
import statistics
from pathlib import Path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def write_json(path: Path, payload) -> None:
    """Write *payload* as indented JSON to *path*."""
    path.write_text(json.dumps(payload, indent=2))


def read_json(path: Path):
    """Read and parse a JSON file."""
    return json.loads(path.read_text())


def median_value(values):
    """Compute median, preserving int type when all inputs are int."""
    med = statistics.median(values)
    if all(isinstance(v, int) and not isinstance(v, bool) for v in values):
        return int(round(med))
    return float(med)


# ---------------------------------------------------------------------------
# Dict-level aggregation
# ---------------------------------------------------------------------------

def aggregate_dicts(samples: list[dict]) -> dict:
    """Merge a list of structurally-similar dicts using per-key median."""
    if not samples:
        return {}

    merged = dict(samples[0])
    all_keys: set[str] = set()
    for s in samples:
        all_keys.update(s.keys())

    for key in all_keys:
        values = [s.get(key) for s in samples if key in s]
        if len(values) != len(samples):
            continue
        if all(isinstance(v, bool) for v in values):
            merged[key] = all(values)
        elif all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in values):
            merged[key] = median_value(values)
        else:
            merged[key] = values[0]

    return merged


# ---------------------------------------------------------------------------
# Macro aggregation
# ---------------------------------------------------------------------------

def _macro_row_key(row: dict) -> tuple:
    return (
        row.get("dataset", "?"),
        row.get("mode", "?"),
        row.get("scenario_id", "baseline"),
        row.get("access_path", "-"),
        row.get("selected_columns", row.get("num_columns", 0)),
    )


def aggregate_macro_payloads(payloads: list[dict], run_type: str) -> dict | None:
    """Aggregate multiple macro repetition payloads via median."""
    if not payloads:
        return None

    result_map: dict[tuple, list[dict]] = {}
    for payload in payloads:
        for row in payload.get("results", []):
            result_map.setdefault(_macro_row_key(row), []).append(row)

    merged_rows = [aggregate_dicts(result_map[k]) for k in sorted(result_map)]

    out = dict(payloads[0])
    totals = [
        p.get("total_time_sec")
        for p in payloads
        if isinstance(p.get("total_time_sec"), (int, float))
    ]
    if totals:
        out["total_time_sec"] = float(statistics.median(totals))

    out["run_type"] = run_type
    out["results"] = merged_rows
    out["aggregation"] = {"method": "median", "repetitions": len(payloads)}
    return out


def aggregate_micro_payloads(payloads: list[dict]) -> dict | None:
    """Aggregate multiple micro repetition payloads via median."""
    if not payloads:
        return None

    bm_map: dict[str, list[dict]] = {}
    for payload in payloads:
        for bm in payload.get("benchmarks", []):
            bm_map.setdefault(bm.get("name", "?"), []).append(bm)

    merged = [aggregate_dicts(bm_map[k]) for k in sorted(bm_map)]

    out = dict(payloads[0])
    out["benchmarks"] = merged
    out["aggregation"] = {"method": "median", "repetitions": len(payloads)}
    return out


def merge_macro_payloads(payloads_by_type: dict[str, dict]) -> dict | None:
    """Merge MACRO-SMALL + MACRO-LARGE into a single payload."""
    if not payloads_by_type:
        return None

    merged_results: list[dict] = []
    for rt in ("MACRO-SMALL", "MACRO-LARGE"):
        payload = payloads_by_type.get(rt)
        if not payload:
            continue
        for row in payload.get("results", []):
            r = dict(row)
            r["run_type"] = rt
            merged_results.append(r)

    base = dict(next(iter(payloads_by_type.values())))
    totals = [
        p.get("total_time_sec")
        for p in payloads_by_type.values()
        if isinstance(p.get("total_time_sec"), (int, float))
    ]
    if totals:
        base["total_time_sec"] = float(sum(totals))

    base["run_types"] = [
        rt for rt in ("MACRO-SMALL", "MACRO-LARGE") if rt in payloads_by_type
    ]
    base["results"] = merged_results
    return base
