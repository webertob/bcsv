# PyBCSV - Python Bindings for BCSV Library

PyBCSV provides Python bindings for the high-performance BCSV (Binary CSV) library, enabling efficient binary CSV file handling with pandas integration.

## Features

- **High Performance**: Binary format with optional LZ4 compression
- **Pandas Integration**: Direct DataFrame read/write support
- **Type Safety**: Preserves column types and data integrity
- **Cross-platform**: Works on Linux, macOS, and Windows
- **Memory Efficient**: Streaming support for large datasets

## Installation

The Python wrapper has been successfully built and installed. To use it:

```bash
cd /home/tobias/bcsv/python
source venv/bin/activate
pip install .
```

## Basic Usage

### Core BCSV Operations

```python
import pybcsv

# Create a layout
layout = pybcsv.Layout()
layout.add_column("id", pybcsv.INT32)
layout.add_column("name", pybcsv.STRING) 
layout.add_column("value", pybcsv.DOUBLE)

# Write data
writer = pybcsv.Writer(layout)
writer.open("data.bcsv")
writer.write_row([1, "Alice", 123.45])
writer.write_row([2, "Bob", 678.90])
writer.close()

# Read data
reader = pybcsv.Reader()
reader.open("data.bcsv")
all_rows = reader.read_all()
reader.close()

print(all_rows)
# Output: [[1, 'Alice', 123.45], [2, 'Bob', 678.9]]
```

### Pandas Integration

```python
import pybcsv
import pandas as pd

# Create a DataFrame
df = pd.DataFrame({
    'id': [1, 2, 3],
    'name': ['Alice', 'Bob', 'Charlie'],
    'value': [123.45, 678.90, 111.22]
})

# Write DataFrame to BCSV
pybcsv.write_dataframe(df, "data.bcsv")

# Read back as DataFrame
df_read = pybcsv.read_dataframe("data.bcsv")
print(df_read.equals(df))  # True
```

### CSV Conversion

```python
import pybcsv

# Convert CSV to BCSV
pybcsv.from_csv("input.csv", "output.bcsv")

# Convert BCSV to CSV
pybcsv.to_csv("output.bcsv", "output.csv")
```

## Available Types

- `pybcsv.BOOL` - Boolean values
- `pybcsv.INT8` / `pybcsv.UINT8` - 8-bit integers
- `pybcsv.INT16` / `pybcsv.UINT16` - 16-bit integers  
- `pybcsv.INT32` / `pybcsv.UINT32` - 32-bit integers
- `pybcsv.INT64` / `pybcsv.UINT64` - 64-bit integers
- `pybcsv.FLOAT` - 32-bit floating point
- `pybcsv.DOUBLE` - 64-bit floating point
- `pybcsv.STRING` - Variable-length strings

## API Reference

### Layout Class

```python
layout = pybcsv.Layout()
layout.add_column(name: str, column_type: ColumnType)
layout.column_count() -> int
layout.column_name(index: int) -> str
layout.column_type(index: int) -> ColumnType
layout.has_column(name: str) -> bool
layout.column_index(name: str) -> int
```

### Writer Class

```python
writer = pybcsv.Writer(layout: Layout)
writer.open(filename: str) -> bool
writer.write_row(values: list) -> None
writer.flush() -> None
writer.close() -> None
writer.is_open() -> bool
```

### Reader Class

```python
reader = pybcsv.Reader()
reader.open(filename: str) -> bool
reader.read_next() -> bool
reader.read_all() -> list[list]
reader.close() -> None
reader.is_open() -> bool
reader.layout() -> Layout
```

### Utility Functions

```python
# Pandas integration
pybcsv.write_dataframe(df: pd.DataFrame, filename: str, compression: bool = True)
pybcsv.read_dataframe(filename: str) -> pd.DataFrame

# CSV conversion
pybcsv.from_csv(csv_filename: str, bcsv_filename: str, compression: bool = True)
pybcsv.to_csv(bcsv_filename: str, csv_filename: str)

# Type utilities
pybcsv.type_to_string(column_type: ColumnType) -> str
```

## Performance Benefits

The binary format provides significant advantages:

1. **Faster I/O**: Binary format is faster to read/write than text CSV
2. **Type Safety**: Preserves exact data types without parsing
3. **Compression**: Optional LZ4 compression reduces file size
4. **Memory Efficiency**: Streaming support for large datasets

## Testing

Run the included test scripts to verify functionality:

```bash
python test_basic.py      # Basic BCSV operations
python test_pandas.py     # Pandas integration tests
```

## Python Benchmark Lane (Item 11.B)

Dedicated benchmark runner:

```bash
python3 python/benchmarks/run_pybcsv_benchmarks.py --size=S
```

See `python/benchmarks/README.md` for workload/mode options and output schema.

## File Structure

```text
python/
├── pybcsv/
│   ├── __init__.py           # Main module interface
│   ├── __version__.py        # Version information
│   ├── bindings.cpp          # C++ pybind11 bindings
│   └── pandas_utils.py       # Pandas integration utilities
├── examples/
│   ├── basic_example.py      # Basic usage examples
│   └── pandas_example.py     # Pandas integration examples
├── tests/
│   ├── test_basic.py         # Basic functionality tests
│   └── test_pandas.py        # Pandas integration tests
├── setup.py                  # Package build configuration
├── pyproject.toml           # Modern Python packaging config
└── README.md                # This documentation
```

## Compatibility

- **Python**: 3.7+ (tested with 3.12)
- **Dependencies**:
  - numpy >= 1.19.0 (required)
  - pandas >= 1.3.0 (optional, for DataFrame integration)
- **Platforms**: Linux, macOS, Windows
- **Compilers**: GCC 7+, Clang 8+, MSVC 2019+

## Performance Results

Based on testing with sample data:

- **DataFrame I/O**: Perfect data fidelity with type preservation
- **File Size**: Efficient binary encoding (varies by data and compression)
- **Speed**: Significantly faster than CSV for repeated I/O operations
- **Memory**: Streaming support for large datasets

The Python wrapper successfully bridges the high-performance C++ BCSV library with Python's data science ecosystem, providing both convenience and performance for data processing workflows.

## License

MIT License

Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See the [LICENSE](LICENSE) file for full details.

## Publishing

To publish built wheels to TestPyPI and PyPI you'll need to create API tokens and add them as GitHub repository secrets.

1. Create API tokens:

  - TestPyPI: Go to the TestPyPI account page and create an API token. Copy the token: [TestPyPI account page](https://test.pypi.org/manage/account/).

  - PyPI: Go to the PyPI account page and create an API token for the project (or your account). Copy the token: [PyPI account page](https://pypi.org/manage/account/).

2. Add GitHub secrets:

  - In your repository on GitHub, go to Settings → Secrets → Actions.

  - Add a new secret named `TEST_PYPI_API_TOKEN` and paste the TestPyPI token.

  - Optionally add `PYPI_API_TOKEN` with the PyPI token when you're ready to publish to the main index.

3. Trigger the publish workflow:

  - The workflow triggers on pushes to the `release` branch or via manual `workflow_dispatch`.

4. Install from TestPyPI for verification:

```bash
# in a fresh virtualenv
python -m venv venv && source venv/bin/activate
pip install --index-url https://test.pypi.org/simple/ --extra-index-url https://pypi.org/simple pybcsv
python -c "import pybcsv; print(pybcsv.__version__)"
```

If the import and version check succeed the wheel is good for release.