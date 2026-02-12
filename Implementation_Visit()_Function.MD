# Implementation Plan: visit() Function for BCSV Row Classes

**Status: ✅ COMPLETED (February 2026)**

## Analysis: visit() Function Benefits for BCSV

### **1. Context & BCSV Architecture**

Currently, BCSV has four row types:
- `Row` - Dynamic layout (runtime-defined)
- `RowStatic<ColumnTypes...>` - Static layout (compile-time defined)
- `RowView` - Zero-copy view into serialized buffer
- `RowViewStatic<ColumnTypes...>` - Zero-copy view with static layout

Current access patterns use:
- `get<T>(index)` / `set<T>(index, value)` - Type-based access
- Template index access for static: `get<Index>()`
- Vectorized operations for multiple columns

### **2. Benefits of visit() for BCSV**

#### **A. Performance Benefits**
1. **Zero Virtual Dispatch**: Visit uses compile-time polymorphism via templates, avoiding vtable lookups
2. **Optimal Inlining**: Visitor lambdas can be fully inlined by compiler, eliminating function call overhead
3. **Cache Locality**: Single pass through row data without repeated bounds checks
4. **Branch Prediction**: Single switch/jump table vs. multiple conditional branches per column
5. **SIMD Opportunities**: Enables auto-vectorization for operations on multiple columns

#### **B. Type Safety Benefits**
1. **Exhaustive Handling**: Compiler ensures all column types are handled
2. **Compile-Time Errors**: Type mismatches caught at compile time, not runtime
3. **No Runtime Type Checks**: Eliminates repeated `columnType()` queries
4. **Generic Algorithms**: Write once, works for any column type combination

#### **C. Code Quality Benefits**
1. **Cleaner Code**: Eliminates verbose switch/if-else chains
2. **DRY Principle**: Single visitor handles all types uniformly
3. **Composability**: Visitors can be combined and reused
4. **Maintainability**: Adding new column types updates one place

### **3. BCSV-Specific Use Cases**

#### **Use Case 1: Fast Serialization/Deserialization**
```cpp
// Current: Multiple get() calls with type switches
// Proposed: Single visit pass
row.visit([&](auto index, const auto& value) {
    serialize(buffer, index, value);
});
```

#### **Use Case 2: CSV Conversion** (csv2bcsv, bcsv2csv tools)
```cpp
row.visit([&](auto index, const auto& value) {
    csvStream << (index > 0 ? "," : "") << value;
});
```

#### **Use Case 3: ZoH Comparison** (Zero-Order-Hold compression)
```cpp
bool unchanged = row1.visit([&](auto index, const auto& value1) {
    return value1 == row2.template get<decltype(value1)>(index);
});
```

#### **Use Case 4: Statistics/Aggregation** (for point 10: statistics in packet headers)
```cpp
struct Stats { double min, max, sum; size_t count; };
std::vector<Stats> columnStats(columnCount);

row.visit([&](auto index, const auto& value) {
    if constexpr (std::is_arithmetic_v<decltype(value)>) {
        auto& stat = columnStats[index];
        stat.min = std::min(stat.min, static_cast<double>(value));
        stat.max = std::max(stat.max, static_cast<double>(value));
        stat.sum += value;
        stat.count++;
    }
});
```

#### **Use Case 5: Validation** (point 15: sampler with conditions)
```cpp
bool matches = row.visit([&](auto index, const auto& value) {
    return satisfiesCondition(index, value, userConditions[index]);
});
```

### **4. Integration with BCSV Roadmap**

The visit() function directly supports:

- **Point 8**: Benchmarks - Enables faster comparison code in benchmark_large
- **Point 10**: Statistics - Fast computation of min/max/mean per packet
- **Point 12**: Row encoding - Single-pass analysis for VLE decisions
- **Point 13**: Dictionary encoding - Fast string cardinality analysis
- **Point 15**: Sampler - Efficient condition checking across columns
- **Point 16**: CLI tools - Fast operations in bcsv2csv, bcsvInspect

### **5. Implementation Strategy**

#### **Design Decisions (Based on Analysis)**

