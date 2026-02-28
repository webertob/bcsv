/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "definitions.h"
#include "layout.h"
#include "bitset.h"
#include "row_codec_flat001.h"

namespace bcsv {

/**
 * @brief BCSV Binary Row Format Documentation
 * 
 * The BCSV flat wire format uses a four-section layout that mirrors the Row's
 * in-memory storage (bits_, data_, strg_). Sections are written sequentially,
 * enabling near-memcpy serialization and efficient codec access.
 * 
 * =============================================================================
 * FLAT ROW WIRE FORMAT LAYOUT
 * =============================================================================
 * 
 * [bits_section][data_section][strg_lengths][strg_data]
 * 
 * Section 1: bits_         Bit-packed boolean values, layout order
 * Section 2: data_         Tightly packed scalars (non-bool, non-string), layout order
 * Section 3: strg_lengths  uint16_t per string column, layout order
 * Section 4: strg_data     Concatenated string payloads, layout order
 * 
 * All sections are byte-aligned (no padding between them). A section with
 * zero elements contributes zero bytes to the wire format.
 * 
 * =============================================================================
 * SECTION 1: BITS_ (Boolean values)
 * =============================================================================
 * 
 * Size: ⌈bool_count / 8⌉ bytes  (0 bytes if no bool columns)
 * 
 * Bool columns are stored as individual bits in layout order:
 *   - Bit 0 = first bool column in layout, Bit 1 = second, etc.
 *   - Within each byte: bit 0 is least significant (rightmost).
 *   - Padding bits (above bool_count) are zero-filled to byte boundary.
 * 
 * ┌─────────────────────────────────────────────────────────────────┐
 * │   Byte 0: [pad|pad|pad|bool4|bool3|bool2|bool1|bool0]         │
 * │   Byte 1: [bool15|...|bool8]  (if bool_count > 8)             │
 * └─────────────────────────────────────────────────────────────────┘
 * 
 * For Row, bits_ can be memcpy'd directly
 * (size == ⌈bool_count / 8⌉) between row and wire format.
 * 
 * =============================================================================
 * SECTION 2: DATA_ (Scalar values)
 * =============================================================================
 * 
 * Size: sum of sizeOf(type) for all non-bool, non-string columns
 * 
 * Scalars are packed tightly in layout order with NO alignment padding:
 * ┌──────────┬──────────┬──────────┬──────────┐
 * │ INT8(1B) │DOUBLE(8B)│UINT16(2B)│ FLOAT(4B)│
 * └──────────┴──────────┴──────────┴──────────┘
 * 
 * Access uses memcpy to handle misalignment safely. The in-memory Row data_
 * vector uses aligned offsets (for SIMD-friendliness), so serialize/deserialize
 * copies each scalar individually — not a single memcpy of the whole section.
 * 
 * =============================================================================
 * SECTION 3: STRG_LENGTHS (String lengths)
 * =============================================================================
 * 
 * Size: string_count × sizeof(uint16_t) bytes  (0 bytes if no string columns)
 * 
 * One uint16_t per string column in layout order, giving the byte length of
 * each string payload in section 4. Max string length: 65,535 bytes.
 * 
 * ┌──────────────────┬──────────────────┬──────────────────┐
 * │  len_0 (uint16)  │  len_1 (uint16)  │  len_2 (uint16)  │
 * └──────────────────┴──────────────────┴──────────────────┘
 * 
 * String offsets into strg_data are derived by cumulative sum of lengths;
 * no explicit offsets are stored. Codec pre-computes these during setup.
 * 
 * =============================================================================
 * SECTION 4: STRG_DATA (String payloads)
 * =============================================================================
 * 
 * Size: sum of all string lengths
 * 
 * Concatenated raw string bytes in layout order. No null terminators,
 * no length prefixes, no delimiters. Empty strings contribute 0 bytes.
 * 
 * ┌────────────────┬────────────────┬────────────────┐
 * │  "Hello" (5B)  │  "" (0B)       │  "World" (5B)  │
 * └────────────────┴────────────────┴────────────────┘
 * 
 * =============================================================================
 * EXAMPLE: Row with ["John", 25, "Engineer", 3.14, true]
 * =============================================================================
 * 
 * Layout: [STRING, INT32, STRING, DOUBLE, BOOL]
 *   bool columns:   col 4             → bool_count = 1
 *   scalar columns: col 1 (INT32), col 3 (DOUBLE) → data_size = 12
 *   string columns: col 0, col 2      → string_count = 2
 * 
 * bits_ (1 byte):    [0000000|true]           = 0x01
 * data_ (12 bytes):  [INT32: 25][DOUBLE: 3.14]
 * strg_lengths (4B): [0x0004][0x0008]
 * strg_data (12B):   "JohnEngineer"
 * 
 * Total: 1 + 12 + 4 + 12 = 29 bytes
 * 
 * =============================================================================
 * SERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Write bits_ section:
 *    - Pack bool column values into ⌈bool_count/8⌉ bytes (bit-packed, layout order)
 * 
 * 2. Write data_ section:
 *    - For each non-bool, non-string column in layout order:
 *        memcpy(buffer, &value, sizeOf(type))
 * 
 * 3. Write strg_lengths section:
 *    - For each string column in layout order:
 *        write uint16_t(min(string.length(), MAX_STRING_LENGTH))
 * 
 * 4. Write strg_data section:
 *    - For each string column in layout order:
 *        write string payload bytes (no terminator)
 * 
 * Note: Steps 3 and 4 can be combined into a single pass over string columns.
 * 
 * =============================================================================
 * DESERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Compute section boundaries from layout wire-format metadata:
 *      bits_start   = 0
 *      data_start   = wire_bits_size
 *      strg_l_start = wire_bits_size + wire_data_size
 *      strg_d_start = wire_bits_size + wire_data_size + wire_strg_count * 2
 * 
 * 2. Read bits_ section → set bool column values
 * 
 * 3. Read data_ section → set scalar column values (memcpy each)
 * 
 * 4. Read strg_lengths → read strg_data → assign string column values
 *    (cumulative sum of lengths gives each string's offset in strg_data)
 * 
 * 5. Validate buffer size at each section boundary.
 * 
 * =============================================================================
 * PERFORMANCE CHARACTERISTICS
 * =============================================================================
 * 
 * Benefits:
 * - Wire format mirrors in-memory Row storage (bits_, data_, strg_)
 * - Bool columns use 1 bit instead of 1 byte (87.5% savings per bool)
 * - No StringAddr computation — lengths stored directly, offsets derived
 * - Scalar section is contiguous — good LZ4 compression, cache-friendly
 * - String lengths section enables O(1) skip of all strings if not needed
 * - Bit-level bool access (>>3 for byte, &7 for bit)
 * 
 * Trade-offs:
 * - Bool access requires bit-level addressing (trivial shift+mask)
 * - String access requires cumulative offset computation
 * - In-memory data_ is aligned but wire data_ is packed — per-column copy
 * 
 * =============================================================================
 * MEMORY LAYOUT CONSIDERATIONS
 * =============================================================================
 * 
 * - No alignment padding in wire format (minimizes file size)
 * - All access uses memcpy for alignment safety
 * - In-memory Row data_ has natural alignment (via Layout::Data offsets_)
 * - Codec stores section start offsets + per-column section-relative offsets
 * 
 * =============================================================================
 * ZERO ORDER HOLD (ZoH) COMPRESSION FORMAT
 * =============================================================================
 * 
 * When Zero Order Hold compression is enabled (FileFlags::ZERO_ORDER_HOLD),
 * rows use a completely different encoding optimized for time-series data
 * where values remain constant for extended periods.
 * 
 * IMPORTANT: Boolean columns have special encoding in ZoH format:
 * - Boolean values are encoded directly in the head_ Bitset
 * - Bit value 0 = false, Bit value 1 = true
 * - Boolean columns never contribute data to the data section
 * - Boolean encoding is independent of whether the value changed
 * 
 * =============================================================================
 * ZoH BINARY FORMAT LAYOUT
 * =============================================================================
 * 
 * [Head Bitset] [Changed Data 1] [Changed Data 2] ... [Changed Data N]
 * |            | |              | |              |   |              |
 * |Bool Values | | Value 1      | | Value 2      |   | Value N      |
 * |+Change Flags| (if changed) | | (if changed) |   | (if changed) |
 * 
 * =============================================================================
 * HEAD BITSET FORMAT (type-grouped layout)
 * =============================================================================
 * 
 * The head Bitset has columnCount bits with a type-grouped layout:
 *
 *   Bits [0 .. boolCount):           Boolean VALUES (same layout as row.bits_)
 *   Bits [boolCount .. columnCount): Change flags, grouped by ColumnType enum:
 *       UINT8 flags | UINT16 flags | UINT32 flags | UINT64 flags |
 *       INT8 flags  | INT16 flags  | INT32 flags  | INT64 flags  |
 *       FLOAT flags | DOUBLE flags | STRING flags
 *
 * Within each type group, columns appear in their original layout order.
 *
 * This type-grouped layout enables:
 * - Bulk assignRange/equalRange for bool values (word-level operations)
 * - Tight per-type inner loops for scalar change detection (no type dispatch)
 * - head_[0..boolCount) also serves as previous-bool-value storage
 *
 * Bit Layout Example (for 6 columns: [BOOL, INT32, BOOL, FLOAT, STRING, UINT8]):
 * ┌──────┬──────┬──────┬──────┬──────┬──────┬─────┬─────┐
 * │ PAD  │ PAD  │ STR  │FLOAT │INT32 │UINT8 │BOOL1│BOOL0│
 * │  7   │  6   │  5   │  4   │  3   │  2   │  1  │  0  │
 * └──────┴──────┴──────┴──────┴──────┴──────┴─────┴─────┘
 *  ↑ padding     ↑ STRING flag  ↑ FLOAT flag ↑ bool values
 *                ↑ change flags (type-grouped: UINT8, INT32, FLOAT, STRING)
 * 
 * Bit Meanings:
 * - Bits [0..boolCount): Boolean VALUES (0=false, 1=true), always present
 * - Bits [boolCount..columnCount): Change FLAGS (1=changed, 0=unchanged)
 *   grouped by ColumnType enum order (UINT8, UINT16, ..., DOUBLE, STRING)
 * 
 * Bit Ordering: Bit 0 is the least significant bit (rightmost),
 * padding bits are added to the left (most significant bits) to reach byte boundary.
 * 
 * Bitset Size: Rounded up to nearest byte boundary for efficient storage
 * 
 * =============================================================================
 * ZoH DATA SECTION FORMAT
 * =============================================================================
 * 
 * Only non-BOOL columns with their change flag set have data in the section.
 * Data appears in the same type-grouped order as the change flags in the
 * head Bitset (ColumnType enum order: UINT8, UINT16, ..., DOUBLE, STRING).
 * 
 * For BOOLEAN columns:
 * - No data in data section (value stored directly in head Bitset)
 * - Bit 0 = false, Bit 1 = true
 * - Boolean bits represent the actual value, not change status
 * - Booleans are always included in serialization regardless of changes
 * 
 * For STRING columns (if changed):
 * ┌─────────────────┬─────────────────────────────────────────┐
 * │   Length        │           String Payload                │
 * │  (uint16_t)     │         (Length bytes)                  │
 * │  (2 bytes)      │        (no null terminator)            │
 * └─────────────────┴─────────────────────────────────────────┘
 * - Length: String length in bytes (max 65,535)
 * - Payload: Raw string bytes (UTF-8 encoded)
 * - No offset calculation needed (in-place encoding)
 * 
 * For PRIMITIVE columns (if changed):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                     Raw Value Data                              │
 * │                (1, 2, 4, or 8 bytes)                            │
 * │              (little-endian encoding)                           │
 * └─────────────────────────────────────────────────────────────────┘
 * 
 * =============================================================================
 * ZoH EXAMPLE: Row with ["Alice", 30, "Manager", 75000.0, true]
 * =============================================================================
 * 
 * Layout: [STRING, INT32, STRING, DOUBLE, BOOL]
 * Scenario: Only INT32 and BOOL columns changed from previous row
 * 
 * Head Bitset (1 byte for 5 columns):
 * Type-grouped bit assignment:
 *   Bit 0: BOOL (col 4) = true  → value bit
 *   Bit 1: INT32 (col 1)        → change flag
 *   Bit 2: DOUBLE (col 3)       → change flag
 *   Bit 3: STRING (col 0)       → change flag
 *   Bit 4: STRING (col 2)       → change flag
 *
 * ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 * │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  │
 * ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
 * │ PAD │ PAD │ PAD │STR2 │STR1 │DOUB │INT32│BOOL │
 * │  0  │  0  │  0  │  0  │  0  │  0  │  1  │  1  │
 * └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
 * 
 * Bitset Value: 0b00000011 = 0x03
 * - Bit 0 (BOOL col 4): 1 = boolean value is true (no data in section)
 * - Bit 1 (INT32 col 1): 1 = changed (4 bytes in data section)
 * - Bit 2 (DOUBLE col 3): 0 = unchanged (no data in section)
 * - Bit 3 (STRING col 0): 0 = unchanged (no data in section)
 * - Bit 4 (STRING col 2): 0 = unchanged (no data in section)
 * - Bits 5-7: Padding bits (always 0)
 * 
 * Data Section (4 bytes, type-grouped order: INT32 data):
 * ┌────────────────────┐
 * │       INT32        │
 * │     (4 bytes)      │
 * │      30            │
 * └────────────────────┘
 * 
 * Total ZoH Row Size: 5 bytes (1 Bitset + 4 data)
 * vs. Standard Row Size: 45 bytes (33 fixed + 12 variable)
 * Compression Ratio: 89% space savings!
 * 
 * =============================================================================
 * ZoH SERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Bool bulk compare and assign:
 *    any_change = !head_.equalRange(row.bits_, 0, boolCount)
 *    head_.assignRange(row.bits_, 0, boolCount)
 * 
 * 2. Reserve space for head Bitset in buffer
 * 
 * 3. For each scalar ColumnType (UINT8..DOUBLE), iterate per-type offset vector:
 *    for each offset in off_<type>:
 *        if data_[offset] != row.data_[offset]:  // memcmp of TypeSize bytes
 *            head_[head_idx] = 1     // mark changed
 *            copy to data_[offset]   // update prev
 *            append value to buffer
 *        else:
 *            head_[head_idx] = 0     // mark unchanged
 *        head_idx++
 * 
 * 4. For each string column, compare and emit similarly
 * 
 * 5. If any_change:
 *        Write head_ to reserved buffer location
 *        Return span of serialized data
 *    Else:
 *        Undo buffer growth, return empty span (ZoH repeat)
 * 
 * =============================================================================
 * ZoH DESERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Read head Bitset from buffer:
 *    head_.readFrom(buffer, head_.sizeBytes())
 * 
 * 2. Bool bulk extract:
 *    row.bits_.assignRange(head_, 0, boolCount)
 * 
 * 3. For each scalar ColumnType (UINT8..DOUBLE), iterate per-type offset vector:
 *    for each offset in off_<type>:
 *        if head_[head_idx]:
 *            memcpy from buffer into row.data_[offset]
 *        head_idx++
 *    // else: keep previous value (Zero Order Hold principle)
 * 
 * 4. For each string column:
 *    if head_[head_idx]:
 *        read uint16_t length, then string payload
 *    head_idx++
 * 
 * =============================================================================
 * ZoH PERFORMANCE CHARACTERISTICS
 * =============================================================================
 * 
 * Benefits:
 * - Dramatic space savings for slowly-changing time-series data (70-95% typical)
 * - Boolean values require only 1 bit instead of 1 byte (87.5% savings)
 * - Improved compression ratios with LZ4 due to sparse data patterns
 * - Reduced I/O bandwidth and memory usage
 * - Cache-friendly due to smaller serialized data
 * - Perfect for sensor data, financial tickers, configuration changes
 * 
 * Trade-offs:
 * - Not suitable for rapidly-changing data (overhead exceeds benefits)
 * - Deserialization requires maintaining previous row state
 * - Slightly more complex serialization/deserialization logic
 * - First row in each packet must be fully populated (no compression)
 * 
 * Optimal Use Cases:
 * - Time-series data with sparse updates
 * - Configuration data that changes infrequently  
 * - Sensor readings with stable periods
 * - Financial data with hold periods
 * - Any scenario where < 30% of columns change per row
 * 
 * =============================================================================
 */



