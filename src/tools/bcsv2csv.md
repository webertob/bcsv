# bcsv2csv — BCSV to CSV Converter

Convert BCSV files to CSV format with RFC 4180 compliance. Supports row slicing,
custom delimiters, and all BCSV codecs (flat, ZoH, delta, LZ4, batch).

## Usage

```bash
bcsv2csv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
```

```bash
# Convert BCSV to CSV
bcsv2csv data.bcsv

# Custom output filename
bcsv2csv data.bcsv output.csv

# European format (semicolon delimiter)
bcsv2csv -d ';' data.bcsv euro.csv
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Output field delimiter | `,` |
| `--no-header` | Omit header row in output | |
| `--firstRow N` | Start from row N (0-based) | `0` |
| `--lastRow N` | End at row N (0-based, inclusive) | last |
| `--slice SLICE` | Python-style slice (overrides firstRow/lastRow) | |
| `-v, --verbose` | Verbose output | |
| `--benchmark` | Print timing stats to stderr | |
| `--json` | With `--benchmark`: emit JSON timing blob to stdout | |
| `-h, --help` | Show help message | |

## Row Selection

```bash
# Rows 100–200 (inclusive)
bcsv2csv --firstRow 100 --lastRow 200 data.bcsv

# Python-style slicing
bcsv2csv --slice 10:20 data.bcsv        # Rows 10–19
bcsv2csv --slice :100 data.bcsv          # First 100 rows
bcsv2csv --slice 50: data.bcsv           # From row 50 to end
bcsv2csv --slice ::2 data.bcsv           # Every 2nd row
bcsv2csv --slice -10: data.bcsv          # Last 10 rows
```

## CSV Compliance

Output is **RFC 4180 compliant**:

- Fields containing delimiters, quotes, or newlines are automatically quoted
- Internal quotes are doubled (`"` → `""`)
- Leading/trailing whitespace is preserved

```bash
# Round-trip conversion
csv2bcsv original.csv temp.bcsv
bcsv2csv temp.bcsv converted.csv
```

## Examples

```bash
# Tab-separated output without header
bcsv2csv -d $'\t' --no-header data.bcsv tsv_output.txt

# Excel-compatible semicolon format
bcsv2csv -d ';' data.bcsv excel_import.csv

# Extract rows 500–999 to CSV
bcsv2csv --slice 500:1000 data.bcsv subset.csv

# Measure throughput
bcsv2csv --benchmark data.bcsv output.csv
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (file not found, invalid BCSV, I/O error) |