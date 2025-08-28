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
    ValueType defaultValue(ColumnDataType type) {
        ValueType value;
        switch (type) {
            case ColumnDataType::INT8: value = int8_t{0}; break;
            case ColumnDataType::INT16: value = int16_t{0}; break;
            case ColumnDataType::INT32: value = int32_t{0}; break;
            case ColumnDataType::INT64: value = int64_t{0}; break;
            case ColumnDataType::UINT8: value = uint8_t{0}; break;
            case ColumnDataType::UINT16: value = uint16_t{0}; break;
            case ColumnDataType::UINT32: value = uint32_t{0}; break;
            case ColumnDataType::UINT64: value = uint64_t{0}; break;
            case ColumnDataType::FLOAT: value = float{0.0f}; break;
            case ColumnDataType::DOUBLE: value = double{0.0}; break;
            case ColumnDataType::BOOL: value = bool{false}; break;
            case ColumnDataType::STRING: value = std::string{}; break;
            default: throw std::runtime_error("Unknown column type");
        }
        return value;
    }

    template<typename Type>
    constexpr Type defaultValueT() {
        if constexpr (std::is_same_v<Type, std::string>) {
            return std::string{};  // Empty string
        } else if constexpr (std::is_same_v<Type, int8_t>) {
            return int8_t{0};
        } else if constexpr (std::is_same_v<Type, int16_t>) {
            return int16_t{0};
        } else if constexpr (std::is_same_v<Type, int32_t>) {
            return int32_t{0};
        } else if constexpr (std::is_same_v<Type, int64_t>) {
            return int64_t{0};
        } else if constexpr (std::is_same_v<Type, uint8_t>) {
            return uint8_t{0};
        } else if constexpr (std::is_same_v<Type, uint16_t>) {
            return uint16_t{0};
        } else if constexpr (std::is_same_v<Type, uint32_t>) {
            return uint32_t{0};
        } else if constexpr (std::is_same_v<Type, uint64_t>) {
            return uint64_t{0};
        } else if constexpr (std::is_same_v<Type, float>) {
            return float{0.0f};
        } else if constexpr (std::is_same_v<Type, double>) {
            return double{0.0};
        } else if constexpr (std::is_same_v<Type, bool>) {
            return bool{false};
        } else {
            return ValueType{std::string{}};  // Fallback
        }
    }

    template<typename T>
    constexpr bool always_false = false;

    template<typename T>
    constexpr size_t binaryFieldLength() {
        if constexpr (std::is_same_v<T, int8_t>) return sizeof(int8_t);
        else if constexpr (std::is_same_v<T, int16_t>) return sizeof(int16_t);
        else if constexpr (std::is_same_v<T, int32_t>) return sizeof(int32_t);
        else if constexpr (std::is_same_v<T, int64_t>) return sizeof(int64_t);
        else if constexpr (std::is_same_v<T, uint8_t>) return sizeof(uint8_t);
        else if constexpr (std::is_same_v<T, uint16_t>) return sizeof(uint16_t);
        else if constexpr (std::is_same_v<T, uint32_t>) return sizeof(uint32_t);
        else if constexpr (std::is_same_v<T, uint64_t>) return sizeof(uint64_t);
        else if constexpr (std::is_same_v<T, float>) return sizeof(float);
        else if constexpr (std::is_same_v<T, double>) return sizeof(double);
        else if constexpr (std::is_same_v<T, bool>) return sizeof(bool);
        else if constexpr (std::is_same_v<T, std::string>) return sizeof(uint64_t); // StringAddress
        else static_assert(always_false<T>, "Unsupported type");
    }

    // Helper function to get size for each column type
    constexpr size_t binaryFieldLength(ColumnDataType type) {
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

    bool isType(const ValueType& value, ColumnDataType type) {
        return std::visit([type](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return type == ColumnDataType::STRING;
            } else if constexpr (std::is_integral_v<T>) {
                return type == ColumnDataType::INT64;
            } else if constexpr (std::is_floating_point_v<T>) {
                return type == ColumnDataType::FLOAT;
            } else {
                return false;
            }
        }, value);
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

    template<size_t candidate, typename ...T>
    void setTupleValue(std::tuple<T...>& tuple, size_t index, const ValueType& value) {
        if constexpr(candidate >= sizeof...(T)) {
            throw std::out_of_range("Index out of range");
        } else {
            if(candidate == index) {
                std::get<candidate>(tuple) = std::get<std::tuple_element_t<candidate, std::tuple<T...>>>(value);
            } else {
                setTupleValue<candidate + 1, T...>(tuple, index, value);
            }
        }
    }

    template<size_t candidate, typename ...T>
    std::variant<T...> getTupleValue(const std::tuple<T...>& tuple, size_t index) {
        if constexpr(candidate >= sizeof...(T)) {
            throw std::out_of_range("Index out of range");
        } else {
            if(candidate == index) {
                return std::get<candidate>(tuple);
            } else {
                return getTupleValue<candidate + 1, T...>(tuple, index);
            }
        }
    }
} // namespace bcsv