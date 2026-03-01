# csv2bcsv — CSV to BCSV Converter

Convert CSV files to BCSV format with automatic delimiter detection, aggressive
type optimization, and ZoH compression. Supports European CSV formats.

## Usage

```bash
csv2bcsv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
```

```bash
# Auto-detect delimiter, convert to BCSV
csv2bcsv data.csv

# Custom output filename
csv2bcsv data.csv output.bcsv

# European format (semicolon, comma decimal)
csv2bcsv -d ';' --decimal-separator ',' german_data.csv
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Field delimiter (auto-detected if omitted) | auto |
| `--decimal-separator CHAR` | Decimal separator: `.` or `,` | `.` |
| `--no-header` | CSV file has no header row | |
| `--no-zoh` | Disable Zero-Order Hold compression | ZoH enabled |
| `-v, --verbose` | Verbose output | |
| `--benchmark` | Print timing stats to stderr | |
| `--json` | With `--benchmark`: emit JSON timing blob to stdout | |
| `-h, --help` | Show help message | |

## Delimiter Auto-Detection

When `-d` is omitted, the tool scans the first line and selects the most likely delimiter:

**Detection priority**: comma → semicolon → tab → pipe

## Type Optimization

All columns are scanned in a first pass to select the most compact type:

| Input Data | Detected Type | Savings vs INT64 |
|------------|---------------|-------------------|
| `0, 1, 255` | `UINT8` | 87.5% |
| `-100, 0, 100` | `INT8` | 87.5% |
| `1000, 50000` | `UINT16` | 75% |
| `3.14, 2.71` | `FLOAT` | 50% |
| `3.141592653589793` | `DOUBLE` | — |

## Examples

```bash
# Basic conversion (ZoH enabled by default)
csv2bcsv sales_data.csv

# Flat encoding (no ZoH)
csv2bcsv --no-zoh data.csv flat.bcsv

# European CSV
csv2bcsv -d ';' --decimal-separator ',' -v european_sales.csv

# Header-less CSV with tab delimiter
csv2bcsv -d $'\t' --no-header raw_data.csv processed.bcsv

# Measure throughput
csv2bcsv --benchmark data.csv output.bcsv
```

## Batch Processing

```bash
# Convert all CSV files in directory
for file in *.csv; do
    csv2bcsv "$file" "${file%.csv}.bcsv"
done
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (file not found, character conflict, I/O error) |