# BCSV — ToDo / Roadmap

Unified roadmap as of 2026-07-11. Sources: previous `ToDo.txt` (completed history moved to
`docs/archive/ToDo_2026-07_archive.txt`), the critical review + parquet head-to-head in
`review_2026-07-11.md`, and the prior review cycles (`review.md`, `plan.md` — all cycles complete).

Background (unchanged): BCSV combines CSV flexibility with binary speed/size. Core in
`include/bcsv`, tools in `src/tools`, tests in `tests/` (GTest via CTest), benchmarks in
`benchmark/` (see its README), docs in `docs/`. `tmp/` is scratch (gitignored).

Versioning policy for this roadmap:
- **1.5.10 (patch)** — bug fixes and hardening only. No wire-format change, no public-API change.
  Old readers read new files and vice versa, bit-for-bit format compatibility.
- **1.6.0 (minor)** — additive format features (new flags/codecs/footer sections) and additive
  APIs. New library reads all old files; old libraries cannot read files using new features.
- **2.0.0 (major)** — reserved; see below. Nothing currently *requires* it.

---

## Release 1.5.10 — correctness & hardening (no format/API change)

Detailed execution plan: `plan_1.5.10.md`. Items reference findings in `review_2026-07-11.md` §2.

### A. Batch codec concurrency (default codec — highest priority)
- [x] A1 (H1): eliminate main-thread/BG-thread race on the shared `ifstream`. Reader must not
      poll `stream_.good()` while the BG task may touch the stream; EOF is reported via codec
      state only. Fixes silent loss of the last packet on footer-less (crash-recovered) files.
- [x] A2 (H2): `bg_exception_` accessed only under `mutex_` / after `waitForBgIdle()`.
- [x] A3 (M6): `finalize()` rethrows a pending BG exception unconditionally (disk-full during
      last packet must not produce a "clean" file with a missing packet).
- [x] A4: ThreadSanitizer build preset + batch-codec test target run under TSan; regression test:
      multi-packet footer-less file is read to the last complete packet.

### B. Format hardening (write/read validation symmetry)
- [x] B1 (H3): enforce `MAX_ROW_LENGTH` in `writeRow()` (all read paths already throw at 16 MiB−2;
      the writer currently produces files its own reader rejects).
- [x] B2 (M1): Delta002 deserialize validates header code (`deltaBytes <= sizeof(T)`) — removes
      shift UB on hostile input (dynamic + static paths).
- [x] B3 (M3): Flat001 string pre-scan clamps to `MAX_STRING_LENGTH` so buffer size matches bytes
      written — stops uninitialized heap bytes leaking into files for >64 KiB strings.
- [x] B4 (M4): `FileFooter::read()` validates `start_offset` (≥ minimum, ≤ file size) before
      computing `indexSize` (size_t underflow → allocation bomb); batch codec cross-checks
      declared packet sizes against the header packet size instead of the 1 GiB ceiling.
- [x] B5 (M2): direct-access `loadPacket()` consumes terminator + checksum and validates, matching
      sequential-read guarantees.
- [x] B6 (M5): `static_assert(std::endian::native == std::endian::little)` with a clear message;
      fix stale comments (packet_header checksum range, terminator value in definitions.h).
- [x] B7: `FileHeader::readFromBinary` — cap cumulative header size (carried over from review.md §2).

### C. NaN / ±Inf enablement
The binary core already round-trips NaN/Inf bit-exactly except two static-layout comparison sites.
- [x] C1: static-layout ZoH + Delta change detection via bit comparison (`std::bit_cast`) for
      float/double — fixes silent −0.0 → +0.0 corruption and restores ZoH hold for repeated NaN.
- [x] C2: Delta002 encoder skips FoC when prev/gradient is non-finite (removes decoder-side
      NaN-arithmetic ambiguity; wire format unchanged).
- [x] C3: csv2bcsv float-compat inference guards non-finite (`std::isfinite`) so one `nan` cell
      no longer forces DOUBLE.
- [x] C4: pybcsv `write_dataframe` NaN policy — stop coercing float NaN to 0.0 (additive
      `nan_policy` kwarg; default preserves NaN for float columns; document interplay with
      `strict` and parquet nulls).
- [x] C5: test matrix — NaN (incl. payloads), ±Inf, −0.0, subnormals × {flat, zoh, delta} ×
      {dynamic, static} × full file round-trip; CSV bridge nan/inf round-trip; Python pandas
      preservation test.
- [x] C6: document the guarantee (README + docs): binary format is IEEE-754 bit-exact including
      NaN payloads and signed zero; CSV bridge preserves values, not payloads.

