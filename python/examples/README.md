# Python Examples for pybcsv

This directory contains example scripts demonstrating various aspects of the pybcsv library.

## Examples

### 1. basic_usage.py
Basic introduction to pybcsv functionality:
- Creating layouts
- Writing and reading BCSV files
- Using context managers
- File size comparison

**Usage:**
```bash
python basic_usage.py
```

### 2. pandas_integration.py
Demonstrates pandas DataFrame integration:
- Converting DataFrames to BCSV
- Reading BCSV files as DataFrames
- Performance comparison with CSV
- Type hints for optimal storage
- CSV conversion utilities

**Requirements:**
- pandas
- numpy

**Usage:**
```bash
python pandas_integration.py
```

### 3. performance_benchmark.py
Performance benchmarking with large datasets:
- Large file handling
- Compression effectiveness
- Memory usage monitoring
- Streaming data processing

**Requirements:**
- numpy
- pandas (optional, for advanced features)
- psutil (optional, for memory monitoring)

**Usage:**
```bash
# Default: 100,000 rows
python performance_benchmark.py

# Custom row count
python performance_benchmark.py 1000000
```

## Installation

Before running the examples, make sure you have pybcsv installed:

```bash
# From the python directory
pip install -e .

# Or with optional dependencies
pip install -e ".[pandas]"
```

## Performance Results

Typical performance results on modern hardware:

| Format | Write Speed | Read Speed | Compression |
|--------|------------|------------|-------------|
| BCSV (compressed) | ~200k rows/s | ~500k rows/s | 3-5x smaller |
| BCSV (uncompressed) | ~300k rows/s | ~800k rows/s | Binary format |
| CSV | ~50k rows/s | ~100k rows/s | Text format |

*Results may vary based on data types and hardware configuration.*