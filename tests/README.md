# BCSV Test Suite Documentation

## Overview

This directory contains comprehensive tests for the BCSV (Binary CSV) library, focusing on the recently refactored Row API that replaced `std::variant` with direct memory access for improved performance.

**Test Framework:** Google Test (GTest)  
**Total Tests:** 694 across 26 .cpp files + 3 .c files  
**Test Executable:** `bcsv_gtest` (GTest), `test_c_api`, `test_c_api_full`, `test_row_api`

---

## Test Files

### Core Row API Tests

#### 1. **row_parameterized_test.cpp** ⭐ NEW
**Purpose:** Reduce test duplication using typed parameterized tests  
**Lines:** ~500  
**Tests:** 55+ (11 types × 5 test patterns + edge cases)

**Coverage:**
- ✅ All primitive types (bool, int8/16/32/64, uint8/16/32/64, float, double)
- ✅ Scalar get/set operations
- ✅ Vectorized get/set operations
- ✅ Serialization round-trip for all types
- ✅ Change tracking for all types
- ✅ Type mismatch detection
- ✅ Boundary value testing (min/max)
- ✅ Self-assignment edge case
- ✅ Move-after-move edge case
- ✅ String with embedded null bytes
- ✅ Vectorized out-of-bounds detection
- ✅ Flexible get with type conversions

**Key Features:**
- Uses `TYPED_TEST_SUITE` for type parameterization
- Consistent test patterns across all types
- Improved error messages with type context
- Reduces ~500 lines of duplicated code

#### 2. **vectorized_access_test.cpp**
**Purpose:** Test bulk data access optimizations  
**Lines:** 394  
**Tests:** 15

**Coverage:**
- ✅ Row vectorized get/set (dynamic layout)
- ✅ RowStatic compile-time vectorized access
- ✅ RowStatic runtime vectorized access
- ✅ Mixed column types with partial access
- ✅ Performance comparison (2.4x speedup verified)
- ✅ Change tracking with vectorized operations
- ✅ Boundary checking

**Performance Baseline:**
- Individual access: ~152ms for 1M operations
- Bulk access: ~63ms for 1M operations
- Speedup: 2.39x

#### 3. **bcsv_comprehensive_test.cpp**
**Purpose:** End-to-end integration tests  
**Lines:** 3411  
**Tests:** 44

**Coverage:**
- ✅ Layout manipulation (add/remove/change columns)
- ✅ Flexible interface (runtime layout) write/read
- ✅ Static interface (compile-time layout) write/read
- ✅ Cross-compatibility (Flexible ↔ Static)
- ✅ Compression levels (0-12, including HC)
- ✅ Zero-Order-Hold (ZoH) compression
- ✅ Checksum validation and corruption detection
- ✅ Packet recovery from broken data
- ✅ Multi-packet large datasets
- ✅ Edge cases (zero columns, zero rows)
- ✅ Boundary tests (max columns: 1000, max string: 65KB, max row: 16MB)

**Test Data:**
- 10,000 rows with 24 columns (all types)
- Random data with fixed seed for reproducibility
- Cross-validates Flexible vs Static implementations

### Specialized Tests

#### 4. **file_footer_test.cpp**
**Purpose:** Test packet index and footer serialization  
**Tests:** 18

**Coverage:**
- ✅ Footer structure and sizing
- ✅ Packet index entry creation
- ✅ Serialization/deserialization
- ✅ Corruption detection (magic bytes, checksum)
- ✅ Large index handling

#### 5. **lz4_stream_test.cpp**
**Purpose:** LZ4 streaming compression integration  
**Tests:** 19

**Coverage:**
- ✅ Stream creation/destruction
- ✅ Basic compression/decompression
- ✅ Round-trip verification
- ✅ Context preservation across packets
- ✅ Stream reset functionality
- ✅ Move semantics
- ✅ Large data handling
- ✅ Small row streaming (BCSV use case)

#### 6. **lz4_stress_test.cpp** (Optional, enabled with `-DBCSV_ENABLE_STRESS_TESTS=ON`)
**Purpose:** Stress test compression with extreme patterns  
**Tests:** 4

**Coverage:**
- ✅ Random data stream (100K rows)
- ✅ Corner case patterns (alternating sizes, boundaries)
- ✅ Parallel execution
- ✅ Benchmark (sustained throughput)

#### 7. **vle_test.cpp**
**Purpose:** Variable-Length Encoding tests  
**Tests:** 24

