# Bcsv — C# Bindings for Binary CSV

Fast, compact time-series storage with streaming row-by-row and columnar bulk I/O.

**Bcsv** provides C# bindings for the [BCSV](https://github.com/webertob/bcsv) library via P/Invoke to the native `bcsv_c_api` shared library. The NuGet package includes pre-built native binaries for Windows x64, Linux x64/ARM64, and macOS x64/ARM64.

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
