/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
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
#include <array>
#include <bit>
#include <variant>
#include <limits>
#include <cstring>
#include <stdexcept>
#include <type_traits>

// Include auto-generated version information.
// Use angle-bracket form so the compiler searches -I paths (including
// CMAKE_BINARY_DIR/include) with the bcsv/ prefix, which is required
// when building from a clean clone where the header lives only in the
// build tree at <build>/include/bcsv/version_generated.h.
#include <bcsv/version_generated.h>

// Force-inline macro for hot-path accessors (get/set/ref/visit).
// Without this, GCC's inlining heuristic may arbitrarily chose which
// template instantiations to inline, causing inconsistent benchmark
// results and sub-optimal performance in real workloads.
#if defined(_MSC_VER)
    #define BCSV_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define BCSV_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
    #define BCSV_ALWAYS_INLINE inline
#endif

namespace bcsv {
    
    // Configuration
    constexpr bool RANGE_CHECKING = true;
#ifndef NDEBUG
    constexpr bool DEBUG_OUTPUTS  = true;       // sends information to std::cerr and std::cout to support development and debugging
#else
    constexpr bool DEBUG_OUTPUTS  = false;      // disabled in release builds
#endif             

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
    constexpr size_t MAX_STRING_LENGTH = std::numeric_limits<uint16_t>::max(); // Maximum length of string data (wire format uses uint16_t lengths)
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
        STREAM_MODE         = 0x0004,                  ///< Bit 2: File uses stream mode (no packets/checksums/footer). Default (0) = packet mode.
        BATCH_COMPRESS      = 0x0008,                  ///< Bit 3: Packet payload is batch-compressed as a single LZ4 block (async double-buffered I/O).
        DELTA_ENCODING      = 0x0010,                  ///< Bit 4: Delta + VLE row encoding (type-grouped, combined header codes, ZoH/FoC/VLE for numerics).
        // Bits 5-15 reserved for future use
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

    /**
     * @brief Identifies the file-level codec used for framing, compression and I/O.
     *
     * Each ID maps to a concrete FileCodec class.  The ID is derived from
     * FileHeader fields (compression_level, FileFlags::STREAM_MODE) — it is
     * NOT stored explicitly in the file.
     *
     * Naming: FileCodec + Structure + [Compression] + Version
     */
    enum class FileCodecId : uint8_t {
        STREAM_001,             ///< Stream-Raw: no packets, no compression, per-row XXH32 checksums
        STREAM_LZ4_001,         ///< Stream-LZ4: no packets, streaming LZ4 compression, per-row XXH32 checksums
        PACKET_001,             ///< Packet-Raw: packet framing + checksums, no compression
        PACKET_LZ4_001,         ///< Packet-LZ4-Streaming: packet framing + streaming LZ4 (v1.3.0 default)
        PACKET_LZ4_BATCH_001,   ///< Packet-LZ4-Batch: packet framing + batch LZ4, async double-buffered I/O
    };

    /**
     * @brief Derive the FileCodecId from file header fields.
     *
     * Uses compression_level and the STREAM_MODE / BATCH_COMPRESS flags to
     * select the codec.  Batch-LZ4 is selected when both BATCH_COMPRESS is
     * set and compression is enabled.
     */
    inline constexpr FileCodecId resolveFileCodecId(uint8_t compressionLevel, FileFlags flags) noexcept {
        const bool stream = (flags & FileFlags::STREAM_MODE) != FileFlags::NONE;
        const bool compressed = compressionLevel > 0;
        const bool batch = (flags & FileFlags::BATCH_COMPRESS) != FileFlags::NONE;
        if (stream) {
            return compressed ? FileCodecId::STREAM_LZ4_001 : FileCodecId::STREAM_001;
        } else if (batch && compressed) {
            return FileCodecId::PACKET_LZ4_BATCH_001;
        } else {
            return compressed ? FileCodecId::PACKET_LZ4_001 : FileCodecId::PACKET_001;
        }
    }

    /** Column data type enumeration (stored as uint8_t in file) 
     * Ensure the order matches ValueType variant! As we rely on index() to match note isType()
     */
    enum class ColumnType : uint8_t {
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
        STRING,
        VOID = 255
    };
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
                                    std::string>;

    inline bool isType(const ValueType& value, const ColumnType& type) {
        return value.index() == static_cast<size_t>(type);
    }

    // Helper template for static_assert
    template<typename T>
    constexpr bool ALWAYS_FALSE = false;

    // Convert C++ type to ColumnType enum
    template<typename T>
    constexpr ColumnType toColumnType() {
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
        else if constexpr (std::is_same_v<T, std::string>)   return ColumnType::STRING;
        else if constexpr (std::is_same_v<T, std::string_view>) return ColumnType::STRING;
        else static_assert(ALWAYS_FALSE<T>, "Unsupported type");
    };

    // Base template
    template<ColumnType Type>
    struct GetTypeT {
        using type = void; // Invalid by default
    };

    // Specializations
    template<> struct GetTypeT< ColumnType::BOOL   > { using type = bool;     };
    template<> struct GetTypeT< ColumnType::UINT8  > { using type = uint8_t;  };
    template<> struct GetTypeT< ColumnType::UINT16 > { using type = uint16_t; };
    template<> struct GetTypeT< ColumnType::UINT32 > { using type = uint32_t; };
    template<> struct GetTypeT< ColumnType::UINT64 > { using type = uint64_t; };
    template<> struct GetTypeT< ColumnType::INT8   > { using type = int8_t;   };
    template<> struct GetTypeT< ColumnType::INT16  > { using type = int16_t;  };
    template<> struct GetTypeT< ColumnType::INT32  > { using type = int32_t;  };
    template<> struct GetTypeT< ColumnType::INT64  > { using type = int64_t;  };
    template<> struct GetTypeT< ColumnType::FLOAT  > { using type = float;    };
    template<> struct GetTypeT< ColumnType::DOUBLE > { using type = double;   };
    template<> struct GetTypeT< ColumnType::STRING > { using type = std::string; };

