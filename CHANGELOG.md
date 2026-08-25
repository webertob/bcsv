# Changelog

All notable changes to the BCSV Unity package will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.5.17] - 2026-08-25

### Added

- **`scripts/run-unity-tests.sh`**, which runs the EditMode tests in a headless
  editor. It exists rather than being a one-line `Unity -runTests` because
  pointing an editor at `unity/` as a local package **silently deletes committed
  files**: the AssetDatabase removes any `.meta` with no asset beside it, and the
  four plugin sidecars are exactly that until CI injects the binaries. Three were
  lost that way during this release's development, which would have shipped
  plugins Unity ignores — a P/Invoke failure in a consumer's project rather than
  a build error here. The script stages placeholders first and cleans up after.
  An opt-in `unity-tests` job in `unity-package.yml` runs the same tests in CI
  wherever a `UNITY_LICENSE` secret is configured.

- **An EditMode test assembly, `Tests/Editor/BCSV.Tests.asmdef`.** Covers the
  recorder and player at component level — lifecycle, rate validation, decimation,
  filter windows, binding resolution, completion, and a recorder-to-player round
  trip — the parts that need a MonoBehaviour and the native plugin rather than
  plain arithmetic. It ships with the package behind `UNITY_INCLUDE_TESTS`, so it
  compiles only for a consumer who opts the package into their `testables`.
  Everything that does *not* need Unity, including the sample clock and its rate
  validation, is covered in the standalone C# suite instead, which runs in CI.


- **`BCSV.BcsvPlayer`** — the recorder's mirror: it plays a BCSV recording back
  into a running scene. Same `BcsvSampleClock`, same `Advance(dt)` / `Trigger()`
  so a test rig or external driver can pace it, same channel model — except that
  where the recorder pulls values out with a getter, this pushes them back in
  with a setter.

  **It does not read the recording's own timestamps, and this is a decision.** A
  recording's idea of time is whatever its author chose to call time — seconds or
  milliseconds, absolute or relative, float, double or a step counter, under any
  column name — so there is nothing to look for. Rows are presented at
  `playbackRateHz` and the file's time channel arrives as an ordinary binding for
  the caller to interpret. The consequence is worth stating rather than
  discovering: if that rate does not match the rate the file was recorded at,
  playback runs fast or slow and nothing detects it.

  Bindings are named per type — `BindFloat`, `BindInt32`, `BindString` and so on
  — rather than overloaded on one name as `Track` is. The asymmetry is forced by
  the language, not chosen: the compiler infers a getter's *return* type from a
  lambda body, but not a setter's *parameter* type, so a single `Bind` would be
  ambiguous at every call site.

  Every binding is resolved against the file's real layout before a row plays,
  and **all** mismatches are reported together with the recording's actual
  columns — a caller with thirty bindings and three typos sees three, rather than
  one per run. `Seek(index)`, `Play()`, `Pause()`, `loop` and a `Completed` event
  cover scrubbing and repeat. Rows are addressed by index rather than streamed,
  which measures about 60 ns a row more and buys one code path for playback,
  seeking and looping alike; a file written without a packet index has one
  rebuilt when the component opens it.

  **Execution order is -1000**, the mirror of the recorder's +1000. A player is a
  source, so everything reading its values has to run after it or spend a step on
  the previous row — the same defect the recorder's order prevents, seen from the
  other end. The two bracket a `FixedUpdate` step between them.

