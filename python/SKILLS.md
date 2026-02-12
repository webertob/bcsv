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

15 test files in `python/tests/` covering types, errors, interop, pandas, performance.

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

# Write
writer = pybcsv.Writer()
writer.open("data.bcsv", layout)
writer.row().set_double(0, 1.0)
writer.row().set_float(1, 42.0)
writer.write_row()
writer.close()

# Read
reader = pybcsv.Reader()
reader.open("data.bcsv")
while reader.read_next():
    t = reader.row().get_double(0)
    v = reader.row().get_float(1)
reader.close()

# Pandas integration
import pandas as pd
from pybcsv import write_dataframe, read_dataframe
write_dataframe(df, "data.bcsv")
df = read_dataframe("data.bcsv")
```

> **Note:** Python API uses `snake_case` (Pythonic), C++ API uses `lowerCamelCase`.

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
- `bcsv::Layout` → `pybcsv.Layout`
- `bcsv::Writer<Layout>` → `pybcsv.Writer`
- `bcsv::Reader<Layout>` → `pybcsv.Reader`
- `bcsv::Row` → `pybcsv.Row` (via Writer/Reader `.row()` accessor)
- All 12 column types with type-specific get/set methods
