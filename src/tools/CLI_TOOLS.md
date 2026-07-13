# BCSV CLI Tools

Command-line utilities for CSV ↔ BCSV conversion, data inspection, filtering, validation, comparison, and synthetic dataset generation.
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
| **bcsvCompare** | Deterministic file comparison | BCSV × 2 | Exit code + report |
| **bcsvCast** | Cast column types (scan / narrow / static / dynamic) | BCSV | BCSV |
| **bcsvValidate** | Validate structure & content | BCSV (+CSV) | Report |
| **bcsvRepair** | Repair damaged/interrupted files | BCSV | BCSV |

## Common Options

Every tool accepts these two flags:

| Flag | Description |
|------|-------------|
| `-h, --help` | Show the tool's help message and exit |
| `-V, --version` | Show version information (`<tool> (BCSV <version>)`) and exit |

Run `<tool> -V` (or `--version`) to print the tool name, BCSV version, and
license. Argument parsing, help text, and validation are provided by the
bundled [CLI11](https://github.com/CLIUtils/CLI11) parser (BSD-3-Clause;
see [LICENSE](../../LICENSE)).

## Quick Start

```bash
csv2bcsv data.csv data.bcsv            # Convert CSV → BCSV
bcsvHeader data.bcsv                    # Inspect schema
bcsvHead data.bcsv -n 5                 # Preview first rows
bcsvTail data.bcsv -n 5                 # Preview last rows
bcsv2csv data.bcsv output.csv           # Convert back to CSV
bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv  # Filter rows
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv   # Generate test data
bcsvValidate -i data.bcsv                              # Validate structure
bcsvValidate -i data.bcsv --compare source.csv         # Compare files
bcsvCompare a.bcsv b.bcsv                              # Deterministic comparison (strict)
bcsvCompare --mode values a.bcsv b.bcsv                 # Compare values only (coercion)
bcsvCompare --mode types,values --tolerance 1e-6 a.bcsv b.bcsv # Compatible mode
bcsvCast data.bcsv                                # Scan for narrowing
bcsvCast data.bcsv narrow.bcsv                     # Apply narrowing
bcsvCast --in-place data.bcsv                      # In-place overwrite
bcsvRepair -i broken.bcsv --dry-run                    # Analyze damage
bcsvRepair -i broken.bcsv -o repaired.bcsv             # Repair to new file
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
| `bcsvRepair.cpp` | Repair damaged/interrupted BCSV files |
| `bcsvCompare.cpp` | Deterministic file comparison (combining modes: names, types, values) |
| `bcsvCast.cpp` | Column type cast: scan / narrow / static / dynamic |
| `cli_app.h` | Shared CLI11 layer (version flag, parse handling, codec options) |
| `cli_common.h` | Shared CLI utilities (codec dispatch, formatting, type/range parsing) |
| `CMakeLists.txt` | Build definitions for all 11 tools |

---

## csv2bcsv — CSV to BCSV Converter

Convert CSV files to BCSV format with automatic delimiter detection, aggressive type optimization, and compression.

### Usage

```bash
csv2bcsv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
csv2bcsv data.csv                                    # Auto-detect, default output
csv2bcsv data.csv output.bcsv                         # Custom output
csv2bcsv -d ';' --decimal-separator ',' german.csv    # European format
csv2bcsv -w padded_columns.txt                        # Space/tab-padded columns
csv2bcsv --row-codec delta data.csv output.bcsv       # Explicit delta codec
csv2bcsv --benchmark data.csv output.bcsv             # Measure throughput
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Field delimiter (auto-detected if omitted) | auto |
| `-w, --whitespace` | Treat runs of spaces/tabs as a single delimiter | |
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

### Whitespace-Collapse Mode (`-w`)

Use `-w` for files whose columns are separated by variable-width runs of spaces
and/or tabs (e.g. fixed-width, space-padded exports). Any run of spaces/tabs is
treated as one separator, and leading/trailing whitespace on each line is ignored.
A single trailing delimiter (e.g. a line-ending tab) is tolerated automatically
even without `-w`.

