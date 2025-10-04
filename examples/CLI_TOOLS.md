# BCSV CLI Tools

Professional command-line utilities for CSV ↔ BCSV conversion and data manipulation. These tools provide high-performance data processing with advanced features for production workflows.

## Overview

The BCSV library includes five main CLI tools designed for seamless integration into data processing pipelines:

| Tool | Purpose | Input | Output | Key Features |
|------|---------|-------|--------|-------------|
| **[csv2bcsv](csv2bcsv.md)** | Convert CSV to BCSV | CSV | BCSV | Auto-detection, type optimization, compression |
| **[bcsvHeader](bcsvHeader.md)** | Display file structure | BCSV | Text | Schema overview, column info, quick inspection |
| **[bcsvHead](bcsvHead.md)** | Display first rows | BCSV | CSV | Quick inspection, pipe-friendly, header control |
| **[bcsvTail](bcsvTail.md)** | Display last rows | BCSV | CSV | Memory efficient, recent data analysis |
| **[bcsv2csv](bcsv2csv.md)** | Convert BCSV to CSV | BCSV | CSV | RFC 4180 compliant, round-trip safe |

## Quick Start

### Basic Workflow

```bash
# 1. Convert CSV to BCSV (70-85% size reduction)
csv2bcsv data.csv data.bcsv

# 2. Inspect file structure
bcsvHeader data.bcsv

# 3. Preview first few rows
bcsvHead data.bcsv -n 5

# 4. Check recent entries
bcsvTail data.bcsv -n 5

# 5. Convert back to CSV when needed
bcsv2csv data.bcsv output.csv
```

### Pipeline Integration

```bash
# Data validation pipeline
csv2bcsv input.csv temp.bcsv && \
bcsvHead temp.bcsv | grep -q "expected_column" && \
bcsvTail temp.bcsv -n 100 --no-header | your_validator && \
bcsv2csv temp.bcsv validated_output.csv
```

## Tool Highlights

### [csv2bcsv](csv2bcsv.md) - CSV to BCSV Converter

**Primary Use**: Convert CSV files to BCSV format with automatic optimization

**Key Benefits**:
- **70-85% file size reduction** through intelligent compression
- **Automatic type detection** for optimal storage efficiency  
- **European CSV support** (semicolon delimiters, comma decimals)
- **13x performance improvement** with Release builds

```bash
# Auto-convert with optimization
csv2bcsv sales_data.csv

# European format with verbose output
csv2bcsv -d ';' --decimal-separator ',' -v european_data.csv
```

### [bcsvHeader](bcsvHeader.md) - Display File Structure

**Primary Use**: Quick schema inspection and file structure analysis

**Key Benefits**:
- **Instant schema overview** with column index, name, and type
- **Header-only reading** for minimal resource usage
- **Structure validation** before data processing
- **Development aid** for understanding file layouts

```bash
# Quick schema inspection
bcsvHeader dataset.bcsv

# Verbose output with processing details
bcsvHeader -v complex_data.bcsv
```

### [bcsvHead](bcsvHead.md) - Display First Rows

**Primary Use**: Quick data inspection and pipeline validation

**Key Benefits**:
- **Fast data preview** without loading entire file
- **Header included by default** for better readability
- **Pipe-friendly output** for Unix tool integration
- **Custom formatting options** for various output needs

```bash
# Quick file inspection
bcsvHead dataset.bcsv

# Data-only output for processing
bcsvHead --no-header data.bcsv | awk -F, '{sum+=$3} END {print sum}'
```

### [bcsvTail](bcsvTail.md) - Display Last Rows  

**Primary Use**: Recent data analysis and log monitoring

**Key Benefits**:
- **Memory efficient** circular buffer design
- **Unlimited file size support** with constant memory usage
- **Header control** for both readability and data processing
- **Perfect for log analysis** and recent activity monitoring

```bash
# Monitor recent activity
bcsvTail activity_log.bcsv -n 50

# Extract recent high-value transactions
bcsvTail --no-header transactions.bcsv | awk -F, '$4 > 1000'
```

### [bcsv2csv](bcsv2csv.md) - BCSV to CSV Converter

**Primary Use**: Export BCSV data to CSV format for external tools

**Key Benefits**:
- **RFC 4180 compliant** CSV output
- **Round-trip conversion safety** with data integrity preservation
- **Flexible output formats** (Excel, database, custom)
- **High-performance processing** for large datasets

```bash
# Standard conversion
bcsv2csv report.bcsv

# Excel-compatible format
bcsv2csv -d ';' data.bcsv excel_import.csv
```

## Performance Overview

All tools are optimized for production use with significant performance improvements in Release builds:

| Tool | Debug Performance | Release Performance | Improvement |
|------|------------------|-------------------|-------------|
| csv2bcsv | ~10K rows/sec | ~127K rows/sec | **13x faster** |
| bcsvHead | Instant | Instant | Optimized |
| bcsvTail | Memory efficient | Memory efficient | Constant memory |
| bcsv2csv | ~15K rows/sec | ~200K rows/sec | **13x faster** |

### File Size Benefits

BCSV format provides substantial storage savings:

- **Typical compression**: 70-85% size reduction vs CSV
- **Type optimization**: Automatic selection of most efficient data types
- **Large file friendly**: Maintains compression efficiency at scale

## Integration Patterns

### Unix Pipeline Integration

```bash
# Data quality pipeline
bcsvHead data.bcsv | grep "ERROR" | wc -l

# Statistical analysis
bcsvTail --no-header data.bcsv -n 1000 | \
  awk -F, '{sum+=$3; count++} END {print "Average:", sum/count}'

# Format conversion chain
csv2bcsv input.csv | process_bcsv | bcsv2csv output.csv
```

### Batch Processing

```bash
# Convert all CSV files in directory
for file in *.csv; do
  csv2bcsv "$file" "${file%.csv}.bcsv"
  echo "Converted: $file -> ${file%.csv}.bcsv"
done

# Validate all BCSV files
for file in *.bcsv; do
  echo "=== $file ==="
  bcsvHead "$file" -n 3
  echo "..."
  bcsvTail "$file" -n 3
done
```

### Data Validation Workflows

```bash
#!/bin/bash
# comprehensive_validation.sh

echo "Validating CSV structure..."
if ! bcsvHead data.bcsv -n 1 | grep -q "expected_header"; then
  echo "ERROR: Invalid file structure"
  exit 1
fi

echo "Checking data quality..."
error_count=$(bcsvTail data.bcsv -n 1000 --no-header | grep "ERROR" | wc -l)
echo "Found $error_count errors in recent data"

echo "Validation complete!"
```

## Getting Started

1. **[Read csv2bcsv documentation](csv2bcsv.md)** for data conversion
2. **[Read bcsvHeader documentation](bcsvHeader.md)** for file structure inspection
3. **[Read bcsvHead documentation](bcsvHead.md)** for data inspection  
4. **[Read bcsvTail documentation](bcsvTail.md)** for recent data analysis
5. **[Read bcsv2csv documentation](bcsv2csv.md)** for data export

## Build Requirements

All tools require a Release build for optimal performance:

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make csv2bcsv bcsvHeader bcsvHead bcsvTail bcsv2csv
```

## Support

For detailed documentation on each tool, click the links above or refer to the individual documentation files in the `examples/` directory.