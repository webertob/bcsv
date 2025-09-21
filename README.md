# BCSV - Binary CSV Library

A production-ready header-only C++17 library for reading and writing binary CSV files with LZ4 compression, CRC32 validation, and advanced optimization features.

## Features

- **High Performance**: Up to 13x faster with O3 optimization, processes 127K+ rows/second
- **Excellent Compression**: 40.4% compression ratio with LZ4, typically 75-85% size reduction vs raw CSV
- **Dual API Design**: Both flexible runtime interface and static compile-time interface
- **Advanced CLI Tools**: Professional csv2bcsv and bcsv2csv conversion utilities with auto-detection
- **Type Safety**: Full std::variant-based type system with automatic optimization
- **International Support**: Decimal separator configuration for European CSV formats
- **Production Ready**: CRC32 validation, packet-based architecture, robust error handling
- **Header-only**: Easy integration with CMake and modern C++17 projects

## Project Structure

```
bcsv/
├── include/
│   └── bcsv/
│       ├── bcsv.h           # Main header including all components
│       ├── definitions.h    # Core constants and type definitions
│       ├── file_header.h    # Binary file header management
│       ├── layout.h         # Column layout and schema management
│       ├── row.h           # Individual data rows (flexible and static)
│       ├── packet_header.h  # Packet-based compression management
│       ├── reader.h        # Template-based file reading
│       └── writer.h        # Template-based file writing
├── examples/
│   ├── example.cpp         # Flexible interface demo
│   ├── example_static.cpp  # Static interface demo
│   ├── performance_benchmark.cpp # Performance comparison
│   ├── csv2bcsv.cpp       # Professional CSV→BCSV converter
│   ├── bcsv2csv.cpp       # Professional BCSV→CSV converter
│   └── CMakeLists.txt
├── tests/
│   ├── bcsv_gtest.cpp     # Comprehensive Google Test suite
│   └── CMakeLists.txt
├── build/                 # Build output directory
├── CMakeLists.txt         # CMake configuration with O3 optimization
└── .vscode/              # VS Code configuration
```

## API Design

BCSV provides two complementary interfaces for different use cases:

### Flexible Interface (Runtime Schema)
**Best for**: Dynamic schemas, prototyping, data exploration, varying column structures

```cpp
#include <bcsv/bcsv.h>

// Create layout at runtime
auto layout = bcsv::Layout::create();
layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
layout->insertColumn({"score", bcsv::ColumnDataType::FLOAT});

// Write data
bcsv::Writer<bcsv::Layout> writer(layout, "data.bcsv", true);
auto row = layout->createRow();
(*row).set(0, int32_t{123});          // Note: (*row).set() syntax
(*row).set(1, std::string{"Alice"});
(*row).set(2, 95.5f);
writer.writeRow(*row);

// Read data
bcsv::Reader<bcsv::Layout> reader(layout, "data.bcsv");
bcsv::RowView rowView(layout);
while (reader.readRow(rowView)) {
    auto id = rowView.get<int32_t>(0);
    auto name = rowView.get<std::string>(1);
    auto score = rowView.get<float>(2);
    // Process data...
}
```

### Static Interface (Compile-time Schema)
**Best for**: Known schemas, performance-critical applications, type safety, large datasets

```cpp
#include <bcsv/bcsv.h>

// Define layout type at compile time
using EmployeeLayout = bcsv::LayoutStatic<int32_t, std::string, float>;

// Create with column names
auto layout = EmployeeLayout::create({"id", "name", "score"});

// Write data (4-5x faster than flexible interface)
bcsv::Writer<EmployeeLayout> writer(layout, "data.bcsv", true);
auto row = layout->createRow();
(*row).set<0>(int32_t{123});        // Template index - type safe
(*row).set<1>(std::string{"Alice"});
(*row).set<2>(95.5f);
writer.writeRow(*row);

// Read data
bcsv::Reader<EmployeeLayout> reader(layout, "data.bcsv");
typename EmployeeLayout::RowViewType rowView(layout);
while (reader.readRow(rowView)) {
    auto id = rowView.get<0>();      // Type automatically inferred
    auto name = rowView.get<1>();
    auto score = rowView.get<2>();
    // Process data...
}
```

## CLI Tools

BCSV includes professional-grade command-line conversion tools:

### csv2bcsv - Advanced CSV to BCSV Converter

```bash
# Auto-detect everything
csv2bcsv data.csv

# Specify options for European CSV format
csv2bcsv -d ';' --decimal-separator ',' -v european_data.csv

# Full option set
csv2bcsv --delimiter ',' --quote '"' --decimal-separator '.' --no-header data.csv output.bcsv
```

