# BCSV Project — AI Skills Reference

> Master quick-reference for AI agents to onboard, build, test, and contribute to the BCSV library.
> For humans, see also: README.md and ARCHITECTURE.md

## Project Mission

Binary-CSV (BCSV) combines the ease of CSV files with the speed and size efficiency of a binary format.
Optimized for **large time-series data** on platforms from STM32 to desktop.

**Key design principles:**
- Files larger than RAM — streaming row-by-row read/write, never load entire file
- Constant-time per-row I/O — suitable for real-time recording
- Schema defined in code (C++/Python/C#) — no external schema files (unlike FlatBuffers/ProtoBuf)
- Header is mandatory, stores column names + types — validate once, trust every row
- Crash resilient — retrieve last fully written row even after interrupted writes
- Time-series optimized — exploits ZoH (Zero-Order-Hold) and delta encoding for compression

**Performance targets:**
| Platform | Target |
|----------|--------|
| STM32F4, 1000 ch | 1 KHz sequential recording |
| STM32F7, Zynq7000, RPi-nano | 10 KHz sequential recording |
| Desktop Zen3, 1000 ch | ≥ 1 million rows/sec |
| File size vs CSV, 1000 ch | < 30% |
| File growth (idle, 1000 ch, 10 KHz) | < 1 KB/s |

**Target users:** Anyone running metrology tasks with digital tools.

**Language support:** C++ (primary), C API, Python (pybind11), C# / Unity (P/Invoke)

## Repository Structure

```
bcsv/
├── include/bcsv/          # Header-only C++ library (28 files: .h declarations, .hpp implementations)
├── examples/              # CLI tools + usage examples (11 build targets)
├── tests/                 # Google Test suite + benchmark executables
├── benchmark/             # Python orchestrator, report generator, regression detector
├── python/                # Python bindings (pybind11) + pandas integration
├── unity/                 # C# / Unity bindings (P/Invoke via bcsv_c_api)
├── docs/                  # API overview, error handling, interoperability docs
├── cmake/                 # Git-based versioning scripts
├── .github/workflows/     # CI: benchmark, release, Python wheel publishing
├── ARCHITECTURE.md        # Design philosophy, file format spec, roadmap
├── ToDo.txt               # Active task list with priorities and status
├── VERSIONING.md          # Git-tag-based versioning system
└── tmp/                   # Temporary experiments (gitignored)
```

### Temporary Files Policy

- Use `tmp/` under the project root for temporary scripts, scratch outputs, and ad-hoc experiments.
- Do not place temporary artifacts in the repository root or tracked source folders.
- `tmp/` is gitignored by design, so temporary files there do not pollute version management.

## Build Skill

### Prerequisites
- CMake ≥ 3.28, Ninja, C++20 compiler (GCC 12+ / Clang 15+ / MSVC 2022)
- Dependencies (bundled): xxHash 0.8.3, LZ4 1.10.0 — no external installs needed

### Configure & Build

```bash
# Debug build (Ninja preset)
cmake --preset ninja-debug
cmake --build --preset ninja-debug-build -j$(nproc)

# Release build (Ninja preset)
cmake --preset ninja-release
cmake --build --preset ninja-release-build -j$(nproc)

# Release benchmark targets
cmake --build --preset ninja-release-build -j$(nproc) --target bench_macro_datasets bench_micro_types
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_EXAMPLES` | ON | Build CLI tools + example programs |
| `BUILD_TESTS` | ON | Build Google Test suite + C API tests |
| `BCSV_ENABLE_BENCHMARKS` | ON | Macro benchmarks + CSV generator |
| `BCSV_ENABLE_MICRO_BENCHMARKS` | ON | Google Benchmark per-type latency |
| `BCSV_ENABLE_EXTERNAL_CSV_BENCH` | OFF | External csv-parser comparison (fetches ~30 MB) |
| `BCSV_ENABLE_STRESS_TESTS` | OFF | Time-consuming LZ4 stress tests |
| `BCSV_ENABLE_LEGACY_BENCHMARKS` | OFF | Deprecated benchmark_large / benchmark_performance |

### CMake Presets

6 presets in `CMakePresets.json`: `gcc-debug`, `gcc-release`, `msvc-debug`, `msvc-release`, `ninja-debug`, `ninja-release`.

### Build Output

Executables land in `build/ninja-debug/bin/` (debug) or `build/ninja-release/bin/` (release):
- Tests: `bcsv_gtest`, `test_row_api`, `test_c_api`
- CLI tools: `csv2bcsv`, `bcsv2csv`, `bcsvHead`, `bcsvTail`, `bcsvHeader`
- Examples: `example`, `example_static`, `example_zoh`, `example_zoh_static`, `visitor_examples`, `c_api_vectorized_example`
- Benchmarks: `bench_macro_datasets`, `bench_micro_types`, `bench_generate_csv`, `bench_external_csv`

### Quick Verification Commands

```bash
# Build + test (debug)
cmake --preset ninja-debug && cmake --build --preset ninja-debug-build -j$(nproc)
./build/ninja-debug/bin/bcsv_gtest

# Build + test (release)
cmake --preset ninja-release && cmake --build --preset ninja-release-build -j$(nproc)
./build/ninja-release/bin/bcsv_gtest

# Quick benchmark smoke test
python3 benchmark/run_benchmarks.py --type=MICRO --no-report
```

## Public API Classes

Users interact with these types (declared in `include/bcsv/`):

| Class | File | Role |
|-------|------|------|
| `Layout` | layout.h | Dynamic column schema (names, types). Observer pattern syncs attached Rows. |
| `LayoutStatic<Types...>` | layout.h | Compile-time fixed schema, variadic template. |
| `Row` | row.h | In-memory row for read/write. Alias for `RowImpl<TrackingPolicy::Disabled>`. |
| `RowTracked<Policy>` | row.h | Policy-based row alias for advanced use (e.g., `TrackingPolicy::Enabled` with ZoH codec). |
| `RowView` | row.h | Zero-copy read-only view into serialized row buffer (dynamic layout). |
| `RowStatic<Types...>` | row.h | Compile-time typed row (tracking disabled). |
| `RowStaticTracked<Policy, Types...>` | row.h | Policy-based static row alias for advanced use. |
| `RowViewStatic<Types...>` | row.h | Zero-copy view for static layouts. |
| `Reader<LayoutType, Policy>` | reader.h | Stream-based BCSV file reader with LZ4 decompression. |
| `Writer<LayoutType, Policy>` | writer.h | Stream-based BCSV file writer with LZ4 compression, optional ZoH. |
| `RowCodecFlat001<Layout, Policy>` | row_codec_flat001.h | Dense flat row codec — serialize, deserialize, zero-copy column access. |
| `RowCodecZoH001<Layout, Policy>` | row_codec_zoh001.h | Zero-Order-Hold codec — delta-encodes unchanged columns. |

> **Note:** public row change-tracking methods were removed. Tracking is internal-only and consumed by codecs (especially ZoH).

### Core workflow pattern

```cpp
bcsv::Layout layout;
layout.addColumn("time", bcsv::ColumnType::DOUBLE);
layout.addColumn("value", bcsv::ColumnType::FLOAT);

// Write
bcsv::Writer<bcsv::Layout> writer;
writer.open("data.bcsv", layout);
writer.row().set<double>(0, 1.0);
writer.row().set<float>(1, 42.0f);
writer.writeRow();
writer.close();

// Read
bcsv::Reader<bcsv::Layout> reader;
reader.open("data.bcsv");
while (reader.readNext()) {
    double t = reader.row().get<double>(0);
    float  v = reader.row().get<float>(1);
}
reader.close();
```

## Source File Inventory (include/bcsv/)

### Declarations (.h) — concise, human-readable

| File | Purpose |
|------|---------|
| `bcsv.h` | Main include — aggregates all headers into single include |
| `definitions.h` | Core types: `ColumnType` enum, `FileFlags`, `ValueType`, magic bytes, limits |
| `layout.h` | `Layout`, `LayoutStatic<>`, `ColumnDefinition`, observer pattern |
| `row.h` | `RowImpl<>`, `RowView`, `RowStaticImpl<>`, `RowViewStatic<>` — binary format docs |
| `row_visitors.h` | C++20 concepts for visitor pattern: `ConstRowVisitor`, `MutableRowVisitor` |
| `reader.h` | `Reader<>` — streaming file reader with LZ4 decompression |
| `writer.h` | `Writer<>` — streaming file writer with LZ4 compression |
| `row_codec_flat001.h` | `RowCodecFlat001<>` — dense flat row codec (serialize, deserialize, column access) |
| `row_codec_zoh001.h` | `RowCodecZoH001<>` — ZoH delta codec (composes Flat001 for first row) |
| `row_codec_detail.h` | Shared codec helpers: `computeWireMetadata()`, `readColumnFromWire()` |
| `row_codec_variant.h` | `RowCodecType<>` — compile-time codec selection for Writer |
| `row_codec_dispatch.h` | `CodecDispatch<>` — runtime codec selection for Reader (union + function pointers) |
| `file_header.h` | `FileHeader` — 12-byte fixed header + variable schema section |
| `packet_header.h` | `PacketHeader` — 16-byte per-packet header (magic, row index, checksum) |
| `file_footer.h` | `FileFooter`, `PacketIndexEntry` — EOF index for random access |
| `bitset.h` | `Bitset<N>` (fixed) / `Bitset<>` (dynamic, SOO) — change tracking + bool storage |
| `byte_buffer.h` | `LazyAllocator<T>`, `ByteBuffer` — no-init byte vector |
| `string_addr.h` | `StrAddrT<>` — packed string offset+length in single integer |
| `column_name_index.h` | `ColumnNameIndex<>` — flat-map for column name → index lookup |
| `bcsv_c_api.h` | C API surface — opaque handles, `extern "C"` functions |
| `checksum.hpp` | `Checksum` / `Checksum::Streaming` — xxHash64 wrapper |

### Implementations (.hpp) — complex logic, detailed documentation

| File | Purpose |
|------|---------|
| `bcsv.hpp` | Stream type traits (`is_fstream`, `has_open_method`, etc.) |
| `layout.hpp` | Offset computation, observer callbacks, bool/tracked mask management |
| `row.hpp` | `get<T>()`/`set<T>()`/`visit()`, RowView access — ~3300 lines |
| `reader.hpp` | `open()`, `close()`, `readNext()`, packet handling, codec dispatch |
| `writer.hpp` | `open()`, `close()`, `writeRow()`, packet management, codec dispatch |
| `row_codec_flat001.hpp` | Flat001 codec implementation: serialize, deserialize, column access |
| `row_codec_zoh001.hpp` | ZoH001 codec implementation: delta serialize/deserialize |
| `file_header.hpp` | Header read/write/validation |
| `bitset.hpp` | Full bitset implementation (~1800 lines), SOO, shift, slice views |
| `column_name_index.hpp` | Name parsing, sorted insert, binary search |
| `vle.hpp` | Variable-Length Encoding — zigzag for signed ints, VLE encode/decode |
| `lz4_stream.hpp` | Streaming LZ4 compressor/decompressor with ring-buffer dictionary |
| `bcsv_c_api.cpp` | C API implementation wrapping C++ classes |

## Naming Conventions (Enforced)

| Element | Convention | Example |
|---------|-----------|---------|
| Private/protected member variables | `snake_case_` (trailing underscore) | `file_path_`, `row_count_`, `data_` |
| Public struct/class member variables | `snake_case` (no trailing underscore) | `version_major`, `byte_offset`, `name` |
| Local variables | `snake_case` | `column_count`, `row_buffer` |
| Functions / methods | `lowerCamelCase` | `addColumn()`, `getErrorMsg()`, `readNext()` |
| Classes / structs | `UpperCamelCase` | `Layout`, `FileHeader`, `PacketHeader` |
| Constants (`constexpr`, `static const`, enum values) | `CAPITAL_SNAKE_CASE` | `MAX_COLUMNS`, `MAGIC_BYTES`, `ZERO_ORDER_HOLD` |
| Template parameters | `UpperCamelCase` | `Policy`, `LayoutType`, `Types...` |
| C API functions | `bcsv_snake_case` | `bcsv_layout_create()`, `bcsv_reader_open()` |

### File organization

- `.h` files: concise declarations, human-readable, minimal implementation
- `.hpp` files: complex implementations, detailed documentation as needed
- Header guard: `#pragma once`
- Include order: standard library (`<>`) first, then project headers (`""`)

## Error Handling Philosophy

- **I/O operations** (open, read, write, close): return `bool` — check `getErrorMsg()` for details
- **Logic errors** (type mismatch, out-of-range): `throw` exceptions
- **Debug output**: controlled by `DEBUG_OUTPUTS` constexpr in definitions.h (sends to `std::cerr`)
- **Pattern**: no error codes — bool success + string message for I/O, exceptions for programmer errors

## Key Design Patterns

- **Header-only library**: all C++ code in `include/bcsv/`, `bcsv` is an INTERFACE CMake target
- **Observer pattern**: `Layout` notifies attached `Row` objects of schema changes (`onAddColumn`, `onChangeColumnType`, `onRemoveColumn`)
- **Policy-based design**: `TrackingPolicy::Enabled/Disabled` as template parameter for ZoH change tracking (compile-time decision, no runtime branching)
- **CRTP + static dispatch**: `LayoutStatic<Types...>` for zero-overhead compile-time schemas
- **Three-container Row storage**: `bits_` (Bitset<>), `data_` (vector<byte>), `strg_` (vector<string>) — booleans as bits, scalars packed, strings separate
- **Codec extraction**: Wire-format serialization/deserialization lives in `RowCodecFlat001`/`RowCodecZoH001`, not in Row classes. Writer uses compile-time `RowCodecType` (`std::conditional_t`); Reader uses runtime `CodecDispatch` (union + function pointers, resolved at file open). TrackingPolicy and file codec are orthogonal axes — all 4 combinations work. Codecs access Row internals via `friend`.

## Current Status

- **Version**: v1.1.2 (generated from Git tags)
- **Active work**: see ToDo.txt (items 10-14 are next: wire format, serializer layer, delta encoding)
- **Test suite**: 404 tests passing (Google Test), 76 C API tests, row API tests
- **Benchmark suite**: 11 dataset profiles, macro + micro benchmarks, Python orchestrator

## Subsystem Skill Files

| Subsystem | Skill File | Covers |
|-----------|-----------|---------|
| Tests | tests/SKILLS.md | Build, run, filter tests; test files inventory; sanitizers |
| Benchmarks | benchmark/README.md | Build, run, interpret benchmarks; streamlined benchmark CLI and reporting |
| Examples & CLI | examples/SKILLS.md | CLI tools, example programs, known caveats |
| Python bindings | python/SKILLS.md | Build, test, publish Python package |
| Unity / C# | unity/SKILLS.md | Architecture, prerequisites, key files |

## Lean Architecture Gate

Use `docs/LEAN_CHECKLIST.md` for every non-trivial item (especially wire format, serialization, Reader/Writer, and API changes).

Recommended cadence:
- **Before coding:** run sections A-C to validate scope and ownership
- **Before merge:** run sections D-H and add the summary template to commit/PR notes

This keeps complexity proportional to requested scope and prevents drift in ownership/layering.

## AI Agent Priming

**Copy-paste this block at the start of a new AI session:**

```
I'm working on the BCSV library (Binary CSV for time-series data).
Read these files to onboard:
1. SKILLS.md — project overview, build commands, naming conventions, API structure
2. The SKILLS.md in the subsystem I'll be working on (tests/, benchmark/, examples/, python/, unity/)
3. ARCHITECTURE.md — design philosophy, file format spec, roadmap (if needed for design decisions)
4. ToDo.txt — active task list with status and priorities

Key source entry points by area:
- Types & constants: include/bcsv/definitions.h
- Column schema: include/bcsv/layout.h
- Row data model: include/bcsv/row.h (RowImpl is internal — users see Row, RowView, RowStatic, RowViewStatic)
- Row codecs: include/bcsv/row_codec_flat001.h, include/bcsv/row_codec_zoh001.h
- File I/O: include/bcsv/reader.h, include/bcsv/writer.h
- Visitor pattern: include/bcsv/row_visitors.h
- VLE encoding: include/bcsv/vle.hpp
- Bitset: include/bcsv/bitset.h

Build & verify: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc) && ./build/bin/bcsv_gtest
```

**Task-specific additions** (append to the block above):
- Working on Reader/Writer: "Also read include/bcsv/reader.hpp and writer.hpp for implementation details"
- Working on benchmarks: "Also read benchmark/README.md and tests/bench_common.hpp"
- Working on Python: "Also read python/SKILLS.md and python/pybcsv/bindings.cpp"
- Working on CLI tools: "Also read examples/SKILLS.md and examples/CLI_TOOLS.md"
- Working on file format: "Also read include/bcsv/file_header.h, packet_header.h, file_footer.h"
- Working on bitset: "Also read include/bcsv/bitset.h and bitset.hpp"
