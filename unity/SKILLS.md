# BCSV Unity / C# Bindings — AI Skills Reference

> Quick-reference for AI agents working with the C# wrapper for Unity.
> For full integration guide, see: unity/README.md

## Architecture

```
C# (Unity)  →  P/Invoke  →  bcsv_c_api.dll/.so  →  C++ bcsv library
```

The C# bindings call the **C shared library** (`bcsv_c_api`) via platform invoke.
No managed C++/CLI — pure P/Invoke with opaque handles.

## Prerequisites

1. Build the C shared library target:
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target bcsv_c_api -j$(nproc)
   ```
   Produces `build/libbcsv_c_api.so` (Linux) or `bcsv_c_api.dll` (Windows).

2. Copy the shared library into your Unity project's `Assets/Plugins/` directory.

## Key Files

| File | Role |
|------|------|
| `Scripts/BcsvNative.cs` | P/Invoke declarations — all `extern` function signatures matching `bcsv_c_api.h` |
| `Scripts/BcsvLayout.cs` | C# `BcsvLayout` wrapper (IDisposable) — column management |
| `Scripts/BcsvReader.cs` | C# `BcsvReader` wrapper — file reading |
| `Scripts/BcsvWriter.cs` | C# `BcsvWriter` wrapper — file writing |
| `Scripts/BcsvRow.cs` | C# `BcsvRow` wrapper — get/set values |
| `Examples/BcsvUnityExample.cs` | Basic usage example |
| `Examples/BcsvRecorder.cs` | Recording example for Unity scenes |

## Installation in Unity

1. Copy `unity/Scripts/` → `Assets/BCSV/Scripts/`
2. Copy `unity/Examples/` → `Assets/BCSV/Examples/`
3. Copy shared library → `Assets/Plugins/`
4. See `unity/README.md` for platform-specific plugin setup

## Memory Management

All C# wrapper classes implement `IDisposable`:
```csharp
using (var layout = new BcsvLayout()) {
    layout.AddColumn("time", BcsvColumnType.Double);
    using (var writer = new BcsvWriter()) {
        writer.Open("data.bcsv", layout);
        // ...
        writer.Close();
    }
}
```

See `unity/OWNERSHIP_SEMANTICS.md` for detailed memory management rules.

## C API Surface

The C# P/Invoke declarations in `BcsvNative.cs` map 1:1 to the C API in `include/bcsv/bcsv_c_api.h`.
Any changes to the C API require updating both files.

**C API naming convention:** `bcsv_snake_case` (C convention, not lowerCamelCase).
