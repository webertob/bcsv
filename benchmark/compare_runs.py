#!/usr/bin/env python3
"""
BCSV Benchmark Regression Detector

Compares two benchmark runs and flags regressions.  Can also maintain a
persistent leaderboard of best-ever results per (profile x mode x size).

Usage:
    # Compare two explicit run directories:
    python compare_runs.py <baseline_dir> <candidate_dir>

    # Compare latest run against previous:
    python compare_runs.py --latest [--hostname=NAME]

    # Show leaderboard:
    python compare_runs.py --leaderboard [--hostname=NAME]

Options:
    --threshold=N     Regression threshold percentage (default: 5)
    --hostname=NAME   Override hostname (default: current)
    --update-leaderboard   Update leaderboard with new bests from candidate
    --output=PATH     Write diff report to file (default: stdout)
    --json            Output JSON instead of Markdown
"""

import argparse
import json
import os
import socket
import sys
from datetime import datetime
from pathlib import Path


def get_project_root():
    p = Path(__file__).resolve().parent.parent
    if (p / "CMakeLists.txt").exists():
        return p
    raise RuntimeError(f"Cannot find project root from {__file__}")


def get_runs_dir(hostname=None):
    root = get_project_root()
    hostname = hostname or socket.gethostname()
    return root / "benchmark" / "results" / hostname


def find_latest_runs(runs_dir, count=2):
    """Find the N most recent run directories (sorted by timestamp)."""
    if not runs_dir.exists():
        return []
    dirs = sorted(
        [d for d in runs_dir.iterdir() if d.is_dir()],
        key=lambda d: d.name,
        reverse=True,
    )
    return dirs[:count]


def load_macro_results(run_dir):
    """Load macro results from a run directory, keyed by (dataset, mode)."""
    path = run_dir / "macro_results.json"
    if not path.exists():
        return {}
    data = json.loads(path.read_text())
    results = {}
    for r in data.get("results", []):
        key = (r.get("dataset", "?"), r.get("mode", "?"))
        results[key] = r
    return results


def load_external_results(run_dir):
    """Load external CSV results, keyed by (dataset, mode)."""
    path = run_dir / "external_results.json"
    if not path.exists():
        return {}
    data = json.loads(path.read_text())
    results = {}
    for r in data.get("results", []):
        key = (r.get("dataset", "?"), r.get("mode", "?"))
        results[key] = r
    return results


def find_row_count_mismatches(baseline, candidate):
    """Return list of (dataset, mode, baseline_rows, candidate_rows) mismatches."""
    mismatches = []
    shared_keys = set(baseline.keys()) & set(candidate.keys())
    for key in sorted(shared_keys):
        base_rows = baseline[key].get("num_rows")
        cand_rows = candidate[key].get("num_rows")

        # Only enforce when both sides provide row metadata.
        if base_rows is None or cand_rows is None:
            continue
        if base_rows != cand_rows:
            ds, mode = key
            mismatches.append((ds, mode, base_rows, cand_rows))

    return mismatches


# ============================================================================
# Comparison
# ============================================================================

