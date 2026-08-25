# BCSV API Overview

This document provides a comprehensive comparison of all BCSV APIs across different programming languages.

---

## API Comparison Matrix

| Feature | C++ API | C API | Python API | C# API |
|---------|---------|-------|------------|--------|
| **Type** | Header-only | Shared library | Pip package | NuGet package |
| **Performance** | Fastest | Fast | Fast (native bindings) | Fast (P/Invoke) |
| **Type Safety** | Compile-time (static) / Runtime (flexible) | Runtime | Runtime | Runtime |
| **Memory Management** | RAII / Manual | Manual | Automatic (GC) | Automatic (GC) |
| **Pandas Integration** | N/A | N/A | ✅ Native | N/A |
| **Best For** | High-performance, embedded systems | C projects, language bindings | Data science, scripting | .NET applications, data pipelines |

---

## C++ API (Native)

**Type:** Header-only C++20 library  
**Documentation:** [README.md](../README.md), [examples/](../examples/)  
**Performance:** Fastest (zero abstraction overhead)

### Key Features

- **Dual interface**: Flexible (runtime) and Static (compile-time)
- **RAII memory management**: Automatic cleanup
- **Modern C++20**: Concepts, constexpr, std::span
- **Zero-copy reads**: Direct buffer access for strings
- **Template metaprogramming**: Compile-time optimization

### Quick Example

```cpp
#include <bcsv/bcsv.h>

// Static interface (compile-time schema)
using Layout = bcsv::LayoutStatic<int32_t, double, std::string>;
auto layout = Layout::create({"id", "value", "name"});

bcsv::Writer<Layout> writer(layout);
writer.open("data.bcsv", /*overwrite=*/true);
writer.row().set<0>(42);
writer.row().set<1>(3.14);
writer.row().set<2>("Alice");
writer.writeRow();
```

### When to Use

- ✅ High-performance applications (millions of rows/sec)
- ✅ Embedded systems (STM32, Zynq, Raspberry Pi)
- ✅ Real-time data acquisition
- ✅ When you need compile-time type safety
- ✅ C++ native projects

---

## C API

**Type:** Shared library (.dll/.so)  
**Documentation:** [include/bcsv/bcsv_c_api.h](../include/bcsv/bcsv_c_api.h)  
**Performance:** Fast (thin wrapper over C++)

### Key Features

- **C89 compatible**: Works with any C compiler
- **Opaque handles**: Encapsulated state management
- **Manual memory management**: Explicit create/destroy
- **Language binding foundation**: Base for Python, C#, etc.
- **Stable ABI**: Binary compatibility across versions

### Quick Example

```c
#include <bcsv/bcsv_c_api.h>

// Create layout
bcsv_layout_t layout = bcsv_layout_create();
bcsv_layout_add_column(layout, 0, "id", BCSV_TYPE_INT32);
bcsv_layout_add_column(layout, 1, "value", BCSV_TYPE_DOUBLE);

// Create writer (flat, ZOH, or delta codec)
bcsv_writer_t writer = bcsv_writer_create(layout);
bcsv_writer_open(writer, "data.bcsv", true, 0, 0, BCSV_FLAG_NONE);

// Write row
bcsv_row_t row = bcsv_writer_row(writer);
bcsv_row_set_int32(row, 0, 42);
bcsv_row_set_double(row, 1, 3.14);
bcsv_writer_next(writer);

// Cleanup
bcsv_writer_destroy(writer);
bcsv_layout_destroy(layout);
```

### When to Use

- ✅ C projects without C++ support
- ✅ Creating language bindings (Python, Ruby, Go, etc.)
- ✅ FFI from other languages
- ✅ When ABI stability is critical
- ✅ Interfacing with legacy C code

### Additional Features

