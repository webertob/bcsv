# BCSV CLI Tools — AI Skills Reference

> Quick-reference for AI agents working with CLI tools.
> For full CLI usage docs, see: [CLI_TOOLS.md](CLI_TOOLS.md)
> For library usage examples, see: [examples/SKILLS.md](../../examples/SKILLS.md)

## Build

```bash
# Build all tools (BUILD_TOOLS=ON by default)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Build a single tool
cmake --build build --target bcsvSampler -j$(nproc)
```

All executables output to `build/bin/`.

## Build Targets (7 total)

| Target | Source | Description |
|--------|--------|-------------|
| `csv2bcsv` | csv2bcsv.cpp | Convert CSV → BCSV (auto-detect, type optimization) |
| `bcsv2csv` | bcsv2csv.cpp | Convert BCSV → CSV (RFC 4180, row slicing) |
| `bcsvHeader` | bcsvHeader.cpp | Display schema (column names, types) |
| `bcsvHead` | bcsvHead.cpp | Display first N rows in CSV format |
| `bcsvTail` | bcsvTail.cpp | Display last N rows in CSV format |
| `bcsvSampler` | bcsvSampler.cpp | Filter & project using Sampler expressions |
| `bcsvGenerator` | bcsvGenerator.cpp | Generate synthetic datasets from 14 profiles |

## Quick Reference

```bash
# Convert CSV → BCSV (ZoH enabled by default)
csv2bcsv input.csv output.bcsv

# Convert BCSV → CSV
bcsv2csv input.bcsv output.csv

# Inspect schema
bcsvHeader input.bcsv

# Display first / last 10 rows
bcsvHead -n 10 input.bcsv
bcsvTail -n 10 input.bcsv

# Filter rows where column 0 > 100
bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv

# Generate 100K rows of sensor data
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv
```

## Source Structure

All CLI tool source code is in `src/tools/`:
- `CMakeLists.txt` — build definitions (controlled by `BUILD_TOOLS` CMake option)
- `CLI_TOOLS.md` — comprehensive usage docs with pipeline examples
- `cli_common.h` — shared CLI utilities (codec dispatch, validation, formatting)
- Each tool has a matching `.md` documentation file (e.g., `bcsvSampler.md`)
