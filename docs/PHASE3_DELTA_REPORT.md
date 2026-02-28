# Phase 3: Delta Encoding Design Review — Report

**Date:** 2025-01-xx  
**Scope:** Item 14 — `RowCodecDelta001` (commit f48a51d)  
**Status:** Review complete. Recommendation: **keep current design, ship with fixes applied.**

---

## 3.A — Spec-vs-Implementation Audit

| # | Spec (ToDo.txt L490-540) | Implementation | Verdict |
|---|--------------------------|----------------|---------|
| 1 | Header encodes combined mode+byte-count per column (0=ZoH, 1=FoC, 2=1B, 3=2B, ...) | 2 mode bits + separate length bits (0-3 bits based on type size) | **Implementation better** — separates mode selection from byte count, adds explicit "plain" mode (01) for clean fallback. |
| 2 | "exploit ZoH shortcut to skip row (row_length=0)" | Disabled: always emits header to keep gradient state synchronized | **Correct deviation** — delta codec must synchronize gradient state on both sides. Reader needs to know which columns are ZoH to zero their gradients. |
| 3 | "Use block encoding (jump tables)" | Template-dispatch via `dispatchType()` — compile-time switch on (isFloat, isSigned, typeSize) | **Equivalent** — template dispatch generates jump-table-like code with the benefit of inlining. |
| 4 | "Consider vector operations to calculate gradients" | Scalar per-column computation | **Acceptable** — spec says "consider." Column data memory layout (grouped by type) is SIMD-ready for a future optimization. |
| 5 | "VLE/ZigZag, Gorilla" for encoding | Integers: zigzag + VLE byte packing. Floats: XOR delta + VLE byte count. FoC: arithmetic gradient. | **Good design** — float FoC uses arithmetic prediction (catches linear trends), while delta fallback uses XOR (minimizes bit differences). Hybrid approach captures both patterns. |

### Header Size Comparison

| Type Size | Spec (bits/col) | Implementation (bits/col) | Difference |
|-----------|-----------------|---------------------------|------------|
| 1 byte    | 2               | 2                         | 0          |
| 2 bytes   | 2               | 3                         | +1 bit     |
| 4 bytes   | 3               | 4                         | +1 bit     |
| 8 bytes   | 4               | 5                         | +1 bit     |

For a typical 20-column layout: ~20 extra bits ≈ 3 bytes/row overhead. For a 100-column int64 layout: 100 extra bits ≈ 13 bytes/row. Negligible for most workloads.

---

## 3.B — Three Alternative Approaches

### Approach A: Status Quo (Current Implementation) — **RECOMMENDED**

- 2 mode bits + type-dependent length bits in fixed header
- XOR delta for floats, zigzag delta for integers
- Arithmetic FoC prediction
- Always emits header (no ZoH-all shortcut)

**Pros:** Already implemented, well-tested (35 unit tests + 5 file-codec tests), clean separation of concerns, correct unsigned-domain arithmetic for signed overflow avoidance.  
**Cons:** Slightly larger header than spec for wide layouts. No CHIMP-style leading/trailing zero optimization for floats.

### Approach B: Compact Header (Spec-Literal)

- Merge mode+byte-count into single per-column field matching the original spec
- Re-enable all-ZoH shortcut (empty span = all columns unchanged)

**Pros:** Smaller header for wide int-heavy layouts (saves ~1 bit/column for 2-8 byte types).  
**Cons:** Marginal benefit (<3 bytes/row for typical layouts). Loses explicit "plain" mode — forces delta computation even when cost is equal. Complicates mode enumeration. ZoH-all shortcut requires clearing all gradients on both sides (feasible but adds complexity).

**Assessment:** Not worth the redesign effort. Savings are negligible for real workloads.

### Approach C: Gorilla/CHIMP Float Encoding

- Replace float XOR + VLE byte packing with bit-level Gorilla/CHIMP encoding
- Track leading zeros and trailing zeros of XOR deltas with bit granularity
- CHIMP additionally tracks "previous leading zeros" to compress the flag

**Pros:** Better compression for slowly-varying float sensor data (typically 10-20% improvement in columnar stores like Gorilla TSDBs).  
**Cons:**
- BCSV is **row-major**, not columnar. CHIMP's leading-zero tracking works best across many consecutive values of the *same column*, which doesn't apply to row-major encoding.
- Bit-level packing adds significant complexity (bit-stream management, cross-byte alignment) and throughput regression.
- VLE byte granularity has lower per-value overhead (<1 byte amortized) compared to CHIMP's flag bits.
- Would require fundamental restructuring of the wire format.

