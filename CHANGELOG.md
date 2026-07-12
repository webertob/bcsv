# Changelog

All notable changes to BCSV are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/).

> **Maintenance:** This changelog is maintained manually. When tagging a release,
> update this file with the changes since the last tag.

---

## [1.5.10] - 2026-07-12

Includes MSVC/Windows build and test portability fixes.

### Added
- **Benchmark: `--no-validate` flag** — `bench_macro_datasets` can now time pure
  decode throughput (the default timed read loops include per-row validation,
  which understates decode speed several-fold — e.g. Flexible Delta reads
  1.15 M rows/s validated vs 7.0 M rows/s pure decode). Use for absolute
  claims and cross-format comparisons.
- **Benchmark: measurement methodology documented** — noise-floor study
  (`docs/archive/NOISE_FLOOR_2026-07-12.md`) quantifying repetition noise,
  warm-up effects, and the parallel-vs-solo regime offset; rules of thumb in
  `benchmark/README.md`; generated reports footnote the mixed-generator
  compression column and the validation-inclusive read timings.
- **NaN/±Inf bit-exactness guarantee, tested and documented** — the binary
  format round-trips every IEEE-754 bit pattern (NaN payloads, ±Inf, signed
  zero, subnormals) through all codecs × both layout APIs; 17-test matrix in
  `tests/nan_inf_test.cpp`, guarantee documented in README and
  `docs/INTEROPERABILITY.md`.
- **pybcsv: `write_dataframe(nan_policy=...)`** — new default `"preserve"`
  writes float NaN through bit-exactly (previously coerced to `0.0` with a
  warning — a silent data corruption for legitimate NaN data). `"coerce"`
  restores the legacy behavior; `"raise"` rejects NaN/None (equivalent to
  `strict=True`, which is retained). Non-float columns are still coerced with
  a warning under `"preserve"` (BCSV has no null type). Handles pandas
  nullable Float dtypes (`pd.NA` → `NaN`).

- **Sanitizer presets** — `clang-tsan` and `clang-ubsan` CMake presets
  (ThreadSanitizer / UndefinedBehaviorSanitizer, RelWithDebInfo). The batch
  codec's threading contract is documented in `docs/THREAD_SAFETY.md` and
  guarded by the TSan preset.
- **`bcsvCast` CLI tool** — generalizes column re-typing with four modes: `--scan`
  (report the smallest lossless type per column, read-only), `--optimize`
  (auto-derive and apply — the former `bcsvNarrowType` behavior), `--static SPEC`
  (apply caller-chosen types, saturating/rounding lossy cells), and `--dynamic SPEC`
  (apply a SPEC per column, skipping any column that would lose data). Adds a quoted
  type SPEC grammar (`'0=int32,7:8=float'` map form or `'int32,uint64,…'` positional
  list) with canonical + short type aliases, `--tolerance` (absolute epsilon; a larger
  tolerance lets `--optimize` narrow more aggressively — `|orig − new| ≤ tol` counts as
  lossless), and `--json` output (with a ready-to-reuse `suggested_spec`).
  Default mode: `--optimize` when an output path is given, else `--scan`.

### Changed
- **`bcsvNarrowType` removed — replaced by `bcsvCast`** (breaking, tooling). Migration:
  `bcsvNarrowType in out` → `bcsvCast in out` (or `--optimize`); `--stringsToValue` →
  `--string-to-value`; all other flags (`--cols`, `-o`, `--in-place`, `--overwrite`,
  `-v`) are unchanged. Library and wire format are unchanged (patch release).
- **`bcsvCast` apply always writes an output** — a no-op plan now still produces the
  output file (previously `bcsvNarrowType` skipped the write when nothing narrowed),
  so pipelines get a deterministic output path.

- `bcsv2parquet`: corrected the `--row-group-size` help text (the parameter is
  applied per streamed batch and is honored, not ignored).
- Documented `parquet2bcsv`/`bcsv2parquet` (usage, schema mapping, limitations) in
  the Python README; hardened the conversion + narrowing test suites with
  per-element, multi-batch, collision, ordering, empty-file, type/value-fidelity,
  and packet-size regression tests.

