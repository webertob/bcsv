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
    constexpr uint32_t BCSV_MAGIC = 0x56534342;   // "BCSV" in little-endian
    constexpr uint32_t PCKT_MAGIC = 0xDEADBEEF;   // "PCKT" in little-endian
    constexpr size_t LZ4_BLOCK_SIZE_KB = 64;      // Average uncompressed payload of a packet
    constexpr size_t MAX_STRING_LENGTH = 65535-1; // Maximum length of string data
    constexpr size_t MAX_COLUMN_WIDTH  = 65535-1; // Maximum width of column content
    constexpr size_t MAX_COLUMN_COUNT  = 65535-1;

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

    template<typename T>
    static constexpr ColumnDataType toColumnDataType() {
        if constexpr (std::is_same_v<T, bool>) return ColumnDataType::BOOL;
        else if constexpr (std::is_same_v<T, int8_t>) return ColumnDataType::INT8;
        else if constexpr (std::is_same_v<T, int16_t>) return ColumnDataType::INT16;
        else if constexpr (std::is_same_v<T, int32_t>) return ColumnDataType::INT32;
        else if constexpr (std::is_same_v<T, int64_t>) return ColumnDataType::INT64;
        else if constexpr (std::is_same_v<T, uint8_t>) return ColumnDataType::UINT8;
        else if constexpr (std::is_same_v<T, uint16_t>) return ColumnDataType::UINT16;
        else if constexpr (std::is_same_v<T, uint32_t>) return ColumnDataType::UINT32;
        else if constexpr (std::is_same_v<T, uint64_t>) return ColumnDataType::UINT64;
        else if constexpr (std::is_same_v<T, float>) return ColumnDataType::FLOAT;
        else if constexpr (std::is_same_v<T, double>) return ColumnDataType::DOUBLE;
        else if constexpr (std::is_same_v<T, std::string>) return ColumnDataType::STRING;
        else static_assert(always_false<T>, "Unsupported type");
    };

    // Base template
    template<ColumnDataType Type>
    struct fromColumnDataTypeT {
        using type = void; // Invalid by default
    };

    // Specializations
    template<> struct fromColumnDataTypeT<ColumnDataType::BOOL> { using type = bool; };
    template<> struct fromColumnDataTypeT<ColumnDataType::UINT8> { using type = uint8_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::UINT16> { using type = uint16_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::UINT32> { using type = uint32_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::UINT64> { using type = uint64_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::INT8> { using type = int8_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::INT16> { using type = int16_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::INT32> { using type = int32_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::INT64> { using type = int64_t; };
    template<> struct fromColumnDataTypeT<ColumnDataType::FLOAT> { using type = float; };
    template<> struct fromColumnDataTypeT<ColumnDataType::DOUBLE> { using type = double; };
    template<> struct fromColumnDataTypeT<ColumnDataType::STRING> { using type = std::string; };

    // Convert ValueType variant to ColumnDataType
    template<ColumnDataType Type>
    using fromColumnDataType = typename fromColumnDataTypeT<Type>::type;

    ColumnDataType toColumnDataType(const ValueType& value) {
        return std::visit([](auto&& arg) -> ColumnDataType {
            using T = std::decay_t<decltype(arg)>;
            return toColumnDataType<T>();
        }, value);
    }

    // Helper functions for type conversion
    inline ColumnDataType stringToDataType(const std::string& typeString) {
        if (typeString == "bool") return ColumnDataType::BOOL;
        if (typeString == "uint8") return ColumnDataType::UINT8;
        if (typeString == "uint16") return ColumnDataType::UINT16;
        if (typeString == "uint32") return ColumnDataType::UINT32;
        if (typeString == "uint64") return ColumnDataType::UINT64;
        if (typeString == "int8") return ColumnDataType::INT8;
        if (typeString == "int16") return ColumnDataType::INT16;
        if (typeString == "int32") return ColumnDataType::INT32;
        if (typeString == "int64") return ColumnDataType::INT64;
        if (typeString == "float") return ColumnDataType::FLOAT;
        if (typeString == "double") return ColumnDataType::DOUBLE;
        return ColumnDataType::STRING; // default
    }

    inline std::string dataTypeToString(ColumnDataType type) {
        switch (type) {
            case ColumnDataType::BOOL: return "bool";
            case ColumnDataType::UINT8: return "uint8";
            case ColumnDataType::UINT16: return "uint16";
            case ColumnDataType::UINT32: return "uint32";
            case ColumnDataType::UINT64: return "uint64";
            case ColumnDataType::INT8: return "int8";
            case ColumnDataType::INT16: return "int16";
            case ColumnDataType::INT32: return "int32";
            case ColumnDataType::INT64: return "int64";
            case ColumnDataType::FLOAT: return "float";
            case ColumnDataType::DOUBLE: return "double";
            case ColumnDataType::STRING: return "string";
            default: return "undefined";
        }
    }

    using ValueType = std::variant<bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, double, std::string>;

    /**
     * @brief Get default value for a given column data type
     * @param type The column data type
     * @return Default ValueType for the specified type
     */
    inline ValueType defaultValue(ColumnDataType type) {
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
            static_assert(always_false<Type>, "Unsupported type for defaultValueT");
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

    inline bool isType(const ValueType& value, ColumnDataType type) {
        return std::visit([type](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, bool>) return type == ColumnDataType::BOOL;
            else if constexpr (std::is_same_v<T, int8_t>) return type == ColumnDataType::INT8;
            else if constexpr (std::is_same_v<T, int16_t>) return type == ColumnDataType::INT16;
            else if constexpr (std::is_same_v<T, int32_t>) return type == ColumnDataType::INT32;
            else if constexpr (std::is_same_v<T, int64_t>) return type == ColumnDataType::INT64;
            else if constexpr (std::is_same_v<T, uint8_t>) return type == ColumnDataType::UINT8;
            else if constexpr (std::is_same_v<T, uint16_t>) return type == ColumnDataType::UINT16;
            else if constexpr (std::is_same_v<T, uint32_t>) return type == ColumnDataType::UINT32;
            else if constexpr (std::is_same_v<T, uint64_t>) return type == ColumnDataType::UINT64;
            else if constexpr (std::is_same_v<T, float>) return type == ColumnDataType::FLOAT;
            else if constexpr (std::is_same_v<T, double>) return type == ColumnDataType::DOUBLE;
            else if constexpr (std::is_same_v<T, std::string>) return type == ColumnDataType::STRING;
            else return false;
        }, value);
    }

    /* Get the serialized size of a value (cell)
    *  Considering our custom file layout, especially for strings.
    */
    template<typename T>
    constexpr size_t serializedSize(const T& val) {
        using DecayT = std::decay_t<T>;
        if constexpr (std::is_same_v<DecayT, std::string>) {
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
                using TargetType = std::tuple_element_t<candidate, std::tuple<T...>>;
                if (std::holds_alternative<TargetType>(value)) {
                    std::get<candidate>(tuple) = std::get<TargetType>(value);
                } else {
                    throw std::runtime_error("Type mismatch in setTupleValue");
                }
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

    template<typename T>
    class LazyAllocator {
    public:
        using value_type = T;
        
        T* allocate(size_t n) {
            return static_cast<T*>(std::malloc(n * sizeof(T)));
        }
        
        void deallocate(T* p, size_t) {
            std::free(p);
        }
        
        // Don't construct elements
        template<typename U, typename... Args>
        void construct(U*, Args&&...) {}
        
        template<typename U>
        void destroy(U*) {}
    };

    using ByteBuffer = std::vector<std::byte, LazyAllocator<std::byte>>;

    ValueType convertValueType(const ValueType &value, ColumnDataType targetType) {
        std::visit([targetType](const auto& v) -> ValueType {
            using SrcType = std::decay_t<decltype(v)>;

            switch (targetType)
            {
            case ColumnDataType::BOOL:
                if constexpr (std::is_same_v<SrcType, bool>) return v;
                else if constexpr (std::is_convertible_v<SrcType, bool>) return static_cast<bool>(v);
                break;
            case ColumnDataType::UINT8:
                if constexpr (std::is_same_v<SrcType, uint8_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, uint8_t>) return static_cast<uint8_t>(v);
                break;
            case ColumnDataType::UINT16:
                if constexpr (std::is_same_v<SrcType, uint16_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, uint16_t>) return static_cast<uint16_t>(v);
                break;
            case ColumnDataType::UINT32:
                if constexpr (std::is_same_v<SrcType, uint32_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, uint32_t>) return static_cast<uint32_t>(v);
                break;
            case ColumnDataType::UINT64:
                if constexpr (std::is_same_v<SrcType, uint64_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, uint64_t>) return static_cast<uint64_t>(v);
                break;
            case ColumnDataType::INT8:
                if constexpr (std::is_same_v<SrcType, int8_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, int8_t>) return static_cast<int8_t>(v);
                break;
            case ColumnDataType::INT16:
                if constexpr (std::is_same_v<SrcType, int16_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, int16_t>) return static_cast<int16_t>(v);
                break;
            case ColumnDataType::INT32:
                if constexpr (std::is_same_v<SrcType, int32_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, int32_t>) return static_cast<int32_t>(v);
                break;
            case ColumnDataType::INT64:
                if constexpr (std::is_same_v<SrcType, int64_t>) return v;
                else if constexpr (std::is_convertible_v<SrcType, int64_t>) return static_cast<int64_t>(v);
                break;
            case ColumnDataType::FLOAT:
                if constexpr (std::is_same_v<SrcType, float>) return v;
                else if constexpr (std::is_convertible_v<SrcType, float>) return static_cast<float>(v);
                break;
            case ColumnDataType::DOUBLE:
                if constexpr (std::is_same_v<SrcType, double>) return v;
                else if constexpr (std::is_convertible_v<SrcType, double>) return static_cast<double>(v);
                break;
            case ColumnDataType::STRING:
                if constexpr (std::is_same_v<SrcType, std::string>) return v;
                else if constexpr (std::is_convertible_v<SrcType, std::string>) return static_cast<std::string>(v);
                break;
            default:
                break;
            }
        }, value);
        return defaultValue(targetType);
    }
} // namespace bcsv