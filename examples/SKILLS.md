# BCSV Examples & CLI Tools — AI Skills Reference

> Quick-reference for AI agents working with CLI tools and example programs.
> For full CLI usage, see: examples/CLI_TOOLS.md

## Build

```bash
# All examples + CLI tools build with the default cmake configuration
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Build a single target
cmake --build build --target csv2bcsv -j$(nproc)
```

All executables output to `build/bin/`.

## Build Targets (11 total)

### CLI Tools

| Target | Source | Description |
|--------|--------|-------------|
| `csv2bcsv` | csv2bcsv.cpp | Convert CSV files to BCSV format |
| `bcsv2csv` | bcsv2csv.cpp | Convert BCSV files to CSV format |
| `bcsvHeader` | bcsvHeader.cpp | Display BCSV file header (column names, types, flags) |
| `bcsvHead` | bcsvHead.cpp | Display first N rows of a BCSV file |
| `bcsvTail` | bcsvTail.cpp | Display last N rows of a BCSV file |

### Example Programs

| Target | Source | Description |
|--------|--------|-------------|
| `example` | example.cpp | Flexible layout — dynamic schema, write + read |
| `example_static` | example_static.cpp | Static layout — compile-time typed schema |
| `example_zoh` | example_zoh.cpp | Zero-Order-Hold compression (flexible layout) |
| `example_zoh_static` | example_zoh_static.cpp | ZoH with static layout |
| `visitor_examples` | visitor_examples.cpp | Visit pattern examples (const + mutable visitors) |
| `c_api_vectorized_example` | c_api_vectorized_example.c | C API vectorized read example |

## CLI Tool Quick Reference

```bash
# Convert CSV to BCSV (default: ZoH enabled)
./build/bin/csv2bcsv input.csv output.bcsv

# Convert CSV to BCSV (no ZoH — flat encoding)
./build/bin/csv2bcsv --no-zoh input.csv output.bcsv

# Convert BCSV to CSV
./build/bin/bcsv2csv input.bcsv output.csv

# Show file header
./build/bin/bcsvHeader input.bcsv

# Show first/last 10 rows
./build/bin/bcsvHead -n 10 input.bcsv
./build/bin/bcsvTail -n 10 input.bcsv
```

## Known Caveat: ZoH Mismatch

`csv2bcsv` defaults to ZoH-enabled output, but the reader CLI tools (`bcsv2csv`, `bcsvHead`, `bcsvTail`, `bcsvHeader`) construct `Reader<Layout>` with `TrackingPolicy::Disabled`, which **cannot read ZoH files**. 

Workarounds:
- Use `csv2bcsv --no-zoh` to write flat-encoded files readable by all tools
- Or update CLI tools to auto-detect `FileFlags::ZERO_ORDER_HOLD` and switch readers (planned in ToDo item 13)

## Source Structure

All in `examples/`:
- Each tool has a matching `.md` documentation file (e.g., `csv2bcsv.md`, `bcsvHead.md`)
- `CMakeLists.txt` defines all 11 targets, outputs to `${CMAKE_BINARY_DIR}/bin`
- `CLI_TOOLS.md` has comprehensive usage docs with pipeline examples
- `README.md` has API usage patterns and troubleshooting