- **Version API**: `bcsv_version()` returns the library version string (unified with file format version since v1.5.0).
- **Writer codecs**: `bcsv_writer_create(layout)` (flat), `bcsv_writer_create_zoh(layout)` (zero-order hold), `bcsv_writer_create_delta(layout)` (delta + VLE encoding).
- **Extended reader**: `bcsv_reader_open_ex(reader, filename, rebuild_footer)` opens with optional footer rebuild; `bcsv_reader_read(reader, index)` provides random-access by row index.
- **CSV reader/writer**: `bcsv_csv_reader_create(layout, delimiter, decimal_sep)` and `bcsv_csv_writer_create(layout, delimiter, decimal_sep)` for reading and writing plain CSV files through the same row API.
- **File flags**: `bcsv_file_flags_t` enum supports `BCSV_FLAG_NONE`, `BCSV_FLAG_ZOH`, `BCSV_FLAG_NO_FILE_INDEX`, `BCSV_FLAG_STREAM_MODE`, `BCSV_FLAG_BATCH_COMPRESS`, `BCSV_FLAG_DELTA_ENCODING`.
- **Error API**: `bcsv_last_error()` returns the thread-local last error string. `bcsv_clear_last_error()` explicitly resets error state. Error state is set on failure and persists until the next failure or explicit clear — always check function return values for success/failure, and consult `bcsv_last_error()` for detail when a function reports failure.

### Row Visitor API

The visitor API iterates over columns in a row without knowing their type at compile time, dispatching each value through a callback:

```c
// Callback signature
void my_visitor(size_t col_index, bcsv_type_t col_type,
                const void* value, void* user_data);

// Visit columns [start_col, start_col + count)
bcsv_row_visit_const(row, 0, bcsv_row_column_count(row),
                     my_visitor, &my_context);
```

- **`bcsv_row_column_count(row)`** returns the number of columns in the row's layout.
- **`bcsv_row_visit_const(row, start, count, cb, user_data)`** invokes `cb` for each column in the range. The `value` pointer points to the native type (`int32_t*`, `double*`, `const char*`, `bool*`, etc.).

### Sampler API (Filter & Project)

The Sampler applies expression-based filtering (conditional) and projection (selection) over a reader, powered by a bytecode VM with sliding-window look-behind/look-ahead:

```c
bcsv_reader_t reader = bcsv_reader_create();
bcsv_reader_open(reader, "data.bcsv");

bcsv_sampler_t sampler = bcsv_sampler_create(reader);

// Filter: only rows where temperature > 25
bcsv_sampler_set_conditional(sampler, "X[0][1] > 25.0");

// Project: timestamp and temperature only
bcsv_sampler_set_selection(sampler, "X[0][0], X[0][1]");

while (bcsv_sampler_next(sampler)) {
    const_bcsv_row_t row = bcsv_sampler_row(sampler);
    double ts   = bcsv_row_get_double(row, 0);
    double temp = bcsv_row_get_double(row, 1);
}

bcsv_sampler_destroy(sampler);
bcsv_reader_close(reader);
bcsv_reader_destroy(reader);
```

**Key functions:**
- `bcsv_sampler_create(reader)` / `bcsv_sampler_destroy(sampler)` — lifecycle
- `bcsv_sampler_set_conditional(sampler, expr)` — compile a filter expression (returns `true` on success)
- `bcsv_sampler_set_selection(sampler, expr)` — compile a projection expression
- `bcsv_sampler_set_mode(sampler, mode)` — set boundary mode (`BCSV_SAMPLER_TRUNCATE` or `BCSV_SAMPLER_EXPAND`)
- `bcsv_sampler_next(sampler)` — advance to next matching row
- `bcsv_sampler_row(sampler)` — access the current output row
- `bcsv_sampler_output_layout(sampler)` — layout of projected columns
- `bcsv_sampler_source_row_pos(sampler)` — position in the source file
- `bcsv_sampler_error_msg(sampler)` — compilation error detail

---

## Python API (PyBCSV)

**Type:** Pip-installable package with native C++ bindings  
**Documentation:** [python/README.md](../python/README.md)  
**Performance:** Fast (native nanobind bindings, minimal overhead)

### Key Features

- **Pandas integration**: Native DataFrame support
- **Pythonic API**: Context managers, iterators, list comprehensions
- **Type hints**: Full typing support
- **NumPy compatible**: Direct array conversion
- **Automatic memory management**: Python GC handles cleanup

