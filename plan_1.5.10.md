# BCSV 1.5.10 — Execution Plan

**Scope:** correctness + hardening only. Bit-for-bit wire-format compatibility, no public-API
signature changes (one additive Python kwarg). Items = `ToDo.md` sections A–D; findings referenced
from `review_2026-07-11.md` §2.

**Code-clarity ground rules (apply to every cycle):**
- Current code is a solid base. Change structure only where the benefit is clear; when in doubt,
  keep the existing shape. No drive-by refactors.
- Fixes live at the narrowest scope that is correct (e.g. the write-side row-length check goes in
  one place — `Writer::writeRow()` — not in all four file codecs).
- Where a fix would otherwise duplicate logic in dynamic + static codec paths, extract one small
  shared helper (e.g. `bitEqual<T>()` for C1, delta-code validation for B2) into the codec header
  it belongs to — no new files, no new layers.
- Every fix ships with a test that fails before / passes after.
- Comments state contracts (esp. the batch codec threading contract), not narration.

**Parallelism:** all builds/tests with `-j32`; the four cycles are largely independent —
develop/test A and B/C in parallel worktrees if desired; benchmarks always pinned + exclusive.

---

## Cycle A — batch codec concurrency (H1, H2, M6)

The default codec spawns an internal `std::jthread`. Today the Reader's hot loop
(`reader.hpp:210`) polls `stream_.good()` while that thread may be mid-`read()` — a data race
with two observable failure modes (mid-file false EOF; silent loss of the final packet on
footer-less files). The exception slot has the same problem.

**A1 — stream/EOF ownership.**
Design: the Reader stops consulting `stream_` state directly on the read path; instead
`FileCodecDispatch` gains an internal `bool good(std::istream&) const` query that dispatches to
the active codec (trivial `stream.good()` for synchronous codecs; the batch codec answers from
its own state — `bg_has_next_packet_` / current-packet cursor — and never exposes transient BG
stream flags). The BG read task always leaves the stream in a defined state (`clear()` on all
exit paths, position restored where required) and reports "no more packets" exclusively via the
existing atomic flag, never via stream flags. This is an internal-header change; `Reader::readNext()`
keeps its exact semantics for synchronous codecs.

**A2 — exception slot.** `bg_exception_` writes/reads move under `mutex_` (BG catch handler
already transitions to IDLE under the mutex; the reader side inspects only after
`waitForBgIdle()` or under the same lock). No hot-path cost: the check happens per packet, not
per row.

**A3 — finalize.** `finalize()` calls `rethrowBgException()` unconditionally after the final
`waitForBgIdle()`, before writing the footer; `Writer::close()` propagates (existing error-message
path, no signature change).

**Tests (A4):**
- New `tsan` CMake preset (clang, `-fsanitize=thread`, batch codec + lz4 stream/stress tests).
  Run in dev loop; document in tests/README.
- Crash-recovery regression: write N packets, strip footer, read back → all rows of every
  complete packet are returned (this currently fails nondeterministically / drops the last packet).
- Disk-full path (Linux): writer on `/dev/full` → `close()` reports the error, no "clean" file
  (guards A3; `#ifdef __linux__`).
- Existing 871 tests + lz4_stress under TSan clean.

**Clarity note:** the threading contract (who owns the stream when; state machine
IDLE→READ_DECOMPRESS→…) gets a single block comment at the top of
`file_codec_packet_lz4_batch001.h` plus a short section in `docs/THREAD_SAFETY.md` (D4). No
structural rewrite of the codec — the state machine itself is sound; the fix is ownership +
synchronization discipline.

## Cycle B — format hardening (H3, M1, M3, M4, M2, M5, B7)

| Item | Change (single location) | Test |
|---|---|---|
| B1 row length | `Writer::writeRow()`: after serialize, `span.size() > MAX_ROW_LENGTH` → throw with column-count/string-size hint | layout with ~300 × 64 KiB strings: write throws; file remains readable up to previous row |
| B2 delta codes | shared `validateDeltaCode(code, sizeof(T))` helper used by dynamic + static deserialize | crafted row buffers with codes 10–15 (8-byte col) and 3 (1-byte col) → clean throw, no UB (UBSan) |
| B3 flat strings | pre-scan uses `min(str.size(), MAX_STRING_LENGTH)` so buffer == bytes written | >64 KiB string round-trip: truncated per contract, serialized length exact, no uninit bytes (write into poisoned buffer, assert pattern absent) |
| B4 footer/batch sizes | `FileFooter::read()`: `start_offset` range-checked before `indexSize` math; batch `readAndDecompressPacket()` checks declared sizes vs `packet_size_limit_` + slack | fuzzed footers (start_offset 0, 12, > file size); 40-byte file declaring 1 GiB sizes → rejected without large allocation |
| B5 direct access | `loadPacket()` reads through terminator + checksum, validates like sequential path | corrupt a packet body → direct access throws (today: returns garbage rows) |
| B6 endianness | `static_assert` in definitions.h + comment fixes (packet_header.h:51/89 checksum range, definitions.h:83 terminator) | build-time only |
| B7 header cap | cumulative header size limit in `FileHeader::readFromBinary` | hostile header with 65 k × 65 k-byte names → rejected early |

