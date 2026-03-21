# BCSV — Copilot Instructions

Binary-CSV (BCSV): C++20 header-only library for fast, compact time-series storage.
Streaming row-by-row I/O, constant-time writes, crash resilient. Targets STM32 to desktop.

Minimum toolchains: GCC 13+, Clang 16+/libstdc++13, MSVC 2022 17.4+, Xcode 15.4+,
Arm GNU 13.2+ (STM32 Linux), Vitis/PetaLinux 2024.1+ (AMD/Xilinx Linux).

## Build & Test Commands

```bash
# Debug build + unit tests
cmake --preset ninja-debug && cmake --build --preset ninja-debug-build -j$(nproc)
./build/ninja-debug/bin/bcsv_gtest

# Release build
cmake --preset ninja-release && cmake --build --preset ninja-release-build -j$(nproc)

# Run ALL tests (unit + integration via CTest)
ctest --test-dir build/ninja-debug --output-on-failure

# Filter specific test suite
./build/ninja-debug/bin/bcsv_gtest --gtest_filter="ComprehensiveTest.*"

# C API tests
./build/ninja-debug/bin/test_c_api && ./build/ninja-debug/bin/test_row_api

# Quick micro benchmark
python3 benchmark/run.py wip --type=MICRO --no-report

# Macro benchmark (15 profiles)
python3 benchmark/run.py wip --type=MACRO-SMALL

# Codec parity investigation (Flexible vs Static)
./build/ninja-release/bin/bench_codec_compare \
    --rows=50000 --iterations=5 --profile=measurement_campaign \
    --storage=both --json=tmp/parity_investigation.json

# Build single CLI tool
cmake --build --preset ninja-release-build --target bcsvSampler -j$(nproc)

# Clean rebuild
rm -rf build/ninja-debug && cmake --preset ninja-debug && cmake --build --preset ninja-debug-build -j$(nproc)
```

## Project Layout

```
include/bcsv/         # Header-only C++ library (.h = declarations, .hpp = implementations)
  codec_row/           # Row codecs (flat001, zoh001, delta002)
  codec_file/          # File codecs (stream, packet, LZ4 variants)
  sampler/             # Bytecode VM for row filtering/projection
src/tools/             # 8 CLI tools (csv2bcsv, bcsv2csv, bcsvHead/Tail/Header, bcsvSampler, bcsvGenerator, bcsvValidate)
tests/                 # GTest suite (25 .cpp) + C API tests (3 .c) + shell integration tests
benchmark/             # Python orchestrator + Google Benchmark executables
python/                # nanobind bindings + pandas integration
unity/                 # C# / Unity bindings (P/Invoke)
examples/              # 11 C++ example programs
```

## Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Private members | `snake_case_` (trailing `_`) | `file_path_`, `row_count_` |
| Public members | `snake_case` | `version_major`, `name` |
| Functions/methods | `lowerCamelCase` | `addColumn()`, `readNext()` |
| Classes/structs | `UpperCamelCase` | `Layout`, `FileHeader` |
| Constants/enums | `CAPITAL_SNAKE_CASE` | `MAX_COLUMNS`, `ZERO_ORDER_HOLD` |
| C API | `bcsv_snake_case` | `bcsv_reader_open()` |

## File Organization

- `.h` = concise declarations, human-readable
- `.hpp` = implementations, detailed logic
- Header guard: `#pragma once`
- Include order: standard library (`<>`) first, then project headers (`""`)

## Error Handling

- I/O operations (`open`, `readNext`, `writeRow`): return `bool`, use `getErrorMsg()` for details
- Logic errors (out-of-range, type mismatch): throw exceptions
- Debug output: controlled by `DEBUG_OUTPUTS` constexpr in `definitions.h`

## Key Design Patterns

- **Streaming I/O**: never load entire file — row-by-row read/write
- **Observer pattern**: `Layout` notifies attached `Row` objects of schema changes
- **Codec-based design**: row encoding (Flat/ZoH/Delta) and file codec (stream/packet) are orthogonal template axes
- **Three-container Row**: `bits_` (Bitset), `data_` (byte vector), `strg_` (string vector)
- **Codec separation**: wire-format lives in codec classes, not in Row
- **LayoutGuard**: RAII lock prevents layout mutation while a codec is active

## Temporary Files Policy

Use `tmp/` under project root for scratch work — it's gitignored.

## Release Workflow

When tagging a release version, update `CHANGELOG.md` with the changes since the last tag.
Follow [Keep a Changelog](https://keepachangelog.com/) format (Added/Changed/Fixed/Removed sections).

## Deep Reference

- `SKILLS.md` — full AI skills reference (API classes, source inventory, all conventions)
- `ARCHITECTURE.md` — design philosophy, binary format spec, roadmap
- `src/tools/CLI_TOOLS.md` — all 8 CLI tools with options and examples
- `tests/README.md` — test infrastructure details
- `benchmark/README.md` — benchmark orchestrator and profiles
- `ToDo.txt` — active task list with priorities