### Fixed
- **Batch codec: lost-wakeup hang on close** — `request_stop()` sets the stop
  flag outside the codec mutex, so the stop callback's CV notification could
  land while the background thread held the mutex between its (pre-stop)
  predicate check and going to sleep; the notification was lost and `close()`
  blocked forever in `join()`. Readily reproduced by rapid open()+close()
  cycles under load (empty-file tests hung ~30 % of parallel CI runs). The
  callback now takes the mutex before notifying.
- **Direct access: corrupt neighbor packets no longer poison valid reads** —
  the packet-checksum validation added earlier in this release read *through*
  the terminator into the next packet, so corruption in packet N+1 made the
  fully valid packet N unreadable; and a checksum failure left stale row-cache
  metadata that could serve the corrupt packet's rows under the previous
  packet's indices (or index out of bounds). New codec entry point
  `finishPacketRead()` consumes terminator + checksum and stops; the row cache
  is invalidated before any mutation.
- **Writer: rejected oversized rows poison the codec state — now enforced** —
  the `MAX_ROW_LENGTH` check runs after the serializer has committed the row
  into the ZoH/Delta reference state, so continuing to write after the throw
  silently corrupted the stream (a retry became a 0-byte ZoH repeat). The
  writer now refuses further rows until `flush()` resynchronizes at a packet
  boundary (or the file is closed; rows written before the rejection stay
  valid).
- **Writer: disk-full is reported for small (fully buffered) files** —
  `close()` now flushes before inspecting stream state and throws on failure;
  previously the physical write happened inside `stream_.close()` after the
  check, and a full disk was completely silent. Sync packet codec also
  verifies its footer write (parity with the batch codec).
- **pybcsv: `nan_policy="preserve"` handles object columns with `pd.NA`** —
  an object-dtype column containing `pd.NA` with a float `type_hint` crashed
  with a raw `TypeError`; it is now converted (`pd.NA`/`None` → `NaN`) like
  nullable extension dtypes.
- **Static-layout ZoH/Delta: `-0.0` silently became `+0.0`** — change detection
  used IEEE `operator==`, which treats `-0.0 == +0.0`, so a sign flip was
  encoded as "unchanged" and the decoder held the previous `+0.0`. Both static
  codecs now compare bit patterns (`bcsv::bitEqual`), matching the dynamic
  layouts' `memcmp` semantics. Side benefit: repeated NaN rows now compress
  via ZoH hold instead of being re-serialized every row (`NaN != NaN`).
- **Delta002: no FoC predictions through NaN arithmetic** — the encoder now
  declines first-order-constant encoding when the prediction is NaN, because
  NaN *payload* propagation through `prev + grad` is implementation-defined
  and the decoder recomputes that expression; the XOR-delta path taken instead
  is bit-exact on every platform. Encoder-only change, wire format unchanged.
- **csv2bcsv: a single `nan` cell no longer forces a column to DOUBLE** —
  the float-compatibility probe (`(double)(float)v != v`) is always true for
  NaN; non-finite values are now skipped (`std::isfinite` guard).

- **Writer now enforces `MAX_ROW_LENGTH`** — `writeRow()` throws when a serialized
  row exceeds the 16 MiB format limit. Previously the writer happily produced
  files that every read path rejects (and a row length could in principle
  collide with the packet terminator marker).
- **Flat001 no longer writes uninitialized memory for oversized strings** — the
  serialize pre-scan now clamps each string to `MAX_STRING_LENGTH` (64 KiB, the
  documented truncation), so the emitted row span contains exactly the bytes
  written. Previously a string > 64 KiB left uninitialized heap bytes in the
  row (written to disk — an information leak) and desynced the row framing.
- **Delta002 rejects invalid header length codes** — crafted input could
  declare more delta bytes than the column type holds, causing undefined
  behavior (shift past 64-bit width). Two-layer fix: `decodeDelta()` clamps
  its loop bound (total by construction — UB impossible regardless of caller;
  the provable trip count also lets the compiler fully unroll the loop), plus
  a cold-path validation that reports malformed files cleanly. Net effect
  measured **faster** than the unchecked baseline (decode −6 %, file-level
  delta reads +3.8 %); methodology and variant comparison in
  `docs/archive/B2_VALIDATION_COST_INVESTIGATION.md`.
- **Direct access validates packet checksums** — `ReaderDirectAccess::loadPacket()`
  now consumes the packet terminator + checksum for synchronous packet codecs,
  so random access rejects the same corrupt packets a sequential read would
  (the batch codec already validated whole packets on seek).
