# Changelog

All notable changes to BCSV are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/).

> **Maintenance:** This changelog is maintained manually. When tagging a release,
> update this file with the changes since the last tag.

---

## [1.5.17] - 2026-08-25

### Added

- **`BcsvMetadata.ReadCompanion` can skip the SHA-256 verification**
  (C# and Unity): `ReadCompanion(path, expectedRows, verifyDigest: false)` keeps
  the cheap `bcsv_bytes` / `bcsv_rows` pre-checks and never reads the BCSV file.
  Requested by a consumer whose replay path opens multi-gigabyte recordings
  through random access: hashing to establish provenance made a windowed reader
  read the whole file on every open — 948 MB, ~0.5 s warm, per scene load — which
  is the exact cost `Read(long index)` exists to avoid. The digest remains the
  identity check and remains the default; skipping it leaves a heuristic, since
  two recordings of the same shape can share size and row count. Verify once
  where a file enters a project, skip it per open.

  Added as a separate three-argument overload rather than a third optional
  parameter: C# bakes default argument values into the call site, so an added
  optional parameter would break callers already compiled against the assembly.

- **`BCSV.BcsvPlayer`, the recorder's mirror** (Unity package only) — plays a
  recording back into a running scene, driven by the same `BcsvSampleClock` and
  the same `Advance(dt)` / `Trigger()` pacing, with channels bound by name to
  setters. It deliberately does not read the recording's own timestamps: a
  recording's time channel is a column like any other and the component cannot
  know which one, so rows are presented at the rate asked for and the file's time
  arrives as an ordinary binding. Execution order -1000, the mirror of the
  recorder's +1000. See `unity/CHANGELOG.md` for the full entry.

- **`BCSV.BcsvRecorder`, a supported Unity Runtime component** (Unity package
  only) — promoted from `Samples~`, where using it meant copying it and a copy
  stopped receiving fixes. Adds a first-class `sampleRateHz` backed by
  `BcsvSampleClock`, `[DefaultExecutionOrder(1000)]` so it runs last in a
  `FixedUpdate` step, and external pacing via `Advance(dt)` / `Trigger()`. It
  writes no time column: that is a channel the caller defines. Sampling is chosen
  per channel — zero-order hold by default, or a boxcar average that genuinely
  anti-aliases when recording slower than the host runs, or linear interpolation
  onto the sample instant. See
  `unity/CHANGELOG.md` for the full entry, including the three sample defects it
  does not carry forward.

- **`BcsvSampleClock`** (C# and Unity) — the decimator every recorder built on
  this library was writing for itself. `Advance(dt)` reports how many sample
  instants fell inside the host step just taken: 0 on most steps when recording
  slower than the simulation, 1 when the rates match, k when a step spans several
  periods. Callers loop over the count and never branch on the regime.

  It exists because the naive version of this is wrong in a way nothing reports.
  Resetting the accumulator to zero on each edge — rather than carrying the
  remainder — silently rounds the requested rate down to a divisor of the host
  step rate: on a 1 kHz simulation, 300 Hz becomes 250 Hz, 400 Hz becomes 333 Hz,
  and 999 Hz becomes 500 Hz. That defect has now been written at least twice by
  consumers of this library, once by re-implementing a recorder rather than using
  the packaged sample that already avoided it. Carrying the remainder makes the
  individual intervals uneven and the mean rate exact, which is the trade a
  recording wants.

  No time base, no time column, no file access: a recording's time channel stays
  the caller's to define, in whatever unit and width it wants. The phase is
  single-precision, which is sufficient *because* the clock advances by relative
  intervals only and never accumulates absolute time — the error is a bounded
  random walk of about 2e-7 s over an hour at 1 kHz, not a drift. That stops
  holding the moment a clock wants a ppm-class rate offset, which this one does
  not.

  `EdgeFraction(index, dt)` reports where an edge fell inside the step it was
  advanced by, which is what a caller sampling once per step needs in order to
  place a row at the sample instant rather than at the step boundary.

- **A writer can now report the flags it actually wrote** — `Writer::fileFlags()`
  (C++), `bcsv_writer_file_flags` (C), `BcsvWriter.FileFlags` (C# and Unity),
  `Writer.file_flags()` (Python). Until now only a *reader* could answer that, so
  checking what a file carried meant closing it and reopening it.

  This exists because the answer is not the question. `Open`'s `flags` argument
  does not decide the row codec — the writer strips those bits and sets them from
  its own codec, so a header can never claim a codec the rows were not written
  with. That is deliberate and correct; what it was not is visible. Asking for
  `BATCH_COMPRESS` alone gives a header of 24 with the delta codec, 9 with zoh
  and 8 with flat, and nothing said so. Documented under "File Flags, and the two
  that are output-only" in `docs/API_OVERVIEW.md`, on the enum members
  themselves, and in XML docs on `Open`/`TryOpen`, which had none.

- `tests/version_gate_test.cpp` — the file-format version rules (VERSIONING.md
  Rules A/B/C) had no test. Eight cases stamp patched version bytes into real
  files and assert the reader's verdict, including the case a MINOR relies on: a
  file from a newer minor is refused outright rather than parsed to the point
  where a new header section has moved the packet stream. One case pins the
  boundary deliberately — an unknown `FileFlags` bit on its own is *not* a gate,
  so a feature bit must ship with the `version::MINOR` bump that gates it.

### Fixed

- **`BcsvSampleClock.TryFromRate`** — a non-throwing counterpart to `FromRate`,
  for a caller holding a rate it did not choose. The inline test it replaces,
  `hertz > 0 ? FromRate(hertz) : EveryStep()`, is wrong at both ends: a negative
  rate and a NaN both fail the comparison and come back as "every step", while an
  infinite one passes it and throws. Both Unity components used that pattern and
  built their clock *after* opening the file, so an infinite rate threw with a
  native handle already in hand.

- **`scripts/check-unity-package.sh` now rejects duplicate asset guids**, and
  `.gitattributes` gained Unity YAML handling. A `.meta` file's guid is how every
  scene and prefab in a consuming project refers to an asset, and a duplicate is
  the one `.meta` fault nothing reports at import time — references simply rebind
  to whichever asset resolves first, in someone else's project, weeks later.
  `.meta` files are also near-identical to each other apart from that guid, which
  makes git pair them across unrelated paths as renames; that was observed while
  moving the recorder out of `Samples~` in this release, not hypothesised. Scene,
  prefab and `.asset` files are now routed to `UnityYAMLMerge`, with the
  per-machine merge-driver configuration written down beside the rule, since the
  attribute does nothing without it.

- **`bcsv_writer_create` returned a delta writer, so the flat row codec was
  unreachable from the C API and from both C# bindings.** It is documented as the
  flat writer and tagged its handle `Type::Flat`, but constructed
  `bcsv::Writer<bcsv::Layout>` — which takes the *default* template argument,
  `RowCodecDelta002`. `bcsv_writer_create_zoh` and `_delta` name their codecs
  explicitly; this one did not, and picked up delta by omission.

  `new BcsvWriter(layout, "flat")` therefore produced a delta-encoded file, in
  both the NuGet and Unity packages, which route `"flat"` through this entry
  point. Python was unaffected — its binding constructs `WriterFlat` directly,
  which is why `pybcsv` could produce flat files and nothing else could.

  **No data was ever wrong and no file needs re-encoding.** The output was a
  valid delta file whose header honestly said delta; the request was what got
  lost. Only someone comparing a file's flags against what they asked for would
  have noticed, and before this release there was no writer-side way to do that.
  Guarded now on both paths: a C API test asserting the three constructors
  produce three different codecs and that writer and reader agree on each, and
  `WriteRead_AllCodecs` in the C# suite, which round-tripped all three codecs
  throughout without noticing — every codec reproduces the data correctly, so
  only the header distinguishes them. It checks the header now.

- **`BcsvWriter` rejects an unknown row codec instead of silently using delta.**
  The C# constructors mapped `"flat"` and `"zoh"` and sent *everything else* to
  delta, so `"Delta"`, `"zho"` or a typo recorded happily in a codec nobody asked
  for. It throws `ArgumentException` now, which is what `pybcsv` has always done
  — the two bindings disagreed, and C# was the one that was wrong.

- **`pybcsv.FileFlags` could not express a combination of flags at all.** It was
  bound with `nb::is_arithmetic()`, which nanobind maps onto `enum.IntEnum`, and
  bound `__or__`/`__and__`/`__invert__` by hand to return a bare `int`. So
  `BATCH_COMPRESS | NO_FILE_INDEX` produced `10`, the `FileFlags` parameter of
  every write function rejected that as the wrong type, and `FileFlags(10)`
  raised `ValueError`. There was no way to ask for two flags at once from Python.

  It is now `nb::is_flag(), nb::is_arithmetic()`, which nanobind maps onto
  `enum.IntFlag` — the one shape that both holds a combination and remains an
  `int`. The hand-written operators are gone; `IntFlag` supplies them. Unknown
  bits are preserved rather than rejected, so a file written by a newer minor
  version still reads. `Reader.file_flags()` returns a `FileFlags` rather than a
  bare `int`, so the round-trip is symmetric. C# was never affected.

  The tests that were here passed throughout, because they only asserted that a
  combination differs from `NONE` and that `int()` of one is non-zero — both true
  of a bare `int`. They assert the type and a round-trip through a real file now.

- **`Reader::open` no longer replaces the reason a header was rejected with a
  generic message.** `readFileHeader()` records the specific failure —
  `"Error: Incompatible file version: 1.6 (Expected: 1.5 or earlier)"`,
  `"Invalid magic number..."`, `"Column type mismatch at index N..."` — and
  `open()` then threw `"Failed to read file header"`, whose text overwrote it in
  the catch. Every caller of `getErrorMsg()`, in every binding, saw the generic
  string; `docs/ERROR_HANDLING.md` has documented the specific ones as
  observable since 1.5.0. Found by the version-gate test above.

---

## [1.5.16] - 2026-08-25

### Changed

- **The default LZ4 compression level is now 6 (was 1).** This changes the bytes every
  BCSV writer produces, so it is called out first. On a 23.2 GB corpus of wide sensor
  recordings (300-1052 columns) the default output went from **1.49x the size of the
  equivalent Parquet to 1.08x** — the reported "BCSV files are 30-50 % larger" gap, closed
  by a configuration change.

  The old default was the weakest setting LZ4 offers. `compression_level` selects between
  two different compressors, and level 1 meant `LZ4_compress_fast` at *acceleration 9*.
  Levels 1-5 land within 4 % of each other on real data; the step from 5 to 6 is where
  `LZ4BlockCompressor` switches to LZ4HC, and it is worth ~27 % on its own. Levels 7-9 add
  well under 1 % for substantially more CPU, so 6 is the knee of the curve, not the maximum.

  **The wire format is unchanged and this is backward compatible in both directions.**
  LZ4HC emits ordinary LZ4 blocks; `LZ4BlockDecompressor` reads both kinds without being
  told which, and a reader only ever tests `level > 0` (`resolveFileCodecId`). Verified by
  having a **BCSV 1.5.10** binary read files written at the new default and render
  byte-identical CSV. Nothing needs re-encoding, and older readers are unaffected.

  **Both the win and the cost scale with how wide and idle the data is**, so quote the range,
  not a single number. On the wide sensor corpus above (650-1052 columns, most channels
  constant) level 6 is **-28 % size for +48 % write CPU** — the long-range redundancy is
  exactly what LZ4HC's larger search window finds. On the synthetic macro benchmark profiles
  (50-84 columns, mostly changing) the same switch is **-5 % size for +7 % write CPU**: little
  redundancy to find, so little to pay for. Narrow, fast-changing writers barely notice this
  change in either direction.

  Read speed is not affected: a full sequential decode of the 950-column file takes 0.20 s at
  level 1, at level 9 and uncompressed alike, because LZ4 decompression is not the bottleneck.
  Callers who need the old write throughput can pass `compression_level=1` explicitly; the new
  default is exposed as `bcsv::DEFAULT_COMPRESSION_LEVEL`.

  The `packet_lz4` and `stream_lz4` codecs compress per row and never reach LZ4HC, so for
  them the new default means acceleration 4 instead of 9: measured at -3.9 % size for
  +1.3-2.0 % write time. They were left on the same dial rather than special-cased.

- The level -> compressor mapping is now documented where users can see it
  (`docs/INTEROPERABILITY.md`, `bcsv::DEFAULT_COMPRESSION_LEVEL`). "Level 1-9" reads like a
  smooth dial and is not: it is two compressors with a cliff at 6, and which one you get
  depends on the file codec. `src/tools/CLI_TOOLS.md` also carried a stale `--block-size`
  default of 64 KB; the tools have used 8192 KB for some time.


- **The charconv fallback now accepts exactly what `std::from_chars` accepts.**
  `strtod` is more permissive than `from_chars`: it skips leading whitespace,
  takes a leading `'+'`, and reads hex literals such as `0x1p3`. So the same CSV
  cell could parse on macOS and be rejected on Linux. The fallback now scans the
  input against the `from_chars` grammar first and hands `strtod` only the
  matched prefix. **This makes macOS stricter**, matching the behaviour Linux and
  Windows have always had — a file that relied on `+1.5`, a leading space, or a
  hex literal parsing on macOS was already failing everywhere else. Verified
  against `std::from_chars` over 400 000 randomised inputs with no divergence.
- **The fallback no longer allocates per parsed value.** It built a
  `std::string` for `strtod`'s null termination on every cell. The grammar scan
  gives the length up front, so short numbers now use a stack buffer and only
  pathological literals reach the heap — measured at zero allocations for 9 000
  parses. This matters most on the embedded targets, which are the platforms
  that actually take this path.
- **The charconv fallback is now compiled on every platform**, not just the ones
  that use it. It previously lived inside `#if !BCSV_HAS_FLOAT_CHARCONV`, so on
  Linux it was never even parsed — which is how both bugs above survived a green
  CI. The implementations moved to `bcsv::compat::fallback`, the feature test
  now only selects a dispatcher, and `BCSV_HAS_FLOAT_CHARCONV` is overridable so
  a build can force the fallback path. New `tests/charconv_compat_test.cpp`
  exercises it directly on all platforms, including a differential suite that
  pins it against `std::from_chars`/`to_chars` wherever both exist.

### Performance

- **The Python Arrow and columnar read paths are roughly 2x faster on wide files.** Building
  column-major output from a row-wise file ran a `switch` over `ColumnType` for every single
  cell — 132 M dispatches for a 950-column x 139 464-row recording, with the types
  interleaved within each row so the indirect branch mispredicted constantly. Columns are now
  bucketed by type once per batch and each bucket drained by a loop with a compile-time type,
  mirroring what the row codecs already do with `forEachScalarType`.

  Measured on that file: full `iter_arrow_batches` scan **0.98 s -> 0.46 s**, `read_to_arrow`
  0.91 s -> 0.43 s, and the columnar write path 1.43 s -> 1.17 s. Applies to `read_batch`,
  `read_arrow_batch`, `read_columns`, `read_to_arrow`, `write_columns` and
  `write_from_arrow`. No API change.

  For reference the underlying format decodes the same file in 0.20 s using 9 MB of RSS, so
  the Arrow bridge is now ~2.3x the raw decode rather than ~4.8x. The remainder is the
  transpose itself — 950 concurrent output streams — which needs a tiled copy to improve
  further.

### Fixed

- **The new default compression level was not actually applied everywhere.** The first pass
  changed `Writer::open` and `BcsvWriter.Open` but left several user-facing entry points
  writing level 1, so the same data compressed differently depending on which API you
  called: `BcsvColumns.WriteColumns` (C# and Unity), `parquet2bcsv --compression-level`
  (whose argparse default said 1 while the `parquet_to_bcsv()` function default said 6), and
  the pandas/polars wrappers. Every one of them now resolves the level from a single named
  constant — `BcsvDefaults.CompressionLevel` in C#/Unity, `pybcsv.DEFAULT_COMPRESSION_LEVEL`
  (newly exported from the native `bcsv::DEFAULT_COMPRESSION_LEVEL`) in Python — so there is
  no literal left to drift. `python/README.md`'s documented signatures still showed level 1
  and were corrected.

  The regression tests assert on the **written file header** (`Reader.compression_level()`,
  `BcsvReader.CompressionLevel`) rather than on source literals, since a source-literal check
  is exactly what would have passed while the files disagreed. See
  `python/tests/test_default_compression_level.py` and
  `csharp/tests/Bcsv.Tests/BcsvDefaultsTests.cs`.

  The benchmark harness (`bench_macro_datasets.cpp`) deliberately keeps level 1, documented
  in place: every stored baseline under `benchmark/results/` was produced at that level.

- **The source distribution did not compile.** `python/include/` and `python/src/` are
  generated copies of the project headers and CLI sources — they are what an sdist ships and
  therefore what a standalone build compiles, but nothing kept them in step with `include/`.
  A header change committed without re-running `sync_headers.py` left the packaged copy
  without `bcsv::DEFAULT_COMPRESSION_LEVEL` while the packaged `bindings.cpp` referenced it,
  so building a wheel from the sdist failed. Builds from the project root stayed green
  because CMake prefers the parent `include/`, which is precisely what hid it.

  `python/CMakeLists.txt` now re-runs `sync_headers.py` at configure time, and
  `sdist.cmake = true` makes that happen before an sdist is assembled, so the bundled copies
  cannot lag. A new `test-sdist` CI job builds a wheel from the generated sdist and runs the
  suite against it — the only job that compiles what we publish as source — and publishing
  now depends on it.

  While there: the sdist no longer ships `python/dist/`, which was carrying stale build
  output (a 1.5.7 wheel inside a 1.5.16 sdist). 2.1 MB → 632 KB.

- **`bcsv_to_parquet` could not reconstruct `FixedSizeList<struct<...>>` columns.**
  `parquet_to_bcsv` flattens a list of structs into `field[i].subfield` columns, but the
  reverse direction never learned that shape, so any BCSV holding one failed to convert
  with `ValueError: Cannot find array for 'imu[0]'`. Present since the Parquet bridge
  landed; the two directions were asymmetric. No data was ever lost or silently corrupted
  — the flat columns in the `.bcsv` were always correct and readable via
  `read_to_arrow()`; only the conversion back to nested Parquet failed, and it failed
  loudly. Two defects had to be fixed together:
  - `_trie_to_arrow_field` guessed `pa.int64()` for any list element it could not read as
    a scalar type, quietly rebuilding `list<struct<x, y>>` as `list<int64>`. Element types
    are now built by recursion, so structs, nested structs, and lists inside list elements
    all reconstruct; there is no fallback type left to guess wrong.
  - `_build_nested_array` looked for one flat column per list element, which cannot exist
    when the element is a struct. It now recurses into nested element types, exactly as
    the struct branch already did.
- **Four silently-wrong unflatten shapes now raise instead.** Each produced a confusing
  downstream error or dropped a column: list indices with a gap (`x[0]`, `x[2]`) claimed a
  dense size and then failed looking for `x[1]`; a name used as both list and struct
  (`a[0]` plus `a.b`) dropped the indexed columns; a name that is both a leaf and a path
  prefix (`a` plus `a.b`) either crashed with an opaque `TypeError` or discarded one of the
  two columns; and elements of one list disagreeing on type took element 0's type for all
  of them. All four now say what is wrong and point at `--no-unflatten`.

- **Two silent wrong-value bugs in the float charconv fallback**
  (`bcsv::compat`, used for CSV parsing and formatting). Both were reachable
  only where the standard library lacks the floating-point `std::from_chars` /
  `std::to_chars` overloads — Apple libc++, and likely the STM32/Zynq toolchains
  BCSV targets. Linux and Windows dispatch to `std::` and were never affected.
  - **A representable subnormal was discarded.** C permits `strtod`/`strtof` to
    raise `ERANGE` when the result underflows to a subnormal, and both glibc and
    Apple's libc do. The shim treated any `ERANGE` as fatal and returned
    `result_out_of_range` without assigning the value, so `CsvReader` kept its
    zero-initialised `0` *and* counted a parse error. Now only overflow to ±inf
    and flush-to-zero are reported, matching `std::from_chars`, which also leaves
    the caller's value untouched on a genuine range error. Caught by macOS CI as
    `NanInfFileTest.CsvBridgeSpecialValues`.
  - **Parsing and formatting followed the process locale.** `strtod` and
    `snprintf("%g")` read and write the *locale's* decimal point, while this
    shim's contract — like `std::from_chars` — is always `'.'`. Under a
    comma-decimal locale `strtod("1.5")` returns `1` and `to_chars(1.5)` emitted
    `"1,5"` into CSV. This collided directly with the `decimal_sep_` feature,
    which normalises the user's separator *to* `'.'` before parsing. Both
    directions now translate at the boundary rather than touching the process
    locale, which is global and unsafe for a library to change.

---

## [1.5.15] - 2026-08-24

Answers all of R1–R4 of the `diss-digital-twin` T13 requirements: a null policy
for the Parquet transcoder, a file-level metadata JSON companion readable from
Python and C#/Unity, a corrected feature matrix, and a seekability guard.
**The wire format is unchanged** — every 1.5.x reader opens files written by
this code, so this is a PATCH under the clarified rule in `VERSIONING.md`.

### Added

- **`parquet_to_bcsv(null_policy=...)` / `parquet2bcsv --null-policy`** — BCSV
  has no null type, and until now a single Parquet null aborted the whole
  conversion. `"reject"` (the default) keeps exactly that behaviour and its
  message. `"nan"` fills nulls in **float** columns with `NaN` — a real
  IEEE-754 value BCSV already round-trips bit-exactly — and still aborts on
  every other type, naming the column and row, because there is no such value
  for an integer or a bool. `"zero"` fills **every** column with the BCSV
  default (`0` / `False` / `""`) — what an unset BCSV cell already holds — and
  is the only policy that can carry integer, bool or string nulls; it is
  unconditionally lossy, since a filled zero is indistinguishable from a
  measured one. The fill is explicit rather than left to `write_batch`'s
  `to_numpy()` conversion, which would turn an integer null into `INT_MIN`
  rather than `0`. The returned dict gains `nulls_filled`.
- **File-level metadata JSON companion** — `parquet_to_bcsv` writes the
  source's Parquet key/value metadata (minus pyarrow's internal `ARROW:schema`)
  to `<output>.meta.json` together with the source's SHA-256, and
  `bcsv_to_parquet` restores it into the output footer. The document records
  the BCSV file's SHA-256 and is refused if it does not match, so it cannot be
  applied to data it does not describe. Controlled by `metadata2json=` /
  `--no-metadata2json` on the writer and `json2metadata=` / `--no-json2metadata`
  on the reader, plus `source_hash=` / `--no-source-hash`, `bcsv_hash=` /
  `--no-bcsv-hash` (skips the digest, leaving only a size-and-row-count
  heuristic), and `metadata=` for an explicit override. Optional in both directions: the
  `.bcsv` never depends on it, and a conversion back to Parquet without one
  simply carries no key/value metadata. BCSV's 24-byte header has no key/value
  section; an in-format channel is planned for 1.6.0 and this one will keep
  working when it lands.

- **`BcsvMetadata.ReadCompanion()` for C# and Unity** — reads the
  `<file>.bcsv.meta.json` companion, so a C#/Unity consumer can check a file's
  provenance (release id, contract marker) without a Python round trip. Returns
  `null` when absent, throws `BcsvException` when malformed or when the document
  does not describe the file beside it. No third-party dependency: Unity 2021.3
  has no `System.Text.Json`, so the JSON reader is hand-rolled and internal.
  **Transitional — scheduled for deletion.** It exists only because the format
  has no metadata section; 1.6.0 adds one as `BcsvReader.Metadata` and this class
  goes with it, after a deprecation release. See item E12 in `ToDo.md`. Point one
  call site at it rather than building a layer on top.

### Changed

- **`parquet_to_bcsv` warns when the output is not randomly addressable** — the
  `stream` and `stream_lz4` file codecs write no packets and no footer, so
  `ReaderDirectAccess` / `BcsvReader.Read(index)` fail on the result and cannot
  rebuild an index. The `packet*` codecs (including the default) always wrote
  one and still do; only the warning is new.
- **CI runs the pybcsv test suite on every push and pull request** — a new
  `pybcsv` job builds the bindings and runs `python/tests/`. That directory is
  not registered with ctest (only `tests/integration/*.py` is), so the entire
  Python suite, including the Parquet transcoders, previously ran only in the
  tag-driven publish workflow: a regression landed on master and surfaced in the
  middle of a release.
- **`docs/API_OVERVIEW.md` and `VERSIONING.md` now say what lock step means** —
  all five channels ship one version number because it is also the file-format
  version, so version parity is not feature parity, and the feature matrix is
  the record of what each binding actually exposes.
- **`VERSIONING.md` now states the rule the project actually follows** — MINOR
  is for changes that move the wire format; additive API surface that leaves it
  untouched (a keyword argument, a CLI flag, a binding method) ships as PATCH.
  Because the version number is also the file-format version, spending a MINOR
  on a language-binding addition would falsely signal a format change to every
  reader. Adds the deciding question and a worked example.

### Fixed

- **A stale `<file>.meta.json` could stamp unrelated data with someone else's
  provenance** — the document is addressed only by path, so re-converting to an
  existing output name while suppressing metadata left the previous document in
  place, and `bcsv_to_parquet` then applied it to the new data. Writing a BCSV
  file without a new document now deletes any stale one, and each document
  records the BCSV file's SHA-256 (plus byte size and row count as cheap
  pre-checks) so a mismatched one is refused rather than applied. Size and rows
  alone were a heuristic — two recordings of the same shape share both — which
  is why the digest is the actual binding. Found in review before release.
- **`python/VERSION.txt` was committed and three releases stale (1.5.11)** while
  `check_versions.py` reported "all version stamps agree". It is a
  `sync_headers.py` artifact consumed only by sdist builds, where it is the
  fallback `python/CMakeLists.txt` reads — so an sdist built without running
  `sync_headers.py` first stamped 1.5.11 silently. This is the same shape as the
  v1.5.12 incident documented in `VERSIONING.md`. The file is now gitignored,
  `check_versions.py` fails on a stale copy if one is present, and
  `update_version.sh` refreshes it when it exists.
- **Documentation: `Columnar bulk I/O` was marked unsupported for C and Python**
  in the `docs/API_OVERVIEW.md` feature matrix. Both have it — C through
  `bcsv_reader_read_columns` and the vectorized bulk get/set block, Python
  through `read_columns` / `write_columns` (covered by
  `test_pybcsv_columnar.py`). Only the C++ core genuinely lacks a first-class
  API, per backlog item 23.a. Found by auditing the whole matrix after the delta
  row below turned out to be wrong.
- **Two pandas tests failed instead of skipping when pandas was absent** —
  `test_pandas_roundtrip` and `test_csv_conversion` in
  `python/tests/test_pybcsv_pandas.py` are plain pytest functions, so the
  `skipUnless` on the surrounding TestCase class did not cover them. They now
  carry their own guard, which is the correct behaviour for an optional
  dependency.
- **Documentation: the C# feature matrix wrongly showed delta encoding as
  unsupported** (`docs/API_OVERVIEW.md`). C# contains no codec logic at all —
  `BcsvWriter` selects the codec through `bcsv_writer_create_delta` and reading
  dispatches natively from the file header flags. Covered by existing tests in
  `csharp/tests/Bcsv.Tests/BcsvTests.cs` and `tests/bcsv_c_api_full_test.c`.

---

## [1.5.14] - 2026-08-22

Unity package hygiene. The package no longer logs a warning on every domain
reload, and packing now decides what ships instead of copying a directory and
hoping.

### Changed
- `unity/tools/build-windows.ps1` moved to `scripts/build-unity-windows.ps1`.
  It is a repository build script, not package content, and living under
  `unity/` meant it shipped to consumers without a `.meta` — logging "has no
  meta file, but it's in an immutable folder" on every domain reload for anyone
  who installed the package. `unity/` now holds only what a consumer installs,
  and `scripts/pack-unity.sh` selects that content explicitly while
  `scripts/check-unity-package.sh` refuses to ship a package whose `.meta` files
  are missing, orphaned, or malformed.

### Fixed
- **`.gitignore` no longer swallows Unity's hidden package directories.** The
  editor-backup rule `*~` matches directories as well as files, so everything
  under `unity/Samples~/` was ignored and files added to a sample never reached
  the repository — which is why the Basic sample shipped without `.meta` files.
  A `!*~/` negation re-includes the directories while still ignoring backups.

---

## [1.5.13] - 2026-08-21

Version-stamping fix. `VERSION.txt` is now the single source of truth for every
distribution channel, and builds that cannot verify their own version fail
instead of guessing.

### Fixed
- **Native libraries could ship stamped with the wrong version.** The version was
  derived from `git describe` with `VERSION.txt` as a *silent* fallback, so any
  build environment where git was unavailable produced a wrong-but-plausible
  version with no warning. The v1.5.12 Unity package shipped both Linux natives
  (`x86_64` and `arm64`) stamped `1.5.11` for this reason: those jobs build
  inside a manylinux container, git refused to read the workspace ("detected
  dubious ownership"), and the build fell through to a `VERSION.txt` that
  release tagging never updated. Windows and macOS, which build on the bare
  runner, were correct — so the same recorder produced different header bytes
  per platform. Payload data was unaffected: only the three header bytes
  covering the patch version and the checksum that follows from it differed.
- **The Unity `upm` branch was stamped `1.5.3` for nine releases.**
  `upm-branch.yml` read its version from the *committed* `unity/package.json`,
  which is only patched during packing. Every `-upm` tag from `v1.5.4` onwards
  carries `package.json` version `1.5.3`. It now reads `VERSION.txt`.
- **Stale committed manifests.** `unity/package.json` (`1.5.3`) and
  `csharp/src/Bcsv/Bcsv.csproj` (`1.5.7`) had drifted from the shipped versions.
  Both now mirror `VERSION.txt` and are checked on every push.

### Changed
- **`VERSION.txt` is the single source of truth.** Git tags no longer supply the
  version; they are verified against it. A tagged build whose tag disagrees with
  `VERSION.txt` is now a hard configure error, which is precisely the condition
  that produced the v1.5.12 mismatch. This matters beyond provenance:
  `version::MINOR` selects the file codec, so a guessed version can change how
  data is encoded, not just how it is labelled.
- **`scripts/update_version.sh` now sets the release version everywhere**
  (`VERSION.txt`, `unity/package.json`, `Bcsv.csproj`) so the bump lands in one
  commit before tagging. It no longer writes the gitignored
  `include/bcsv/version_generated.h`.
- `scripts/validate_version.sh` is now a wrapper around `check_versions.py`; it
  previously compared against a header that does not exist in a clean checkout.
- `VERSIONING.md` rewritten: it described tag-as-truth and referenced a
  `release.yml` workflow and an auto-commit step that do not exist.

### Added
- **`BCSV_STRICT_VERSION` CMake option.** When on, a build that cannot verify its
  version against git fails rather than falling back. Enabled in every release
  and packaging workflow; off by default for local and tarball builds.
- **`scripts/check_versions.py`** — one implementation, used by developers and
  every CI workflow. Verifies the committed manifests, optionally cross-checks a
  git tag, and loads a **built** shared library to confirm the version it
  actually reports. Every packaging workflow now runs this against each native
  before uploading it, so the artifact is checked rather than the build inputs.
- A `version-consistency` job on `ci.yml` and `build-and-publish.yml` catches
  manifest drift on every push instead of at release time.
- Linux container build jobs now mark the workspace `safe.directory`, fixing the
  underlying git failure, and all packaging checkouts use `fetch-depth: 0` so
  tags are visible.

## [1.5.12] - 2026-08-21

Unity native packaging. No library or format changes.

### Fixed
- Linux natives are built in `manylinux_2_28` and link the C++ runtime
  statically, so the Unity package no longer requires a matching `libstdc++` or
  a glibc as new as the CI runner's.
- Windows natives link the static MSVC runtime, removing the Visual C++
  Redistributable requirement.

### Known issue
- Both Linux natives in this release report version `1.5.11`. Data written is
  fully portable — only the version stamp in the file header is wrong. Fixed in
  1.5.13.

### Added
- `unity/tools/build-windows.ps1` for local Windows plugin builds.
- Benchmarks are now optional at configure time (`-DBUILD_BENCHMARKS=OFF`).

---

## [1.5.11] - 2026-07-13

CSV converter rework: validated type inference with automatic widening (no more
silently zeroed cells), strict partial-row handling, per-column name/type
overrides, row/column selection — plus pybcsv wheels that now ship the full
native CLI tool suite.

### Added
- **pybcsv wheels bundle the full CLI tool suite.** `pip install pybcsv` now
  also installs all eleven BCSV tools (csv2bcsv, bcsv2csv, bcsvHeader,
  bcsvHead, bcsvTail, bcsvCast, bcsvSampler, bcsvValidate, bcsvRepair,
  bcsvCompare, bcsvGenerator) into the environment's scripts directory (on
  PATH in an activated venv), so Python users get the high-performance,
  version-matched toolchain with no compiler or CMake required. Programmatic
  access via `pybcsv.tools.run("csv2bcsv", ...)` / `pybcsv.tools.path(...)`.
  Adds ~5 MB to the wheel; opt out of a source build with
  `-DPYBCSV_BUILD_TOOLS=OFF`. (`sync_headers.py` now also copies
  `src/tools`/`src/shared` and the CLI11 header for standalone sdist builds.)
- **csv2bcsv: validated type inference with automatic widening.** Types are still
  inferred from a sample (default 1000 rows, configurable via `--sample N`, `0` =
  full pre-scan), but every row is now checked during conversion: a later cell
  that does not fit the inferred type triggers a full-file re-scan and one retry
  with the widened type (ints grow to int64/uint64, floats to double; numbers
  beyond those stay STRING with a warning; clearly non-numeric cells widen to
  STRING silently). Previously such cells were **silently written as 0**.
  Inference now parses integers exactly (values in `(int64 max, uint64 max]` become
  UINT64 instead of a lossy DOUBLE) and uses round-trip verification for
  FLOAT vs DOUBLE (the dead FLOAT16/FLOAT128 decimal-place heuristics are gone).
  Output is written via a temp file + atomic rename, so a failed conversion
  never destroys an existing output.
- **csv2bcsv: partial rows abort by default.** A row whose field count does not
  match the columns is treated as a corrupt-CSV indicator and stops the
  conversion with the offending row/line. `--skip-partial-rows` skips such rows
  and `--pad-partial-rows` pads short rows with empty cells; both always report
  the affected row count. Trailing all-empty fields (trailing delimiters)
  remain tolerated, and headerless files take their column count from the
  first data row with trailing empty fields trimmed (no more phantom trailing
  column).
- **csv2bcsv: `--skip-header`, `--names SPEC`, `--types SPEC`, `--tolerance`,
  `--strict`.** Names/types use the bcsvCast SPEC grammar (map form
  `0=int32,price=float,7:8=double` with index, range, or column-name keys; list
  form covering all columns, `auto` = infer). Forced types never widen: misfits
  clamp with a warning summary (bcsvCast `--static` semantics); `--strict`
  aborts instead. `--skip-header` consumes and discards the header row.
- **csv2bcsv & bcsv2csv: `--rows` / `--cols` selection** using the shared
  index-range grammar (`0:99,200,-10:`); `--cols` also accepts column names.
  bcsv2csv's `--firstRow/--lastRow/--slice` remain as legacy flags (stepping is
  still `--slice`-only) and conflict with `--rows`. csv2bcsv `--rows` addresses
  0-based data rows after the header and rejects negative indices (streaming).
- **bcsvCast: column names as SPEC keys and in `--cols`** (e.g.
  `--static 'price=float'`, `--cols temp`); numeric-looking names must be
  addressed by index.
- **CsvReader: raw-cells mode and parse-error visibility** — `readNextRaw()` /
  `rawCells()` expose split, untyped cells; `parseErrorCount()` counts typed-cell
  parse failures (previously an unprinted, per-cell-allocated message);
  `unquote()` is public/static. The UTF-8 BOM is now stripped for headerless
  files too, and `\r`-only lines are skipped like empty lines.
- Shared tool headers `src/tools/type_probe.h` (bcsvCast's `ColumnProbeState`,
  loss model, and the new CSV cell classifier/probe) and `src/tools/spec_parse.h`
  (SPEC grammar with name+index keys), plus direct unit tests
  (`tests/type_probe_test.cpp`).
- Third-party component notices (LZ4, xxHash, CLI11) are now reproduced in the
  `LICENSE` file and installed with the binaries (`share/doc/bcsv/LICENSE`).

### Fixed
- **CsvReader silently truncated out-of-range INT8/UINT8 cells** (e.g. `300` in an
  INT8 column stored as `44`): INT8/UINT8 now parse into their exact type and
  report out-of-range like the other integer widths (value stays the default 0,
  and the failure is visible via `parseErrorCount()`).

### Changed
- **CLI tools now use the CLI11 argument parser** (bundled under `include/CLI11-2.6.2/`,
  BSD-3-Clause). All 11 tools share a thin layer (`src/tools/cli_app.h`) for the
  `-V/--version` flag, `--help`, validation, and the common encoding options,
  replacing ~800 lines of hand-rolled per-tool parsing. Behavior and flags are
  preserved. CLI11 is a tools-only build dependency and is **not** installed with
  the library headers.
- **Argument-error exit codes** for the converter/inspection tools (csv2bcsv,
  bcsv2csv, bcsvHead, bcsvTail, bcsvHeader, bcsvSampler, bcsvGenerator) are now the
  parser's codes (non-zero) rather than always `1`. bcsvCompare, bcsvValidate,
  bcsvRepair, and bcsvCast keep their documented `2 = argument error` code.
- `--help` output leads with the tool description; the BCSV version is available
  via `-V/--version` (previously the version tag also prefixed `--help`).

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
- **`--version` / `-V` flag for all CLI tools** — every tool now reports its name and
  the BCSV version (`<tool> (BCSV <version>)`) plus copyright/license. The same version
  tag is also printed as the header of each tool's `--help` output. Shared helpers
  (`programName`, `versionTag`, `printVersion`) live in `src/tools/cli_common.h` to
  avoid duplication.
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

[1.5.17]: https://github.com/webertob/bcsv/compare/v1.5.16...v1.5.17
[1.5.16]: https://github.com/webertob/bcsv/compare/v1.5.15...v1.5.16
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