    // Mutable visitor concept: accepts (size_t, T&) signature
    // Uses || (OR) to check if visitor accepts the pattern with at least one BCSV type
    // Generic lambdas with auto& will satisfy this constraint
    template<typename V>
    concept RowVisitor = 
        std::is_invocable_v<V, size_t, bool&> ||
        std::is_invocable_v<V, size_t, int8_t&> ||
        std::is_invocable_v<V, size_t, int16_t&> ||
        std::is_invocable_v<V, size_t, int32_t&> ||
        std::is_invocable_v<V, size_t, int64_t&> ||
        std::is_invocable_v<V, size_t, uint8_t&> ||
        std::is_invocable_v<V, size_t, uint16_t&> ||
        std::is_invocable_v<V, size_t, uint32_t&> ||
        std::is_invocable_v<V, size_t, uint64_t&> ||
        std::is_invocable_v<V, size_t, float&> ||
        std::is_invocable_v<V, size_t, double&> ||
        std::is_invocable_v<V, size_t, std::string&> ||
        std::is_invocable_v<V, size_t, std::string_view&>;

    // Const visitor concept: accepts (size_t, const T&)  
    // Uses || (OR) to check if visitor accepts the pattern with at least one BCSV type
    // Generic lambdas with const auto& will satisfy this constraint
    template<typename V>
    concept RowVisitorConst = 
        std::is_invocable_v<V, size_t, const bool&> ||
        std::is_invocable_v<V, size_t, const int8_t&> ||
        std::is_invocable_v<V, size_t, const int16_t&> ||
        std::is_invocable_v<V, size_t, const int32_t&> ||
        std::is_invocable_v<V, size_t, const int64_t&> ||
        std::is_invocable_v<V, size_t, const uint8_t&> ||
        std::is_invocable_v<V, size_t, const uint16_t&> ||
        std::is_invocable_v<V, size_t, const uint32_t&> ||
        std::is_invocable_v<V, size_t, const uint64_t&> ||
        std::is_invocable_v<V, size_t, const float&> ||
        std::is_invocable_v<V, size_t, const double&> ||
        std::is_invocable_v<V, size_t, const std::string&> ||
        std::is_invocable_v<V, size_t, const std::string_view&>;