Duplication policy: the four existing read-side `MAX_ROW_LENGTH` checks stay as they are (locally
clear, codec-specific error text); we add exactly one write-side check.

## Cycle C — NaN/±Inf enablement (C1–C6)

- C1: `bitEqual<T>()` (`std::bit_cast` to unsigned, integer compare) replaces `operator==`/`!=`
  for float/double change detection in static ZoH (`row_codec_zoh001.hpp:444–453`) and static
  Delta ZoH check (`row_codec_delta002.hpp:697`). Integers/strings/bools keep `operator==`.
  Encoder-side only → wire format unchanged. Expected side effect: −0.0 and NaN-payload
  correctness plus *better* ZoH compression for repeated NaN.
- C2: Delta002 encoder: skip FoC candidate when prev or gradient is non-finite (one `isfinite`
  guard in the float path; decoder untouched).
- C3: csv2bcsv type inference: skip float-compat cast-back test for non-finite values.
- C4: pybcsv `write_dataframe(nan_policy=...)`: `"preserve"` (new default — float NaN passes
  through bit-exact; the C++ path already supports it), `"coerce"` (old behavior),
  `"raise"` (subsumes today's `strict` interplay). Non-float columns keep the fillna path.
  Update warning text + docs incl. parquet-null distinction. pybcsv patch version bump.
- C5: tests — parameterized C++ round-trip matrix {NaN, NaN-with-payload, ±Inf, −0.0, subnormal}
  × {flat, zoh, delta} × {dynamic, static} × {in-memory codec, full file I/O}; bit-exactness via
  `bit_cast` comparison; repeated-NaN rows assert ZoH hold (serialized size). CSV round-trip
  `nan`/`inf`/`-inf`. Python: pandas NaN preservation (replaces the "acceptable to reject" test).
- C6: docs — guarantee statement in README + ERROR_HANDLING/INTEROPERABILITY as fits.

Decision point (flagging, not blocking): C4 changes what pandas users get on disk (NaN preserved
instead of 0.0 + warning). I treat the old behavior as a data-corruption bug; if you prefer
strict compatibility, default stays `"coerce"` and 1.6.0 flips it.

## Cycle D — infra & docs (D1–D5)

- D1: macro benchmark: static-layout "dispatch unavailable" becomes `status: skipped` (not
  a failure), exit code 0 when only skips occurred; runner warning disappears.
- D2: report footnote (mixed generators in Comp-vs-CSV); `--no-validate` flag for pure-decode
  read timing (benchmark-only; default unchanged so history stays comparable).
- D3: UBSan preset (clang, `-fsanitize=undefined`) + run gtest suite in dev loop.
- D4: THREAD_SAFETY.md internal-thread section (from A).
- D5: CHANGELOG entries; version bump happens via git tag (GetGitVersion) at release.

---

## Verification protocol (release gate — demonstrated at the end)

All commands use all cores (`-j32`); benchmarks pinned + exclusive.

1. **Builds:** `ninja-release` and `ninja-debug` (GCC) + `clang-release` — zero warnings
   (BCSV_WERROR=ON).
2. **Tests:** full ctest in **debug and release** — 871+ tests (plus the ~25 new ones), 100% pass.
3. **Sanitizers:** TSan preset → batch/lz4/stress tests clean; UBSan preset → full gtest clean
   (incl. new hostile-input tests).
4. **Macro validation:** `bench_macro_datasets` MACRO-SMALL full matrix — all profiles
   `validation_passed`, exit code 0 (D1 proves itself here).
5. **Performance — no regression:** interleaved head-to-head, baseline = current master
   (`6ec0741`), candidate = 1.5.10 WIP:
   `python3 benchmark/run.py interleaved --baseline-bin <clean build> --candidate-bin <wip build>
   --types MICRO,MACRO-SMALL --repetitions 5` (pinned).
   Acceptance: median write/read rows/s per mode within noise (±3%); any mode worse than 3%
   is investigated before release. Watched hot paths: batch writeRow/readRow (A: per-packet
   locking only), delta deserialize (B2: one compare per column — expected ≲1%, verify),
   flat serialize (B3: `min()` in pre-scan), static ZoH/delta write (C1: integer compare, expected
   neutral or faster).
6. **Compression unchanged:** MACRO-SMALL file sizes byte-identical to baseline for all
   profiles/codecs (format untouched) — except static-ZoH/delta files containing repeated NaN
   (none in the benchmark profiles), so: byte-identical across the board.

Deliverable at the end: summary table (builds × tests × sanitizers × perf deltas) posted with the
interleaved comparison report.
