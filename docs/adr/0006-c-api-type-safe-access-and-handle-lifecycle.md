# ADR-006: Type-Safe C API Cell Access and Idempotent Handle Lifecycle

**Status:** Accepted  
**Date:** 2026-09-14  
**Review:** com.bcsv.unity 1.5.19 consumer defect reports (player-exit abort,
silent-zero getters); C/C++, compiler, assembler and CPU expert review before release

## Context

Two independent defects were reported by a Unity consumer of the C API and
reproduced on v1.5.19:

1. **Exit-time double free.** A managed wrapper disposed a writer on the main
   thread while the GC finalizer concurrently (or later) called
   `bcsv_writer_destroy` on the same handle. The second `delete` corrupted
   glibc heap metadata, which aborted the *process* later during teardown with
   `free(): invalid size` (SIGABRT) — buffered rows lost, the crash attributed
   to an unrelated frame. ASan confirmed the same heap-use-after-free for a
   destroy-during-write race.
2. **Silent-zero typed getters.** `bcsv_row_get_double` on a FLOAT (or any
   non-DOUBLE) column returned `0.0` with no usable signal on platforms
   without CRT `atexit` hooks (Unity IL2CPP). The root cause sat one layer
   lower: v1.5.19 routed every `bcsv_row_get_*` through the strict C++
   `Row::get<T>()`, which type-checks **only when `RANGE_CHECKING` is on**.
   With the constant flipped off (the embedded configuration), `get<double>`
   on a FLOAT cell read 4 bytes past the cell — a type pun, which ASan/UBSan
   caught as a wild value rather than an error. The C API must be correct in
   *every* build configuration, because a consumer cannot see which one the
   shipped `.so` was compiled with.

Both fixes touch the same surface (`src/bcsv_c_api.cpp`) and share the
project's standing constraint: the C API is a speed surface. A safety net may
not cost branches on the hot path or runtime dispatch where compile-time
dispatch is available.

## Decision 1 — Handles: claim-on-destroy via a live-handle registry

Every handle kind (`Row`, `Sampler`, `Reader`, `CsvReader`, `CsvWriter`,
`Writer`, `Layout`) is registered at creation in a process-wide
`unordered_map<void*, HandleKind>` guarded by a mutex. Each `*_destroy` first
*claims* the handle (find-and-erase under the mutex): the first caller owns
the destruction, every later caller finds the slot empty and the destroy
becomes a logged no-op described by `bcsv_last_error()`. All destroys are
idempotent without changing the C++ classes or requiring consumer discipline.

- The registry is a **leaky singleton** (never destroyed). A registry whose
  mutex could be torn down by static destructors while a finalizer thread is
  still calling destroy would recreate the exact exit-order hazard being
  fixed. One small intentional leak at exit is the correct trade.
- Create/destroy are cold lifecycle calls (rows come from
  `bcsv_writer_row()`/`bcsv_reader_row()` without allocation), so the mutex
  never sits on a per-row hot path.
- Borrowed handles (`bcsv_writer_row()`, `bcsv_reader_row()`) are never
  registered, so destroying one through this API is the same logged no-op —
  a misuse that previously was a wild `delete`.
- The exported `close`/`flush` calls carry the same check WITHOUT claiming
  (find, no erase): a managed `Dispose` is close-before-destroy, so a
  finalizer running after `bcsv_shutdown()` would otherwise call `close_fn`
  through the freed handle — the same abort class, one door further in. A
  non-registered handle there is the same logged no-op as on destroy.

## Decision 2 — Lifecycle: `bcsv_shutdown()`

A new `extern "C"` function performs deterministic bulk teardown on the
calling thread. Phase 1 closes every open writer/csv-writer, so the last
packet and footer land on disk while the runtime is fully alive. Phase 2
destroys all handles in kind order — borrowers (writers, samplers, rows)
before lenders (readers, csv-readers, layouts). The registry is swapped empty
under one lock first, so any later `*_destroy` or close/flush from any
thread observes an empty registry and no-ops instead of touching freed
memory. The function
never propagates an exception (every stage is noexcept-swallowed) and is
idempotent. Hosts call it from their last exit hook; the Unity layer hooks
`Application.quitting` automatically and ends open recorders first.
Precondition: process quiescence with respect to this API (documented).

## Decision 3 — Getters: unconditional type check at the C API layer

