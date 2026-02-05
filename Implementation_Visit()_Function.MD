# Implementation Plan: visit() Function for BCSV Row Classes

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

#### **Phase 1: Core visit() Implementation**
```cpp
// Row (dynamic layout)
template<typename Visitor>
void visit(Visitor&& visitor) const;

// RowStatic (static layout) - optimal version
template<typename Visitor>
void visit(Visitor&& visitor) const;

// RowView (lightweight, may skip strings)
template<typename Visitor>
void visit(Visitor&& visitor) const;

// RowViewStatic (optimal + lightweight)
template<typename Visitor>
void visit(Visitor&& visitor) const;
```

#### **Phase 2: Specialized Variants**
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

#### **Phase 3: Parallel Visit** (for point 1: 1M rows/sec target)
```cpp
// Visit columns in parallel (SIMD/threading)
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

### Phase 1: RowStatic (Core)
- [ ] Add `visit()` method to row.h
- [ ] Implement in row.hpp using fold expressions
- [ ] Write basic test (iterate all columns)
- [ ] Benchmark vs. manual get() loop

### Phase 2: Other Row Types
- [ ] Implement Row::visit() (runtime dispatch)
- [ ] Implement RowViewStatic::visit()
- [ ] Implement RowView::visit()
- [ ] Add tests for all types

### Phase 3: Specialized Variants (Optional)
- [ ] visitChanged() for ZoH use case
- [ ] visitUntil() for early termination
- [ ] visitRange() for partial row operations

### Phase 4: Integration & Documentation
- [ ] Update examples to demonstrate visit()
- [ ] Add API documentation
- [ ] Performance comparison benchmarks
- [ ] Update CLI tools to use visit() where beneficial
