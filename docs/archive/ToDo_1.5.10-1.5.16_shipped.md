# ToDo archive — releases 1.5.10 and 1.5.16 (shipped)

Moved out of `ToDo.md` on 2026-08-25. Every item below is checked off and shipped;
the user-facing record is `CHANGELOG.md` under `[1.5.10]` and `[1.5.16]`. Kept here
for the measurement notes and rationale, which the changelog entries summarise.

---

## Release 1.5.16 — Parquet size gap, closed without touching the wire format

Driven by user reports of BCSV files 30-50% larger than the equivalent Parquet. Measured
against `~/ws/diss-recordings` (228 recordings, 300-1052 columns, 56.4 M rows, 23.2 GB of
Parquet); a 17-file sample covering every column-count group in that corpus was 1.49x Parquet
at the old default and swung 0.71x-2.24x per file.

**The cause was the default compression level, not the layout.** `compression_level = 1` maps
to `LZ4_compress_fast` at *acceleration 9* — the weakest setting LZ4 offers. Levels 1-5 land
within 4% of each other on this data; the cliff is at 6, where `LZ4BlockCompressor::init()`
switches to LZ4HC.

- [x] Default compression level 1 → 6 everywhere (`bcsv::DEFAULT_COMPRESSION_LEVEL`), across
      C++, pybcsv, the CLI tools, C# and Unity. "Everywhere" was initially overclaimed — the
      first pass changed `Writer::open` and `BcsvWriter.Open` but left `BcsvColumns.WriteColumns`
      (C# and Unity), `parquet2bcsv --compression-level`, and the pandas/polars wrappers writing
      level 1, so the level you got depended on which entry point you called. Every managed
      default now references `BcsvDefaults.CompressionLevel` and every Python default
      `pybcsv.DEFAULT_COMPRESSION_LEVEL`, so there is no literal left to drift. Guarded by
      `python/tests/test_default_compression_level.py` and
      `csharp/tests/Bcsv.Tests/BcsvDefaultsTests.cs`, which assert on the *written file header*
      rather than on source literals. **1.49x → 1.08x** on the sample, for ~48% more
      write CPU. Both numbers are width-dependent: on the synthetic macro profiles (50-84
      columns, mostly changing) the same switch is only -5% size for +7% write CPU, because
      there is little long-range redundancy for LZ4HC's larger window to find. Quote the range,
      not one number. Verified PATCH-safe against `VERSIONING.md`'s deciding question: LZ4HC emits
      ordinary LZ4 blocks, `LZ4BlockDecompressor` is stateless, and `resolveFileCodecId()` only
      tests `level > 0` — a **BCSV 1.5.10** binary reads the new files and renders byte-identical
      CSV. Regression test: `BCSVTestSuite.CompressionLevels_DefaultAndLevelInvariance`.
- [x] Type-grouped columnar fill in the Python bindings (`ColumnFillPlan` / `ColumnStorePlan` in
      `bindings.cpp`). The old code ran a `switch` on `ColumnType` per *cell* — 132 M dispatches
      for a 950-column, 139k-row file — which made the Arrow path 4.8x slower than the format.
      Full Arrow scan **0.98 s → 0.46 s**; columnar write path 1.43 s → 1.17 s.

Left on the table, deliberately (all need a wire-format change, so 1.6.0+): the row header is
still 48% of the uncompressed delta output, and column-grouping the packet payload is worth a
further 16-18% — see E1, E3 and the Packet002 note below, all now carrying measured numbers.

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

