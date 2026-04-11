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

**Language support:** C++ (primary), C API, Python (nanobind), C# / Unity (P/Invoke)

## Repository Structure

```
bcsv/
├── include/bcsv/          # Header-only C++ library (.h declarations, .hpp implementations)
│   ├── sampler/           # Sampler API headers
│   ├── codec_file/        # File codec headers
│   └── codec_row/         # Row codec headers
├── src/tools/             # CLI tools (9 targets: csv2bcsv, bcsv2csv, bcsvHead, bcsvTail, bcsvHeader, bcsvSampler, bcsvGenerator, bcsvValidate, bcsvRepair)
├── examples/              # Usage examples (7 targets)
├── tests/                 # Google Test suite + benchmark executables
├── benchmark/             # Python orchestrator, report generator, regression detector
├── python/                # Python bindings (nanobind) + pandas integration
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
- CMake ≥ 3.28, Ninja, C++20 compiler with full library support:
  - GCC 13+ (Linux), Clang 16+ with libstdc++ 13+ (Linux), MSVC 2022 17.4+ (Windows), Xcode 15.4+ (macOS)
  - Arm GNU Toolchain 13.2+ / STM32CubeIDE 1.14+ (STM32 with Linux)
  - Vitis / PetaLinux 2024.1+ (AMD/Xilinx with Linux)
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
| `BUILD_EXAMPLES` | ON | Build example programs |
| `BUILD_TOOLS` | ON | Build CLI tools (csv2bcsv, bcsv2csv, bcsvHead, bcsvTail, bcsvHeader, bcsvSampler, bcsvGenerator, bcsvValidate) |
| `BUILD_TESTS` | ON | Build Google Test suite + C API tests |
| `BCSV_ENABLE_BATCH_CODEC` | ON | Enable batch-LZ4 file codec (requires threading) |
| `BCSV_BUILD_BENCHMARKS` | ON | Macro benchmarks + CSV generator |
| `BCSV_BUILD_MICRO_BENCHMARKS` | ON | Google Benchmark per-type latency |
| `BCSV_ENABLE_EXTERNAL_CSV_BENCH` | OFF | External csv-parser comparison (fetches ~30 MB) |
| `BCSV_ENABLE_STRESS_TESTS` | OFF | Time-consuming LZ4 stress tests |

### CMake Presets

7 presets in `CMakePresets.json`: `gcc-debug`, `gcc-release`, `msvc-debug`, `msvc-release`, `ninja-debug`, `ninja-release`, `ninja-release-min`.

### Build Output

Executables land in `build/ninja-debug/bin/` (debug) or `build/ninja-release/bin/` (release):
- Tests: `bcsv_gtest`, `test_row_api`, `test_c_api`
- CLI tools: `csv2bcsv`, `bcsv2csv`, `bcsvHead`, `bcsvTail`, `bcsvHeader`, `bcsvSampler`, `bcsvGenerator`, `bcsvValidate`
- Examples: `example`, `example_static`, `example_zoh`, `example_zoh_static`, `visitor_examples`, `c_api_vectorized_example`, `example_sampler`
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
python3 benchmark/run.py wip --type=MICRO --no-report
```

## Public API Classes

Users interact with these types (declared in `include/bcsv/`):

| Class | File | Role |
|-------|------|------|
| `Layout` | layout.h | Dynamic column schema (names, types). Observer pattern syncs attached Rows. |
| `LayoutStatic<Types...>` | layout.h | Compile-time fixed schema, variadic template. |
| `Row` | row.h | In-memory row for read/write (dynamic layout). |
| `RowStatic<Types...>` | row.h | Compile-time typed row. |
| `Reader<LayoutType>` | reader.h | Stream-based BCSV file reader with LZ4 decompression. |
| `ReaderDirectAccess<LayoutType>` | reader.h | Random-access reader with O(log N) seek via FileFooter. |
| `Writer<LayoutType, RowCodec>` | writer.h | Stream-based BCSV file writer with LZ4 compression. |
| `WriterFlat<LayoutType>` | writer.h | Alias for `Writer<LayoutType, RowCodecFlat001<LayoutType>>`. |
| `WriterZoH<LayoutType>` | writer.h | Alias for `Writer<LayoutType, RowCodecZoH001<LayoutType>>`. |
| `WriterDelta<LayoutType>` | writer.h | Alias for `Writer<LayoutType, RowCodecDelta002<LayoutType>>`. |
| `RowCodecFlat001<LayoutType>` | codec_row/row_codec_flat001.h | Dense flat row codec — serialize, deserialize. |
| `RowCodecZoH001<LayoutType>` | codec_row/row_codec_zoh001.h | Zero-Order-Hold codec — delta-encodes unchanged columns. |
| `RowCodecDelta002<LayoutType>` | codec_row/row_codec_delta002.h | Delta + VLE encoding — type-grouped loops, FoC/ZoH per column. |
| `Sampler<Layout>` | sampler/sampler.h | Bytecode VM for row filtering and column projection. |