**Features:**
- **Automatic delimiter detection** (comma, semicolon, tab, pipe)
- **Aggressive type optimization** (UINT8→INT64, FLOAT→DOUBLE based on data analysis)
- **Decimal separator support** for German/European formats (comma vs point)
- **Character conflict validation** (prevents delimiter/quote/decimal conflicts)
- **Progress reporting** for large files (>10K rows)
- **Comprehensive error handling** with detailed validation

### bcsv2csv - BCSV to CSV Converter

```bash
# Convert back to CSV
bcsv2csv data.bcsv output.csv

# Custom delimiter and quoting
bcsv2csv -d ';' --quote-all data.bcsv european_output.csv
```

**Features:**
- **Perfect round-trip conversion** (data integrity guaranteed)
- **Custom delimiters and quoting** options
- **CSV escaping compliance** (RFC 4180 compatible)
- **Header inclusion control**

### Performance Characteristics

| Dataset Size | csv2bcsv Speed | Compression Ratio | bcsv2csv Speed |
|-------------|---------------|------------------|---------------|
| 10K rows | 45K rows/sec | 75-85% reduction | 85K rows/sec |
| 100K rows | 127K rows/sec | 75-85% reduction | 200K rows/sec |
| 1M+ rows | 130K+ rows/sec | 75-85% reduction | 220K+ rows/sec |

*With O3 optimization enabled*

## Supported Data Types

BCSV supports a comprehensive set of data types with automatic optimization:

| Type | Description | Size | Auto-Detection |
|------|-------------|------|----------------|
| `BOOL` | Boolean value | 1 byte | true/false, 1/0, yes/no |
| `UINT8` | 8-bit unsigned integer | 1 byte | 0-255 range |
| `UINT16` | 16-bit unsigned integer | 2 bytes | 256-65535 range |
| `UINT32` | 32-bit unsigned integer | 4 bytes | Large positive integers |
| `UINT64` | 64-bit unsigned integer | 8 bytes | Very large positive integers |
| `INT8` | 8-bit signed integer | 1 byte | -128 to 127 range |
| `INT16` | 16-bit signed integer | 2 bytes | -32768 to 32767 range |
| `INT32` | 32-bit signed integer | 4 bytes | Standard signed integers |
| `INT64` | 64-bit signed integer | 8 bytes | Large signed integers |
| `FLOAT` | 32-bit floating point | 4 bytes | Decimals with ≤7 digits precision |
| `DOUBLE` | 64-bit floating point | 8 bytes | High-precision decimals |
| `STRING` | Variable-length string | Variable | All other text data |

### Automatic Type Optimization

The csv2bcsv tool performs **aggressive type optimization** to minimize file size:

```bash
# Input CSV data analysis:
"123"     → UINT8  (1 byte instead of 8)
"65000"   → UINT16 (2 bytes instead of 8) 
"-1234"   → INT16  (2 bytes instead of 8)
"3.14"    → FLOAT  (4 bytes instead of 8)
"3.141592653589793" → DOUBLE (8 bytes, high precision needed)
```

**Space savings example**: A column with values 0-255 uses UINT8 (1 byte/value) instead of defaulting to INT64 (8 bytes/value), **87.5% space reduction** for that column.

## Binary File Format (Version 1.0)

BCSV uses a packet-based architecture with mandatory compression and validation:

### File Structure
```
[File Header]     - Magic, version, column definitions
[Packet Header]   - LZ4 compressed data packet metadata
[Compressed Data] - LZ4-compressed row data with CRC32 validation
[Packet Header]   - Next packet...
[Compressed Data] - ...continues for all data
```

### File Header (v1.0)
```
Offset | Size | Field           | Description
-------|------|-----------------|----------------------------------
0      | 4    | Magic           | 0x56534342 ("BCSV" little-endian)
4      | 1    | Version Major   | 1 (current version)
5      | 1    | Version Minor   | 0
6      | 1    | Version Patch   | 0
7      | 1    | Compression     | LZ4 level (always enabled in v1.0)
8      | 2    | Flags           | Reserved for future features
10     | 2    | Column Count    | Number of columns (max 65535)
12     | 2*N  | Column Types    | Data type ID for each column
12+2*N | 2*N  | Name Lengths    | Length of each column name
Var    | Var  | Column Names    | Null-terminated column names
```

### Packet-Based Architecture
- **Optimal packet size**: 64KB uncompressed (best compression ratio)
- **LZ4 compression**: Fast compression with excellent ratios (40-60%)
- **CRC32 validation**: Every packet has integrity checking
- **Random access**: Packet boundaries allow efficient seeking

