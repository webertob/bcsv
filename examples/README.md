# BCSV Examples

This directory contains eleven examples demonstrating different aspects of the BCSV library:

## Examples Overview

### 1. `quickstart.cpp` — Quick Start
Minimal write-and-read example (~30 lines). Start here.

### 2. `example.cpp` — Flexible Interface Demo
Demonstrates the runtime flexible `Layout`/`Row` interface for basic BCSV write and read.

### 3. `example_static.cpp` — Static Interface Demo
Demonstrates the compile-time `LayoutStatic<Types...>`/`RowStatic` interface for type-safe, performance-optimized usage.

### 4. `example_zoh.cpp` / `example_zoh_static.cpp` — Zero-Order Hold Demos
ZoH compression (only stores values that change between rows), for both flexible and static layouts.

### 5. `example_delta.cpp` — Delta Encoding
`WriterDelta` for time-series data with small consecutive differences. Shows how delta + VLE encoding compresses slow-changing numeric columns.

### 6. `example_direct_access.cpp` — Random-Access Reader
`ReaderDirectAccess` for O(log P) random row access. Demonstrates `read(index)`, `rowCount()`, and packet metadata.

### 7. `example_error_handling.cpp` — Error Handling
Illustrates I/O error patterns (bool return + `getErrorMsg()`) and logic-error exceptions.

### 8. `visitor_examples.cpp` — Visitor Pattern
Demonstrates const and mutable visitor patterns for iterating over row columns without knowing types at compile time.

### 9. `c_api_vectorized_example.c` — C API Vectorized Read
Shows how to use the C API with vectorized (batch) row access.

### 10. `example_sampler.cpp` — Sampler API
Demonstrates the Sampler API for expression-based row filtering and column projection.

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
