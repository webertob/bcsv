# BCSV - Binary CSV for Time-Series Data

> **Fast, compact, and easy-to-use binary format for time-series data**  
> Combines CSV simplicity with binary performance and compression

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Version](https://img.shields.io/badge/version-1.2.0--dev-orange.svg)](VERSIONING.md)

---

## Mission Statement

**BCSV combines the ease of use of CSV files with the performance and storage efficiency of binary formats**, specifically optimized for large time-series datasets on both high-performance and embedded platforms.

### Core Principles

- **No schema files**: Define data structures directly in your code (C++, Python, C#)
- **Self-documenting**: File header contains all type information, like CSV but enforced
- **Streaming first**: Read/write data larger than available RAM, row by row
- **Constant-time operations**: Predictable performance for real-time recording
- **Time-series optimized**: Efficient compression for constant values and binary waveforms
- **Crash-resilient**: Retrieve data even from incomplete/interrupted writes
- **Natural to use**: Feels like CSV, works like binary

### Design Goals

| Goal | Target | Status |
|------|--------|--------|
| **Sequential recording** | 1000 channels @ 1KHz (STM32F4), 10KHz (STM32F7/Zynq/RPi) | ✅ Achievable |
| **Idle file growth** | <1KB/s for 1000-channel 10KHz stream (counter only) | ✅ With ZoH |
| **Processing speed** | ≥1M rows/sec for 1000-channel streams on Zen3 CPU | ⚠️ 127K+ rows/sec* |
| **Compression ratio** | <30% of equivalent CSV size | ✅ 15-25% typical |
| **Platform support** | C/C++, C#, Python | ✅ All supported |

\* _Typical datasets (8-33 columns) achieve 3.6M rows/sec (flexible API) to 7.5M rows/sec (static API). The 1M rows/sec target for 1000-channel streams requires further optimization._

**Target users**: Anyone running metrology, data acquisition, or telemetry tasks with digital tools.

---

## Quick Start

### Installation

```bash
# Header-only library - just copy include directory
cp -r include/bcsv/ your_project/include/

# Or use CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Hello BCSV

```cpp
#include <bcsv/bcsv.h>

int main() {
    // Create layout
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

---

## Features

### Core Capabilities

- **Header-only C++20 library**: Easy integration, no linking required
- **LZ4 compression**: Fast compression with excellent ratios (70-85% reduction)
- **xxHash64 checksums**: Fast, reliable data integrity validation (3-5x faster than CRC32)
- **Zero-Order Hold (ZoH)**: Extreme compression for constant/sparse data
- **Dual API**: Flexible runtime interface + static compile-time interface
- **Type safety**: Enforced types per column with compile-time or runtime validation
- **Crash recovery**: Read last complete row even from interrupted writes
- **Random access**: Efficient seeking for non-sequential reads

### Cross-Platform & Multi-Language

- **C++ API**: Modern C++20 header-only library
- **C API**: Shared library (.dll/.so) for language bindings
- **Python**: Full pandas integration via PyBCSV package
- **C# / Unity**: Game development integration with minimal GC impact
- **CLI Tools**: Professional csv2bcsv and bcsv2csv converters

### Performance Highlights

- **3.6M-7.5M rows/second** processing (8-33 columns, flexible/static API, Release build, Zen3)
- **127K+ rows/second** for 1000-channel wide datasets
- **40-60% compression** with LZ4 on real-world datasets
- **75-96% size reduction** vs CSV (ZoH for sparse data)
- **3-5x faster checksums** with xxHash64 vs CRC32
- **Constant-time writes**: No compression spikes (streaming mode)

---

## Project Status

**Current Version**: v1.2.0-dev (Active Development)

### Recent Changes (v1.2.0)
- ✅ **Replaced CRC32 with xxHash64** (3-5x faster checksums)
- ✅ **Removed Boost dependency** (zero external dependencies)
- ✅ **Upgraded to C++20** (modern concepts and features)
- ✅ **Improved test coverage** (59/59 tests passing)

### Roadmap

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed roadmap.

**Next milestones**:
- **v1.3.0** (Dec 2025): Streaming LZ4 compression (constant write latency)
- **v1.4.0** (Jan 2026): File indexing for fast random access
- **v1.5.0** (Feb 2026): Variable-length encoding (VLE) for better compression
- **v2.0.0** (Q2 2026): Stable release with compatibility guarantees

⚠️ **Development Notice**: Until v2.0.0, file formats may change between minor versions without migration paths. Use for experimentation and non-critical data storage.

---

## Documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md)**: Design decisions, performance requirements, technical details
- **[VERSIONING.md](VERSIONING.md)**: Automated versioning system
- **[examples/CLI_TOOLS.md](examples/CLI_TOOLS.md)**: CLI tool usage and examples
- **[examples/PERFORMANCE_COMPARISON.md](examples/PERFORMANCE_COMPARISON.md)**: Benchmark results
- **[python/README.md](python/README.md)**: Python package documentation
- **[unity/README.md](unity/README.md)**: Unity integration guide

---

## Supported Data Types

| Type | Size | Range/Precision | Use Case |
|------|------|-----------------|----------|
| `BOOL` | 1 byte | true/false | Flags, states |
| `UINT8` | 1 byte | 0-255 | Small counters, IDs |
| `UINT16` | 2 bytes | 0-65535 | Sensor IDs, small values |
| `UINT32` | 4 bytes | 0-4.3B | Timestamps, large counters |
| `UINT64` | 8 bytes | 0-18.4E | High-res timestamps |
| `INT8` | 1 byte | -128 to 127 | Small signed values |
| `INT16` | 2 bytes | -32K to 32K | Temperature readings |
| `INT32` | 4 bytes | -2.1B to 2.1B | Standard integers |
| `INT64` | 8 bytes | Large range | 64-bit signed data |
| `FLOAT` | 4 bytes | ±3.4E±38, 7 digits | Sensor values |
| `DOUBLE` | 8 bytes | ±1.7E±308, 15 digits | High-precision measurements |
| `STRING` | Variable | UTF-8 text | Labels, descriptions |

**Automatic type optimization** in CLI tools minimizes storage by selecting smallest appropriate type.

---

## Building

### Prerequisites

- **C++20 compiler**: GCC 10+, Clang 12+, MSVC 2019+
- **CMake 3.20+**
- **Git** (for dependency fetching)

### Build Commands

```bash
# Configure (Release recommended for performance)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build -j

# Run tests
./build/bin/bcsv_gtest

# Run examples
./build/bin/example
./build/bin/performance_benchmark

# Use CLI tools
./build/bin/csv2bcsv data.csv
./build/bin/bcsv2csv data.bcsv output.csv
```

### Python Package

```bash
cd python/
pip install -e .  # Development mode
# or
pip install .     # Standard install
```

### Integration Patterns

**CMake Project**:
```cmake
add_subdirectory(external/bcsv)
target_link_libraries(your_target PRIVATE bcsv)
```

**Header-only (manual)**:
```cpp
#include "bcsv/bcsv.h"
// No linking needed, just include path
```

---

## Usage Examples

### CLI Tools

```bash
# Convert CSV to BCSV (auto-detect format)
csv2bcsv measurements.csv

# European CSV (semicolon delimiter, comma decimal)
csv2bcsv -d ';' --decimal-separator ',' data.csv

# Convert back to CSV
bcsv2csv measurements.bcsv output.csv
```

### Python

```python
import pybcsv
import pandas as pd

# Pandas integration
df = pd.DataFrame({'time': [1, 2, 3], 'value': [10.5, 20.3, 15.7]})
pybcsv.write_dataframe(df, "data.bcsv")

df_read = pybcsv.read_dataframe("data.bcsv")
print(df_read)
```

### C# / Unity

```csharp
using BCSV;

var layout = new BcsvLayout();
layout.AddColumn("timestamp", BcsvColumnType.DOUBLE);
layout.AddColumn("position_x", BcsvColumnType.FLOAT);

var writer = new BcsvWriter(layout);
writer.Open("player_data.bcsv");

var row = writer.GetRow();
row.SetDouble(0, Time.time);
row.SetFloat(1, transform.position.x);
writer.WriteRow();
writer.Close();
```

---

## API Design

### Flexible Interface (Runtime Schema)

Best for dynamic schemas, prototyping, varying structures:

```cpp
bcsv::Layout layout;
layout.addColumn({"id", bcsv::ColumnType::INT32});
layout.addColumn({"name", bcsv::ColumnType::STRING});

bcsv::Writer<bcsv::Layout> writer(layout);
writer.open("data.bcsv", true);
writer.row().set(0, int32_t{123});
writer.row().set(1, std::string{"Alice"});
writer.writeRow();
```

### Static Interface (Compile-Time Schema)

Best for known schemas, performance-critical code (4-5x faster):

```cpp
using MyLayout = bcsv::LayoutStatic<int32_t, std::string>;
auto layout = MyLayout::create({"id", "name"});

bcsv::Writer<MyLayout> writer(layout);
writer.open("data.bcsv", true);
writer.row().set<0>(int32_t{123});      // Type-safe compile-time index
writer.row().set<1>(std::string{"Alice"});
writer.writeRow();
```

---

## Performance

### Benchmarks (Zen3 CPU, Release Build)

| Operation | Speed | Details |
|-----------|-------|---------|
| **Sequential write (static)** | 4.7M rows/sec | 8-column dataset |
| **Sequential read (static)** | 7.5M rows/sec | 8-column dataset |
| **Sequential write (flexible)** | 3.6M rows/sec | 8-column dataset |
| **Sequential read (flexible)** | 2.3M rows/sec | 8-column dataset |
| **1000-channel stream** | 127K rows/sec | Wide dataset (1000 columns) |
| **CSV → BCSV** | 130K rows/sec | Auto type optimization |
| **BCSV → CSV** | 220K rows/sec | Round-trip conversion |
| **Compression ratio** | 15-25% | Typical vs CSV |
| **ZoH compression** | 3-4% | Sparse/constant data |

### Real-World Example

**Dataset**: 105K rows × 33 columns (time-series data)
- CSV size: 105 MB
- BCSV size: 16.3 MB (84% reduction)
- Processing: 127K rows/second
- Type optimization: 13 FLOAT + 19 DOUBLE + 1 UINT32 (vs 33 STRING default)

---

## Dependencies

- **LZ4**: Embedded in repository (v1.10.0) or system version
- **xxHash**: Embedded in repository (v0.8.3)
- **C++20 Standard Library**: No external runtime dependencies

**Optional** (build/test only):
- **Google Test**: Auto-fetched by CMake for testing
- **Python**: For PyBCSV package
- **pybind11**: Auto-fetched for Python bindings

---

## License

MIT License - Copyright (c) 2025 Tobias Weber

See [LICENSE](LICENSE) file for full details.

---

## Contributing

Contributions welcome! Please:

1. **Open an issue** for bugs or feature requests
2. **Include benchmarks** for performance-related changes
3. **Add tests** for new features
4. **Update documentation** for API changes

---

## Project Structure

```
bcsv/
├── include/bcsv/          # Header-only library (copy this to integrate)
├── examples/              # Usage examples and CLI tools
├── tests/                 # Comprehensive test suite
├── python/                # Python package (PyBCSV)
├── unity/                 # C# Unity integration
├── cmake/                 # Build system utilities
├── CMakeLists.txt         # Main build configuration
├── README.md              # This file
├── ARCHITECTURE.md        # Design and requirements
└── VERSIONING.md          # Version management
```

---

**Ready to get started?** Check out the [examples/](examples/) directory or dive into [ARCHITECTURE.md](ARCHITECTURE.md) for technical details.