- **Hostile-input hardening** — `FileFooter::read()` validates `start_offset`
  before use (a crafted value < 28 underflowed `size_t`; large values could
  trigger multi-GiB allocations); the batch codec bounds declared packet sizes
  by the file header's packet size instead of the absolute 1 GiB limit (a
  40-byte crafted file could previously trigger ~2 GiB of allocations);
  `FileHeader` enforces a cumulative column-name cap (`MAX_HEADER_NAME_BYTES`,
  16 MiB) symmetrically on write and read.
- **Zero-length UB fixes** — `Row::clear()` and `Bitset::readFrom()/writeTo()`
  no longer call `memset`/`memcpy` with null pointers on empty layouts/bitsets
  (flagged by UBSan; full test suite is now UBSan-clean).
- **Compile-time endianness guard** — the wire format is little-endian;
  `definitions.h` now refuses to compile on big-endian targets instead of
  silently producing incompatible files. Stale comments fixed (packet-header
  checksum coverage, terminator value).
- **Benchmark: expected static-layout skips no longer fail the run** —
  profiles without a compile-time `LayoutStatic` reported `status: "error"`
  and forced exit code 1 (every full macro run "failed" cosmetically). They
  now report `status: "skipped"` and the exit code is 0 when only skips occur.
- **Batch codec: silent loss of the last packet on footer-less (crashed) files** —
  the background pre-read thread and the reader main loop raced on the shared
  stream state (`Reader::readNext()` polled `stream_.good()` while the background
  thread was reading). On crash-recovered files without a footer this dropped
  every row of the final complete packet; under ThreadSanitizer the reader could
  deadlock. `Reader::readNext()` now queries liveness through the codec
  (`FileCodecDispatch::readGood()`); the batch codec answers from main-thread-owned
  state and its background thread restores a defined stream state on every exit.
  Regression tests: `tests/batch_codec_recovery_test.cpp`.
- **Batch codec: data race on the background exception slot** — `bg_exception_`
  was read/written without synchronization (UB; errors could be missed). It is
  now guarded by the codec mutex and checked only at packet boundaries, flush,
  and finalize — never on the per-row fast path.
- **Batch codec: background write failures could be swallowed at close** —
  `finalize()` now rethrows a pending background exception unconditionally
  before writing the footer (a file with a failed packet never gets a clean
  footer), and verifies the footer write itself. `Writer::close()` records the
  error in `getErrorMsg()`, performs full cleanup, then propagates the exception.

- **`bcsvCast` double→int64/uint64 boundary (inherited from `bcsvNarrowType`)** —
  the range check compared against `static_cast<double>(INT64_MAX)`/`UINT64_MAX`,
  which round up to 2⁶³/2⁶⁴, so a value of exactly 2⁶³/2⁶⁴ passed the guard and
  overflowed (UB) on cast. Both the scan ladder and the coercion path now exclude
  `≥ 2⁶³` / `≥ 2⁶⁴` strictly.
- **parquet2bcsv/bcsv2parquet: FixedSizeList columns were transposed** — element
  `i` of a fixed-size list was extracted as a contiguous block
  (`child[i*N:(i+1)*N]`) instead of a strided per-element gather, scrambling the
  flat BCSV columns and corrupting streamed round-trips when read/write batch
  boundaries differ. Both the flatten and unflatten paths now use the correct
  (offset-safe) strided transform.
- **parquet2bcsv: name-collision escaping was one-way** — a struct path escaped to
  `a_.b` (because a literal `a.b` column existed) failed to extract with
  `Column 'a_' not found`. Escape suffixes are now stripped when navigating.
  `bcsv2parquet --unflatten` now fails loudly on truly ambiguous collisions instead
  of silently merging columns.
- **bcsv2parquet: `--columns` reordering was ignored on the default (unflatten)
  path** — output followed file order and disagreed with `--no-unflatten` and the
  empty-file fallback. The requested column order is now honored consistently.
- **parquet2bcsv: null-rejection reported a wrong (sometimes negative) row number**
  for sliced arrays with a non-zero offset. Null location is now offset-safe.