**Visitor Signature:** `visitor(index, value)`
- Provides both column index and value to visitor
- Covers 90% of BCSV use cases (column-aware operations)
- Users can ignore index if not needed: `[](auto /*index*/, const auto& value)`

**Access Pattern:** Read-only (const) only
- Covers all current roadmap needs (serialization, CSV, stats, ZoH comparison)
- Mutable visit deferred to future phase if needed
- Explicit set() API remains available for mutations

**Return Type:** void
- Simple, clear semantics - always processes all columns
- Matches std::for_each and common visitor patterns
- Early termination variants (visitUntil) deferred to Phase 2

#### **Phase 1: Core visit() Implementation**
```cpp
// Row (dynamic layout) - runtime type dispatch
template<typename Visitor>
void visit(Visitor&& visitor) const;

// RowStatic (static layout) - compile-time fold expressions
template<typename Visitor>
void visit(Visitor&& visitor) const;

// RowView (zero-copy buffer view)
template<typename Visitor>
void visit(Visitor&& visitor) const;

// RowViewStatic (optimal compile-time + zero-copy)
template<typename Visitor>
void visit(Visitor&& visitor) const;
```

**Visitor Contract:**
- Called once per column in layout order (0 to N-1)
- Receives: `(size_t index, const T& value)` or `(compile_time_index, const T& value)` for static
- Must not throw (or handle exceptions internally)
- Return value ignored (void)

#### **Phase 2: Specialized Variants (Future)**
```cpp
// Visit only changed columns (for ZoH)
template<typename Visitor>
void visitChanged(Visitor&& visitor) const;

// Visit with early termination (for validation)
template<typename Visitor>
bool visitUntil(Visitor&& visitor) const;

// Visit specific column range
template<typename Visitor>
void visitRange(size_t start, size_t end, Visitor&& visitor) const;
```

#### **Phase 3: Advanced Features (Future)**
```cpp
// Mutable visit (requires change tracking considerations)
template<typename Visitor>
void visit(Visitor&& visitor);  // non-const overload

// Parallel visit (for point 1: 1M rows/sec target)
template<typename Visitor>
void visitParallel(Visitor&& visitor, size_t threadCount = 0) const;
```

### **6. Performance Impact Estimate**

Based on BCSV's design goals:
- **Sequential write**: visit() could improve ZoH detection by 2-3x (single pass vs. multiple get() calls)
- **CSV conversion**: 20-40% faster (eliminate type dispatch overhead)
- **Statistics**: 3-5x faster (single pass, better cache locality)
- **Memory bandwidth**: Better utilization (sequential access pattern)

### **7. Compatibility & Migration**

**Advantages:**
- Non-breaking change (new API alongside existing)
- Gradual adoption possible
- Existing code continues to work
- Python/C# bindings can expose simplified visit() via callbacks

**Implementation Cost:**
- Low - Template-based, no ABI changes
- Reuses existing infrastructure (tuple iteration for RowStatic)
- Already have jump table patterns in place (see `get()` implementations)

### **Recommendation**

**Priority: HIGH** - Implement visit() before point 12 (new row encoding) as it will be critical for:
1. Analyzing compression opportunities in single pass
2. Benchmarking different encoding strategies efficiently
3. Enabling fast statistics computation for packet headers
4. Supporting the sampler/filter API efficiently

The visit() pattern aligns perfectly with BCSV's performance-first philosophy and time-series data focus.

---

## Complexity Assessment & Time Estimate

### Complexity: **LOW to MEDIUM**

#### Why It's Relatively Simple:

**1. Infrastructure Already Exists**
- RowStatic already uses compile-time iteration with fold expressions (see line 1350+ in row.hpp)
- Jump table pattern already implemented in `get()` methods (lines 1484-1523)
- Template metaprogramming patterns well-established
- std::tuple iteration for RowStatic is working

**2. Pattern Reuse**
The visit() implementation can directly reuse the existing compile-time dispatch patterns:
```cpp
// Similar to existing get() jump table pattern
template<typename Visitor>
void visit(Visitor&& visitor) const {
    [&]<size_t... I>(std::index_sequence<I...>) {
        (visitor(I, std::get<I>(data_)), ...);  // fold expression
    }(std::make_index_sequence<column_count>{});
}
```

