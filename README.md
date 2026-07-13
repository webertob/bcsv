# BCSV Unity Package

Unity Package Manager (UPM) package for the BCSV (Binary CSV) library.
Fast, compact binary format for streaming time-series data with LZ4 compression
and Zero-Order Hold encoding. P/Invoke bindings to the native `bcsv_c_api` library.

## Installation

### Option A: Git URL (recommended)

In Unity, open **Window → Package Manager → + → Add package from git URL** and enter:

```
https://github.com/webertob/bcsv.git#upm
```

This installs from the `upm` branch which includes **pre-built native binaries**
for Windows x64, Linux x64/arm64, and macOS arm64. No manual build required.

### Option B: Tarball

Download `com.bcsv.unity-<version>.tgz` from the
[GitHub Releases](https://github.com/webertob/bcsv/releases) page, then in Unity
open **Window → Package Manager → + → Add package from tarball** and select the file.
The tarball includes pre-built native binaries for all supported platforms.

### Option C: Local development

Clone the repo and in Unity open **Window → Package Manager → + → Add package from disk**,
then select `unity/package.json`.

For local development installs, you need to build `bcsv_c_api` and copy the
library into the `Runtime/Plugins/` directories:

```bash
# Windows (from repo root)
cmake --preset ninja-release
cmake --build --preset ninja-release-build --target bcsv_c_api
copy build\ninja-release\bin\bcsv_c_api.dll unity\Runtime\Plugins\Windows\x86_64\

# Linux
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build --target bcsv_c_api
cp build/libbcsv_c_api.so unity/Runtime/Plugins/Linux/x86_64/

# macOS
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build --target bcsv_c_api
cp build/libbcsv_c_api.dylib unity/Runtime/Plugins/macOS/
```

## Usage Examples

### Basic Writing

```csharp
using UnityEngine;
using BCSV;

public class BcsvWriteExample : MonoBehaviour
{
    void Start()
    {
        // Fluent layout builder
        using var layout = new BcsvLayout();
        layout.AddColumn("id", ColumnType.Int32)
              .AddColumn("name", ColumnType.String)
              .AddColumn("position_x", ColumnType.Float)
              .AddColumn("position_y", ColumnType.Float)
              .AddColumn("position_z", ColumnType.Float);

        // Default row codec is "delta" (most compact)
        using var writer = new BcsvWriter(layout);
        string filePath = Application.persistentDataPath + "/gamedata.bcsv";

        writer.Open(filePath, overwrite: true);  // throws BcsvException on failure

        var row = writer.Row;
        row.SetInt32(0, 1);
        row.SetString(1, "Player");
        row.SetFloat(2, transform.position.x);
        row.SetFloat(3, transform.position.y);
        row.SetFloat(4, transform.position.z);
        writer.WriteRow();

        writer.Close();
        Debug.Log("Data written to: " + filePath);
    }
}
```

### Basic Reading

```csharp
using UnityEngine;
using BCSV;

public class BcsvReadExample : MonoBehaviour
{
    void Start()
    {
        string filePath = Application.persistentDataPath + "/gamedata.bcsv";

        using var reader = new BcsvReader();
        reader.Open(filePath);  // throws BcsvException on failure

        // foreach via IEnumerable<BcsvRow>
        foreach (var row in reader)
        {
            int id = row.GetInt32(0);
            string name = row.GetString(1);
            float x = row.GetFloat(2);
            float y = row.GetFloat(3);
            float z = row.GetFloat(4);
            Debug.Log($"ID: {id}, Name: {name}, Position: ({x}, {y}, {z})");
        }
        reader.Close();
    }
}
```

## Package Structure

```
unity/
├── package.json                 # UPM manifest
├── CHANGELOG.md
├── README.md
├── Runtime/
│   ├── link.xml                 # IL2CPP stripping protection
│   ├── Scripts/
│   │   ├── BCSV.asmdef          # Assembly definition
│   │   ├── BcsvNative.cs        # P/Invoke declarations + enums
│   │   ├── BcsvException.cs     # Exception type for native failures
│   │   ├── ColumnDefinition.cs  # Describes a single column
│   │   ├── BcsvLayout.cs        # Column schema (IReadOnlyList)
│   │   ├── BcsvRow.cs           # Row access (readonly struct)
│   │   ├── BcsvWriter.cs        # Streaming binary writer
│   │   ├── BcsvReader.cs        # Streaming binary reader (IEnumerable)
│   │   ├── BcsvCsvReader.cs     # CSV text reader (IEnumerable)
│   │   ├── BcsvCsvWriter.cs     # CSV text writer
│   │   ├── BcsvSampler.cs       # Expression filter/projection
│   │   ├── BcsvColumns.cs       # Columnar bulk I/O
│   │   └── BcsvVersion.cs       # Library version query
│   └── Plugins/
│       ├── Windows/x86_64/      # bcsv_c_api.dll
│       ├── Linux/x86_64/        # libbcsv_c_api.so
│       ├── Linux/arm64/         # libbcsv_c_api.so
│       └── macOS/               # libbcsv_c_api.dylib (universal)
└── Samples~/
    └── Basic/
        ├── BcsvRecorder.cs      # Data recording component
        └── BcsvUnityExample.cs  # API demo
```

## Supported Data Types

| BCSV Type | Unity Type | Description |
|-----------|------------|-------------|
| `BOOL` | `bool` | Boolean values |
| `INT8` | `sbyte` | 8-bit signed integer |
| `INT16` | `short` | 16-bit signed integer |
| `INT32` | `int` | 32-bit signed integer |
| `INT64` | `long` | 64-bit signed integer |
| `UINT8` | `byte` | 8-bit unsigned integer |
| `UINT16` | `ushort` | 16-bit unsigned integer |
| `UINT32` | `uint` | 32-bit unsigned integer |
| `UINT64` | `ulong` | 64-bit unsigned integer |
| `FLOAT` | `float` | 32-bit floating point |
| `DOUBLE` | `double` | 64-bit floating point |
| `STRING` | `string` | UTF-8 encoded strings |

## Features

- **High Performance**: Optimized binary format with LZ4 compression
- **Type Safety**: Compile-time and runtime type checking
- **Cross-Platform**: Works on Windows, macOS, and Linux
- **Memory Efficient**: Minimal garbage collection impact
- **Unity Integration**: Seamless integration with Unity's asset pipeline

## File Paths

### Recommended File Locations

- **Persistent Data**: `Application.persistentDataPath` - For save games, user data
- **Streaming Assets**: `Application.streamingAssetsPath` - For read-only game data
- **Temporary**: `Application.temporaryDataPath` - For cache files

Example:
```csharp
string saveGamePath = Path.Combine(Application.persistentDataPath, "savegame.bcsv");
string gameDataPath = Path.Combine(Application.streamingAssetsPath, "levels.bcsv");
```

## Compression

BCSV supports LZ4 compression controlled via the `compression` parameter when opening a writer:
- **Level 0**: No compression (fastest writes)
- **Level 1**: Fast compression (good balance, default for Unity)
- **Level 9**: Maximum compression (smallest files)

```csharp
writer.Open(filePath, overwrite: true, compression: 1);
```

## Building the Native Library

The CI workflow ([`.github/workflows/unity-package.yml`](../.github/workflows/unity-package.yml))
builds `bcsv_c_api` for all 5 platforms and packs the UPM `.tgz` automatically on every push.

To build locally for your current platform:

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release-build --target bcsv_c_api
```

The output is at `build/ninja-release/bin/bcsv_c_api.dll` (Windows) or `build/libbcsv_c_api.so|.dylib`.

## Error Handling

BCSV uses two error patterns:

- **Throwing methods** (`Open`, `WriteRow`): throw `BcsvException` on failure.
- **Try methods** (`TryOpen`): return `false` on failure (no exception).

```csharp
// Pattern 1 — exceptions (default)
try
{
    using var reader = new BcsvReader();
    reader.Open(filePath);
    foreach (var row in reader)
        Debug.Log(row.GetInt32(0));
}
catch (BcsvException e)
{
    Debug.LogError("BCSV Error: " + e.Message);
}

// Pattern 2 — TryOpen (no exceptions)
using var reader2 = new BcsvReader();
if (reader2.TryOpen(filePath))
{
    while (reader2.ReadNext())
        Debug.Log(reader2.Row.GetInt32(0));
}
else
{
    Debug.LogError("Failed to open: " + reader2.ErrorMessage);
}
```

## Troubleshooting

### Common Issues

1. **DllNotFoundException**
   - Ensure the native library for your platform is in `Runtime/Plugins/`
   - For local dev: build `bcsv_c_api` and copy it to the correct Plugins subfolder
   - Verify Unity target platform matches library architecture (x86_64)

2. **EntryPointNotFoundException**
   - DLL version mismatch — rebuild the native library from the same commit as the C# scripts

3. **AccessViolationException**
   - Ensure proper disposal of readers/writers (`using` statements or `Dispose()`)
   - Verify data types match between write and read operations

### IL2CPP / AOT Builds

The package includes `link.xml` to prevent managed code stripping. If you use a
custom assembly name, update `link.xml` accordingly.

## Platform Support

| Platform | Architecture | Status |
|----------|-------------|--------|
| Windows | x86_64 | ✅ Supported |
| Linux | x86_64 | ✅ Supported |
| Linux | ARM64 | ✅ Supported |
| macOS | x86_64 + ARM64 | ✅ Universal binary |
| Android / iOS | — | Not yet supported |

## License

This Unity plugin is part of the BCSV library and is licensed under the MIT License.