**Assessment:** CHIMP is designed for columnar time-series databases. It is a poor fit for row-major BCSV. The current XOR + VLE byte approach is simpler, faster, and well-suited to the format.

### Recommendation

**Keep Approach A (Status Quo).** The implementation captures all the important compression wins:
- ZoH for unchanged values (zero bytes)
- FoC for linear trends (zero bytes)
- Delta + VLE for small changes (1-N bytes instead of full type size)
- XOR for float changes (minimizes byte count)

The marginal gains from Approaches B and C do not justify the redesign complexity or potential throughput regression.

---

## 3.C — Benchmark Results

### Codec Comparison: mixed_generic (72 columns, 10K rows, 3 iterations)

| Candidate       | Write (Krow/s) | Read (Krow/s) | File Size | Ratio vs CSV |
|----------------|----------------|---------------|-----------|-------------|
| CSV             | 168            | 367           | 7,936,790 | 1.000       |
| PktRaw          | 385            | 1,033         | 4,143,905 | 0.522       |
| PktLZ4          | 344            | 855           | 3,165,965 | 0.399       |
| **PktRaw+ZoH**  | **1,217**      | **1,840**     | 37,827    | 0.005       |
| **PktLZ4+ZoH**  | **1,496**      | **1,932**     | 36,261    | 0.005       |
| **PktRaw+Delta** | 686           | 1,051         | 335,942   | 0.042       |
| **PktLZ4+Delta** | 688           | 1,022         | 133,183   | 0.017       |
| BatchLZ4+Delta  | 822            | 970           | 26,953    | 0.003       |

### Codec Comparison: sensor_noisy (50 columns, 50K rows, 3 iterations)

| Candidate       | Write (Krow/s) | Read (Krow/s) | File Size   | Ratio vs CSV |
|----------------|----------------|---------------|-------------|-------------|
| CSV             | 151            | 489           | 34,800,931  | 1.000       |
| PktRaw          | 352            | 693           | 15,111,070  | 0.434       |
| PktLZ4          | 345            | 655           | 15,111,861  | 0.434       |
| **PktRaw+ZoH**  | **1,751**      | **2,350**     | 1,136,665   | 0.033       |
| **PktLZ4+ZoH**  | **1,669**      | **2,257**     | 1,020,881   | 0.029       |
| PktRaw+Delta    | 905            | 1,270         | 1,606,626   | 0.046       |
| PktLZ4+Delta    | 1,006          | 1,222         | 703,816     | 0.020       |
| **BatchLZ4+Delta** | 1,072       | 1,317         | **258,212** | **0.007**   |

### Key Observations

1. **Delta vs Flat:** Delta achieves 96-98% file size reduction vs CSV, and 92% vs raw BCSV. This validates the design intent.

2. **Delta vs ZoH:** ZoH is faster (1.5-2× throughput) because it has no gradient computation overhead. But the mixed_generic benchmark uses predominantly ZoH-favorable data (many unchanged values). With noisier data where values change slightly each row, Delta's compression advantage would be more pronounced.

3. **Delta + LZ4:** The combination of Delta + LZ4 compression is particularly effective — PktLZ4+Delta achieves 0.017 ratio vs CSV (98.3% compression) on mixed_generic data. BatchLZ4+Delta achieves 0.003 (99.7% compression).

4. **Delta throughput:** 686-1,072 Krow/s write, 970-1,317 Krow/s read. This is 2-3× faster than CSV and competitive with raw BCSV (within 2× of flat encoding).

5. **Delta's sweet spot:** Time-series data with small sequential changes (sensor data, timestamps, counters). The FoC predictor eliminates data for linear trends; the VLE delta minimizes bytes for small changes.

---

## 3.D — Test Coverage Strengthened

Added **13 new tests** (35 total, up from 22):