### Time Estimate:

#### **Core Implementation: 3-5 hours**
- RowStatic::visit() - 1 hour (straightforward tuple iteration)
- RowViewStatic::visit() - 1 hour (similar pattern)
- Row::visit() - 1.5 hours (runtime dispatch, more complex)
- RowView::visit() - 1.5 hours (buffer offset handling)

#### **Testing: 2-3 hours**
- Basic functionality tests (all 4 types)
- Type correctness tests
- Performance microbenchmarks
- Edge cases (empty rows, strings, etc.)

#### **Documentation: 1-2 hours**
- API documentation
- Usage examples (2-3 realistic cases)
- Update existing examples to show visit() where appropriate

#### **Total: 6-10 hours** → **~1-1.5 working days**

### Risk Factors (could add time):

1. **String Handling in RowView** (+1-2 hours)
   - StringAddr decoding might need special handling
   - Buffer boundary checks for variable section

2. **Performance Tuning** (+2-4 hours if needed)
   - If initial implementation doesn't beat current get() loops
   - Might need compiler hints, `[[likely]]`, or reordering

3. **Visitor Signature Design** (+1-2 hours)
   - Deciding exact signature (index first? value first? both?)
   - Const/non-const variants
   - Return value handling

### Recommended Approach:

**Phase 1 (2-3 hours):** Start with RowStatic - it's cleanest and validates the API design  
**Phase 2 (2-3 hours):** Add Row, RowViewStatic  
**Phase 3 (2-3 hours):** Add RowView, write tests  
**Phase 4 (1-2 hours):** Benchmarks and documentation

### Confidence Level: **HIGH**

The patterns already exist in the codebase. This is mostly about extracting and generalizing what's already been done in the get() implementations. The template machinery is already there and working.

**Bottom line: 1 focused day if everything goes smoothly, 2 days if you hit edge cases or want to add the specialized variants (visitChanged, visitRange, etc.).**

---

## Implementation Checklist

### Phase 1: Core visit() - Read-only, void return ✅ COMPLETED
- [x] Add `visit()` declaration to row.h for all 4 types
- [x] Implement RowStatic::visit() (compile-time fold expressions)
- [x] Implement Row::visit() (runtime dispatch)
- [x] Implement RowViewStatic::visit() (compile-time with buffer)
- [x] Implement RowView::visit() (runtime with buffer)
- [x] Write basic tests (iterate all columns, verify values)
- [x] All tests passing (24 visit tests + 266 other = 290 total)

**Implementation Summary:**
- **RowStatic**: Uses compile-time fold expressions over tuple - optimal zero-overhead iteration
- **Row**: Uses runtime switch/case dispatch based on ColumnType - consistent with existing get() patterns
- **RowViewStatic**: Uses compile-time fold with get<I>() - zero-copy buffer access
- **RowView**: Uses runtime switch with buffer pointer arithmetic and StringAddr decoding for strings

### Phase 1.5: Mutable visit() with Change Tracking ✅ COMPLETED (Beyond Original Plan)
- [x] Implement mutable visit() for Row (non-const overload)
- [x] Add fine-grained change tracking with optional `bool& changed` parameter
- [x] Support both 2-param (legacy) and 3-param (fine-grained) visitor signatures
- [x] Implement compile-time signature detection using std::is_invocable_v
- [x] Extend to RowStatic with fold expressions checking each column independently
- [x] Add mutable visit() for RowView (primitives only, strings read-only)
- [x] Add mutable visit() for RowViewStatic
- [x] Tests for mutable access, change tracking, and backward compatibility
- [x] Conservative default: `bool changed = true` for safety

**Change Tracking Design:**
- **3-parameter signature**: `(size_t index, T& value, bool& changed)` - visitor controls per-column marking
- **2-parameter signature**: `(size_t index, T& value)` - all visited columns marked as changed
- **Detection**: Per-column type-based using `std::is_invocable_v` at compile time
- **Heterogeneous visitors**: Supports mixing 2-param and 3-param for different column types
- **Default**: `changed = true` before invoking visitor - conservative, prevents ZoH data loss

