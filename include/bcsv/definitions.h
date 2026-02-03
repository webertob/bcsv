/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/* This file holds all constants and definitions used throughout the BCSV library */
#include <cstdint>
#include <string>
#include <cstddef>
#include <variant>
#include <limits>
#include "string_addr.h"

// Include auto-generated version information
#include "version_generated.h"

namespace bcsv {
    
    // Configuration
    constexpr bool RANGE_CHECKING = true;

    // Helper template for static_assert
    template<typename T>
    constexpr bool always_false = false;

    // Version information (from auto-generated header)
    constexpr int VERSION_MAJOR = version::MAJOR;
    constexpr int VERSION_MINOR = version::MINOR;
    constexpr int VERSION_PATCH = version::PATCH;
    
    inline std::string getVersion() {
        return version::STRING;
    }

    // File format version (separate from library version)
    constexpr uint8_t BCSV_FORMAT_VERSION_MAJOR = 1;
    constexpr uint8_t BCSV_FORMAT_VERSION_MINOR = 3;
    constexpr uint8_t BCSV_FORMAT_VERSION_PATCH = 0;

    // Constants for the binary file format
    constexpr char MAGIC_BYTES_BCSV[4] = { 'B', 'C', 'S', 'V' };
    constexpr char MAGIC_BYTES_PCKT[4] = { 'P', 'C', 'K', 'T' };
    constexpr char MAGIC_BYTES_FOOTER_BIDX[4] = { 'B', 'I', 'D', 'X' };
    constexpr char MAGIC_BYTES_FOOTER_EIDX[4] = { 'E', 'I', 'D', 'X' }; 
    constexpr uint32_t BCSV_MAGIC = 0x56534342;   // "BCSV" in little-endian
    constexpr uint32_t PCKT_MAGIC = 0x54434B50;   // "PCKT" in little-endian
    constexpr uint32_t FOOTER_BIDX_MAGIC = 0x58444942;   // "BIDX" in little-endian
    constexpr uint32_t FOOTER_EIDX_MAGIC = 0x58444945;   // "EIDX" in little-endian
    constexpr uint32_t PCKT_TERMINATOR = 0x3FFFFFFF ;    // Marker value to indicate end of packet data (no more rows to come)
    constexpr size_t MAX_COLUMN_COUNT  = std::numeric_limits<uint16_t>::max();  // Maximum number of columns
    constexpr size_t MAX_COLUMN_LENGTH = std::numeric_limits<uint16_t>::max();  // Maximum width of column content
    constexpr size_t MAX_STRING_LENGTH = MAX_COLUMN_LENGTH;                     // Maximum length of string data
    constexpr size_t MAX_ROW_LENGTH    = (1ULL << 24) - 2 ;                     // about 16MB maximum Maximum size of a single row in bytes, using 4b BLE encoding (2 bits for length), reserve 0xFFFF for terminator.
    constexpr size_t MIN_PACKET_SIZE   = 64 * 1024;                             // 64KB minimum packet size    
    constexpr size_t MAX_PACKET_SIZE   = 1024 * 1024 * 1024;                    // 1GB maximum packet size
    /**
     * @brief Feature flag bit positions (Reserved for future optional features)
     * 
     * Note: Core features (CHECKSUMS, ROW_INDEX, ALIGNED, COMPRESSED) are now
     * mandatory in v1.0+ and no longer require flag bits.
     */
    enum class FileFlags : uint16_t {
        // All bits currently reserved for future optional features
        NONE                = 0x0000,                  ///< No special features enabled
        ZERO_ORDER_HOLD     = 0x0001,                  ///< Bit 0: Indicates this file uses zero-order hold compression (v1.2.0, will be always-on in v1.3.0)
        NO_FILE_INDEX       = 0x0002,                  ///< Bit 1: File has no index (sequential scan only, minimal footer) - for embedded platforms
        // Bits 2-15 reserved for future use
    };

    // Enable bitwise operations for FileFlags
    constexpr FileFlags operator|(FileFlags lhs, FileFlags rhs) {
        return static_cast<FileFlags>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
    }

    constexpr FileFlags operator&(FileFlags lhs, FileFlags rhs) {
        return static_cast<FileFlags>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
    }

    constexpr FileFlags operator~(FileFlags flag) {
        return static_cast<FileFlags>(~static_cast<uint16_t>(flag));
    }

