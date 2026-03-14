# BCSV CLI Tools

Command-line utilities for CSV ↔ BCSV conversion, data inspection, filtering, validation, and synthetic dataset generation.
All tools are built from `src/tools/` and output to `build/bin/`.

## Overview

| Tool | Purpose | Input | Output |
|------|---------|-------|--------|
| **csv2bcsv** | Convert CSV to BCSV | CSV | BCSV |
| **bcsv2csv** | Convert BCSV to CSV | BCSV | CSV |
| **bcsvHeader** | Display file schema | BCSV | Text |
| **bcsvHead** | Display first N rows | BCSV | CSV |
| **bcsvTail** | Display last N rows | BCSV | CSV |
| **bcsvSampler** | Filter & project rows | BCSV | BCSV |
| **bcsvGenerator** | Generate synthetic datasets | — | BCSV |
| **bcsvValidate** | Validate structure & content | BCSV (+CSV) | Report |

## Quick Start

```bash
csv2bcsv data.csv data.bcsv            # Convert CSV → BCSV
bcsvHeader data.bcsv                    # Inspect schema
bcsvHead data.bcsv -n 5                 # Preview first rows
bcsvTail data.bcsv -n 5                 # Preview last rows
bcsv2csv data.bcsv output.csv           # Convert back to CSV
bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv  # Filter rows
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv   # Generate test data
bcsvValidate -i data.bcsv               # Validate structure
bcsvValidate -i data.bcsv --compare source.csv           # Compare files
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

# Batch convert all CSV files
for file in *.csv; do csv2bcsv "$file" "${file%.csv}.bcsv"; done
```

## Build

```bash
# Build all tools (BUILD_TOOLS=ON by default)
cmake --preset ninja-release && cmake --build --preset ninja-release-build -j$(nproc)

# Build a single tool
cmake --build --preset ninja-release-build --target bcsvSampler -j$(nproc)
```

## Source Structure

| File | Description |
|------|-------------|
| `csv2bcsv.cpp` | CSV → BCSV converter |
| `bcsv2csv.cpp` | BCSV → CSV converter |
| `bcsvHead.cpp` | First-N-rows display |
| `bcsvTail.cpp` | Last-N-rows display |
| `bcsvHeader.cpp` | Schema display |
| `bcsvSampler.cpp` | Expression-based filter & project |
| `bcsvGenerator.cpp` | Synthetic dataset generator |
| `bcsvValidate.cpp` | Structure & content validation |
| `cli_common.h` | Shared CLI utilities (codec dispatch, validation, formatting) |
| `CMakeLists.txt` | Build definitions for all 8 tools |

---

## csv2bcsv — CSV to BCSV Converter

Convert CSV files to BCSV format with automatic delimiter detection, aggressive type optimization, and compression.

### Usage

```bash
csv2bcsv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
csv2bcsv data.csv                                    # Auto-detect, default output
csv2bcsv data.csv output.bcsv                         # Custom output
csv2bcsv -d ';' --decimal-separator ',' german.csv    # European format
csv2bcsv --row-codec delta data.csv output.bcsv       # Explicit delta codec
csv2bcsv --benchmark data.csv output.bcsv             # Measure throughput
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Field delimiter (auto-detected if omitted) | auto |
| `--decimal-separator CHAR` | Decimal separator: `.` or `,` | `.` |
| `--no-header` | CSV file has no header row | |
| `-v, --verbose` | Verbose output | |
| `--benchmark` | Print timing stats to stderr | |
| `--json` | With `--benchmark`: emit JSON timing blob to stdout | |
| `-h, --help` | Show help message | |

### Type Optimization

All columns are scanned in a first pass to select the most compact type:

| Input Data | Detected Type | Savings vs INT64 |
|------------|---------------|-------------------|
| `0, 1, 255` | `UINT8` | 87.5% |
| `-100, 0, 100` | `INT8` | 87.5% |
| `1000, 50000` | `UINT16` | 75% |
| `3.14, 2.71` | `FLOAT` | 50% |
| `3.141592653589793` | `DOUBLE` | — |

### Delimiter Auto-Detection

When `-d` is omitted, scans the first line. Priority: comma → semicolon → tab → pipe.

---

## bcsv2csv — BCSV to CSV Converter

Convert BCSV files to CSV format with RFC 4180 compliance. Supports row slicing, custom delimiters, and all BCSV codecs.

### Usage

```bash
bcsv2csv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
bcsv2csv data.bcsv output.csv                  # Basic conversion
bcsv2csv -d ';' data.bcsv euro.csv             # European format
bcsv2csv --slice 10:20 data.bcsv               # Rows 10–19
bcsv2csv -d $'\t' --no-header data.bcsv        # Tab-separated, no header
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Output field delimiter | `,` |
| `--no-header` | Omit header row in output | |
| `--firstRow N` | Start from row N (0-based) | `0` |
| `--lastRow N` | End at row N (0-based, inclusive) | last |
| `--slice SLICE` | Python-style slice (overrides firstRow/lastRow) | |
| `-v, --verbose` | Verbose output | |
| `--benchmark` | Print timing stats to stderr | |
| `-h, --help` | Show help message | |