### D. Infrastructure & docs polish
- [x] D1: macro benchmark — expected static-layout skips must not produce exit code 1 or count as
      failures in the JSON.
- [x] D2: benchmark report — footnote that ZoH/Delta modes use `generateTimeSeries` while
      CSV/Dense use volatile `generate` (the Comp-vs-CSV column mixes datasets); optional
      `--no-validate` switch for pure-decode read timing (needed for honest cross-format numbers).
- [x] D3: UBSan job for the gtest suite (would have caught B2 class bugs).
- [x] D4: `docs/THREAD_SAFETY.md` — document the batch codec's *internal* thread and its contract.
- [x] D5: CHANGELOG for all of the above.

---

## Release 1.6.0 — additive format & API features

Priorities chosen to close the measured gaps vs Parquet (`review_2026-07-11.md` §3) while keeping
the streaming row-wise write path intact. Order = suggested implementation order.

- [ ] E1: **Delta002 header suppression** — when a row's header equals the previous row's and no
      payload follows, emit the ZoH-style 0-length row (or 1-bit repeat marker). Removes the
      per-row header floor (⌈header_bits/8⌉, e.g. ~375 B/row for 1000 float channels) for idle
      periods. New row-codec version (delta003 or flag bit); old readers reject new files cleanly.
- [ ] E2: **Per-packet column min/max statistics** — optional footer section (or per-packet stats
      block): per numeric column min/max. Enables packet skipping for time-range and predicate
      reads (turns scan-class queries into seek-class at 8 MB granularity). Backward compatible
      for new readers; feature-flagged.
- [ ] E3: **zstd batch file codec** — new FileFlags bit + `FileCodecPacketZstdBatch`. Cheapest
      compression-ratio lever (delta output is entropy-coder friendly); closes most of the
      noisy-float/string size gap vs parquet+zstd. LZ4 remains default for embedded/streaming.
      Optional dependency (CMake option, like the batch codec).
- [ ] E4: **Per-packet string dictionary** (was parking-lot item 27) — store each distinct string
      once per packet, reference by integer ID. Closes the 2–3× string-heavy size gap; speeds
      string reads. Synergy: pybcsv Arrow export can emit dictionary arrays (zero-copy
      pandas Categorical / Polars).
- [ ] E5: **Column modifiers / encoding hints** (was item 23) — per-column hints (index, volatile,
      monotonic, ordered) stored in the header; codecs use them to pick encodings (e.g. skip
      XOR-delta for volatile floats). Header extension + additive API.
- [ ] E6: **Sparse-column read API** (was item 24) — `Reader` support for reading a column subset
      (RowView-based punch-out). Note: with row-wise ZoH/Delta the wire format still requires
      full-row decode; real I/O savings arrive with Packet002 (see 2.0.0). API lands here so
      callers are ready.
- [ ] E7: **Stream I/O API** (was item 26/29) — Reader/Writer on arbitrary `std::istream`/
      `std::ostream` (stdin/stdout piping, network). API addition; format unchanged.
      Includes CLI piping support + docs/examples (bcsvCat/bcsvMore-style usage).
- [ ] E8: pybcsv/C# surface for E1–E7 as applicable; parquet converters pick up dictionary/stats.
- [ ] E10: **Unify typed CSV cell parsing in CsvReader** (from the 2026-07-13 tools review) —
      csv2bcsv's checked conversion (`parseCellChecked` + slow paths) and the library's
      `CsvReader::parseCells` are two implementations with deliberate divergences (strict
      case-insensitive bool token set vs legacy true/1/TRUE/True-else-false; quoted numerics
      unquoted by the tool but parse-as-0 in the library). Move checked/typed parsing into
      CsvReader as the single semantics, and expose `parseErrorCount()` through the C API and
      python bindings. Natural companion to E9 (new float types then land in one parser).
- [ ] E9: **FP8 / FP16 / FP128 column types** (requested 2026-07-13) — additive format + API
      support for reduced/extended-precision floats (FP8, IEEE half, quad). Includes updating the
      CLI tools for the new types: csv2bcsv (inference ladder + `--types`), bcsvCast (probe,
      loss model, SPEC), bcsv2csv/CsvWriter formatting, bcsvHeader display. Note: the old
      csv2bcsv FLOAT16/FLOAT128 decimal-place heuristics were removed in the 2026-07 tools
      rework (they were dead code — `BCSV_HAS_FLOAT16/128` was never defined) — the new
      inference is round-trip-exact and caps at FLOAT/DOUBLE until these types land.