    /** Column data type enumeration (stored as uint16_t in file) 
     * Ensure the order matches ValueType variant! As we rely on index() to match note isType()
     */
    enum class ColumnType : uint16_t {
        BOOL,
        UINT8,
        UINT16,
        UINT32,
        UINT64,
        INT8,
        INT16,
        INT32,
        INT64,
        FLOAT,
        DOUBLE,
        STRING
    };
    using std::string;

    /** Variant type representing all possible column value types
     *  Ensure the order matches ColumnType enum! As we rely on index() to match note isType()
     */
    using ValueType = std::variant< bool, 
                                    uint8_t, 
                                    uint16_t, 
                                    uint32_t, 
                                    uint64_t, 
                                    int8_t, 
                                    int16_t, 
                                    int32_t, 
                                    int64_t, 
                                    float, 
                                    double, 
                                    string>;

    inline bool isType(const ValueType& value, const ColumnType& type) {
        return value.index() == static_cast<size_t>(type);
    }

    // Convert C++ type to ColumnType enum
    template<typename T>
    static constexpr ColumnType toColumnType() {
        if      constexpr (std::is_same_v<T, bool>)     return ColumnType::BOOL;
        else if constexpr (std::is_same_v<T, int8_t>)   return ColumnType::INT8;
        else if constexpr (std::is_same_v<T, int16_t>)  return ColumnType::INT16;
        else if constexpr (std::is_same_v<T, int32_t>)  return ColumnType::INT32;
        else if constexpr (std::is_same_v<T, int64_t>)  return ColumnType::INT64;
        else if constexpr (std::is_same_v<T, uint8_t>)  return ColumnType::UINT8;
        else if constexpr (std::is_same_v<T, uint16_t>) return ColumnType::UINT16;
        else if constexpr (std::is_same_v<T, uint32_t>) return ColumnType::UINT32;
        else if constexpr (std::is_same_v<T, uint64_t>) return ColumnType::UINT64;
        else if constexpr (std::is_same_v<T, float>)    return ColumnType::FLOAT;
        else if constexpr (std::is_same_v<T, double>)   return ColumnType::DOUBLE;
        else if constexpr (std::is_same_v<T, string>)   return ColumnType::STRING;
        else static_assert(always_false<T>, "Unsupported type");
    };

    // Base template
    template<ColumnType Type>
    struct getTypeT {
        using type = void; // Invalid by default
    };

    // Specializations
    template<> struct getTypeT< ColumnType::BOOL   > { using type = bool;     };
    template<> struct getTypeT< ColumnType::UINT8  > { using type = uint8_t;  };
    template<> struct getTypeT< ColumnType::UINT16 > { using type = uint16_t; };
    template<> struct getTypeT< ColumnType::UINT32 > { using type = uint32_t; };
    template<> struct getTypeT< ColumnType::UINT64 > { using type = uint64_t; };
    template<> struct getTypeT< ColumnType::INT8   > { using type = int8_t;   };
    template<> struct getTypeT< ColumnType::INT16  > { using type = int16_t;  };
    template<> struct getTypeT< ColumnType::INT32  > { using type = int32_t;  };
    template<> struct getTypeT< ColumnType::INT64  > { using type = int64_t;  };
    template<> struct getTypeT< ColumnType::FLOAT  > { using type = float;    };
    template<> struct getTypeT< ColumnType::DOUBLE > { using type = double;   };
    template<> struct getTypeT< ColumnType::STRING > { using type = string;   };  

    inline ColumnType toColumnType(const ValueType& value) {
        return std::visit([](auto&& arg) -> ColumnType {
            using T = std::decay_t<decltype(arg)>;
            return toColumnType<T>();
        }, value);
    }

    // Helper functions for type conversion
    inline ColumnType toColumnType(const std::string& typeString) {
        if (typeString == "bool")   return ColumnType::BOOL;
        if (typeString == "uint8")  return ColumnType::UINT8;
        if (typeString == "uint16") return ColumnType::UINT16;
        if (typeString == "uint32") return ColumnType::UINT32;
        if (typeString == "uint64") return ColumnType::UINT64;
        if (typeString == "int8")   return ColumnType::INT8;
        if (typeString == "int16")  return ColumnType::INT16;
        if (typeString == "int32")  return ColumnType::INT32;
        if (typeString == "int64")  return ColumnType::INT64;
        if (typeString == "float")  return ColumnType::FLOAT;
        if (typeString == "double") return ColumnType::DOUBLE;
        return ColumnType::STRING; // default
    }

