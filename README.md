# BCSV - Binary CSV for Time-Series Data

> **Fast, compact, and easy-to-use binary format for time-series data**  
> Combines CSV simplicity with binary performance and compression

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Version](https://img.shields.io/badge/version-1.3.0-orange.svg)](VERSIONING.md)

---

## What is BCSV?

**BCSV combines the ease of use of CSV files with the performance and storage efficiency of binary formats.** It's specifically designed for time-series data on both high-performance and embedded platforms.

### Key Features

- 📦 **15-25% of CSV size** with LZ4 compression (3-4% with Zero-Order Hold)
- 🚀 **3.6M-7.5M rows/second** processing speed (8-33 columns, Zen3 CPU)
- 🎯 **Self-documenting format** - no separate schema files needed
- 🌊 **Streaming I/O** - process datasets larger than available RAM
- 🛡️ **Crash-resilient** - recover data from incomplete writes
- 🌍 **Multi-language** - C++, Python, C#/Unity support
- ⚡ **Real-time capable** - constant-time operations for embedded systems

### Target Users

Anyone running **metrology, data acquisition, or telemetry** tasks - from embedded systems to HPC clusters.

---

## Quick Start

### Installation

**Header-only library** - just include the directory:

```cpp
#include <bcsv/bcsv.h>  // C++ API
```

Or build with CMake:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/bcsv_gtest  # Run tests
```

### 5-Minute Example

```cpp
#include <bcsv/bcsv.h>

int main() {
    // Define schema
    bcsv::Layout layout;
    layout.addColumn({"timestamp", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"temperature", bcsv::ColumnType::FLOAT});
    layout.addColumn({"sensor_id", bcsv::ColumnType::UINT16});
    
    // Write data
    bcsv::Writer<bcsv::Layout> writer(layout);
    writer.open("measurements.bcsv", true);
    
    writer.row().set(0, 1234567890.0);
    writer.row().set(1, 23.5f);
    writer.row().set(2, uint16_t{42});
    writer.writeRow();
    
    writer.close();
    
    // Read data
    bcsv::Reader<bcsv::Layout> reader;
    reader.open("measurements.bcsv");
    
    while (reader.readNext()) {
        auto timestamp = reader.row().get<double>(0);
        auto temp = reader.row().get<float>(1);
        auto id = reader.row().get<uint16_t>(2);
        std::cout << "Sensor " << id << ": " << temp << "°C @ " << timestamp << "\n";
    }
}
```

**Result:** 84% size reduction (105MB CSV → 16.3MB BCSV) with full type safety.

---

## Documentation

### 📚 Core Documentation

- **[docs/API_OVERVIEW.md](docs/API_OVERVIEW.md)** - Compare C++, C, Python, C# APIs with examples
- **[docs/ERROR_HANDLING.md](docs/ERROR_HANDLING.md)** - Error handling patterns and best practices
- **[docs/INTEROPERABILITY.md](docs/INTEROPERABILITY.md)** - Cross-language file compatibility
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Design decisions and binary format specification
- **[VERSIONING.md](VERSIONING.md)** - Automated versioning system

### 🎯 Getting Started

- **[examples/](examples/)** - C++ usage examples
- **[src/tools/CLI_TOOLS.md](src/tools/CLI_TOOLS.md)** - CLI tools documentation (csv2bcsv, bcsv2csv, bcsvSampler, bcsvGenerator, ...)
- **[python/README.md](python/README.md)** - Python package (pandas integration)
- **[unity/README.md](unity/README.md)** - C# Unity integration guide

### 📊 Performance & Benchmarks

- **[benchmark/README.md](benchmark/README.md)** - Benchmark quick-start and script-friendly run options
- **[tests/PERFORMANCE_COMPARISON.md](tests/PERFORMANCE_COMPARISON.md)** - Detailed benchmarks and comparisons
- **Typical speeds:** 3.6M rows/sec (flexible), 7.5M rows/sec (static), 127K rows/sec (1000 columns)
- **Compression:** 15-25% of CSV size (LZ4), 3-4% with Zero-Order Hold

**Run benchmarks locally:**

```bash
# Quick macro smoke run (default)
python3 benchmark/run.py wip

# Direct macro CLI overview (profiles/scenarios/examples)
build/ninja-release/bin/bench_macro_datasets --help

# Direct macro run with compact summary (narrow terminal friendly)
build/ninja-release/bin/bench_macro_datasets --size=S --summary=compact

# Micro benchmark pinned to CPU2
python3 benchmark/run.py wip --type=MICRO --pin=CPU2

# Combined campaign
python3 benchmark/run.py wip --type=MICRO,MACRO-SMALL,MACRO-LARGE

# Manual compare report (candidate vs baseline)
python3 benchmark/report.py <candidate_run_dir> --baseline <baseline_run_dir>
```

14 dataset profiles × multiple mode combinations (storage × tracking × codec), with optional
external CSV library comparison. See [benchmark/README.md](benchmark/README.md) for details.

---

## Supported Platforms & Languages

### C++ (Header-Only)

```cpp
bcsv::Layout layout;
layout.addColumn({"id", bcsv::ColumnType::INT32});
bcsv::Writer<bcsv::Layout> writer(layout);
```

Modern C++20, header-only library. **Fastest performance** with static API.

### Python (pip package)

```python
import pybcsv
df = pybcsv.read_dataframe("data.bcsv")
pybcsv.write_dataframe(df, "output.bcsv")
```

Full pandas integration. See [python/README.md](python/README.md).

### C# / Unity

```csharp
var layout = new BcsvLayout();
var writer = new BcsvWriter(layout);
writer.Open("data.bcsv");
```

Game development integration. See [unity/README.md](unity/README.md).

### C API (Shared Library)

```c
bcsv_writer_t writer = bcsv_writer_create(layout);
bcsv_writer_open(writer, "data.bcsv", 1);
```

For language bindings and embedded systems.

**All APIs produce identical binary format** - files are 100% cross-compatible. See [docs/INTEROPERABILITY.md](docs/INTEROPERABILITY.md).

---

## Project Status

**Current Version:** v1.3.0

### Delivered in v1.3.0

- ✅ Streaming LZ4 compression with constant write latency
- ✅ File indexing (`FileFooter`) for O(log N) random access (`ReaderDirectAccess`)
- ✅ Delta + VLE row codec (`RowCodecDelta002`) — 2.3% of CSV size
- ✅ Five file codec strategies (stream, stream-LZ4, packet, packet-LZ4, packet-LZ4-batch)
- ✅ Sampler API — bytecode VM for row filtering and column projection
- ✅ 8 CLI tools including `bcsvSampler`, `bcsvGenerator`, `bcsvValidate`

### Roadmap

- **v2.0.0** (Q2 2026): Stable release with compatibility guarantees

⚠️ **Development Notice:** Until v2.0.0, file formats may change between minor versions. Use for experimentation and non-critical storage.

---

## Project Structure

```text
bcsv/
├── include/bcsv/          # 📦 Header-only library (copy this to integrate)
│   ├── bcsv.h             #    Main include file
│   ├── reader.hpp         #    Reader implementation
│   ├── writer.hpp         #    Writer implementation
│   └── ...                #    Supporting headers
│
├── docs/                  # 📚 User-facing documentation
│   ├── API_OVERVIEW.md    #    Multi-language API comparison
│   ├── ERROR_HANDLING.md  #    Error handling guide
│   └── INTEROPERABILITY.md#    Cross-language compatibility
│
├── examples/              # 🎯 Usage examples (7 programs)
│   ├── example.cpp        #    Basic flexible-layout usage
│   ├── example_static.cpp #    Static-layout usage
│   └── example_sampler.cpp#    Sampler API demonstration
│
├── src/tools/             # 🔧 CLI tools (8 utilities)
│   ├── csv2bcsv.cpp       #    CSV → BCSV converter
│   ├── bcsv2csv.cpp       #    BCSV → CSV converter
│   ├── bcsvSampler.cpp    #    Filter & project rows
│   └── ...                #    bcsvHead, bcsvTail, bcsvHeader, bcsvGenerator, bcsvValidate
│
├── tests/                 # ✅ Comprehensive test suite (694 tests)
├── python/                # 🐍 Python package (pip install)
├── unity/                 # 🎮 C# Unity integration
├── cmake/                 # 🔧 Build system utilities
│
├── ARCHITECTURE.md        # 🏗️  Design and technical specification
├── VERSIONING.md          # 📋 Version management
└── README.md              # 👋 This file
```

---

## Key Concepts

### Data Types

Supports 12 types: `BOOL`, `UINT8/16/32/64`, `INT8/16/32/64`, `FLOAT`, `DOUBLE`, `STRING`

All types are stored in **little-endian** format with **IEEE 754** for floating point. See [docs/INTEROPERABILITY.md](docs/INTEROPERABILITY.md#type-compatibility).

### Flexible vs Static API

- **Flexible (Runtime):** Dynamic schemas, runtime validation, 3.6M rows/sec
- **Static (Compile-Time):** Type-safe templates, compile-time validation, 7.5M rows/sec

See [docs/API_OVERVIEW.md](docs/API_OVERVIEW.md#flexible-vs-static-apis) for detailed comparison.

### Error Handling

- **I/O operations return `bool`** - check return value and use `getErrorMsg()`
- **Row access throws exceptions** - catch `std::out_of_range` and `std::runtime_error`

See [docs/ERROR_HANDLING.md](docs/ERROR_HANDLING.md) for complete guide.

---

## Building & Testing

### Build Commands

**Build and test:**

```bash
# Configure and build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure

# Try examples
./build/bin/example
./build/bin/csv2bcsv data.csv
```

**Python package:**

```bash
cd python/
pip install -e .  # Development mode
```

**CMake integration:**

```cmake
add_subdirectory(external/bcsv)
target_link_libraries(your_target PRIVATE bcsv)
```

---

## Dependencies

- **LZ4** (v1.10.0) - Embedded or system version
- **xxHash** (v0.8.3) - Embedded
- **C++20 Standard Library** - No other runtime dependencies

**Build/test only:**

- Google Test (auto-fetched)
- pybind11 (auto-fetched for Python)

---

## License & Contributing

**MIT License** - Copyright (c) 2025 Tobias Weber. See [LICENSE](LICENSE).

**Contributions welcome!** Please open issues for bugs/features and include tests with PRs.

---

**Ready to start?** Check out [examples/](examples/) or [docs/API_OVERVIEW.md](docs/API_OVERVIEW.md) for detailed API documentation.
