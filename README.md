# BCSV - Binary CSV Library

A modular header-only C++ library for reading and writing binary CSV files with optional LZ4 compression support.

## Features

- **Modular header-only design** with separate files for each component
- **Binary file format** with controlled memory layout for direct I/O
- **Magic number validation** (0x56534342 = "BCSV")
- **Structured file header** with version, compression, column type and column name information
- **Column name storage** with efficient length-prefixed format (no null terminators)
- Type-safe field access using std::variant
- Modern C++17 design
- Cross-platform support
- Future LZ4 compression support (infrastructure prepared)

## Project Structure

```
bcsv/
├── include/
│   └── bcsv/
│       ├── bcsv.hpp          # Main header including all components
│       ├── file_header.hpp   # Binary file header management
│       ├── header.hpp        # Column definitions and metadata
│       ├── row.hpp          # Individual data rows
│       ├── packet.hpp       # Data packet abstraction
│       ├── reader.hpp       # Template-based file reading
│       └── writer.hpp       # Template-based file writing
├── examples/
│   ├── example.cpp       # Example usage program
│   └── CMakeLists.txt
├── tests/
│   ├── basic_test.cpp    # Basic functionality tests
│   └── CMakeLists.txt
├── build/                # Build output directory
├── CMakeLists.txt        # CMake configuration
└── .vscode/             # VS Code configuration
```

## Modular Design

The library is now organized into separate, focused header files:

### Core Components

- **`bcsv.hpp`** - Main header that includes all components
- **`file_header.hpp`** - FileHeader class for binary file metadata
- **`header.hpp`** - Header class for column definitions  
- **`row.hpp`** - Row class for individual data records
- **`packet.hpp`** - Packet class for data abstraction
- **`reader.hpp`** - Reader template class for file input
- **`writer.hpp`** - Writer template class for file output

### Usage Options

You can include the entire library:
```cpp
#include <bcsv/bcsv.hpp>  // Includes all components
```

Or include specific components:
```cpp
#include <bcsv/header.hpp>      // Just Header class
#include <bcsv/file_header.hpp> // Just FileHeader class
#include <bcsv/row.hpp>         // Just Row class
// etc.
```

## Supported Data Types

The BCSV library supports a comprehensive set of data types, all stored efficiently with controlled memory layout:

| Type | Description | Size | Enum Value |
|------|-------------|------|------------|
| `BOOL` | Boolean value | 1 byte | 0x0001 |
| `UINT8` | 8-bit unsigned integer | 1 byte | 0x0002 |
| `UINT16` | 16-bit unsigned integer | 2 bytes | 0x0003 |
| `UINT32` | 32-bit unsigned integer | 4 bytes | 0x0004 |
| `UINT64` | 64-bit unsigned integer | 8 bytes | 0x0005 |
| `INT8` | 8-bit signed integer | 1 byte | 0x0006 |
| `INT16` | 16-bit signed integer | 2 bytes | 0x0007 |
| `INT32` | 32-bit signed integer | 4 bytes | 0x0008 |
| `INT64` | 64-bit signed integer | 8 bytes | 0x0009 |
| `FLOAT` | 32-bit floating point | 4 bytes | 0x000A |
| `DOUBLE` | 64-bit floating point | 8 bytes | 0x000B |
| `STRING` | Variable-length string | Variable | 0x000C |

### Type-Safe Access

All data types are accessible through std::variant-based type-safe access:

```cpp
bcsv::Row row(header);
row.setValue("age", static_cast<uint8_t>(25));
row.setValue("score", static_cast<int16_t>(-1234));
row.setValue("temperature", 23.5f);
row.setValue("precise_value", 3.141592653589793);

// Type-safe retrieval
uint8_t age = std::get<uint8_t>(row.getValue("age"));
int16_t score = std::get<int16_t>(row.getValue("score"));
float temp = std::get<float>(row.getValue("temperature"));
double precise = std::get<double>(row.getValue("precise_value"));
```

## Binary File Format

The BCSV binary format provides efficient storage with controlled memory layout:

### File Header Structure
```
[12 bytes] Fixed Header:
  - 4 bytes: Magic number (0x56534342 = "BCSV")
  - 1 byte:  Major version
  - 1 byte:  Minor version
  - 1 byte:  Patch version
  - 1 byte:  Compression level (0-9, 0=none)
  - 2 bytes: Feature flags
  - 2 bytes: Column count (N)

[N×2 bytes] Column Data Types:
  - 2 bytes per column: ColumnDataType enum value

[N×2 bytes] Column Name Lengths:
  - 2 bytes per column: Length of column name (0-65535)

[Variable] Column Names:
  - Variable length: Column names without null terminators
  - Length determined by preceding length values
```