def compare_results(baseline, candidate, threshold_pct=5.0):
    """
    Compare two result sets.  Returns a list of comparison dicts:
    {
      'dataset': str, 'mode': str,
      'baseline_write_ms': float, 'candidate_write_ms': float, 'write_delta_pct': float,
      'baseline_read_ms': float, 'candidate_read_ms': float, 'read_delta_pct': float,
      'baseline_total_ms': float, 'candidate_total_ms': float, 'total_delta_pct': float,
      'baseline_size': int, 'candidate_size': int, 'size_delta_pct': float,
      'regression': bool,  # True if any metric regressed > threshold
      'regression_fields': [str],  # which metrics regressed
    }
    """
    comparisons = []

    all_keys = set(baseline.keys()) | set(candidate.keys())
    for key in sorted(all_keys):
        ds, mode = key
        base = baseline.get(key)
        cand = candidate.get(key)

        if not base or not cand:
            continue  # can't compare if missing from one side

        def delta_pct(base_val, cand_val):
            if base_val == 0:
                return 0
            return (cand_val - base_val) / base_val * 100

        base_w = base.get("write_time_ms", 0)
        cand_w = cand.get("write_time_ms", 0)
        base_r = base.get("read_time_ms", 0)
        cand_r = cand.get("read_time_ms", 0)
        base_total = base_w + base_r
        cand_total = cand_w + cand_r
        base_sz = base.get("file_size", 0)
        cand_sz = cand.get("file_size", 0)

        w_delta = delta_pct(base_w, cand_w)
        r_delta = delta_pct(base_r, cand_r)
        t_delta = delta_pct(base_total, cand_total)
        s_delta = delta_pct(base_sz, cand_sz)

        regression_fields = []
        if w_delta > threshold_pct:
            regression_fields.append("write")
        if r_delta > threshold_pct:
            regression_fields.append("read")
        if t_delta > threshold_pct:
            regression_fields.append("total")
        if s_delta > threshold_pct:
            regression_fields.append("size")

        comparisons.append({
            "dataset": ds,
            "mode": mode,
            "baseline_write_ms": base_w,
            "candidate_write_ms": cand_w,
            "write_delta_pct": w_delta,
            "baseline_read_ms": base_r,
            "candidate_read_ms": cand_r,
            "read_delta_pct": r_delta,
            "baseline_total_ms": base_total,
            "candidate_total_ms": cand_total,
            "total_delta_pct": t_delta,
            "baseline_size": base_sz,
            "candidate_size": cand_sz,
            "size_delta_pct": s_delta,
            "regression": len(regression_fields) > 0,
            "regression_fields": regression_fields,
        })

    return comparisons


# ============================================================================
# Report generation
# ============================================================================

def format_delta(pct):
    """Format a percentage delta with color indicators."""
    if pct > 5:
        return f"+{pct:.1f}% **REGRESSION**"
    elif pct > 2:
        return f"+{pct:.1f}%"
    elif pct < -5:
        return f"{pct:.1f}% (faster)"
    elif pct < -2:
        return f"{pct:.1f}%"
    else:
        return f"{pct:+.1f}%"


def generate_markdown_report(comparisons, baseline_dir, candidate_dir, threshold):
    """Generate a Markdown diff report."""
    lines = []
    lines.append("# BCSV Benchmark Comparison Report\n")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M')}\n")
    lines.append(f"- Baseline: `{baseline_dir.name}`")
    lines.append(f"- Candidate: `{candidate_dir.name}`")
    lines.append(f"- Regression threshold: {threshold}%\n")

    regressions = [c for c in comparisons if c["regression"]]
    improvements = [c for c in comparisons if c["total_delta_pct"] < -threshold]

    if regressions:
        lines.append(f"**{len(regressions)} REGRESSION(S) DETECTED**\n")
        for r in regressions:
            lines.append(f"- `{r['dataset']}/{r['mode']}`: "
                         f"{', '.join(r['regression_fields'])} regressed")
        lines.append("")
    else:
        lines.append("**No regressions detected.**\n")

    if improvements:
        lines.append(f"**{len(improvements)} improvement(s):**\n")
        for r in improvements:
            lines.append(f"- `{r['dataset']}/{r['mode']}`: "
                         f"total {r['total_delta_pct']:.1f}%")
        lines.append("")

    # Full comparison table
    lines.append("## Detailed Comparison\n")
    lines.append("| Dataset | Mode | Base Total (ms) | Cand Total (ms)"
                 " | Delta | Base Size (MB) | Cand Size (MB) | Size Delta |")
    lines.append("|---------|------|----------------:|----------------:"
                 "|------:|---------------:|---------------:|----------:|")

    for c in comparisons:
        base_sz_mb = c["baseline_size"] / (1024 * 1024)
        cand_sz_mb = c["candidate_size"] / (1024 * 1024)
        t_delta = format_delta(c["total_delta_pct"])
        s_delta = format_delta(c["size_delta_pct"])

        lines.append(
            f"| {c['dataset']} | {c['mode']}"
            f" | {c['baseline_total_ms']:.1f} | {c['candidate_total_ms']:.1f}"
            f" | {t_delta}"
            f" | {base_sz_mb:.2f} | {cand_sz_mb:.2f}"
            f" | {s_delta} |"
        )

    lines.append("")

    # Write/read breakdown for regressions
    if regressions:
        lines.append("## Regression Details\n")
        lines.append("| Dataset | Mode | Metric | Baseline (ms) | Candidate (ms) | Delta |")
        lines.append("|---------|------|--------|-------------:|---------------:|------:|")
        for c in regressions:
            for field in c["regression_fields"]:
                base_val = c[f"baseline_{field}_ms"] if field != "size" else c["baseline_size"]
                cand_val = c[f"candidate_{field}_ms"] if field != "size" else c["candidate_size"]
                delta = c[f"{field}_delta_pct"]
                lines.append(
                    f"| {c['dataset']} | {c['mode']} | {field}"
                    f" | {base_val:.1f} | {cand_val:.1f} | {format_delta(delta)} |"
                )

    return "\n".join(lines)


