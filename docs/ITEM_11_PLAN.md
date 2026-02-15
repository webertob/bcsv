# Item 11: Codec Extraction

> Extract serialization/deserialization logic from Row and RowView classes into
> dedicated, format-specific Codec types — one class per wire format, each versioned.
> Each codec handles both serialize and deserialize (symmetric), and provides both
> bulk Row operations (throughput) and per-column RowView access (sparse reads).
>
> Enables pluggable encoding (flat, ZoH, CSV, delta) and backward-compatible
> format evolution with minimal impact on Row, Reader, Writer, and RowView.

**Status:** Implementation complete — 2026-02-16 (Phases 1–7 + post-review cleanup + pre-fetch optimization)
**Prerequisite:** Item 10 (flat wire format) — completed 2026-02-13
**Successor items:** 12 (CSV Reader/Writer), 13/14 (delta encoding), backward compatibility
**Internal milestones:** 11a (Phases 1–3: Flat + ZoH codecs + Reader/Writer migration), 11b (Phases 4–6: RowView migration + cleanup)

---

## 0. Post-Implementation Assessment (2026-02-16)

### Honest Accounting

| Metric | Plan Target | Actual | Verdict |
|--------|-------------|--------|---------|
| row.hpp reduction | ~3800 → ~2200 lines (−42%) | 3800 → 3275 (−14%) | **Under-delivered.** Much of `visit()`/`visitConst()` loop logic stayed — it uses codec metadata but the loop structure is intrinsically `RowView`'s concern. |
| Net library LOC change | Reduction expected | +1571 new codec, −858 existing = **+713 net** | **Growth, not shrinkage.** Codec extraction is an additive refactor at this scope. |
| New test LOC | Not quantified | +1658 lines (3 files) | Acceptable — validates the new layer. |
| Tracked files changed | — | 23 files, 499 ins / 943 del | Moderate-scope refactor. |
| Test count | No regression | 404 GTest + 76 C API — all passing | ✅ |

### Benchmark Results (5 reps, 8 configs, AMD Ryzen 9 5900X)

Pre-optimization (codec extraction only, before pre-fetch fix):

| Profile | Change | Assessment |
|---------|--------|------------|
| `BM_VisitConst_72col` | **+18–32% regression** | Consistent across configs. Per-iteration `layout_.columnType(i)` chases `shared_ptr` → `Data*` → `checkRange()` → `array[i]` on every iteration. |
| `BM_RowViewVisit_Int32` | **+8–12% regression** | Same root cause. |
| `bool_heavy` (BCSV Flex) | **−3 to −10% improvement** | Reliable improvement from cleaner codec path. |
| `mixed_generic` (ZoH) | **−6% improvement** | ZoH dispatch path benefits from function pointer resolution. |
| Most macro profiles | ±3% (noise band) | No meaningful change. |

### Post-Review Fixes Applied

1. **Removed 3 redundant scalar caches** from RowView (`wire_data_off_`, `wire_lens_off_`,
   `wire_fixed_size_`). Each was only used in pre-loop local variable initialization.
   Only `offsets_ptr_` (per-iteration inner-loop pointer, 8 call sites) was retained.
   Saved 12 bytes per RowView instance, simplified every constructor and assignment operator.

2. **Fixed const_cast** in `deserializeElementsZoH` — signature changed to `RowType&`.

3. **Pre-fetch layout accessors** in all 11 hot-path visit/get functions.
   Per-iteration `layout_.columnType(i)` calls resolved through:
   `Layout::columnType(i)` → `data_->columnType(i)` (shared_ptr chase) →
   `Data::checkRange(i)` (bounds check) → `column_types_[i]`.
   That's 2 pointer chases + 1 range check per call × N columns per visit.
   Fix: pre-fetch `const ColumnType* types = layout_.columnTypes().data()` before each loop,
   then use direct `types[i]` access. Same pattern for `columnOffsets()`.
   Functions optimized: `RowImpl::visitConst()`, `visit()`, `visit<T>()`, `visitConst<T>()`,
   `get(span)`; `RowView::visitConst()`, `visit()`, `visit<T>()`, `visitConst<T>()`,
   `validate()`, `get(span)`.
   Also added missing `out_of_range` bounds check in `RowImpl::get(span)`.

### Known Remaining Issues

| Issue | Severity | Status |
|-------|----------|--------|
| `const_cast` in `deserializeElementsZoH` | Medium — correctness risk | **Fixed** — signature changed to `RowType&` |
| Redundant `layout_` in ZoH (also in `flat_`) | Low — wasted pointer | Deferred |
| `setLayout()` public on Flat001 dynamic | Low — API footgun | Deferred |
| `sizeOf()` vs `wireSizeOf()` naming inconsistency | Low — confusing | Deferred |
| `reset()` is no-op in both codecs | Low — dead code path in CodecDispatch | Deferred |
| ZoH serialize incremental resize | Low — perf (not correctness) | Deferred |

### Summary

The codec extraction achieved its architectural goal: wire-format knowledge is
now encapsulated in versioned codec classes, Reader uses runtime dispatch, and
adding a new format requires only a new codec file. The quantitative targets
(row.hpp −42%, net LOC reduction) were not met — the refactor is net additive
(+713 library lines). Post-review fixes addressed cache bloat (−12 bytes/RowView),
a const_cast correctness issue, and per-iteration accessor overhead in all visit loops.
Remaining visit regression (if any) to be quantified by post-commit benchmark.

The value is structural: the codec layer is the prerequisite for Items 12–14
(CSV codec, delta encoding).

---

## 1. Why — Problem Statement

Serialization logic is currently embedded inside the Row classes (`RowImpl`, `RowStaticImpl`) and
the Row view classes (`RowView`, `RowViewStatic`). This creates several problems:

| Problem | Evidence |
|---------|----------|
| **Row is too heavy** | row.hpp is ~3800 lines. Rows carry wire-specific state (`offsets_`, `wire_data_off_`, `wire_lens_off_`, `wire_fixed_size_`, `bool_scratch_`) that is only needed during I/O. |
| **Wire metadata in wrong place** | `wire_bits_size_`, `wire_data_size_`, `wire_strg_size_` live in `Layout::Data`. Layout should be format-agnostic (column names, types, offsets). |
| **Duplication** | Flat encoding logic is duplicated across RowImpl, RowStaticImpl, RowView, RowViewStatic — four copies of similar column-loop and offset-computation code. Serialize and deserialize mirror each other's structure but share no extracted helpers. |
| **Encoding concerns leak into Reader/Writer** | Writer does ZoH repeat detection (byte comparison), change-flag management (`setChanges()`/`resetChanges()`), first-row-in-packet logic. Reader branches on `FileFlags::ZERO_ORDER_HOLD`. These are encoding policy, not I/O policy. |
| **No extension point** | Adding a new encoding (CSV, delta) requires modifying Row classes and Reader/Writer. No way to plug in an alternative format without touching core code. |
| **Delta encoding needs heavy inter-row state** | Item 13/14 requires ring buffers, prediction state, per-column history. This state is fundamentally different from flat's stateless encoding and ZoH's single-prev buffer. Combining all formats in one class would force every flat codec instance to carry dead delta state. |
| **Backward compatibility forces runtime format dispatch** | When Reader opens a file with an older format version, it must select the matching codec at runtime. A combined class would require internal if-chains for every format version — unmaintainable. Separate classes fit naturally into `std::variant`-based dispatch. |
| **Row vs RowView: different access patterns, same format knowledge** | Row uses eager/bulk deserialization (throughput for dense reads). RowView uses lazy per-column access (efficiency for sparse reads). Both need identical wire-format knowledge (section sizes, per-column offsets, string scanning) but currently compute it independently in duplicated code. |