> **Note:** Change tracking is internal to codecs (ZoH001, Delta002). There is no public tracking API.

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
| `row.h` | `RowImpl<>`, `RowStaticImpl<>` — binary format docs |
| `row_visitors.h` | C++20 concepts for visitor pattern: `ConstRowVisitor`, `MutableRowVisitor` |
| `reader.h` | `Reader<>` — streaming file reader with LZ4 decompression |
| `writer.h` | `Writer<>` — streaming file writer with LZ4 compression |
| `row_codec_flat001.h` | `RowCodecFlat001<>` — dense flat row codec (serialize, deserialize, column access) |
| `row_codec_zoh001.h` | `RowCodecZoH001<>` — ZoH delta codec (composes Flat001 for first row) |
| `row_codec_variant.h` | `RowCodecType<>` — compile-time codec selection for Writer |
| `row_codec_dispatch.h` | `CodecDispatch<>` — runtime codec selection for Reader (union + function pointers) |
| `layout_guard.h` | `LayoutGuard` — RAII structural lock, prevents layout mutation while a codec is active |
| `file_header.h` | `FileHeader` — 12-byte fixed header + variable schema section |
| `packet_header.h` | `PacketHeader` — 16-byte per-packet header (magic, row index, checksum) |
| `file_footer.h` | `FileFooter`, `PacketIndexEntry` — EOF index for random access |
| `bitset.h` | `Bitset<N>` (fixed) / `Bitset<>` (dynamic, SOO) — change tracking + bool storage |
| `byte_buffer.h` | `LazyAllocator<T>`, `ByteBuffer` — no-init byte vector |
| `column_name_index.h` | `ColumnNameIndex<>` — flat-map for column name → index lookup |
| `bcsv_c_api.h` | C API surface — opaque handles, `extern "C"` functions |
| `checksum.hpp` | `Checksum` / `Checksum::Streaming` — xxHash64 wrapper |

Note: Row codec headers are in `codec_row/` and file codec headers are in `codec_file/` subdirectories.
The table above uses short names; actual paths are `include/bcsv/codec_row/row_codec_*.h` etc.

### Implementations (.hpp) — complex logic, detailed documentation

| File | Purpose |
|------|---------|
| `bcsv.hpp` | Stream type traits (`is_fstream`, `has_open_method`, etc.) |
| `layout.hpp` | Offset computation, observer callbacks, bool/tracked mask management |
| `row.hpp` | `get<T>()`/`set<T>()`/`visit()` — ~1750 lines |
| `reader.hpp` | `open()`, `close()`, `readNext()`, packet handling, codec dispatch |
| `writer.hpp` | `open()`, `close()`, `writeRow()`, packet management, codec dispatch |
| `row_codec_flat001.hpp` | Flat001 codec implementation: serialize, deserialize |
| `row_codec_zoh001.hpp` | ZoH001 codec implementation: delta serialize/deserialize |
| `row_codec_delta002.hpp` | Delta002 codec: type-grouped loops, FoC/ZoH per column, float XOR + VLE |
| `file_header.hpp` | Header read/write/validation |
| `bitset.hpp` | Full bitset implementation (~1800 lines), SOO, shift, slice views |
| `column_name_index.hpp` | Name parsing, sorted insert, binary search |
| `vle.hpp` | Variable-Length Encoding — zigzag for signed ints, VLE encode/decode |
| `lz4_stream.hpp` | Streaming LZ4 compressor/decompressor with ring-buffer dictionary |
| `bcsv_c_api.cpp` | C API implementation wrapping C++ classes (in `src/`) |

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
- **Codec-based design**: Row encoding (Flat001/ZoH001/Delta002) and file codec (stream/packet/packet-batched/LZ4 variants) are orthogonal axes selected via template parameters
- **CRTP + static dispatch**: `LayoutStatic<Types...>` for zero-overhead compile-time schemas
- **Three-container Row storage**: `bits_` (Bitset<>), `data_` (vector<byte>), `strg_` (vector<string>) — booleans as bits, scalars packed, strings separate
- **Codec extraction**: Wire-format serialization/deserialization lives in `RowCodecFlat001`/`RowCodecZoH001`/`RowCodecDelta002`, not in Row classes. Writer uses compile-time `RowCodecType` template parameter; Reader uses runtime `CodecDispatch` (union + function pointers, resolved at file open). Row codec and file codec are orthogonal axes. Codecs access Row internals via `friend`.
- **LayoutGuard (RAII structural lock)**: Codecs acquire a `LayoutGuard` during `setup()`. While held, structural layout mutations throw `std::logic_error`. Automatically released on codec destruction or move-assignment.

