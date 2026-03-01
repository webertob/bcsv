# bcsvSampler – Filter & Project BCSV Files

Apply conditional filters and column projections to a BCSV file using the Sampler bytecode VM. Writes matching rows to a new BCSV file with configurable encoding.

## Basic Usage

```bash
# Filter rows where column 0 > 100
bcsvSampler -c 'X[0][0] > 100' data.bcsv

# Project columns 0 and 2
bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv projected.bcsv

# Combine filter + projection
bcsvSampler -c 'X[0][1] > 0' -s 'X[0][0], X[0][1]' data.bcsv out.bcsv

# Help
bcsvSampler --help
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `-c, --conditional EXPR` | Row filter (boolean expression) | none (pass all) |
| `-s, --selection EXPR` | Column projection (comma-separated) | none (all columns) |
| `-m, --mode MODE` | Boundary mode: `truncate` or `expand` | `truncate` |
| `--compression-level N` | LZ4 compression level | `1` |
| `--block-size N` | Block size in KB | `64` |
| `--no-batch` | Disable batch compression | |
| `--no-delta` | Use flat codec instead of delta | |
| `--no-lz4` | Disable LZ4 compression | |
| `-f, --overwrite` | Overwrite output file if it exists | |
| `--disassemble` | Print compiled bytecode and exit | |
| `-v, --verbose` | Verbose progress output | |
| `-h, --help` | Show help message | |

## Expression Syntax

Expressions reference cells as `X[row_offset][column_index]`:

- `X[0][0]` – current row, column 0
- `X[-1][0]` – previous row, column 0
- `X[1][0]` – next row, column 0

### Conditional Examples

```bash
# Simple threshold
bcsvSampler -c 'X[0][0] > 100' data.bcsv

# Edge detection (value changed from previous row)
bcsvSampler -c 'X[0][1] != X[-1][1]' data.bcsv

# Boolean logic
bcsvSampler -c 'X[0][0] > 10 && X[0][1] < 50' data.bcsv

# Bitwise flag check
bcsvSampler -c '(X[0][3] & 0x04) != 0' data.bcsv
```

### Selection Examples

```bash
# Pick specific columns
bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv

# Compute gradient (current − previous)
bcsvSampler -s 'X[0][0], X[0][1] - X[-1][1]' data.bcsv

# Moving average
bcsvSampler -s 'X[0][0], (X[-1][1] + X[0][1] + X[1][1]) / 3.0' data.bcsv
```

### Boundary Mode

When expressions reference previous or next rows (`X[-1]`, `X[1]`), the boundary mode controls behavior at file edges:

- **truncate** (default): rows with incomplete windows are skipped
- **expand**: out-of-bounds references clamp to the edge row

```bash
# Use expand mode for gradient at file boundaries
bcsvSampler -c 'X[0][1] != X[-1][1]' -m expand data.bcsv
```

## Encoding

Default output encoding is **packet + lz4 + batch + delta**, which provides the best compression and read performance for most workloads.

```bash
# Disable delta encoding (flat codec)
bcsvSampler --no-delta -c 'X[0][0] > 0' data.bcsv

# Disable batch compression
bcsvSampler --no-batch -c 'X[0][0] > 0' data.bcsv

# Maximum compression
bcsvSampler --compression-level 12 -c 'X[0][0] > 0' data.bcsv

# No compression at all (flat, no LZ4, no batch)
bcsvSampler --no-delta --no-lz4 --no-batch -c 'X[0][0] > 0' data.bcsv
```

If batch compression was not compiled in (`BCSV_ENABLE_BATCH_CODEC=OFF`), the tool falls back gracefully to packet + lz4 + delta and prints a note when `--verbose` is set.

## Diagnostics

### Compilation Error Feedback

If an expression cannot be compiled, the tool prints the expression with a caret `^` pointing to the error position:

```
Error: Failed to compile conditional expression.
  Expression: X[0][0] > > 100
                        ^
  Unexpected token '>'
```

### Disassembly

Inspect the compiled bytecode without running the filter:

```bash
bcsvSampler --disassemble -c 'X[0][0] > 100' -s 'X[0][0], X[0][1]' data.bcsv
```

### Summary Output

After every run, `bcsvSampler` prints a summary to stderr:

```
=== bcsvSampler Summary ===
Input (5 columns):
  Idx  Name         Type
  ---  -----------  --------
  0    timestamp    double
  1    temperature  float
  2    pressure     float
  3    humidity     uint8
  4    status       string

Output (2 columns):
  Idx  Name    Type
  ---  ------  --------
  0    sel_0   double
  1    sel_1   float

Rows:
  Source rows read:   100000
  Rows written:       28734
  Pass rate:          28.7%

Encoding:  delta + lz4 + batch (level 1)

File sizes:
  Input:  1234567 bytes (1205.63 KB)
  Output: 345678 bytes (337.58 KB)

Performance:
  Wall time:   42 ms
  Throughput:  684143 rows/s

Output written to: data_sampled.bcsv
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (file not found, compile failure, I/O error) |
