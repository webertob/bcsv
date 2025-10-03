# BCSV Unity Plugin

This folder contains Unity integration files for the BCSV (Binary CSV) library, allowing you to use BCSV's high-performance binary data format in Unity projects.

## Overview

BCSV provides a fast, compact binary format for storing tabular data with built-in compression and type safety. This Unity plugin allows you to read and write BCSV files directly from Unity scripts.

## Installation

### Step 1: Copy Unity Scripts

Copy both Unity folders to your Unity project's Assets directory:

```text
YourUnityProject/
└── Assets/
    ├── BCSV/
    │   ├── Scripts/         (copy from unity/plugin/)
    │   │   ├── BcsvLayout.cs
    │   │   ├── BcsvNative.cs
    │   │   ├── BcsvReader.cs
    │   │   ├── BcsvRow.cs
    │   │   └── BcsvWriter.cs
    │   └── Examples/        (copy from unity/example/)
    │       └── BcsvRecorder.cs
    └── Plugins/
        └── bcsv_c_api.dll   (copy from build output)
```

### Step 2: Copy Native Library

Copy the `bcsv_c_api.dll` to your Unity project's `Assets/Plugins/` folder:

**From Release Build:**

```bash
copy "path/to/bcsv/build/Release/bcsv_c_api.dll" "YourUnityProject/Assets/Plugins/"
```

**From Debug Build (for development):**

```bash
copy "path/to/bcsv/build/Debug/bcsv_c_api.dll" "YourUnityProject/Assets/Plugins/"
```

### Step 3: Configure Platform Settings

1. Select `bcsv_c_api.dll` in the Unity Project window
2. In the Inspector, configure platform settings:
   - **Settings for Any Platform**: ✅ Check
   - **Windows**: ✅ Check (x86_64)
   - **Editor**: ✅ Check (Windows x86_64)

## Usage Examples

### Basic Writing Example

```csharp
using UnityEngine;
using BCSV;

public class BcsvWriteExample : MonoBehaviour
{
    void Start()
    {
        // Create layout
        var layout = new BcsvLayout();
        layout.AddColumn("id", BcsvColumnType.INT32);
        layout.AddColumn("name", BcsvColumnType.STRING);
        layout.AddColumn("position_x", BcsvColumnType.FLOAT);
        layout.AddColumn("position_y", BcsvColumnType.FLOAT);
        layout.AddColumn("position_z", BcsvColumnType.FLOAT);

        // Create writer
        var writer = new BcsvWriter(layout);
        string filePath = Application.persistentDataPath + "/gamedata.bcsv";
        
        if (writer.Open(filePath))
        {
            // Write data
            var row = writer.GetRow();
            
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
}
```

### Basic Reading Example

```csharp
using UnityEngine;
using BCSV;

public class BcsvReadExample : MonoBehaviour
{
    void Start()
    {
        string filePath = Application.persistentDataPath + "/gamedata.bcsv";
        
        var reader = new BcsvReader();
        if (reader.Open(filePath))
        {
            while (reader.ReadNext())
            {
                var row = reader.GetRow();
                
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
}
```

## File Structure

### Scripts (`unity/plugin/`)

- **`BcsvNative.cs`**: P/Invoke declarations for the native BCSV C API
- **`BcsvLayout.cs`**: Manages column definitions and data schema
- **`BcsvWriter.cs`**: High-level writer interface for creating BCSV files
- **`BcsvReader.cs`**: High-level reader interface for reading BCSV files
- **`BcsvRow.cs`**: Row data access with type-safe get/set methods

### Examples (`unity/example/`)

- **`BcsvRecorder.cs`**: Complete example showing data recording and playback

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

BCSV supports multiple compression levels (0-9):
- **Level 0**: No compression (fastest)
- **Level 1**: Fast compression (good balance)
- **Level 5**: Medium compression (default)
- **Level 9**: Maximum compression (slowest)

```csharp
Example:

```csharp
var writer = new BcsvWriter(layout, compressionLevel: 1); // Fast compression
```
```

## Error Handling

Always check return values and wrap operations in try-catch blocks:

```csharp
try
{
    var reader = new BcsvReader();
    if (!reader.Open(filePath))
    {
        Debug.LogError("Failed to open BCSV file: " + filePath);
        return;
    }
    
    // Process data...
    reader.Close();
}
catch (System.Exception e)
{
    Debug.LogError("BCSV Error: " + e.Message);
}
```

## Platform Considerations

### Windows
- Requires Visual C++ Redistributable (usually included with Unity)
- 64-bit only

### Building for Other Platforms
Currently, only Windows is supported. For other platforms, you would need to:
1. Build the native BCSV library for the target platform
2. Replace `bcsv_c_api.dll` with the platform-specific library
3. Configure Unity's platform settings accordingly

## Troubleshooting

### Common Issues

1. **"DllNotFoundException"**
   - Ensure `bcsv_c_api.dll` is in `Assets/Plugins/`
   - Check platform settings on the DLL
   - Verify Unity target platform matches DLL architecture

2. **"EntryPointNotFoundException"**
   - DLL version mismatch - ensure you're using the correct DLL version
   - Rebuild the native library if necessary

3. **"AccessViolationException"**
   - Check file paths are valid
   - Ensure proper disposal of readers/writers
   - Verify data types match between write and read operations

### Debug Tips

- Enable "Load symbols" in Unity's platform settings for better error messages
- Use the Debug build of `bcsv_c_api.dll` during development
- Check Unity's Console for detailed error messages

## Performance Tips

1. **Reuse Objects**: Create readers/writers once and reuse them
2. **Batch Operations**: Write multiple rows before closing
3. **Choose Appropriate Compression**: Balance file size vs. speed
4. **Proper Disposal**: Always close readers/writers to free resources

## License

This Unity plugin is part of the BCSV library and is licensed under the MIT License.