- **`BCSV.BcsvRecorder` is now a supported Runtime component**, not a sample.
  Add it to a GameObject, subscribe channels with `Track(name, getter)`, and it
  owns the file. Previously the only recorder was `Samples~/Basic/BcsvRecorder.cs`,
  which a consumer had to copy to use — and a copy stops receiving fixes, which is
  exactly what happened: a downstream project re-implemented it and reintroduced a
  rate-decimation defect the sample had already avoided.

  What it adds over the sample it replaces:
  - **`sampleRateHz`**, public and live-editable, `0` meaning one row per host
    step. Backed by `BcsvSampleClock`, so the remainder is carried and the mean
    rate is exact.
  - **`[DefaultExecutionOrder(1000)]`**, so the recorder runs last in a
    `FixedUpdate` step. Unity does not otherwise define the order between
    components, so a recorder can run part way through the objects it records and
    write a row mixing two steps. The symptom is worth naming because it looks
    physical rather than broken: channels that must agree exactly instead differ
    by precisely one sample.
  - **External pacing.** `pacing = Pacing.External` makes `FixedUpdate` a no-op;
    the driver calls `Advance(dt)` for a rate or `Trigger()` for a single row at
    an event. A rig with its own pump no longer needs a global flag to stop the
    component fighting it.

  - **Per-channel sampling.** `Sampling.Latest` (the default, a zero-order hold),
    `Sampling.Average` (the mean of every host step since the previous row) or
    `Sampling.Interpolate` (linear between the host samples bracketing the
    instant), chosen per `Track` call rather than per recording.

    `Average` is the one that matters when decimating, and it is a real
    anti-alias filter rather than a smoothing pass, because it sits before the
    decimation instead of after it. Recording a 1 kHz simulation at 50 Hz puts
    the Nyquist at 25 Hz, and content above it folds down into the recording
    looking like signal. Measured on a 410 Hz tone recorded at 50 Hz: `Latest`
    reproduces it at full amplitude as a 10 Hz alias; `Average` attenuates it
    by a factor of 33.

    `bool` and `string` channels are always `Latest` — neither has a mean or a
    midpoint — and their `Track` overloads do not accept the argument, so the
    rule is enforced at the call site rather than by ignoring what was passed.
    Both filtered modes read their getter on every host step rather than only on
    the ones producing a row, which is why the choice is per channel;
    `SamplesEveryStep` reports whether any channel does. Filters are primed with
    one read when the file opens, so a recording does not begin with a run of
    repeated rows and a getter that cannot work says so before a file exists.

  And three defects in the sample that a supported component cannot carry:
  - **No time column.** The sample wrote `Time.time` into a `Float` column;
    single precision loses millisecond resolution after about two hours of scene
    time. What a recording calls time is now a channel like any other, defined by
    the caller in whatever unit and width the experiment needs.
  - **Reading a value no longer allocates.** `Track` is overloaded per column
    type and closes over a typed getter, where the sample's generic
    `Func<object>` boxed once per column per row — thirty thousand garbage
    objects a second at 1 kHz and thirty columns, inside the physics loop. The
    column type is now inferred from the getter, so a channel also cannot be
    declared one type and fed another.
  - **A failed getter drops the row instead of writing a stale one.** bcsv reuses
    the row buffer between writes, so a column left unset keeps the previous
    row's value — and under the delta and zero-order-hold codecs an unchanged
    value is indistinguishable from a genuinely constant channel. The sample
    caught the exception and committed the row anyway. A dropped row is visible
    in the caller's own time channel; `FailedRows` counts them and closing the
    file reports the total.

  Subscriptions are refused while a file is open, rather than silently having no
  column: the sample's `AddSubscription` was public but the layout was built in
  `Start`, so anything added later wrote past the end of the layout and produced
  one caught exception per row.

- **`BCSV.BcsvSampleClock`** — decides *when* a row is written, which is the half
  of a recorder that is the same regardless of what is being recorded.
  `Advance(dt)` returns the number of sample instants inside the step just taken,
  so a `FixedUpdate` running at the physics rate can record at a lower one
  without the rate error that resetting an accumulator to zero produces (on a
  1 kHz project: 300 Hz asked, 250 Hz recorded, reported by nothing). Set the
  rate with `FromRate(hz)` or `FromPeriod(seconds)`; `EveryStep()` records one
  row per host step.

  Plain C# in the `BCSV` assembly — no `MonoBehaviour`, no `UnityEngine.Time`, so
  a test rig or an external driver can pace it by calling `Advance` itself. It
  holds no time base and writes no time column: what a recording calls time, and
  in what unit, stays the recorder's decision.

### Fixed

- **The package no longer fails to compile in a project without the Physics
  module.** Promoting the recorder out of `Samples~` moved `TrackRigidbody` into
  the package assembly, and `BCSV.asmdef` references nothing — so `UnityEngine.
  Rigidbody` could not be resolved and the whole assembly failed with CS1069. A
  sample had never shown this, because samples compile into the project's default
  assembly, which references every enabled module.

  Physics is now an optional dependency rather than a required one: the asmdef
  carries a `versionDefines` entry mapping `com.unity.modules.physics` to
  `BCSV_HAS_PHYSICS`, and `TrackRigidbody` is compiled only under it. Everything
  else in the recorder works in a project with no physics engine at all, which is
  the right trade for a data-recording package.

