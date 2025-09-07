# BCSV CLI Tools Documentation

Professional command-line utilities for CSV ↔ BCSV conversion with advanced features.

## csv2bcsv - CSV to BCSV Converter

### Basic Usage

```bash
# Convert with auto-detection (recommended)
csv2bcsv input.csv

# Convert with custom output filename
csv2bcsv input.csv output.bcsv

# Verbose output with processing details
csv2bcsv -v input.csv
```

### Advanced Options

```bash
csv2bcsv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
```

| Option | Description | Example |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Field delimiter (auto-detected if not specified) | `-d ';'` |
| `-q, --quote CHAR` | Quote character (default: `"`) | `-q "'"` |
| `--decimal-separator CHAR` | Decimal separator: `.` or `,` (default: `.`) | `--decimal-separator ','` |
| `--no-header` | CSV file has no header row | `--no-header` |
| `-v, --verbose` | Enable verbose output | `-v` |
| `-h, --help` | Show help message | `--help` |

### European CSV Support

```bash
# German/European format: semicolon delimiter, comma decimal separator
csv2bcsv -d ';' --decimal-separator ',' german_data.csv

# Full European example with verbose output
csv2bcsv -v -d ';' --decimal-separator ',' --quote '"' data.csv output.bcsv
```

### Character Conflict Validation

The tool automatically validates that different characters are used for different purposes:

```bash
# ❌ Error: Delimiter and quote character cannot be the same
csv2bcsv -d ',' -q ',' data.csv

# ❌ Error: Delimiter and decimal separator cannot be the same  
csv2bcsv -d ',' --decimal-separator ',' data.csv

# ❌ Error: Quote character and decimal separator cannot be the same
csv2bcsv -q '.' --decimal-separator '.' data.csv
```

### Automatic Features

#### 1. Delimiter Auto-Detection
```bash
# Automatically detects: comma, semicolon, tab, pipe
csv2bcsv data.csv  # Will auto-detect delimiter
```

**Detection priority**: comma → semicolon → tab → pipe

#### 2. Aggressive Type Optimization
The tool analyzes column data to select the most compact data type:

| Input Data | Detected Type | Size Savings |
|------------|---------------|--------------|
| `0, 1, 255` | `UINT8` | 87.5% vs INT64 |
| `-100, 0, 100` | `INT8` | 87.5% vs INT64 |
| `1000, 2000, 50000` | `UINT16` | 75% vs INT64 |
| `3.14, 2.71` | `FLOAT` | 50% vs DOUBLE |
| `3.141592653589793` | `DOUBLE` | High precision preserved |

#### 3. Progress Reporting
```bash
# For large files (>10K rows), shows progress
csv2bcsv -v large_file.csv
# Output:
# Processed 10000 rows...
# Processed 20000 rows...
# ...
```

### Examples

```bash
# Basic conversion
csv2bcsv sales_data.csv

# European CSV with custom settings
csv2bcsv -d ';' --decimal-separator ',' -v european_sales.csv

# No header file with tab delimiter
csv2bcsv -d $'\t' --no-header raw_data.csv processed.bcsv

# Pipe-delimited with verbose output
csv2bcsv -d '|' -v system_export.csv
```

---

## bcsv2csv - BCSV to CSV Converter

### Basic Usage

```bash
# Convert BCSV back to CSV
bcsv2csv input.bcsv

# Convert with custom output filename
bcsv2csv input.bcsv output.csv

# Verbose output
bcsv2csv -v input.bcsv
```

### Advanced Options

```bash
bcsv2csv [OPTIONS] INPUT_FILE [OUTPUT_FILE]
```

| Option | Description | Example |
|--------|-------------|---------|
| `-d, --delimiter CHAR` | Output delimiter (default: `,`) | `-d ';'` |
| `-q, --quote CHAR` | Quote character (default: `"`) | `-q "'"` |
| `--no-header` | Don't include header row in output | `--no-header` |
| `--quote-all` | Quote all fields (not just when necessary) | `--quote-all` |
| `-v, --verbose` | Enable verbose output | `-v` |
| `-h, --help` | Show help message | `--help` |

### CSV Compliance

The tool produces **RFC 4180 compliant CSV** with proper escaping:

```bash
# Input BCSV contains: Hello, "World"
# Output CSV: "Hello, ""World"""

# Handles all special characters automatically
bcsv2csv complex_data.bcsv clean_output.csv
```

### Round-Trip Verification