### Installation

```bash
pip install pybcsv
```

### Quick Example

```python
import pybcsv

# Create layout
layout = pybcsv.Layout()
layout.add_column("id", pybcsv.ColumnType.INT32)
layout.add_column("value", pybcsv.ColumnType.DOUBLE)
layout.add_column("name", pybcsv.ColumnType.STRING)

# Write data
with pybcsv.Writer(layout) as writer:
    writer.open("data.bcsv", compression_level=6)
    writer.write_row([42, 3.14, "Alice"])
    writer.write_row([43, 2.71, "Bob"])

# Read data
with pybcsv.Reader() as reader:
    reader.open("data.bcsv")
    for row in reader:
        print(f"ID: {row[0]}, Value: {row[1]}, Name: {row[2]}")
```

### Pandas Integration

```python
import pandas as pd
import pybcsv

# Write DataFrame to BCSV
df = pd.DataFrame({
    'id': [1, 2, 3],
    'value': [1.1, 2.2, 3.3],
    'name': ['Alice', 'Bob', 'Charlie']
})
pybcsv.write_dataframe(df, "data.bcsv")

# Read BCSV to DataFrame
df = pybcsv.read_dataframe("data.bcsv")
print(df.head())
```

### When to Use

- ✅ Data science and analysis workflows
- ✅ Pandas/NumPy integration needed
- ✅ Rapid prototyping and scripting
- ✅ Jupyter notebooks
- ✅ Machine learning pipelines

---

## C# API (.NET)

**Type:** NuGet package with P/Invoke to native library  
**Documentation:** [csharp/README.md](../csharp/README.md)  
**Performance:** Fast (minimal marshaling overhead)

### Key Features

- **Full BCSV feature set**: Sequential and random-access I/O, Sampler, CSV interop
- **Columnar bulk I/O**: Read/write entire columns at once with pinned arrays
- **Typed accessors**: `GetInt32()`, `GetDouble()`, `GetString()`, etc.
- **Cross-platform**: Windows x64, Linux x64/ARM64, macOS x64/ARM64
- **Minimal GC pressure**: Efficient P/Invoke with unmanaged memory

### Installation

```bash
dotnet add package Bcsv
```

### Quick Example

```csharp
using Bcsv;

// Define schema
var layout = new BcsvLayout();
layout.AddColumn("timestamp", ColumnType.Double);
layout.AddColumn("temperature", ColumnType.Float);
layout.AddColumn("label", ColumnType.String);

// Write data
using var writer = new BcsvWriter(layout);
writer.Open("data.bcsv");
var row = writer.NewRow();
row.Set(0, 1.0);
row.Set(1, 23.5f);
row.Set(2, "sensor-A");
writer.WriteRow(row);
writer.Close();

// Read data
using var reader = new BcsvReader("data.bcsv");
while (reader.ReadNext())
{
    double ts = reader.Row.GetDouble(0);
    float temp = reader.Row.GetFloat(1);
    string label = reader.Row.GetString(2);
}
```

### When to Use

- ✅ .NET desktop and server applications
- ✅ Data pipelines and ETL workflows
- ✅ Time-series storage in C# projects
- ✅ Cross-platform data exchange
- ✅ High-throughput columnar bulk I/O

---

## Unity Plugin

For Unity game engine integration, BCSV provides a dedicated set of C# bindings optimized for the Unity runtime. See **[unity/README.md](../unity/README.md)** for installation, usage examples, platform support, and troubleshooting.

---

## API Selection Guide

### Choose C++ API when:
- You need maximum performance (millions of rows/sec)
- Building embedded systems or real-time applications
- Using C++ already in your project
- Need compile-time type safety
- Want zero abstraction overhead

### Choose C API when:
- Writing pure C code
- Creating bindings for other languages
- Need stable ABI for plugin systems
- Interfacing with legacy C codebases
- Building language-agnostic tools

### Choose Python API when:
- Working with data science/analysis
- Need Pandas/NumPy integration
- Prototyping or scripting
- Using Jupyter notebooks
- Building machine learning pipelines