## Current Status

- **Version**: v1.3.0 (file format v1.4.0)
- **Active work**: see ToDo.txt for current priorities
- **Test suite**: 694 Google Test cases (26 .cpp files), C API tests (3 .c files), shell-based CLI tests
- **Benchmark suite**: 14 dataset profiles, macro + micro benchmarks, Python orchestrator

## Subsystem Quick Reference

### Tests (`tests/`)

```bash
# Build + run all GTests
cmake --build --preset ninja-debug-build -j$(nproc) && ./build/ninja-debug/bin/bcsv_gtest

# Filter specific suite
./build/ninja-debug/bin/bcsv_gtest --gtest_filter="ComprehensiveTest.*"

# Run C API tests
./build/ninja-debug/test_c_api && ./build/ninja-debug/test_row_api

# Run shell integration tests via CTest
ctest --test-dir build/ninja-debug -R "integration" --output-on-failure

# AddressSanitizer build
cmake -S . -B build_asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build_asan -j$(nproc) && ./build_asan/bin/bcsv_gtest
```

Test targets: `bcsv_gtest` (25 .cpp), `test_c_api`, `test_row_api`, `test_c_api_full`.
Pytest integration: `tests/integration/test_cli_tools.py`, `test_sampler.py`, `test_validate.py` (CTest-registered, 300s timeout).
Deep reference: `tests/README.md`.

### Benchmarks (`benchmark/`)

```bash
# Quick micro benchmark
python3 benchmark/run.py wip --type=MICRO --no-report

# Macro small (15 profiles, ~30s)
python3 benchmark/run.py wip --type=MACRO-SMALL

# Full campaign
python3 benchmark/run.py wip --type=MICRO,MACRO-SMALL,MACRO-LARGE

# Direct CLI
./build/ninja-release/bin/bench_macro_datasets --size=S --summary=compact

# Compare runs
python3 benchmark/report.py <candidate_dir> --baseline <baseline_dir>

# Codec parity investigation (Flexible vs Static, all file codecs × row codecs)
./build/ninja-release/bin/bench_codec_compare \
    --rows=50000 --iterations=5 --profile=measurement_campaign \
    --storage=both --json=tmp/parity_investigation.json
```

Targets: `bench_macro_datasets`, `bench_micro_types`, `bench_micro_bitset`, `bench_direct_access`, `bench_c_api`, `bench_sampler`, `bench_codec_compare`.
Deep reference: `benchmark/README.md`, `benchmark/REFERENCE_WORKLOADS.md`.

### CLI Tools (`src/tools/`)

```bash
# Build all 9 tools
cmake --build --preset ninja-release-build -j$(nproc)

# Core pipeline
csv2bcsv data.csv data.bcsv
bcsv2csv data.bcsv output.csv
bcsvHeader data.bcsv
bcsvHead -n 5 data.bcsv
bcsvTail -n 5 data.bcsv
bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv
bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv
bcsvValidate -i data.bcsv
bcsvRepair -i broken.bcsv --dry-run --json
```

Targets: `csv2bcsv`, `bcsv2csv`, `bcsvHead`, `bcsvTail`, `bcsvHeader`, `bcsvSampler`, `bcsvGenerator`, `bcsvValidate`, `bcsvRepair`.
Deep reference: `src/tools/CLI_TOOLS.md`.

### Examples (`examples/`)

