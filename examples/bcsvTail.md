# bcsvTail - Display Last Rows in CSV Format

Display the last few rows of a BCSV file in CSV format with memory-efficient circular buffer implementation. Perfect for monitoring recent data and log analysis.

## Basic Usage

```bash
# Display last 10 rows (default) with header
bcsvTail data.bcsv

# Display last 5 rows with header
bcsvTail -n 5 data.bcsv

# Display without header
bcsvTail --no-header data.bcsv

# Display with verbose output
bcsvTail -v data.bcsv
```

## Advanced Options

```bash
bcsvTail [OPTIONS] INPUT_FILE
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

## Key Features

### 1. Header Output by Default

Like `bcsvHead`, `bcsvTail` includes the header by default for better usability:

```bash
# bcsvHead includes header
./bcsvHead -n 3 data.bcsv
# id,name,value
# 1,Alice,100.5
# 2,Bob,200.7
# 3,Carol,150.3

# bcsvTail also includes header (NEW BEHAVIOR)
./bcsvTail -n 3 data.bcsv
# id,name,value
# 18,Rose,230.9
# 19,Sam,275.3
# 20,Tina,215.7

# Use --no-header for data-only output
./bcsvTail -n 3 --no-header data.bcsv
# 18,Rose,230.9
# 19,Sam,275.3
# 20,Tina,215.7
```

### 2. Memory Efficient

Uses a circular buffer to handle large files efficiently:

```bash
# Works efficiently even with huge files
bcsvTail -n 100 massive_dataset.bcsv   # Only uses memory for 100 rows
```

### 3. Pipeline Integration

Perfect for data analysis workflows with or without headers:

```bash
# Monitor recent activity (with header for readability)
bcsvTail -n 50 activity_log.bcsv | grep "ERROR"

# Get statistics on recent data (without header for pure data)
bcsvTail -n 1000 --no-header data.bcsv | awk -F, '{sum+=$3; count++} END {print "Avg:", sum/count}'

# Check data quality of recent entries (header helps with debugging)
bcsvTail data.bcsv | awk -F, 'NF != 4 && NR > 1 {print "Malformed row:", NR}'

# Pipe-friendly counting (exclude header from count)
bcsvTail --no-header data.bcsv | wc -l
```

## Examples

```bash
# Basic inspection of recent data (includes header)
bcsvTail sales_data.bcsv

# Show more recent entries with formatting
bcsvTail -n 25 -p 2 transaction_log.bcsv

# Data-only output for pure processing
bcsvTail -n 100 --no-header data.bcsv | grep -v "^[0-9]" | wc -l

# Custom delimiter for specific analysis
bcsvTail -d '|' --quote-all sensor_data.bcsv

# Extract recent high-value transactions (skip header for awk)
bcsvTail -n 500 --no-header transactions.bcsv | awk -F, '$4 > 1000 {print $2, $4}'

# Monitor data with header for readability
bcsvTail -n 20 --verbose activity_log.bcsv
```

## Comparison with bcsvHead

| Tool | Purpose | Header Default | Use Case |
|------|---------|----------------|----------|
| `bcsvHead` | First N rows | ✅ Included | Data structure inspection, schema validation |
| `bcsvTail` | Last N rows | ✅ Included | Recent data analysis, log monitoring |

**Header Control Options:**

- **Default behavior**: Both tools include headers for better usability
- **`--no-header` option**: Available in both tools for data-only output
- **Pipe-friendly**: Use `--no-header` when piping to tools that expect pure data

```bash
# Combined usage for full picture
echo "=== Data Structure ==="
bcsvHead -n 5 data.bcsv

echo "=== Recent Activity ==="
bcsvTail -n 5 data.bcsv

# Data-only processing
bcsvTail --no-header -n 100 data.bcsv | awk -F, '{sum+=$3} END {print sum}'
```

## Performance

### Memory Efficiency

bcsvTail uses a circular buffer strategy that provides:

- **Constant memory usage**: Only stores the requested number of rows
- **Large file support**: Can process files of any size efficiently
- **Fast processing**: Optimized for speed with minimal overhead

### Memory Usage Examples

| Requested Rows | Memory Usage | File Size Limit |
|---------------|--------------|-----------------|
| 10 rows | ~1 KB | Unlimited |
| 100 rows | ~10 KB | Unlimited |
| 1000 rows | ~100 KB | Unlimited |

## Use Cases

### Log Monitoring
- Monitor recent log entries for errors
- Extract recent activity patterns
- Analyze tail-end data trends

### Data Quality Assurance
- Check recent data for anomalies
- Validate data integrity at file end
- Monitor real-time data streams

### Development & Debugging
- Inspect recent processing results
- Verify data transformation outputs
- Quick validation of generated files

## Tips

1. **Use default header for readability when inspecting data manually**
2. **Use `--no-header` when piping to awk/grep for data processing**
3. **Combine with grep to filter recent entries by condition**
4. **Use `-v` verbose mode to understand file structure**
5. **Memory efficient design makes it safe for any file size**