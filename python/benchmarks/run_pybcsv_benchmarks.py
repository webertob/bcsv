#!/usr/bin/env python3

import argparse
import hashlib
import json
import random
import socket
import time
from datetime import datetime
from pathlib import Path

import pybcsv

try:
    import numpy as np
except ImportError:
    np = None

try:
    import pandas as pd
except ImportError:
    pd = None


SIZE_TO_ROWS = {
    "S": 10_000,
    "M": 100_000,
    "L": 500_000,
}

MODE_LABELS = {
    "plain": "PYBCSV Plain",
    "numpy": "PYBCSV NumPy",
    "pandas": "PYBCSV Pandas",
}


def project_root() -> Path:
    root = Path(__file__).resolve().parent.parent.parent
    if not (root / "CMakeLists.txt").exists():
        raise RuntimeError(f"Cannot resolve project root from {__file__}")
    return root


def parse_csv_arg(raw: str) -> list[str]:
    return [item.strip() for item in raw.split(",") if item.strip()]


def workload_specs() -> dict:
    return {
        "weather_timeseries": {
            "columns": [
                ("timestamp", pybcsv.ColumnType.INT64),
                ("station", pybcsv.ColumnType.STRING),
                ("temperature", pybcsv.ColumnType.FLOAT),
                ("humidity", pybcsv.ColumnType.FLOAT),
                ("pressure", pybcsv.ColumnType.DOUBLE),
                ("wind_speed", pybcsv.ColumnType.FLOAT),
                ("raining", pybcsv.ColumnType.BOOL),
                ("quality", pybcsv.ColumnType.UINT8),
            ]
        },
        "iot_fleet": {
            "columns": [
                ("timestamp", pybcsv.ColumnType.INT64),
                ("device_id", pybcsv.ColumnType.UINT32),
                ("firmware", pybcsv.ColumnType.STRING),
                ("region", pybcsv.ColumnType.STRING),
                ("battery", pybcsv.ColumnType.FLOAT),
                ("temperature", pybcsv.ColumnType.FLOAT),
                ("vibration", pybcsv.ColumnType.DOUBLE),
                ("online", pybcsv.ColumnType.BOOL),
            ]
        },
        "financial_orders": {
            "columns": [
                ("timestamp", pybcsv.ColumnType.INT64),
                ("symbol", pybcsv.ColumnType.STRING),
                ("venue", pybcsv.ColumnType.STRING),
                ("side", pybcsv.ColumnType.STRING),
                ("qty", pybcsv.ColumnType.INT32),
                ("price", pybcsv.ColumnType.DOUBLE),
                ("order_type", pybcsv.ColumnType.STRING),
                ("trader", pybcsv.ColumnType.STRING),
                ("is_cancel", pybcsv.ColumnType.BOOL),
            ]
        },
    }


def build_layout(columns: list[tuple[str, object]]) -> object:
    layout = pybcsv.Layout()
    for name, col_type in columns:
        layout.add_column(name, col_type)
    return layout


def build_plain_rows(workload: str, num_rows: int, seed: int) -> list[list]:
    rng = random.Random(seed)
    rows = []
    stations = ["SEA", "HAM", "MUC", "SFO", "NYC"]
    regions = ["eu-west", "us-east", "ap-south"]
    firmware = ["1.2.0", "1.2.1", "1.3.0", "1.3.1"]
    symbols = ["AAPL", "MSFT", "NVDA", "AMZN", "GOOG"]
    venues = ["XNAS", "XNYS", "BATS"]
    sides = ["BUY", "SELL"]
    order_types = ["LMT", "MKT", "IOC"]
    traders = ["T1", "T2", "T3", "T4"]

    for row_idx in range(num_rows):
        ts = 1_700_000_000 + row_idx

        if workload == "weather_timeseries":
            rows.append([
                ts,
                stations[row_idx % len(stations)],
                float(12.0 + 8.0 * (row_idx % 97) / 97.0 + rng.uniform(-0.5, 0.5)),
                float(45.0 + 40.0 * (row_idx % 53) / 53.0 + rng.uniform(-1.0, 1.0)),
                float(1000.0 + 15.0 * (row_idx % 41) / 41.0 + rng.uniform(-0.2, 0.2)),
                float(3.0 + 10.0 * (row_idx % 31) / 31.0),
                (row_idx % 17) == 0,
                int(row_idx % 4),
            ])
        elif workload == "iot_fleet":
            rows.append([
                ts,
                int(1000 + (row_idx % 5000)),
                firmware[row_idx % len(firmware)],
                regions[row_idx % len(regions)],
                float(100.0 - (row_idx % 100) * 0.3),
                float(20.0 + (row_idx % 120) * 0.1),
                float((row_idx % 40) * 0.02 + rng.uniform(0.0, 0.01)),
                (row_idx % 29) != 0,
            ])
        elif workload == "financial_orders":
            rows.append([
                ts,
                symbols[row_idx % len(symbols)],
                venues[row_idx % len(venues)],
                sides[row_idx % len(sides)],
                int(1 + (row_idx % 5000)),
                float(90.0 + (row_idx % 200) * 0.05 + rng.uniform(-0.02, 0.02)),
                order_types[row_idx % len(order_types)],
                traders[row_idx % len(traders)],
                (row_idx % 23) == 0,
            ])
        else:
            raise ValueError(f"Unknown workload: {workload}")

    return rows


