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

### Recording a Scene

`BcsvRecorder` is a supported component, not a sample: add it to a GameObject,
subscribe the channels you want, and it owns the file for you.

```csharp
using BCSV;
using UnityEngine;

[RequireComponent(typeof(BcsvRecorder))]
public class MyRecording : MonoBehaviour
{
    void Awake()
    {
        var rec = GetComponent<BcsvRecorder>();

        // The recorder writes no time column of its own — what a recording
        // calls time is yours to define, in whatever unit and width fits.
        rec.Track("t", () => Time.fixedTimeAsDouble);

        rec.Track("speed", () => body.linearVelocity.magnitude);
        rec.Track("grounded", () => controller.isGrounded);
        rec.TrackTransform(transform);

        rec.sampleRateHz = 100f;   // 0 records one row per physics step
    }
}
```

The column type is inferred from the getter, so a channel cannot be declared one
type and fed another, and reading a value does not allocate.

**Sample rate.** The achievable rate is quantised to the physics step rate, and
the remainder is carried rather than discarded — so 300 Hz on a 1 kHz project
alternates 4, 3, 3 ms intervals and the *mean* rate is exactly 300 Hz. The naive
alternative, resetting the accumulator on each row, silently records 250 Hz
instead. Use `BcsvSampleClock` directly if you want that decimation without the
component.

**Sampling is chosen per channel.** The default is `Latest` — the value at the
sample instant, a zero-order hold — and it is the only mode for `bool` and
`string` channels, which have no mean and nothing to interpolate; those overloads
do not take the argument at all.

```csharp
rec.Track("pos.x", () => t.position.x, BcsvRecorder.Sampling.Average);
rec.Track("state", () => machine.State);          // string: Latest, necessarily
```

| mode | what a row holds | when it earns its keep |
|---|---|---|
| `Latest` | the value at the sample instant | the default; the only mode that reads the getter solely on steps that produce a row |
| `Average` | the mean of every host step since the previous row | recording *slower* than the host: a real anti-alias filter, applied before the decimation |
| `Interpolate` | linear between the host samples either side of the instant | recording *at or above* the host rate: aligns rows to the instants asked for |

Averaging is the mode that matters when decimating. Recording a 1 kHz simulation
at 50 Hz puts the Nyquist frequency at 25 Hz, and content above it does not
disappear — it folds down and lands in the recording indistinguishable from real
signal. Measured on a 410 Hz tone recorded at 50 Hz: `Latest` reproduces it at
**full amplitude** as a 10 Hz alias, while `Average` attenuates it by a factor of
33.

Interpolation is the other direction and does not do the same job: nothing the
host never sampled can be recovered by interpolating what it did, so it is rate
alignment rather than filtering. It also builds each row partly from a value one
step old, which misrepresents a channel that steps rather than varies smoothly.

Both filtered modes call their getter on **every** host step, not only on the ones
that produce a row — which is why they are opted into per channel. `SamplesEveryStep`
reports whether any channel does.

**Execution order** is fixed at 1000 so the recorder runs last in a
`FixedUpdate` step. Without a declared order Unity may run it part way through
the objects being recorded, and a row then mixes values from two steps —
which shows up as channels that should agree exactly instead differing by
precisely one sample.

**Pacing.** Set `pacing = BcsvRecorder.Pacing.External` and `FixedUpdate` does
nothing; drive it yourself with `Advance(dt)` for a rate, or `Trigger()` to place
a single row at an event. Useful for a test rig with its own pump, or for
recording on a controller step rather than a physics step.

### Replaying a Recording

`BcsvPlayer` is the recorder's mirror: the same clock, the same `Advance(dt)` /
`Trigger()` pacing, and channels bound by name — but pushing values into the
scene instead of pulling them out.

```csharp
[RequireComponent(typeof(BcsvPlayer))]
public class MyReplay : MonoBehaviour
{
    Vector3 pos;

    void Awake()
    {
        var player = GetComponent<BcsvPlayer>();
        player.BindFloat("Cube_0.position.x", v => pos.x = v)
              .BindFloat("Cube_0.position.y", v => pos.y = v)
              .BindFloat("Cube_0.position.z", v => pos.z = v)
              .BindDouble("t", v => recordedTime = v);

        player.Completed += () => Debug.Log("done");
    }

    void FixedUpdate() => transform.position = pos;   // runs after the player
}
```

Bindings are named per type (`BindFloat`, `BindInt32`, `BindString`, …) rather
than overloaded on one name. A setter's parameter type cannot be inferred from a
lambda the way a getter's return type can, so a single `Bind` would be ambiguous
at every call site.

**It does not read the recording's own timestamps**, and cannot: a recording's
idea of time is a column like any other, under whatever name and unit its author
chose. Rows are presented at `playbackRateHz` and the file's time channel arrives
as an ordinary binding. *If that rate does not match the rate the file was
recorded at, playback runs fast or slow and nothing detects it.*

Every binding is checked against the file's real layout before a row is played —
a missing column or a wrong type stops playback with all the mismatches listed at
once, not one per run.

`Seek(index)`, `Play()`, `Pause()` and `loop` cover scrubbing and repeat.
**Execution order is -1000**, the mirror of the recorder's +1000: a player is a
source, so everything consuming its values must run after it. The two bracket a
`FixedUpdate` step between them.

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
│   │   ├── BcsvMetadata.cs      # Sidecar metadata companion reader
│   │   ├── BcsvDefaults.cs      # Shared writer defaults
│   │   ├── BcsvSampleClock.cs   # Decides when a row is written
│   │   ├── BcsvRecorder.cs      # Recording component
│   │   ├── BcsvPlayer.cs        # Playback component
│   │   └── BcsvVersion.cs       # Library version query
│   └── Plugins/
│       ├── Windows/x86_64/      # bcsv_c_api.dll
│       ├── Linux/x86_64/        # libbcsv_c_api.so
│       ├── Linux/arm64/         # libbcsv_c_api.so
│       └── macOS/               # libbcsv_c_api.dylib (universal)
├── Tests/
│   └── Editor/                  # EditMode tests (opt in via `testables`)
└── Samples~/
    └── Basic/
        ├── BcsvRecorderDemo.cs  # Wires BcsvRecorder to the demo scene
        ├── BcsvPlayerDemo.cs    # Replays what the recorder demo wrote
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
