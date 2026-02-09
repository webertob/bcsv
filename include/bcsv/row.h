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
#include <cstring>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "definitions.h"
#include "layout.h"
#include "bitset.h"
#include "byte_buffer.h"

namespace bcsv {

/**
 * @brief BCSV Binary Row Format Documentation
 * 
 * The BCSV row format uses a two-section layout optimized for performance and compression:
 * 1. Fixed Section: Contains all fixed-size data and string addresses
 * 2. Variable Section: Contains actual string payloads
 * 
 * =============================================================================
 * ROW BINARY FORMAT LAYOUT
 * =============================================================================
 * 
 * [Fixed Section] [Variable Section]
 * |              | |              |
 * | Col1 | Col2  | | Str1 | Str2  |
 * |      | Col3  | |      | Str3  |
 * |      | ...   | |      | ...   |
 * 
 * =============================================================================
 * FIXED SECTION FORMAT
 * =============================================================================
 * 
 * The fixed section contains one entry per column in layout order:
 * 
 * For STRING columns:
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                    StringAddress (64-bit)                       │
 * ├─────────────────────────────────────┬───────────────────────────┤
 * │         Payload Offset (48-bit)     │    Length (16-bit)        │
 * │              Bits 16-63             │     Bits 0-15             │
 * └─────────────────────────────────────┴───────────────────────────┘
 * 
 * Payload Offset: Absolute byte offset from start of row to string data
 * Length:        String length in bytes (max 65,535)
 * 
 * For PRIMITIVE columns (INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64, FLOAT, DOUBLE, BOOL):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                     Raw Value Data                              │
 * │                (1, 2, 4, or 8 bytes)                            │
 * └─────────────────────────────────────────────────────────────────┘
 * 
 * =============================================================================
 * VARIABLE SECTION FORMAT
 * =============================================================================
 * 
 * The variable section contains string payloads in the order they appear:
 * 
 * ┌─────────────────┬─────────────────┬─────────────────┬─────────────┐
 * │   String 1      │   String 2      │   String 3      │    ...      │
 * │   Payload       │   Payload       │   Payload       │             │
 * └─────────────────┴─────────────────┴─────────────────┴─────────────┘
 * 
 * - No length prefixes (lengths are in StringAddress)
 * - No null terminators (lengths are explicit)
 * - Strings are tightly packed with no padding
 * - Empty strings contribute 0 bytes to variable section
 * 
 * =============================================================================
 * EXAMPLE: Row with ["John", 25, "Engineer", 3.14, true]
 * =============================================================================
 * 
 * Layout: [STRING, INT32, STRING, DOUBLE, BOOL]
 * 
 * Fixed Section (33 bytes):
 * ┌────────────┬────────────┬────────────┬────────────┬────────────┐
 * │StringAddr1 │   INT32    │StringAddr2 │  DOUBLE    │   BOOL     │
 * │  (8 bytes) │ (4 bytes)  │ (8 bytes)  │ (8 bytes)  │ (1 byte)   │
 * │   Offset:33│    25      │  Offset:37 │    3.14    │   true     │
 * │   Length:4 │            │  Length:8  │            │            │
 * └────────────┴────────────┴────────────┴────────────┴────────────┘
 * 
 * Variable Section (12 bytes):
 * ┌────────────┬────────────────────┐
 * │   "John"   │    "Engineer"      │
 * │  (4 bytes) │    (8 bytes)       │
 * └────────────┴────────────────────┘
 * 
 * Total Row Size: 45 bytes
 * 
 * =============================================================================
 * STRING ADDRESS ENCODING DETAILS
 * =============================================================================
 * 
 * StringAddress struct layout:
 * 
 * Bit Layout (64-bit little-endian):
 * ┌─────────────────────────────────────────────────────────────────┐
 * │ 63  56  48  40  32  24  16   8   0                              │
 * ├──────────────────────────────────┬──────────────────────────────┤
 * │        Payload Offset            │         Length               │
 * │         (48 bits)                │        (16 bits)             │
 * └──────────────────────────────────┴──────────────────────────────┘
 * 
 * Encoding Algorithm:
 * packed_value = ((offset & 0xFFFFFFFFFFFF) << 16) | (length & 0xFFFF)
 * 
 * Decoding Algorithm:
 * offset = (packed_value >> 16) & 0xFFFFFFFFFFFF
 * length = packed_value & 0xFFFF
 * 
 * Limits:
 * - Maximum offset: 281,474,976,710,655 bytes (281 TB)
 * - Maximum length: 65,535 bytes (64 KB)
 * 
 * =============================================================================
 * SERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Calculate Fixed Section Size:
 *    - For each column: add sizeof(StringAddress) for strings, sizeof(T) for primitives
 * 
 * 2. Calculate Variable Section Size:
 *    - Sum lengths of all string values (truncated to MAX_STRING_LENGTH)
 * 
 * 3. Write Fixed Section:
 *    current_payload_offset = fixed_section_size
 *    for each column:
 *        if STRING:
 *            string_length = min(string.length(), MAX_STRING_LENGTH)
 *            string_address = pack(current_payload_offset, string_length)
 *            write(string_address, 8 bytes)
 *            current_payload_offset += string_length
 *        else:
 *            write(primitive_value, sizeof(primitive_value))
 * 
 * 5. Write Variable Section:
 *    for each string column (in layout order):
 *        write(string_data, truncated_length)
 * 
 * 6. Write Padding Section:
 *    write(0x00, padding_bytes)
 * 
 * =============================================================================
 * DESERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Parse Fixed Section:
 *    fixed_ptr = row_start
 *    for each column:
 *        if STRING:
 *            string_address = read_uint64(fixed_ptr)
 *            (offset, length) = unpack(string_address)
 *            store_string_info(offset, length)
 *            fixed_ptr += 8
 *        else:
 *            value = read_primitive(fixed_ptr, column_type)
 *            store_value(value)
 *            fixed_ptr += sizeof(column_type)
 * 
 * 2. Parse Variable Section:
 *    for each string column:
 *        string_data = read_bytes(row_start + offset, length)
 *        store_string(string_data)
 * 
 * =============================================================================
 * PERFORMANCE CHARACTERISTICS
 * =============================================================================
 * 
 * Benefits:
 * - Fixed-size access patterns for non-string data
 * - String pointers enable zero-copy string views
 * - Optimal for columnar processing (skip string parsing if not needed)
 * - Efficient compression (fixed section compresses well)
 * - Cache-friendly for numeric operations
 * - Memory-mapped file friendly
 * - Compact representation with no padding overhead
 * 
 * Trade-offs:
 * - Two-pass parsing required for complete row reconstruction
 * - String access requires offset arithmetic
 * - Not optimal for row-at-a-time processing of mixed data
 * 
 * =============================================================================
 * MEMORY LAYOUT CONSIDERATIONS
 * =============================================================================
 * 
 * - All StringAddress values are 8-byte aligned within the fixed section
 * - Primitive values follow their natural alignment within fixed section
 * - Variable section has no alignment requirements (byte-packed)
 * - Row boundaries are not enforced to any specific alignment
 * - Compact storage with no padding between sections or rows
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
 * - Boolean values are encoded directly in the change bitset bits
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
 * The change bitset is a compact bitfield with different semantics per column type:
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
 *   IMPORTANT: Boolean values are ALWAYS encoded in the bitset, regardless of
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
 * - No data in data section (value stored directly in change bitset)
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
 * Total ZoH Row Size: 5 bytes (1 bitset + 4 data)
 * vs. Standard Row Size: 45 bytes (33 fixed + 12 variable)
 * Compression Ratio: 89% space savings!
 * 
 * Note: The bitset shows correct bit ordering with column 0 at bit position 0
 * (least significant bit) and padding at the most significant bits (left side).
 * 
 * =============================================================================
 * ZoH SERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Check if any changes exist:
 *    if (!hasAnyChanges()) return; // Skip serialization entirely
 * 
 * 2. Reserve space for change bitset:
 *    bitset_size = (column_count + 7) / 8; // Round up to bytes
 *    reserve_space(buffer, bitset_size);
 * 
 * 3. Process each column in layout order:
 *    for each column i:
 *        if column_type == BOOL:
 *            bitset[i] = boolean_value; // Store value directly in bitset
 *        else if changed[i]:
 *            bitset[i] = 1; // Mark as changed
 *            if column_type == STRING:
 *                append(buffer, string_length_uint16);
 *                append(buffer, string_data);
 *            else:
 *                append(buffer, primitive_value);
 * 
 * 4. Write bitset to reserved location:
 *    write_bitset_to_buffer(reserved_location);
 * 
 * =============================================================================
 * ZoH DESERIALIZATION ALGORITHM
 * =============================================================================
 * 
 * 1. Read change bitset:
 *    bitset_size = (column_count + 7) / 8;
 *    read_bitset(buffer, bitset_size);
 *    data_start = buffer + bitset_size;
 * 
 * 2. Process each column in layout order:
 *    current_pos = data_start;
 *    for each column i:
 *        if column_type == BOOL:
 *            column_value[i] = bitset[i]; // Read boolean value directly from bitset
 *        else if bitset[i] == 1: // Column changed
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



    // Mutable visitor concept: requires (size_t, T&, bool&) signature
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
        std::is_invocable_v<V, size_t, std::string&, bool&>;

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
        std::is_invocable_v<V, size_t, const std::string&>;
        
    /* Dynamic row with flexible layout (runtime-defined)*/
    class Row {
    private:
        // Immutable after construction
        Layout                      layout_;               // Shared layout data with observer callbacks
         
