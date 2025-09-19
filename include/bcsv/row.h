#pragma once

#include <bitset>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "definitions.h"
#include "layout.h"
#include "bitset.hpp"
#include "bitset_dynamic.hpp"
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

    /* StringAddress , helps with string column addressing, packing and unpacking
     * Currently we are supporting strings with a maximum length of 16bits (65535 chars)
    */
    struct StringAddress {
        static constexpr uint64_t   OFFSET_MASK  = 0xFFFFFFFFFFFFULL;  // 48 bits (position of first char relative to row start)
        static constexpr uint64_t   LENGTH_MASK  = 0xFFFFULL;          // 16 bits (length of string in chars)
        static constexpr int        OFFSET_SHIFT = 16;
        
        static uint64_t pack(size_t offset, size_t length);
        static void unpack(uint64_t packed, size_t& offset, size_t& length);
    };


    /* Dynamic row with flexible layout (runtime-defined)*/
    class Row {
        Layout                    layout_;  // layout defining column types and order
        std::vector<ValueType>    data_;    // store values for each column
        mutable bitset_dynamic    changes_; // change tracking
        /* change tracking i.e. for Zero-Order-Hold compression
        *  changes_[0:column_count  ] == true   indicates column i has been modified since last reset.
        *  changes_.empty()           == true   indicates change tracking is disabled.
        */
        
    public:
        Row(const Layout& layout);
        Row() = delete;
        ~Row() = default;

        void                    clear(); 
        const Layout&           layout() const                  { return layout_; }
        bool                    hasAnyChanges() const           { return tracksChanges() && changes_.any(); }
        void                    trackChanges(bool enable);      
        bool                    tracksChanges() const;
        void                    setChanges()                    { changes_.set(); } // mark everything as changed
        void                    resetChanges()                  { changes_.reset(); } // mark everything as unchanged

                                template<typename T = ValueType>
        const T&                get(size_t index) const;
        void                    set(size_t index, const auto& value);

        void                    serializedSize(size_t& fixedSize, size_t& totalSize) const;
        void                    serializeTo(ByteBuffer& buffer) const;
        void                    serializeToZoH(ByteBuffer& buffer) const;
        bool                    deserializeFrom(const std::span<const std::byte> buffer);
        bool                    deserializeFromZoH(const std::span<const std::byte> buffer);
    };



    /* Direct view into a buffer. Supports Row interface */
    class RowView {
        Layout                          layout_;
        std::span<std::byte>            buffer_;

    public:
        RowView() = delete;
        RowView(const Layout& layout, std::span<std::byte> buffer = {})
            : layout_(layout), buffer_(buffer)                                          {}
        ~RowView() = default;

                                        template<typename T = ValueType>
        T                               get(size_t index) const;
        const std::span<std::byte>&     buffer() const                                  { return buffer_; }
        const Layout&                   layout() const                                  { return layout_; }
        void                            set(size_t index, const auto& value);
        void                            setBuffer(const std::span<std::byte> &buffer)   { buffer_ = buffer; }

        Row                             toRow() const;
        bool                            validate() const;
    private:
    
                                        template<typename T>
        void                            setExplicit(size_t index, const T& value);
    };



    /* Dynamic row with static layout (compile-time defined) */
    template<typename... ColumnTypes>
    class RowStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        static constexpr size_t column_count = LayoutType::column_count;
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_offsets = LayoutType::column_offsets;
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_lengths = LayoutType::column_lengths;

        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::column_types>;
        using column_types = typename LayoutType::column_types;

    private:
        LayoutType                      layout_;
        column_types                    data_;
        mutable bitset<column_count>    changes_;
        bool                            tracks_changes_ = false;
        /* change tracking i.e. for Zero-Order-Hold compression
        *  changes_[0:column_count  ] == true   indicates column i has been modified since last reset.
        */

    public:
        
        // Constructors
        RowStatic() = delete;
        RowStatic(const LayoutType& layout) : layout_(layout), data_() {}
        ~RowStatic() = default;
       
        void                        clear();
        const LayoutType&           layout() const                  { return layout_; }
        bool                        hasAnyChanges() const           { return tracks_changes_ && changes_.any(); }
        void                        trackChanges(bool enable);
        bool                        trackChanges() const            { return tracks_changes_; }
        void                        setChanges()                    { if(tracks_changes_) changes_.set(); } // mark everything as changed
        void                        resetChanges()                  { if(tracks_changes_) changes_.reset(); } // mark everything as unchanged


                                    template<size_t Index>
        auto&                       get();
                                    template<size_t Index>
        const auto&                 get() const;
                                    template<size_t Index = 0>
        ValueType                   get(size_t index) const;
                                    template<size_t Index = 0>
        void                        set(size_t index, const auto& value);
                                    template<size_t Index>
        void                        set(const auto& value);

        void                        serializedSize(size_t& fixedSize, size_t& totalSize) const;
        void                        serializeTo(ByteBuffer& buffer) const;
        void                        serializeToZoH(ByteBuffer& buffer) const;
        bool                        deserializeFrom(const std::span<const std::byte> buffer);
        bool                        deserializeFromZoH(const std::span<const std::byte> buffer);

    private:
                                    template<size_t Index>
        void                        clearHelper();

                                    template<size_t Index, typename T>
        void                        markChangedAndSet(const T& value);

                                    template<size_t Index>
        void                        serializeElements(std::span<std::byte> &dstBuffer, size_t& strOffset) const;

                                    template<size_t Index>
        void                        serializeElementsZoH(ByteBuffer& buffer) const;

                                    template<size_t Index>
        bool                        deserializeElements(const std::span<const std::byte> &srcBuffer);

                                    template<size_t Index>
        bool                        deserializeElementsZoH(std::span<const std::byte> &srcBuffer);

                                    template<size_t Index>
        void                        calculateStringSizes(size_t& totalSize) const;
    };


    /* View into a buffer. Supports RowStatic interface */
    template<typename... ColumnTypes>
    class RowViewStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        static constexpr size_t column_count = LayoutType::column_count;
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_offsets = LayoutType::column_offsets;
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> column_lengths = LayoutType::column_lengths;

        template<size_t Index>
        using column_type   = std::tuple_element_t<Index, typename LayoutType::column_types>;
        using column_types  = typename LayoutType::column_types;

    private:
        LayoutType  layout_;
        std::span<std::byte> buffer_;

    public:

        RowViewStatic() = delete;
        RowViewStatic(const LayoutType& layout, std::span<std::byte> buffer = {})
            : layout_(layout), buffer_(buffer) {}
        ~RowViewStatic() = default;

        template<size_t Index>
        auto get() const;
        template<size_t I = 0>
        ValueType get(size_t index) const;
        const std::span<std::byte>& buffer() const { return buffer_; }
        const LayoutType& layout() const { return layout_; }
        
        template<size_t Index>
        void set(const auto& value);
        template<size_t I = 0>
        void set(size_t index, const auto& value);
        void setBuffer(const std::span<std::byte> &buffer) { buffer_ = buffer; }
        template<size_t Index, typename T>
        void setExplicit(const T& value);

        RowStatic<ColumnTypes...> toRow() const;
        bool validate() const;

    private:
        template<size_t Index = 0>
        bool validateStringPayloads() const;
        
        template<size_t Index = 0>
        void copyElements(RowStatic<ColumnTypes...>& row) const;
    };

} // namespace bcsv