### Row Slicing

```bash
bcsv2csv --slice 10:20 data.bcsv     # Rows 10–19
bcsv2csv --slice :100 data.bcsv      # First 100 rows
bcsv2csv --slice 50: data.bcsv       # From row 50 to end
bcsv2csv --slice ::2 data.bcsv       # Every 2nd row
bcsv2csv --slice -10: data.bcsv      # Last 10 rows
```

---

## bcsvHeader — Display File Schema

Display column index, name, and type. Header-only read — works instantly on any file size.

### Usage

```bash
bcsvHeader data.bcsv          # Display schema
bcsvHeader -v data.bcsv       # Verbose (includes codec info)
```

### Options

| Option | Description |
|--------|-------------|
| `-v, --verbose` | Additional details (codec names, file version) |
| `-h, --help` | Show help message |

### Output

```text
BCSV Header Structure: dataset.bcsv
Columns: 4

Index  Name      Type
-----  --------  ------
0      id        int32
1      name      string
2      score     float
3      active    bool
```

---

## bcsvHead — Display First N Rows

Display the first rows of a BCSV file in CSV format. Memory efficient — only loads requested rows.

### Usage

```bash
bcsvHead data.bcsv               # First 10 rows (default)
bcsvHead -n 5 data.bcsv          # First 5 rows
bcsvHead --no-header data.bcsv   # Data only (no column header)
bcsvHead data.bcsv | grep "Alice"          # Pipe to grep
bcsvHead -n 100 data.bcsv | awk -F, '{sum+=$3} END {print sum}'  # Pipe to awk
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-n, --lines N` | Number of rows to display | `10` |
| `-d, --delimiter CHAR` | Field delimiter | `,` |
| `--no-header` | Omit header row | |
| `-v, --verbose` | Verbose output | |
| `-h, --help` | Show help message | |

---

## bcsvTail — Display Last N Rows

Display the last rows of a BCSV file in CSV format. Uses a circular buffer — constant memory regardless of file size.

### Usage

```bash
bcsvTail data.bcsv               # Last 10 rows (default)
bcsvTail -n 5 data.bcsv          # Last 5 rows
bcsvTail --no-header data.bcsv   # Data only
bcsvTail -n 500 --no-header data.bcsv | awk -F, '$4 > 1000 {print $2, $4}'
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-n, --lines N` | Number of rows to display | `10` |
| `-d, --delimiter CHAR` | Field delimiter | `,` |
| `--no-header` | Omit header row | |
| `-v, --verbose` | Verbose output | |
| `-h, --help` | Show help message | |

---

## bcsvSampler — Filter & Project BCSV Files

Apply conditional filters and column projections using the Sampler bytecode VM. Writes matching rows to a new BCSV file.

### Usage

```bash
bcsvSampler -c 'X[0][0] > 100' data.bcsv                           # Filter rows
bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv projected.bcsv         # Project columns
bcsvSampler -c 'X[0][1] > 0' -s 'X[0][0], X[0][1]' data.bcsv out.bcsv  # Both
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-c, --conditional EXPR` | Row filter (boolean expression) | none (pass all) |
| `-s, --selection EXPR` | Column projection (comma-separated) | none (all) |
| `-m, --mode MODE` | Boundary mode: `truncate` or `expand` | `truncate` |
| `--compression-level N` | LZ4 compression level | `1` |
| `--block-size N` | Block size in KB | `64` |
| `-f, --overwrite` | Overwrite output file | |
| `--disassemble` | Print compiled bytecode and exit | |
| `-v, --verbose` | Verbose progress output | |
| `-h, --help` | Show help message | |

### Expression Syntax

Cells are referenced as `X[row_offset][column_index]`:

- `X[0][0]` — current row, column 0
- `X[-1][0]` — previous row, column 0
- `X[1][0]` — next row, column 0

**Conditional examples:**

```bash
bcsvSampler -c 'X[0][0] > 100' data.bcsv                    # Threshold
bcsvSampler -c 'X[0][1] != X[-1][1]' data.bcsv              # Edge detection
bcsvSampler -c 'X[0][0] > 10 && X[0][1] < 50' data.bcsv     # Boolean logic
```