### Phase 1.6: Supporting Infrastructure ✅ COMPLETED
- [x] Create row_visitors.h header with concepts and helpers
- [x] Define C++20 concepts: RowReadOnlyVisitor, RowMutableVisitor, RowMutableVisitorWithTracking
- [x] Add helper types: overload, csv_visitor, stats_visitor, type_filter, conditional
- [x] Add bitset reference compound assignment operators (|=, &=, ^=) for cleaner change tracking
- [x] Comprehensive documentation in row_visitors.h with examples
- [x] Create visitor_examples.cpp demonstrating common patterns
- [x] Organize documentation: brief comments in row.h, detailed documentation in row.hpp
- [x] Remove redundant type traits from detail:: namespace (simplified to 292 lines)

### Phase 2: Specialized Variants (Future - Not Yet Implemented)
- [ ] visitChanged() for ZoH use case - visit only changed columns
- [ ] visitUntil() for early termination - short-circuit on predicate
- [ ] visitRange() for partial row operations - visit subset of columns

### Phase 3: Advanced Features (Future - Not Yet Implemented)
- [ ] visitParallel() for multi-threaded row processing

### Phase 4: Integration & Documentation ✅ PARTIALLY COMPLETED
- [x] Create visitor_examples.cpp with 8 comprehensive examples
- [x] Add API documentation in row.h (brief) and row.hpp (detailed)
- [x] Add row_visitors.h with concepts, helpers, and usage patterns
- [ ] Performance comparison benchmarks (deferred)
- [ ] Update CLI tools (bcsv2csv, etc.) to use visit() where beneficial (deferred)

---

## ✅ Implementation Complete - Final Summary

### What Was Delivered

**Core Implementation (Beyond Original Plan):**
1. ✅ **Read-only visit()** for all 4 row types - Single-pass iteration over columns
2. ✅ **Mutable visit()** for all 4 row types - Direct value modification with change tracking
3. ✅ **Fine-grained change tracking** - Optional 3rd parameter `bool& changed` for per-column control
4. ✅ **Backward compatible** - Supports both 2-param and 3-param visitor signatures
5. ✅ **Compile-time optimization** - RowStatic uses fold expressions, zero overhead
6. ✅ **Zero-copy views** - RowView/RowViewStatic work on serialized buffers without deserialization

**Supporting Infrastructure:**
1. ✅ **row_visitors.h** - C++20 concepts, helper types (overload, csv_visitor, stats_visitor), examples
2. ✅ **Bitset enhancements** - Added compound assignment operators (|=, &=, ^=) to reference proxy
3. ✅ **Documentation reorganization** - Brief API docs in .h files, detailed in .hpp
4. ✅ **Comprehensive examples** - visitor_examples.cpp with 8 real-world patterns
5. ✅ **24 visit tests** - Coverage for all types, signatures, edge cases, change tracking

**Test Results:**
- All 290 tests passing (24 visit-specific + 266 existing)
- No performance regressions
- Zero compiler warnings

### Time Investment

**Actual Time:** ~2.5 days (vs. 1-1.5 days estimated)

**Time Breakdown:**
- Phase 1 (Read-only): ~4 hours (as estimated)
- Phase 1.5 (Mutable + tracking): ~8 hours (unplanned, complex design decisions)
- Phase 1.6 (Infrastructure): ~4 hours (concepts, helpers, bitset operators)
- Testing: ~4 hours (comprehensive coverage beyond basic tests)
- Documentation: ~3 hours (row_visitors.h, examples, reorganization)

**Why Longer:**
- Mutable visit() was in original Phase 3 but implemented immediately
- Fine-grained change tracking design required iteration and discussion
- Concepts/helpers infrastructure not in original plan
- Documentation reorganization (brief vs. detailed) emerged during implementation

### Key Learnings

#### 1. **API Design Decisions**

**✅ What Worked:**
- **Conservative defaults** (`changed = true`) - Prevents silent data loss in ZoH compression
- **Compile-time signature detection** - std::is_invocable_v allows heterogeneous visitors
- **Per-column type checking** - Enables mixing 2-param and 3-param in same visitor
- **Visitor signature** `(index, value)` - Natural, covers 95% of use cases
- **Documentation organization** - Brief in .h (API), detailed in .hpp (implementation notes)