    // Typed mutable visitor concept: accepts (size_t, T&) for a specific type T
    // Used by visit<T>() for compile-time type-safe iteration over homogeneous column ranges
    template<typename V, typename T>
    concept TypedRowVisitor = 
        std::is_invocable_v<V, size_t, T&>;

    // Typed const visitor concept: accepts (size_t, const T&) for a specific type T
    // Used by visitConst<T>() for compile-time type-safe read-only iteration
    template<typename V, typename T>
    concept TypedRowVisitorConst = 
        std::is_invocable_v<V, size_t, const T&>;

    /* Dynamic row with flexible layout (runtime-defined)
     *
     * Storage is split into three type-family containers:
     *   bits_  — Bitset storing bool column values
     *   data_  — contiguous byte buffer for scalar/arithmetic column values (aligned)
     *   strg_  — vector of strings for string column values
     *
     * Per-column offsets live in Layout::Data (shared across all rows sharing the same layout).
     * The meaning of layout_.columnOffset(i) depends on columnType(i):
     *   BOOL   → bit index into bits_
     *   STRING → index into strg_
     *   scalar → byte offset into data_
     *
     * bits_.size() == number of BOOL columns only.
     */
    // Forward declarations for codec friend access
    template<typename LayoutType> class RowCodecFlat001;
    template<typename LayoutType> class RowCodecZoH001;
    template<typename LayoutType> class RowCodecDelta001;