**Selection examples:**

```bash
bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv                 # Pick columns
bcsvSampler -s 'X[0][0], X[0][1] - X[-1][1]' data.bcsv      # Gradient
bcsvSampler -s 'X[0][0], (X[-1][1] + X[0][1] + X[1][1]) / 3.0' data.bcsv  # Moving avg
```

**Boundary mode:** `truncate` (default) skips rows with incomplete windows; `expand` clamps to edge row.

---

## bcsvGenerator — Generate Synthetic Datasets

Generate BCSV files from 14 built-in dataset profiles with configurable row count, data mode, and encoding.

### Usage

```bash
bcsvGenerator -o test.bcsv                                  # Default: 10K mixed_generic
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv      # 100K sensor data
bcsvGenerator --list                                        # List profiles
bcsvGenerator -p weather_timeseries -d random -o stress.bcsv  # Random (stress test)
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output FILE` | Output BCSV file (required) | |
| `-p, --profile NAME` | Dataset profile | `mixed_generic` |
| `-n, --rows N` | Number of rows | `10000` |
| `-d, --data-mode MODE` | `timeseries` or `random` | `timeseries` |
| `--file-codec CODEC` | File codec (see below) | `packet_lz4_batch` |
| `--row-codec CODEC` | Row codec: `delta`, `zoh`, `flat` | `delta` |
| `--compression-level N` | LZ4 compression level | `1` |
| `--block-size N` | Block size in KB | `64` |
| `-f, --overwrite` | Overwrite output file | |
| `-v, --verbose` | Verbose output | |
| `--list` | List all profiles and exit | |
| `-h, --help` | Show help message | |

### File Codecs

| Value | Description |
|-------|-------------|
| `packet_lz4_batch` | Async double-buffered LZ4 (default, best throughput) |
| `packet_lz4` | Packet LZ4 |
| `packet` | Packet, no compression |
| `stream_lz4` | Streaming LZ4 |
| `stream` | Streaming, no compression |

### Dataset Profiles (14)

| Profile | Cols | Description |
|---------|------|-------------|
| `mixed_generic` | 72 | All 12 types (6 each) — baseline |
| `sparse_events` | 100 | ~1% activity — ZoH best-case |
| `sensor_noisy` | 50 | Float/double Gaussian noise |
| `string_heavy` | 30 | Varied string cardinality |
| `bool_heavy` | 132 | 128 bools + 4 scalars |
| `arithmetic_wide` | 200 | 200 numeric columns, no strings |
| `simulation_smooth` | 100 | Slow linear drift — ideal for ZoH |
| `weather_timeseries` | 40 | Realistic weather pattern |
| `high_cardinality_string` | 50 | Near-unique UUIDs |
| `event_log` | 27 | Backend event stream |
| `iot_fleet` | 25 | Fleet telemetry |
| `financial_orders` | 22 | Order/trade feed |
| `realistic_measurement` | 38 | DAQ session with phases |
| `rtl_waveform` | 290 | RTL digital waveform capture |

---

## bcsvValidate — Validate BCSV Structure & Content

Validate BCSV files in three modes: structure inspection, pattern validation against benchmark profiles, and file-to-file comparison.

### Usage

```bash
bcsvValidate -i data.bcsv                                   # Structure check
bcsvValidate -i data.bcsv --deep                             # Parse every row
bcsvValidate -i data.bcsv -p sensor_noisy -n 10000           # Pattern validation
bcsvValidate -i a.bcsv --compare b.bcsv                      # File comparison
bcsvValidate -i a.bcsv --compare b.csv --tolerance 1e-6      # With float tolerance
bcsvValidate -i data.bcsv -p mixed_generic --json            # JSON output for CI
bcsvValidate --list                                          # List profiles
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-i, --input FILE` | Input BCSV file (required) | |
| `-p, --profile NAME` | Benchmark profile name | |
| `-n, --rows N` | Expected row count | profile default |
| `-d, --data-mode MODE` | `timeseries` or `random` | `timeseries` |
| `--compare FILE` | Second file for comparison (BCSV or CSV) | |
| `--tolerance TOL` | Float/double comparison tolerance | `0.0` |
| `--max-errors N` | Maximum mismatches to report | `10` |
| `--deep` | Parse every row during structure check | |
| `--json` | Machine-readable JSON output | |
| `--list` | List available profiles and exit | |
| `-v, --verbose` | Verbose output | |
| `-h, --help` | Show help message | |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Validation passed |
| 1 | Validation failed (mismatches found) |
| 2 | Error (file not found, bad arguments) |