**❌ What Didn't Work:**
- **Type traits in detail:: namespace** - Redundant with std::is_invocable_v, removed
- **Concept constraints on templates** - Over-constrains API, prevents valid use cases
- **Overload helper with specific types** - Too brittle across platforms, simplified to if constexpr

**💡 Design Insights:**
- Concepts are valuable for **documentation**, not necessarily for **template constraints**
- Conservative defaults > Optimization when data correctness is at stake
- Compile-time signature detection enables powerful backward compatibility
- The visitor pattern's strength is **type-agnostic processing**, not type-specific dispatch

#### 2. **Change Tracking Complexity**

**Challenge:** How to mark columns as changed during mutable visit()?

**Solutions Explored:**
1. ❌ Mark all columns changed automatically - Too conservative for ZoH
2. ❌ Require visitor to call changes_.set(i) - Error-prone, easy to forget
3. ✅ Optional bool& changed parameter - Gives control but defaults safe

**Implementation Details:**
- Default `changed = true` before invoking visitor
- Visitor can set to `false` to opt-out of marking
- Uses `changes_[i] |= changed` for clean compound assignment
- Required adding |=, &=, ^= operators to bitset::reference

**Edge Cases:**
- Heterogeneous visitors (some columns 2-param, others 3-param) - Handled per-column
- RowView accepts but ignores `changed` - API consistency vs. actual behavior
- Lazy programmers - Conservative default protects data integrity

#### 3. **C++20 Concepts**

**What We Learned:**
- Concepts are **excellent for documentation** - Clear requirements, shows intent
- Concepts are **good for error messages** - Reference named concept in SFINAE failures
- Concepts can **over-constrain** - `typename Visitor` often better for flexibility
- Type traits can be **redundant** - std::is_invocable_v does the job directly

**Best Practice Emerged:**
- Define concepts in separate header (row_visitors.h) for discoverability
- Document with concepts, implement with typename
- Avoid intermediate type trait layers unless adding value

#### 4. **Code Organization**

**Documentation Strategy:**
- **row.h** - Brief comments (3-5 lines), signature, cross-references
- **row.hpp** - Detailed documentation, examples, behavior notes
- **row_visitors.h** - Concepts, examples, helper types

**Why This Works:**
- Users browse .h files for API overview
- Implementation details in .hpp for deep dive
- Helpers in separate file for optional inclusion (not forced via row.h)

#### 5. **Bitset Enhancement**

**Problem:** Change tracking code with compound assignment is cleaner:
```cpp
changes_[i] |= changed;  // Clean
// vs
if (changed) changes_.set(i);  // Verbose
```

**Solution:** Add compound assignment operators to bitset::reference
- Matches std::bitset API
- ~15 lines of code
- Significant readability improvement

**Lesson:** Small API improvements have outsized impact on code clarity

### What's Missing (Deferred Features)

**Phase 2: Specialized Variants**
- `visitChanged()` - Visit only changed columns (ZoH optimization)
- `visitUntil()` - Early termination on predicate
- `visitRange()` - Visit subset of columns

**Rationale for Deferral:**
- No immediate use case in roadmap
- Core visit() covers 95% of needs
- Can be added incrementally without breaking changes
- Better to validate core pattern first

**Phase 3: Parallel Iteration**
- `visitParallel()` - Multi-threaded row processing

**Rationale for Deferral:**
- Requires careful resource management
- Synchronization overhead may negate benefits for small rows
- Best implemented when we have real performance data

**Phase 4: Integration**
- Performance benchmarks comparing visit() vs. get() loops
- Update CLI tools (bcsv2csv, bcsvInspect) to use visit()

**Rationale for Deferral:**
- Examples demonstrate patterns sufficiently
- Tools work correctly with current implementation
- Benchmarks can be misleading without real-world workloads
- Can be done incrementally as tools evolve

### Success Metrics

**Functionality:** ✅
- All 4 row types support visit()
- Both read-only and mutable access
- Change tracking works correctly
- Zero-copy views maintain zero-copy semantics