    class Row {
        // Codec friend access — direct access to bits_, data_, strg_ for serialization.
        // See docs/ITEM_11_PLAN.md §5 for rationale.
        template<typename LT> friend class RowCodecFlat001;
        template<typename LT> friend class RowCodecZoH001;
        template<typename LT> friend class RowCodecDelta001;

    private:
        // Immutable after construction
        Layout                      layout_;               // Shared layout data with observer callbacks

        // Mutable data — three type-family storage containers
        Bitset<>                    bits_;                  // Bool values (one bit per BOOL column)
        std::vector<std::byte>      data_;                 // Aligned scalar/arithmetic column values (no bools, no strings)
        std::vector<std::string>    strg_;                 // String column values

        // Observer callback for layout changes
        void onLayoutUpdate(const std::vector<Layout::Data::Change>& changes);

    public:
        Row() = delete; // no default constructor, we always need a layout
        Row(const Layout& layout);
        Row(const Row& other);
        Row(Row&& other);

        ~Row();

        void                        clear();
        const Layout&               layout() const noexcept         { return layout_; }


        [[nodiscard]] const void*   get(size_t index) const;
                                    template<typename T>
        [[nodiscard]] decltype(auto) get(size_t index) const;
                                    template<typename T>
        void                        get(size_t index, std::span<T> &dst) const;
                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T &dst) const;                        // Flexible: allows type conversions, returns false on failure

