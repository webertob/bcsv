# Bcsv — C# Bindings for Binary CSV

Fast, compact time-series storage with streaming row-by-row I/O, columnar bulk access, Sampler filtering, and CSV interop.

**Bcsv** provides C# bindings for the [BCSV](https://github.com/webertob/bcsv) library via P/Invoke to the native `bcsv_c_api` shared library. The NuGet package includes pre-built native binaries for Windows x64, Linux x64/ARM64, and macOS x64/ARM64.

## Features

- **Sequential & Random Access I/O** — stream rows or jump to any row by index in O(1)
- **Columnar Bulk I/O** — read/write entire columns at once with pinned arrays (`BcsvColumns`)
- **Sampler** — bytecode VM for expression-based row filtering and column projection (`BcsvSampler`)
- **CSV Interop** — read/write plain CSV files through the same row API (`BcsvCsvReader`/`BcsvCsvWriter`)
- **Typed Accessors** — `GetInt32()`, `GetDouble()`, `GetString()`, generic `Get<T>()`, vectorized `GetDoubles()`, etc.
- **LZ4 Compression** — transparent compression with configurable level
- **Zero-Order Hold Encoding** — skip unchanged rows for slowly-varying time-series
- **xxHash64 Checksums** — data integrity verification
- **Crash Recovery** — recoverable streaming format with per-packet checksums
- **Cross-platform** — Windows x64, Linux x64/ARM64, macOS x64/ARM64

## Installation

```bash
dotnet add package Bcsv
```

## Quick Start

```csharp
using Bcsv;

// Define a schema
var layout = new BcsvLayout();
layout.AddColumn("timestamp", ColumnType.Double);
layout.AddColumn("temperature", ColumnType.Float);
layout.AddColumn("label", ColumnType.String);

// Write rows
using var writer = new BcsvWriter(layout);
writer.Open("data.bcsv");
var row = writer.NewRow();
row.Set(0, 1.0);
row.Set(1, 23.5f);
row.Set(2, "sensor-A");
writer.WriteRow(row);
writer.Close();

// Read rows
using var reader = new BcsvReader("data.bcsv");
while (reader.ReadNext())
{
    double ts = reader.Row.GetDouble(0);
    float temp = reader.Row.GetFloat(1);
    string label = reader.Row.GetString(2);
}
```

## Random Access

Read any row by index in O(1) time:

```csharp
using Bcsv;

using var reader = new BcsvReader();
reader.Open("data.bcsv");

// Jump directly to row 1000
if (reader.Read(1000))
{
    double ts = reader.Row.GetDouble(0);
    Console.WriteLine($"Row 1000 timestamp: {ts}");
}

Console.WriteLine($"Total rows: {reader.RowCount}");
```

## Columnar Bulk I/O

For high-throughput scenarios, use `BcsvColumns` to read/write entire columns at once with pinned arrays and a single P/Invoke call:

```csharp
using Bcsv;

// Write columnar data
double[] timestamps = { 1.0, 2.0, 3.0 };
float[] temperatures = { 20.1f, 21.3f, 19.8f };
string[] labels = { "a", "b", "c" };

BcsvColumns.Write("data.bcsv", layout,
    new object[] { timestamps, temperatures, labels });

// Read columnar data
var columns = BcsvColumns.Read("data.bcsv");
```

## Sampler (Filter & Project)

Apply expression-based filtering and column projection over a reader, powered by a bytecode VM:

```csharp
using Bcsv;

using var reader = new BcsvReader();
reader.Open("data.bcsv");

using var sampler = new BcsvSampler(reader);

// Filter: only rows where temperature > 25
sampler.SetConditional("X[0][1] > 25.0");

// Project: timestamp and temperature only
sampler.SetSelection("X[0][0], X[0][1]");

while (sampler.Next())
{
    double ts = sampler.Row.GetDouble(0);
    double temp = sampler.Row.GetDouble(1);
    Console.WriteLine($"{ts}: {temp}°C");
}
```

## CSV Interop

Read and write plain CSV files through the same row API:

```csharp
using Bcsv;

var layout = new BcsvLayout();
layout.AddColumn("id", ColumnType.Int32);
layout.AddColumn("name", ColumnType.String);
layout.AddColumn("value", ColumnType.Double);

// Read CSV
using var csvReader = new BcsvCsvReader(layout, delimiter: ',', decimalSep: '.');
csvReader.Open("input.csv", hasHeader: true);
while (csvReader.ReadNext())
{
    int id = csvReader.Row.GetInt32(0);
    string name = csvReader.Row.GetString(1);
}

// Write CSV
using var csvWriter = new BcsvCsvWriter(layout, delimiter: ',', decimalSep: '.');
csvWriter.Open("output.csv");
var row = csvWriter.Row;
row.SetInt32(0, 42);
row.SetString(1, "Alice");
row.SetDouble(2, 3.14);
csvWriter.WriteRow();
```

## API Reference

| Class | Purpose |
|-------|---------|
| `BcsvLayout` | Define column schema (name, type) |
| `BcsvWriter` | Sequential row-by-row writing |
| `BcsvReader` | Sequential and random-access reading |
| `BcsvRow` | Typed accessors (`Get<T>`/`Set<T>`, `GetInt32`/`SetInt32`, vectorized arrays) |
| `BcsvSampler` | Expression-based filter/project over a reader |
| `BcsvCsvReader` | Read CSV files through the BCSV row API |
| `BcsvCsvWriter` | Write CSV files through the BCSV row API |
| `BcsvColumns` | Columnar bulk read/write with pinned arrays |

## Supported Platforms

| Runtime ID  | OS             | Architecture |
|-------------|----------------|--------------|
| win-x64     | Windows 10+    | x86-64       |
| linux-x64   | Linux (glibc)  | x86-64       |
| linux-arm64 | Linux (glibc)  | ARM64        |
| osx-x64     | macOS 13.3+    | x86-64       |
| osx-arm64   | macOS 13.3+    | ARM64        |

## Requirements

- .NET 8.0 or .NET 10.0
- Native library is bundled in the NuGet package — no separate installation needed

## Documentation

- [API Overview](https://github.com/webertob/bcsv/blob/master/docs/API_OVERVIEW.md)
- [Architecture](https://github.com/webertob/bcsv/blob/master/ARCHITECTURE.md)
- [Full README](https://github.com/webertob/bcsv)

## License

MIT — see [LICENSE](https://github.com/webertob/bcsv/blob/master/LICENSE)