### What success looks like

- Row classes contain only data storage and access (get/set/visit/clear/clone). ✅
- RowView classes contain only typed accessors over a buffer, delegating format logic to the codec. ✅ (partially — visit loop structure remains in RowView, uses codec metadata)
- Reader/Writer contain only stream, packet, compression, and checksum management. ✅
- Each wire format lives in its own codec class — one class, one format, one version, both directions. ✅
- Adding a new encoding means writing a new codec file — no changes to Row, RowView, or existing codecs. ✅
- Format version evolution is handled by adding a new versioned class (e.g., `RowCodecFlat002`) alongside the existing one. ✅ (architecture supports this)
- ~~row.hpp shrinks from ~3800 to ~2200 lines.~~ Actual: 3800 → 3275 (−14%). Visit loop structure is RowView-intrinsic.
- ~~No performance regression (benchmark verified).~~ Mixed: bool/ZoH improved, 72-col visitConst regressed +18–32%.

---

## 2. What — Design

### 2.1 Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                      PUBLIC API (unchanged)                          │
│                                                                      │
│  Writer<Layout, Policy>              Reader<Layout, Policy>          │
│  ├─ row_: RowType                    ├─ row_: RowType                │
│  ├─ codec_: RowCodecVariant          ├─ codec_: RowCodecVariant      │
│  ├─ stream_, lz4_, packet_hash_      ├─ stream_, lz4_, packet_hash_ │
│  ├─ writeRow()                       ├─ readNext()                   │
│  │    └─ visit(codec_, serialize)    │    └─ visit(codec_, deserialize)
│  └─ openPacket()                     └─ openPacket()                 │
│       └─ visit(codec_, reset)              └─ visit(codec_, reset)  │
│                                                                      │
│  RowView / RowViewStatic<Ts...>                                      │
│  ├─ layout_, buffer_: span<byte>                                     │
│  ├─ codec_: RowCodecFlat001          (owns per-column offset state)  │
│  ├─ get(col) → codec_.readColumn(buffer_, col)                      │
│  ├─ set(col) → codec_.writeColumn(buffer_, col)                     │
│  └─ visit()  → codec_.visitColumns(buffer_, ...)                    │
│                                                                      │
├──────────────────────────────────────────────────────────────────────┤
│                    CODEC LAYER (new, internal)                       │
│     One class per wire format, versioned, no inheritance             │
│     Each codec handles BOTH serialize+deserialize AND                │
│     provides per-column access for RowView                           │
│                                                                      │
│  RowCodecFlat001<L, P>                                               │
│  ├─ wire_bits_size_, wire_data_size_, wire_strg_count_              │
│  ├─ offsets_: vector<uint32_t>       (per-column wire offsets)       │
│  ├─ setup(layout)                    (compute all wire metadata)    │
│  ├─ reset()                          (no-op for flat)               │
│  │                                                                   │
│  ├─ ── Bulk operations (for Row) ──────────────────────────────────  │
│  ├─ serialize(row, buffer) → span    (Row → bytes, dense/fast)      │
│  ├─ deserialize(buffer, row)         (bytes → Row, dense/fast)      │
│  │                                                                   │
│  ├─ ── Per-column access (for RowView) ────────────────────────────  │
│  ├─ readColumn(buffer, col) → span   (lazy, O(1) scalars, O(N) str)│
│  ├─ writeColumn(buffer, col, val)    (in-place for primitives)      │
│  └─ wireBitsSize(), wireFixedSize()  (metadata accessors)           │
│                                                                      │
│  RowCodecZoH001<L, P>                                                │
│  ├─ flat_: RowCodecFlat001           (composition — reuses flat)    │
│  ├─ prev_buffer_: ByteBuffer         (ZoH inter-row state)         │
│  ├─ is_first_row_: bool                                             │
│  ├─ setup(layout)                    (delegates to flat_.setup())   │
│  ├─ reset()                          (clears prev_buffer_)         │
│  ├─ serialize(row, buffer) → span    (delta encode, first row→flat) │
│  ├─ deserialize(buffer, row)         (delta decode)                 │
│  ├─ readColumn(), writeColumn()      (delegates to flat_)           │
│  └─ wireBitsSize(), wireFixedSize()  (delegates to flat_)           │
│                                                                      │
│  ── Future (not in Item 11) ──────────────────────────────────────── │
│  RowCodecCSV001<L, P>                (Item 12 — text format)        │
│  RowCodecDelta001<L, P>              (Item 13/14 — ring buffer,     │
│  │  ├─ history_ring_[]                 prediction, per-col state)   │
│  RowCodecFlat002<L, P>               (backward-compat evolution)    │
│                                                                      │
├──────────────────────────────────────────────────────────────────────┤
│  Dispatch (stack-allocated, no heap, no virtual):                    │
│                                                                      │
│  using RowCodecVariant = std::variant<                               │
│      RowCodecFlat001<L, P>,                                          │
│      RowCodecZoH001<L, P>                                            │
│      // future: RowCodecDelta001<L, P>, RowCodecFlat002<L, P>, ...  │
│  >;                                                                  │
│                                                                      │
├──────────────────────────────────────────────────────────────────────┤
│                      DATA LAYER (simplified)                         │
│                                                                      │
│  Layout / LayoutStatic<Ts...>        RowImpl<Policy>                 │
│  ├─ names_, types_, offsets_         ├─ bits_, data_, strg_          │
│  ├─ tracked_mask_                    ├─ layout_&                     │
│  ├─ ✗ NO wire metadata               ├─ get/set/visit/clear/clone    │
│  └─ columnCount(), columnType()...   └─ ✗ NO serialize/deserialize   │
│                                                                      │
├──────────────────────────────────────────────────────────────────────┤
│  ── Future Layer (not in Item 11) ─────────────────────────────────  │
│                                                                      │
│  FileCodecStream001                  FileCodecPackets001             │
│  (streaming I/O, future)             (LZ4, VLE, checksums, packets) │
│  These compose a RowCodecVariant internally.                         │
│  Extracted from Writer/Reader — separate facility from RowCodec.     │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 Naming Convention

All codec types follow a versioned naming scheme:

```
Row  Codec  Flat  001
│    │      │     │
│    │      │     └─ Version: 3-digit, monotonically increasing
│    │      └────── Format : Flat, ZoH, CSV, Delta, ...
│    └───────────── Role   : Codec (serialize + deserialize + column access)
└────────────────── Scope  : Row (row-level) or File (future)
```

**Rules:**
- Each version number represents a **wire-format version**, not a refactoring iteration.
- A new wire-format version (e.g., `Flat002`) is created only when the on-disk byte layout changes.
- Old versions are kept for backward-compatible reading — they are never deleted.
- The Writer always writes the latest version; the Reader selects the appropriate codec based on the file header's format version field.

**Planned classes (Item 11 scope):**

| Class | Format | Purpose |
|-------|--------|---------|
| `RowCodecFlat001<L, P>` | Binary flat: `[bits][data][strg_lens][strg_data]` | Bulk Row serialize/deserialize + RowView per-column access |
| `RowCodecZoH001<L, P>` | Binary ZoH: `[change_bitset][changed_data]`, composes Flat001 | Bulk Row serialize/deserialize + delegates column access to Flat001 |

**Future classes (out of scope, shown for naming illustration):**

| Class | Format | Scope |
|-------|--------|-------|
| `RowCodecCSV001<L, P>` | Text CSV | Item 12 |
| `RowCodecDelta001<L, P>` | Binary delta (ring buffer, prediction) | Item 13/14 |
| `RowCodecFlat002<L, P>` | Binary flat v2 (wire-format evolution) | Backward compat |
| `FileCodecStream001` | Streaming file I/O (future extraction from Writer) | Post-14 |
| `FileCodecPackets001` | Packet-based file I/O (LZ4, VLE, checksums) | Post-14 |