        // Mutable data
        std::vector<std::byte>      data_;                 // continuous memory buffer to hold data (Note: We store string objects here too. But strings themselves are allocating additional memory elsewhere for their content.)
        std::vector<std::byte*>     ptr_;                  // pointers into data_ for each column, set up according to layout and updated on layout changes (for efficient access without recalculating offsets every time)
        bitset<>                    changes_;              // change tracking
        /* change tracking i.e. for Zero-Order-Hold compression
        *  changes_[0:column_count  ] == true   indicates column i has been modified since last reset.
        *  changes_.empty()           == true   indicates change tracking is disabled.
        */

        // Observer callback for layout changes
        void onLayoutUpdate(const std::vector<Layout::Data::Change>& changes);
 
        //Depreceated: we are going to remove this in the next future, when moving to Serializer/Deserializer classes, as the Row class should be only responsible for in-memory data management, not serialization details. But for now we keep it here for simplicity.
        std::vector<uint32_t>       offsets_;              // offsets into data_ to access actual data for each column. Considers types alignment requirements.
        uint32_t                    offset_var_;           // offset into serilized buffer (wire), marking the beginning of variable-length section / size of the fixed section. Naive packed wire format (no alignment/padding).
 
    public:
        Row() = delete; // no default constructor, we always need a layout
        Row(const Layout& layout, bool trackChangesEnabled = false);
        Row(const Row& other);
        Row(Row&& other) noexcept;
        
