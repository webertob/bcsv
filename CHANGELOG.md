# Changelog

All notable changes to BCSV are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/).

> **Maintenance:** This changelog is maintained manually. When tagging a release,
> update this file with the changes since the last tag.

---

## [Unreleased]

## [1.5.3] - 2026-03-22

### Fixed
- Unity: `BCSV.asmdef` now sets `allowUnsafeCode: true` for Span-based BcsvRow array accessors

## [1.5.2] - 2026-03-22

### Fixed
- Unity: `ColumnDefinition` reverted from `readonly record struct` to `readonly struct` for C# 9.0 / Mono compatibility
- C#: `BcsvColumns.WriteColumns` now accepts an `overwrite` parameter (default `false`)
- C#: `PtrToStringAuto` helper for cross-platform filename marshalling (Windows `wchar_t*` vs Unix `char*`)

## [1.5.0] - 2026-03-22

### Changed
- C++: Unified library version and file format version — single version from git tags stamped into every `.bcsv` header (see VERSIONING.md)
- C++: Version-gated codec registry — `resolveRowCodecId(fileMinor, flags)` and `resolveFileCodecId(fileMinor, ...)` enable backward-compatible codec selection from file header
- C++: `static_assert` guardrails on `ROW_CODEC_COUNT` / `FILE_CODEC_COUNT` break the build when a codec enum is added without updating the registry
- Docs: VERSIONING.md rewritten with A/B/C compatibility rules and Codec Registry section
- Docs: ARCHITECTURE.md codec dispatch section updated for version-gated selection
- Docs: SKILLS.md codec recipe added ("Adding a New Codec — Version-Gated Registry")

### Removed
- C API: `bcsv_format_version()` — superseded by unified `bcsv_version()` (library = format)
- C#: `BcsvVersion.FormatVersion` property and its P/Invoke binding

### Added
- C++: Delta002 row codec now supports `LayoutStatic` (compile-time static layouts) — full serialize/deserialize with recursive template iteration, multi-bit headers, ZoH/FoC/VLE delta tiers
- Benchmark: Delta002 codec included in all 14 macro benchmark profiles for both Flexible and Static layouts
- Benchmark: `--codec=delta` and `--codec=primary` (Dense + ZoH + Delta) selection modes
- Benchmark: unified measurement campaign data generator (`generateTimeSeries`) with 70% active / 30% standstill pattern for fair codec comparison
- Benchmark: codec recommendation table in report output
- Benchmark: Delta mode aliases and interleaved comparison metrics in Python reporting
- Test coverage: 8 new LayoutStatic Delta002 tests (round-trip, ZoH, FoC, all-types, file I/O, reset)
- Test coverage: version compatibility, architecture boundaries, Unicode, Delta002 special floats, golden-file wire format, VLE malformed encoding, crash resilience expansion (Cycle 5)
- Python: type stubs (.pyi), strict NaN mode for `write_dataframe()`, `pathlib.Path` support, `__repr__` methods (Cycle 4)
- C#: complete P/Invoke array declarations for all numeric types (Cycle 3)

### Removed
- Benchmark: `--tracking` CLI flag, `TrackingSelection` enum, and all related dead code from `bench_macro_datasets`
- Benchmark: `--macro-tracking` flag from `run.py` orchestrator
- Benchmark: `_TRK_SUFFIX` legacy label stripping regex from `constants.py`
- CLI: `--no-zoh` deprecated alias from `csv2bcsv`
- CLI: `--no-batch`, `--no-delta`, `--no-lz4` deprecated aliases from `bcsvSampler`
- C++: unused `bool silent` parameter from `PacketHeader::read()`
- Docs: stale "tracking" references from ARCHITECTURE.md, README.md, benchmark/README.md

### Fixed
- C++: string exceeding `MAX_STRING_LENGTH` now throws `std::length_error` instead of silent truncation (Cycle 2)
- C++: `Writer::close()` detects I/O errors during footer write (Cycle 2)
- C++: Sampler VM stack underflow guard (Cycle 2)
- C++: ZoH001 `reset()` clears reference state (Cycle 2)
- C++: Sampler compiler guards against string pool uint16_t overflow (Cycle 2)
- C#/Unity: added finalizers to all IDisposable classes (Cycle 3)
- Unity: replaced `PtrToStringAnsi` with `PtrToStringUTF8` at 6 locations (Cycle 3)
- Unity: added `[Preserve]` attributes for IL2CPP compatibility (Cycle 3)
- Docs: fixed broken URLs, corrected forward-compatibility documentation (Cycle 1)
- CI: benchmark workflow scoped ctest to only run built targets (exclude examples/pytest)
- CI: pybcsv publish workflow added `skip-existing` to PyPI publish step
- Unity: double-free in `BcsvRowBase.Layout` (now uses non-owning handle)
- Unity: replaced `UIntPtr.MaxValue` with .NET Standard 2.1 compatible cast