### 2.3 Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Combined Codec** | Serialize + deserialize in one class | They operate on the same wire format symmetrically. Share wire metadata, `setup()` logic, column offset computation, and lifecycle state. In a header-only template library, unused methods (e.g., `deserialize()` in Writer) are never instantiated by the compiler — zero binary cost. |
| **Row + RowView in one codec** | Bulk ops AND per-column access in one class | Both Row (eager/dense) and RowView (lazy/sparse) need identical wire-format knowledge: section sizes, per-column offsets, string scanning. Keeping it in one class means exactly ONE place to update when the format evolves. RowView delegates `get()`/`set()` to codec column-access methods. Dead methods are free in templates. |
| **One class per format** | Separated classes, not combined across formats | Delta encoding carries heavy state (ring buffers, prediction) that flat doesn't need. Backward compatibility requires runtime dispatch between format versions — separate classes map naturally to `std::variant`. Each codec is self-contained and testable in isolation. |
| **ZoH composes Flat** | `RowCodecZoH001` holds a `RowCodecFlat001` member | ZoH's first-row-in-packet is a full flat row. Changed-column encoding reuses the flat column loop. Per-column RowView access delegates to the inner flat codec. Composition avoids duplicating flat logic while keeping ZoH's inter-row state isolated. |
| **`std::variant` dispatch** | Stack-allocated variant, `std::visit` at call sites | No heap allocation, no virtual table overhead. The variant is set once (at file open) and visited per row. Branch prediction handles the invariant dispatch efficiently (~1 predicted branch vs. the serialize work). |
| **No base class / no concept enforcement** | Duck-typed interface (implicit concept) | All codecs expose `setup()`, `reset()`, `serialize()`, `deserialize()`, `readColumn()`, `writeColumn()`, `wireFixedSize()` etc. This is enforced by `std::visit` — if a new codec doesn't match the lambda signature, it won't compile. An explicit C++20 concept can be added later if the family grows beyond ~4 types. |
| **Row-level vs File-level: separate** | RowCodec and FileCodec are distinct class families | RowCodec handles Row ↔ bytes. FileCodec (future) handles packets, LZ4, VLE, checksums. FileCodec composes RowCodec. Different abstraction layers, different state, different lifecycles. Merging would create a god-class. |
| **Wire metadata** | Moves to codec entirely | Layout becomes format-agnostic: only column names, types, in-memory offsets, tracked_mask. Wire sizes and per-column wire offsets move to each codec class. |
| **API visibility** | Internal only (for now) | Codecs are used by Reader/Writer and RowView, not exposed as public API. Can be promoted later for Item 12 (CSV) if needed. |
| **Friend access** | Narrow internal `friend` boundary (locked) | Each codec class is declared `friend` of `RowImpl`/`RowStaticImpl`. This gives direct access to `bits_`, `data_`, `strg_` without polluting Row's public API. Combined codec means fewer friend declarations (1 per format vs 2). |

### 2.4 Row vs RowView: Two Access Patterns, One Codec

The codec serves two fundamentally different access patterns:

```
┌─────────────────────────────────────────────────────────────────┐
│  Row (RowImpl / RowStaticImpl) — DENSE / THROUGHPUT             │
│                                                                  │
│  serialize():   Row.bits_ + Row.data_ + Row.strg_  ──→  bytes  │
│                 Single-pass column loop, bulk memcpy,            │
│                 string pre-scan for buffer sizing.               │
│                                                                  │
│  deserialize(): bytes  ──→  Row.bits_ + Row.data_ + Row.strg_  │
│                 Single-pass, all columns decoded, data owned.    │
│                                                                  │
│  Used by: Writer (serialize), Reader (deserialize)               │
├─────────────────────────────────────────────────────────────────┤
│  RowView (RowView / RowViewStatic) — SPARSE / LAZY              │
│                                                                  │
│  readColumn(buf, col):   buffer[offset] → span<byte>            │
│                          No decode until requested.              │
│                          O(1) for scalars/bools,                 │
│                          O(string_index) for strings.            │
│                                                                  │
│  writeColumn(buf, col):  value → buffer[offset]                 │
│                          In-place mutation, primitives only.      │
│                                                                  │
│  Used by: RowView.get(), RowView.set(), RowView.visit()          │
└─────────────────────────────────────────────────────────────────┘
```

**Why one class handles both:**
- Both paths share **100% of wire metadata** (section sizes, per-column offsets).
- The per-column offset computation in RowView's current constructor is **identical** to the cursor logic inside `serializeTo()`.
- If the wire format changes (Flat001 → Flat002), only ONE class needs updating — not separate Codec + Decoder.
- Dead methods are free: Writer never calls `readColumn()`, RowView never calls `serialize()`. The compiler doesn't instantiate unused template methods.

### 2.5 Codec Lifecycle Contract

All codecs follow the same lifecycle state machine:

```
[Constructed] ──setup(layout)──▶ [Ready] ──reset()──▶ [Active] ⇄ serialize()/deserialize()/readColumn()
                                    ▲                     │
                                    └─────── reset() ─────┘  (new packet)
```

**Invariants (apply to all RowCodec types):**
- `setup(layout)` **must** be called exactly once before any operation.
- `reset()` **must** be called at each packet start (before the first row in a packet).
- Codec is **single-stream stateful** — not thread-safe. One codec per Writer/Reader.
- `readColumn()` / `writeColumn()` can be called without `reset()` (they are stateless per-column operations). This allows RowView to use the codec without packet-level lifecycle.

**Format-specific notes:**
- `RowCodecFlat001`: stateless between rows — `reset()` is a no-op (provided for interface uniformity).
- `RowCodecZoH001`: `reset()` clears `prev_buffer_`, sets `is_first_row_`, marks all columns changed.
- Future `RowCodecDelta001`: `reset()` would clear the history ring and prediction state.

### 2.6 Interface Sketch

```cpp
// ────────────────────────────────────────────────────────────────────
// RowCodecFlat001: flat binary format — both directions + column access
// ────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
class RowCodecFlat001 {
    using RowType = typename LayoutType::template RowType<Policy>;
public:
    // ── Lifecycle ────────────────────────────────────────────────────
    void setup(const LayoutType& layout);     // compute wire metadata + offsets
    void reset();                              // no-op (stateless between rows)

    // ── Bulk operations (Row — throughput path) ─────────────────────
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer) const;
    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

    // ── Per-column access (RowView — sparse/lazy path) ──────────────
    [[nodiscard]] std::span<const std::byte> readColumn(
        std::span<const std::byte> buffer, size_t col) const;
    bool writeColumn(
        std::span<std::byte> buffer, size_t col,
        std::span<const std::byte> value) const;

    // ── Wire metadata ───────────────────────────────────────────────
    uint32_t wireBitsSize()  const noexcept;
    uint32_t wireDataSize()  const noexcept;
    uint32_t wireStrgCount() const noexcept;
    uint32_t wireFixedSize() const noexcept;
    uint32_t columnOffset(size_t col) const noexcept;  // per-column

private:
    const LayoutType* layout_{nullptr};
    uint32_t wire_bits_size_{0};
    uint32_t wire_data_size_{0};
    uint32_t wire_strg_count_{0};
    std::vector<uint32_t> offsets_;  // per-column section-relative offsets
    bool setup_done_{false};
};

// ────────────────────────────────────────────────────────────────────
// RowCodecZoH001: zero-order-hold delta, composes Flat001
// ────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
class RowCodecZoH001 {
    using RowType = typename LayoutType::template RowType<Policy>;
public:
    void setup(const LayoutType& layout);
    void reset();                              // clears prev_buffer_, resets first-row flag

    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer) const;
    // Empty span return → ZoH repeat (write length 0 to stream)
    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

    // Per-column access — delegates to flat_
    [[nodiscard]] std::span<const std::byte> readColumn(
        std::span<const std::byte> buffer, size_t col) const;
    bool writeColumn(
        std::span<std::byte> buffer, size_t col,
        std::span<const std::byte> value) const;

    uint32_t wireBitsSize()  const noexcept;   // all delegate to flat_
    uint32_t wireDataSize()  const noexcept;
    uint32_t wireStrgCount() const noexcept;
    uint32_t wireFixedSize() const noexcept;
    uint32_t columnOffset(size_t col) const noexcept;

private:
    RowCodecFlat001<LayoutType, Policy> flat_;  // composition
    mutable ByteBuffer prev_buffer_;
    mutable bool is_first_row_{true};
};

// ────────────────────────────────────────────────────────────────────
// dispatch variant used by Writer / Reader
// ────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
using RowCodecVariant = std::variant<
    RowCodecFlat001<LayoutType, Policy>,
    RowCodecZoH001<LayoutType, Policy>
>;
```