> **Caveat:** `-w` also splits the header row on whitespace, so it is not suitable
> for files whose column names contain spaces (e.g. `OCP Time`). For tab-delimited
> files that merely pad values with spaces, prefer the default auto-detection
> (which selects the tab) — the padding is trimmed during type conversion.

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

---

## bcsvRepair — Repair Damaged BCSV Files

Recover data from BCSV files where the writer was interrupted before completing the write (crash, power loss, killed process). Supports all five file codecs and all three row codecs.

**Packet-mode** files (packet, packet_lz4, packet_lz4_batch): walks packet-by-packet, rebuilds the packet index, attempts partial recovery of the last incomplete packet, writes a valid footer.

**Stream-mode** files (stream, stream_lz4): walks row-by-row using per-row XXH32 checksums, truncates at the last valid row.

### Usage

```bash
bcsvRepair -i broken.bcsv --dry-run                      # Analyze — report damage without modifying
bcsvRepair -i broken.bcsv --dry-run --json               # Machine-readable damage report
bcsvRepair -i broken.bcsv -o repaired.bcsv               # Copy repaired data to new file
bcsvRepair -i broken.bcsv -o repaired.bcsv --deep -v     # Deep checksum validation + progress
bcsvRepair -i broken.bcsv --in-place --backup            # Truncate + rewrite footer in place
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-i, --input FILE` | Input BCSV file (required) | |
| `-o, --output FILE` | Write repaired file to new location (copy mode) | |
| `--in-place` | Modify input file directly (truncate + append footer) | |
| `--backup` | With `--in-place`: copy original to `FILE.bak` first | |
| `--deep` | Validate packet payload checksums (slower, more thorough) | |
| `--dry-run` | Analyze only — no files modified (auto-enables `--deep`) | |
| `--json` | Machine-readable JSON output to stdout | |
| `-v, --verbose` | Print progress (packets scanned, rows counted) | |
| `-h, --help` | Show help message | |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Repair successful (or file already valid) |
| 1 | Repair failed or file not repairable |
| 2 | Argument error |

### Notes

- The `packet_lz4_batch` codec wraps entire packets in a single LZ4 frame; if the frame is truncated, zero rows can be recovered from that packet. Non-batch codecs recover individual rows.
- Use `--dry-run --json` in CI pipelines to detect interrupted writes.
- `recovery_pct` in JSON output shows the percentage of rows successfully recovered.

---

## bcsvCompare — Deterministic File Comparison

Compare two BCSV files and return exit code 0 (identical) or 1 (different). Supports combining check modes (names, types, values) for flexible comparison.

### Mode Selection

Modes are comma-separated (no spaces):

| Aspect | What it checks |
|--------|---------------|
| `names` | Column names must match (header-only, fast) |
| `types` | Column types must match (header-only, fast) |
| `values` | Cell values must match (streams row data) |

**Default** (no `--mode` flag): `all` — equivalent to `names,types,values` (legacy `strict`).

Any combination is valid: `names`, `types`, `values`, `names,types`, `names,values`,
`types,values`, `names,types,values` (or `all`).

Legacy aliases cannot be combined:

| Alias | Expands to |
|-------|-----------|
| `strict` | `all` (names,types,values) |
| `compatible` | `types,values` |
| `value` | `values` |

### Value Coercion Semantics

Whether values are compared strictly or with cross-type coercion depends on whether `types` is also selected:

| `types` selected? | Value comparison strategy |
|------------------|--------------------------|
| **Yes** (`types,values` or `all`) | Types already match — use **strict** per-cell comparison |
| **No** (`values` alone or `names,values`) | Types may differ — use **coerced** cross-type comparison |

### Value Coercion Matrix (when types may differ)

| Comparison | Mechanism | Precision |
|-----------|-----------|-----------|
| int vs int | `std::cmp_equal` (C++20) | Lossless, handles all signedness |
| INT8–UINT16 vs FLOAT | Both compared in `float` | Always exact |
| INT32/UINT32 vs FLOAT/DOUBLE | Both promoted to `double` | Always exact |
| INT64/UINT64 vs FLOAT/DOUBLE | Both promoted to `double` | Exact only within ±2^53 |
| FLOAT vs DOUBLE | Widen float to `double` | Always exact |
| STRING vs STRING | Exact string match | — |
| STRING vs numeric | Mismatch (use `--string-to-value` to parse string as number) | — |

