# BCSV Interoperability Guide

This guide demonstrates cross-language interoperability and best practices for working with BCSV files across different APIs.

---

## Core Principle

**All BCSV APIs produce identical binary format.** Files written by any API can be read by any other API without conversion.

```
┌─────────┐
│ C++     │────┐
├─────────┤    │
│ C API   │────┼────► .bcsv file ◄────┬────┤ Python  │
├─────────┤    │      (binary)        │    ├─────────┤
│ Python  │────┘                      └────┤ C#      │
├─────────┤                                └─────────┘
│ C#      │
└─────────┘
   Write                                      Read
```

---

## Cross-Language Examples

### Example 1: C++ Writer → Python Reader

**Data acquisition in C++:**

```cpp
// sensor_recorder.cpp
#include <bcsv/bcsv.h>

int main() {
    bcsv::Layout layout;
    layout.addColumn({"timestamp", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"temperature", bcsv::ColumnType::FLOAT});
    layout.addColumn({"humidity", bcsv::ColumnType::FLOAT});
    layout.addColumn({"sensor_id", bcsv::ColumnType::STRING});
    
    bcsv::Writer<bcsv::Layout> writer(layout);
    writer.open("sensor_data.bcsv", true, 6);
    
    // Record 1000 samples
    for (int i = 0; i < 1000; i++) {
        writer.row().set(0, getCurrentTime());
        writer.row().set(1, readTemperature());
        writer.row().set(2, readHumidity());
        writer.row().set(3, std::string("SENSOR_01"));
        writer.writeRow();
    }
    
    writer.close();
    return 0;
}
```

**Analysis in Python:**

```python
# analyze_sensors.py
import pybcsv
import pandas as pd
import matplotlib.pyplot as plt

# Read C++-generated file
df = pybcsv.read_dataframe("sensor_data.bcsv")

# Pandas analysis
print(f"Temperature range: {df['temperature'].min():.1f} to {df['temperature'].max():.1f}°C")
print(f"Average humidity: {df['humidity'].mean():.1f}%")

# Plot
df.plot(x='timestamp', y=['temperature', 'humidity'], subplots=True)
plt.show()
```

---

### Example 2: Python Writer → C# Unity Reader

**ML model output in Python:**

```python
# train_model.py
import pybcsv
import pandas as pd

# Train model and generate predictions
predictions = model.predict(test_data)

# Save predictions to BCSV
df = pd.DataFrame({
    'player_id': test_data['player_id'],
    'predicted_score': predictions,
    'confidence': confidence_scores,
    'recommendation': recommendations
})

pybcsv.write_dataframe(df, "ml_predictions.bcsv", compression_level=6)
print(f"Saved {len(df)} predictions to ml_predictions.bcsv")
```

**Load in Unity game:**

```csharp
// PredictionLoader.cs
using UnityEngine;
using BCSV;

public class PredictionLoader : MonoBehaviour
{
    void LoadPredictions()
    {
        using (var reader = new BCSVReader())
        {
            if (!reader.Open("StreamingAssets/ml_predictions.bcsv"))
            {
                Debug.LogError("Failed to load predictions");
                return;
            }
            
            while (reader.ReadNext())
            {
                int playerId = reader.GetInt32(0);
                float score = reader.GetFloat(1);
                float confidence = reader.GetFloat(2);
                string recommendation = reader.GetString(3);
                
                ApplyPrediction(playerId, score, confidence, recommendation);
            }
            
            Debug.Log("Predictions loaded successfully");
        }
    }
}
```

---

### Example 3: C API Writer → C++ Reader

**Data logger in C:**

```c
// embedded_logger.c
#include <bcsv/bcsv_c_api.h>

void log_system_metrics(void) {
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "timestamp", BCSV_TYPE_UINT64);
    bcsv_layout_add_column(layout, 1, "cpu_usage", BCSV_TYPE_FLOAT);
    bcsv_layout_add_column(layout, 2, "memory_mb", BCSV_TYPE_UINT32);
    bcsv_layout_add_column(layout, 3, "error_code", BCSV_TYPE_INT32);
    
    bcsv_writer_t writer = bcsv_writer_create(layout);
    bcsv_writer_open(writer, "system_metrics.bcsv", true, 0, 0, BCSV_FLAG_NONE);
    
    for (int i = 0; i < 10000; i++) {
        bcsv_row_t row = bcsv_writer_row(writer);
        bcsv_row_set_uint64(row, 0, get_timestamp_us());
        bcsv_row_set_float(row, 1, get_cpu_usage());
        bcsv_row_set_uint32(row, 2, get_memory_usage_mb());
        bcsv_row_set_int32(row, 3, get_last_error());
        bcsv_writer_next(writer);
    }
    
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}
```

**Analysis tool in C++:**