### 2.7 Why Combined Codec (Design Rationale)

Three earlier design alternatives were evaluated and rejected:

| Alternative | Why rejected |
|-------------|-------------|
| **Separate Serializer + Deserializer per format** | Wire metadata (`wire_bits_size_`, etc.), `setup()` logic, lifecycle state, and per-column offset computation are identical in both directions. Splitting them duplicates all of this. Combined "Codec" class eliminates this duplication. Unused direction methods are free in header-only templates (compiler only instantiates called member functions). |
| **Separate Codec + FlatDecoder (Row vs RowView)** | RowView's per-column offset computation is **identical** to the cursor logic in `serializeTo()`. Splitting them into separate classes duplicates wire-format knowledge. When the format evolves (Flat001 → Flat002), two classes must be updated instead of one. Keeping both bulk and per-column access in one codec ensures a single source of format truth. |
| **Combined across formats (one class for Flat + ZoH + Delta)** | Delta encoding carries heavy state (ring buffers, prediction) that bloats flat instances. Backward compatibility requires runtime dispatch between format versions — separate classes map naturally to `std::variant`. |

### 2.8 What Moves Where

| Current location | Current member/method | Destination |
|-----------------|----------------------|-------------|
| `RowImpl::serializeTo()` | row.hpp L467 | `RowCodecFlat001::serialize()` |
| `RowImpl::serializeToZoH()` | row.hpp L622 | `RowCodecZoH001::serialize()` |
| `RowImpl::deserializeFrom()` | row.hpp L548 | `RowCodecFlat001::deserialize()` |
| `RowImpl::deserializeFromZoH()` | row.hpp L686 | `RowCodecZoH001::deserialize()` |
| `RowStaticImpl::serializeTo()` | row.hpp L2836 | `RowCodecFlat001<LayoutStatic>::serialize()` |
| `RowStaticImpl::serializeToZoH()` | row.hpp L2884 | `RowCodecZoH001<LayoutStatic>::serialize()` |
| `RowStaticImpl::deserializeFrom()` | row.hpp L2958 | `RowCodecFlat001<LayoutStatic>::deserialize()` |
| `RowStaticImpl::deserializeFromZoH()` | row.hpp L3004 | `RowCodecZoH001<LayoutStatic>::deserialize()` |
| `RowStaticImpl::serializeElements<>()` etc. | row.hpp private helpers | `RowCodecFlat001<LayoutStatic>` private helpers |
| `RowView` constructor offset computation | row.hpp L1459-1481 | `RowCodecFlat001::setup()` (shared with bulk path) |
| `RowView::get()` column-access logic | row.hpp L1488-1531 | `RowCodecFlat001::readColumn()` |
| `RowView::set()` column-mutation logic | row.hpp L1703-1770 | `RowCodecFlat001::writeColumn()` |
| `RowView::offsets_` | row.h L592 | `RowCodecFlat001::offsets_` |
| `RowView::wire_data_off_` | row.h L593 | Derived from `RowCodecFlat001::wireBitsSize()` |
| `RowView::wire_lens_off_` | row.h L594 | Derived from codec wire metadata |
| `RowView::wire_fixed_size_` | row.h L595 | `RowCodecFlat001::wireFixedSize()` |
| `RowView::bool_scratch_` | row.h L596 | Stays in RowView (accessor-level scratch, not format-specific) |
| `RowViewStatic::WIRE_OFFSETS[]` | row.h constexpr | `RowCodecFlat001<LayoutStatic>` constexpr |
| `RowViewStatic::WIRE_BITS_SIZE` etc. | row.h constexpr | `RowCodecFlat001<LayoutStatic>` constexpr |
| `Layout::Data::wire_bits_size_` | layout.h L103 | `RowCodecFlat001` member |
| `Layout::Data::wire_data_size_` | layout.h L104 | `RowCodecFlat001` member |
| `Layout::Data::wire_strg_size_` | layout.h L105 | `RowCodecFlat001` member |
| `Layout::wireBitsSize()` etc. | layout.h L140-144, L242-255 | `RowCodecFlat001` accessors |
| `RowStaticImpl::WIRE_BITS_SIZE` etc. | row.h constexpr | `RowCodecFlat001<LayoutStatic>` constexpr |
| `Writer::row_buffer_prev_` comparison | writer.hpp L267-280 | `RowCodecZoH001` internal `prev_buffer_` |
| `Writer: setChanges()/resetChanges()` | writer.hpp L246-250 | `RowCodecZoH001::reset()` / `serialize()` |

### 2.9 What Stays Where

| Component | Keeps |
|-----------|-------|
| **Row classes** | `bits_`, `data_`, `strg_`, `layout_&`, `get()`, `set()`, `visit()`, `visitConst()`, `clear()`, `clone()`, `assign()`, `hasChanges()`, `setChanges()`, `resetChanges()` |
| **RowView** | `layout_`, `buffer_`, `bool_scratch_`, `codec_` member (owns format state), `get()`, `set()`, `visit()` (delegate to codec), `toRow()` |
| **Layout** | `column_names_`, `column_types_`, `offsets_`, `tracked_mask_`, `boolMask()`, `addColumn()`, `removeColumn()`, observer callbacks |
| **Writer** | `stream_`, `lz4_stream_`, `packet_hash_`, `packet_index_`, `row_buffer_raw_`, `row_cnt_`, `openPacket()`, `closePacket()`, `writeRow()` (delegating via variant visit), VLE encoding, LZ4 compression |
| **Reader** | `stream_`, `lz4_stream_`, `packet_hash_`, `row_buffer_`, `row_pos_`, `openPacket()`, `closePacket()`, `readNext()` (delegating via variant visit), VLE decoding, LZ4 decompression, ZoH repeat detection (`rowLen==0` → skip deserialize) |

### 2.10 New File Organization

| File | Contents | ~Lines |
|------|----------|--------|
| `include/bcsv/row_codec_flat001.h` | `RowCodecFlat001` declaration (primary + LayoutStatic specialization) | ~150 |
| `include/bcsv/row_codec_flat001.hpp` | Flat001 implementation: setup, serialize, deserialize, readColumn, writeColumn, wire metadata | ~600 |
| `include/bcsv/row_codec_zoh001.h` | `RowCodecZoH001` declaration | ~100 |
| `include/bcsv/row_codec_zoh001.hpp` | ZoH001 implementation: compose Flat001, delta logic, prev_buffer | ~400 |
| `include/bcsv/row_codec_variant.h` | `RowCodecVariant` type alias | ~20 |
| `include/bcsv/row_codec_detail.h` | `detail::` shared column-loop helpers (encode/decode columns) | ~200 |