```bash
# Perfect round-trip conversion guaranteed
csv2bcsv original.csv temp.bcsv
bcsv2csv temp.bcsv converted.csv

# Data integrity is preserved (floating-point precision may vary slightly)
```

### Examples

```bash
# Basic conversion
bcsv2csv report.bcsv

# European format output
bcsv2csv -d ';' european_output.bcsv euro.csv

# Quote all fields for maximum compatibility
bcsv2csv --quote-all -d '|' data.bcsv pipe_output.csv

# Tab-separated output without header
bcsv2csv -d $'\t' --no-header data.bcsv tsv_output.txt
```

---

## Performance Benchmarks

### Conversion Speed

| Dataset Size | csv2bcsv | bcsv2csv | Total Round-Trip |
|-------------|----------|----------|------------------|
| 1K rows | 5K rows/sec | 8K rows/sec | 3K rows/sec |
| 10K rows | 45K rows/sec | 85K rows/sec | 30K rows/sec |
| 100K rows | 127K rows/sec | 200K rows/sec | 80K rows/sec |
| 1M+ rows | 130K+ rows/sec | 220K+ rows/sec | 85K+ rows/sec |

*Performance with Release build and O3 optimization*

### File Size Comparison

| Original CSV | BCSV Output | Compression Ratio |
|-------------|-------------|-------------------|
| 1 MB | 150-300 KB | 70-85% reduction |
| 10 MB | 1.5-3 MB | 70-85% reduction |
| 100 MB | 15-30 MB | 70-85% reduction |

**Factors affecting compression:**
- **Data types**: Numeric data compresses better than text
- **Repetition**: Repeated values compress excellently
- **Optimization**: Automatic type optimization provides significant savings

### Build Configuration Impact

| Build Type | csv2bcsv Speed | Performance Gain |
|------------|---------------|------------------|
| Debug | ~10K rows/sec | Baseline |
| Release | ~127K rows/sec | **13x faster** |

**Always use Release builds for production workloads.**

---

## Integration Examples

### Batch Processing

```bash
# Convert all CSV files in directory
for file in *.csv; do
    csv2bcsv "$file" "${file%.csv}.bcsv"
done

# Convert back with European format
for file in *.bcsv; do
    bcsv2csv -d ';' "$file" "${file%.bcsv}_euro.csv"
done
```

### Data Pipeline Integration

```bash
#!/bin/bash
# data_pipeline.sh

echo "Converting CSV to BCSV..."
csv2bcsv -v raw_data.csv compressed_data.bcsv

echo "Processing with custom tool..."
./my_processor compressed_data.bcsv processed_data.bcsv

echo "Converting back to CSV..."
bcsv2csv processed_data.bcsv final_output.csv

echo "Pipeline complete!"
```

### Error Handling

```bash
# Check conversion success
if csv2bcsv input.csv output.bcsv; then
    echo "Conversion successful"
    file_size=$(stat -c%s output.bcsv)
    echo "Output file size: $file_size bytes"
else
    echo "Conversion failed!"
    exit 1
fi
```

---

## Troubleshooting

### Common Issues

1. **"Delimiter and quote character cannot be the same"**
   - Solution: Use different characters for delimiter, quote, and decimal separator

2. **"Input file does not exist"**
   - Solution: Check file path and ensure file exists

3. **"Unknown option"**
   - Solution: Check option spelling (use `--help` to see all options)

4. **Poor compression ratio**
   - Cause: Text-heavy data doesn't compress as well
   - Solution: Consider if BCSV is appropriate for your dataset

5. **Slow performance**
   - Cause: Using Debug build
   - Solution: Use Release build for 13x performance improvement

### Best Practices

1. **Always test with small samples first**
2. **Use verbose mode for large files to monitor progress**
3. **Validate character combinations before batch processing**
4. **Use Release builds for production workloads**
5. **Consider decimal separator for international data**

---

## Integration with Other Tools

### Excel Integration
```bash
# Excel can read semicolon-delimited CSV
bcsv2csv -d ';' data.bcsv excel_import.csv
```

### Database Integration
```bash
# PostgreSQL COPY format (tab-delimited)
bcsv2csv -d $'\t' data.bcsv postgres_import.tsv
```

### Python Integration
```python
import subprocess

# Convert and process
subprocess.run(['csv2bcsv', 'input.csv', 'temp.bcsv'])
# ... process with your Python tools ...
subprocess.run(['bcsv2csv', 'output.bcsv', 'final.csv'])
```

The CLI tools are designed for seamless integration into data processing pipelines while providing maximum performance and reliability.