                                    template<typename T>
        [[nodiscard]] decltype(auto) ref(size_t index);                                      // get a mutable reference to the internal data (returns T& for scalars/strings, Bitset<>::reference for bool)

                                    template<detail::BcsvAssignable T>
        void                        set(size_t index, const T& value);
        void                        set(size_t index, const char* value)        { set<std::string_view>(index, value); } // Convenience overload for C-style strings, forwarding to std::string_view version
                                    template<typename T>
        void                        set(size_t index, std::span<const T> values);

        /** @brief Visit columns with read-only access. @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        /** @brief Visit all columns with read-only access. @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(Visitor&& visitor) const;

        /** @brief Visit columns with mutable access. @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Visit all columns with mutable access. @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

        /** @brief Type-safe visit: iterate columns of known type T with compile-time dispatch (no runtime switch). @see row.hpp */
                                    template<typename T, typename Visitor>
                                        requires TypedRowVisitor<Visitor, T>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Type-safe const visit: iterate columns of known type T with compile-time dispatch (no runtime switch). @see row.hpp */
                                    template<typename T, typename Visitor>
                                        requires TypedRowVisitorConst<Visitor, T>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        Row&                        operator=(const Row& other);               // Throws std::invalid_argument if layouts incompatible
        Row&                        operator=(Row&& other);
    };


    /* Static row with compile-time defined layout */
    template<typename... ColumnTypes>
    class RowStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        static constexpr size_t COLUMN_COUNT = LayoutType::columnCount();
        
        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;

        // Codec friend access — direct access to data_ for serialization.
        // See docs/ITEM_11_PLAN.md §5 for rationale.
        template<typename LT> friend class RowCodecFlat001;
        template<typename LT> friend class RowCodecZoH001;
        template<typename LT> friend class RowCodecDelta001;

    private:
        // Immutable after construction
        LayoutType layout_; 

        // Mutable data
        typename LayoutType::ColTypes   data_;

    public:
        // Constructors
        RowStatic() = delete;
        RowStatic(const LayoutType& layout);
        RowStatic(const RowStatic& other) = default;        // default copy constructor, tuple manages copying its members including strings
        RowStatic(RowStatic&& other) noexcept = default;    // default move constructor, tuple manages moving its members including strings
        ~RowStatic() = default;
       
                                    template<size_t Index = 0>
        void                        clear();
        const LayoutType&           layout() const noexcept         { return layout_; }  // Reconstruct facade

        // =========================================================================
        // 1. Static Access (Compile-Time Index) - Zero Overhead
        // =========================================================================
                                    template<size_t Index>
        [[nodiscard]] const auto&   get() const noexcept;                                   // Direct reference to column data. No overhead.

                                    template<size_t StartIndex, typename T, size_t Extent>
        void                        get(std::span<T, Extent> &dst) const;                   // Vectorized: exact match fast path, conversions if needed
        
        // =========================================================================
        // 2. Dynamic Access (Runtime Index) - Branching Overhead
        // =========================================================================
        [[nodiscard]] const void*   get(size_t index) const noexcept;                       // Get raw pointer (void*). returns nullptr if index invalid.

                                    template<typename T>
        [[nodiscard]] const T&      get(size_t index) const;                                // Scalar runtime access. Throws on type/index mismatch.
        
                                    template<typename T, size_t Extent = std::dynamic_extent> 
        void                        get(size_t index, std::span<T, Extent> &dst) const;     // Strict: vectorized runtime access. Throws on type/index mismatch.

                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T &dst) const noexcept;               // Flexible: allows type conversions, returns false on failure.
        
                                    template<typename T>
        [[nodiscard]] T&            ref(size_t index);                                      // get a mutable reference to the internal data (no type conversion, may throw)

                                    template<size_t Index, typename T>
        void                        set(const T& value);                                    // scalar compile-time indexed access
                                    template<typename T>
        void                        set(size_t index, const T& value);                      // scalar runtime indexed access
                                    template<size_t StartIndex, typename T, size_t Extent>
        void                        set(std::span<const T, Extent> values);                 // vectorized compile-time indexed access
                                    template<typename T, size_t Extent = std::dynamic_extent>
        void                        set(size_t index, std::span<const T, Extent> values);   // vectorized runtime indexed access

        /** @brief Visit columns with read-only access (compile-time). @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        /** @brief Visit all columns with read-only access (compile-time). @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(Visitor&& visitor) const;
        
        /** @brief Visit columns with mutable access (compile-time). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Visit all columns with mutable access (compile-time). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

        RowStatic& operator=(const RowStatic& other) = default;          // default copy assignment, tuple manages copying its members including strings (may throw on string alloc)
        RowStatic& operator=(RowStatic&& other) noexcept = default;      // default move assignment, tuple manages moving its members including strings
    };

} // namespace bcsv