`row.hpp` shrinks from ~3800 to ~2200 lines.

`bcsv.h` adds includes for all the above headers.

### 2.11 How This Enables Future Items

| Future item | How codec architecture helps |
|-------------|------------------------------|
| **Item 12 (CSV)** | `RowCodecCSV001` in its own file — text format, delimiters, quoting. All in one class: serialize Row → CSV text, deserialize CSV → Row, per-column text parsing for a hypothetical CsvRowView. Zero shared code with binary codecs. |
| **Item 13/14 (delta)** | `RowCodecDelta001` with ring buffers and prediction state, in its own file. Flat/ZoH codecs don't carry any of this weight. `reset()` clears delta history at packet boundaries. |
| **Backward compatibility** | Reader reads file header → format version field → constructs the matching `RowCodecFlatNNN` in the variant. Old codecs are never deleted, just kept alongside new ones. Writer always uses the latest version. |
| **FileCodec (future)** | `FileCodecStream001` / `FileCodecPackets001` extract packet management and LZ4/VLE/checksum from Writer/Reader. They compose a `RowCodecVariant` internally. The RowCodec interface is unchanged. |
| **New format** | Create `RowCodecFlat002`, add it to the variant, update the factory in Reader. No existing codec is modified. Open-closed principle. |

---

## 3. How — Implementation Steps

### Phase 1: Create RowCodecFlat001

**Goal:** New files exist for `RowCodecFlat001`, flat serialize + deserialize + per-column access works, Row still has its old methods (dual path for validation).

| Step | Action | Validation |
|------|--------|------------|
| 1.1 | Create `row_codec_flat001.h` with `RowCodecFlat001` declaration (template on LayoutType, Policy). Include `setup()`, `reset()`, `serialize()`, `deserialize()`, `readColumn()`, `writeColumn()`, wire metadata accessors. Primary template (dynamic Layout) and partial specialization (LayoutStatic). | Compiles. |
| 1.2 | Create `row_codec_detail.h` with `detail::` free functions for shared column-loop encoding/decoding helpers (pack bools, encode scalars, encode strings). These are used by both Flat001 and future ZoH001. | Compiles. |
| 1.3 | Create `row_codec_flat001.hpp` — implement `setup()`: compute `wire_bits_size_`, `wire_data_size_`, `wire_strg_count_`, and per-column `offsets_[]` from layout. Implement `serialize()` for dynamic Layout (extract from `RowImpl::serializeTo()`). Implement `deserialize()` (extract from `RowImpl::deserializeFrom()`). | Unit tests: setup produces correct wire metadata and per-column offsets. Roundtrip parity: byte-identical output to old `RowImpl::serializeTo()`. |
| 1.4 | Implement `readColumn()` and `writeColumn()` — extract from `RowView::get()` / `RowView::set()` logic. These use the same `offsets_[]` computed in `setup()`. | Unit tests: `readColumn()` returns same spans as current `RowView::get()` for all column types. |
| 1.5 | Implement Flat001 for **LayoutStatic** partial specialization — compile-time recursive helpers for serialize/deserialize, constexpr `WIRE_OFFSETS[]` for readColumn/writeColumn. | Same roundtrip + column-access tests for static rows. |
| 1.6 | Add friend declarations in `RowImpl` and `RowStaticImpl` for `RowCodecFlat001`. Add forward declarations in `layout.h`. Update `bcsv.h` includes. | Full build. All 343 existing GTest pass. |

**Checkpoint A — Flat001 parity verified.** All roundtrip tests pass (serialize, deserialize, column access). Old and new paths produce byte-identical output.

### Phase 2: Create RowCodecZoH001

**Goal:** New `RowCodecZoH001` that composes Flat001. ZoH encoding parity verified.

| Step | Action | Validation |
|------|--------|------------|
| 2.1 | Create `row_codec_zoh001.h` with `RowCodecZoH001` declaration. Holds a `RowCodecFlat001` member (composition). Includes `prev_buffer_`, `is_first_row_` state. `readColumn()` / `writeColumn()` delegate to `flat_`. | Compiles. |
| 2.2 | Create `row_codec_zoh001.hpp` — implement `serialize()` for RowImpl: move from `RowImpl::serializeToZoH()`. Handle change flags, prev_buffer comparison, ZoH repeat detection internally. First-row-in-packet delegates to `flat_.serialize()`. | ZoH roundtrip test: serialize via new → identical bytes to old `serializeToZoH()`. |
| 2.3 | Implement `deserialize()` for RowImpl — move from `RowImpl::deserializeFromZoH()`. | ZoH deserialize parity test. |
| 2.4 | Implement ZoH for LayoutStatic partial specialization. | Same roundtrip tests for static rows. |
| 2.5 | Implement `reset()`: clear `prev_buffer_`, set `is_first_row_`, mark all columns changed (for ZoH first-row semantics). | Packet-boundary test: reset + serialize first row → all columns present. |
| 2.6 | Add friend declarations in `RowImpl` and `RowStaticImpl` for `RowCodecZoH001`. Update `bcsv.h`. | Full build. All existing GTest pass. |

**Checkpoint B — Full parity.** Flat001 + ZoH001, RowImpl + RowStaticImpl. New codecs produce byte-identical output for all encoding paths.

### Phase 3: Wire Reader/Writer to variant-dispatched codecs

**Goal:** Reader/Writer use `RowCodecVariant` with `std::visit` dispatch. All existing tests pass through the new layer.

| Step | Action | Validation |
|------|--------|------------|
| 3.1 | Create `row_codec_variant.h` with `RowCodecVariant` type alias (`std::variant<Flat001, ZoH001>`). | Compiles. |
| 3.2 | Add `codec_: RowCodecVariant` member to Writer. In `open()`, construct the appropriate variant member based on `FileFlags::ZERO_ORDER_HOLD`. In `writeRow()`, replace `row_.serializeTo(buffer)` / `row_.serializeToZoH(buffer)` with `std::visit([&](auto& c) { return c.serialize(row_, buffer); }, codec_)`. Remove `setChanges()`/`resetChanges()` calls and `row_buffer_prev_` comparison. In `openPacket()`, call `std::visit([](auto& c) { c.reset(); }, codec_)`. | **All existing tests pass** — 343 GTest + 76 C API + Row API. |
| 3.3 | Add `codec_: RowCodecVariant` member to Reader. In `open()`, construct based on file header flags. In `readNext()`, replace `row_.deserializeFrom()` / `row_.deserializeFromZoH()` with `std::visit`. | All existing tests pass. |
| 3.4 | Run full benchmark suite (`--size=S`). Compare against item 10 baseline. No regression beyond noise (±3%). | Benchmark comparison report. |

**Checkpoint C — Reader/Writer migrated.** All tests and benchmarks pass through `std::variant` dispatch.

> **Milestone 11a complete.** Phases 1–3 are self-contained and committable. RowView migration and cleanup follow in 11b.

### Phase 4: Migrate RowView to codec

| Step | Action | Validation |
|------|--------|------------|
| 4.1 | Add `codec_: RowCodecFlat001` member to RowView. In constructor, call `codec_.setup(layout)` instead of computing `offsets_`, `wire_data_off_`, `wire_lens_off_`, `wire_fixed_size_` inline. | All RowView tests pass. |
| 4.2 | Replace `get()` / `set()` / `visit()` internals in RowView to delegate to `codec_.readColumn()` / `codec_.writeColumn()`. Remove `offsets_`, `wire_data_off_`, `wire_lens_off_`, `wire_fixed_size_` from RowView. Keep `bool_scratch_` (accessor-level, not format-specific). | All RowView tests pass. |
| 4.3 | Same migration for RowViewStatic — the `constexpr WIRE_OFFSETS[]`, `WIRE_BITS_SIZE` etc. move to `RowCodecFlat001<LayoutStatic>` constexpr members. RowViewStatic delegates to codec. | All RowViewStatic tests pass. |