**Performance:** ✅ (No regressions observed)
- No slowdown in existing tests
- Compile times unchanged
- Binary size impact minimal

**API Quality:** ✅
- Clear, self-documenting signatures
- Comprehensive examples
- Backward compatible (new API, existing code unchanged)
- Extensible (future variants possible)

**Code Quality:** ✅
- 290 tests passing
- No compiler warnings
- Clean implementation reusing existing patterns
- Well-documented, maintainable

### Recommendations for Future Work

1. **Monitor Usage Patterns**
   - See which visitor patterns users adopt
   - Identify missing helper types
   - Consider specialized variants if usage shows need

2. **Performance Validation**
   - Benchmark visit() vs. get() in real workloads
   - Measure ZoH compression improvement with fine-grained tracking
   - Profile CPU cache behavior

3. **CLI Tool Integration**
   - Update bcsv2csv to use visitors::csv_visitor
   - Add bcsvInspect command to show row contents via visit()
   - Benchmark before/after to validate performance claims

4. **Python/C# Bindings**
   - Expose visit() via callback interface
   - Allow scripting languages to iterate rows efficiently
   - Consider simplified 1-parameter signature `visitor(value)` for scripting

5. **Documentation**
   - Add cookbook section to docs with common visitor patterns
   - Document performance characteristics (when to use visit vs. get)
   - Add migration guide for existing code

### Conclusion

The visit() function implementation exceeded the original scope by delivering mutable access and fine-grained change tracking in Phase 1, features originally planned for Phase 3. The investment in supporting infrastructure (concepts, helpers, bitset operators) was not originally planned but proved valuable for API usability and code clarity.

**Key Takeaway:** The visitor pattern is now a first-class citizen in BCSV's API, providing an efficient, type-safe, and ergonomic way to process row data. The implementation aligns perfectly with BCSV's performance-first philosophy while maintaining code clarity and composability.

**Status:** Ready for production use. Feature complete for current roadmap needs. Future enhancements can be added incrementally without breaking changes.

---

**Document Status:** CLOSED - Implementation complete, lessons captured, recommendations documented.  
**Date Closed:** February 7, 2026

---

## Phase 1.7: Typed visit<T>() and visitConst<T>() — Compile-Time Dispatch ✅ COMPLETED

**Date:** February 2026 (addendum)

### Motivation

The untyped `visit()` uses a runtime `switch(ColumnType)` to dispatch each column to the visitor.
For **homogeneous column ranges** (e.g., 100 consecutive double columns), this runtime switch is
pure overhead — the type is known at call time. Micro-benchmarks showed the runtime switch adds
a consistent **~2x cost** vs. compile-time dispatch for bulk column access.

### API

```cpp
// Typed mutable visit — compile-time dispatch, no runtime switch
template<typename T, typename Visitor>
    requires TypedRowVisitor<Visitor, T>
void visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

// Typed read-only visit — compile-time dispatch, zero-copy
template<typename T, typename Visitor>
    requires TypedRowVisitorConst<Visitor, T>
void visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;
```

**New concepts** (defined in row.h):
- `TypedRowVisitor<V, T>`: requires `(size_t, T&, bool&)` or `(size_t, T&)`
- `TypedRowVisitorConst<V, T>`: requires `(size_t, const T&)`

### Implementation Details

Uses `if constexpr` for compile-time type dispatch into three branches:
1. **bool** → materialize from `bits_[]`, call visitor, write back
2. **string** → direct `strg_[]` reference, with truncation and tracking
3. **scalar** → `reinterpret_cast<T*>(&data_[offset])`

Runtime type checking (when `RANGE_CHECKING` enabled) validates all columns
in the range match the expected type T before iterating.

**Key design insight:** `std::is_convertible_v<std::string_view, std::string>` is **false** in
C++17/20 because `std::string`'s constructor from `string_view` is `explicit`. The string
branch uses `detail::is_string_like_v<T>` which correctly covers both `std::string` and
`std::string_view`.

### set<T>() Refactoring

As part of this work, `set<T>()` was refactored from routing through `visit()` (runtime switch)
to using the same direct `if constexpr` dispatch. This eliminates the visitor overhead for the
most common single-column write operation.

### Benchmark Results