**Coverage:**
- ✅ Encoding 1-9 byte values
- ✅ Decode round-trip
- ✅ Peek operations
- ✅ Error handling (buffer too small, invalid encoding)
- ✅ Streaming decoder
- ✅ BCSV-specific use cases

#### 8. **vle_template_test.cpp**
**Purpose:** Template-based VLE tests  
**Tests:** 8

**Coverage:**
- ✅ Type-specific encoding (uint8, int8, uint16)
- ✅ Truncation behavior
- ✅ Buffer and ByteBuffer API
- ✅ Overflow handling

### C API Tests

#### 9. **bcsv_c_api_test.c**
**Purpose:** C language API validation  

#### 10. **bcsv_c_api_row_test.c**
**Purpose:** Focused C API row tests  

#### 11. **bcsv_c_api_full_test.c**
**Purpose:** Full C API test coverage including reader/writer/sampler

### Additional Test Files (added post-v1.2.0)

| File | Covers |
|------|--------|
| `row_codec_flat001_test.cpp` | Flat001 codec serialize/deserialize |
| `row_codec_zoh001_test.cpp` | ZoH001 codec delta encoding |
| `row_codec_delta002_test.cpp` | Delta002 codec type-grouped loops, FoC, VLE |
| `codec_dispatch_test.cpp` | Runtime codec selection and dispatch |
| `layout_guard_test.cpp` | RAII structural lock on Layout |
| `file_codec_test.cpp` | File codec strategies (stream, packet, LZ4, batch) |
| `writer_reader_test.cpp` | Writer/Reader integration |
| `csv_reader_writer_test.cpp` | CsvReader/CsvWriter round-trip |
| `direct_access_test.cpp` | ReaderDirectAccess random seek |
| `sampler_test.cpp` | Sampler bytecode VM filter/project |
| `stream_operator_test.cpp` | Stream operator overloads |
| `review_fixes_test.cpp` | Regression tests for review-found bugs |
| `file_header_byte_buffer_test.cpp` | FileHeader + ByteBuffer edge cases |
| `bench_dataset_profiles_test.cpp` | Benchmark dataset profile validation |
| `vle_test.cpp` | Variable-Length Encoding |
| `lz4_stress_test.cpp` | LZ4 long-running stress tests (optional) |

---

## Test Organization

### Test Suite Structure

```
tests/
├── Core Tests
│   ├── bcsv_comprehensive_test.cpp    # End-to-end integration
│   ├── row_parameterized_test.cpp     # Type-parameterized tests
│   ├── vectorized_access_test.cpp     # Bulk access optimizations
│   └── visit_test.cpp                 # Visitor pattern
│
├── Codec Tests
│   ├── row_codec_flat001_test.cpp     # Flat001 codec
│   ├── row_codec_zoh001_test.cpp      # ZoH001 codec
│   ├── row_codec_delta002_test.cpp    # Delta002 codec
│   ├── codec_dispatch_test.cpp        # Runtime dispatch
│   └── file_codec_test.cpp            # File codec strategies
│
├── Schema & Layout Tests
│   ├── layout_sync_test.cpp           # Observer callbacks
│   ├── layout_guard_test.cpp          # RAII structural lock
│   └── bitset_test.cpp                # Bitset SOO, shift, slice
│
├── I/O & Format Tests
│   ├── writer_reader_test.cpp         # Writer/Reader integration
│   ├── csv_reader_writer_test.cpp     # CSV round-trip
│   ├── direct_access_test.cpp         # ReaderDirectAccess
│   ├── file_footer_test.cpp           # Footer/index
│   ├── file_header_byte_buffer_test.cpp # Header + ByteBuffer
│   ├── lz4_stream_test.cpp            # LZ4 streaming
│   ├── lz4_stress_test.cpp            # LZ4 stress (optional)
│   ├── vle_test.cpp                   # VLE encoding
│   └── vle_template_test.cpp          # VLE template
│
├── Feature Tests
│   ├── sampler_test.cpp               # Sampler bytecode VM
│   ├── stream_operator_test.cpp       # Stream operators
│   └── error_handling_test.cpp        # Error paths
│
├── Validation & Regression
│   ├── bench_dataset_profiles_test.cpp # Profile validation
│   └── review_fixes_test.cpp          # Regression tests
│
├── C API Tests
│   ├── bcsv_c_api_test.c              # Core C API
│   ├── bcsv_c_api_full_test.c          # Full C API coverage
│   └── bcsv_c_api_row_test.c           # Row-specific C API
│
└── Configuration
    ├── CMakeLists.txt                 # Build configuration
    └── README.md                      # This file
```

