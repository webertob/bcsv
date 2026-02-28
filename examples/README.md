# BCSV Examples

This directory contains three essential examples demonstrating different aspects of the BCSV (Binary CSV) library:

## Examples Overview

### 1. `example.cpp` - Flexible Interface Demo
**Purpose**: Demonstrates the runtime flexible Layout/Row interface for basic BCSV usage.

**Key Features**:
- Runtime-defined column schemas using `bcsv::Layout`
- Dynamic column addition with `addColumn()`
- Row data population using `row.set(index, value)`
- Sequential read/write operations
- Ideal for scenarios where data structure is not known at compile time

**Usage**:
```bash
.\build\bin\Debug\example.exe
```

**Output**: Creates `example_flexible.bcsv` with sample employee data (ID, Name, Score, Active status).

### 2. `example_static.cpp` - Static Interface Demo
**Purpose**: Demonstrates the compile-time static LayoutStatic/RowStatic interface for performance-optimized BCSV usage.

**Key Features**:
- Compile-time-defined schemas using `bcsv::LayoutStatic<Types...>`
- Template-based type-safe column access with `(*row).set<N>(value)`
- Better performance through compile-time optimization
- Type safety and template specialization
- Ideal for scenarios with known, fixed data structures

**Usage**:
```bash
.\build\bin\Debug\example_static.exe
```

**Output**: Creates `example_static.bcsv` with the same employee data structure as the flexible example.

### 3. `example_zoh.cpp` / `example_zoh_static.cpp` - Zero-Order Hold Demos
**Purpose**: Demonstrates ZoH (Zero-Order Hold) compression, which only stores values that change between rows.

**Key Features**:
- Flexible and static ZoH writer variants
- Significant compression for slowly-changing data
- Binary-compatible output with standard readers

**Usage**:
```bash
./build/bin/example_zoh
./build/bin/example_zoh_static
```

## Building the Examples

All examples are built automatically when building the project:

```bash
# Configure CMake
cmake -B build -S .

# Build all examples (fast parallel build)
cmake --build build -j --target example
cmake --build build -j --target example_static

# Or build all at once
cmake --build build -j
```

## Example Data Structure

All examples use the same basic data structure for consistency:

| Column | Type | Description |
|--------|------|-------------|
| id | int32_t | Unique identifier |
| name | string | Person's name |
| score | float | Performance score |
| active | bool | Active status |

## API Usage Patterns

### Flexible Interface Pattern
```cpp
// Create layout
bcsv::Layout layout;
layout.addColumn({"name", bcsv::ColumnType::TYPE});

// Write data
bcsv::Writer<bcsv::Layout> writer(layout);
writer.open(filename, /*overwrite=*/true);
auto& row = writer.row();
row.set(index, value);
writer.writeRow();

// Read data
bcsv::Reader<bcsv::Layout> reader;
reader.open(filename);
while (reader.readNext()) {
    auto& row = reader.row();
    auto value = row.get<Type>(index);
}
```

### Static Interface Pattern
```cpp
// Define layout type
using MyLayout = bcsv::LayoutStatic<int32_t, std::string, float>;

// Write data
MyLayout layout(columnNames);
bcsv::Writer<MyLayout> writer(layout);
writer.open(filename, /*overwrite=*/true);
auto& row = writer.row();
row.set<0>(value);  // Template index, type-safe
writer.writeRow();

// Read data
bcsv::Reader<MyLayout> reader;
reader.open(filename);
while (reader.readNext()) {
    auto& row = reader.row();
    auto value = row.get<0>();  // Template index
}
```

## Performance Considerations

1. **Static Interface**: Choose when:
   - Data structure is known at compile time
   - Performance is critical
   - Type safety is important
   - Processing large datasets

2. **Flexible Interface**: Choose when:
   - Data structure varies at runtime
   - Prototyping or dynamic schemas
   - Schema flexibility is more important than performance
   - Smaller datasets

3. **File Compatibility**: Both interfaces produce binary-compatible files, allowing you to write with one interface and read with another.

## Troubleshooting

- **Compilation Errors**: Verify template parameters match data types exactly
- **Reading Issues**: Ensure read layout is compatible with write layout (use `isCompatible()`)
- **Performance**: Use static interface for large datasets (>10K rows)

## Next Steps

After running these examples:
1. Try modifying the data structures to match your use case
2. Experiment with different column types (int8_t, uint64_t, double, etc.)
3. Test with your actual data volumes
4. Consider ZoH compression for slowly-changing time-series data
5. Consider LZ4 compression options if file size is a concern