**Checkpoint D — RowView decoupled from wire format.** RowView is now a thin typed accessor layer over the codec.

### Phase 5: Remove old serialize/deserialize from Row classes

| Step | Action | Validation |
|------|--------|------------|
| 5.1 | Remove `serializeTo()`, `serializeToZoH()`, `deserializeFrom()`, `deserializeFromZoH()` from RowImpl. Remove companion private helpers. | All tests pass (they now use the codec variant). |
| 5.2 | Same removal from RowStaticImpl — including `serializeElements<>()`, `serializeElementsZoH<>()`, `deserializeElements<>()`, `deserializeElementsZoH<>()`, `WIRE_*` constexpr. | All tests pass. |
| 5.3 | Remove wire metadata from Layout::Data (`wire_bits_size_`, `wire_data_size_`, `wire_strg_size_`, `wireFixedSize()`, and their computation in `addColumn()`/`removeColumn()`/`rebuildOffsets()`). Remove from Layout facade. Remove from copy constructor. | All tests pass. |
| 5.4 | Update `bcsv.h` includes. Clean up any remaining references. | Full build: debug + release. All tests. All examples. All CLI tools. |
| 5.5 | Final benchmark run (`--size=S`). Compare against Checkpoint C baseline. | No regression. |

**Checkpoint E — Extraction complete.** row.hpp is ~2200 lines. Layout is format-agnostic. Each codec is the single source of truth for its wire format.

> **Milestone 11b complete.** RowView migrated, legacy code removed, Layout cleaned up.

### Phase 6: Verification and close

| Step | Action |
|------|--------|
| 6.1 | Run Lean Checklist (docs/LEAN_CHECKLIST.md) against final state. |
| 6.2 | Update ARCHITECTURE.md with new codec layer description (include naming convention, variant dispatch, composition pattern, Row vs RowView access patterns). |
| 6.3 | Update SKILLS.md with new file references. |
| 6.4 | Update ToDo.txt — mark item 11 `[x]`, document deliverables. |
| 6.5 | Commit. |

---

## 4. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| **Friend count growth** | Each new codec format needs a friend declaration in Row. | Medium | Acceptable for the current roadmap (~4 codecs = 4 friend lines, down from 8 with separate ser/deser). If it grows further, introduce a `detail::RowAccessor` intermediary that is the **sole** friend. |
| **`std::variant` size growth** | Each new codec added to the variant increases its `sizeof` to the largest member. Delta encoding's heavy state could bloat the variant. | Medium | Keep the variant minimal. If Delta's state exceeds ~2KB, switch to `std::unique_ptr<DeltaCodec>` wrapped in a thin variant member (heap for Delta only, stack for Flat/ZoH). |
| **Performance regression from `std::visit`** | Virtual-like dispatch overhead on hot path. | Low | `std::visit` on a small variant (2–4 types) compiles to a jump table or branch chain. The variant is invariant per-file — branch prediction handles it after ~2 rows. Validate via benchmark at Checkpoints C and E. |
| **RowView API break** | Moving offset state to codec changes RowView's memory layout (sizeof). | Low | RowView is typically held by reference. Codec is a member — sizeof may change but public API is preserved. |
| **C API / Python / Unity impact** | If Row's serialize/deserialize removal changes the C API boundary. | Low | C API calls Reader/Writer, not Row serialize directly. Verify by building `test_c_api` and running 76/76 tests at each checkpoint. |
| **Codec used by RowView carries unused bulk methods** | Slightly larger class. | Negligible | Header-only templates: unused methods are never instantiated. Zero binary cost. The per-column `offsets_[]` is ~50 bytes for a typical layout — negligible even in Writer where it's "unused". |

---

## 5. Access Pattern for Codec → Row

**Decision (locked):** Narrow internal `friend` boundary.

- `RowImpl` and `RowStaticImpl` declare `friend class RowCodecFlat001<...>` and `friend class RowCodecZoH001<...>`.
- This gives each codec direct `const` access to `bits_`, `data_`, `strg_` (read for serialize, column access).
- This gives each codec direct mutable access to `bits_`, `data_`, `strg_` (write for deserialize).
- No new public accessors are added to Row. The friend boundary is narrow and explicit.
- Combined codec halves the friend count vs separate serializer + deserializer (1 per format instead of 2).
- Future formats (CSV, Delta) add their own friend declarations when they are implemented.

**Rationale:** Friend avoids polluting Row's public API with wire-format-specific accessors. The coupling is intentional and documented. Each codec class is tightly co-designed with Row's internal storage layout.

**Escalation path:** If the friend count grows beyond ~6, introduce a `detail::RowAccessor<Layout, Policy>` intermediary class that is the **sole** friend of Row and exposes raw buffer access to codecs through a narrow internal API.

---

## 6. Interruption Recovery

If work is interrupted at any checkpoint, here's how to resume:

| Interrupted at | State | How to resume |
|----------------|-------|---------------|
| Before Phase 1 | No new files. | Start from Step 1.1. |
| Checkpoint A | `row_codec_flat001.h/hpp` and `row_codec_detail.h` exist. Row still has old methods. | Continue to Phase 2 (ZoH001). Old code paths still work. |
| Checkpoint B | Flat001 + ZoH001 exist. Row still has old methods. | Continue to Phase 3 (variant + Reader/Writer). Both paths work. |
| Checkpoint C | Reader/Writer use variant dispatch. Row still has old methods (unused). | Continue to Phase 4 (RowView migration) or Phase 5 (remove old). Safe state. |
| Checkpoint D | RowView uses codec. | Continue to Phase 5. |
| Checkpoint E | Extraction complete. | Continue to Phase 6 (docs/close). |

At every checkpoint, **all tests pass and a commit can be made.** The codebase is in a shippable state.

---

## 7. Test Strategy

| Test type | When | What |
|-----------|------|------|
| **Flat001 roundtrip parity** | Phase 1 | `RowCodecFlat001.serialize()` produces byte-identical output to old `RowImpl::serializeTo()`, for all layout configurations. |
| **Flat001 column-access parity** | Phase 1 | `RowCodecFlat001.readColumn()` returns same spans as current `RowView::get()` for all column types. |
| **ZoH001 roundtrip parity** | Phase 2 | `RowCodecZoH001.serialize()` produces byte-identical output to old `RowImpl::serializeToZoH()`. |
| **Existing GTest suite** | Every checkpoint | 343 tests must pass (debug + release). |
| **C API + Row API** | Every checkpoint | 76 + all pass. |
| **Examples + CLI tools** | Checkpoint C, E | Build and smoke-run all examples and CLI tools. |
| **Benchmark** | Checkpoint C, E | See benchmark protocol below. No regression >3%. |
| **Lean Checklist** | Phase 6 | Full checklist pass. |

### 7.1 Benchmark Acceptance Protocol

| Parameter | Value |
|-----------|-------|
| **Command** | `run_benchmarks.py --size=S --repeat=3` |
| **Aggregation** | Median of 3 runs per scenario |
| **Build type** | Release (`-DCMAKE_BUILD_TYPE=Release`) |
| **Machine** | WorkhorseLNX (reference profile) |
| **Acceptance threshold** | Median must be within ±3% of baseline (item 10) |
| **Baseline** | Stored in WorkhorseLNX (item 10 commit) |
| **Comparison tool** | `compare_runs.py` |

