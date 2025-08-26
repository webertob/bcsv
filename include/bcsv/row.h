#pragma once


#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
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

    struct StringAddress {
        static constexpr uint64_t OFFSET_MASK = 0xFFFFFFFFFFFFULL;  // 48 bits
        static constexpr uint64_t LENGTH_MASK = 0xFFFFULL;          // 16 bits
        static constexpr int OFFSET_SHIFT = 16;
        
        static uint64_t pack(size_t offset, size_t length) {
            if (offset > OFFSET_MASK) {
                throw std::overflow_error("Offset too large for 48-bit field");
            }
            if (length > LENGTH_MASK) {
                throw std::overflow_error("Length too large for 16-bit field");
            }
            return ((offset & OFFSET_MASK) << OFFSET_SHIFT) | (length & LENGTH_MASK);
        }
        
        static void unpack(uint64_t packed, size_t& offset, size_t& length) {
            offset = (packed >> OFFSET_SHIFT) & OFFSET_MASK;
            length = static_cast<uint16_t>(packed & LENGTH_MASK);
        }
    };

    /**/
    class RowInterface {
    public:
        RowInterface() = default;
        virtual ~RowInterface() = default;

        virtual void setValue(size_t index, const ValueType& value) = 0;
        virtual ValueType getValue(size_t index) const = 0;
        virtual size_t size() const = 0;
    };

    class Row : public RowInterface {
        std::vector<ValueType> data_;

    public:
        Row() = delete;
        Row(const Layout &layout);
        ~Row() = default;

        // Virtual overrides
        void setValue(size_t index, const ValueType& value) override;
        ValueType getValue(size_t index) const override;
        size_t size() const override { return data_.size(); }

        // Template methods (declared in header, implemented in .hpp)
        template<typename T>
        const T& get(size_t index) const;
        
        template<typename T>
        void set(size_t index, const T& value);

        // serialization/deserialization
        void serializedSize(size_t& fixedSize, size_t& totalSize) const;
        void serializeTo(char* dstBuffer, size_t dstBufferSize) const;
        void deserializeFrom(const char* srcBuffer, size_t srcBufferSize);
    };

    template<typename... ColumnTypes>
    class RowStatic : public RowInterface {
        std::tuple<ColumnTypes...> data_;

        // Helper functions (declared here, implemented in .hpp)
        template<size_t... Indices>
        void setValueAtIndex(size_t index, const ValueType& value, std::index_sequence<Indices...>);
        
        template<size_t Index>
        void setValueAtIndexHelper(const ValueType& value);
        
        template<size_t... Indices>
        ValueType getValueAtIndex(size_t index, std::index_sequence<Indices...>) const;
        
        template<size_t Index>
        void copyFromRowAtIndex(const Row& row);
        
        template<size_t... Indices>
        void copyFromRow(const Row& row, std::index_sequence<Indices...>);

        // Helper functions for deserialization
        template<size_t... Indices>
        void deserializeAtIndices(const char* srcBuffer, size_t srcBufferSize, std::index_sequence<Indices...>);
        
        template<size_t Index>
        void deserializeAtIndex(const char* srcBuffer, size_t srcBufferSize, const char*& fixedPtr);

        // Compile-time size calculations
        static constexpr size_t FIXED_SIZE = ((std::is_same_v<ColumnTypes, std::string> ? sizeof(uint64_t) : sizeof(ColumnTypes)) + ...);
        static constexpr bool HAS_STRINGS = (std::is_same_v<ColumnTypes, std::string> || ...);
        static constexpr size_t STRING_COUNT = ((std::is_same_v<ColumnTypes, std::string> ? 1 : 0) + ...);

    public:
        // Constructors
        RowStatic();
        
        template<typename... Args>
        explicit RowStatic(Args&&... args);
        
        explicit RowStatic(const Row& other);

        // Virtual overrides
        void setValue(size_t index, const ValueType& value) override;
        ValueType getValue(size_t index) const override;
        size_t size() const override;

        // Template access methods
        template<size_t Index>
        auto& get();
        
        template<size_t Index>
        const auto& get() const;
        
        template<size_t Index, typename T>
        void set(const T& value);

        // serialization/deserialization 
        void serializedSize(size_t& fixedSize, size_t& totalSize) const;
        void serializeTo(char* dstBuffer, size_t dstBufferSize) const;
        void deserializeFrom(const char* srcBuffer, size_t srcBufferSize);

        // Expose compile-time constants
        static constexpr size_t getFixedSize() { return FIXED_SIZE; }
        static constexpr bool hasStrings() { return HAS_STRINGS; }
        static constexpr size_t getStringCount() { return STRING_COUNT; }
    };

    /* Direct view into a buffer. Supports Row interface */
    class RowView : public RowInterface {
        char* buffer_;
        size_t bufferSize_;
        Layout layout_;
        std::vector<size_t> fixedOffsets_;

    public:
        RowView(char* buffer, size_t bufferSize, Layout layout)
            : buffer_(buffer), bufferSize_(bufferSize), layout_(std::move(layout)) {
            updateFixedOffsets();
        }

        // Row interface implementation
        void setValue(size_t index, const ValueType& value) override {
            auto columnType = layout_.getColumnType(index);
            //check time provided match type defined
            if (!isType(value, columnType)) {
                throw std::runtime_error("Type mismatch");
            }
            size_t len = binaryFieldLength(columnType);
            size_t off = fixedOffsets_[index];
            if (off + len > bufferSize_) {
                throw std::runtime_error("Field end exceeds buffer size");
            }
            char* ptr = buffer_ + off;
            switch(columnType) {
                case ColumnDataType::BOOL: {
                    bool src = std::get<bool>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::UINT8: {
                    uint8_t src = std::get<uint8_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;  
                }
                case ColumnDataType::UINT16: {
                    uint16_t src = std::get<uint16_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::UINT32: {
                    uint32_t src = std::get<uint32_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }         
                case ColumnDataType::UINT64: {
                    uint64_t src = std::get<uint64_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::INT8: {
                    int8_t src = std::get<int8_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::INT16: {
                    int16_t src = std::get<int16_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::INT32: {
                    int32_t src = std::get<int32_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::INT64: {
                    int64_t src = std::get<int64_t>(value);
                    std::memcpy(ptr, &src, sizeof(src));
                    break;
                }
                case ColumnDataType::DOUBLE: {
                    double src = std::get<double>(value);
                    std::memcpy(ptr, &src, sizeof(src));    
                    break;
                }
                case ColumnDataType::STRING: {
                    // Unpack string address
                    uint64_t packedAddr;
                    std::memcpy(&packedAddr, ptr, sizeof(packedAddr));
                    size_t strOff, strLen;
                    StringAddress::unpack(packedAddr, strOff, strLen);

                    // Validate string payload is within buffer
                    if (strOff + strLen > bufferSize_) {
                        throw std::runtime_error("String payload extends beyond buffer");
                    }

                    std::string src = std::get<std::string>(value);
                    if(src.size() < strLen) {
                        src.resize(strLen, '\0'); // Pad with null bytes if shorter
                    }
                    // Copy string data into buffer (truncate to strLen)
                    std::memcpy(buffer_ + strOff, src.data(), strLen);
                    break;
                }
            }
        }

        ValueType getValue(size_t index) const {
            auto columnType = layout_.getColumnType(index);
            size_t len = binaryFieldLength(columnType);
            size_t off = fixedOffsets_[index];
            if (off + len > bufferSize_) {
                throw std::runtime_error("Field end exceeds buffer size");
            }
            const char* ptr = buffer_ + off;
            switch(columnType) {
                case ColumnDataType::BOOL: {
                    bool value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::UINT8: {
                    uint8_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::UINT16: {
                    uint16_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::UINT32: {
                    uint32_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::UINT64: {
                    uint64_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::INT8: {
                    int8_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::INT16: {
                    int16_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::INT32: {
                    int32_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::INT64: {
                    int64_t value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::DOUBLE: {
                    double value;
                    std::memcpy(&value, ptr, sizeof(value));
                    return ValueType{value};
                }
                case ColumnDataType::STRING: {
 
                    uint64_t packedAddr;
                    std::memcpy(&packedAddr, ptr, sizeof(packedAddr));

                    // Unpack string address
                    size_t strOff, strLen;
                    StringAddress::unpack(packedAddr, strOff, strLen);

                    // Validate string payload is within buffer
                    if (strOff + strLen > bufferSize_) {
                        throw std::runtime_error("String payload extends beyond buffer");
                    }

                    // Create string from payload
                    if (strLen > 0) {
                        return ValueType{std::string(buffer_ + strOff, strLen)};
                    } else {
                        return ValueType{std::string()};
                    }
                }
            }
        }

        size_t size() const override {
            return fixedOffsets_.size();
        }

        const Layout& getLayout() const { return layout_; }
        void setLayout(Layout layout) { 
            layout_ = std::move(layout);
            updateFixedOffsets();
        }

        void setBuffer(char* buffer, size_t bufferSize) {
            buffer_ = buffer;
            bufferSize_ = bufferSize;
        }

    private:
        void updateFixedOffsets() {
            fixedOffsets_.clear();
            size_t offset = 0;
            for (size_t i = 0; i < layout_.getColumnCount(); ++i) {
                fixedOffsets_.push_back(offset);
                ColumnDataType colType = layout_.getColumnType(i);
                offset += binaryFieldLength(colType);
            }
        }

    };

    //ToDo:
    // RowViewStatic implementation
    // Implement Reader::readRow, readRows, decompressing, indexing, sequential and random access
    // Implement Writer::writeRow, writeRows
    // Code Review, Testing (unit tests), Documentation

} // namespace bcsv
