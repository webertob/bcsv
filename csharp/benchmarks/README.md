# C# Benchmarks (Standalone .NET)

Standalone .NET benchmark harness for Item 11.B.

## Prerequisites

- .NET SDK 8+
- `bcsv_c_api` shared library built from this repo

Build native library:

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release-build -j$(nproc) --target bcsv_c_api
```

## Run

From repository root:

```bash
export LD_LIBRARY_PATH="$PWD/build/ninja-release/lib:$LD_LIBRARY_PATH"
dotnet run --project csharp/benchmarks/Bcsv.Benchmarks.csproj -- --size=S
```

By default the benchmark uses `--flags=none` (flat codec, no ZoH), matching the Python benchmark lane.
To benchmark ZoH explicitly, pass `--flags=zoh`.

Select workloads and output file:

```bash
dotnet run --project csharp/benchmarks/Bcsv.Benchmarks.csproj -- \
  --size=M \
  --flags=zoh \
  --workloads=weather_timeseries,iot_fleet \
  --output=benchmark/results/$(hostname)/csharp/cs_macro_results.json
```

## Output Schema

JSON fields are aligned to macro benchmark row fields used by `benchmark/report.py`:

- `dataset`, `mode`, `scenario_id`, `access_path`
- `selected_columns`, `num_columns`, `num_rows`
- `write_time_ms`, `read_time_ms`
- `write_rows_per_sec`, `read_rows_per_sec`
- `file_size`