def build_numpy_rows(workload: str, num_rows: int, seed: int) -> list[list]:
    if np is None:
        raise RuntimeError("numpy is not available")

    rng = np.random.default_rng(seed)
    idx = np.arange(num_rows, dtype=np.int64)
    ts = 1_700_000_000 + idx

    if workload == "weather_timeseries":
        stations = np.array(["SEA", "HAM", "MUC", "SFO", "NYC"], dtype=object)
        rows = np.column_stack([
            ts,
            stations[idx % len(stations)],
            12.0 + 8.0 * ((idx % 97) / 97.0) + rng.uniform(-0.5, 0.5, num_rows),
            45.0 + 40.0 * ((idx % 53) / 53.0) + rng.uniform(-1.0, 1.0, num_rows),
            1000.0 + 15.0 * ((idx % 41) / 41.0) + rng.uniform(-0.2, 0.2, num_rows),
            3.0 + 10.0 * ((idx % 31) / 31.0),
            (idx % 17) == 0,
            (idx % 4).astype(np.uint8),
        ])
    elif workload == "iot_fleet":
        firmware = np.array(["1.2.0", "1.2.1", "1.3.0", "1.3.1"], dtype=object)
        regions = np.array(["eu-west", "us-east", "ap-south"], dtype=object)
        rows = np.column_stack([
            ts,
            (1000 + (idx % 5000)).astype(np.uint32),
            firmware[idx % len(firmware)],
            regions[idx % len(regions)],
            100.0 - (idx % 100) * 0.3,
            20.0 + (idx % 120) * 0.1,
            (idx % 40) * 0.02 + rng.uniform(0.0, 0.01, num_rows),
            (idx % 29) != 0,
        ])
    elif workload == "financial_orders":
        symbols = np.array(["AAPL", "MSFT", "NVDA", "AMZN", "GOOG"], dtype=object)
        venues = np.array(["XNAS", "XNYS", "BATS"], dtype=object)
        sides = np.array(["BUY", "SELL"], dtype=object)
        order_types = np.array(["LMT", "MKT", "IOC"], dtype=object)
        traders = np.array(["T1", "T2", "T3", "T4"], dtype=object)
        rows = np.column_stack([
            ts,
            symbols[idx % len(symbols)],
            venues[idx % len(venues)],
            sides[idx % len(sides)],
            (1 + (idx % 5000)).astype(np.int32),
            90.0 + (idx % 200) * 0.05 + rng.uniform(-0.02, 0.02, num_rows),
            order_types[idx % len(order_types)],
            traders[idx % len(traders)],
            (idx % 23) == 0,
        ])
    else:
        raise ValueError(f"Unknown workload: {workload}")

    return rows.tolist()


def build_pandas_rows(workload: str, num_rows: int, seed: int) -> list[list]:
    if pd is None:
        raise RuntimeError("pandas is not available")

    rows = build_numpy_rows(workload, num_rows, seed)
    if not rows:
        return rows

    if workload == "weather_timeseries":
        cols = ["timestamp", "station", "temperature", "humidity", "pressure", "wind_speed", "raining", "quality"]
    elif workload == "iot_fleet":
        cols = ["timestamp", "device_id", "firmware", "region", "battery", "temperature", "vibration", "online"]
    else:
        cols = ["timestamp", "symbol", "venue", "side", "qty", "price", "order_type", "trader", "is_cancel"]

    frame = pd.DataFrame(rows, columns=cols)
    return frame.values.tolist()


