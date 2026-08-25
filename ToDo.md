# BCSV — ToDo / Roadmap

Open work only. Shipped releases live in `CHANGELOG.md`; the task-level history and
measurement notes behind them are in `docs/archive/` (`ToDo_2026-07_archive.txt` for
everything up to 1.5.9, `ToDo_1.5.10-1.5.16_shipped.md` for 1.5.10 and 1.5.16).

Core is in `include/bcsv`, tools in `src/tools`, tests in `tests/` (GTest via CTest),
benchmarks in `benchmark/` (see its README), docs in `docs/`. `tmp/` is scratch (gitignored).

Versioning policy for this roadmap:
- **1.6.0 (minor)** — additive format features (new flags/codecs/footer sections) and additive
  APIs. New library reads all old files; old libraries cannot read files using new features.
- **2.0.0 (major)** — reserved; see below. Nothing currently *requires* it.

---

## Release 1.6.0 — additive format & API features

Priorities chosen to close the measured gaps vs Parquet while keeping
the streaming row-wise write path intact. Order = suggested implementation order.

> **Confirmed for the 1.6.0 scope (2026-08-24).** Four things are wanted in this release and are
> already tracked below — this note fixes them as *in scope* rather than candidates:
> **FP16 and FP8 column types** (E9, which also carries FP128 — decide whether FP128 rides along
> or is deferred, since it is the only one of the three with no hardware and no obvious consumer),
> **dictionary encoding** (E4, currently scoped per-packet and string-only — E9's low-precision
> floats and E5's column hints both want it generalised to any low-cardinality column, so scope
> E4 as a generic per-packet dictionary with strings as the first user), and **header
> compression** (E11, new — see below).
>
> Together these are the release's size story: E3 (zstd) compresses the payload, E4 removes
> repeated values, E9 shrinks the values themselves, and E11 removes the fixed overhead that
> survives all three. Wide-and-idle files — the 1000-channel case the design targets — pay all
> four costs today.

