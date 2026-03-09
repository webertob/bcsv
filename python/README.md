# PyBCSV — Python Bindings for BCSV Library

High-performance Python bindings for the [BCSV](https://github.com/bcsv/bcsv) (Binary CSV) library — fast, compact time-series storage with pandas integration.

## Features

- **High Performance**: Binary format with optional LZ4 compression and delta encoding
- **Pandas Integration**: Columnar DataFrame read/write via numpy zero-copy
- **Type Safety**: Preserves column types and data integrity (10 numeric types + strings)
- **Cross-platform**: Linux (x86_64, ARM64), macOS (x86_64, ARM64), Windows (AMD64)
- **Context Managers**: All readers/writers support `with` statements
- **Streaming I/O**: Row-by-row read/write, never loads entire file into memory
- **Direct Access**: Random-access reads by row index via `ReaderDirectAccess`
- **Sampler**: Bytecode VM for server-side row filtering and column projection
- **CSV Interop**: Convert between CSV and BCSV via `from_csv()` / `to_csv()`

## Installation

```bash
pip install pybcsv

# With pandas support
pip install pybcsv[pandas]
```

## Quick Start

### Write and Read

```python
import pybcsv

# Define schema
layout = pybcsv.Layout()
layout.add_column("id", pybcsv.INT32)
layout.add_column("name", pybcsv.STRING)
layout.add_column("value", pybcsv.DOUBLE)

# Write rows (context manager auto-closes)
with pybcsv.Writer(layout) as writer:
    writer.open("data.bcsv")
    writer.write_row([1, "Alice", 123.45])
    writer.write_row([2, "Bob", 678.90])

# Read all rows
with pybcsv.Reader() as reader:
    reader.open("data.bcsv")
    for row in reader:          # iterator protocol
        print(row)
    # or: all_rows = reader.read_all()
```

### Pandas Integration

```python
import pybcsv
import pandas as pd

df = pd.DataFrame({
    'id': [1, 2, 3],
    'name': ['Alice', 'Bob', 'Charlie'],
    'value': [123.45, 678.90, 111.22]
})

# Write DataFrame (columnar path, numpy zero-copy for numerics)
pybcsv.write_dataframe(df, "data.bcsv")

# Read back as DataFrame
df_read = pybcsv.read_dataframe("data.bcsv")
```

### CSV Conversion

```python
import pybcsv

pybcsv.from_csv("input.csv", "output.bcsv")   # CSV → BCSV
pybcsv.to_csv("output.bcsv", "output.csv")    # BCSV → CSV
```

### Random Access

```python
import pybcsv

with pybcsv.ReaderDirectAccess() as da:
    da.open("data.bcsv")
    print(f"Total rows: {len(da)}")
    row = da[42]         # read row 42 directly (O(1) seek)
    print(da.read(100))  # alternative syntax
```

## Available Types

| Constant | Description |
|----------|-------------|
| `pybcsv.BOOL` | Boolean |
| `pybcsv.INT8` / `pybcsv.UINT8` | 8-bit integers |
| `pybcsv.INT16` / `pybcsv.UINT16` | 16-bit integers |
| `pybcsv.INT32` / `pybcsv.UINT32` | 32-bit integers |
| `pybcsv.INT64` / `pybcsv.UINT64` | 64-bit integers |
| `pybcsv.FLOAT` | 32-bit float |
| `pybcsv.DOUBLE` | 64-bit float |
| `pybcsv.STRING` | Variable-length string |

## API Reference

### Layout

```python
layout = pybcsv.Layout()                              # empty layout
layout = pybcsv.Layout([ColumnDefinition("x", INT32)]) # from list

layout.add_column(name: str, type: ColumnType)
layout.add_column(col: ColumnDefinition)
layout.column_count() -> int
layout.column_name(index: int) -> str
layout.column_type(index: int) -> ColumnType
layout.has_column(name: str) -> bool
layout.column_index(name: str) -> int
layout.get_column_names() -> list[str]
layout.get_column_types() -> list[ColumnType]
layout.get_column(index: int) -> ColumnDefinition
len(layout)           # column count
layout[i]             # ColumnDefinition at index i
```

### Writer

```python
writer = pybcsv.Writer(layout: Layout, row_codec: str = "delta")
writer.open(filename: str, overwrite: bool = True,
            compression_level: int = 1, block_size_kb: int = 64,
            flags: FileFlags = FileFlags.BATCH_COMPRESS)  # raises RuntimeError on failure
writer.write_row(values: list)
writer.write_rows(rows: list[list])     # batch write
writer.flush()
writer.close()
writer.is_open() -> bool
writer.row_count() -> int
writer.row_codec() -> str
writer.compression_level() -> int
writer.layout() -> Layout

# Context manager
with pybcsv.Writer(layout) as w:
    w.open("out.bcsv")
    w.write_row([...])
```

Row codec options: `"flat"`, `"zoh"` (zero-order hold), `"delta"` (default).

### Reader

```python
reader = pybcsv.Reader()
reader.open(filename: str)              # raises RuntimeError on failure
reader.read_next() -> bool              # advance to next row
reader.read_row() -> list | None        # read+advance, None at EOF
reader.read_all() -> list[list]         # read remaining rows
reader.close()
reader.is_open() -> bool
reader.layout() -> Layout
reader.row_pos() -> int                 # current row index
reader.row_value(column: int) -> Any    # typed value from current row
reader.row_dict() -> dict               # current row as {name: value}
reader.file_flags() -> FileFlags
reader.compression_level() -> int
reader.version_string() -> str
reader.creation_time() -> str
reader.count_rows() -> int              # total row count

# Iterator protocol
for row in reader:
    print(row)

# Context manager
with pybcsv.Reader() as r:
    r.open("data.bcsv")
    for row in r:
        print(row)
```

### ReaderDirectAccess

Random-access reader — reads any row by index without scanning.

```python
da = pybcsv.ReaderDirectAccess()
da.open(filename: str, rebuild_footer: bool = False)
da.read(index: int) -> list             # read row at index
da.row_count() -> int
da.layout() -> Layout
da.close()
da.is_open() -> bool
da.file_flags() -> FileFlags
da.compression_level() -> int
da.version_string() -> str
da.creation_time() -> str

len(da)               # row count
da[i]                 # read row at index i
```

### CsvWriter / CsvReader

Native CSV I/O with the same Layout-based schema.

```python
# Write CSV
csv_w = pybcsv.CsvWriter(layout, delimiter=',', decimal_sep='.')
csv_w.open(filename, overwrite=True, include_header=True)
csv_w.write_row(values)
csv_w.write_rows(rows)
csv_w.close()

# Read CSV
csv_r = pybcsv.CsvReader(layout, delimiter=',', decimal_sep='.')
csv_r.open(filename, has_header=True)
for row in csv_r:       # iterator support
    print(row)
csv_r.close()
```

### Sampler

Bytecode VM for filtering and projecting rows from an open Reader.

```python
reader = pybcsv.Reader()
reader.open("data.bcsv")

sampler = pybcsv.Sampler(reader)
sampler.set_conditional("col_a > 10")    # filter expression
sampler.set_selection("col_a, col_b")    # column projection

result = sampler.output_layout()         # SamplerCompileResult (bool-testable)
if result:
    for row in sampler:                  # iterate matching rows
        print(row)
```

### FileFlags

```python
pybcsv.FileFlags.NONE
pybcsv.FileFlags.ZERO_ORDER_HOLD
pybcsv.FileFlags.NO_FILE_INDEX
pybcsv.FileFlags.STREAM_MODE
pybcsv.FileFlags.BATCH_COMPRESS
pybcsv.FileFlags.DELTA_ENCODING

# Combinable with | and &
flags = pybcsv.FileFlags.BATCH_COMPRESS | pybcsv.FileFlags.NO_FILE_INDEX
```

### Utility Functions

```python
# Pandas integration (requires pandas)
pybcsv.write_dataframe(df, filename,
                       compression_level=1,
                       row_codec="delta",
                       type_hints=None)  # dict[str, ColumnType]
pybcsv.read_dataframe(filename, columns=None)  # -> pd.DataFrame

# CSV conversion (requires pandas)
pybcsv.from_csv(csv_file, bcsv_file, compression_level=1, type_hints=None)
pybcsv.to_csv(bcsv_file, csv_file)

# Columnar I/O (numpy arrays)
pybcsv.read_columns(filename) -> dict[str, np.ndarray | list[str]]
pybcsv.write_columns(filename, columns, col_order, col_types,
                     row_codec="delta", compression_level=1)
pybcsv.read_to_dataframe(filename, columns=None) -> pd.DataFrame

# Type utilities
pybcsv.type_to_string(column_type) -> str
```

## Testing

```bash
pip install pybcsv[test]
python -m pytest tests/ -v
```

## File Structure

```text
python/
├── pybcsv/
│   ├── __init__.py           # Public API and exports
│   ├── __version__.py        # Version (setuptools-scm)
│   ├── bindings.cpp          # C++ pybind11 bindings
│   └── pandas_utils.py       # Pandas/CSV integration
├── examples/
│   ├── basic_usage.py        # Core BCSV operations
│   ├── pandas_integration.py # DataFrame examples
│   ├── advanced_usage.py     # DirectAccess, Sampler, CSV, columnar I/O
│   └── performance_benchmark.py
├── tests/                    # 17 test modules (pytest)
├── benchmarks/               # Python benchmark runner
├── setup.py
├── pyproject.toml
└── README.md
```

## Compatibility

- **Python**: 3.11, 3.12, 3.13
- **Platforms**: Linux (x86_64, ARM64), macOS (x86_64, ARM64), Windows (AMD64)
- **Compilers**: GCC 13+, Clang 16+, MSVC 2022 17.4+, Apple Clang (Xcode 15.4+)
- **C++ Standard**: C++20
- **Dependencies**:
  - numpy >= 1.19.0 (required)
  - pandas >= 1.0.0 (optional — `pip install pybcsv[pandas]`)

## License

MIT — see [LICENSE](LICENSE) for details.

## Publishing

Wheels are built automatically via GitHub Actions (cibuildwheel) and published using
[Trusted Publisher (OIDC)](https://docs.pypi.org/trusted-publishers/) — no API tokens required.

- **TestPyPI**: every push to `main`/`master` or version tags
- **PyPI**: only on `v*` tags (e.g. `git tag v1.4.0 && git push origin v1.4.0`)

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