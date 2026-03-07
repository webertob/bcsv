# bcsvValidate — Validate BCSV Structure & Content

Validate BCSV files in three modes: structure inspection, pattern validation against
benchmark profiles, and file-to-file comparison.

## Usage

```bash
bcsvValidate [OPTIONS] -i INPUT_FILE
```

## Modes

### 1. Structure Validation

Inspect header, walk all rows, report schema and statistics:

```bash
bcsvValidate -i data.bcsv
bcsvValidate -i data.bcsv --deep    # decompress and parse every row
```

### 2. Pattern Validation

Regenerate expected data from a benchmark profile and compare every cell:

```bash
bcsvValidate -i data.bcsv -p sensor_noisy -n 10000
bcsvValidate -i data.bcsv -p mixed_generic -d random
```

### 3. File Comparison

Compare two files row-by-row, cell-by-cell:

```bash
bcsvValidate -i a.bcsv --compare b.bcsv
bcsvValidate -i a.bcsv --compare b.csv --tolerance 1e-6
```

## Options

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
| `--json` | Machine-readable JSON output to stdout | |
| `--list` | List available profiles and exit | |
| `-v, --verbose` | Verbose progress output | |
| `-h, --help` | Show help message | |

## Examples

```bash
# Basic structure validation
bcsvValidate -i data.bcsv

# Validate against benchmark profile
bcsvValidate -i sensor.bcsv -p sensor_noisy -n 100000

# Compare two BCSV files with float tolerance
bcsvValidate -i original.bcsv --compare converted.bcsv --tolerance 1e-6

# JSON output for CI pipelines
bcsvValidate -i data.bcsv -p mixed_generic --json

# List available profiles
bcsvValidate --list
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Validation passed |
| 1 | Validation failed (mismatches found) |
| 2 | Error (file not found, bad arguments, etc.) |
