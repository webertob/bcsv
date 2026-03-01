# bcsvHead - Display First Rows in CSV Format

Display the first few rows of a BCSV file in CSV format with advanced formatting options. Perfect for data inspection and pipeline integration.

## Basic Usage

```bash
# Display first 10 rows (default) with header
bcsvHead data.bcsv

# Display first 5 rows with header
bcsvHead -n 5 data.bcsv

# Display without header
bcsvHead --no-header data.bcsv

# Display with verbose output
bcsvHead -v data.bcsv
```

## Advanced Options

```bash
bcsvHead [OPTIONS] INPUT_FILE
```

| Option | Description | Example |
|--------|-------------|---------|
| `-n, --lines N` | Number of rows to display (default: 10) | `-n 20` |
| `-d, --delimiter CHAR` | Field delimiter (default: `,`) | `-d ';'` |
| `-q, --quote CHAR` | Quote character (default: `"`) | `-q "'"` |
| `--quote-all` | Quote all fields (not just when necessary) | `--quote-all` |
| `--no-header` | Don't include header row in output | `--no-header` |
| `-p, --precision N` | Floating point precision (default: auto) | `-p 2` |
| `-v, --verbose` | Enable verbose output | `-v` |
| `-h, --help` | Show help message | `--help` |

## Features

### 1. Quick Data Inspection

```bash
# Quick peek at file structure and first few rows
bcsvHead dataset.bcsv

# Output includes header automatically
id,name,score,active
1,Alice Johnson,95.500000,true
2,Bob Smith,87.199997,true
3,Carol Williams,92.800003,false
```

### 2. Pipe-Friendly Output

Designed for integration with Unix tools:

```bash
# Filter rows by condition
bcsvHead data.bcsv | grep "Alice"

# Count lines (header + data rows)
bcsvHead -n 20 data.bcsv | wc -l

# Extract specific columns with awk
bcsvHead data.bcsv | awk -F, '{print $2, $3}'

# Find high-value entries
bcsvHead data.bcsv | awk -F, '$3 > 250 {print $2, $3}'
```

### 3. Custom Formatting

```bash
# Semicolon-delimited with quotes
bcsvHead -d ';' --quote-all data.bcsv

# Fixed precision for floats
bcsvHead -p 2 data.bcsv

# European format
bcsvHead -d ';' --quote-all -p 1 data.bcsv
```

## Examples

```bash
# Basic inspection
bcsvHead sales_data.bcsv

# Show more rows with formatting
bcsvHead -n 25 -p 1 financial_data.bcsv

# Data-only output for processing
bcsvHead -n 10 --no-header data.bcsv | awk -F, '{sum+=$3} END {print sum}'

# Pipe to other tools for analysis
bcsvHead -n 100 data.bcsv | grep "error" | wc -l

# Custom delimiter for specific tools
bcsvHead -d '|' --quote-all data.bcsv | some_tool

# Check file structure before processing
bcsvHead -n 3 -v unknown_data.bcsv
```

## Integration Patterns

```bash
# Quick data validation
if bcsvHead -n 1 data.bcsv | grep -q "expected_column"; then
    echo "File format is correct"
fi

# Extract header for schema validation
header=$(bcsvHead -n 0 data.bcsv 2>/dev/null || true)

# Preview before conversion
echo "Preview of data to convert:"
bcsvHead -n 5 -p 2 data.bcsv
echo "Proceeding with full conversion..."
bcsv2csv data.bcsv output.csv
```

## Performance

bcsvHead is optimized for fast startup and minimal memory usage:

- **Memory efficient**: Only loads requested number of rows
- **Fast startup**: Minimal overhead for quick data inspection
- **Pipe optimized**: Designed for seamless integration with Unix tools
- **Large file friendly**: Works efficiently even with massive BCSV files

## Use Cases

### Data Exploration
- Quick inspection of file structure and content
- Schema validation before processing
- Sample data extraction for analysis

### Pipeline Integration
- Data validation in automated workflows
- Header extraction for dynamic processing
- Integration with grep, awk, and other Unix tools

### Development
- Debugging data processing issues
- Verifying file format and content
- Testing data transformations

## Tips

1. **Use `--no-header` when piping to tools that expect pure data**
2. **Combine with grep for quick data filtering**
3. **Use `-v` to understand file structure before processing**
4. **Set precision (`-p`) for consistent floating-point output**
5. **Use custom delimiters (`-d`) for specific tool compatibility**