For INT64/UINT64 vs float/double: if the integer exceeds `±2^53`, default behavior is to report as mismatch. Use `--allow-imprecise` to proceed with double comparison.

String-to-value (`--string-to-value`): trims leading/trailing whitespace, then attempts to parse as number. If parse fails (e.g. `"12x"`), the cells are a mismatch.

### Usage

```bash
bcsvCompare a.bcsv b.bcsv                              # default: all (strict)
bcsvCompare --mode names a.bcsv b.bcsv                 # names only (header-only)
bcsvCompare --mode names,types a.bcsv b.bcsv           # names + types only
bcsvCompare --mode values a.bcsv b.bcsv                # values only (cross-type coercion)
bcsvCompare --mode values --string-to-value a.bcsv b.bcsv # allow string↔number
bcsvCompare --mode types,values a.bcsv b.bcsv          # types + values (compatible)
bcsvCompare --rows 0:99 --cols 2:5 a.bcsv b.bcsv      # compare subset
bcsvCompare -v a.bcsv b.bcsv                           # verbose mismatch report
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--mode MODE` | Comma-separated: `names`, `types`, `values`, `all`, `strict`, `compatible`, `value` | `all` |
| `--string-to-value` | In values mode: parse STRING cells as numbers for comparison | off |
| `--allow-imprecise` | Allow INT64/UINT64 vs float/double beyond ±2^53 | off |
| `--tolerance TOL` | Float/double comparison epsilon | `0.0` |
| `--rows RANGES` | Row indices to compare (inclusive, e.g. `0:99,-10:`) | all rows |
| `--cols RANGES` | Column indices to compare (inclusive, e.g. `2:5`) | all columns |
| `-v, --verbose` | Report mismatch details to stdout | off |
| `-h, --help` | Show help message | — |

### Range Syntax

Ranges use inclusive indexing and support comma-separated, disjoint ranges:

| Example | Meaning |
|---------|---------|
| `5` | Single index 5 |
| `0:99` | Indices 0 through 99 |
| `:100` | First 101 indices (0:100) |
| `-10:` | Last 10 indices |
| `0:9,50:59` | Two disjoint ranges |

### Exit Codes

0 (identical), 1 (different), 2 (argument / file error).

### Examples

```bash
# CI guard: fail the pipeline if outputs differ
bcsvCompare --mode compatible release.bcsv staging.bcsv

# Compare only column names (fast, header-only — ignores row data)
bcsvCompare --mode names a.bcsv b.bcsv

# Compare only the first 100 rows, columns 0-4
bcsvCompare --rows :99 --cols 0:4 a.bcsv b.bcsv

# Cross-type value comparison with tolerance
bcsvCompare --mode value --tolerance 0.001 a.bcsv b.bcsv

# Verbose report of which columns are different
bcsvCompare -v a.bcsv b.bcsv
```

### Differences from bcsvValidate --compare

`bcsvValidate --compare` is a sub-mode of the validation tool, focused on structural validation and pattern matching.  `bcsvCompare` is a dedicated, faster diff tool with:

- Combining mode selection (names, types, values) for flexible checking
- Header-only fast path when only names and/or types are checked
- Cross-type value coercion with precise lossless comparison (`std::cmp_equal`, float vs double widening)
- String-to-value parsing for STRING ↔ numeric comparisons
- Precision guard for INT64/UINT64 vs float/double (`--allow-imprecise`)
- Row/column range scoping for targeted comparisons
- Cleaner exit-code semantics (0 = same, 1 = different)
- No CSV input support (both files must be BCSV)
- NaN values compare equal (NaN == NaN)
- Out-of-range `--rows`/`--cols` exits with code 2

---

## bcsvCast — Column Type Cast (Scan / Narrow / Static / Dynamic)

Change BCSV column types. **Auto** modes derive the smallest lossless type by scanning the data; **explicit** modes apply a caller-supplied type SPEC. Replaces the former `bcsvNarrowType` (whose behavior is now `bcsvCast --optimize`).