        ~Row();

        void                        clear();
        const Layout&               layout() const noexcept         { return layout_; }
        [[nodiscard]] bool          hasAnyChanges() const noexcept  { return !tracksChanges() || changes_.any(); } // defaults to true if change tracking is disabled
        void                        trackChanges(bool enable)       { if (enable) { changes_.resize(layout_.columnCount(), true); } else { changes_.resize(0); } }
        [[nodiscard]] bool          tracksChanges() const noexcept  { return !changes_.empty(); } 
        void                        setChanges() noexcept           { changes_.set(); }     // mark everything as changed
        void                        resetChanges() noexcept         { changes_.reset(); }   // mark everything as unchanged

        [[nodiscard]] const void*   get(size_t index) const;
                                    template<typename T>
        [[nodiscard]] const T&      get(size_t index) const;
                                    template<typename T>
        void                        get(size_t index, std::span<T> &dst) const;
                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T &dst) const;                        // Flexible: allows type conversions, returns false on failure

                                    template<typename T>
        [[nodiscard]] const T&      ref(size_t index) const;                                // get a const reference to the internal data (no type conversion, may throw)

                                    template<typename T>
        [[nodiscard]] T&            ref(size_t index);                                      // get a mutable reference to the internal data (no type conversion, may throw, marks column as changed)    

