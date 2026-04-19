# ADR-005: VLE Pessimistic Buffer Allocation Strategy

**Status:** Accepted  
**Date:** 2026-04-19  
**Review:** v1.5.6 critical review (finding #3 — VLE encoder buffer safety)

## Context

The Delta002 row codec encodes numeric columns using variable-length encoding
(VLE), where the encoded size per column ranges from 0 bytes (ZoH/FoC) to
`sizeof(T)` bytes (worst case: full-width delta). Two buffer strategies were
considered:

**Option A — Pessimistic pre-allocation (current):** Allocate
`headBytes + Σ(cols.size() × sizeof(T))` before the encode loop. The inner loop
writes at most `sizeof(T)` bytes per column, guaranteed to fit.

**Option B — Per-column bounds check:** Allocate a tighter buffer and check
remaining capacity before each column write, resizing if needed.

## Decision

**Keep Option A (pessimistic pre-allocation)** and add a debug assertion after
the encode loop to catch regressions.

### Rationale

1. **Zero branches in the hot path.** The serialize loop processes every numeric
   column with `memcmp` → `memcpy` → `encodeDelta`. Adding a capacity check per
   column introduces a branch for every column of every row. At 500 columns ×
   100K rows/sec, that's 50M extra branches/sec — measurable in benchmarks.

2. **Over-allocation is cheap.** The `ByteBuffer` uses `LazyAllocator` which
   skips zero-initialization. The buffer is reused across rows (not freed per
   row), so the pessimistic size is allocated once and `resize()` is a no-op for
   subsequent rows of equal or smaller size. Typical over-allocation is ~50% for
   compressible data, but the buffer is transient working memory, not stored.

3. **Correctness is provable.** The maximum encoded size per column is
   `sizeof(T)` (VLETraits guarantees `MAX_ENCODED_BYTES ≤ sizeof(T) + 1` but
   `encodeDelta` writes at most `deltaBytes` where `deltaBytes ≤ sizeof(T)` due
   to the safety clamp). The pessimistic allocation accounts for exactly this.

4. **Debug assertion** (`assert(bufIdx <= buffer.size())`) catches logic errors
   during development with zero release-build cost.

## Consequences

- No per-column bounds checking overhead in release builds.
- A debug build will abort immediately if a code change introduces a write
  past the pessimistic bound.
- If a new column type is added with encoded size > `sizeof(T)`, the
  pessimistic calculation must be updated and the assertion will catch the
  regression in debug builds.
