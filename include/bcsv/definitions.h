#pragma once

/* This file holds all constants and definitions used throughout the BCSV library */
#include <cstdint>
#include <string>
#include <cstddef>
#include <variant>

namespace bcsv {

    // Version information
    constexpr int VERSION_MAJOR = 1;
    constexpr int VERSION_MINOR = 0;
    constexpr int VERSION_PATCH = 0;
    
    inline std::string getVersion() {
        return std::to_string(VERSION_MAJOR) + "." + 
               std::to_string(VERSION_MINOR) + "." + 
               std::to_string(VERSION_PATCH);
    }

    // Constants for the binary file format
    constexpr uint32_t BCSV_MAGIC = 0x56534342; // "BCSV" in little-endian
    constexpr uint32_t PCKT_MAGIC = 0xDEADBEEF; // "PCKT" in little-endian
    constexpr size_t LZ4_BLOCK_SIZE_KB = 64;    // Average uncompressed payload of a packet
    constexpr size_t MAX_STRING_LENGTH = 65535; // Maximum length of string data
    constexpr size_t MAX_COLUMN_WIDTH  = 65535; // Maximum width of column content
    constexpr size_t MAX_COLUMN_COUNT  = 65535;

    // File header flags - All mandatory features in v1.0+
    namespace FileFlags {
        // Previously optional flags that are now mandatory:
        // - CHECKSUMS: Always present (CRC32 integrity checking)
        // - ROW_INDEX: Always present (row offset indexing)
        // - ALIGNED: Always present (aligned data structures)
        // - COMPRESSED: Always present (LZ4 compression)
        
        constexpr uint16_t RESERVED1 = 0x0010;
        constexpr uint16_t RESERVED2 = 0x0020;
        constexpr uint16_t RESERVED3 = 0x0040;
        constexpr uint16_t RESERVED4 = 0x0080;
        constexpr uint16_t RESERVED5 = 0x0100;
        constexpr uint16_t RESERVED6 = 0x0200;
        constexpr uint16_t RESERVED7 = 0x0400;
        constexpr uint16_t RESERVED8 = 0x0800;
    }

    // Column data type enumeration (stored as uint16_t in file)
    enum class ColumnDataType : uint16_t {
        BOOL = 0x0001,
        UINT8 = 0x0002,
        UINT16 = 0x0003,
        UINT32 = 0x0004,
        UINT64 = 0x0005,
        INT8 = 0x0006,
        INT16 = 0x0007,
        INT32 = 0x0008,
        INT64 = 0x0009,
        FLOAT = 0x000A,
        DOUBLE = 0x000B,
        STRING = 0x000C
    };

    using ValueType = std::variant<bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, double, std::string>;

    /**
     * @brief Get default value for a given column data type
     * @param type The column data type
     * @return Default ValueType for the specified type
     */
    inline ValueType defaultValue(ColumnDataType type) {
        switch (type) {
            case ColumnDataType::STRING:
                return ValueType{std::string{}};  // Empty string
                
            case ColumnDataType::INT8:
                return ValueType{int8_t{0}};
            case ColumnDataType::INT16:
                return ValueType{int16_t{0}};
            case ColumnDataType::INT32:
                return ValueType{int32_t{0}};
            case ColumnDataType::INT64:
                return ValueType{int64_t{0}};
                
            case ColumnDataType::UINT8:
                return ValueType{uint8_t{0}};
            case ColumnDataType::UINT16:
                return ValueType{uint16_t{0}};
            case ColumnDataType::UINT32:
                return ValueType{uint32_t{0}};
            case ColumnDataType::UINT64:
                return ValueType{uint64_t{0}};
                
            case ColumnDataType::FLOAT:
                return ValueType{float{0.0f}};
            case ColumnDataType::DOUBLE:
                return ValueType{double{0.0}};
                
            case ColumnDataType::BOOL:
                return ValueType{bool{false}};
                
            default:
                // Fallback to string for unknown types
                return ValueType{std::string{}};
        }
    }

    // Helper function to get size for each column type
    size_t binaryFieldLength(ColumnDataType type) {
        switch (type) {
            case ColumnDataType::INT8: return sizeof(int8_t);
            case ColumnDataType::INT16: return sizeof(int16_t);
            case ColumnDataType::INT32: return sizeof(int32_t);
            case ColumnDataType::INT64: return sizeof(int64_t);
            case ColumnDataType::UINT8: return sizeof(uint8_t);
            case ColumnDataType::UINT16: return sizeof(uint16_t);
            case ColumnDataType::UINT32: return sizeof(uint32_t);
            case ColumnDataType::UINT64: return sizeof(uint64_t);
            case ColumnDataType::FLOAT: return sizeof(float);
            case ColumnDataType::DOUBLE: return sizeof(double);
            case ColumnDataType::BOOL: return sizeof(bool);
            case ColumnDataType::STRING: return sizeof(uint64_t); // StringAddress
            default: throw std::runtime_error("Unknown column type");
        }
    }

    template<typename T>
    constexpr ValueType defaultValueT() {
        if constexpr (std::is_same_v<T, std::string>) {
            return ValueType{std::string{}};
        } else if constexpr (std::is_same_v<T, int8_t>) {
            return ValueType{int8_t{0}};
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return ValueType{int16_t{0}};
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return ValueType{int32_t{0}};
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return ValueType{int64_t{0}};
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            return ValueType{uint8_t{0}};
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            return ValueType{uint16_t{0}};
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            return ValueType{uint32_t{0}};
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return ValueType{uint64_t{0}};
        } else if constexpr (std::is_same_v<T, float>) {
            return ValueType{float{0.0f}};
        } else if constexpr (std::is_same_v<T, double>) {
            return ValueType{double{0.0}};
        } else if constexpr (std::is_same_v<T, bool>) {
            return ValueType{bool{false}};
        } else {
            return ValueType{std::string{}};  // Fallback
        }
    }

    // Helper to extract the actual type from ValueType variant
    template<typename T>
    static constexpr T extractDefaultValueT() {
        ValueType defaultVariant = defaultValueT<T>();
        return std::get<T>(defaultVariant);
    }

     /**
     * @brief Generic isSameType for arbitrary data types
     * 
     * Supports:
     * - variant to variant comparison
     * - plain type to plain type comparison  
     * - variant to plain type comparison
     * - any combination of the above
     */
    template<typename T, typename U>
    bool isSameType(const T& a, const U& b) {
        using DecayT = std::decay_t<T>;
        using DecayU = std::decay_t<U>;
        
        // Case 1: Both are the same plain type
        if constexpr (std::is_same_v<DecayT, DecayU>) {
            return true;
        }
        // Case 2: Both are variants
        else if constexpr (requires { a.index(); } && requires { b.index(); }) {
            return a.index() == b.index();
        }
        // Case 3: First is variant, second is plain type
        else if constexpr (requires { a.index(); } && !requires { b.index(); }) {
            return std::holds_alternative<DecayU>(a);
        }
        // Case 4: First is plain type, second is variant  
        else if constexpr (!requires { a.index(); } && requires { b.index(); }) {
            return std::holds_alternative<DecayT>(b);
        }
        // Case 5: Both are different plain types
        else {
            return false;
        }
    }

    /* Get the serialized size of a value (cell)
    *  Considering our custom file layout, especially for strings.
    */
    template<typename T>
    size_t serializedSize(const T& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string>) {
            // offset and length of string + string itself, limited to 16bit
            return sizeof(uint64_t) + std::min(val.size(), MAX_STRING_LENGTH);
        } else {
            return sizeof(T);
        }
    }
} // namespace bcsv