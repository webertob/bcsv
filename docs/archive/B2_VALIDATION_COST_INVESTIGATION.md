# B2 Investigation — Cost of Per-Row Validation in Delta002 Decode

Date: 2026-07-12 · Machine: Ryzen 9 9950X3D, GCC 15 `-O3`, pinned to CPU2
Harness: `RowCodecDelta002::deserialize` in isolation (pre-serialized in-memory
rows, no I/O, no LZ4), 20 k rows, min-of-15 passes, 5–7 interleaved rounds.
Two layouts: `mixed` (6 × each of the 10 numeric types, 60 cols, random-walk
data) and `wide64` (16 × int64 + 16 × double — isolates the 8-byte path where
the shift-UB existed).

## Background

The Delta002 wire format stores one **length code** per numeric column per row
(2–4 header bits). Codes 0/1 mean ZoH/FoC (no payload); code ≥ 2 means
`code − 1` payload bytes follow. The code field is wide enough to express
invalid values (e.g. 4 bits for 8-byte types allows 10–15 → 9–14 payload
bytes for an 8-byte column). The decoder computed
`result |= byte << (i*8)` for `i` up to `byteCount−1` — for a hostile
`byteCount = 14` this shifts up to 104 bits: **undefined behavior**
(no out-of-bounds access — the payload read was always bounds-checked).

Important scoping fact: this is unreachable from *corrupted* files — every
packet's xxHash64 is validated before rows are decoded. It is reachable only
from *deliberately crafted* files (valid checksums, hostile content) or from
encoder bugs.

## Variants measured

| Variant | Description |
|---|---|
| `nocheck` | pre-B2 code (UB present) — baseline |
| `inline` | naive check: `if (bad) throw std::runtime_error(msg + to_string(...))` inlined at both call sites |
| `cold` | check with the throw split into a separate `[[noreturn]]` cold function |
| `total` | **no check at all** — `decodeDelta` clamps its loop bound to ≤ 8 (total function, defined for all inputs) |
| `combo` | `cold` check + `total` clamp |

## Results (median ns/row, decode only; lower is better)

| Variant | mixed | vs base | wide64 | vs base |
|---|---|---|---|---|
| nocheck | 125.7 | — | 105.0 | — |
| inline | 155.6 | **+23.8 %** | 122.9 | **+17.0 %** |
| cold | 117.0–118.7 | **−5.5…−6.9 %** | 98.5–98.8 | **−5.9…−6.1 %** |
| total | 116.5 | **−7.3 %** | 95.0–95.4 | **−9.0…−9.5 %** |
| combo | 117.7 | −6.3 % | 99.2 | −5.4 % |

Hardware counters (whole benchmark process, `perf stat`, same work):

| Variant | cycles | instructions | branches | branch-misses |
|---|---|---|---|---|
| nocheck | 307 M | 1 714 M | 215 M | 2.22 M |
| inline | 362 M | 2 013 M | 290 M | 2.33 M |
| cold | 293 M | 1 547 M | 229 M | 2.21 M |
| total | 289 M | 1 525 M | 215 M | 2.22 M |

## Findings

1. **The −7 % file-level regression was never branch misprediction.**
   Branch-miss counts are identical across variants (~2.2 M, all from
   data-dependent code paths — the CPU predicts the never-taken validation
   branch essentially perfectly, exactly as hypothesized). The `inline`
   variant lost because the throw's *string construction and EH scaffolding
   were inlined into the hot loop*: +17 % instructions, +35 % branches —
   register pressure and code bloat, not prediction failures.

2. **Range information makes the decode FASTER than the unchecked baseline.**
   Both `cold` and `total` beat `nocheck` by 5–9 %. Reason: the check
   (`deltaBytes ≤ sizeof(T)`) — or the clamp (`n ≤ 8`) — hands the compiler a
   provable maximum trip count for `decodeDelta`'s byte-assembly loop, which
   then fully unrolls (−10 % total instructions vs baseline). The unchecked
   loop's trip count was unbounded in the compiler's view.

3. **At these speeds, everything per-column is visible.** The decode budget is
   ~2 ns per column; one extra well-predicted branch (~0.25 ns) is already
   measurable. The check-vs-clamp difference (`cold`/`combo` vs `total`) is
   the expected ~1–3 %.

4. **Security equivalence of `total`:** with packet checksums validated before
   decode and all payload reads bounds-checked, a crafted file with invalid
   length codes can only produce *garbage values* — indistinguishable in
   effect from the attacker writing those garbage values legitimately. No UB,
   no OOB, no desync beyond attacker-controlled content.

## Decision

Shipped: **`combo`** — `decodeDelta` clamps its loop bound (total by
construction: the UB is impossible regardless of what any caller does, and
the clamp is what buys the unrolling win), plus the cold-path validation
(clean "invalid file" diagnostics instead of silently returning garbage
rows; measured cost ~1 % vs `total`, still ~6 % faster than the pre-B2
baseline).

If the last ~1–3 % ever matters more than malformed-file diagnostics, drop
the two `delta002ValidateLengthCode()` calls — `decodeDelta` stays safe on
its own. The general principle this confirms for future codec work:

> Prefer making decode functions **total by construction** (clamped bounds,
> masked indices) over per-datum validation branches — the compiler rewards
> provable ranges, and checksum-gated input makes garbage-in/garbage-out an
> acceptable failure mode. Validate against the layout once; never
> re-validate layout-derivable facts per row.
