# pybcsv Benchmarks

Dedicated Python benchmarks for Item 11.B.

## Scope

Benchmarks run `pybcsv` via three Python-facing modes:
- `PYBCSV Plain`
- `PYBCSV NumPy`
- `PYBCSV Pandas`

Workloads mirror BCSV benchmark naming for comparability:
- `weather_timeseries`
- `iot_fleet`
- `financial_orders`

## Run

From repository root:

```bash
python3 python/benchmarks/run_pybcsv_benchmarks.py --size=S
```

Explicit modes/workloads:

```bash
python3 python/benchmarks/run_pybcsv_benchmarks.py \
  --size=M \
  --modes=plain,numpy,pandas \
  --workloads=weather_timeseries,iot_fleet
```

Output path override:

```bash
python3 python/benchmarks/run_pybcsv_benchmarks.py \
  --output benchmark/results/$(hostname)/python/py_macro_results.json
```

## Output Schema

The output JSON is aligned to macro benchmark row fields used by `benchmark/report.py`:
- `dataset`, `mode`, `scenario_id`, `access_path`
- `selected_columns`, `num_columns`, `num_rows`
- `write_time_ms`, `read_time_ms`
- `write_rows_per_sec`, `read_rows_per_sec`
- `file_size`