    inline std::string toString(ColumnType type) {
        switch (type) {
            case ColumnType::BOOL:   return "bool";
            case ColumnType::UINT8:  return "uint8";
            case ColumnType::UINT16: return "uint16";
            case ColumnType::UINT32: return "uint32";
            case ColumnType::UINT64: return "uint64";
            case ColumnType::INT8:   return "int8";
            case ColumnType::INT16:  return "int16";
            case ColumnType::INT32:  return "int32";
            case ColumnType::INT64:  return "int64";
            case ColumnType::FLOAT:  return "float";
            case ColumnType::DOUBLE: return "double";
            case ColumnType::STRING: return "string";
            default: return "UNKNOWN";
        }
    }

    // Stream operator for ColumnType
    inline std::ostream& operator<<(std::ostream& os, ColumnType type) {
        return os << toString(type);
    }

    /**
     * @brief Get default value for a given column data type
     * @param type The column data type
     * @return Default ValueType for the specified type
     */
    inline ValueType defaultValue(ColumnType type) {
        switch (type) {
            case ColumnType::BOOL:   return ValueType{bool{false}};
            case ColumnType::UINT8:  return ValueType{uint8_t{0} };
            case ColumnType::UINT16: return ValueType{uint16_t{0}};
            case ColumnType::UINT32: return ValueType{uint32_t{0}};
            case ColumnType::UINT64: return ValueType{uint64_t{0}};
            case ColumnType::INT8:   return ValueType{int8_t{0}  };
            case ColumnType::INT16:  return ValueType{int16_t{0} };
            case ColumnType::INT32:  return ValueType{int32_t{0} };
            case ColumnType::INT64:  return ValueType{int64_t{0} };
            case ColumnType::FLOAT:  return ValueType{float{0.0f}};
            case ColumnType::DOUBLE: return ValueType{double{0.0}};
            case ColumnType::STRING: return ValueType{string{}   };
            default: throw std::runtime_error("Unknown column type");
        }
    }

    template<typename Type>
    constexpr Type defaultValueT() {
        if constexpr (std::is_same_v<Type, bool>) {
            return bool{false};
        } else if constexpr (std::is_same_v<Type, uint8_t> ) {
            return uint8_t{0};
        } else if constexpr (std::is_same_v<Type, uint16_t>) {
            return uint16_t{0};
        } else if constexpr (std::is_same_v<Type, uint32_t>) {
            return uint32_t{0};
        } else if constexpr (std::is_same_v<Type, uint64_t>) {
            return uint64_t{0};
        } else if constexpr (std::is_same_v<Type, int8_t>  ) {
            return int8_t{0};
        } else if constexpr (std::is_same_v<Type, int16_t> ) {
            return int16_t{0};
        } else if constexpr (std::is_same_v<Type, int32_t> ) {
            return int32_t{0};
        } else if constexpr (std::is_same_v<Type, int64_t> ) {
            return int64_t{0}; 
        } else if constexpr (std::is_same_v<Type, float>   ) {
            return float{0.0f};
        } else if constexpr (std::is_same_v<Type, double>  ) {
            return double{0.0};
        } else if constexpr (std::is_same_v<Type, string>  ) {
            return string{};  // Empty string
        } else {
            static_assert(always_false<Type>, "Unsupported type for defaultValueT");
        }
    }

    using StringAddress = StringAddr<uint32_t>; // Default to 32bit version for now

    template<typename T>
    constexpr uint8_t binaryFieldLength() {
        if      constexpr (std::is_same_v<T, bool>    ) return sizeof(bool);
        else if constexpr (std::is_same_v<T, uint8_t> ) return sizeof(uint8_t);
        else if constexpr (std::is_same_v<T, uint16_t>) return sizeof(uint16_t);
        else if constexpr (std::is_same_v<T, uint32_t>) return sizeof(uint32_t);
        else if constexpr (std::is_same_v<T, uint64_t>) return sizeof(uint64_t);
        else if constexpr (std::is_same_v<T, int8_t>  ) return sizeof(int8_t);
        else if constexpr (std::is_same_v<T, int16_t> ) return sizeof(int16_t);
        else if constexpr (std::is_same_v<T, int32_t> ) return sizeof(int32_t);
        else if constexpr (std::is_same_v<T, int64_t> ) return sizeof(int64_t);
        else if constexpr (std::is_same_v<T, float>   ) return sizeof(float);
        else if constexpr (std::is_same_v<T, double>  ) return sizeof(double);
        else if constexpr (std::is_same_v<T, string>  ) return sizeof(StringAddress); // StringAddress
        else static_assert(always_false<T>, "Unsupported type");
    }