                                    template<detail::BcsvAssignable T>
        void                        set(size_t index, const T& value);
        void                        set(size_t index, const char* value)        { set<std::string_view>(index, value); } // Convenience overload for C-style strings, forwarding to std::string_view version
                                    template<typename T>
        void                        set(size_t index, std::span<const T> values);

        [[nodiscard]] std::span<std::byte>        serializeTo(ByteBuffer& buffer) const;
        [[nodiscard]] std::span<std::byte>        serializeToZoH(ByteBuffer& buffer) const;
        void                        deserializeFrom(const std::span<const std::byte> buffer);
        void                        deserializeFromZoH(const std::span<const std::byte> buffer);

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

        Row&                        operator=(const Row& other);               // Throws std::invalid_argument if layouts incompatible
        Row&                        operator=(Row&& other) noexcept;
    };

    /* RowView provides a direct view into a serilized buffer, partially supporting the row interface. Intended for sparse data access, avoiding costly full deserialization.
       Currently we only support the basic get/set interface for individual columns, into a flat serilized buffer. We do not support ZoH format or more complex encoding schemes.
    */
    class RowView {
        // Immutable after construction
        Layout                  layout_;        // Shared layout data (no callbacks needed for views)
        std::vector<uint32_t>   offsets_;       // offsets data buffer to access actual data for each column during serialization/deserialization (packed binary format, special handling for strings (variable length types))
        uint32_t                offset_var_;    // begin of variable section in buffer_     

        // Mutable data
        std::span<std::byte>    buffer_;        // serilized data buffer (fixed + variable section, packed binary format)

    
    public:
        RowView() = delete;
        RowView(const Layout& layout, std::span<std::byte> buffer = {});
        RowView(const RowView& other) = default;        // default copy constructor, as we don't own the buffer
        RowView(RowView&& other) noexcept = default;    // default move constructor, as we don't own the buffer
        ~RowView() = default;

        [[nodiscard]] const std::span<std::byte>& buffer() const noexcept                   { return buffer_; }
        const Layout&               layout() const noexcept                                 { return layout_; }
        void                        setBuffer(const std::span<std::byte> &buffer) noexcept  { buffer_ = buffer; }
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
        
        RowView& operator=(const RowView& other) noexcept = default; // default copy assignment, as we don't own the buffer
        RowView& operator=(RowView&& other) noexcept = default;      // default move assignment, as we don't own the buffer
    };



    /* Dynamic row with static layout (compile-time defined) */
    template<typename... ColumnTypes>
    class RowStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        static constexpr size_t column_count = LayoutType::columnCount();
        
        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;

        // Helpers defining the layout of the wire format (serialized data)
        // serialized data: lengths/size of each column in [bytes]
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_lengths = { wireSizeOf<ColumnTypes>()... };
       
        // serialized data: offsets of each column in [bytes]
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_offsets = []() {
            size_t off = 0; 
            return std::array<size_t, sizeof...(ColumnTypes)>{ 
                std::exchange(off, off + wireSizeOf<ColumnTypes>())... 
            }; 
        }();
        
        // serialized data: offset to beginning of variable-length section in [bytes]
        static constexpr size_t offset_var_ = (wireSizeOf<ColumnTypes>() + ... + 0);

    private:
        // Immutable after construction
        LayoutType layout_; 

        // Mutable data
        typename LayoutType::ColTypes   data_;
        bitset<column_count>            changes_;
        bool                            change_tracking_ = false;

    public:
        // Constructors
        RowStatic() = delete;
        RowStatic(const LayoutType& layout);
        RowStatic(const RowStatic& other) = default;        // default copy constructor, tuple manges copying its members including strings
        RowStatic(RowStatic&& other) noexcept = default;    // default move constructor, tuple manges moving its members including strings
        ~RowStatic() = default;
       
                                    template<size_t Index = 0>
        void                        clear();
        const LayoutType&           layout() const noexcept         { return layout_; }  // Reconstruct facade
        [[nodiscard]] bool          hasAnyChanges() const noexcept  { if(change_tracking_) { return changes_.any(); } else { return true; } } // defaults to true if change tracking is disabled
        void                        trackChanges(bool enable)       { change_tracking_ = enable; }
        [[nodiscard]] bool          tracksChanges() const noexcept  { return change_tracking_; }
        void                        setChanges() noexcept           { if(change_tracking_) changes_.set(); }     // mark everything as changed
        void                        resetChanges() noexcept         { if(change_tracking_) changes_.reset(); }   // mark everything as unchanged

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
        [[nodiscard]] const T&      ref(size_t index) const;                                // get a const reference to the internal data (no type conversion, may throw)

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

        [[nodiscard]] std::span<std::byte>        serializeTo(ByteBuffer& buffer) const;
        [[nodiscard]] std::span<std::byte>        serializeToZoH(ByteBuffer& buffer) const;
        void                        deserializeFrom(const std::span<const std::byte> buffer);
        void                        deserializeFromZoH(const std::span<const std::byte> buffer);

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

        RowStatic& operator=(const RowStatic& other) noexcept = default; // default copy assignment, tuple manges copying its members including strings
        RowStatic& operator=(RowStatic&& other) noexcept = default;      // default move assignment, tuple manges moving its members including strings

    private:
                                    template<size_t Index>
        void                        serializeElements(ByteBuffer& buffer, const size_t& offRow, size_t& offVar) const;

                                    template<size_t Index>
        void                        serializeElementsZoH(ByteBuffer& buffer, bitset<column_count>& bitHeader) const;

                                    template<size_t Index>
        void                        deserializeElements(const std::span<const std::byte> &srcBuffer);

                                    template<size_t Index>
        void                        deserializeElementsZoH(std::span<const std::byte> &srcBuffer);
    };


    /* Provides a zero-copy view into a buffer with compile-time layout. 
       Intended for sparse data access, avoiding costly full deserialization.
       Currently we only support the basic get/set interface into a flat serilized buffer. We do not support ZoH format or more complex encoding schemes. Change tracking is not supported.
    */
    template<typename... ColumnTypes>
    class RowViewStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        static constexpr size_t column_count = LayoutType::columnCount();

        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;

        // Helpers defining the layout of the wire format (serialized data)
        // serialized data: lengths/size of each column in [bytes]
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_lengths = { wireSizeOf<ColumnTypes>()... };
       
        // serialized data: offsets of each column in [bytes]
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_offsets = []() {
            size_t off = 0; 
            return std::array<size_t, sizeof...(ColumnTypes)>{ 
                std::exchange(off, off + wireSizeOf<ColumnTypes>())... 
            }; 
        }();
        
        // serialized data: offset to beginning of variable-length section in [bytes]
        static constexpr size_t offset_var_ = (wireSizeOf<ColumnTypes>() + ... + 0);


    private:
        // Immutable after construction --> We need to protect these members from modification (const wont' work due to assignment & move operators)
        LayoutType              layout_;

        // Mutable data
        std::span<std::byte>    buffer_;

    public:

        RowViewStatic() = delete;
        RowViewStatic(const LayoutType& layout, std::span<std::byte> buffer = {})
            : layout_(layout), buffer_(buffer) {}
        RowViewStatic(const RowViewStatic& other) = default;        // default copy constructor, as we don't own the buffer
        RowViewStatic(RowViewStatic&& other) noexcept = default;    // default move constructor
        ~RowViewStatic() = default;

        const std::span<std::byte>& buffer() const noexcept                                     { return buffer_; }
        const LayoutType&           layout() const noexcept                                     { return layout_; }
        void                        setBuffer(const std::span<std::byte> &buffer) noexcept      { buffer_ = buffer; }

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

        RowViewStatic& operator=(const RowViewStatic& other) noexcept = default; // default copy assignment, as we don't own the buffer
        RowViewStatic& operator=(RowViewStatic&& other) noexcept = default;      // default move assignment, as we don't own the buffer

    private:
        template<size_t Index = 0>
        bool validateStringPayloads() const;
    };

} // namespace bcsv