- **bcsvNarrowType: `--stringsToValue` could narrow strings to `FLOAT` losing
  precision** — the string path skipped the `double->float` round-trip check, so e.g.
  `"0.1"` became `0.1f`. It now falls back to `DOUBLE` when a value doesn't survive
  float32.
- **bcsvNarrowType: signed columns flipped to same-width unsigned for 0 bytes saved**
  — an all-non-negative `INT8/16/32` was "narrowed" to `UINT8/16/32` (a pointless
  signedness change). Same-width lateral flips are now suppressed (mirrors the
  existing `INT64->UINT64` guard).
- **bcsvNarrowType: source packet/block size was not preserved** — conversion reset
  the packet size to the default; it now reuses the input file's packet size,
  honoring the encoding-preservation invariant.

## [1.5.8] - 2026-07-04

### Added
- **CLI: `parquet2bcsv` and `bcsv2parquet`** — Streaming Parquet <-> BCSV conversion
  tools. `parquet2bcsv` converts Parquet files to BCSV with schema flattening,
  type widening (float16 -> float32), and NULL rejection. `bcsv2parquet` converts
  BCSV back to Parquet with optional schema unflattening, column selection, and
  row slicing. Both support `--benchmark` and `--json` timing output.

### Fixed
- `bcsvNarrowType` and `bcsvCompare` were missing from the CMake `install()` target;
  `bash scripts/install.sh` now deploys both tools.
- **bcsv2parquet: `--row-group-size` was ignored** — The CLI flag was accepted but
  silently discarded with a warning. The parameter is now passed through to
  `ParquetWriter.write_batch()`.
- **parquet2bcsv: nested field names ending with '_' were not rejected** — A Parquet
  struct field like `loc_` with child `lat` would flatten to `loc_.lat` which bypassed
  the trailing-underscore check. The check is now enforced on every path component
  in both flat and nested schemas.
- **bcsv2parquet: dead code after return** — Removed unreachable `unflatten_batch`
  call after function return.

### Changed
- **CLI: `bcsvNarrowType` argument redesign** — Mode is now inferred from positional
  arguments: `bcsvNarrowType INPUT` analyzes, `bcsvNarrowType INPUT OUTPUT` converts.
  The `--analyze` and `--convert` flags and `-f/--force` were removed (`-o/--output`
  is kept as an alias for the output positional). Added `--in-place` for in-place
  conversion (temp + atomic rename), `--overwrite` to permit replacing an existing
  output (an existing output now errors without it), and `--cols SPEC` to restrict
  narrowing to selected column indices (e.g. `0:3,5,7:-1`, negative indices count
  from the end).
- CLI: Promoted the index-range parser (`IndexRangeSet` / `parseIndexRanges`) into
  `cli_common.h`; `bcsvCompare` now shares it.

## [1.5.7] - 2026-04-19

### Fixed
- **C++: Writer codec/flag mismatch** — When `DELTA_ENCODING` was passed in `FileFlags` but the
  Writer's compile-time codec was `RowCodecFlat001`, the file header advertised delta encoding
  while rows were serialized as flat. The Reader's runtime dispatch (`RowCodecDispatch`) would
  select Delta002 for deserialization, causing data corruption after ~128 rows and
  "Buffer too small for string payload" crashes. `Writer::open()` now strips row-codec flags
  from user input and sets them exclusively via `RowCodecFileFlags<CodecType>`, guaranteeing the
  header always matches the actual codec.
- CLI: `withWriter()` in `cli_common.h` now uses explicit `WriterFlat<>` for the flat codec
  path instead of bare `Writer<>`, ensuring `--row-codec flat` produces flat-encoded files
- Tests: `CoverageGapsTest.Delta002SpecialFloats_FullFileIO` now uses explicit
  `Writer<Layout, RowCodecDelta002<Layout>>` instead of the default Writer (was accidentally
  testing flat, not delta)
- Tests: Adjusted file-size regression threshold in `Ref_WriteThroughFileRoundTrip` from 1000
  to 500 bytes (delta encoding produces smaller files)
- Python: Synced Python include headers with main library headers

### Changed
- **C++: Default row codec is now Delta002** — `Writer<Layout>` uses `RowCodecDelta002` as its
  default template parameter instead of `RowCodecFlat001`. All new files written with the default
  Writer get delta encoding automatically. Use `WriterFlat<Layout>` for explicit flat encoding.
