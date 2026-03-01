# BCSV CLI Tools

Command-line utilities for CSV ↔ BCSV conversion, data inspection, filtering, and synthetic dataset generation.
All tools are built from `src/tools/` and output to `build/bin/`.

## Overview

| Tool | Purpose | Input | Output |
|------|---------|-------|--------|
| **[csv2bcsv](csv2bcsv.md)** | Convert CSV to BCSV | CSV | BCSV |
| **[bcsv2csv](bcsv2csv.md)** | Convert BCSV to CSV | BCSV | CSV |
| **[bcsvHeader](bcsvHeader.md)** | Display file schema | BCSV | Text |
| **[bcsvHead](bcsvHead.md)** | Display first N rows | BCSV | CSV |
| **[bcsvTail](bcsvTail.md)** | Display last N rows | BCSV | CSV |
| **[bcsvSampler](bcsvSampler.md)** | Filter & project rows | BCSV | BCSV |
| **[bcsvGenerator](bcsvGenerator.md)** | Generate synthetic datasets | — | BCSV |

## Quick Start

```bash
# Convert CSV → BCSV
csv2bcsv data.csv data.bcsv

# Inspect schema
bcsvHeader data.bcsv

# Preview first / last rows
bcsvHead data.bcsv -n 5
bcsvTail data.bcsv -n 5

# Convert back to CSV
bcsv2csv data.bcsv output.csv

# Filter rows with expression
bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv

# Generate a test dataset
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv
```

## Pipeline Examples

```bash
# Generate → inspect → filter → export
bcsvGenerator -p weather_timeseries -n 50000 -o weather.bcsv
bcsvHeader weather.bcsv
bcsvSampler -c 'X[0][3] > 25.0' -s 'X[0][0], X[0][3]' weather.bcsv hot_days.bcsv
bcsv2csv hot_days.bcsv hot_days.csv

# Slice and export
bcsv2csv --slice 100:200 data.bcsv rows_100_to_199.csv

# Data validation pipeline
csv2bcsv input.csv temp.bcsv && \
bcsvHead temp.bcsv | grep -q "expected_column" && \
bcsvTail temp.bcsv -n 100 --no-header | your_validator && \
bcsv2csv temp.bcsv validated_output.csv
```

## Batch Processing

```bash
# Convert all CSV files in directory
for file in *.csv; do
  csv2bcsv "$file" "${file%.csv}.bcsv"
done

# Validate all BCSV files
for file in *.bcsv; do
  echo "=== $file ==="
  bcsvHead "$file" -n 3
  echo "..."
  bcsvTail "$file" -n 3
done
```

## Build

```bash
# Build all tools (default: ON)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Build a single tool
cmake --build build --target bcsvSampler -j$(nproc)

# Disable tools build
cmake -S . -B build -DBUILD_TOOLS=OFF
```

Tools are controlled by the `BUILD_TOOLS` CMake option (default: ON).
Always use Release builds for production workloads.

## Source Structure

All CLI tool source code lives in `src/tools/`:

| File | Description |
|------|-------------|
| `csv2bcsv.cpp` | CSV → BCSV converter |
| `bcsv2csv.cpp` | BCSV → CSV converter |
| `bcsvHead.cpp` | First-N-rows display |
| `bcsvTail.cpp` | Last-N-rows display |
| `bcsvHeader.cpp` | Schema display |
| `bcsvSampler.cpp` | Expression-based filter & project |
| `bcsvGenerator.cpp` | Synthetic dataset generator |
| `csv_format_utils.h` | Shared CSV formatting helpers |
| `CMakeLists.txt` | Build definitions for all 7 tools |
| `*.md` | Per-tool documentation |

## Related Documentation

- [BCSV README](../../README.md) — project overview
- [API Overview](../../docs/API_OVERVIEW.md) — C++ library API
- [Examples](../../examples/README.md) — library usage examples