```cpp
// metric_analyzer.cpp
#include <bcsv/bcsv.h>

int main() {
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open("system_metrics.bcsv")) {
        std::cerr << "Error: " << reader.getErrorMsg() << "\n";
        return 1;
    }
    
    double maxCpu = 0.0;
    uint32_t maxMemory = 0;
    int errorCount = 0;
    
    while (reader.readNext()) {
        auto cpu = reader.row().get<float>(1);
        auto memory = reader.row().get<uint32_t>(2);
        auto error = reader.row().get<int32_t>(3);
        
        maxCpu = std::max(maxCpu, static_cast<double>(cpu));
        maxMemory = std::max(maxMemory, memory);
        if (error != 0) errorCount++;
    }
    
    std::cout << "Max CPU: " << maxCpu << "%\n";
    std::cout << "Max Memory: " << maxMemory << " MB\n";
    std::cout << "Error count: " << errorCount << "\n";
    
    return 0;
}
```

---

## Type Compatibility

### Numeric Types

All numeric types are **bit-compatible** across languages:

| BCSV Type | C++ | C | Python | C# | Size | Endianness |
|-----------|-----|---|--------|-----|------|------------|
| BOOL | bool | uint8_t | bool | bool | 1 byte | N/A |
| UINT8 | uint8_t | uint8_t | int | byte | 1 byte | N/A |
| UINT16 | uint16_t | uint16_t | int | ushort | 2 bytes | Little |
| UINT32 | uint32_t | uint32_t | int | uint | 4 bytes | Little |
| UINT64 | uint64_t | uint64_t | int | ulong | 8 bytes | Little |
| INT8 | int8_t | int8_t | int | sbyte | 1 byte | N/A |
| INT16 | int16_t | int16_t | int | short | 2 bytes | Little |
| INT32 | int32_t | int32_t | int | int | 4 bytes | Little |
| INT64 | int64_t | int64_t | int | long | 8 bytes | Little |
| FLOAT | float | float | float | float | 4 bytes | IEEE 754 |
| DOUBLE | double | double | float | double | 8 bytes | IEEE 754 |

**All platforms use little-endian and IEEE 754 floating point.**

### String Types

Strings are stored as **UTF-8 encoded byte sequences** with length prefix:

- **C++**: `std::string` (UTF-8)
- **C**: `char*` (null-terminated UTF-8)
- **Python**: `str` (automatically UTF-8)
- **C#**: `string` (converted to/from UTF-8)

**Cross-language string example:**

```cpp
// C++: Write emoji and Unicode
writer.row().set(0, std::string("Hello 世界 🌍"));
```

```python
# Python: Read Unicode perfectly
text = reader.row()[0]
print(text)  # "Hello 世界 🌍"
```

```csharp
// C#: Read Unicode perfectly
string text = reader.GetString(0);
Debug.Log(text);  // "Hello 世界 🌍"
```

---

## Layout Compatibility

### Schema Must Match

All APIs require **identical layouts** (same column count, types, and order):

✅ **Compatible:**
```python
# Python writer
layout.add_column("id", ColumnType.INT32)
layout.add_column("name", ColumnType.STRING)
```

```cpp
// C++ reader
bcsv::Layout layout;
layout.addColumn({"id", bcsv::ColumnType::INT32});
layout.addColumn({"name", bcsv::ColumnType::STRING});
```

❌ **Incompatible:**
```python
# Python writer (INT32, STRING)
layout.add_column("id", ColumnType.INT32)
layout.add_column("name", ColumnType.STRING)
```

```cpp
// C++ reader (INT32, INT32) - Type mismatch!
bcsv::Layout layout;
layout.addColumn({"id", bcsv::ColumnType::INT32});
layout.addColumn({"age", bcsv::ColumnType::INT32});  // ❌ Wrong type
```

### Runtime Validation

All APIs validate layout compatibility at `open()`:

```cpp
// C++
if (!reader.open("data.bcsv")) {
    std::cerr << reader.getErrorMsg() << "\n";
    // "Column type mismatch at index 1. Expected STRING, got INT32"
}
```

```python
# Python
try:
    reader.open("data.bcsv")
except RuntimeError as e:
    print(e)  # "Column type mismatch..."
```

---

## Compression Compatibility

All APIs support **identical LZ4 compression levels** (0-9):

```cpp
// C++: Write with compression level 6
writer.open("data.bcsv", true, 6);
```

```python
# Python: Read compressed file (automatic)
reader.open("data.bcsv")  # Decompression automatic
```

```csharp
// C#: Read compressed file (automatic)
reader.Open("data.bcsv");  // Decompression automatic
```

**Compression levels:**
- **0**: No compression (fastest write)
- **1-3**: Fast compression
- **4-6**: Balanced (recommended)
- **7-9**: Maximum compression (slower write)

---

## Best Practices

### 1. Establish Schema Convention

Document your schema in a central location:

