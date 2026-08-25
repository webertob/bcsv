# BCSV — Deep Reference

Detail that does not fit in `.github/copilot-instructions.md`. **Read that file first** —
it carries the build commands, layout, naming conventions, error model and design patterns,
and it is the canonical copy of all of them. This file adds four things it deliberately
leaves out: the public API surface, the header-by-header source inventory, the CMake
option/preset matrix, and the procedure for adding a version-gated codec.

For humans: `README.md` (overview), `ARCHITECTURE.md` (design rationale and wire format).

## Public API Classes

Declared in `include/bcsv/`:

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

> Change tracking is internal to the ZoH001 and Delta002 codecs. There is no public tracking API.

```cpp
bcsv::Layout layout;
layout.addColumn("time", bcsv::ColumnType::DOUBLE);
layout.addColumn("value", bcsv::ColumnType::FLOAT);

bcsv::Writer<bcsv::Layout> writer;
writer.open("data.bcsv", layout);
writer.row().set<double>(0, 1.0);
writer.row().set<float>(1, 42.0f);
writer.writeRow();
writer.close();

bcsv::Reader<bcsv::Layout> reader;
reader.open("data.bcsv");
while (reader.readNext()) {
    double t = reader.row().get<double>(0);
    float  v = reader.row().get<float>(1);
}
reader.close();
```

Per-language equivalents: `docs/API_OVERVIEW.md`.

## Source File Inventory (`include/bcsv/`)

Row codec headers live in `codec_row/`, file codec headers in `codec_file/`; the table
uses short names.

### Declarations (`.h`)

| File | Purpose |
|------|---------|
| `bcsv.h` | Main include — aggregates all headers |
| `definitions.h` | `ColumnType`, `FileFlags`, `ValueType`, magic bytes, limits, codec registry + `resolve*CodecId()` |
| `layout.h` | `Layout`, `LayoutStatic<>`, `ColumnDefinition`, observer pattern |
| `row.h` | `RowImpl<>`, `RowStaticImpl<>` — binary format docs |
| `row_visitors.h` | C++20 concepts: `ConstRowVisitor`, `MutableRowVisitor` |
| `reader.h` | `Reader<>`, `ReaderDirectAccess<>` |
| `writer.h` | `Writer<>` and its codec aliases |
| `row_codec_flat001.h` | `RowCodecFlat001<>` — dense flat codec |
| `row_codec_zoh001.h` | `RowCodecZoH001<>` — ZoH delta codec (composes Flat001 for the first row) |
| `row_codec_delta002.h` | `RowCodecDelta002<>` — delta + VLE codec |
| `row_codec_variant.h` | `RowCodecType<>` — compile-time codec selection for Writer |
| `row_codec_dispatch.h` | `CodecDispatch<>` — runtime codec selection for Reader (union + function pointers) |
| `layout_guard.h` | `LayoutGuard` — RAII structural lock |
| `file_header.h` | `FileHeader` — fixed header + variable schema section |
| `packet_header.h` | `PacketHeader` — per-packet header (magic, row index, checksum) |
| `file_footer.h` | `FileFooter`, `PacketIndexEntry` — EOF index for random access |
| `bitset.h` | `Bitset<N>` (fixed) / `Bitset<>` (dynamic, SOO) — change tracking + bool storage |
| `byte_buffer.h` | `LazyAllocator<T>`, `ByteBuffer` — no-init byte vector |
| `column_name_index.h` | `ColumnNameIndex<>` — flat-map name → index lookup |
| `bcsv_c_api.h` | C API surface — opaque handles, `extern "C"` functions |
| `checksum.hpp` | `Checksum` / `Checksum::Streaming` — xxHash64 wrapper |

### Implementations (`.hpp`)

| File | Purpose |
|------|---------|
| `bcsv.hpp` | Stream type traits (`is_fstream`, `has_open_method`, …) |
| `layout.hpp` | Offset computation, observer callbacks, bool/tracked mask management |
| `row.hpp` | `get<T>()` / `set<T>()` / `visit()` |
| `reader.hpp` | `open()`, `close()`, `readNext()`, packet handling, codec dispatch |
| `writer.hpp` | `open()`, `close()`, `writeRow()`, packet management, codec dispatch |
| `row_codec_flat001.hpp` | Flat001 serialize / deserialize |
| `row_codec_zoh001.hpp` | ZoH001 delta serialize / deserialize |
| `row_codec_delta002.hpp` | Delta002 — type-grouped loops, FoC/ZoH per column, float XOR + VLE |
| `file_header.hpp` | Header read / write / validation |
| `bitset.hpp` | Full bitset implementation, SOO, shift, slice views |
| `column_name_index.hpp` | Name parsing, sorted insert, binary search |
| `vle.hpp` | Variable-Length Encoding — zigzag for signed ints |
| `lz4_stream.hpp` | Streaming LZ4 compressor/decompressor with ring-buffer dictionary |
| `bcsv_c_api.cpp` | C API implementation wrapping the C++ classes (lives in `src/`) |