- [ ] E1: **Per-packet idle-column elision** (re-scoped 2026-08-25 — was "Delta002 header
      suppression", per-row). **The original per-row form is dead: it fires on 0 rows.**
      Measured on three real recordings from `~/ws/diss-recordings` (650, 950 and 1012 columns,
      63k–139k rows): *not one row* had a header equal to its predecessor's, and not one row
      was entirely ZoH. With 650+ channels something always changes, so there is never an idle
      row to suppress. The idleness is real but it is **per column, not per row**: in the
      950-column file **567 columns (60%) are identical in every one of 139 464 rows**, costing
      61% of the header bits and zero payload.
      New scope: a per-packet bitmap of columns that are entirely ZoH within that packet, whose
      header bits are then omitted for every row in the packet (2.1 KB of bitmap for the
      950-column file at 8192 rows/packet). Measured effect:
      * raw row header −60% (334 → 122 B/row),
      * **uncompressed packet mode: ~30% smaller file — this is where the value is**,
      * LZ4/zstd-compressed mode: **~2%**, because the compressor already removes that
        redundancy on its own.
      So land and message it as an **embedded / packet-raw** feature, not as a size-vs-Parquet
      lever. Still a new row-codec version (delta003 or flag bit); old readers reject cleanly.
- [ ] E2: **Per-packet column min/max statistics** — optional footer section (or per-packet stats
      block): per numeric column min/max. Enables packet skipping for time-range and predicate
      reads (turns scan-class queries into seek-class at 8 MB granularity). Backward compatible
      for new readers; feature-flagged.
- [ ] E3: **zstd batch file codec** — new FileFlags bit + `FileCodecPacketZstdBatch`. Cheapest
      compression-ratio lever (delta output is entropy-coder friendly); closes most of the
      noisy-float/string size gap vs parquet+zstd. LZ4 remains default for embedded/streaming.
      Optional dependency (CMake option, like the batch codec).
      **Measured 2026-08-25** (delta002 output of the 950-column recording, 8 MB blocks,
      Parquet+ZSTD = 36.87 MB): zstd-1 → 45.7 MB (1.24x), zstd-3 → 43.6 MB (1.18x),
      zstd-9 → 40.2 MB (1.09x). For comparison BCSV's own LZ4 on the same data is 65.3 MB at
      level 1 and 47.3 MB at level 6 (LZ4HC). **zstd-1 matches LZ4HC's ratio at roughly ten
      times LZ4HC's compression speed** (≈680 MB/s vs ≈50–80 MB/s); it gives back decompression
      speed (≈2 GB/s vs LZ4's ≈6–8 GB/s), which the read profile shows is not on the critical
      path — full-scan decode is identical at level 1, level 9 and uncompressed.
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
- [ ] E12: **In-format file metadata, and retire `BcsvMetadata`** (raised 2026-08-24 by T13's R2)
      — an arbitrary `map<string, string>` in the file header behind a new FileFlags bit, written
      by `Writer`, exposed as `Reader::metadata()` / `BcsvReader.Metadata` / `pybcsv`, and carried
      through `parquet_to_bcsv` / `bcsv_to_parquet` in both directions. BCSV's 24-byte header has
      no key/value section today, which is why 1.5.15 shipped the `<file>.bcsv.meta.json`
      companion as a stopgap.
      **Retirement is part of this item, not a follow-up.** When the in-format channel lands:
      * delete `csharp/src/Bcsv/BcsvMetadata.cs`, `unity/Runtime/Scripts/BcsvMetadata.cs` (+ its
        `.meta`), and `csharp/tests/Bcsv.Tests/BcsvMetadataTests.cs` — including the hand-rolled
        `MiniJson`, which exists only because Unity 2021.3 has no `System.Text.Json` and the Unity
        package carries no third-party dependencies;
      * keep `pybcsv`'s `read_metadata_json` for one minor release so existing companions stay
        readable, with `bcsv_to_parquet` preferring in-format pairs over the companion;
      * drop the "Parquet conversion, null policies and the metadata JSON companion are
        Python-only" note from the `docs/API_OVERVIEW.md` feature matrix.
      Removing a public C# type is a breaking API change, so it lands on a MINOR at the earliest.
      Announce the deprecation in the release that ships the in-format channel and delete in the
      next one — do not do both in the same release.
      **Settle the namespace collision while designing this** (reported 2026-08-25 by a
      `com.bcsv.unity` consumer). The companion has two levels — the document level
      (`metadata_json_version`, `source_path`, `source_sha256`, `bcsv_sha256`,
      `bcsv_bytes`, `bcsv_rows`) and the `key_value_metadata` object copied verbatim out
      of the Parquet footer — and `read_metadata_json` / `BcsvMetadata.ReadCompanion`
      return only the inner one. Their pipeline is raw → parquet → bcsv and stamps its own
      `source_sha256` at the raw → parquet step, so every companion carries two different
      digests under that one name and a consumer moving off hand-parsed JSON onto
      `ReadCompanion` silently swaps which link of the chain they are checking. Documented
      on both readers for now (2026-08-25) rather than fixed, because this item deletes the
      companion anyway. **The in-format map inherits the same hazard**: if
      `parquet_to_bcsv` keeps passing footer pairs through unfiltered while the format also
      records provenance, `Reader::metadata()` reproduces the defect exactly. Decide the
      rule up front — reserved key prefix, a separate provenance struct outside the map, or
      a refusal at write time when a source key collides — and do not leave it to a
      follow-up.
      **All four bindings expose `metadata()` in the same release as the C++ core** — C++, C#,
      Unity and pybcsv together. Requested 2026-08-25 by T13, and the right default anyway:
      `com.bcsv.unity` is what that project consumes, and a binding that lands a release late
      forces a consumer to keep the companion path and the in-format path alive simultaneously,
      which is worse than either alone. `scripts/check_csharp_parity.py` now guards both the
      P/Invoke surface and the managed wrappers in CI, so this one is on CI, not only on
      the release checklist.
      **Prerequisites inside the library, from auditing the header-parse path 2026-08-25:**
      * `Reader::readFileHeader()` gates the version (VERSIONING.md Rule B), which is what makes
        a new header section safe for existing readers — a 1.5.x reader refuses a 1.6.0 file
        outright rather than reading packets from an offset the new section moved. Now covered by
        `tests/version_gate_test.cpp`; keep it passing.
      * `FileHeader::readFromBinary` does **not** validate `FileFlags`, so the new metadata bit is
        not itself a gate. The `version::MINOR` bump is the gate; the bit only says what is there.
      * `src/tools/bcsvRepair.cpp` calls `readFromBinary` directly, outside that gate, and locates
        the first packet with `FileHeader::getBinarySize(layout)` — a layout-only computation that
        a metadata section invalidates. Both must be updated with the section, or repair will
        misparse exactly the files this item creates.
- [ ] E13: **Record source nullability, do not restore it** (raised 2026-08-24 by T13, decided
      2026-08-25) — `parquet_to_bcsv(null_policy="nan")` fills Parquet nulls with NaN, and the
      obvious symmetry for `bcsv_to_parquet` — turn every float NaN back into a null — is wrong:
      a corpus can hold genuine NaNs in one column family and nulls in another, so the reverse
      transcode would invent nullability the source schema never had (T13 measured 1.1 M genuine
      NaNs in a non-nullable family). The fix is provenance, not inference:
      * `parquet_to_bcsv` records, per column, whether the *source* field was nullable and how
        many nulls it filled there. `_apply_null_policy` already computes `col.null_count` per
        column and throws it into one aggregate (`nulls_filled`); keep it keyed by field name.
        Home is the E12 metadata section, not the companion — sequence this after E12.
      * `bcsv_to_parquet` never restores nulls by default. An opt-in restore is then *checkable*
        rather than a guess: convert NaN back to null only in columns recorded as source-nullable,
        and only where the column's NaN count equals the recorded fill count — otherwise refuse
        and say which column disagreed. A bare list of nullable names cannot make that check.
      * Note the companion is only written when the source carries footer key/value pairs
        (`parquet_utils.py`, `if kv:`); null provenance has to force it, or land with E12.
      Not blocking for T13 — they restore nullability from the Parquet schema they hold.
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
      **Confirmed in scope for 1.6.0** (see the note at the top of this section).
      **Measured 2026-08-25 — FP16 is not a size lever on wide sensor data, so do not scope it
      as one.** Across three real recordings, the float columns that survive an FP16 round-trip
      *exactly* hold only **2.6%, 16.5% and 3.0% of the delta payload** respectively: they are
      overwhelmingly the constant channels, which already cost zero payload. Halving those bytes
      moves under 4% of the file. FP16/FP8 remain worth having for callers who *know* their data
      is low-precision (raw IMU counts and similar) — just not as part of the size story.
      Notes for the design: FP16 is `std::float16_t` where available with a soft-float fallback
      for the embedded targets; FP8 has two competing IEEE-adjacent layouts (E4M3 / E5M2) and picking one is a
      wire-format commitment, so decide it explicitly rather than by implementation accident.
      Both need `ColumnType` values, delta002 type-grouped loop instantiations, and a row in the
      `nan_inf_test` matrix — the NaN/Inf guarantee must hold for them too.
- [ ] E11: **Header compression** (requested 2026-08-24) — two distinct overheads share the name;
      both are wanted, and they are independent pieces of work:
      * **File header** — column names are stored uncompressed and concatenated. At 1000+ columns
        with long dotted names (the `parquet2bcsv` flattening produces e.g.
        `plc.metrology_estimate_tcp.x`) this is tens of KB before the first row, and it is paid
        again by every reader that opens the file. Candidates: LZ4-compress the name block behind
        a FileFlags bit, or factor the common dotted prefixes into a small prefix table.
        Measure first on a real 1052-column layout — if the win is under a few KB it is not worth
        a flag bit.
        **Measured 2026-08-25, on exactly that layout:** the name block is **29 092 B**, which
        compresses to **6 697 B (LZ4)** or **2 102 B (zstd)** — a best case of ~27 KB saved
        against a 42.0 MB file, i.e. **0.06%**. By the criterion this item set itself, the
        file-header half does not earn a flag bit *for size*. It may still be worth doing for
        open latency on very small files or when opening thousands of files, which is a
        different argument and should be made on its own numbers.
      * **Row header** — the per-row bitfield floor that E1 attacks by suppression. E1 removes it
        for *idle* rows; it does not shrink it for active ones. If E1's measurements show the
        floor still dominating on sparse-but-not-idle data, the follow-up is a narrower encoding
        (entropy-coded or run-length header codes) rather than more suppression.
      Sequence E11 after E1 and E9: E1 settles how much row-header cost is left, and E9 changes
      the per-column header width, so measuring before both have landed measures the wrong thing.

- [ ] E14: **Read-side prefetch for the remaining codecs, and strided reads** (requested
      2026-08-25 by `diss-abb-irb4600`, for real-time replay in Unity) — a replay driven at a
      1 ms step cannot afford a synchronous whole-packet decompress on the row that crosses a
      packet boundary. Audited on the way in, and **most of this already exists**:
      * `FileCodecPacketLZ4Batch001` — the default codec — already double-buffers the read side
        on a background thread, so a boundary is a pointer swap and `readRow()` stays
        O(VLE decode). Nothing to do for the steady state.
      * **The first boundary in a file is the exception**: the BG thread starts lazily, so that
        one packet is decompressed synchronously on the caller's thread
        (`file_codec_packet_lz4_batch001.h`, the `[LIB-4]` comment in `decodeNextRow`). Every
        later boundary is prefetched.

        **Do not "fix" this by starting the thread in `setupRead`** — that was the first idea
        and it is wrong. The lazy start is deliberate and the comment says why: a caller may
        read from the same `std::istream` between `open()` and the first row, and
        `ReaderDirectAccess::readFileFooter` does exactly that, with `tellg`/`seekg` on
        `Base::stream_` (`reader.hpp`) immediately after `setupRead` returns. A background
        thread pulling packets off that stream concurrently would move the position underneath
        it. Checked 2026-08-25 while scoping 1.5.17, and left alone for that reason.

        The shape that could work is starting the prefetch on the first *sequential* `readRow`
        instead, by which point the footer read has happened and `seekToPacket` already stops
        the thread for direct access. That is a concurrency change to the default codec, so it
        wants the `clang-tsan` preset and a test that interleaves direct access with sequential
        reads — not a patch-release drive-by. Its whole payoff is one packet's decompress per
        file, once.
      * **The other three file codecs have no prefetch at all** — `FileCodecPacket001`,
        `FileCodecPacketLZ4001` and `FileCodecStreamLZ4001` contain no `bg_thread_`/`read_next_`.
        A caller who opts out of `BATCH_COMPRESS` stalls on *every* boundary. Either lift the
        double-buffer into something shared, or document that batch is the codec for latency-
        sensitive reading.
      * **Strides are genuinely new.** Nothing implements "give me every Nth row" —
        `sampler_window.h`'s window is expression lookahead, not decimation. Note the honest
        ceiling before promising savings: packets decompress whole, so a stride saves row
        deserialization, not I/O. `ReaderDirectAccess` already has the packet cache and
        `readRow(index)`, so the cheap version is a thin layer on that; real I/O savings need
        Packet002 (see 2.0.0), same as E6.
      Scale note for the requester: at the default 8 MiB `blockSizeKB` their recordings are
      ~0.5 MB, i.e. a single packet with no boundaries at all, so none of this bites them yet —
      it starts at ~8 MiB of compressed data. Shrinking `blockSizeKB` is the zero-library-work
      lever, traded against the compression ratio that made a whole recording fit in one block.

## 2.0.0 — reserved (nothing currently requires it)

Semantic versioning applies to the library API; all items above are additive, so no 2.0.0 is
*forced*. Recommendation: reserve 2.0.0 for the one structural format evolution —

- **Packet002 hybrid row-columnar packets**: the batch codec already buffers full packets;
  transpose to per-column chunklets at flush (per-column encodings: BSS for floats, dictionary
  for strings, RLE-bitpacked bools; per-chunklet offset table).
  **Measured 2026-08-25** — simulated by transposing real delta002 output. Grouping the payload
  by column (`[header block][payload block, column-major]`) is worth a further **16–18% on top
  of zstd**: on the 950-column recording zstd-3 goes 1.18x → 0.85x of Parquet, and zstd-1
  1.24x → 0.97x. Two findings that should shape the design:
  * **No offset table is needed for the payload.** The header block already determines every
    column's payload length, so per-column offsets are recovered by summing the codes. (An
    offset table is still needed if the *header* is transposed too, which is what buys real
    sparse-column reads.)
  * **Byte-stream-split hurts here.** BSS on top of column grouping made the 650- and
    950-column files *larger* (0.67x → 0.73x and 0.88x → 0.95x at zstd-3); delta002's XOR
    deltas are already VLE-stripped to variable length, so BSS misaligns them. Do not port
    Parquet's BSS reflexively — measure per encoding. Streaming `writeRow()`, crash
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
  fallback; Unity UTF-8/IL2CPP readiness.
- C# binding divergences found by `scripts/check_csharp_parity.py` when it was first run
  (2026-08-25), currently listed in its `KNOWN_DIVERGENCE` table so the gate is green.
  Both are the NuGet side lagging Unity, not a deliberate split — decide and close them,
  then delete the entries:
  * `BcsvLayout.RowDataSize` and `BcsvLayout.ColumnCountByType` are exposed by the Unity
    binding only. `NativeMethods.cs` already declares both P/Invoke entry points
    (`bcsv_layout_row_data_size`, `bcsv_layout_column_count_by_type`), so a NuGet consumer
    cannot reach a native function the package already binds — a half-finished port.
  * `BcsvWriter.Open` throws `ArgumentNullException` for a null layout on Unity; the NuGet
    copy passes the null through to the native call.
- 23.a Columnar read/write: move implementation from C API layer into core C++ library
  (three duplicate implementations today — clear clarity win, evaluate for 1.6.0).
- Code cleanup (was item 19): remove ZoH codec? (evaluate once delta header suppression E1 lands —
  delta then strictly dominates ZoH), condense duplicated docs, API surface review.
- Platform matrix (was item 27): confirm clean build/tests on MSVC + Apple clang; performance on
  STM32/Zynq/Versal/Kria targets.
- Performance (was item 28): SIMD serialization hot loops, branch-prediction work, runtime codegen
  (LLVM) — revisit after Packet002; multithreaded filter pipelines.
- Docs/outreach: paper / GitHub wiki (was item 25); versioning-policy alignment between
  VERSIONING.md and `Reader::open()` tolerance.
- Backward-compat demonstration harness: golden files from each released version, read by current
  library in CI (was parking-lot item 26).
