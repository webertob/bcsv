# Changelog

All notable changes to the BCSV Unity package will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/webertob/bcsv/compare/v1.5.0...HEAD
[1.5.0]: https://github.com/webertob/bcsv/compare/v1.4.3...v1.5.0
[1.4.3]: https://github.com/webertob/bcsv/releases/tag/v1.4.3