- **Filtered sampling is no longer offered on 64-bit integer channels.** The
  filters carry values as `double`, whose 53-bit mantissa cannot represent every
  `long` or `ulong`: an average was exact below 2^53 and quietly wrong above it
  (2^53+1 came back as 2^53), and the values that failed were the large ones —
  where a counter or a nanosecond timestamp lives. The clamp on the way back into
  the column also relied on a saturating float-to-integer cast, which CoreCLR
  guarantees and C++ does not, so the behaviour was not the same under IL2CPP as
  in the editor. `Track(name, Func<long>)` and `Track(name, Func<ulong>)` now
  take no `Sampling` argument at all, on the same terms as bool and string.
  32-bit and narrower integers are unaffected — their whole range is exactly
  representable.

- **An unusable sample or playback rate is refused instead of coerced or
  thrown.** `hertz > 0 ? FromRate(hertz) : EveryStep()` is wrong at both ends: a
  negative rate and a NaN both failed the comparison and were silently recorded
  at the full step rate, while an infinite one passed it and threw — after the
  native writer or reader had already been opened, out of `Start()`, leaving the
  component half-initialised around a handle nothing had a reference to. Rates
  are now validated by `BcsvSampleClock.TryFromRate` before anything is opened,
  and a bad value assigned mid-run is reported once while the previous rate keeps
  running.

- **A dropped row no longer splits the averaging windows.** Filter state moved
  one channel at a time in two places, so a getter that threw part way along left
  the channels before it holding this step's sample and the ones after it holding
  the previous step's — their averaging windows and interpolation pairs then
  diverged, which corrupts precisely the data the filters exist to get right.
  Both are atomic now: the per-step read stages every value and commits none of
  them until all have succeeded, and the accumulators clear only once the row has
  reached the writer. A step whose read fails moves nothing at all.

- **`Seek` to the last row finishes the recording.** `BcsvPlayer.Seek` presented
  the row and returned, bypassing the completion path, so seeking to the end left
  `IsPlaying` true and `Completed` unraised until something stepped again — the
  same one-edge lag that `Step` was fixed for, reached by a different route.

- **A recording finishes on its last row, not on the following clock edge.**
  `BcsvPlayer` tested for a next row before presenting the current one, so
  `IsPlaying` stayed true and `Completed` went unraised for a whole sample
  interval past the visible end of a recording — a second at 1 Hz, and
  indefinitely for a single-row file nothing steps again. That contradicted the
  documented contract.

- **`new BcsvWriter(layout, "flat")` produced a delta-encoded file.** The native
  entry point the Unity binding routes `"flat"` to was constructing a delta
  writer; see the root `CHANGELOG.md` for the full account. The managed code
  needed no change — updating the native plugin is the fix. No data was wrong
  and no recording needs re-encoding: the files were valid delta files whose
  headers said delta.

- **An unrecognised `rowCodec` string is refused rather than silently treated as
  delta.** `new BcsvWriter(layout, "zho")` used to record in delta without
  comment; it throws `ArgumentException` now. `BcsvRecorder` validates the field
  before opening a file, so a scene gets a clear message rather than an
  exception out of `Start()`.

### Changed

- **`BcsvWriter.FileFlags`** reports the flags actually written to the header,
  which are not the ones passed to `Open`: the row-codec bits are replaced from
  the codec named in the constructor. `Open` and `TryOpen` now say so in their
  XML docs, and `FileFlags.ZeroOrderHold` / `FileFlags.DeltaEncoding` are marked
  output-only on the enum itself.

- **`Samples~/Basic/BcsvRecorder.cs` is now `BcsvRecorderDemo.cs`** and contains
  only the demo-scene wiring — which objects to record. The recorder itself moved
  into the package, so a consumer configures a supported component instead of
  copying one. An existing copy keeps working and keeps its defects; there is no
  automatic migration.

---

## [1.5.15] - 2026-08-24

### Added

- **`BcsvMetadata.ReadCompanion(path)`** — reads the `<file>.bcsv.meta.json`
  companion written by pybcsv's `parquet2bcsv`, which carries file-level
  key/value metadata (provenance, release identifiers, contract markers) that
  the BCSV header has no room for. Returns `null` when no companion exists, and
  throws `BcsvException` when one is malformed or does not describe the file
  beside it: the document records the BCSV file's SHA-256, and a companion left
  over from an earlier conversion to the same output name must not silently
  stamp unrelated data with someone else's provenance. Byte size and row count
  are checked first as cheap pre-checks; a binding field that is present but
  malformed is rejected rather than skipped. No third-party dependency; the JSON reader is hand-rolled because
  Unity 2021.3 has no `System.Text.Json`.

  ```csharp
  var meta = BcsvMetadata.ReadCompanion(path, expectedRows: reader.RowCount);
  if (meta != null && meta["rotation_contract"] != "unit_xyzw_v1")
      throw new InvalidOperationException("wrong contract");
  ```

  **Transitional — scheduled for deletion.** This class exists only because the
  format has no metadata section. Version 1.6.0 adds one, exposed as
  `BcsvReader.Metadata`, and `BcsvMetadata` is removed after a deprecation
  release (roadmap item E12). Keep usage to a single call site so the migration
  is a one-line change.