### Test Naming Conventions

- **Test Suites:** `ComponentNameTest` (e.g., `RowTypedTest`)
- **Test Cases:** `FunctionName_Behavior` (e.g., `GetSetScalar`)
- **Fixture Classes:** `ComponentNameTest` (e.g., `BCSVTestSuite`)

---

## Building and Running Tests

### Build Tests

```bash
# Configure with tests
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build test executable
cmake --build build --target bcsv_gtest

# Optional: Enable stress tests
cmake -B build -S . -DBCSV_ENABLE_STRESS_TESTS=ON
```

### Run All Tests

```bash
# Run from build directory
./build/bin/bcsv_gtest

# Or from project root
cd build && ctest --verbose
```

### Run Specific Tests

```bash
# Filter by test suite
./build/bin/bcsv_gtest --gtest_filter="RowTypedTest.*"

# Filter by test case pattern
./build/bin/bcsv_gtest --gtest_filter="*Vectorized*"

# Run specific type in parameterized test
./build/bin/bcsv_gtest --gtest_filter="RowTypedTest/0.*"  # bool
./build/bin/bcsv_gtest --gtest_filter="RowTypedTest/3.*"  # int32_t

# List all tests
./build/bin/bcsv_gtest --gtest_list_tests
```

### Run with Sanitizers

