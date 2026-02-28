# BCSV Python Bindings — AI Skills Reference

> Quick-reference for AI agents working with the Python package (pybcsv).
> For full API reference, see: python/README.md

## Build & Install

```bash
# Editable install (for development)
cd python
pip install -e .

# This automatically:
# 1. Runs sync_headers.py (copies ../include/ → python/include/, gitignored)
# 2. Compiles pybind11 C++ extension (_bcsv.so)
# 3. Installs pybcsv package in editable mode
```

### Prerequisites
- Python 3.8+
- C++20 compiler (same as main library)
- pip, setuptools
- pybind11 (auto-installed via pyproject.toml)

## Run Tests

```bash
cd python
pytest tests/           # All tests
pytest tests/ -v        # Verbose
pytest tests/test_basic.py -k "test_write"  # Specific test
```

13 test files in `python/tests/` covering types, errors, interop, pandas, performance.

## Package Structure

```
python/
├── setup.py, pyproject.toml    # Build config, pybind11 extension
├── MANIFEST.in                 # Source distribution includes
├── sync_headers.py             # Copies C++ headers from ../include/
├── pybcsv/
│   ├── __init__.py             # Top-level: imports _bcsv (compiled) + pandas_utils
│   ├── __version__.py          # Version string
│   ├── bindings.cpp            # pybind11 C++ extension (Layout, Writer, Reader, Row)
│   ├── pandas_utils.py         # write_dataframe(), read_dataframe(), to_csv(), from_csv()
│   └── py.typed                # PEP 561 marker
├── examples/
│   ├── basic_usage.py
│   ├── pandas_integration.py
│   └── performance_benchmark.py
└── tests/                      # 15 test files
```

## Key API

```python
import pybcsv

# Layout
layout = pybcsv.Layout()
layout.add_column("time", pybcsv.ColumnType.DOUBLE)
layout.add_column("value", pybcsv.ColumnType.FLOAT)

# Write (layout passed to constructor, not open)
writer = pybcsv.Writer(layout)
writer.open("data.bcsv")             # throws RuntimeError on failure
writer.write_row([1.0, 42.0])        # pass values as Python list
writer.write_rows([[1.0, 42.0], [2.0, 43.0]])  # batch write
writer.close()

# Write with context manager
with pybcsv.Writer(layout) as writer:
    writer.open("data.bcsv")
    writer.write_row([1.0, 42.0])
# auto-closes on exit

# Read (returns Python lists, no per-column get/set)
reader = pybcsv.Reader()
reader.open("data.bcsv")             # throws RuntimeError on failure
while reader.read_next():
    row = reader.read_row()           # returns Python list [1.0, 42.0]
reader.close()

# Read with iterator protocol
with pybcsv.Reader() as reader:
    reader.open("data.bcsv")
    for row in reader:                # yields Python lists
        print(row)

# Read all at once
reader = pybcsv.Reader()
reader.open("data.bcsv")
all_rows = reader.read_all()          # list of lists
row_count = reader.count_rows()       # instant row count (from file footer)
reader.close()

# CSV text I/O
csv_writer = pybcsv.CsvWriter(layout, ',', '.')  # delimiter, decimal separator
csv_writer.open("data.csv")
csv_writer.write_row([1.0, 42.0])
csv_writer.close()

csv_reader = pybcsv.CsvReader(layout)
csv_reader.open("data.csv")
for row in csv_reader:
    print(row)

# Pandas integration
import pandas as pd
from pybcsv import write_dataframe, read_dataframe
write_dataframe(df, "data.bcsv")
df = read_dataframe("data.bcsv")
```

> **Note:** Python API uses `snake_case` (Pythonic), C++ API uses `lowerCamelCase`.
> Writer and Reader `open()` throw `RuntimeError` on failure (not bool return).
> Row data is passed/returned as plain Python lists — no per-column `get_*`/`set_*` methods.

## Header Sync Mechanism

The Python package needs C++ headers at compile time:
- `sync_headers.py` copies `../include/bcsv/` and `../include/lz4-1.10.0/` into `python/include/`
- `python/include/` is in `.gitignore` — auto-generated at build time
- If you modify C++ headers, reinstall: `cd python && pip install -e .`

## Publishing

```bash
# TestPyPI
python -m build && twine upload --repository testpypi dist/*

# PyPI (release)
python -m build && twine upload dist/*
```

See `python/README.md` for full publishing workflow and CI integration via `.github/workflows/build-and-publish.yml`.

## Binding Source

`python/pybcsv/bindings.cpp` — pybind11 module wrapping:
- `bcsv::Layout` → `pybcsv.Layout` (add_column, column_count, column_name, column_type, has_column, column_index, etc.)
- `bcsv::Writer<Layout>` → `pybcsv.Writer(layout)` — open, write_row(list), write_rows(list_of_lists), close, flush, is_open, context manager
- `bcsv::Reader<Layout>` → `pybcsv.Reader()` — open, read_next, read_row, read_all, close, is_open, count_rows, iterator, context manager
- `bcsv::CsvWriter<Layout>` → `pybcsv.CsvWriter(layout, delimiter, decimal_separator)` — open, write_row, write_rows, close
- `bcsv::CsvReader<Layout>` → `pybcsv.CsvReader(layout)` — open, read_next, iterator, context manager
- `bcsv::ColumnType` enum (all 12 types) + `bcsv::FileFlags` (NONE, ZERO_ORDER_HOLD)
- `bcsv::ColumnDefinition` struct (name, type)
- Row data is passed as Python lists, not via per-column accessors