```bash
cmake --build --preset ninja-debug-build --target example -j$(nproc)
./build/ninja-debug/bin/example
```

Targets: `example`, `example_static`, `example_zoh`, `example_zoh_static`, `visitor_examples`, `c_api_vectorized_example`, `example_sampler`.
Deep reference: `examples/README.md`.

### Python (`python/`)

```bash
cd python && pip install -e .       # Editable install (syncs headers, compiles nanobind)
pytest tests/ -v                    # Run all tests
python -m build && twine upload dist/*  # Publish
```

Key API: `pybcsv.Layout`, `pybcsv.Writer`, `pybcsv.Reader`, `pybcsv.CsvWriter`, `pybcsv.CsvReader`, `pybcsv.write_dataframe()`, `pybcsv.read_dataframe()`.
Deep reference: `python/README.md`.

### Unity / C# (`unity/`)

Architecture: `C# → P/Invoke → bcsv_c_api.dll/.so → C++ bcsv`.
Build: `cmake --build build --target bcsv_c_api -j$(nproc)` → copy to `Assets/Plugins/`.
Key files: `Scripts/BcsvNative.cs` (P/Invoke), `BcsvLayout.cs`, `BcsvReader.cs`, `BcsvWriter.cs`, `BcsvRow.cs`.
Deep reference: `unity/README.md`, `unity/OWNERSHIP_SEMANTICS.md`.

### C# / NuGet (`csharp/`)

Architecture: `C# → P/Invoke → libbcsv_c_api.so/.dll/.dylib → C++ bcsv`.
NuGet package: `Bcsv` — multi-target net8.0 + net10.0, native binaries for 5 RIDs (win-x64, linux-x64, linux-arm64, osx-x64, osx-arm64).
Key files: `csharp/src/Bcsv/NativeMethods.cs` (P/Invoke), `BcsvReader.cs`, `BcsvWriter.cs`, `BcsvColumns.cs`, `BcsvSampler.cs`, `BcsvRow.cs`.
CI/CD: `.github/workflows/csharp-nuget.yml` — builds native libs per platform, packs .nupkg, tests, publishes to NuGet.org + GitHub Packages on version tags.
Deep reference: `csharp/README.md`.

## Lean Architecture Gate

Use `docs/LEAN_CHECKLIST.md` for every non-trivial item (especially wire format, serialization, Reader/Writer, and API changes).

Recommended cadence:
- **Before coding:** run sections A-C to validate scope and ownership
- **Before merge:** run sections D-H and add the summary template to commit/PR notes

## Adding a New Codec (Version-Gated Registry)

When a new minor version introduces a new row or file codec, follow this recipe.
A `static_assert` in `definitions.h` will break the build if you add an enum
value without completing these steps.

### Row Codec (e.g., adding DELTA003 in v1.7)

1. **Create codec files**: `include/bcsv/codec_row/row_codec_delta003.h` and `.hpp`.
   Keep `row_codec_delta002.h/.hpp` unchanged — old codec must stay for backward compat.
2. **Add enum value** in `definitions.h`: Add `DELTA003` to `RowCodecId` and bump `ROW_CODEC_COUNT`.
3. **Add version threshold** in `resolveRowCodecId()` (definitions.h):
   ```cpp
   if ((flags & FileFlags::DELTA_ENCODING) != FileFlags::NONE)
       return (fileMinor >= 7) ? RowCodecId::DELTA003 : RowCodecId::DELTA002;
   ```
4. **Add dispatch case** in `RowCodecDispatch::setup()` (row_codec_dispatch.h):
   new `case RowCodecId::DELTA003:` block with trampoline functions.
5. **Bump version** via git tag: `git tag v1.7.0`.
6. **Update** the version→codec table in `VERSIONING.md §Codec Registry`.
7. **Add tests**: cross-version test that writes with v1.7 codec, patches header
   to v1.6 minor, and verifies the old codec is selected.

### File Codec (e.g., adding PACKET_LZ4_BATCH_002 in v1.8)

Same pattern — add enum to `FileCodecId`, bump `FILE_CODEC_COUNT`, add threshold
in `resolveFileCodecId()`, add case in `FileCodecDispatch::setup()`.

### Removing Old Codecs

Old codecs are removed only on **major version bumps** (e.g., v2.0.0).
Reset the version→codec registry thresholds and remove deprecated codec files.