**Multi-column benchmark** (100 double columns, 100K iterations = 10M accesses, -O3):

| Method | Time (ms) | Notes |
|---|---|---|
| `visit()` | ~12 | Runtime switch per column |
| `visit<T>()` | ~5.7 | **2x faster** — no switch |
| `visitConst<T>()` | ~5.2 | Read-only, slightly faster |
| `ref<T>()` loop | ~4.6 | Manual per-column, no visitor overhead |
| `set<T>()` loop | ~4.6 | Refactored to direct dispatch |

At -O2, `visit<T>()` (5.1ms) is faster than even the `ref<T>()` loop (6.7ms) because
the single call with internal iteration avoids per-column function call overhead.

### Tests Added

10 new tests in visit_test.cpp (total: 315 tests):
- TypedVisitScalarReadWrite, TypedVisitConstReadOnly, TypedVisitSingleColumn
- TypedVisitBool, TypedVisitString, TypedVisitWithChangeTracking
- TypedVisitTypeMismatchThrows, TypedVisitRangeOutOfBoundsThrows
- TypedVisitZeroCount, TypedVisitTwoParamVisitor

### Other Row Classes — Analysis

`visit<T>()`/`visitConst<T>()` is implemented on **RowImpl** and **RowView**.

| Row Class | Layout | Ownership | Runtime Dispatch | visit<T> Benefit | Status |
|---|---|---|---|---|---|
| **RowImpl** | Runtime | Owning | 12-branch switch | ✅ High — eliminates switch | **DONE** |
| **RowView** | Runtime | Non-owning | 12-branch switch | ✅ High — same switch, API consistency | **DONE** |
| **RowStaticImpl** | Compile-time | Owning | Fold expr `((I==i ? ... : false) \|\| ...)` | ❌ Low — types already known at compile time | Not needed |
| **RowViewStatic** | Compile-time | Non-owning | Fold expr (same pattern) | ❌ Low — types already known at compile time | Not needed |

#### RowView — Implementation Notes

**RowView** has the same 12-branch `switch(ColumnType)` dispatch as RowImpl. Key design
differences from RowImpl:

- **Strings**: Uses `std::string_view` (not `std::string&`) since RowView doesn't own
  string storage. The buffer holds `StringAddr` records decoded to `string_view`. Both
  `visit<std::string_view>` and `visit<std::string>` are accepted — the latter creates
  a temporary `std::string` from the buffer's `string_view`.
- **Mutability**: Scalars and bools are mutable in-place (direct `reinterpret_cast` into
  buffer). Strings are read-only in both mutable and const visit (buffer cannot resize).
- **Change tracking**: RowView has no tracking infrastructure. The `bool& changed` parameter
  is accepted but ignored (consistent with current untyped visit behavior).
- **Performance**: At -O3, RowView's runtime switch is very well predicted for homogeneous
  column ranges, so `visit<T>()` shows comparable speed (~5-8ms for 10M accesses). At -O2,
  `visit<T>()` is 1.24x faster for mutable access. The primary value is API consistency with
  RowImpl and type safety (compile-time type enforcement).

**12 new tests** added: RowViewTypedVisitConstScalar, RowViewTypedVisitConstDouble,
RowViewTypedVisitConstString, RowViewTypedVisitMutableScalar, RowViewTypedVisitMutableDouble,
RowViewTypedVisitMutableBool, RowViewTypedVisitSingleColumn, RowViewTypedVisitTypeMismatchThrows,
RowViewTypedVisitRangeOutOfBoundsThrows, RowViewTypedVisitZeroCount,
RowViewTypedVisitStringReadOnly, RowViewTypedVisitRoundtrip.

Total test count: 327 (315 + 12 new RowView typed visit tests).

#### RowStaticImpl & RowViewStatic — Not Needed

These classes use compile-time fold expressions that already know each column's type at each
index I. There is no `ColumnType` runtime switch to eliminate. The minor overhead is the
`((I == i ? ... : false) || ...)` linear scan to find the matching index, but this is an
O(N) scan over a small, known-at-compile-time N, and the compiler typically optimizes it.
Adding `visit<T>()` would only add API surface without meaningful performance benefit.