- C++: Added `ROW_CODEC_FLAGS_MASK` constant to `definitions.h` for safe flag manipulation
- Docs: README, ARCHITECTURE.md version references updated to 1.5.7

## [1.5.6] - 2026-04-19

### Added
- Docs: Architectural Decision Records system (`docs/adr/`) with 5 initial ADRs covering
  error model, version checks, endianness, uint32_t offsets, and VLE buffer strategy
- CI: Dependabot configuration for weekly GitHub Actions version updates (`.github/dependabot.yml`)
- C++: ColumnType bounds validation on file read — rejects values outside the defined enum range
- C++: Packet size validation on file read — rejects values outside `MIN_PACKET_SIZE..MAX_PACKET_SIZE`
- C++: Debug assertion in Delta002 serialize loop to catch buffer overruns during development

### Changed
- C++: `Writer::open()` default flags changed from `NONE` to `BATCH_COMPRESS` — new files
  use packet-mode compression by default, harmonizing with Python and C# bindings
- CLI: `csv2bcsv`, `bcsvGenerator`, `bcsvSampler` now use `DEFAULT_PACKET_SIZE_KB` constant
  instead of hard-coded `64`
- CI: All 9 GitHub Actions pinned to commit SHAs for supply-chain security
- CI: Cache keys now use `hashFiles('**/CMakeLists.txt')` to bust on any CMakeLists.txt change
- CMake: Warning/error flags wrapped with `$<BUILD_INTERFACE:...>` to avoid leaking into
  downstream consumers (except `-fexperimental-library` needed by Apple targets)
- Docs: README, ARCHITECTURE.md, SECURITY.md version references updated to 1.5.6
- Docs: SKILLS.md and copilot-instructions.md updated with ADR references

### Removed
- CMake: Removed dead-end `ninja-asan` and `ninja-coverage` presets from CMakePresets.json
  (no CI jobs used them)

## [1.5.5] - 2026-04-19

### Fixed
- CMake: `GetGitVersion.cmake` now uses `CMAKE_CURRENT_SOURCE_DIR` instead of `CMAKE_SOURCE_DIR`,
  fixing version detection when bcsv is consumed via `FetchContent` or `add_subdirectory`
- Python: pybcsv wheels on PyPI now embed the correct version instead of `0.0.0` — added `VERSION.txt`
  file as single source of truth for non-git builds (sdist, FetchContent without git tags)
- CMake: `install(TARGETS ...)` for CLI tools is now guarded by `if(BUILD_TOOLS)`, preventing
  configure failure when `BUILD_TOOLS=OFF`
- CI: Fixed C++ CI failures on Windows MSVC and macOS Apple Clang — `python3` replaced with
  portable `python` command, and `Python3_ROOT_DIR` hint added to CMake configure step to ensure
  CTest uses the same Python interpreter as pip

## [1.5.4] - 2026-04-12

### Fixed
- C++/Python: Default packet size corrected from 64 KB to 8 MB (`DEFAULT_PACKET_SIZE_KB = 8192`)
  — `Writer::open()` `blockSizeKB` default, pybcsv `Writer.open()` default, and `write_columnar_core`
    all now consistently use the new named constant

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
- 9 CLI tools (csv2bcsv, bcsv2csv, bcsvSampler, bcsvGenerator, bcsvValidate, bcsvHead, bcsvTail, bcsvHeader, bcsvRepair)
- Sampler bytecode VM for row filtering and column projection
- GTest suite with crash resilience tests

---

[Unreleased]: https://github.com/webertob/bcsv/compare/v1.5.6...HEAD
[1.5.6]: https://github.com/webertob/bcsv/compare/v1.5.5...v1.5.6
[1.5.5]: https://github.com/webertob/bcsv/compare/v1.5.4...v1.5.5
[1.5.4]: https://github.com/webertob/bcsv/compare/v1.5.3...v1.5.4
[1.5.3]: https://github.com/webertob/bcsv/compare/v1.5.2...v1.5.3
[1.5.2]: https://github.com/webertob/bcsv/compare/v1.5.1...v1.5.2
[1.5.1]: https://github.com/webertob/bcsv/compare/v1.5.0...v1.5.1
[1.5.0]: https://github.com/webertob/bcsv/compare/v1.4.3...v1.5.0
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