### Version 1.0 Mandatory Features
All BCSV v1.0 files include:
- ✅ **LZ4 Compression**: Automatic, configurable level
- ✅ **CRC32 Validation**: Data integrity checking  
- ✅ **Packet Structure**: 64KB optimal packet size
- ✅ **Type Optimization**: Automatic size optimization
- ✅ **Standard Compliance**: RFC-compatible CSV parsing

## Building

### Prerequisites
- **C++17 compatible compiler** (MSVC 2019+, GCC 9+, Clang 10+)
- **CMake 3.20+**
- **Git** (for automatic LZ4 and Google Test fetching)

### Quick Start

```bash
# Configure with Release optimization (recommended)
cmake -B build -S . --preset=default

# Build everything (fast parallel build)
cmake --build build -j

# Run comprehensive test suite
.\build\bin\Release\bcsv_gtest.exe

# Run examples
.\build\bin\Release\example.exe
.\build\bin\Release\example_static.exe
.\build\bin\Release\performance_benchmark.exe

# Use CLI tools
.\build\bin\Release\csv2bcsv.exe --help
.\build\bin\Release\bcsv2csv.exe --help
```

### VS Code Integration

Optimized VS Code configuration included:

1. **Extensions**: C/C++ and CMake Tools auto-configured
2. **Build Tasks**: 
   - `Ctrl+Shift+P` → "Tasks: Run Task" → "Build All"
   - `Ctrl+Shift+P` → "Tasks: Run Task" → "Build Example"
3. **Debugging**: F5 for any target
4. **Testing**: 
   - `Ctrl+Shift+P` → "Tasks: Run Task" → "Run Tests"

### Performance Optimization

For **production use**, always build with Release configuration:

```bash
# 13x performance improvement with O3 optimization
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Performance results:
# Debug:   ~10K rows/second  
# Release: ~127K rows/second (13x faster!)
```

## Real-World Performance

### Production Dataset Analysis

Analysis of `0003_250904_41_4_3.csv` (105K rows, 33 columns):
- **Input CSV**: ~105 MB
- **BCSV v1.0**: 16.3 MB (84% reduction)
- **Processing speed**: 127K rows/second
- **Type optimization**: 13 FLOAT + 19 DOUBLE + 1 UINT32 (vs 33 STRING default)

### Compression Comparison

| Format | Size | vs CSV | vs BCSV |
|--------|------|--------|---------|
| Raw CSV | 105 MB | - | 643% larger |
| ZIP CSV | 23 MB | 78% reduction | 41% larger |
| **BCSV v1.0** | **16.3 MB** | **84% reduction** | **Baseline** |
| BCSV + Zero-Order-Hold* | 3-4 MB | 96% reduction | 75% smaller |

*Estimated for datasets with high temporal correlation

### When to Use BCSV

✅ **Excellent for:**
- **Large datasets** (>10K rows): Maximum performance benefit
- **Numeric data**: Best compression with type optimization  
- **Repeated processing**: Read performance gains compound
- **Storage optimization**: 75-85% space reduction typical
- **Data pipelines**: Fast conversion tools for integration

⚠️ **Consider alternatives for:**
- Small datasets (<1K rows): CSV overhead minimal
- One-time use: Conversion time may not be worth it
- Text-heavy data: Compression less effective
- Legacy systems: May need CSV compatibility

## Core Classes

### Layout Management
- **`Layout`**: Runtime flexible column schema management
- **`LayoutStatic<Types...>`**: Compile-time static schema with template optimization

### Data Access  
- **`Row`**: Flexible row data container with dynamic typing
- **`RowStatic<Types...>`**: Static row with compile-time type safety
- **`RowView`**: Read-only view for efficient data access

### File I/O
- **`Reader<LayoutType>`**: Template-based file reading with type safety
- **`Writer<LayoutType>`**: Template-based file writing with compression
- **`FileHeader`**: Binary file metadata and validation

## Dependencies

- **LZ4**: Automatically fetched via CMake FetchContent
- **Google Test**: Automatically fetched for testing (optional)
- **No runtime dependencies**: Header-only library with static linking

## Version History

- **v1.0.0**: Production release with LZ4 compression, CRC32 validation, CLI tools
- **v0.x**: Development versions (not recommended for production)

## License

MIT License

Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See the [LICENSE](LICENSE) file for full details.

## Contributing

1. **Issues**: Report bugs or request features via GitHub Issues
2. **Performance**: Include benchmark data with performance reports  
3. **Testing**: Add test cases for new features
4. **Documentation**: Update README and examples for API changes

---

**Ready to get started?** Try the [Quick Start](#building) guide or explore the [examples directory](examples/) for comprehensive usage patterns.
