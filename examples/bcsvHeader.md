# bcsvHeader - Display File Structure Overview

Display the header structure of a BCSV file showing column index, name, and type. Essential for understanding file schema and structure before processing.

## Basic Usage

```bash
# Display file structure
bcsvHeader data.bcsv

# Display with verbose output
bcsvHeader -v data.bcsv
```

## Command Syntax

```bash
bcsvHeader [OPTIONS] INPUT_FILE
```

| Option | Description | Example |
|--------|-------------|---------|
| `-v, --verbose` | Enable verbose output with additional details | `-v` |
| `-h, --help` | Show help message | `--help` |

## Output Format

The tool displays file structure in a clean, vertical format:

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

### Column Information

- **Index**: 0-based column position
- **Name**: Column identifier as defined in the BCSV layout
- **Type**: Data type (bool, int8, uint8, int16, uint16, int32, uint32, int64, uint64, float, double, string)

## Features

### 1. Quick Schema Inspection

```bash
# Understand file structure before processing
bcsvHeader dataset.bcsv

# Output shows exact layout
BCSV Header Structure: dataset.bcsv
Columns: 5

Index  Name        Type
-----  ----------  ------
0      timestamp   uint64
1      sensor_id   string
2      temperature float
3      humidity    float
4      status      bool
```

### 2. Integration with Other Tools

```bash
# Combine with other CLI tools for analysis
bcsvHeader data.bcsv
echo "Now displaying first few rows:"
bcsvHead -n 5 data.bcsv

# Check schema before conversion
bcsvHeader input.bcsv
bcsv2csv input.bcsv output.csv

# Schema validation in scripts
columns=$(bcsvHeader data.bcsv | grep "Columns:" | awk '{print $2}')
if [ "$columns" -eq 4 ]; then
    echo "Expected schema confirmed"
fi
```

### 3. Data Analysis Planning

```bash
# Understand data types for processing decisions
bcsvHeader financial_data.bcsv

# Plan column access based on index
# Index 2 (score) is float -> can do numeric operations
# Index 1 (name) is string -> needs string handling
```

## Examples

### Basic File Inspection

```bash
# Check what's in a new file
bcsvHeader unknown_dataset.bcsv

# Output reveals structure
BCSV Header Structure: unknown_dataset.bcsv
Columns: 3

Index  Name    Type
-----  ------  ------
0      user    string
1      age     uint8
2      score   double
```

### Schema Validation

```bash
# Verify expected structure
bcsvHeader config_data.bcsv

# Check if file has expected columns
if bcsvHeader data.bcsv | grep -q "temperature"; then
    echo "Temperature column found"
else
    echo "Warning: No temperature column"
fi
```

### Development Workflow

```bash
# 1. Check schema
bcsvHeader dataset.bcsv

# 2. Preview data structure
bcsvHead -n 3 dataset.bcsv

# 3. Process with knowledge of schema
# (Now you know column types and can plan accordingly)
```

### Verbose Mode

```bash
# Get additional information during processing
bcsvHeader -v large_dataset.bcsv

# Output includes processing details
Reading header from: large_dataset.bcsv
Opened BCSV file successfully
Layout contains 12 columns
BCSV Header Structure: large_dataset.bcsv
Columns: 12
...
Successfully displayed header structure
```

## Use Cases

### Data Discovery

- **Unknown files**: Quickly understand structure of new BCSV files
- **Schema validation**: Verify file contains expected columns and types
- **Documentation**: Generate column references for documentation

### Development

- **API design**: Understand data structure for processing logic
- **Type planning**: Choose appropriate data types for operations
- **Integration**: Plan how to integrate with other tools and formats

### Quality Assurance

- **Schema consistency**: Verify multiple files have same structure
- **Type validation**: Ensure columns have expected data types
- **Documentation**: Create schema documentation from existing files

## Performance

bcsvHeader is optimized for fast schema inspection:

- **Header-only reading**: Only reads file metadata, not data rows
- **Minimal memory**: Uses very little memory regardless of file size
- **Fast execution**: Returns results quickly even for very large files
- **Error handling**: Provides clear error messages for invalid files

## Integration Patterns

```bash
# Schema-aware processing pipeline
schema=$(bcsvHeader data.bcsv)
if echo "$schema" | grep -q "timestamp.*uint64"; then
    # Process as time-series data
    process_timeseries.sh data.bcsv
else
    # Process as regular data
    process_regular.sh data.bcsv
fi

# Automated documentation generation
echo "# Data Schema" > schema_doc.md
bcsvHeader *.bcsv >> schema_doc.md

# File validation
for file in *.bcsv; do
    echo "Checking $file..."
    if ! bcsvHeader "$file" > /dev/null 2>&1; then
        echo "Error: $file has invalid schema"
    fi
done
```

## Tips

1. **Always check schema before processing unknown files**
2. **Use verbose mode (`-v`) when debugging file issues**
3. **Combine with bcsvHead to get complete file overview**
4. **Use in scripts to validate expected file structure**
5. **Column indices are 0-based - important for programmatic access**

## Related Tools

- **bcsvHead**: Display first few rows in CSV format
- **bcsvTail**: Display last few rows in CSV format  
- **bcsv2csv**: Convert BCSV files to CSV format
- **csv2bcsv**: Convert CSV files to BCSV format