If any scenario exceeds ±3%, investigate before proceeding. False regressions from system load should be re-run. Genuine regressions require profiling and resolution before the checkpoint is considered passed.

### 7.2 Cross-Implementation Parity Test Matrix

During Phases 1–2, each roundtrip parity test must cover the full matrix:

| Axis | Values |
|------|--------|
| **Layout type** | Dynamic (`Layout`), Static (`LayoutStatic<Ts...>`) |
| **Codec** | `RowCodecFlat001`, `RowCodecZoH001` |
| **Access pattern** | Bulk (serialize/deserialize), per-column (readColumn/writeColumn) |
| **Column mix** | All-numeric, all-string, mixed, booleans-only, empty-layout |
| **Row count** | Single row, multi-row packet, packet boundary crossing |
| **Reset behavior** | First row after `reset()` in ZoH001 has all columns present |

This matrix ensures no blind spots across template instantiations, encoding paths, and access patterns.

### 7.3 Additional Recommended Tests

| Test | Phase | Purpose |
|------|-------|---------|
| **Corruption resilience** | Phase 1-2 | Malformed row lengths / truncated packet rows in new deserialize path |
| **State reset** | Phase 2 | First row after `reset()` always serializes full context in ZoH mode |
| **Mixed workload** | Phase 2 | Alternating repeated and changing rows to validate ZoH repeat elision |
| **Backward compat golden files** | Phase 3 | Read existing `.bcsv` files from repo as golden fixtures |
| **Variant dispatch** | Phase 3 | Verify `std::visit` correctly routes to Flat001 vs ZoH001 based on FileFlags |
| **Composition delegation** | Phase 2 | ZoH001 first-row serializes identically to Flat001 standalone |
| **RowView parity** | Phase 1, 4 | `readColumn()` matches current `RowView::get()` for bools, scalars, strings |
| **RowView in-place write** | Phase 1, 4 | `writeColumn()` matches current `RowView::set()` for primitives |

---

## 8. Revision History

| Date | Change |
|------|--------|
| 2026-02-13 | Initial plan — combined `RowSerializer` class with `setZoH(bool)` flag. |
| 2026-02-14 | **Rev 1 — Separated-class architecture.** Motivated by: delta encoding weight and backward-compat dispatch. Split into `RowSerializerFlat001` + `RowDeserializerFlat001` + `RowSerializerZoH001` + `RowDeserializerZoH001`. |
| 2026-02-14 | **Rev 2 — Codec architecture.** Three key changes: (1) Combined Serializer + Deserializer into single "Codec" class per format — they share wire metadata, setup logic, and lifecycle state; unused methods free in header-only templates. (2) Merged RowView per-column access (FlatDecoder) into the codec — Row and RowView share 100% of wire-format knowledge; one class is the single source of format truth. (3) Confirmed Row-level and File-level codecs as separate facilities — FileCodec composes RowCodec, different abstraction layers. A Phase 1 implementation of the old combined-serializer design was completed and validated (40 parity tests, 383/383 pass) but reverted to adopt the codec architecture. |
| 2026-02-15 | **Rev 3 — Runtime codec dispatch.** Phases 1–6 complete (391/391 tests). Design review identified that TrackingPolicy and file codec are orthogonal axes that were incorrectly coupled. Phase 7 below adds `CodecDispatch` for runtime codec selection in Reader, decouples TrackingPolicy from codec choice, and makes all 4 combinations (Flat×Disabled, Flat×Enabled, ZoH×Disabled, ZoH×Enabled) functional. |
| 2026-02-15 | **Phase 7 complete.** All steps 7a–7h implemented. 404/404 GTest (13 new cross-combination tests), 76/76 C API, row API pass. Debug and release builds clean. Benchmark regression check passed. ARCHITECTURE.md and SKILLS.md updated. |

---

## Phase 7: Runtime Codec Dispatch — Orthogonal TrackingPolicy × Codec

### 7.1 Problem Statement

Phases 1–6 conflated two independent axes via `RowCodecType` (`std::conditional_t`):

```
TrackingPolicy::Disabled  →  RowCodecFlat001   (compile-time binding)
TrackingPolicy::Enabled   →  RowCodecZoH001    (compile-time binding)
```

**TrackingPolicy** is the programmer's intent: "do I need change flags in my Row?"
**File codec** is a file property: "how are rows encoded on disk?"

These are orthogonal. A programmer cannot know the file's codec before opening it. They may
want change tracking on a flat file (sparse GUI updates) or want to read a ZoH file without
tracking overhead (batch analytics). The current design forces the programmer to match the
Writer's codec choice — an API contract that cannot be checked until `open()` time, and that
silently fuses unrelated concerns.

Additionally, the codec must be selectable at runtime to support future format evolution:
older file versions may require different codec classes.

### 7.2 Compatibility Matrix

All four combinations must work:

| | Flat001 file | ZoH001 file |
|---|---|---|
| **Disabled** | Natural fit. Full decode, no tracking. | ZoH codec reads wire change-header into **internal** bitset. Decodes only changed columns. Extracts bool values → `row.bits_[boolIdx]`. No change flags. |
| **Enabled** | Full decode. Mark all columns as changed in `row.bits_` (or: compare-and-set per column for precise detection; future refinement). | Natural fit. Wire change-header → `row.bits_` directly (fast alias, same sizing). Change flags visible to programmer. |

**Why ZoH + Disabled is possible**: The ZoH wire format's change bitset has `columnCount` bits.
With `Disabled`, `row.bits_` is `boolCount`-sized (different size and indexing). The codec
cannot alias `row.bits_` as the wire header. **Fix**: the ZoH codec maintains its own internal
`Bitset<>` for the wire change header, then translates to row-appropriate form using
`if constexpr (isTrackingEnabled(Policy))`:
- Enabled → direct copy (same sizing, fast path preserved)
- Disabled → extract bool values only → `row.bits_[boolRelativeIndex]`

The `if constexpr` is zero-cost — the wrong branch is eliminated at compile time.

### 7.3 Design: CodecDispatch (Function-Pointer Dispatch Table)

Reader holds `CodecDispatch<LayoutType, Policy>` — resolves codec at `open()` time, hot path
uses function pointers (one indirect call per row, zero branching).

```
┌──────────────────────────────────────────────────────────┐
│ CodecDispatch<LayoutType, Policy>                        │
├──────────────────────────────────────────────────────────┤
│ Function pointers (resolved once at selectCodec):        │
│   deserialize_fn_  → concrete codec::deserialize         │
│   serialize_fn_    → concrete codec::serialize           │
│   reset_fn_        → concrete codec::reset               │
│   readColumn_fn_   → concrete codec::readColumn          │
├──────────────────────────────────────────────────────────┤
│ Storage (union — one active at a time):                  │
│   RowCodecFlat001<LayoutType, Policy> flat_;             │
│   RowCodecZoH001<LayoutType, Policy> zoh_;               │
├──────────────────────────────────────────────────────────┤
│ selectCodec(FileFlags, const LayoutType&)                │
│   → Reads ZERO_ORDER_HOLD flag                           │
│   → Constructs correct codec in union via placement new  │
│   → Wires function pointers                              │
│   → Future: version-based codec selection here           │
│                                                          │
│ deserialize(buffer, row) → deserialize_fn_(...)          │
│ reset()                  → reset_fn_(...)                │
│ serialize(row, buffer)   → serialize_fn_(...)            │
└──────────────────────────────────────────────────────────┘
```

**Hot path**: `codec_.deserialize(buffer, row_)` → single indirect call through function
pointer. Branch predictor learns target after first call. No per-row conditionals.