### Choose C# API when:
- Building .NET desktop or server applications
- Need high-throughput columnar bulk I/O
- Working with data pipelines in C#
- Want Sampler-based filtering and projection
- Need cross-platform .NET 8/10 support

---

## Performance Comparison

### Write Performance (1M rows, 10 columns)

| API | Time | Throughput | Notes |
|-----|------|------------|-------|
| C++ Static | 150ms | 6.7M rows/sec | Compile-time optimization |
| C++ Flexible | 280ms | 3.6M rows/sec | Runtime schema |
| C API | 300ms | 3.3M rows/sec | Thin wrapper overhead |
| Python (native) | 350ms | 2.9M rows/sec | nanobind bindings |
| C# (Unity) | 450ms | 2.2M rows/sec | P/Invoke marshaling |

### Read Performance (1M rows, 10 columns)

| API | Time | Throughput | Notes |
|-----|------|------------|-------|
| C++ Static | 130ms | 7.7M rows/sec | Zero-copy access |
| C++ Flexible | 200ms | 5.0M rows/sec | Type conversions |
| C API | 220ms | 4.5M rows/sec | Minimal overhead |
| Python (native) | 280ms | 3.6M rows/sec | Native bindings |
| C# (Unity) | 400ms | 2.5M rows/sec | Marshaling cost |

*Benchmarks run on AMD Zen3 CPU, Release build, single-threaded*

---

## File Flags, and the two that are output-only

`FileFlags` is a bit set carried in every file's header, and it is reported by
every binding. Three of its five members are settings a caller chooses; **two are
outputs, and passing them to a writer does nothing.**

| flag | value | settable at open? |
|---|---:|---|
| `NO_FILE_INDEX` | 2 | yes |
| `STREAM_MODE` | 4 | yes |
| `BATCH_COMPRESS` | 8 | yes |
| `ZERO_ORDER_HOLD` | 1 | **no — set from the row codec** |
| `DELTA_ENCODING` | 16 | **no — set from the row codec** |

The row-codec bits describe how the rows in a file were actually encoded, so
they cannot be a request. A writer strips them from whatever the caller passed
and sets them from its own codec, because a header claiming one codec while the
rows were produced by another is a file no reader can trust:

```cpp
// include/bcsv/writer.hpp
const FileFlags safeFlags = (flags & ~ROW_CODEC_FLAGS_MASK)
                          | RowCodecFileFlags<CodecType>::value;
```

That is correct and deliberate. What it is not is visible — so **this compiles,
reads as if it works, and has no effect**:

```csharp
writer.Open(path, flags: FileFlags.ZeroOrderHold | FileFlags.BatchCompress);
```

The codec is chosen where the writer is constructed, and nowhere else:

```cpp
bcsv::Writer<Layout, ZoHCodec> writer(layout);   // C++: a template argument
```
```csharp
new BcsvWriter(layout, "zoh");                   // C#
```
```python
pybcsv.Writer(layout, "zoh")                     # Python
```

Asking for `BATCH_COMPRESS` (8) alone, with three different codecs, gives three
different headers — and none of them is 8:

| row codec | asked | in the file |
|---|---:|---|
| `delta` | 8 | 24 = `BATCH_COMPRESS \| DELTA_ENCODING` |
| `zoh` | 8 | 9 = `ZERO_ORDER_HOLD \| BATCH_COMPRESS` |
| `flat` | 8 | 8 = `BATCH_COMPRESS` |

**Since 1.5.17 a writer will tell you what it actually wrote**, rather than
requiring the file to be closed and reopened to find out:

| API | |
|---|---|
| C++ | `writer.fileFlags()` |
| C | `bcsv_writer_file_flags(writer)` |
| C# / Unity | `writer.FileFlags` |
| Python | `writer.file_flags()` |

The reader has always reported the same thing for a file it has open
(`reader.fileFlags()`, `bcsv_reader_file_flags`, `reader.FileFlags`,
`reader.file_flags()`).