# ============================================================================
# Leaderboard
# ============================================================================

def load_leaderboard(runs_dir):
    """Load or create the leaderboard file."""
    path = runs_dir / "leaderboard.json"
    if path.exists():
        return json.loads(path.read_text())
    return {"updated": None, "entries": {}}


def update_leaderboard(leaderboard, results, run_dir_name, git_version=None):
    """Update leaderboard with any new bests from results."""
    updated = False
    for (ds, mode), r in results.items():
        total = r.get("write_time_ms", 0) + r.get("read_time_ms", 0)
        if total <= 0:
            continue

        key = f"{ds}/{mode}"
        entry = leaderboard["entries"].get(key)

        if entry is None or total < entry.get("best_total_ms", float("inf")):
            leaderboard["entries"][key] = {
                "dataset": ds,
                "mode": mode,
                "best_total_ms": total,
                "best_write_ms": r.get("write_time_ms", 0),
                "best_read_ms": r.get("read_time_ms", 0),
                "file_size": r.get("file_size", 0),
                "num_rows": r.get("num_rows", 0),
                "run": run_dir_name,
                "git_version": git_version or "unknown",
                "timestamp": datetime.now().isoformat(),
            }
            updated = True

    if updated:
        leaderboard["updated"] = datetime.now().isoformat()

    return updated


def save_leaderboard(runs_dir, leaderboard):
    path = runs_dir / "leaderboard.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as f:
        json.dump(leaderboard, f, indent=2)
    print(f"Leaderboard saved to: {path}")