### 7.4 ZoH Codec Changes (Internal Change Bitset)

Remove `static_assert(isTrackingEnabled(Policy))` from ZoH001 serialize/deserialize.
Add internal bitset for wire change header:

```cpp
// Dynamic Layout:
Bitset<>  wire_bits_;   // resized to columnCount in setup()

// Static Layout:
Bitset<COLUMN_COUNT>  wire_bits_;   // always columnCount
```

**Deserialize** — new `if constexpr` translation at boundary:
```cpp
// 1. Read wire change header into wire_bits_ (always columnCount-sized)
std::memcpy(wire_bits_.data(), &buffer[0], wire_bits_.sizeBytes());

// 2. Translate to row's bits_
if constexpr (isTrackingEnabled(Policy)) {
    // Fast path: same sizing, direct copy
    std::memcpy(row.bits_.data(), wire_bits_.data(), wire_bits_.sizeBytes());
} else {
    // Disabled: extract only bool values from wire header
    for (size_t i = 0; i < layout_->columnCount(); ++i) {
        if (layout_->columnType(i) == ColumnType::BOOL) {
            row.bits_[layout_->columnOffset(i)] = wire_bits_[i];
        }
    }
}

// 3. Decode changed non-bool columns (unchanged columns retain values)
//    Loop uses wire_bits_[i] (not row.bits_[i]) for change-flag testing
```

**Serialize** (ZoH + Disabled):
```cpp
if constexpr (isTrackingEnabled(Policy)) {
    // Fast path: bits_ IS the change header (existing logic)
    wire_bits_ = row.bits_;
} else {
    // Disabled: no change flags available. Compare current vs previous row.
    // Build wire_bits_ by value comparison per column.
    // (Acknowledged: slower than Enabled path — user accepted this tradeoff.)
    wire_bits_.reset();
    for (size_t i = 0; i < layout_->columnCount(); ++i) {
        auto type = layout_->columnType(i);
        if (type == ColumnType::BOOL) {
            wire_bits_.set(i, row.bits_[layout_->columnOffset(i)]);
        } else {
            wire_bits_.set(i, columnChanged(row, prev_row_, i, type));
        }
    }
}
```

### 7.5 Flat Codec + Enabled TrackingPolicy

When Reader opens a flat file with `Policy::Enabled`, after `Flat001::deserialize()` the code
sets change flags. Initial strategy: mark all columns as changed (simple, correct):

```cpp
// In CodecDispatch's flat deserialize thunk, or as post-deserialize hook:
if constexpr (isTrackingEnabled(Policy)) {
    row.setChanges();  // marks all non-BOOL columns as changed
}
```

Future refinement: per-column comparison for precise change detection (compare current row
vs. buffered previous row). This is an optimization, not required for correctness.

### 7.6 Scope of Changes

| File | Change |
|---|---|
| **New: `row_codec_dispatch.h`** | `CodecDispatch` class: union storage + function pointers + `selectCodec()` |
| **`row_codec_zoh001.h`** | Add internal `Bitset<>`/`Bitset<N>` wire change header member |
| **`row_codec_zoh001.hpp`** | Remove `static_assert(isTrackingEnabled)`. Add `if constexpr` translation in serialize/deserialize. Use `wire_bits_` instead of direct `row.bits_` alias. |
| **`reader.h`** | Replace `RowCodecType<...> codec_` → `CodecDispatch<...> codec_` |
| **`reader.hpp`** | `readFileHeader()`: call `codec_.selectCodec(file_header_.flags(), layout)`. Remove `if constexpr` TrackingPolicy/ZoH validation block. |
| **`writer.h`** | **No change** — keeps `RowCodecType` (compile-time selection; Writer knows what it writes) |
| **`writer.hpp`** | **No change** |
| **`row_codec_variant.h`** | Retained — provides `RowCodecType` alias for Writer |
| **`row_codec_flat001.h/.hpp`** | Minor: Enabled path may need post-deserialize `setChanges()` hook |
| **`row.h` / `row.hpp`** | **No change** — bits_/changes_ sizing unchanged |
| **RowView / RowViewStatic** | **No change** — always flat-only, unchanged |
| **Tests** | Add cross-combination tests: write Flat→read Enabled, write ZoH→read Disabled, etc. |
| **ARCHITECTURE.md / SKILLS.md** | Document orthogonal axes and dispatch |

### 7.7 Implementation Steps

| Step | Description | Verification |
|------|-------------|--------------|
| **7a** | Add internal `wire_bits_` to ZoH codec. Refactor serialize/deserialize to use `wire_bits_` instead of direct `row.bits_` alias. Keep `static_assert` for now. | 391/391 tests pass (no behavior change) |
| **7b** | Remove `static_assert` from ZoH codec. Add `if constexpr` branches for Disabled policy in ZoH serialize/deserialize. | Build succeeds (ZoH+Disabled instantiable but not yet used) |
| **7c** | Add post-deserialize `setChanges()` hook for Flat+Enabled path. | Build succeeds |
| **7d** | Create `CodecDispatch<LayoutType, Policy>` with union storage, function pointers, `selectCodec()`. | Unit test: dispatch to Flat001 and ZoH001 |
| **7e** | Wire `CodecDispatch` into `Reader`. Remove `if constexpr` validation in `readFileHeader()`. | 391/391 tests pass (existing Flat+Disabled and ZoH+Enabled paths) |
| **7f** | Add cross-combination tests: write Flat file → read with Enabled Reader; write ZoH file → read with Disabled Reader. | New tests pass, all 391+ pass |
| **7g** | Benchmark: verify no regression vs. current direct-call dispatch. | Macro benchmark comparison within ±2% |
| **7h** | Update ARCHITECTURE.md, SKILLS.md, this plan. | Docs complete |

### 7.8 Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Codec owns wire state** | ZoH codec keeps its own change bitset (`wire_bits_`), never aliases `row.bits_` directly unless `if constexpr` confirms safe sizing match |
| **Function pointers, not virtual** | No heap allocation, no vtable. Resolved once at `selectCodec()`, predictable indirect call. |
| **Writer stays compile-time** | Producer knows the format. Runtime flexibility is a Reader concern. |
| **Union, not placement-new in byte array** | Type-safe, compiler-checked alignment. Enumerated members — acceptable for 2 codecs; revisit if >4 codecs. |
| **Flat+Enabled: "all changed" initially** | Simple and correct. Per-column comparison is a future optimization, not a correctness requirement. |
| **ZoH+Disabled serialize: compare-and-set** | Only plausible strategy — no change flags available. Acknowledged slower; this combination is unlikely in production (Writer uses Enabled for ZoH files). |
| **No file format changes** | `{version, flags}` → codec mapping remains. `selectCodec()` is the natural extension point for future format versions. |

### 7.9 Test Matrix

| Test | Writer | Reader | Validates |
|------|--------|--------|-----------|
| Flat→Disabled | `Writer<L>` | `Reader<L>` | Existing behavior (regression baseline) |
| ZoH→Enabled | `WriterZoH<L>` | `ReaderZoH<L>` | Existing behavior (regression baseline) |
| **Flat→Enabled** | `Writer<L>` | `ReaderZoH<L>` | Change flags populated after flat decode |
| **ZoH→Disabled** | `WriterZoH<L>` | `Reader<L>` | ZoH transport decode without tracking |
| Mixed repeat | `WriterZoH<L>` | `Reader<L>` | ZoH repeat rows (len=0) decoded correctly |
| RowView on ZoH file | `WriterZoH<L>` | `Reader<L>` + RowView | RowView flat per-column access on ZoH-decoded buffer |
| Static layout variants | All 4 | All 4 | LayoutStatic specializations work with dispatch |