**Note:** With the default codec (delta + packet_lz4_batch), narrowing may yield little or no on-disk savings because integers use zigzag + VLE and floats use XOR + zero-stripping before LZ4. The primary beneficiaries are **flat**-codec files, or fixed-width consumers (C API, Unity P/Invoke, mmap). Reported savings are **max theoretical (flat codec)**.

### Usage

```bash
bcsvCast data.bcsv                          # Scan: report smallest lossless types (read-only)
bcsvCast data.bcsv out.bcsv                 # Narrow + apply (default when an output is given)
bcsvCast --optimize --in-place data.bcsv      # Auto-narrow in place
bcsvCast --static '0=int32,3=float' data.bcsv out.bcsv   # Force these casts (clamp lossy)
bcsvCast --dynamic '0=int32,3=float' data.bcsv out.bcsv  # Cast if lossless, else skip the column
bcsvCast --scan --json data.bcsv            # Machine-readable plan (agent-friendly)
```

### Modes

| Mode | Target types | Writes? | On loss |
|------|--------------|---------|---------|
| `--scan` | auto-derived | never (read-only) | reports smallest lossless types |
| `--optimize` | auto-derived | if output given | lossless by construction |
| `--dynamic SPEC` | from SPEC | if output given | skips any column that would lose data |
| `--static SPEC` | from SPEC | if output given | applies anyway; clamps lossy cells |

Default mode (no flag): `--optimize` when an output path is present, else `--scan`.

### Type SPEC

Quote it (shells expand unquoted `{ }`). Two mutually exclusive forms:

- **Map** — `'0=int32,1=uint64,7:8=float,-1=bool'` (index or `i:j` range `=` type; negative-from-end indices allowed; a subset of columns).
- **List** — `'int32,uint64,bool,...'` (one type per column, covering **every** column).

Type names: `bool int8 int16 int32 int64 uint8 uint16 uint32 uint64 float double string`, plus aliases `i8..i64`, `ui8..ui64`/`u8..u64`, `b`, `ch`/`char`/`byte` (→uint8), `f`/`f32`, `d`/`f64`, `str`/`s`, `int`=int32, `long`=int64, `short`, `ushort`, `uint`=uint32, `ulong`.

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output FILE` | Output BCSV file (enables apply) | — |
| `--in-place` | Rewrite `INPUT_FILE` in place (temp + atomic rename) | — |
| `--overwrite` | Allow overwriting an existing `OUTPUT_FILE` | — |
| `--cols SPEC` | Restrict `--scan`/`--optimize` to columns (`0:3,5,7:-1`) | all |
| `--tolerance TOL` | Absolute epsilon for float/int loss tests | 0.0 |
| `--string-to-value` | `--scan`/`--optimize` also consider STRING→numeric | off |
| `--json` | Emit the plan/result as JSON on stdout | — |
| `-v, --verbose` | Per-column details and progress to stderr | — |
| `-h, --help` | Show help message | — |

### Force semantics (`--static`)

Non-representable cells are made deterministic rather than rejected: out-of-range → **saturate/clamp** to the target min/max; float→int → round to nearest (`std::round`); `NaN`→0; `±Inf`→min/max; a non-numeric **string** → hard error (exit 1). Each affected column is reported on stderr.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success (lossy `--static`/`--dynamic` still exit 0; loss is reported on stderr) |
| 1 | Runtime error (invalid file, unparseable forced string, write failure) |
| 2 | Argument error (bad flags/SPEC, conflicting modes) |

### Examples

```bash
# Discover what would narrow (read-only)
bcsvCast sensor.bcsv
bcsvCast --scan --json sensor.bcsv        # → suggested_spec for reuse

# Auto-narrow and verify values round-trip
bcsvCast sensor.bcsv narrow.bcsv
bcsvCompare --mode value sensor.bcsv narrow.bcsv

# Force an id column to int32 and a noisy sensor to float32
bcsvCast --static '0=int32,3=float' sensor.bcsv out.bcsv --overwrite

# Same, but skip any column that would lose data
bcsvCast --dynamic '0=int32,3=float' sensor.bcsv out.bcsv
```