    // Helper function to get size for each column type
    constexpr uint8_t binaryFieldLength(ColumnType type) {
        switch (type) {
            case ColumnType::BOOL:   return sizeof(bool);
            case ColumnType::UINT8:  return sizeof(uint8_t);
            case ColumnType::UINT16: return sizeof(uint16_t);
            case ColumnType::UINT32: return sizeof(uint32_t);
            case ColumnType::UINT64: return sizeof(uint64_t);
            case ColumnType::INT8:   return sizeof(int8_t);
            case ColumnType::INT16:  return sizeof(int16_t);
            case ColumnType::INT32:  return sizeof(int32_t);
            case ColumnType::INT64:  return sizeof(int64_t);
            case ColumnType::FLOAT:  return sizeof(float);
            case ColumnType::DOUBLE: return sizeof(double);
            case ColumnType::STRING: return sizeof(StringAddress); // StringAddress
            default: throw std::runtime_error("Unknown column type");
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

    inline ValueType convertValueType(const ValueType &value, ColumnType targetType) {
        return std::visit([targetType](const auto& v) -> ValueType {
            using SrcType = std::decay_t<decltype(v)>;
            switch (targetType)
            {
            case ColumnType::BOOL:
                if constexpr (std::is_same_v<SrcType, bool>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, bool>) 
                    return static_cast<bool>(v);
            case ColumnType::UINT8:
                if constexpr (std::is_same_v<SrcType, uint8_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, uint8_t>) 
                    return static_cast<uint8_t>(v);
            case ColumnType::UINT16:
                if constexpr (std::is_same_v<SrcType, uint16_t>) 
                return v;
                else if constexpr (std::is_convertible_v<SrcType, uint16_t>) 
                    return static_cast<uint16_t>(v);
            case ColumnType::UINT32:
                if constexpr (std::is_same_v<SrcType, uint32_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, uint32_t>) 
                    return static_cast<uint32_t>(v);
            case ColumnType::UINT64:
                if constexpr (std::is_same_v<SrcType, uint64_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, uint64_t>) 
                    return static_cast<uint64_t>(v);
            case ColumnType::INT8:
                if constexpr (std::is_same_v<SrcType, int8_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, int8_t>) 
                    return static_cast<int8_t>(v);
            case ColumnType::INT16:
                if constexpr (std::is_same_v<SrcType, int16_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, int16_t>) 
                    return static_cast<int16_t>(v);
            case ColumnType::INT32:
                if constexpr (std::is_same_v<SrcType, int32_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, int32_t>) 
                    return static_cast<int32_t>(v);
            case ColumnType::INT64:
                if constexpr (std::is_same_v<SrcType, int64_t>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, int64_t>) 
                    return static_cast<int64_t>(v);
            case ColumnType::FLOAT:
                if constexpr (std::is_same_v<SrcType, float>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, float>) 
                    return static_cast<float>(v);
            case ColumnType::DOUBLE:
                if constexpr (std::is_same_v<SrcType, double>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, double>) 
                    return static_cast<double>(v);
            case ColumnType::STRING:
                if constexpr (std::is_same_v<SrcType, std::string>) 
                    return v;
                else if constexpr (std::is_convertible_v<SrcType, std::string>) 
                    return static_cast<std::string>(v);
            default:
                break;
            }
            return defaultValue(targetType);
        }, value);
    }

    
    namespace detail {
        // Validation helper: allows templates to check if type is std::variant
        template<typename T> struct is_variant : std::false_type {};
        template<typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};

        template<typename T>
        inline constexpr bool is_variant_v = is_variant<T>::value;

        // Validation helper: Checks if T is an alternative in std::variant<Types...>
        template <typename T, typename Variant> struct is_in_variant;

        template <typename T, typename... Ts>
        struct is_in_variant<T, std::variant<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

        template <typename T, typename Variant>
        constexpr bool is_in_variant_v = is_in_variant<T, Variant>::value;
    }
    
    
} // namespace bcsv