---

## [1.5.14] - 2026-08-22

### Fixed
- **The package no longer trips Unity's "no meta file, but it's in an immutable
  folder" warning.** It shipped `tools/build-windows.ps1` — a maintainer script
  for building the Windows native — with no `.meta` files. A package installed
  from a tarball or a git URL lives in an immutable folder, so Unity cannot
  generate the missing metas itself and instead logged two warnings on every
  import and every domain reload. The script was never package content: it now
  lives at `scripts/build-unity-windows.ps1` in the repository root, and `unity/`
  holds only what a consumer actually installs.

### Changed
- **Packing selects what to ship instead of copying the directory.** Both the
  `.tgz` workflow and the `upm` branch workflow copied `unity/*` wholesale, so
  any file added under `unity/` reached consumers whether or not it belonged in
  the package — which is how the build script shipped in the first place. They
  now copy an explicit list and fail if any staged file would ship without a
  `.meta`, matching the packing scripts in the sibling Unity packages.

### Added
- **`.meta` files for the Basic sample.** The sample shipped without them, so
  importing it minted fresh GUIDs for `BcsvRecorder` and `BcsvUnityExample` —
  different for every user and different again on every re-import, which breaks
  any scene or prefab referencing those components. The GUIDs are now fixed by
  the package. Existing imported copies keep the GUIDs they were given; re-import
  the sample to adopt the stable ones.

## [1.5.13] - 2026-08-21

### Fixed
- **Package version stamps are correct again.** The `.tgz` in the v1.5.12 release
  carried Linux natives (`x86_64` and `arm64`) reporting `1.5.11` while the
  Windows native correctly reported `1.5.12`, so the same recorder wrote
  different header bytes depending on platform. Data was unaffected — only the
  version stamped into the file header differed. The Linux jobs build inside a
  container where git could not read the workspace, and the build silently fell
  back to a stale version; it now fails instead. See the root `CHANGELOG.md`.
- **`package.json` on the `upm` branch reported `1.5.3` for every release since
  v1.5.4.** The branch workflow read the committed `unity/package.json`, which is
  only patched during packing, instead of the release version. Anyone installing
  via the `#upm` Git URL saw `1.5.3` regardless of what they actually got.
- The committed `unity/package.json` now mirrors `VERSION.txt` and is verified on
  every push, so it can no longer drift from the shipped package.

### Note on version history
This package tracks the BCSV library version. Entries between 1.5.3 and 1.5.13
were not recorded here; see the root `CHANGELOG.md` for library changes in that
range. Packaging changes in 1.5.12 (manylinux Linux builds with a static C++
runtime, static MSVC runtime on Windows) removed the need for a matching
`libstdc++` or the Visual C++ Redistributable on target machines.

## [1.5.3] - 2026-03-22

### Fixed
- `BCSV.asmdef` now sets `allowUnsafeCode: true` (required by Span-based BcsvRow array accessors)

## [1.5.2] - 2026-03-22

### Fixed
- `ColumnDefinition` reverted from `readonly record struct` to `readonly struct` for C# 9.0 / Unity Mono compatibility
- `WriteColumns` now accepts an `overwrite` parameter (default `false`) instead of silently overwriting