### Column Name Management

```cpp
bcsv::FileHeader header;
header.setColumnCount(3);

// Set column types and names
header.setColumnDataType(0, bcsv::ColumnDataType::STRING);
header.setColumnName(0, "customer_name");

header.setColumnDataType(1, bcsv::ColumnDataType::UINT32);
header.setColumnName(1, "order_id");

header.setColumnDataType(2, bcsv::ColumnDataType::DOUBLE);
header.setColumnName(2, "total_amount");

// Binary I/O preserves all information
std::ofstream file("data.bcsv", std::ios::binary);
header.writeToBinary(file);
```

## Building

This project uses CMake for building. The library itself is header-only, but examples and tests need to be compiled.

### Prerequisites

- C++17 compatible compiler (MSVC, GCC, or Clang)
- CMake 3.20 or higher
- Git (for future LZ4 integration)

### Quick Start

```powershell
# In VS Code terminal (PowerShell)
# Configure the project
cmake -B build -S .

# Build examples and tests
cmake --build build --config Debug

# Run tests
.\build\bin\Debug\basic_test.exe

# Run example
.\build\bin\Debug\example.exe
```

### Using VS Code

The project includes VS Code configuration for easy development:

1. Open the project folder in VS Code
2. Install the C/C++ and CMake Tools extensions
3. Use Ctrl+Shift+P and run "CMake: Configure" 
4. Use Ctrl+Shift+P and run "CMake: Build"
5. Use F5 to debug the example or tests

## Usage

```cpp
#include <bcsv/bcsv.hpp>

// Create a header
bcsv::Header header;
header.addField("name", "string");
header.addField("age", "int");
header.addField("salary", "double");

// Create rows
bcsv::Row row(header);
row.setValue("name", std::string("John Doe"));
row.setValue("age", static_cast<int64_t>(30));
row.setValue("salary", 75000.50);

// Write to file
bcsv::Writer<> writer("data.bcsv");
writer.writeHeader(header);
writer.writeRow(row);

// Read from file
bcsv::Reader<> reader("data.bcsv");
reader.readHeader();
bcsv::Row readRow;
while(reader.readRow(readRow)) {
    auto name = std::get<std::string>(readRow.getValue("name"));
    // Process row...
}
```

## Classes

- **FileHeader**: File metadata with controlled binary layout for direct I/O
  - 4-byte magic number (0x56534342 = "BCSV")
  - 3-byte version (major.minor.patch)
  - 1-byte compression level
  - 2-byte flags for features
  - 2-byte column count
  - 2 bytes per column for data type information
- **Packet**: Represents a data packet (header, row, or metadata)
- **Header**: Defines column names and types
- **Row**: Contains field values for a single record
- **Reader**: Template class for reading BCSV files
- **Writer**: Template class for writing BCSV files

## Binary File Format

The BCSV file format uses a structured binary layout:

### File Header (12 + 2*N bytes)
```
Offset | Size | Field           | Description
-------|------|-----------------|----------------------------------
0      | 4    | Magic           | 0x56534342 ("BCSV" in little-endian)
4      | 1    | Version Major   | Major version number
5      | 1    | Version Minor   | Minor version number  
6      | 1    | Version Patch   | Patch version number
7      | 1    | Compression     | Compression level (0-9, 0=none)
8      | 2    | Flags           | Feature flags (bit field)
10     | 2    | Column Count    | Number of columns (max 65535)
12     | 2*N  | Column Types    | Data type ID for each column (2 bytes each)
```

### Column Data Types
- `0x0001`: STRING
- `0x0002`: INT64  
- `0x0003`: DOUBLE
- `0x0004`: BOOL
- `0x0005-0x0008`: Reserved for future use

## Dependencies

- Currently no external dependencies
- TODO: Add LZ4 library for compression support

## Future Enhancements

- [ ] Implement LZ4 compression/decompression 
- [ ] Add proper binary file I/O implementation
- [ ] Add more comprehensive test suite
- [ ] Add benchmarking
- [ ] Add documentation generation

## License

MIT License (placeholder - adjust as needed)