## 2.0.0 — reserved (nothing currently requires it)

Semantic versioning applies to the library API; all items above are additive, so no 2.0.0 is
*forced*. Recommendation: reserve 2.0.0 for the one structural format evolution —

- **Packet002 hybrid row-columnar packets**: the batch codec already buffers full packets;
  transpose to per-column chunklets at flush (per-column encodings: BSS for floats, dictionary
  for strings, RLE-bitpacked bools; per-chunklet offset table). Streaming `writeRow()`, crash
  resilience, and the packet index are untouched. This is the only item that closes the measured
  10–50× sparse-column read gap vs Parquet, and it unlocks SIMD type-homogeneous decode and
  parallel reads. Bundle with: nulls as a first-class concept (optional), formal endianness
  statement, and a "BCS2" magic/major format version.

If Packet002 ships as just another optional file codec with unchanged APIs, it could technically
be 1.7.0 — decide by messaging needs, not mechanics.

---

## Backlog (unified from previous ToDo, de-duplicated, no release assignment)

Learnings captured from the 1.5.10 release gate (2026-07-12):

- **Test infra**: extract a shared `tests/temp_dir_fixture.h` (per-test +
  per-process unique temp dirs; the pattern now exists in ≥ 11 test files in
  ≥ 3 variants). Convention: parallel ctest runs tests as separate processes —
  fixtures must never share directories or fixed file names.
- **Hot-path helper discipline**: small helpers called inside codec hot loops
  must be `BCSV_ALWAYS_INLINE` — GCC's TU-wide inlining budget otherwise
  degrades *unrelated* loops (measured −10 % on decode from one encoder-side
  helper; see docs/archive/B2_VALIDATION_COST_INVESTIGATION.md and the
  noise-floor study for the measurement rules that caught it).
- **Throw-after-state-commit is a bug pattern**: validation that runs after a
  stateful serializer has committed reference state must poison/resync the
  writer (see write_poisoned_). Audit new codec write paths for this shape.
- **Add an ASan preset** (`clang-asan`) alongside tsan/ubsan; run all three
  before releases.
- **.gitattributes**: repo has mixed CRLF/LF text files; pick a policy
  (e.g. `* text=auto` + explicit exceptions) to stop EOL churn in diffs.
- **Third-party**: googlebenchmark headers fail clang 21 `-Werror`
  (`__COUNTER__` C2y extension) — pin a newer googlebenchmark or add
  `-Wno-c2y-extensions` scoped to `_deps` targets.
- **bench_macro exit-code wart**: results with `mode == "ERROR"` (per-profile
  exceptions) are excluded from the failure check in main() — an exception
  during a profile does not fail the run. Decide intended semantics.
- **encode/decodeDelta duplication**: now that `delta002ValidateLengthCode`
  is a shared free function, hoisting the (duplicated) encode/decodeDelta
  helpers out of the two Delta002 classes is the natural follow-up.

- Sampler: conditional assignments, wildcards, index-based conditions (was item 25/28);
  performance phase 2 (was item 21, phase 1 complete).
- CLI tools: remaining ideas — bcsvInspect (validate+repair+info unification), bcsvCompress
  (2-phase re-compression), bcsvIndex/bcsvConvert (partially covered by bcsvRepair/bcsvCast —
  evaluate before building), bcsvMore/bcsvCat/bcsvSed (depends on E7 piping).
- C# / Unity: NuGet packaging + CI/CD (was item 23, library+benchmarks done); SafeHandle/finalizer
  fallback (review.md); Unity UTF-8/IL2CPP readiness.
- 23.a Columnar read/write: move implementation from C API layer into core C++ library
  (three duplicate implementations today — clear clarity win, evaluate for 1.6.0).
- Code cleanup (was item 19): remove ZoH codec? (evaluate once delta header suppression E1 lands —
  delta then strictly dominates ZoH), condense duplicated docs, API surface review.
- Platform matrix (was item 27): confirm clean build/tests on MSVC + Apple clang; performance on
  STM32/Zynq/Versal/Kria targets.
- Performance (was item 28): SIMD serialization hot loops, branch-prediction work, runtime codegen
  (LLVM) — revisit after Packet002; multithreaded filter pipelines.
- Docs/outreach: paper / GitHub wiki (was item 25); versioning-policy alignment between
  VERSIONING.md and `Reader::open()` tolerance (review.md).
- Backward-compat demonstration harness: golden files from each released version, read by current
  library in CI (was parking-lot item 26).
