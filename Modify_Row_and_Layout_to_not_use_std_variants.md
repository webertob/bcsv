# Refactoring Plan: Removing std::variant from Row and Layout

## 1. Situation Analysis
Currently, the `bcsv::Row` class uses `std::vector<std::variant<...>>` to store runtime-flexible column data. While flexible, this approach introduces significant performance and architectural drawbacks:
- **Memory Optimization:** `std::variant` forces every cell to be as large as the largest possible type (typically ~32-40 bytes if `std::string` is involved), causing massive memory fragmentation and poor cache locality.
- **Runtime Overhead:** Accessing values requires `std::visit` or index checks, adding branch overhead to every cell operation.
- **Compute Efficiency:** Interpolation, math operations, and comparisons cannot exploit SIMD or simple pointer arithmetic effectively due to the variant indirection.

## 2. Problem Statement
The current architecture limits high-performance compute scenarios (e.g., signal processing, heavy interpolation) and creates a disconnect between the in-memory representation and the efficient on-disk serialization format. We need a way to store mixed-type row data that is CPU-cache friendly, type-safe, and serialization-ready.

## 3. Goals
1.  **Maximize Compute Performance:** Enable O(1) access to column data with direct pointer arithmetic (no variant dispatch).
2.  **Improve Cache Locality:** Store primitive values in a contiguous memory block.
3.  **Maintain Flexibility:** Continue supporting runtime-defined layouts (Flexible mode) and compile-time layouts (Static mode).
4.  **Optimize Serialization:** Clarify the distinction between the "Host" (aligned, fast access) and "Wire" (packed, dense storage) formats.

## 4. Constraints & Requirements
-   **Alignment:** Host memory must respect natural alignment (e.g., `double` on 8-byte boundaries) for maximum CPU efficiency.
-   **Wire Density:** On-disk format must remain packed (no padding) when `FileFlag::raw` is set.
-   **String Handling:** String mutations must not invalidate pointers to primitive data or require massive buffer shifts.
-   **Compatibility:** `Row` (runtime) and `RowStatic` (compile-time) must share compatible behavior.
-   **Embedded Friendly:** The Wire format must be consumable by restricted devices (MCUs) using packed structs or direct views (`RowView`).

## 5. Architectural Decisions

### A. Two-Domain Memory Layout
We formally distinguish between two data representations:
1.  **Host Domain (In-Memory):**
    -   **Usage:** `bcsv::Row`, `bcsv::RowStatic`
    -   **Layout:** Natural alignment (padding inserted).
    -   **Storage:** Primitives in a flat `std::vector<std::byte>`; Strings managed separately to allow resizing.
2.  **Wire Domain (On-Disk/Network):**
    -   **Usage:** `bcsv::RowView`, Serialization Output
    -   **Layout:** Packed (byte-aligned, no padding).
    -   **Storage:** Dense byte stream.

### B. "Option 1" Storage Strategy for Host Row
To balance performance with editability, `bcsv::Row` will use a hybrid storage approach:
-   **Fixed Buffer:** `std::vector<std::byte>` stores all primitive values and *indices* for string columns.
-   **Variable Buffer:** `std::vector<std::string>` stores the actual string content.
-   **Mechanism:** Accessing a string involves reading an index from the Fixed Buffer and look it up in the Variable Buffer. Accessing primitives is a direct `reinterpret_cast` using pre-calculated aligned offsets.

### C. Storage Strategy for RowStatic (Compile-Time)
For `bcsv::RowStatic`, we will **retain the existing `std::tuple` implementation**.
-   **Reasoning:** `std::tuple` provides compiler-optimized, aligned storage with O(1) access and type safety inherently. It avoids the runtime overhead of calculating offsets manually and perfectly serves the "Host Layout" role.
-   **Offsets:** While we don't need manual offsets for *Host* access (the compiler handles `std::get`), we **must** add logic to `LayoutStatic` to calculate **Packed Wire Offsets** to support correct serialization and `RowView` access.

### D. Serialization Stability
We explicitly choose **not** to change the Wire Format (serialization output) during this refactoring phase.
-   **Standard Serialization:** Packed binary format (fixed section + variable section).
-   **ZoH (Zero-Order-Hold):** Bitset-based compression where only changed values are written.
-   **Plan:** The `serializeTo` and `deserializeFrom` methods will be updated to bridge the new internal `Row` memory structure to the *existing* Wire Format. The on-disk binary compatibility remains unchanged.

### E. Layout Responsibility
The `Layout` class becomes the source of truth for offset calculations. It will compute and cache:
-   `host_offsets`: Aligned offsets for in-memory access (Dynamic Row only).
-   `wire_offsets`: Packed offsets for serialization/deserialization (Both Dynamic and Static).

## 6. Implementation Plan

### Step 1: Refactor Layout Class
-   Modify `Layout` to calculate `host_offsets_` using `alignof(T)` and `std::align`.
-   Modify `Layout` to calculate `wire_offsets_` (packed accumulation).
-   Add accessors `getHostOffset(i)` and `getWireOffset(i)`.

### Step 2: Refactor LayoutStatic Class
-   Add static `constexpr` calculation for `wire_offsets` (packed) to complement the existing offsets.
-   Ensure `RowViewStatic` can access these packed offsets.

### Step 3: Refactor Row Class
-   Remove `std::vector<ValueType>`.
-   Add `std::vector<std::byte> fixed_data_`.
-   Add `std::vector<std::string> string_data_`.
-   Re-implement `get<T>(i)` and `set<T>(i, val)` to use raw pointer arithmetic based on `layout_.getHostOffset(i)`.
-   Implement special handling for `std::string` (store index in fixed buffer, value in string vector).

### Step 4: Update Row Serialization
-   Update `serializeTo` (Standard): Iterate columns, read from aligned Host memory, write to Packed Wire format.
-   Update `serializeToZoH`: Ensure change tracking logic maps correctly to the new memory layout.
-   Update `deserializeFrom`: Read Packed Wire data, write to Aligned Host memory.

### Step 5: Update RowStatic Serialization
-   Update `serializeTo`: Iterate tuple elements, write to Packed Wire format (using calculated Wire offsets for structure if needed, or simple type iteration).
-   Update `LayoutStatic::wire_offsets` usage in `RowViewStatic`.

### Step 6: Verify & Test
-   Update unit tests (`RowTest`, `LayoutTest`) to reflect internal structural changes.
-   Run benchmarks to confirm performance improvements in interpolation/access patterns.