def print_leaderboard(leaderboard):
    """Print the leaderboard in a nice table."""
    print(f"\nBCSV Benchmark Leaderboard")
    print(f"{'='*90}")
    print(f"Last updated: {leaderboard.get('updated', 'never')}\n")

    entries = leaderboard.get("entries", {})
    if not entries:
        print("No entries yet.")
        return

    print(f"{'Key':<40} {'Total (ms)':>10} {'Run':>20} {'Git':>15}")
    print("-" * 90)

    for key in sorted(entries.keys()):
        e = entries[key]
        print(f"{key:<40} {e['best_total_ms']:>10.1f}"
              f" {e.get('run', '?'):>20}"
              f" {e.get('git_version', '?'):>15}")


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="BCSV Benchmark Regression Detector")
    parser.add_argument("dirs", nargs="*", help="baseline_dir candidate_dir")
    parser.add_argument("--latest", action="store_true",
                        help="Compare the two most recent runs")
    parser.add_argument("--leaderboard", action="store_true",
                        help="Show the leaderboard")
    parser.add_argument("--threshold", type=float, default=5.0,
                        help="Regression threshold percentage (default: 5)")
    parser.add_argument("--hostname", default=None,
                        help="Override hostname for run directory")
    parser.add_argument("--update-leaderboard", action="store_true",
                        help="Update leaderboard with new bests from candidate")
    parser.add_argument("--output", default=None,
                        help="Write report to file (default: stdout)")
    parser.add_argument("--json", action="store_true",
                        help="Output JSON instead of Markdown")
    parser.add_argument("--allow-mismatched-rows", action="store_true",
                        help="Allow comparison when dataset/mode num_rows differ")

    args = parser.parse_args()
    runs_dir = get_runs_dir(args.hostname)

    # Leaderboard mode
    if args.leaderboard:
        lb = load_leaderboard(runs_dir)
        print_leaderboard(lb)
        return 0

    # Determine baseline and candidate directories
    if args.latest:
        recent = find_latest_runs(runs_dir, 2)
        if len(recent) < 2:
            print(f"ERROR: Need at least 2 runs in {runs_dir}, found {len(recent)}")
            return 1
        candidate_dir = recent[0]
        baseline_dir = recent[1]
        print(f"Comparing latest: {baseline_dir.name} → {candidate_dir.name}")
    elif len(args.dirs) == 2:
        baseline_dir = Path(args.dirs[0])
        candidate_dir = Path(args.dirs[1])
    else:
        parser.print_help()
        return 1

    if not baseline_dir.exists():
        print(f"ERROR: Baseline directory not found: {baseline_dir}")
        return 1
    if not candidate_dir.exists():
        print(f"ERROR: Candidate directory not found: {candidate_dir}")
        return 1

    # Load results
    baseline = load_macro_results(baseline_dir)
    candidate = load_macro_results(candidate_dir)

    if not baseline:
        print(f"ERROR: No macro results in baseline: {baseline_dir}")
        return 1
    if not candidate:
        print(f"ERROR: No macro results in candidate: {candidate_dir}")
        return 1

    mismatches = find_row_count_mismatches(baseline, candidate)
    if mismatches and not args.allow_mismatched_rows:
        print("ERROR: Workload mismatch detected (num_rows differs for matching dataset/mode).")
        print("Refusing to compare to avoid false regression reports.")
        print("Use --allow-mismatched-rows to override.")
        print("\nExamples:")
        for ds, mode, base_rows, cand_rows in mismatches[:10]:
            print(f"  - {ds}/{mode}: baseline={base_rows}, candidate={cand_rows}")
        if len(mismatches) > 10:
            print(f"  ... and {len(mismatches) - 10} more")
        return 2

    # Compare
    comparisons = compare_results(baseline, candidate, args.threshold)

    if args.json:
        output = json.dumps(comparisons, indent=2)
    else:
        output = generate_markdown_report(comparisons, baseline_dir, candidate_dir,
                                           args.threshold)

    if args.output:
        Path(args.output).write_text(output)
        print(f"Report written to: {args.output}")
    else:
        print(output)

    # Leaderboard update
    if args.update_leaderboard:
        # Save leaderboard alongside the candidate run
        lb_dir = candidate_dir.parent
        lb_dir.mkdir(parents=True, exist_ok=True)
        lb = load_leaderboard(lb_dir)

        # Try to get git version from candidate's platform.json
        git_ver = None
        platform_path = candidate_dir / "platform.json"
        if platform_path.exists():
            plat = json.loads(platform_path.read_text())
            git_ver = plat.get("git_describe")

        if update_leaderboard(lb, candidate, candidate_dir.name, git_ver):
            save_leaderboard(lb_dir, lb)
            print("Leaderboard updated with new best(s).")
        else:
            print("No new bests — leaderboard unchanged.")

    # Return non-zero if regressions found
    has_regression = any(c["regression"] for c in comparisons)
    if has_regression:
        print(f"\n*** {sum(1 for c in comparisons if c['regression'])} "
              f"regression(s) detected (threshold: {args.threshold}%) ***")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