Dedicated presets exist for the two sanitizers that guard known bug classes
(TSan: the batch codec's internal thread; UBSan: hostile-input decode paths).
Run both before a release — the full suite is expected to be clean:

```bash
# ThreadSanitizer (clang, RelWithDebInfo)
cmake --preset clang-tsan
cmake --build --preset clang-tsan-build --target bcsv_gtest bcsvRepair -j$(nproc)
./build/clang-tsan/bin/bcsv_gtest --gtest_filter='*Batch*:*LZ4*:FileCodec*'

# UndefinedBehaviorSanitizer (clang, RelWithDebInfo, halt on first error)
cmake --preset clang-ubsan
cmake --build --preset clang-ubsan-build --target bcsv_gtest -j$(nproc)
./build/clang-ubsan/bin/bcsv_gtest

# Address Sanitizer (no preset yet — manual flags)
cmake -B build -S . -DCMAKE_CXX_FLAGS="-fsanitize=address -g"
./build/bin/bcsv_gtest
```

### Run with Valgrind

```bash
valgrind --leak-check=full --show-leak-kinds=all ./build/bin/bcsv_gtest
```

### Python Integration Tests

Three pytest modules test CLI tools end-to-end (cross-platform):

```bash
# All CLI tools (csv2bcsv, bcsv2csv, bcsvHead, bcsvTail, bcsvHeader, bcsvGenerator)
python3 -m pytest tests/integration/test_cli_tools.py -v

# bcsvSampler conditional sampling
python3 -m pytest tests/integration/test_sampler.py -v

# bcsvValidate structure, pattern, and comparison modes
python3 -m pytest tests/integration/test_validate.py -v

# Run all integration tests
python3 -m pytest tests/integration/ -v
```

---

## Coverage Analysis

### Generate Coverage Report

```bash
# Build with coverage flags
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -g"
cmake --build build

# Run tests
./build/bin/bcsv_gtest

# Generate report
lcov --capture --directory build --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/googletest/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_html

# View report
xdg-open coverage_html/index.html
```

### Current Coverage

Run `lcov` after building with `--coverage` to get up-to-date numbers.
All public API methods are covered. All error paths are validated. All 12 data types are tested.

---

## Test Categories

### Unit Tests (Component Isolation)
- ✅ Row API (all variants)
- ✅ Layout API
- ✅ VLE encoding
- ✅ File footer
- ✅ LZ4 streaming

### Integration Tests (Component Interaction)
- ✅ Reader/Writer with Row
- ✅ Flexible ↔ Static compatibility
- ✅ Serialization with compression
- ✅ ZoH compression end-to-end

### Stress Tests (Performance & Limits)
- ✅ Large datasets (1M+ rows)
- ✅ Boundary conditions (max sizes)
- ✅ Random data patterns
- ✅ Parallel execution

### Edge Case Tests
- ✅ Empty collections (zero rows, zero columns)
- ✅ Boundary values (min/max integers)
- ✅ Self-assignment
- ✅ Move-after-move
- ✅ String with null bytes
- ✅ Type conversions

---

## Verification Checklist

Use this checklist when adding new Row API functionality:

### For New Row Functions
- [ ] Add to `row_parameterized_test.cpp` (all types)
- [ ] Test with vectorized access if applicable
- [ ] If ZoH behavior is affected: add codec-level tests for internal tracking semantics
- [ ] Test serialization/deserialization
- [ ] Add error cases (type mismatch, out of bounds)
- [ ] Update this README with new coverage

### For Bug Fixes
- [ ] Add regression test reproducing the bug
- [ ] Verify fix doesn't break existing tests
- [ ] Add edge case variations
- [ ] Document the issue in test comments

### For Refactoring
- [ ] Run full test suite before changes
- [ ] Run full test suite after changes
- [ ] Check coverage hasn't decreased
- [ ] Update tests if API changed
- [ ] Update this README if structure changed

---

## Known Limitations & Future Work

### Under-Tested Areas (60-80% coverage)

1. **Error Handling Edge Cases**
   - Memory exhaustion scenarios
   - Partial write failures (disk full)
   - Concurrent access (if threading intended)

2. **Platform-Specific Issues**
   - Big-endian systems
   - 32-bit vs 64-bit pointer differences
   - SIMD instruction availability

3. **Resource Limits**
   - File descriptor exhaustion
   - Virtual memory limits
   - Long-running fragmentation

### Planned Improvements

1. **Immediate (Before v1.0)**
   - ✅ Add parameterized type tests (DONE)
   - ✅ Improve error messages (DONE)
   - ✅ Document test plan (DONE)
   - [ ] Enable code coverage reporting in CI

2. **Short-term (Next Sprint)**
   - [ ] Add fuzzing test harness
   - [ ] Property-based testing integration
   - [ ] Cross-platform CI (Windows, macOS, ARM)

3. **Long-term (Ongoing)**
   - [x] Performance regression tracking (benchmark/report.py comparison mode)
   - [ ] Memory leak detection in CI (Valgrind/ASan)
   - [x] Benchmark dashboard (benchmark/report.py + CI workflow)

---

## Contributing Tests

### Adding a New Test

1. Choose appropriate test file (or create new one)
2. Use descriptive test names: `Component_Behavior_ExpectedResult`
3. Add clear error messages to all assertions
4. Include test documentation at file/function level
5. Update this README with coverage info

### Example Test Template

```cpp
TEST(ComponentTest, FunctionName_SuccessCase) {
    // Arrange
    Setup test_data;
    
    // Act
    auto result = component.function(test_data);
    
    // Assert with descriptive error message
    EXPECT_EQ(expected, result)
        << "Function should return X when input is Y";
}
```

### Error Message Guidelines

**Good:**
```cpp
EXPECT_EQ(42, value) 
    << "Column 'age' at row " << i << " expected 42, got " << value;
```

**Bad:**
```cpp
EXPECT_EQ(42, value);  // What failed? Which column? Which row?
```

---

## Benchmark Suite

The benchmark workflow is streamlined to three run types: `MICRO`, `MACRO-SMALL`, `MACRO-LARGE`.
See [benchmark/README.md](../benchmark/README.md) for the authoritative operational guide.

### Architecture

```
bench_macro_datasets    Macro: full write→read→validate cycles per dataset profile
bench_micro_types       Micro: per-type Get/Set, VisitConst, Serialize (Google Benchmark)
```

Orchestrated by **`benchmark/run.py`** using a unified CLI with subcommands:
`wip`, `baseline`, `compare`, `interleaved`.

Macro benchmarks run in 5 modes: `CSV`, `BCSV Flexible`, `BCSV Flexible ZoH`,
`BCSV Static`, `BCSV Static ZoH`.

### Dataset Profiles

| Profile | Columns | Character |
|---------|---------|-----------|
| `mixed_generic` | 72 | General-purpose mix of all types |
| `sparse_events` | 100 | Many empty/zero columns, burst data |
| `sensor_noisy` | 50 | High-entropy floats, ZoH-unfriendly |
| `string_heavy` | 30 | Predominantly string columns |
| `bool_heavy` | 128+ | Mostly bool columns, bitset-friendly |
| `arithmetic_wide` | 200 | Pure numeric, wide rows |
| `simulation_smooth` | 100 | Slowly-drifting floats, ZoH-optimal |
| `weather_timeseries` | 40 | Realistic mixed weather telemetry |
| `high_cardinality_string` | 50 | UUID strings, worst-case compression |
| `event_log` | 27 | Backend event stream with bounded categorical strings |
| `iot_fleet` | 25 | Fleet telemetry with round-robin device metadata |
| `financial_orders` | 22 | Order/trade feed with categorical tags + derived metrics |
| `realistic_measurement` | 38 | DAQ session phases with mixed update rates |
| `rtl_waveform` | 290 | Digital waveform capture (bool-heavy + register buses) |

### Running Benchmarks

```bash
# Default run (MACRO-SMALL)
python3 benchmark/run.py wip

# Micro benchmark pinned to CPU2
python3 benchmark/run.py wip --type=MICRO --pin=CPU2

# Full campaign
python3 benchmark/run.py wip --type=MICRO,MACRO-SMALL,MACRO-LARGE

# Explicit comparison report
python3 benchmark/report.py <candidate-run-dir> --baseline <baseline-run-dir>
```

### Runtime Targets

| Type | Rows | Target Duration |
|------|------|-----------------|
| `MACRO-SMALL` | 10K | < 3 minutes |
| `MACRO-LARGE` | 500K | < 60 minutes |
| `MICRO` | n/a | < 5 minutes (pinned to CPU2) |

### CMake Options

| Option | Default | Purpose |
|--------|---------|---------|
| `BCSV_ENABLE_BENCHMARKS` | ON | Build macro/micro/generator benchmarks |
| `BCSV_ENABLE_MICRO_BENCHMARKS` | ON | Build Google Benchmark micro-benchmarks |
| `BCSV_ENABLE_EXTERNAL_CSV_BENCH` | OFF | Optional one-time external reference benchmark |

### Output Files

All outputs are JSON for machine consumption:

| File | Producer | Contents |
|------|----------|----------|
| `macro_small_results.json` / `macro_large_results.json` | `bench_macro_datasets` | Per-type macro write/read times, file sizes, validation |
| `macro_results.json` | orchestrator compatibility view | Merged macro view across selected macro types |
| `micro_results.json` | `bench_micro_types` | Per-type latency (ns/op) |
| `platform.json` | orchestrator | CPU, RAM, OS, git version |
| `report.md` | `report.py` | Summary markdown report |
| `report_<label>_<timestamp>.md` | `report.py` | Timestamped report artifact |

When `--repetitions > 1`, top-level `macro_*_results.json` and `micro_results.json` are median-aggregated across repetitions.

### Performance Benchmarks

#### Vectorized Access Performance
- **Individual access:** 152ms for 1M get/set operations
- **Bulk access:** 63ms for 1M get/set operations
- **Speedup:** 2.39x

### ZoH Compression Effectiveness
- **Normal serialization:** 18,457 bytes (10K rows, sparse data)
- **ZoH compression:** 15,747 bytes
- **Compression ratio:** 1.17x (17% reduction)
- **Typical savings:** 70-95% for slowly-changing time-series data

### Memory Layout (vs std::variant)
- **Old approach:** ~40 bytes per cell (variant overhead)
- **New approach:** Type-dependent (1-8 bytes primitives, direct string storage)
- **Improvement:** ~80% memory reduction for primitive types

---

## Debugging Failed Tests

### Common Issues

1. **Floating-point comparison failures**
   - Use `EXPECT_FLOAT_EQ` or `EXPECT_DOUBLE_EQ`
   - Consider tolerance with `EXPECT_NEAR`

2. **Type mismatch errors**
   - Check ColumnType matches C++ type
   - Verify layout column order

3. **Out-of-bounds access**
   - Check column count vs access index
   - Verify vectorized span size

4. **Serialization failures**
   - Ensure layout compatibility
   - Check buffer size calculations
   - Verify string encoding

### Debug Build Assertions

The library includes extensive debug assertions:
- Alignment verification
- Buffer overflow detection  
- Type compatibility checking
- Null pointer validation

Run tests in Debug mode to enable these checks:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
```

---

## Contact & Support

For questions or issues with tests:
- Review test comments and documentation
- Check existing issues on GitHub
- Run with `--gtest_filter` to isolate failing tests
- Enable verbose output: `--gtest_brief=0`

---

**Last Updated:** February 12, 2026  
**Test Suite Version:** 1.3.0  
**Maintained by:** BCSV Development Team
