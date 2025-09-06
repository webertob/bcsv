# BCSV Examples

This directory contains three essential examples demonstrating different aspects of the BCSV (Binary CSV) library:

## Examples Overview

### 1. `example.cpp` - Flexible Interface Demo
**Purpose**: Demonstrates the runtime flexible Layout/Row interface for basic BCSV usage.

**Key Features**:
- Runtime-defined column schemas using `bcsv::Layout`
- Dynamic column addition with `insertColumn()`
- Row creation and data population using `(*row).set(index, value)`
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

### 3. `performance_benchmark.cpp` - Performance Comparison
**Purpose**: Comprehensive performance benchmarking comparing flexible vs static interfaces with large datasets.

**Key Features**:
- Benchmarks both flexible and static interfaces
- Processes 100,000 rows with 8 mixed-type columns
- Measures write and read performance separately
- Reports throughput, speedup ratios, and file size comparison
- Binary format compatibility verification
- Realistic data generation with random values and strings

**Usage**:
```bash
.\build\bin\Debug\performance_benchmark.exe
```

**Typical Results**:
- Static interface is ~4-5x faster overall
- Write operations show the largest performance difference
- Files are binary-compatible between interfaces
- Throughput: Static ~220K rows/sec vs Flexible ~50K rows/sec

## Building the Examples

All examples are built automatically when building the project:

```bash
# Configure CMake
cmake -B build -S .

# Build all examples
cmake --build build --target example
cmake --build build --target example_static  
cmake --build build --target performance_benchmark

# Or build all at once
cmake --build build
```

## Example Data Structure

All examples use the same basic data structure for consistency:

| Column | Type | Description |
|--------|------|-------------|
| id | int32_t | Unique identifier |
| name | string | Person's name |
| score | float | Performance score |
| active | bool | Active status |

The performance benchmark extends this to 8 columns with additional types (double, int64_t, uint32_t, additional string).

## API Usage Patterns

### Flexible Interface Pattern
```cpp
// Create layout
auto layout = bcsv::Layout::create();
layout->insertColumn({"name", bcsv::ColumnDataType::TYPE});

// Write data
bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
auto row = layout->createRow();
(*row).set(index, value);  // Note: (*row) not row->
writer.writeRow(*row);

// Read data
bcsv::Reader<bcsv::Layout> reader(layout, filename);
bcsv::RowView rowView(layout);
while (reader.readRow(rowView)) {
    auto value = rowView.get<Type>(index);
}
```

### Static Interface Pattern
```cpp
// Define layout type
using MyLayout = bcsv::LayoutStatic<int32_t, std::string, float>;

// Write data
auto layout = MyLayout::create(columnNames);
bcsv::Writer<MyLayout> writer(layout, filename, true);
auto row = layout->createRow();
(*row).set<0>(value);  // Template index, type-safe
writer.writeRow(*row);

// Read data
bcsv::Reader<MyLayout> reader(layout, filename);
typename MyLayout::RowViewType rowView(layout);
while (reader.readRow(rowView)) {
    auto value = rowView.get<0>();  // Template index
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

- **Heap Exceptions**: Ensure you use `(*row).set()` syntax, not `row->set()`
- **Compilation Errors**: Verify template parameters match data types exactly
- **Reading Issues**: Ensure read layout exactly matches write layout
- **Performance**: Use static interface for large datasets (>10K rows)

## Next Steps

After running these examples:
1. Try modifying the data structures to match your use case
2. Experiment with different column types (int8_t, uint64_t, double, etc.)
3. Test with your actual data volumes using the performance benchmark as a template
4. Consider compression options if file size is a concern