### Added
- **BcsvCsvReader**: CSV text file reader with `IEnumerable<BcsvRow>` and `TryOpen()`
- **BcsvCsvWriter**: CSV text file writer with `TryOpen()`
- **BcsvSampler**: Expression-based filter/projection over a `BcsvReader` with `IEnumerable`
- **BcsvColumns**: Columnar (column-oriented) bulk read/write with `ColumnData`
- **BcsvVersion**: Static class for querying native library version
- **BcsvException**: Dedicated exception type for native operation failures
- **ColumnDefinition**: Readonly struct describing a single column (name, type, index)
- **SamplerMode** enum in `BcsvNative.cs`
- `BcsvReader.ReadBatch(int maxRows)` for columnar batch reads
- `BcsvReader.Read(long index)` for random access
- `BcsvReader.TryOpen()` / `BcsvWriter.TryOpen()` — bool-returning alternatives to throwing `Open()`
- `BcsvLayout.Clone()`, `ColumnCountByType()`, `RowDataSize`, `ToString()`
- `BcsvLayout` now implements `IReadOnlyList<ColumnDefinition>` with indexer and foreach
- `BcsvLayout.AddColumn()` returns `this` for fluent chaining
- `BcsvReader` implements `IEnumerable<BcsvRow>` for foreach iteration
- `BcsvWriter.Write(BcsvRow)` writes an external row
- `BcsvRow.ColumnCount`, `ToString()`, and complete `Span<T>`-based array accessors
- P/Invoke coverage expanded from 91 to 153+ functions (full C API parity)
- Version API: `bcsv_version()`, `bcsv_version_major/minor/patch()`
- `CompressionLevel`, `FileFlags`, `ErrorMessage` properties on Reader/Writer

### Changed
- **BREAKING**: `BcsvRow` is now a lightweight `readonly struct` (non-owning handle). Removed `BcsvRowBase`, `BcsvRowRef`, `BcsvRowRefConst` class hierarchy.
- **BREAKING**: `BcsvWriter` constructor takes `string rowCodec` parameter (`"flat"`, `"zoh"`, `"delta"`; default `"delta"`). Removed `BcsvWriterZoH` subclass.
- **BREAKING**: `Open()` on Reader/Writer now throws `BcsvException` on failure instead of returning `bool`. Use `TryOpen()` for non-throwing variant.
- **BREAKING**: `writer.Next()` renamed to `writer.WriteRow()`
- **BREAKING**: `reader.Next()` renamed to `reader.ReadNext()`
- **BREAKING**: `reader.CountRows()` replaced with `reader.RowCount` property
- **BREAKING**: `reader.Index` replaced with `reader.CurrentIndex`
- **BREAKING**: Default `overwrite` parameter changed from `true` to `false` across all writers
- P/Invoke layer modernized: `IntPtr` → `nint`/`nuint`, added `[MarshalAs]` attributes for bool/string marshalling
- `BcsvNative.DllName` renamed to `BcsvNative.Lib`
- `link.xml` updated: removed old types, added all new types
- Samples updated for new API

### Removed
- `BcsvRowBase`, `BcsvRowRef`, `BcsvRowRefConst` (replaced by `BcsvRow` struct)
- `BcsvWriterZoH` (use `new BcsvWriter(layout, "zoh")` instead)
- `BcsvRow.Create()`, `BcsvRow.Clone()`, `row.Assign()` (BcsvRow is now non-owning)
- `WriteRow(params object[])`, `WriteRows()`, `ReadAll()`, `ReadAllRows()` helper methods
- `BcsvLayout(BcsvLayout other)` copy constructor (use `Clone()`)

## [1.5.0] - 2026-03-22

### Added
- `upm` branch with pre-built native binaries — Git URL installs now work without local builds
- Tarball (`.tgz`) from GitHub Releases includes pre-built natives for all platforms
- `.meta` files for all package assets (stable GUIDs)
- `upm-branch.yml` workflow auto-updates the `upm` branch after each build

### Fixed
- Replaced `UIntPtr.MaxValue` with .NET Standard 2.1 compatible `(UIntPtr)ulong.MaxValue`
- Updated `package.json` version to 1.5.0
- README updated: Git URL now points to `#upm` branch with pre-built natives

## [1.4.3] - 2026-03-21

### Added
- Initial UPM package structure (`package.json`, assembly definition, Samples~)
- GitHub Actions workflow for multi-platform native builds and `.tgz` packaging
- `FileFlags` enum: `NoFileIndex`, `StreamMode`, `BatchCompress`, `DeltaEncoding`

### Fixed
- Double-free bug in `BcsvRowBase.Layout` (now uses non-owning handle)
- README path references (`unity/plugin/` → `unity/Runtime/Scripts/`)

[1.5.17]: https://github.com/webertob/bcsv/compare/v1.5.16...v1.5.17
[1.5.3]: https://github.com/webertob/bcsv/compare/v1.5.2...v1.5.3
[1.5.2]: https://github.com/webertob/bcsv/compare/v1.5.1...v1.5.2
[1.5.0]: https://github.com/webertob/bcsv/compare/v1.4.3...v1.5.0
[1.4.3]: https://github.com/webertob/bcsv/releases/tag/v1.4.3
