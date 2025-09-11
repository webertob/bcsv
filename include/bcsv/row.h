#pragma once


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

namespace bcsv {

/**
 * @brief BCSV Binary Row Format Documentation
 * 
 * The BCSV row format uses a two-section layout optimized for performance and compression:
 * 1. Fixed Section: Contains all fixed-size data and string addresses
 * 2. Variable Section: Contains actual string payloads
 * 3. Padding Section: Zero-filled bytes for 4-byte alignment
 * 
 * =============================================================================
 * ROW BINARY FORMAT LAYOUT
 * =============================================================================
 * 
 * [Fixed Section] [Variable Section] [Padding Section]
 * |              | |              | |               |
 * | Col1 | Col2  | | Str1 | Str2  | | 0x00 | 0x00   |
 * |      | Col3  | |      | Str3  | |      | 0x00   |
 * |      | ...   | |      | ...   | |      | ...    |
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
 * PADDING SECTION FORMAT
 * =============================================================================
 * 
 * The padding section ensures 4-byte alignment for optimal performance:
 * 
 * ┌─────────────────┐
 * │   Padding       │
 * │   (0-3 bytes)   │
 * │   All 0x00      │
 * └─────────────────┘
 * 
 * Padding Calculation:
 * unpadded_size = fixed_section_size + variable_section_size
 * padding_bytes = (4 - (unpadded_size % 4)) % 4
 * 
 * Padding Examples:
 * - Unpadded size 45 bytes → 3 padding bytes → Total 48 bytes
 * - Unpadded size 48 bytes → 0 padding bytes → Total 48 bytes
 * - Unpadded size 49 bytes → 3 padding bytes → Total 52 bytes
 * - Unpadded size 50 bytes → 2 padding bytes → Total 52 bytes
 * - Unpadded size 51 bytes → 1 padding byte  → Total 52 bytes
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
 * Padding Section (3 bytes):
 * ┌────────────┬────────────┬────────────┐
 * │   0x00     │   0x00     │   0x00     │
 * │  (1 byte)  │  (1 byte)  │  (1 byte)  │
 * └────────────┴────────────┴────────────┘
 * 
 * Total Row Size: 48 bytes (45 + 3 padding)
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
 * 3. Calculate Padding Size:
 *    unpadded_size = fixed_section_size + variable_section_size
 *    padding_bytes = (4 - (unpadded_size % 4)) % 4
 * 
 * 4. Write Fixed Section:
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
 * 3. Skip Padding Section:
 *    - Calculate expected padding bytes
 *    - Skip padding_bytes at end of row
 *    - Validate next row starts at aligned boundary
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
 * - 4-byte alignment enables SIMD operations and reduces CPU stalls
 * - Memory-mapped file friendly
 * 
 * Trade-offs:
 * - Two-pass parsing required for complete row reconstruction
 * - String access requires offset arithmetic
 * - Not optimal for row-at-a-time processing of mixed data
 * - Up to 3 bytes of padding overhead per row
 * 
 * =============================================================================
 * MEMORY ALIGNMENT CONSIDERATIONS
 * =============================================================================
 * 
 * - All StringAddress values are 8-byte aligned
 * - Primitive values follow their natural alignment within fixed section
 * - Variable section has no alignment requirements (byte-packed)
 * - Row start should be aligned to 8-byte boundary for optimal access
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
        Layout                    layout_;    
        std::vector<ValueType>    data_;
    
    public:
        Row(const Layout& layout);
        Row() = delete;
        ~Row() = default;

        // Layout access
        const Layout& getLayout() const { return layout_; }

        template<typename T>
        T getAs(size_t index) const;
        const ValueType& get(size_t index) const;

        void set(size_t index, const auto& value);

        // serialization/deserialization
        void serializedSize(size_t& fixedSize, size_t& totalSize) const;
        void serializeTo(std::span<std::byte> buffer) const;
        void deserializeFrom(const std::span<const std::byte> buffer);  //ToDo: implement
    
        // Assignment validates layout compatibility
        Row& operator=(const Row& other);
    };



    /* Direct view into a buffer. Supports Row interface */
    class RowView {
        Layout                  layout_;
        std::span<std::byte>    buffer_;

        template<typename T>
        void setExplicit(size_t index, const T& value);

    public:
        RowView() = delete;
        RowView(const Layout& layout, std::span<std::byte> buffer = {})
            : layout_(layout), buffer_(buffer) {}
        ~RowView() = default;

        template<typename T = ValueType>
        T get(size_t index) const;
        const std::span<std::byte>& getBuffer() const { return buffer_; }
        const Layout& getLayout() const { return layout_; }
        void set(size_t index, const auto& value);
        void setBuffer(const std::span<std::byte> &buffer) { buffer_ = buffer; }

        Row toRow() const;
        bool validate() const;
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
        LayoutType   layout_;
        column_types data_;
        
    public:
        
        // Constructors
        RowStatic() = delete;
        RowStatic(const LayoutType& layout) : layout_(layout), data_() {}
        ~RowStatic() = default;
       
        // Layout access
        const LayoutType& getLayout() const { return layout_; }

        // get/set using compile time fixed indices
        template<size_t Index>
        auto& get();

        template<size_t Index>
        const auto& get() const;

        template<size_t Index>
        void set(const auto& value);

        template<size_t Index, typename T>
        void setExplicit(const T& value);

        // get/set using run time variable indices
        template<size_t I = 0>
        ValueType get(size_t index) const;

        template<size_t I = 0>
        void set(size_t index, const auto& value);

        // serialization/deserialization 
        void serializedSize(size_t& fixedSize, size_t& totalSize) const;
        void serializeTo(std::span<std::byte> buffer) const;
        void deserializeFrom(const std::span<const std::byte> buffer);  //ToDo: implement

    private:
        template<size_t Index>
        void serializeElements(std::span<std::byte> &dstBuffer, size_t& strOffset) const;

        template<size_t Index>
        void deserializeElements(const std::span<std::byte> &srcBuffer);

        template<size_t Index>
        void calculateStringSizes(size_t& totalSize) const;
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
        const std::span<std::byte>& getBuffer() const { return buffer_; }
        const LayoutType& getLayout() const { return layout_; }
        
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
    };

} // namespace bcsv