```markdown
# sensor_data.bcsv Schema

| Column | Type | Description |
|--------|------|-------------|
| timestamp | DOUBLE | Unix timestamp (seconds) |
| temperature | FLOAT | Temperature in Celsius |
| humidity | FLOAT | Relative humidity (0-100%) |
| sensor_id | STRING | Sensor identifier |
```

### 2. Use Header Inspection

Before reading, inspect the file header to understand the schema:

**C++:**
```cpp
bcsv::Reader<bcsv::Layout> reader;
reader.open("unknown.bcsv");
const auto& layout = reader.layout();

for (size_t i = 0; i < layout.columnCount(); i++) {
    std::cout << layout.columnName(i) << ": " 
              << toString(layout.columnType(i)) << "\n";
}
```

**Python:**
```python
reader = pybcsv.Reader()
reader.open("unknown.bcsv")
layout = reader.get_layout()

for i in range(layout.column_count()):
    print(f"{layout.column_name(i)}: {layout.column_type(i)}")
```

### 3. Handle Version Differences

BCSV format version is stored in header. Check compatibility:

```cpp
if (reader.fileVersion() != bcsv::VERSION) {
    std::cerr << "Warning: File version mismatch\n";
    // Decide whether to proceed
}
```

### 4. Use Consistent Naming

Establish naming conventions across teams:

- **snake_case**: Python-style (recommended for cross-language)
- **camelCase**: C#/JavaScript-style
- **PascalCase**: C#-style

```python
# Recommended: snake_case (works everywhere)
layout.add_column("player_id", ColumnType.INT32)
layout.add_column("score_value", ColumnType.FLOAT)
layout.add_column("player_name", ColumnType.STRING)
```

### 5. Document Units and Ranges

Add metadata in a companion file or comments:

```python
# Write metadata file
metadata = {
    "timestamp": "Unix seconds since epoch",
    "temperature": "Celsius, typical range -40 to 85",
    "humidity": "Percentage, 0-100",
    "sensor_id": "Format: SENSOR_XX where XX is 01-99"
}
with open("sensor_data.meta.json", "w") as f:
    json.dump(metadata, f)
```

---

## Workflow Examples

### Data Pipeline: C++ → Python → C#

```
┌──────────────┐
│ C++ Sensor   │  Collect data (high-speed)
│ Recorder     │  Write: sensor_raw.bcsv
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Python ML    │  Feature engineering
│ Pipeline     │  Train model
└──────┬───────┘  Write: features.bcsv, predictions.bcsv
       │
       ▼
┌──────────────┐
│ Unity Game   │  Load predictions
│ Client       │  Apply AI behaviors
└──────────────┘
```

### ETL Pipeline: Any API → Python → Database

```python
# etl_pipeline.py
import pybcsv
import pandas as pd
import sqlalchemy

# Step 1: Read BCSV (from any source)
df = pybcsv.read_dataframe("data.bcsv")

# Step 2: Transform
df['timestamp'] = pd.to_datetime(df['timestamp'], unit='s')
df['category'] = df['value'].apply(categorize)

# Step 3: Load to database
engine = sqlalchemy.create_engine('postgresql://...')
df.to_sql('sensor_data', engine, if_exists='append')
```

---

## Troubleshooting

### Problem: "Column type mismatch"

**Cause:** Writer and reader have different schemas

**Solution:** Inspect file header and match layout exactly

```python
# Inspect existing file
reader = pybcsv.Reader()
reader.open("data.bcsv")
layout = reader.get_layout()

# Print schema
for i in range(layout.column_count()):
    print(f"{i}: {layout.column_name(i)} ({layout.column_type(i)})")
```

### Problem: Unicode/encoding issues

**Cause:** C API requires manual UTF-8 handling

**Solution:** Ensure UTF-8 encoding in C:

```c
// C: Ensure UTF-8
const char* utf8_string = "Hello 世界";
bcsv_row_set_string(row, 0, utf8_string);
```

### Problem: Performance slower than expected

**Cause:** Wrong compression level or API choice

**Solution:**
- Use compression level 0-3 for high-speed writing
- Use C++ Static API for maximum performance
- Profile your specific use case

---

## File Format Specification

For implementers creating new language bindings, see:
- [ARCHITECTURE.md](../ARCHITECTURE.md) - Binary format details
- [include/bcsv/file_header.h](../include/bcsv/file_header.h) - Header structure
- [tests/](../tests/) - Reference test cases

---

## Summary

✅ **All APIs produce identical binary format**  
✅ **Files are 100% cross-compatible**  
✅ **Layout must match exactly**  
✅ **UTF-8 strings work everywhere**  
✅ **Compression transparent to reader**  
✅ **Best practice: Document your schema**

For specific API documentation:
- [API_OVERVIEW.md](API_OVERVIEW.md) - Compare all APIs
- [C++ examples/](../examples/)
- [Python python/README.md](../python/README.md)
- [C# unity/README.md](../unity/README.md)