> **Python before 1.5.17 could not express a combination at all.** `FileFlags`
> was bound as an `IntEnum` with hand-written operators returning a bare `int`,
> so `BATCH_COMPRESS | NO_FILE_INDEX` produced `10`, every write function
> rejected it as the wrong type, and `FileFlags(10)` raised `ValueError`. It is
> an `IntFlag` now and combines normally. C# was never affected.

---

## Interoperability

All APIs produce **identical binary format** - files are 100% compatible:

```
C++ Writer → Python Reader ✅
Python Writer → C# Reader ✅
C API Writer → C++ Reader ✅
[Any API] → [Any API] ✅
```

See [INTEROPERABILITY.md](INTEROPERABILITY.md) for cross-language examples and best practices.

---

## Feature Matrix

| Feature | C++ | C | Python | C# |
|---------|-----|---|--------|-----|
| Sequential write | ✅ | ✅ | ✅ | ✅ |
| Sequential read | ✅ | ✅ | ✅ | ✅ |
| Random access | ✅ | ✅ | ✅ | ✅ |
| Compression (LZ4) | ✅ | ✅ | ✅ | ✅ |
| Zero-Order Hold | ✅ | ✅ | ✅ | ✅ |
| Delta encoding | ✅ | ✅ | ✅ | ✅ |
| CSV read/write | ✅ | ✅ | ✅ | ✅ |
| Checksums (xxHash64) | ✅ | ✅ | ✅ | ✅ |
| Crash recovery | ✅ | ✅ | ✅ | ✅ |
| Sampler (filter/project) | ✅ | ✅ | ✅ | ✅ |
| Row visitor | ✅ | ✅ | ❌ | ❌ |
| Static typing | ✅ | ❌ | ❌ | ❌ |
| Columnar bulk I/O | ❌ | ✅ | ✅ | ✅ |
| Pandas integration | N/A | N/A | ✅ | N/A |
| Polars integration | N/A | N/A | ✅ | N/A |
| Header-only | ✅ | ❌ | ❌ | ❌ |

**Reading this table.** All five distribution channels ship the *same version
number* — it is also the file-format version (see [VERSIONING.md](../VERSIONING.md)).
Version parity is not feature parity: this table is the record of what each
binding actually exposes at that version. A `❌` here means "not surfaced in this
language", not "the format cannot do it".

Notes on the current gaps:

- **Columnar bulk I/O** is implemented in the C API layer and surfaced by C,
  Python (`read_columns` / `write_columns`) and C#. The C++ core has no
  first-class API for it yet — see backlog item 23.a.
- **Row visitor** is a C++/C construct; Python and C# read rows through typed
  accessors instead.
- **Parquet conversion and null policies** are Python-only
  (`parquet2bcsv` / `bcsv2parquet`). Files they produce are ordinary BCSV and are
  readable from every binding.
- **File-level metadata** rides in a `<file>.bcsv.meta.json` companion, written by
  Python and readable from Python (`read_metadata_json`) and C#/Unity
  (`BcsvMetadata.ReadCompanion`). C and C++ have no reader for it. The companion
  records a SHA-256 of the BCSV file as its identity check; verifying it costs a
  full read of that file, so `ReadCompanion(path, expectedRows, verifyDigest:
  false)` keeps only the cheap `bcsv_bytes` / `bcsv_rows` pre-checks for callers
  who open a large recording through random access. Verify once at ingest, skip
  it per open. All of this is a stopgap: the format gains an in-format metadata
  section in 1.6.0, after which `BcsvMetadata` is deleted — see item E12 in
  `ToDo.md`. Do not build long-lived code against it.

---

## Getting Help

- **C++ API**: [examples/](../examples/), [tests/](../tests/)
- **C API**: [include/bcsv/bcsv_c_api.h](../include/bcsv/bcsv_c_api.h)
- **Python API**: [python/README.md](../python/README.md), [python/examples/](../python/examples/)
- **C# API**: [csharp/README.md](../csharp/README.md)
- **Unity Plugin**: [unity/README.md](../unity/README.md)
- **Issues**: [GitHub Issues](https://github.com/webertob/bcsv/issues)