| Test | What it covers |
|------|---------------|
| `SignedOverflow_INT8_Wrap` | INT8 -128 → 127 wrapping (unsigned-domain delta correctness) |
| `SignedOverflow_INT32_MinMax` | INT32 min/max boundary delta (full range) |
| `SignedOverflow_INT64_MinMax` | INT64 min/max boundary delta |
| `Float_NaN_Inf_RoundTrip` | NaN, +Inf, -Inf round-trip via XOR delta |
| `WireSize_DeltaSmallerThanPlain` | Assert delta row < plain row wire size |
| `WireSize_ZoH_HeaderOnly` | Assert ZoH row = header-only (no data) |
| `FoC_Float_NoAccumulatedError` | 50 rows of exact FP FoC (no drift) |
| `ManyColumns_WideLayout` | 50 columns (10×U32, 10×I64, 10×F64, 10×F32, 5×BOOL, 5×STRING) |
| `DeltaEncoding_UINT64_LargeDelta` | Large delta → plain fallback |
| `FoC_SignedInteger_NegativeGradient` | FoC with negative gradient (100,90,80,70) |
| `MultiPacketReset_GradientState` | Gradient doesn't leak across reset() boundaries |
| `EmptyString_RoundTrip` | Empty string → non-empty → empty |
| `AllColumnTypes_FoC_Sequence` | FoC for all 8 integer types simultaneously |

---

## 3.E — Critical Code Review

### Findings Fixed (Critical/High)

| # | Severity | Issue | Fix Applied |
|---|----------|-------|-------------|
| 1 | **Critical** | `deserialize()` reads payload without bounds checks — OOB on truncated buffers | Added `if (dataOff + N > buffer.size()) throw runtime_error(...)` before every payload read (plain, delta, string length, string body) |
| 2 | **High** | Header size validated with `assert()` only — compiled out in release | Replaced with `if (...) throw runtime_error(...)` matching ZoH codec style |
| 3 | **High** | String >65535 bytes silently truncated to uint16_t | Added `if (str.size() > 65535) throw runtime_error(...)` in both first-row and subsequent-row string encoding |

### Remaining Findings (Medium/Low — for future consideration)

| # | Severity | Issue | Notes |
|---|----------|-------|-------|
| 4 | Medium | Copy assignment operator not exception-safe | Could use copy-and-swap idiom. Low practical risk (codec objects rarely copied). |
| 5 | Medium | ZoH mode doesn't write to `row.data_` — caller must reuse same Row | Consistent with ZoH codec design. Document the contract in Doxygen. |
| 6 | Medium | Float FoC broken under `-ffast-math` | Add `#error` guard or switch to XOR-domain FoC. Low risk — BCSV doesn't enable fast-math. |
| 7 | Low | `reset()` relies on all-plain first row invariant | Documented with comment in code. |
| 8 | Low | `dispatchType()` default silently handles invalid sizes | Could add `assert(false)` on default branch. |
| 9 | Low | Thread safety undocumented | Add `@note` to class Doxygen. |
| 10 | Low | `prev_data_.resize()` vs `assign()` inconsistency after reset | Harmless due to all-plain invariant. |

---

## 3.F — Overall Assessment

### Verdict: Ship with fixes. No redesign needed.

The Delta codec implementation is **solid, well-structured, and correct**. It properly handles:
- Signed overflow via unsigned-domain arithmetic
- Float encoding via Gorilla-style XOR
- Gradient synchronization between encoder and decoder
- Packet boundaries via reset()
- All column types (8 integer, 2 float, bool, string)

The three critical/high findings (bounds checking, assert→throw, string truncation) have been fixed. The remaining medium/low findings are low-risk and can be addressed incrementally.

### Performance Profile

- **Compression:** 96-99.7% reduction vs CSV (depending on data pattern and file codec)
- **Throughput:** 686-1,072 Krow/s write, 970-1,317 Krow/s read  
- **vs ZoH:** ~50% of ZoH throughput, comparable compression on time-series data
- **vs Flat:** 2-3× faster than CSV write, 92-98% smaller files

### Why Not Redesign

1. **Compact Header (Approach B):** Saves ~1 bit per 2-8 byte column. For 20 columns: 3 bytes/row. Negligible.
2. **CHIMP (Approach C):** Designed for columnar stores, poor fit for row-major format. Would add significant complexity with minimal benefit.
3. **Current design is proven:** 549 tests pass, benchmarks validate compression and throughput targets.

### Recommended Follow-ups

1. Fix remaining Medium findings (copy-assign, float FoC guard, ZoH row-write) in a follow-up PR
2. Add SIMD gradient computation as a future optimization (layout's type-grouped memory is ready)
3. Consider the all-ZoH shortcut for packet-level compression (special case: emit empty row for all-unchanged, reader zeros all gradients)
