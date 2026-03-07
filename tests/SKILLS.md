# BCSV Test Suite — AI Skills Reference

> Quick-reference for AI agents to build, run, and extend the test suite.
> For humans, see also: tests/README.md

## Build & Run

```bash
# Build tests (debug — default)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target bcsv_gtest -j$(nproc)

# Run all tests
./build/bin/bcsv_gtest

# Run specific test suite
./build/bin/bcsv_gtest --gtest_filter="ComprehensiveTest.*"

# Run specific test
./build/bin/bcsv_gtest --gtest_filter="ComprehensiveTest.WriteAndReadBack"

# List all tests without running
./build/bin/bcsv_gtest --gtest_list_tests

# Run with verbose output
./build/bin/bcsv_gtest --gtest_print_time=1
```

## Build Targets

| Target | Source(s) | Gate | Description |
|--------|-----------|------|-------------|
| `bcsv_gtest` | 26 .cpp test files | `BUILD_TESTS=ON` | Main Google Test executable (694 tests) |
| `test_c_api` | `bcsv_c_api_test.c` | always | Standalone C API test (linked against `bcsv_c_api`) |
| `test_row_api` | `bcsv_c_api_row_test.c` | always | Standalone C Row API test |

```bash
# Build all test targets
cmake --build build -j$(nproc)

# Run C API tests
./build/test_c_api
./build/test_row_api
```

## Test Framework

- **Google Test 1.12.1** (fetched via CMake FetchContent — no manual install)
- C++20 required
- Test files generated at runtime go into `build/bcsv_test_files/` (gitignored)

## Test Source Files

| File | Covers |
|------|--------|
| `bcsv_comprehensive_test.cpp` | Core write/read/validate, flexible + static layouts, ZoH, string handling, edge cases |
| `bench_dataset_profiles_test.cpp` | Benchmark dataset profile definitions and validation |
| `bitset_test.cpp` | Bitset fixed + dynamic, SOO transitions, shift, slice, insert, resize |
| `codec_dispatch_test.cpp` | Codec selection and dispatch logic |
| `csv_reader_writer_test.cpp` | CSV text reader/writer round-trips |
| `direct_access_test.cpp` | ReaderDirectAccess random-access API |
| `error_handling_test.cpp` | Reader/Writer error paths, missing files, corrupt data, type mismatches |
| `file_codec_test.cpp` | File-level codec encode/decode (stream, packet, LZ4 variants) |
| `file_footer_test.cpp` | FileFooter encode/decode, packet index, random access |
| `file_header_byte_buffer_test.cpp` | FileHeader serialization to/from byte buffers |
| `layout_guard_test.cpp` | LayoutGuard RAII scoping |
| `layout_sync_test.cpp` | Layout observer callbacks, Row sync on addColumn/removeColumn/changeType |
| `lz4_stream_test.cpp` | LZ4 streaming compression/decompression, ring buffer, multi-block |
| `review_fixes_test.cpp` | Regression tests for code-review fixes |
| `row_codec_delta002_test.cpp` | Delta002 row codec encode/decode |
| `row_codec_flat001_test.cpp` | Flat001 row codec encode/decode |
| `row_codec_zoh001_test.cpp` | ZoH001 row codec encode/decode |
| `row_parameterized_test.cpp` | Parameterized type coverage for Row get/set/visit across all 12 column types |
| `sampler_test.cpp` | Sampler time-based sub-sampling |
| `stream_operator_test.cpp` | operator<< formatting for Row / Layout |
| `vectorized_access_test.cpp` | Vectorized C API read patterns, batch operations |
| `visit_test.cpp` | Visitor pattern: const/mutable visitors, typed visit, all column types |
| `vle_template_test.cpp` | VLE encode/decode round-trip for all integer types, edge values, zigzag |
| `vle_test.cpp` | VLE basic encode/decode |
| `writer_reader_test.cpp` | Writer → Reader round-trip tests |

Optional (gated):
| File | Gate | Description |
|------|------|-------------|
| `lz4_stress_test.cpp` | `BCSV_ENABLE_STRESS_TESTS=ON` | Long-running LZ4 stress tests, large data volumes |

C API tests (standalone executables, not part of `bcsv_gtest`):
| File | Executable |
|------|------------|
| `bcsv_c_api_test.c` | `test_c_api` |
| `bcsv_c_api_row_test.c` | `test_row_api` |
| `bcsv_c_api_full_test.c` | `test_c_api_full` |

## Adding a New Test

1. Add test source to `tests/` directory
2. Add the `.cpp` file to the `bcsv_gtest` source list in `tests/CMakeLists.txt`
3. Use Google Test macros: `TEST(SuiteName, TestName)` for simple tests, `TEST_F` for fixtures, `TEST_P` for parameterized
4. Follow naming: `SuiteName` = component being tested (e.g., `BitsetTest`, `ReaderTest`), `TestName` = what is tested
5. Build and run: `cmake --build build --target bcsv_gtest -j$(nproc) && ./build/bin/bcsv_gtest --gtest_filter="SuiteName.*"`

## Sanitizers & Debugging

```bash
# AddressSanitizer
cmake -S . -B build_asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build build_asan -j$(nproc)
./build_asan/bin/bcsv_gtest

# Valgrind (memory leak detection)
valgrind --leak-check=full ./build/bin/bcsv_gtest

# UndefinedBehaviorSanitizer
cmake -S . -B build_ubsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=undefined"
cmake --build build_ubsan -j$(nproc)
./build_ubsan/bin/bcsv_gtest
```

## Verification Checklist (New Features / Bug Fixes)

```bash
# 1. Debug build + all tests pass
cmake --build --preset ninja-debug-build -j$(nproc) && ./build/ninja-debug/bin/bcsv_gtest

# 2. Release build + all tests pass
cmake --build --preset ninja-release-build -j$(nproc) && ./build/ninja-release/bin/bcsv_gtest

# 3. C API tests pass
./build/ninja-debug/test_c_api && ./build/ninja-debug/test_row_api

# 4. Examples build and run without errors
./build/ninja-debug/bin/example && ./build/ninja-debug/bin/example_static

# 5. Benchmark smoke test (release only)
python3 benchmark/run.py wip --type=MICRO --no-report
```
