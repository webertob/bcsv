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
 * enabling near-memcpy serialization and simple zero-copy RowView access.
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
 * For RowImpl with TrackingPolicy::Disabled, bits_ can be memcpy'd directly
 * (size == ⌈bool_count / 8⌉). For TrackingPolicy::Enabled, bool values are
 * extracted via the layout's boolMask() (derived from ~tracked_mask_) since bits_ also holds change flags.
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
 * no explicit offsets are stored. RowView pre-computes these at construction.
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
 * - Zero-copy RowView: bit-level bool access (>>3 for byte, &7 for bit)
 * 
 * Trade-offs:
 * - RowView bool access requires bit-level addressing (trivial shift+mask)
 * - String access in RowView requires pre-computed cumulative offsets
 * - In-memory data_ is aligned but wire data_ is packed — per-column copy
 * 
 * =============================================================================
 * MEMORY LAYOUT CONSIDERATIONS
 * =============================================================================
 * 
 * - No alignment padding in wire format (minimizes file size)
 * - All access uses memcpy for alignment safety
 * - In-memory Row data_ has natural alignment (via Layout::Data offsets_)
 * - RowView stores section start offsets + per-column section-relative offsets
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
 * - Boolean values are encoded directly in the change Bitset bits
 * - Bit value 0 = false, Bit value 1 = true
 * - Boolean columns never contribute data to the data section
 * - Boolean encoding is independent of whether the value changed
 * 
 * =============================================================================
 * ZoH BINARY FORMAT LAYOUT
 * =============================================================================
 * 
 * [Change Bitset] [Changed Data 1] [Changed Data 2] ... [Changed Data N]
 * |              | |              | |              |   |              |
 * | Column Flags | | Value 1      | | Value 2      |   | Value N      |
 * | + Bool Values| | (if changed) | | (if changed) |   | (if changed) |
 * 
 * =============================================================================
 * CHANGE BITSET FORMAT
 * =============================================================================
 * 
 * The change Bitset is a compact bitfield with different semantics per column type:
 * 
 * Bit Layout (for N columns):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │Padding (if needed)       │ N-1│ N-2 │ ... │  2  │  1  │  0  │   │
 * ├─────────────────────────┼─────┼─────┼─────┼─────┼─────┼─────┤   │
 * │      Zero-filled        │ColN-1│Col-2│ ... │Col2 │Col1 │Col0 │   │
 * │                         │     │     │     │     │     │     │   │
 * └─────────────────────────┴─────┴─────┴─────┴─────┴─────┴─────┘   │
 * 
 * Bit Meanings:
 * - For NON-BOOL columns: Bit = 1 means changed, 0 means unchanged
 * - For BOOL columns: Bit directly encodes the boolean value (0=false, 1=true)
 *   IMPORTANT: Boolean values are ALWAYS encoded in the Bitset, regardless of
 *   whether they changed from the previous row. The bit represents the actual
 *   boolean value, not a change indicator.
 * 
 * Bit Ordering: Bit 0 (column 0) is the least significant bit (rightmost),
 * padding bits are added to the left (most significant bits) to reach byte boundary.
 * 
 * Bitset Size: Rounded up to nearest byte boundary for efficient storage
 * 
 * =============================================================================
 * ZoH DATA SECTION FORMAT
 * =============================================================================
 * 
 * Only columns marked as changed (bit = 1) have data in the data section.
 * Data appears in layout order (column 0, column 1, ..., column N).
 * 
 * For BOOLEAN columns:
 * - No data in data section (value stored directly in change Bitset)
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
 * Change Bitset (1 byte for 5 columns):
 * ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 * │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  │
 * ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
 * │ PAD │ PAD │ PAD │BOOL │DOUB │STR2 │INT32│STR1 │
 * │  0  │  0  │  0  │  1  │  0  │  0  │  1  │  0  │
 * └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
 * 
 * Bitset Value: 0b00010010 = 0x12
 * - Bit 0 (STR1): 0 = unchanged (no data in section)
 * - Bit 1 (INT32): 1 = changed (4 bytes in data section)  
 * - Bit 2 (STR2): 0 = unchanged (no data in section)
 * - Bit 3 (DOUBLE): 0 = unchanged (no data in section)
 * - Bit 4 (BOOL): 1 = boolean value is true (no data in section)
 * - Bits 5-7: Padding bits (always 0)
 * 
 * Data Section (4 bytes):
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
 * Note: The Bitset shows correct bit ordering with column 0 at bit position 0
 * (least significant bit) and padding at the most significant bits (left side).
 * 
 * =============================================================================
 * ZoH SERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Check if any non-BOOL changes exist (internal tracking state):
 *    if (!trackingAnyChanged()) return; // Skip serialization entirely
 * 
 * 2. Reserve space for change Bitset:
 *    bitset_size = (COLUMN_COUNT + 7) / 8; // Round up to bytes
 *    reserve_space(buffer, bitset_size);
 * 
 * 3. Process each column in layout order:
 *    for each column i:
 *        if column_type == BOOL:
 *            Bitset[i] = boolean_value; // Store value directly in Bitset
 *        else if changed[i]:
 *            Bitset[i] = 1; // Mark as changed
 *            if column_type == STRING:
 *                append(buffer, string_length_uint16);
 *                append(buffer, string_data);
 *            else:
 *                append(buffer, primitive_value);
 * 
 * 4. Write Bitset to reserved location:
 *    write_bitset_to_buffer(reserved_location);
 * 
 * =============================================================================
 * ZoH DESERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Read change Bitset:
 *    bitset_size = (COLUMN_COUNT + 7) / 8;
 *    read_bitset(buffer, bitset_size);
 *    data_start = buffer + bitset_size;
 * 
 * 2. Process each column in layout order:
 *    current_pos = data_start;
 *    for each column i:
 *        if column_type == BOOL:
 *            column_value[i] = Bitset[i]; // Read boolean value directly from Bitset
 *        else if Bitset[i] == 1: // Column changed
 *            if column_type == STRING:
 *                length = read_uint16(current_pos);
 *                current_pos += 2;
 *                column_value[i] = read_string(current_pos, length);
 *                current_pos += length;
 *            else:
 *                column_value[i] = read_primitive(current_pos, column_type);
 *                current_pos += sizeof(column_type);
 *        // else: keep previous value (Zero Order Hold principle)
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
 * - Requires change tracking overhead during data modification
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



    // Mutable visitor concept: accepts either (size_t, T&, bool&) or (size_t, T&) signature
    // Uses || (OR) to check if visitor accepts the pattern with at least one BCSV type
    // Generic lambdas with auto& will satisfy this constraint
    template<typename V>
    concept RowVisitor = 
        std::is_invocable_v<V, size_t, bool&, bool&> ||
        std::is_invocable_v<V, size_t, int8_t&, bool&> ||
        std::is_invocable_v<V, size_t, int16_t&, bool&> ||
        std::is_invocable_v<V, size_t, int32_t&, bool&> ||
        std::is_invocable_v<V, size_t, int64_t&, bool&> ||
        std::is_invocable_v<V, size_t, uint8_t&, bool&> ||
        std::is_invocable_v<V, size_t, uint16_t&, bool&> ||
        std::is_invocable_v<V, size_t, uint32_t&, bool&> ||
        std::is_invocable_v<V, size_t, uint64_t&, bool&> ||
        std::is_invocable_v<V, size_t, float&, bool&> ||
        std::is_invocable_v<V, size_t, double&, bool&> ||
        std::is_invocable_v<V, size_t, std::string&, bool&> ||
        std::is_invocable_v<V, size_t, std::string_view&, bool&> ||
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

    // Typed mutable visitor concept: accepts (size_t, T&, bool&) or (size_t, T&) for a specific type T
    // Used by visit<T>() for compile-time type-safe iteration over homogeneous column ranges
    template<typename V, typename T>
    concept TypedRowVisitor = 
        std::is_invocable_v<V, size_t, T&, bool&> ||
        std::is_invocable_v<V, size_t, T&>;

    // Typed const visitor concept: accepts (size_t, const T&) for a specific type T
    // Used by visitConst<T>() for compile-time type-safe read-only iteration
    template<typename V, typename T>
    concept TypedRowVisitorConst = 
        std::is_invocable_v<V, size_t, const T&>;

    /* Dynamic row with flexible layout (runtime-defined)
     *
     * Storage is split into three type-family containers:
     *   bits_  — Bitset storing bool column values (and change flags when tracking enabled)
     *   data_  — contiguous byte buffer for scalar/arithmetic column values (aligned)
     *   strg_  — vector of strings for string column values
     *
     * Per-column offsets live in Layout::Data (shared across all rows sharing the same layout).
     * The meaning of layout_.columnOffset(i) depends on columnType(i):
     *   BOOL   → bit index into bits_  (when tracking disabled; when enabled, column index i is used directly)
     *   STRING → index into strg_
     *   scalar → byte offset into data_
     *
     * When tracking is enabled, bits_.size() == columnCount. Bit i is:
     *   - the bool VALUE for BOOL columns (no separate change tracking for bools)
     *   - the CHANGE FLAG for non-BOOL columns (1 = changed since last reset)
     * When tracking is disabled, bits_.size() == number of BOOL columns only.
     */
    // Forward declarations for codec friend access
    template<typename LayoutType, TrackingPolicy P> class RowCodecFlat001;
    template<typename LayoutType, TrackingPolicy P> class RowCodecZoH001;

    template<TrackingPolicy Policy>
    class RowImpl {
        // Codec friend access — direct access to bits_, data_, strg_ for serialization.
        // See docs/ITEM_11_PLAN.md §5 for rationale.
        template<typename LT, TrackingPolicy P> friend class RowCodecFlat001;
        template<typename LT, TrackingPolicy P> friend class RowCodecZoH001;

    private:
        // Immutable after construction
        Layout                      layout_;               // Shared layout data with observer callbacks

        // Mutable data — three type-family storage containers
        Bitset<>                    bits_;                  // Bool values + change tracking (see class comment for semantics)
        std::vector<std::byte>      data_;                 // Aligned scalar/arithmetic column values (no bools, no strings)
        std::vector<std::string>    strg_;                 // String column values

        // Observer callback for layout changes
        void onLayoutUpdate(const std::vector<Layout::Data::Change>& changes);

        // Helper: get the bits_ index for a given column
        inline size_t bitsIndex(size_t columnIndex) const noexcept {
            if constexpr (isTrackingEnabled(Policy)) {
                return columnIndex;  // bits_.size() == columnCount, direct index
            } else {
                return layout_.columnOffset(columnIndex);  // sequential bool index
            }
        }

        [[nodiscard]] bool          trackingAnyChanged() const noexcept;
        void                        trackingSetAllChanged() noexcept;
        void                        trackingResetChanged() noexcept;

    public:
        RowImpl() = delete; // no default constructor, we always need a layout
        RowImpl(const Layout& layout);
        RowImpl(const RowImpl& other);
        RowImpl(RowImpl&& other) noexcept;

        ~RowImpl();

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
        [[nodiscard]] decltype(auto) ref(size_t index);                                      // get a mutable reference to the internal data (returns T& for scalars/strings, Bitset<>::reference for bool, marks column as changed)

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

        /** @brief Visit columns with mutable access and change tracking. @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Visit all columns with mutable access and change tracking. @see row.hpp */
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

        RowImpl&                    operator=(const RowImpl& other);               // Throws std::invalid_argument if layouts incompatible
        RowImpl&                    operator=(RowImpl&& other) noexcept;
    };

    template<TrackingPolicy Policy>
    using RowTracked = RowImpl<Policy>;

    using Row = RowImpl<TrackingPolicy::Disabled>;

    /* RowView provides a direct view into a serilized buffer, partially supporting the row interface. Intended for sparse data access, avoiding costly full deserialization.
       Currently we only support the basic get/set interface for individual columns, into a flat serilized buffer. We do not support ZoH format or more complex encoding schemes.
    */
    class RowView {
        // Immutable after construction
        Layout                  layout_;        // Shared layout data (no callbacks needed for views)
        RowCodecFlat001<Layout, TrackingPolicy::Disabled> codec_; // Wire-format metadata + per-column offsets (Item 11)

    public:
        RowView() = delete;
        RowView(const Layout& layout, std::span<std::byte> buffer = {});
        RowView(const RowView& other);
        RowView(RowView&& other) noexcept;
        ~RowView() = default;

        [[nodiscard]] const std::span<std::byte>& buffer() const noexcept                   { return codec_.buffer(); }
        const Layout&               layout() const noexcept                                 { return layout_; }
        void                        setBuffer(const std::span<std::byte> &buffer) noexcept  { codec_.setBuffer(buffer); }
        [[nodiscard]] Row           toRow() const;
        [[nodiscard]] bool          validate(bool deepValidation = true) const;

        [[nodiscard]] std::span<const std::byte>  get(size_t index) const;
                                    template<typename T>
        [[nodiscard]] T             get(size_t index) const;
                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, std::span<T> &dst) const;
                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T &dst) const;                        // Flexible: allows type conversions, returns false on failure
                
                                    template<typename T>
        [[nodiscard]] bool          set(size_t index, const T& value);
                                    template<typename T>
        [[nodiscard]] bool          set(size_t index, std::span<const T> src);
        
        /** @brief Visit columns with read-only access (zero-copy). @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        /** @brief Visit all columns with read-only access (zero-copy). @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(Visitor&& visitor) const;
        
        /** @brief Visit columns with mutable access (primitives only). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Visit all columns with mutable access (primitives only). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

        /** @brief Type-safe visit: iterate columns of known type T with compile-time dispatch (no runtime switch).
         *  For string columns use T=std::string_view (read-only). @see row.hpp */
                                    template<typename T, typename Visitor>
                                        requires TypedRowVisitor<Visitor, T>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Type-safe const visit: iterate columns of known type T with compile-time dispatch (no runtime switch).
         *  For string columns use T=std::string_view. @see row.hpp */
                                    template<typename T, typename Visitor>
                                        requires TypedRowVisitorConst<Visitor, T>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        RowView& operator=(const RowView& other);
        RowView& operator=(RowView&& other) noexcept;
    };



    /* Dynamic row with static layout (compile-time defined) */
    template<TrackingPolicy Policy = TrackingPolicy::Disabled, typename... ColumnTypes>
    class RowStaticImpl {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        static constexpr size_t COLUMN_COUNT = LayoutType::columnCount();
        
        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;

        // Codec friend access — direct access to data_, changes_ for serialization.
        // See docs/ITEM_11_PLAN.md §5 for rationale.
        template<typename LT, TrackingPolicy P> friend class RowCodecFlat001;
        template<typename LT, TrackingPolicy P> friend class RowCodecZoH001;

    private:
        // Immutable after construction
        LayoutType layout_; 

        // Mutable data
        typename LayoutType::ColTypes   data_;
        Bitset<COLUMN_COUNT>            changes_;

        [[nodiscard]] bool              trackingAnyChanged() const noexcept     { return isTrackingEnabled(Policy) ? changes_.any() : true; }
        void                            trackingSetAllChanged() noexcept         { if constexpr (isTrackingEnabled(Policy)) { changes_.set(); } }
        void                            trackingResetChanged() noexcept          { if constexpr (isTrackingEnabled(Policy)) { changes_.reset(); } }

    public:
        // Constructors
        RowStaticImpl() = delete;
        RowStaticImpl(const LayoutType& layout);
        RowStaticImpl(const RowStaticImpl& other) = default;        // default copy constructor, tuple manges copying its members including strings
        RowStaticImpl(RowStaticImpl&& other) noexcept = default;    // default move constructor, tuple manges moving its members including strings
        ~RowStaticImpl() = default;
       
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
        [[nodiscard]] T&            ref(size_t index);                                      // get a mutable reference to the internal data (no type conversion, may throw, marks column as changed)   

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
        
        /** @brief Visit columns with mutable access and tracking (compile-time). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Visit all columns with mutable access and tracking (compile-time). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

        RowStaticImpl& operator=(const RowStaticImpl& other) noexcept = default; // default copy assignment, tuple manges copying its members including strings
        RowStaticImpl& operator=(RowStaticImpl&& other) noexcept = default;      // default move assignment, tuple manges moving its members including strings
    };

    template<typename... ColumnTypes>
    using RowStatic = RowStaticImpl<TrackingPolicy::Disabled, ColumnTypes...>;

    template<TrackingPolicy Policy, typename... ColumnTypes>
    using RowStaticTracked = RowStaticImpl<Policy, ColumnTypes...>;


    /* Provides a zero-copy view into a buffer with compile-time layout. 
       Intended for sparse data access, avoiding costly full deserialization.
       Currently we only support the basic get/set interface into a flat serilized buffer. We do not support ZoH format or more complex encoding schemes. Change tracking is not supported.
    */
    template<typename... ColumnTypes>
    class RowViewStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        using Codec = RowCodecFlat001<LayoutType, TrackingPolicy::Disabled>;  // Wire-format metadata source (Item 11)
        static constexpr size_t COLUMN_COUNT = LayoutType::columnCount();

        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;

        // ── Flat wire-format constants (sourced from codec — single source of truth) ──
        static constexpr size_t BOOL_COUNT      = Codec::BOOL_COUNT;
        static constexpr size_t STRING_COUNT    = Codec::STRING_COUNT;
        static constexpr size_t WIRE_BITS_SIZE  = Codec::WIRE_BITS_SIZE;
        static constexpr size_t WIRE_DATA_SIZE  = Codec::WIRE_DATA_SIZE;
        static constexpr size_t WIRE_STRG_COUNT = Codec::WIRE_STRG_COUNT;
        static constexpr size_t WIRE_FIXED_SIZE = Codec::WIRE_FIXED_SIZE;
        static constexpr auto   WIRE_OFFSETS    = Codec::WIRE_OFFSETS;


    private:
        // Immutable after construction --> We need to protect these members from modification (const wont' work due to assignment & move operators)
        LayoutType              layout_;
        Codec                   codec_;

    public:

        RowViewStatic() = delete;
        RowViewStatic(const LayoutType& layout, std::span<std::byte> buffer = {})
            : layout_(layout), codec_() {
            codec_.setup(layout_);
            codec_.setBuffer(buffer);
        }
        RowViewStatic(const RowViewStatic& other);
        RowViewStatic(RowViewStatic&& other) noexcept;
        ~RowViewStatic() = default;

        const std::span<std::byte>& buffer() const noexcept                                     { return codec_.buffer(); }
        const LayoutType&           layout() const noexcept                                     { return layout_; }
        void                        setBuffer(const std::span<std::byte> &buffer) noexcept      { codec_.setBuffer(buffer); }

        // =========================================================================
        // 1. Static Access (Compile-Time) - Zero Copy where possible
        // =========================================================================
        /** Get value by Static Index.
         *  for Primitives: Returns T by value (via memcpy, handles misalignment).
         *  for Strings:    Returns std::string_view pointing into buffer (Zero Copy).
         */
                                    template<size_t Index>
        [[nodiscard]] auto          get() const;                                                    // scalar static access (const)

                                    template<size_t StartIndex, typename T, size_t Extent>
        void                        get(std::span<T, Extent> &dst) const;                            // Vectorized: exact match fast path, conversions if needed


        /** Set primitive value by Static Index.
         *  As we cannot resize the underlying buffer, strings are only able to be set to same length as existing string.
         *  for Primitives: Copies value into buffer (via memcpy, handles misalignment).
         */
                                    template<size_t Index, typename T>
        void                        set(const T& value) noexcept;                                   // scalar static access

                                    template<size_t StartIndex, typename T, size_t Extent>
        void                        set(std::span<const T, Extent> values) noexcept;                // vectorized static access

        // =========================================================================
        // 2. Dynamic Access (Runtime Index)
        // =========================================================================
        [[nodiscard]] std::span<const std::byte>  get(size_t index) const noexcept;                 // returns empty if index invalid

                                    template<typename T, size_t Extent>
        [[nodiscard]] bool          get(size_t index, std::span<T, Extent>& dst) const noexcept;    // Strict: vectorized runtime access

                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T& dst) const noexcept;                       // Flexible: allows type conversions

                                    template<typename T>
        void                        set(size_t index, const T& value) noexcept;                     // scalar runtime access

                                    template<typename T, size_t Extent>
        void                        set(size_t index, std::span<const T, Extent> values) noexcept;  // vectorized runtime access

        // =========================================================================
        // 3. Conversion / Validation
        // =========================================================================
        [[nodiscard]] RowStatic<ColumnTypes...>   toRow() const;
        [[nodiscard]] bool          validate() const noexcept;

        /** @brief Visit columns with read-only access (compile-time, zero-copy). @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        /** @brief Visit all columns with read-only access (compile-time, zero-copy). @see row.hpp */
                                    template<RowVisitorConst Visitor>
        void                        visitConst(Visitor&& visitor) const;
        
        /** @brief Visit columns with mutable access (compile-time, primitives only). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

        /** @brief Visit all columns with mutable access (compile-time, primitives only). @see row.hpp */
                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

        RowViewStatic& operator=(const RowViewStatic& other) noexcept;
        RowViewStatic& operator=(RowViewStatic&& other) noexcept;

    private:
        void validateVisitRange(size_t startIndex, size_t count, const char* fnName) const;

        template<RowVisitorConst Visitor, size_t... I>
        void visitConstAtIndex(size_t index, Visitor&& visitor, std::index_sequence<I...>) const;

        template<RowVisitor Visitor, size_t... I>
        void visitMutableAtIndex(size_t index, Visitor&& visitor, std::index_sequence<I...>);

        template<size_t Index = 0>
        bool validateStringPayloads() const;
    };

} // namespace bcsv