### Added (Unity / CI)
- Unity: UPM package structure (`package.json`, assembly definition, Samples~, .meta files)
- Unity: `FileFlags` enum entries: `NoFileIndex`, `StreamMode`, `BatchCompress`, `DeltaEncoding`
- CI: `unity-package.yml` — multi-platform native builds (win-x64, linux-x64/arm64, osx-arm64) with `.tgz` packaging
- CI: `upm-branch.yml` — auto-updates `upm` branch with pre-built native binaries for Git URL installs

---

## [1.4.3] - 2026-03-14

### Fixed
- CI: remove `branches-ignore` from release.yml to stop phantom failures

## [1.4.2] - 2026-03-13

### Fixed
- CI: smoke test uses `mkdtemp` to avoid file-exists error

## [1.4.1] - 2026-03-09

### Fixed
- CI: upgrade macOS to macos-15 runners (macos-14 deprecated July 2026)

## [1.4.0] - 2026-03-09

### Added
- Python: nanobind migration with Arrow C Data Interface, cross-platform CI
- Python: Polars integration via Arrow zero-copy (`read_polars`, `write_polars`)
- Python: `ReaderDirectAccess` for O(1) random access by row index
- Python: Sampler support (bytecode VM filter/projection)
- Python: CSV interop (`from_csv`, `to_csv`)

### Changed
- CI: use `uv` build frontend for Linux wheel builds
- Centralized `charconv` compatibility into `std_charconv_compat.h`

### Fixed
- macOS: Apple libc++ compatibility for `from_chars`/`to_chars`, `constexpr`, `static_assert`

## [1.3.0] - 2026-03-08

### Added
- Python (pybcsv): complete PyPI package with 128 tests, 22 exports, 4 examples
- Python: pandas DataFrame integration (zero-copy for numerics)

## [1.2.0] - 2026-02-28

### Changed
- Refactored FileCodec concept — slimmer Writer/Reader, internalized buffers & packet lifecycle
- Delta002 row codec with VLE encoding
- Footer index for random access

### Added
- xxHash64 checksums for data integrity
- Streaming LZ4 compression (batch mode)
- New PacketHeader format

## [1.1.2] - 2025-10-13

### Changed
- Updated ToDo.txt with benchmarking insights and streaming analysis

## [1.1.1] - 2025-10-04

### Added
- Python: `count_rows()` method

## [1.1.0] - 2025-10-04

### Added
- CLI tools: `bcsvHead`, `bcsvTail`, `bcsvHeader`

### Fixed
- Compiler issue in `bcsv_c_api.h`

## [1.0.3] - 2025-10-01

### Fixed
- CI: trigger on tag pushes for releases

## [1.0.2] - 2025-10-01

### Fixed
- CI: fetch Git tags for setuptools-scm version detection

## [1.0.0] - 2025-09-28

### Added
- Initial public release
- C++20 header-only library with streaming row-by-row I/O
- C API (`bcsv_c_api`) for language bindings
- Flat001 and ZoH001 row codecs
- Stream and packet file codecs
- 8 CLI tools (csv2bcsv, bcsv2csv, bcsvSampler, bcsvGenerator, bcsvValidate, bcsvHead, bcsvTail, bcsvHeader)
- Sampler bytecode VM for row filtering and column projection
- GTest suite with crash resilience tests

---

[Unreleased]: https://github.com/webertob/bcsv/compare/v1.5.3...HEAD
[1.5.3]: https://github.com/webertob/bcsv/compare/v1.5.2...v1.5.3
[1.5.2]: https://github.com/webertob/bcsv/compare/v1.5.1...v1.5.2
[1.4.3]: https://github.com/webertob/bcsv/compare/v1.4.2...v1.4.3
[1.4.2]: https://github.com/webertob/bcsv/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/webertob/bcsv/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/webertob/bcsv/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/webertob/bcsv/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/webertob/bcsv/compare/v1.1.2...v1.2.0
[1.1.2]: https://github.com/webertob/bcsv/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/webertob/bcsv/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/webertob/bcsv/compare/v1.0.3...v1.1.0
[1.0.3]: https://github.com/webertob/bcsv/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/webertob/bcsv/compare/v1.0.0...v1.0.2
[1.0.0]: https://github.com/webertob/bcsv/releases/tag/v1.0.0