def write_rows(file_path: Path, layout, rows: list[list]) -> float:
    writer = pybcsv.Writer(layout)
    start = time.perf_counter()
    writer.open(str(file_path), True, 1, 64, pybcsv.FileFlags.NONE)
    writer.write_rows(rows)
    writer.close()
    return (time.perf_counter() - start) * 1000.0


def read_rows(file_path: Path) -> tuple[float, int]:
    reader = pybcsv.Reader()
    start = time.perf_counter()
    reader.open(str(file_path))
    count = 0
    while reader.read_next():
        count += 1
    reader.close()
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    return elapsed_ms, count


def mode_rows(mode: str, workload: str, num_rows: int, seed: int) -> list[list]:
    if mode == "plain":
        return build_plain_rows(workload, num_rows, seed)
    if mode == "numpy":
        return build_numpy_rows(workload, num_rows, seed)
    if mode == "pandas":
        return build_pandas_rows(workload, num_rows, seed)
    raise ValueError(f"Unknown mode: {mode}")


def stable_seed_offset(token: str, modulus: int) -> int:
    digest = hashlib.sha256(token.encode("utf-8")).hexdigest()
    return int(digest[:12], 16) % modulus


def run_one(mode: str, workload: str, columns: list[tuple[str, object]], num_rows: int, work_dir: Path, seed: int) -> dict:
    layout = build_layout(columns)
    rows = mode_rows(mode, workload, num_rows, seed)
    file_path = work_dir / f"{workload}_{mode}.bcsv"

    write_ms = write_rows(file_path, layout, rows)
    read_ms, counted_rows = read_rows(file_path)
    col_count = len(columns)

    return {
        "dataset": workload,
        "mode": MODE_LABELS[mode],
        "scenario_id": "baseline",
        "access_path": "dense",
        "selected_columns": col_count,
        "num_columns": col_count,
        "num_rows": counted_rows,
        "write_time_ms": write_ms,
        "read_time_ms": read_ms,
        "write_rows_per_sec": (counted_rows / (write_ms / 1000.0)) if write_ms > 0 else 0.0,
        "read_rows_per_sec": (counted_rows / (read_ms / 1000.0)) if read_ms > 0 else 0.0,
        "file_size": file_path.stat().st_size,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run dedicated pybcsv macro-style benchmarks")
    parser.add_argument("--size", default="S", choices=list(SIZE_TO_ROWS.keys()))
    parser.add_argument("--modes", default="plain,numpy,pandas")
    parser.add_argument("--workloads", default="weather_timeseries,iot_fleet,financial_orders")
    parser.add_argument("--output", default="")
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    root = project_root()
    specs = workload_specs()

    selected_modes = parse_csv_arg(args.modes)
    selected_workloads = parse_csv_arg(args.workloads)

    unknown_modes = [mode for mode in selected_modes if mode not in MODE_LABELS]
    if unknown_modes:
        raise ValueError(f"Unknown mode(s): {', '.join(unknown_modes)}")

    unknown_workloads = [workload for workload in selected_workloads if workload not in specs]
    if unknown_workloads:
        raise ValueError(f"Unknown workload(s): {', '.join(unknown_workloads)}")

    available_modes = []
    for mode in selected_modes:
        if mode == "numpy" and np is None:
            print("[skip] numpy mode requested but numpy is not available")
            continue
        if mode == "pandas" and pd is None:
            print("[skip] pandas mode requested but pandas is not available")
            continue
        available_modes.append(mode)

    if not available_modes:
        raise RuntimeError("No benchmark modes can run in this environment")

    num_rows = SIZE_TO_ROWS[args.size]
    run_stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = Path(args.output) if args.output else root / "benchmark" / "results" / socket.gethostname() / "python" / f"py_macro_results_{run_stamp}.json"
    if not output_path.is_absolute():
        output_path = root / output_path
    output_path.parent.mkdir(parents=True, exist_ok=True)

    work_dir = root / "tmp" / "pybench"
    work_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for workload in selected_workloads:
        for mode in available_modes:
            seed = args.seed + stable_seed_offset(workload, 10_000) + stable_seed_offset(mode, 1_000)
            print(f"[run] workload={workload} mode={mode} rows={num_rows}")
            result = run_one(mode, workload, specs[workload]["columns"], num_rows, work_dir, seed)
            results.append(result)

    payload = {
        "run_type": "PYTHON-MACRO",
        "size": args.size,
        "num_rows": num_rows,
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "results": results,
    }
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