## CMake Options & Presets

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_EXAMPLES` | ON | Build example programs |
| `BUILD_TOOLS` | ON | Build the CLI tools |
| `BUILD_TESTS` | ON | Build the GTest suite + C API tests |
| `BUILD_BENCHMARKS` | ON | Build benchmark programs |
| `BCSV_ENABLE_BATCH_CODEC` | ON | Batch-LZ4 file codec (requires threading) |
| `BCSV_WERROR` | ON | Treat compiler warnings as errors |

Presets in `CMakePresets.json`: `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release`,
`clang-tsan`, `clang-ubsan`, `msvc-debug`, `msvc-release`, `ninja-debug`, `ninja-release`,
`ninja-msvc-debug`, `ninja-msvc-release`, `ninja-release-min`.

Run `clang-tsan` and `clang-ubsan` before releases — both have caught shipped bugs.
There is no ASan preset yet; use manual flags (see `tests/README.md`).

Dependencies are bundled (xxHash 0.8.3, LZ4 1.10.0) — no external installs.

## Build Targets

| Group | Targets |
|---|---|
| Tests | `bcsv_gtest`, `test_c_api`, `test_row_api`, `test_c_api_full` |
| CLI tools (11) | `csv2bcsv`, `bcsv2csv`, `bcsvHead`, `bcsvTail`, `bcsvHeader`, `bcsvSampler`, `bcsvGenerator`, `bcsvValidate`, `bcsvRepair`, `bcsvCompare`, `bcsvCast` |
| Examples (11) | `quickstart`, `example`, `example_static`, `example_zoh`, `example_zoh_static`, `example_delta`, `example_direct_access`, `example_error_handling`, `c_api_vectorized_example`, `visitor_examples`, `example_sampler` |
| Benchmarks | `bench_macro_datasets`, `bench_micro_types`, `bench_micro_bitset`, `bench_direct_access`, `bench_c_api`, `bench_sampler`, `bench_codec_compare`, `bench_generate_csv`, `bench_external_csv` |

Executables land in `build/<preset>/bin/`.

## Subsystem Deep References

| Subsystem | Read |
|---|---|
| Tests | `tests/README.md` |
| Benchmarks | `benchmark/README.md`, `benchmark/REFERENCE_WORKLOADS.md` |
| CLI tools | `src/tools/CLI_TOOLS.md` |
| Examples | `examples/README.md` |
| Python (nanobind + pandas/Polars) | `python/README.md` |
| C# / NuGet | `csharp/README.md` |
| Unity | `unity/README.md` |
| Cross-language file compatibility | `docs/INTEROPERABILITY.md` |
| Thread safety | `docs/THREAD_SAFETY.md` |
| Settled design decisions | `docs/adr/README.md` |
| Scope / duplication rules | `docs/LEAN_CHECKLIST.md` |

The C# and Unity bindings are the same source in two namespaces — see the note in
`csharp/README.md` before editing either.

## Adding a New Codec (Version-Gated Registry)

When a new minor version introduces a row or file codec, follow this recipe. A
`static_assert` in `definitions.h` breaks the build if you add an enum value without
completing these steps.

### Row codec (e.g. adding DELTA003 in v1.7)

1. **Create codec files**: `include/bcsv/codec_row/row_codec_delta003.h` and `.hpp`.
   Leave `row_codec_delta002.h/.hpp` untouched — the old codec stays for backward compat.
2. **Add the enum value** in `definitions.h`: `DELTA003` in `RowCodecId`, bump `ROW_CODEC_COUNT`.
3. **Add the version threshold** in `resolveRowCodecId()` (`definitions.h`):
   ```cpp
   if ((flags & FileFlags::DELTA_ENCODING) != FileFlags::NONE)
       return (fileMinor >= 7) ? RowCodecId::DELTA003 : RowCodecId::DELTA002;
   ```
4. **Add the dispatch case** in `RowCodecDispatch::setup()` (`row_codec_dispatch.h`) —
   a new `case RowCodecId::DELTA003:` block with trampoline functions.
5. **Bump the version** via git tag: `git tag v1.7.0`.
6. **Update** the version→codec table in `VERSIONING.md` § Codec Registry.
7. **Add tests**: a cross-version test that writes with the v1.7 codec, patches the header
   to minor 6, and verifies the old codec is selected.

### File codec (e.g. adding PACKET_LZ4_BATCH_002 in v1.8)

Same pattern — add the enum to `FileCodecId`, bump `FILE_CODEC_COUNT`, add the threshold in
`resolveFileCodecId()`, add the case in `FileCodecDispatch::setup()`.

### Removing old codecs

Only on a major version bump (e.g. v2.0.0): reset the version→codec thresholds and remove
the deprecated codec files.