The C API no longer relies on the build configuration of `Row::get<T>()`.
Every scalar getter (and each new `bcsv_row_try_get_*` twin) verifies the
column type before reading the cell; the mismatch path never reinterprets
bytes:

- **Strict rule** for every type: read succeeds iff the column type is
  exactly `T`'s, via the template `row_cell_strict<T>` — the comparison is
  against the compile-time constant `toColumnType<T>()`, zero dispatch.
- **One documented exception: `bcsv_row_get_double` widens losslessly** over
  BOOL, INT8…INT32, UINT8…UINT32, FLOAT, DOUBLE — each converts to `double`
  exactly. INT64/UINT64 (inexact above 2⁵³) and STRING stay errors.
  Implemented as an inlined DOUBLE compare (the by-type majority) falling
  through to a noinline eight-arm switch over the dense enum — a measured
  single-function form kept the getter out of the caller's inline budget and
  cost 4x on DOUBLE columns. No polymorphic reads: the per-call cost is the
  same one enum comparison as the strict path.
- **On mismatch the plain getters keep their fallback value and always set
  `bcsv_last_error()`** (1.5.19 could return a wild or zero value in
  `RANGE_CHECKING=false` builds with a stale or empty error channel). Under
  the fresh-channel rule a row accessor's channel always describes THAT call
  — `docs/ERROR_HANDLING.md` §5 is normative for the mechanism — so a stale
  error cannot masquerade as a fresh one.
- **`try_get_*` twins** for callers that must not guess: `true` + `*out` on
  success; `false` + `bcsv_last_error()` on bad index or type mismatch, with
  `*out` untouched on failure. Each twin applies exactly the type rule of its
  plain getter. The managed bindings call the `try_get_*` form exclusively,
  keeping one P/Invoke per read with no second call to fetch an error.
- **Vectorized `*_array` getters are unchanged from 1.5.19**: their type
  check lives in the core and is gated by `RANGE_CHECKING` (on by default);
  they are bulk fast-paths, not probes, and this decision does not extend
  per-cell enforcement into them.

Error branches are `[[unlikely]]`-annotated and the mismatch paths are
`[[noreturn]]` throws caught only at the wrapper macros, so the success trace
of a matched getter is: clear one TLS bool, compare the index, compare one
enum byte, load the cell. Measured against the v1.5.19 library: matched
scalar getters +0.2 ns (1.17 → 1.37 ns) — the type check this decision
mandates is the direct cause of Defect 2's silent-0 root; `get_double` on a
DOUBLE column is parity; `get_double` on a widened column costs ~5 ns where
1.5.19 threw (~500+ ns) or silently returned 0.0; the `try_get_*` twins
measure equal to their plain getters. C++-core benchmark suites show no regression.

## Rejected options

- **Consumer-side disposal flags only** (for Defect 1): correct for one
  wrapper, unfixed for handle misuse across languages — the C API cannot make
  a managed runtime's finalizer timing its own contract.
- **Close-before-destroy in the wrappers without a registry**: narrows the
  window but keeps a data race on the handle itself; the ASan UAF reproduced.
- **Keep `get_double` strict** (for Defect 2): the consumer's read pattern is
  a numeric union read through the widest type; strict + error would have
  preserved their bug report verbatim instead of making the read correct.
  Widening is limited to the exactly-representable types, so no silent
  precision loss is introduced.
- **A runtime-dispatched tagged-cell accessor**: dynamic dispatch in a
  constant-time accessor contradicts the speed mandate; the
  template-for-strict / jump-table-for-double split keeps the per-cell test
  a single enum comparison.

## Consequences

- `*_destroy` on a foreign or borrowed pointer is a safe, reported no-op
  instead of UB; exit-time double destroy cannot abort a process.
- Files written through leaked handles can be finalized deterministically
  with one `bcsv_shutdown()` call; the Unity package registers recorders and
  tears them down at `Application.quitting`.
- `get_double`/`try_get_double` on narrower numeric columns now succeed with
  the widened value where 1.5.19 returned `0.0` + error — a deliberate
  contract relaxation that makes the common numeric-union read correct. Every
  other wrong-type read turns a possible silent `0.0` into an error through a
  trustworthy error channel. The 144-cell getter × type matrix in
  `tests/bcsv_c_api_defects_1520_test.c` locks both sides of the contract.
- The C++ core is untouched: `RANGE_CHECKING` remains an embedded-size knob,
  and the C API's guarantees no longer depend on it.
