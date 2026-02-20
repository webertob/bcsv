#!/usr/bin/env python3

import argparse
import json
import ssl
import sys
import urllib.request
from pathlib import Path


def project_root() -> Path:
    root = Path(__file__).resolve().parent.parent
    if not (root / "CMakeLists.txt").exists():
        raise RuntimeError(f"Cannot resolve project root from {__file__}")
    return root


def load_manifest(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_ids(raw: str | None) -> set[str] | None:
    if not raw:
        return None
    values = {item.strip() for item in raw.split(",") if item.strip()}
    return values or None


def download_file(url: str, target: Path, timeout: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    context = ssl.create_default_context()
    with urllib.request.urlopen(url, timeout=timeout, context=context) as response:
        target.write_bytes(response.read())


def main() -> int:
    parser = argparse.ArgumentParser(description="Download open reference time-series datasets for BCSV benchmarks")
    parser.add_argument("--manifest", default="benchmark/reference_workloads.json", help="Manifest JSON path")
    parser.add_argument("--ids", default="", help="Comma-separated workload ids to fetch")
    parser.add_argument("--cache-root", default="", help="Override cache root")
    parser.add_argument("--timeout", type=int, default=60, help="Per-download timeout in seconds")
    parser.add_argument("--force", action="store_true", help="Re-download even if file exists")
    args = parser.parse_args()

    root = project_root()
    manifest_path = root / args.manifest
    if not manifest_path.exists():
        raise FileNotFoundError(f"Manifest not found: {manifest_path}")

    manifest = load_manifest(manifest_path)
    selected_ids = parse_ids(args.ids)
    default_cache = manifest.get("cache_root", "tmp/reference_datasets")
    cache_root = (root / args.cache_root) if args.cache_root else (root / default_cache)

    workloads = manifest.get("workloads", [])
    if selected_ids is not None:
        workloads = [item for item in workloads if item.get("id") in selected_ids]

    if not workloads:
        print("No workloads selected.")
        return 0

    downloaded = 0
    skipped = 0

    for workload in workloads:
        workload_id = workload["id"]
        url = workload["url"]
        suffix = Path(url).suffix or ".csv"
        target = cache_root / workload_id / f"source{suffix}"

        if target.exists() and not args.force:
            skipped += 1
            print(f"[skip] {workload_id}: {target}")
            continue

        print(f"[fetch] {workload_id}: {url}")
        try:
            download_file(url, target, args.timeout)
        except Exception as exc:
            print(f"[error] {workload_id}: {exc}")
            return 2

        downloaded += 1
        print(f"[ok] {workload_id}: {target}")

    print(f"Done. downloaded={downloaded}, skipped={skipped}, cache_root={cache_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
