# bcsv2csv - BCSV to CSV Converter

Convert BCSV files back to CSV format with full RFC 4180 compliance and advanced formatting options. Perfect for data export and integration with CSV-based tools.

## Basic Usage

```bash
# Convert BCSV back to CSV
bcsv2csv input.bcsv

# Convert with custom output filename
bcsv2csv input.bcsv output.csv

# Verbose output
bcsv2csv -v input.bcsv
```

## Advanced Options

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

## CSV Compliance

The tool produces **RFC 4180 compliant CSV** with proper escaping:

```bash
# Input BCSV contains: Hello, "World"
# Output CSV: "Hello, ""World"""

# Handles all special characters automatically
bcsv2csv complex_data.bcsv clean_output.csv
```

## Round-Trip Verification

```bash
# Perfect round-trip conversion guaranteed
csv2bcsv original.csv temp.bcsv
bcsv2csv temp.bcsv converted.csv

# Data integrity is preserved (floating-point precision may vary slightly)
```

## Examples

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

## Performance

### Conversion Speed

| Dataset Size | bcsv2csv Speed | Use Case |
|-------------|----------------|----------|
| 1K rows | 8K rows/sec | Development |
| 10K rows | 85K rows/sec | Small datasets |
| 100K rows | 200K rows/sec | Production |
| 1M+ rows | 220K+ rows/sec | Large-scale |

### Performance Notes

Performance with Release build and O3 optimization

### Build Configuration Impact

| Build Type | bcsv2csv Speed | Performance Gain |
|------------|----------------|------------------|
| Debug | ~15K rows/sec | Baseline |
| Release | ~200K rows/sec | **13x faster** |

**Always use Release builds for production workloads.**

## Integration Examples

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

## Features

### Automatic CSV Escaping

- **Quote escaping**: Doubles internal quotes correctly
- **Delimiter handling**: Quotes fields containing delimiters
- **Newline preservation**: Handles embedded newlines safely
- **Whitespace protection**: Preserves leading/trailing spaces

### Flexible Output Formats

- **Custom delimiters**: Semicolon, tab, pipe, or any character
- **Quote control**: Choose quote character or quote all fields
- **Header control**: Include or exclude header row
- **European formats**: Support for regional CSV variants

### Data Integrity

- **Type preservation**: Maintains original data types and precision
- **Lossless conversion**: No data corruption during conversion
- **Unicode support**: Full UTF-8 character set handling
- **Large file support**: Efficient processing of massive datasets

## Use Cases

### Data Export
- Export BCSV data for Excel analysis
- Generate CSV reports for stakeholders
- Create database import files

### System Integration
- Interface with legacy CSV-based systems
- Generate input for third-party tools
- Export for data visualization tools

### Development & Testing
- Convert test data for validation
- Generate human-readable output for debugging
- Create sample datasets for development

## Troubleshooting

### Common Issues

1. **"Input file does not exist"**
   - Solution: Check file path and ensure BCSV file exists

2. **"Cannot open BCSV file"**
   - Solution: Verify file is valid BCSV format

3. **"Unknown option"**
   - Solution: Check option spelling (use `--help` to see all options)

4. **Character encoding issues**
   - Solution: Ensure consistent UTF-8 encoding

### Best Practices

1. **Test with small samples first**
2. **Use `--quote-all` for maximum compatibility**
3. **Specify delimiter explicitly for non-comma output**
4. **Use Release builds for performance**
5. **Validate output format with target system**

## Tips

1. **Use `-d ';'` for European Excel compatibility**
2. **Use `--no-header` when importing to databases**
3. **Combine with `--quote-all` for problematic data**
4. **Use tab delimiter for easy text processing**
5. **Always verify round-trip conversion for critical data**