    inline constexpr ColumnType toColumnType(const ValueType& value) {
        return std::visit([](auto&& arg) -> ColumnType {
            using T = std::decay_t<decltype(arg)>;
            return toColumnType<T>();
        }, value);
    }

    // Helper functions for type conversion
    inline constexpr ColumnType toColumnType(const std::string& typeString) {
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
        if (typeString == "string") return ColumnType::STRING;
        if (typeString == "void" || typeString.empty())  return ColumnType::VOID;
        throw std::invalid_argument("Unknown type: " + typeString);  // Fail loud
    }

    inline constexpr size_t alignOf(ColumnType type) {
        switch (type) {
            case ColumnType::BOOL:   return alignof(bool);
            case ColumnType::UINT8:  return alignof(uint8_t);
            case ColumnType::UINT16: return alignof(uint16_t);
            case ColumnType::UINT32: return alignof(uint32_t);
            case ColumnType::UINT64: return alignof(uint64_t);
            case ColumnType::INT8:   return alignof(int8_t);
            case ColumnType::INT16:  return alignof(int16_t);
            case ColumnType::INT32:  return alignof(int32_t);
            case ColumnType::INT64:  return alignof(int64_t);
            case ColumnType::FLOAT:  return alignof(float);
            case ColumnType::DOUBLE: return alignof(double);
            case ColumnType::STRING: return alignof(std::string);
            default: return 1; // Default to 1 for unknown types
        }
    }

    inline constexpr size_t sizeOf(ColumnType type) {
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
            case ColumnType::STRING: return sizeof(std::string);
            default: return 0; // Default to 0 for unknown types
        }
    }

    inline constexpr std::string_view toString(ColumnType type) {
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
    constexpr ValueType defaultValue(ColumnType type) {
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
            case ColumnType::STRING: return ValueType{std::string{}};
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
        } else if constexpr (std::is_same_v<Type, std::string>) {
            return std::string{};  // Empty string
        } else {
            static_assert(ALWAYS_FALSE<Type>, "Unsupported type for defaultValueT");
        }
    }
        
    template<typename T>
    constexpr uint8_t wireSizeOf() {
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
        else if constexpr (std::is_same_v<T, std::string>) return sizeof(uint16_t);    // wire format: uint16_t length prefix
        else static_assert(ALWAYS_FALSE<T>, "Unsupported type");
    }

    // Helper function to get size for each column type
    constexpr uint8_t wireSizeOf(ColumnType type) {
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
            case ColumnType::STRING: return sizeof(uint16_t);    // wire format: uint16_t length prefix
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

    // convertValueType removed — zero callers in codebase (dead code since 2026-02-25 review).
    // Retrievable from git history if ever needed.

    template<typename T>
    inline T unalignedRead(const void *src) {
        static_assert(std::is_trivially_copyable_v<T>, "unalignedRead requires trivially copyable type");
        std::array<std::byte, sizeof(T)> raw{};
        std::memcpy(raw.data(), src, sizeof(T));
        return std::bit_cast<T>(raw);
    }

    template<typename T>
    inline void unalignedWrite(void *dst, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "unalignedWrite requires trivially copyable type");
        const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
        std::memcpy(dst, raw.data(), sizeof(T));
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

        // Helper to check if a type is supported by BCSV (primitive types only, no string)
        template<typename T>
        struct is_primitive : std::false_type {};

        template<> struct is_primitive<bool>     : std::true_type {};
        template<> struct is_primitive<int8_t>   : std::true_type {};
        template<> struct is_primitive<int16_t>  : std::true_type {};
        template<> struct is_primitive<int32_t>  : std::true_type {};
        template<> struct is_primitive<int64_t>  : std::true_type {};
        template<> struct is_primitive<uint8_t>  : std::true_type {};
        template<> struct is_primitive<uint16_t> : std::true_type {};
        template<> struct is_primitive<uint32_t> : std::true_type {};
        template<> struct is_primitive<uint64_t> : std::true_type {};
        template<> struct is_primitive<float>    : std::true_type {};
        template<> struct is_primitive<double>   : std::true_type {};

        template<typename T>
        inline constexpr bool is_primitive_v = is_primitive<T>::value;

        // Helper to check if a type is a string-like type
        template<typename T>
        struct is_string_like : std::false_type {};

        template<> struct is_string_like<std::string> : std::true_type {};
        template<> struct is_string_like<std::string_view> : std::true_type {};
        //template<> struct is_string_like<const char*> : std::true_type {};

        template<typename T>
        inline constexpr bool is_string_like_v = is_string_like<T>::value;

        // C++20 Concept: Types that can be assigned to BCSV columns
        // Must be defined AFTER the helper traits it uses
        template<typename T>
        concept BcsvAssignable = 
            is_primitive_v<std::decay_t<T>> ||
            is_string_like_v<std::decay_t<T>> ||
            (std::is_convertible_v<std::decay_t<T>, bool> && !std::is_arithmetic_v<std::decay_t<T>>);  // For std::_Bit_reference
    }    

} // namespace bcsv