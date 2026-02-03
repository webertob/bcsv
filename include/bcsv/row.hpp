/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file row.hpp
 * @brief Binary CSV (BCSV) Library - Row implementations
 * 
 * This file contains the implementations for the Row and RowStatic classes.
 */

#include "bcsv/bitset.hpp"
#include "bcsv/definitions.h"
#include "row.h"
#include "layout.h"
#include "byte_buffer.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <cassert>
#include <variant>
#include <type_traits>
#include "string_addr.h"

namespace bcsv {

    // ========================================================================
    // Row Implementation
    // ========================================================================

    inline Row::Row(const Layout &layout, bool trackChangesEnabled)
        : layout_(layout), offsets_(layout.columnCount()), offset_var_(0), data_(),  changes_()
    {
        size_t columnCount = layout.columnCount();

        // Build row's internal data structures based on layout
        // Calculate offsets considering alignment requirements
        uint32_t offset = 0;
        for(size_t i = 0; i < columnCount; ++i) {
            size_t length= 0;
            size_t alignment = 0;
            ColumnType type = layout_.columnType(i);
            switch(type) {
                case ColumnType::BOOL:
                    length = sizeof(bool);    
                    alignment = alignof(bool);
                    break;
                case ColumnType::UINT8:
                    length = sizeof(uint8_t);    
                    alignment = alignof(uint8_t);
                    break;
                case ColumnType::UINT16:
                    length = sizeof(uint16_t);
                    alignment = alignof(uint16_t);
                    break;
                case ColumnType::UINT32:
                    length = sizeof(uint32_t);
                    alignment = alignof(uint32_t);
                    break;
                case ColumnType::UINT64:
                    length = sizeof(uint64_t);
                    alignment = alignof(uint64_t);
                    break;
                case ColumnType::INT8:
                    length = sizeof(int8_t);
                    alignment = alignof(int8_t);
                    break;
                case ColumnType::INT16:
                    length = sizeof(int16_t);
                    alignment = alignof(int16_t);
                    break;
                case ColumnType::INT32:
                    length = sizeof(int32_t);
                    alignment = alignof(int32_t);
                    break;
                case ColumnType::INT64:
                    length = sizeof(int64_t);
                    alignment = alignof(int64_t);
                    break;
                case ColumnType::FLOAT:
                    length = sizeof(float);
                    alignment = alignof(float);
                    break;
                case ColumnType::DOUBLE:
                    length = sizeof(double);
                    alignment = alignof(double);
                    break;
                case ColumnType::STRING:
                    length = sizeof(std::string);
                    alignment = alignof(std::string);
                    break;
                default: [[unlikely]]
                    throw std::runtime_error("Unknown column type");
            }
            // align offset to match type's alignment requirement
            // Assumes alignment is a power of 2, which is true for all standard types
            offset = (offset + (alignment - 1)) & ~(alignment - 1);
            offsets_[i] = offset;
            offset += length; //advance offset for next column
        }

        // construct all types within data_ using their default values
        data_.resize(offset);
        for(size_t i = 0; i < columnCount; ++i) {
            uint32_t offset = offsets_[i];
            std::byte* ptr = data_.data() + offset;
            ColumnType type = layout_.columnType(i);
            switch (type) {
                case ColumnType::BOOL:
                    new (ptr) bool(defaultValueT<bool>());
                    break;
                case ColumnType::UINT8:
                    new (ptr) uint8_t(defaultValueT<uint8_t>());
                    break;
                case ColumnType::UINT16:
                    new (ptr) uint16_t(defaultValueT<uint16_t>());
                    break;
                case ColumnType::UINT32:
                    new (ptr) uint32_t(defaultValueT<uint32_t>());  
                    break;
                case ColumnType::UINT64:
                    new (ptr) uint64_t(defaultValueT<uint64_t>());
                    break;
                case ColumnType::INT8:
                    new (ptr) int8_t(defaultValueT<int8_t>());
                    break;
                case ColumnType::INT16:
                    new (ptr) int16_t(defaultValueT<int16_t>());
                    break;
                case ColumnType::INT32:
                    new (ptr) int32_t(defaultValueT<int32_t>());
                    break;
                case ColumnType::INT64:
                    new (ptr) int64_t(defaultValueT<int64_t>());
                    break;
                case ColumnType::FLOAT:
                    new (ptr) float(defaultValueT<float>());
                    break;  
                case ColumnType::DOUBLE:
                    new (ptr) double(defaultValueT<double>());
                    break;  
                case ColumnType::STRING:
                    new (ptr) std::string(defaultValueT<std::string>());
                    break;
                default: [[unlikely]]
                    throw std::runtime_error("Unknown column type");
            }
            
        }        
        trackChanges(trackChangesEnabled);

        // calculate offset_var_ (wire format) to accelerate serialization
        offset_var_ = 0; // start of variable section (string content), within flat binary buffer
        for(size_t i = 0; i < columnCount; ++i) {
            ColumnType type = layout_.columnType(i);
            offset_var_ += binaryFieldLength(type);
        }
    }

    inline Row::Row(const Row& other)
        : layout_(other.layout_)
        , offsets_(other.offsets_)
        , offset_var_(other.offset_var_)
        , data_(other.data_)
        , changes_(other.changes_)
    {
        // Now we need to perform a deep-copy for strings, as they allocate memory themselves
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            if(type == ColumnType::STRING) {
                // Strings need deep copy
                std::byte* ptrThis = data_.data() + offsets_[i];
                const std::byte* ptrOther = other.data_.data() + offsets_[i];
                new (ptrThis) std::string(*reinterpret_cast<const std::string*>(ptrOther)); // placement new with copy constructor
            }
        }
    }

    Row& Row::operator=(const Row& other) noexcept
    {
        if (this != &other) {
            // Destroy existing strings before overwriting data_
            for(size_t i = 0; i < layout_.columnCount(); ++i) {
                if(layout_.columnType(i) == ColumnType::STRING) {
                    std::byte* ptr = data_.data() + offsets_[i];
                    reinterpret_cast<std::string*>(ptr)->~basic_string();
                }
            }

            // Copy layout and metadata
            layout_ = other.layout_;
            offsets_ = other.offsets_;
            offset_var_ = other.offset_var_;
            data_ = other.data_;  // Shallow copy (includes string objects)
            changes_ = other.changes_;

            // Reconstruct strings for deep copy
            for(size_t i = 0; i < layout_.columnCount(); ++i) {
                if(layout_.columnType(i) == ColumnType::STRING) {
                    std::byte* ptrThis = data_.data() + offsets_[i];
                    const std::byte* ptrOther = other.data_.data() + offsets_[i];
                    new (ptrThis) std::string(*reinterpret_cast<const std::string*>(ptrOther)); // placement new with copy constructor
                }
            }
        }
        return *this;
    }

    inline Row::~Row()
    {
        // Safety check for moved-from objects (data_ is empty after move)
        if(data_.empty()) 
            return;

        // Manually call destructors for those types that need it (e.g. strings as they allocate memory themselves)
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            uint32_t offset = offsets_[i];
            std::byte* ptr = data_.data() + offset;
            ColumnType type = layout_.columnType(i);
            if(type == ColumnType::STRING) {
                // Strings need destructor called
                reinterpret_cast<std::string*>(ptr)->~string();
            }
        }
    }

    /** Clear the row to its default state (default values) */
    inline void Row::clear()
    {
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            uint32_t offset = offsets_[i];
            std::byte* ptr = data_.data() + offset;
            ColumnType type = layout_.columnType(i);
            switch (type) {
                case ColumnType::BOOL:
                    *reinterpret_cast<bool*>(ptr) = defaultValueT<bool>();
                    break;
                case ColumnType::UINT8:
                    *reinterpret_cast<uint8_t*>(ptr) = defaultValueT<uint8_t>();
                    break;
                case ColumnType::UINT16:
                    *reinterpret_cast<uint16_t*>(ptr) = defaultValueT<uint16_t>();
                    break;
                case ColumnType::UINT32:
                    *reinterpret_cast<uint32_t*>(ptr) = defaultValueT<uint32_t>();  
                    break;
                case ColumnType::UINT64:
                    *reinterpret_cast<uint64_t*>(ptr) = defaultValueT<uint64_t>();
                    break;
                case ColumnType::INT8:
                    *reinterpret_cast<int8_t*>(ptr) = defaultValueT<int8_t>();
                    break;
                case ColumnType::INT16:
                    *reinterpret_cast<int16_t*>(ptr) = defaultValueT<int16_t>();
                    break;
                case ColumnType::INT32:
                    *reinterpret_cast<int32_t*>(ptr) = defaultValueT<int32_t>();
                    break;
                case ColumnType::INT64:
                    *reinterpret_cast<int64_t*>(ptr) = defaultValueT<int64_t>();
                    break;
                case ColumnType::FLOAT:
                    *reinterpret_cast<float*>(ptr) = defaultValueT<float>();
                    break;  
                case ColumnType::DOUBLE:
                    *reinterpret_cast<double*>(ptr) = defaultValueT<double>();
                    break;  
                case ColumnType::STRING:
                    *reinterpret_cast<std::string*>(ptr) = defaultValueT<std::string>();
                    break;
                default: [[unlikely]]
                    throw std::runtime_error("Unknown column type");
            }
        }
        if(tracksChanges()) {
            changes_.set(); // mark everything as changed
        }
    }
    
    // =========================================================================
    // 1. RAW POINTER ACCESS (caller must ensure type safety)
    // =========================================================================
    inline const void* Row::get(size_t index) const {
        if(index >= layout_.columnCount()) {
            return nullptr;
        }
        size_t offset = offsets_[index];
        const void* ptr = data_.data() + offset;

        // Debug checks for safety
#ifndef NDEBUG
        ColumnType type = layout_.columnType(index);
        size_t size  = 1;
        size_t align = 1;

        switch(type) {
            case ColumnType::BOOL:   size = sizeof(bool); align = alignof(bool); break;
            case ColumnType::UINT8:  size = sizeof(uint8_t); align = alignof(uint8_t); break;
            case ColumnType::UINT16: size = sizeof(uint16_t); align = alignof(uint16_t); break;
            case ColumnType::UINT32: size = sizeof(uint32_t); align = alignof(uint32_t); break;
            case ColumnType::UINT64: size = sizeof(uint64_t); align = alignof(uint64_t); break;
            case ColumnType::INT8:   size = sizeof(int8_t); align = alignof(int8_t); break;
            case ColumnType::INT16:  size = sizeof(int16_t); align = alignof(int16_t); break;
            case ColumnType::INT32:  size = sizeof(int32_t); align = alignof(int32_t); break;
            case ColumnType::INT64:  size = sizeof(int64_t); align = alignof(int64_t); break;
            case ColumnType::FLOAT:  size = sizeof(float); align = alignof(float); break;
            case ColumnType::DOUBLE: size = sizeof(double); align = alignof(double); break;
            case ColumnType::STRING: size = sizeof(std::string); align = alignof(std::string); break;
            default: break;
        }

        assert(offset + size <= data_.size() && "Buffer overflow");
        assert(reinterpret_cast<uintptr_t>(ptr) % align == 0 && "Alignment violation");
#endif

        return ptr;
    }
   
    // =========================================================================
    // 2. TYPED REFERENCE ACCESS (Row Only)
    // Fastest and type-safe access to single column value. Ensures type matches exactly.
    // =========================================================================
    template<typename T>
    inline const T& Row::get(size_t index) const {
        // Use void* get for bounds check
        const void* ptr = get(index); 
        if (!ptr) {
            throw std::out_of_range("Row::get: Invalid column index " + std::to_string(index));
        }
        
        // Strict Type Check
        ColumnType actualType = layout_.columnType(index);
        ColumnType requestedType = toColumnType<T>();
        if(requestedType != actualType) {
             // More informative error message
             std::string msg = "Row::get<T> Type mismatch at index " + std::to_string(index) + 
                               ". Requested: " + std::string(toString(requestedType)) + 
                               ", Actual: " + std::string(toString(actualType));
             throw std::runtime_error(msg);
        }

        // Safe strict aliasing because we constructed the object at this address
        return *reinterpret_cast<const T*>(ptr);
    }

    /** Vectorized access to multiple columns of same type. Types must match exactly */
    template<typename T>
    inline void Row::get(size_t index, std::span<T> &dst) const
    {
        static_assert(std::is_arithmetic_v<T>, "vectorized get() supports arithmetic types only");
        constexpr ColumnType targetType = toColumnType<T>();

        if (RANGE_CHECKING) {
            // combined range (touch each column) and type check
            for (size_t i = 0; i < dst.size(); ++i) {
                const ColumnType &sourceType = layout_.columnType(index + i);
                if (targetType != sourceType) [[unlikely]]{
                    throw std::runtime_error("vectorized get() types must match exactly");
                }
            }
        }

        // All type checks passed, safe to get values
        // Exploit the fact that types are aligned within the row data_ and that we don't have to expect padding bytes between the values. Thus we can do a fast copy.
        memcpy(dst.data(), data_.data() + offsets_[index], dst.size() * sizeof(T));
    }

    // =========================================================================
    // 3. FLEXIBLE / CONVERTING ACCESS (Best for Shared Interface)
    // Type safe with implicit conversions
    // =========================================================================
    template<typename T>
    inline bool Row::get_as(size_t index, T &dst) const {
        const void* ptr = get(index);
        if (!ptr) [[unlikely]] {
             return false;
        }

        // Functor to check compatibility and assign.
        auto try_assign = [&]<typename SrcType>() -> bool {
            // Check if C++ allows assignment (e.g. int = int8_t)
            if constexpr (std::is_assignable_v<T&, SrcType>) {
                // Validate Alignment (Debug only)
#ifndef NDEBUG
                assert(reinterpret_cast<uintptr_t>(ptr) % alignof(SrcType) == 0 && "Memory alignment violation");
#endif
                // Read source type and assign to destination (implicit C++ conversion)
                dst = *reinterpret_cast<const SrcType*>(ptr); 
                return true;
            } else {
                return false;
            }
        };

        ColumnType type = layout_.columnType(index);
        switch(type) {
            case ColumnType::BOOL:   return try_assign.template operator()<bool>();
            case ColumnType::UINT8:  return try_assign.template operator()<uint8_t>();
            case ColumnType::UINT16: return try_assign.template operator()<uint16_t>();
            case ColumnType::UINT32: return try_assign.template operator()<uint32_t>();
            case ColumnType::UINT64: return try_assign.template operator()<uint64_t>();
            case ColumnType::INT8:   return try_assign.template operator()<int8_t>();
            case ColumnType::INT16:  return try_assign.template operator()<int16_t>();
            case ColumnType::INT32:  return try_assign.template operator()<int32_t>();
            case ColumnType::INT64:  return try_assign.template operator()<int64_t>();
            case ColumnType::FLOAT:  return try_assign.template operator()<float>();
            case ColumnType::DOUBLE: return try_assign.template operator()<double>();
            
            case ColumnType::STRING: {
                // String objects are constructed in place in data_
                const std::string& str = *reinterpret_cast<const std::string*>(ptr);
                
                if constexpr (std::is_same_v<T, std::string>) {
                    dst = str; // deep copy
                    return true;
                } else if constexpr (std::is_same_v<T, std::string_view>) {
                    dst = std::string_view(str); // zero-copy view (careful with lifetime!)
                    return true;
                } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                    dst = std::span<const char>(str); // zero-copy span
                    return true;
                } else if constexpr (std::is_assignable_v<T&, std::string>) {
                     dst = str; // fallback for other assignable types (e.g. std::filesystem::path)
                     return true;
                } else {
                    return false;
                }
            }
            default:
                return false;
        }
    }

    /** Set the value at the specified column index, return false if type mismatch */
    template<typename T>
    inline bool Row::set(size_t index, const T& value) {
        using DecayedT = std::decay_t<T>;

        // unwrap variant types incl. ValueType
        if constexpr (detail::is_variant_v<DecayedT>) {
            return std::visit([this, index](auto&& v) -> bool { 
                return this->set(index, v); 
            }, value);
        } else if constexpr (std::is_same_v<DecayedT, std::span<char>> || std::is_same_v<DecayedT, std::span<const char>>) {
            // treat C-style strings and char spans as std::string_view
            return this->set(index, std::string_view(value.data(), value.size()));
        }

        if(index >= layout_.columnCount()) [[unlikely]] return false;

        ColumnType type = layout_.columnType(index);
        std::byte* ptr = data_.data() + offsets_[index];

        // Specific handling for STRING to support truncation and arithmetic conversion
        if (type == ColumnType::STRING) {
            std::string* strPtr = reinterpret_cast<std::string*>(ptr);

            // 1. Efficient View Conversion (string, string_view, const char*)
            if constexpr (std::is_convertible_v<T, std::string_view>) {
                std::string_view sv = value;
                if (sv.size() > MAX_STRING_LENGTH) {
                    sv = sv.substr(0, MAX_STRING_LENGTH);
                }
                if (*strPtr != sv) {
                    strPtr->assign(sv);
                    if(tracksChanges()) changes_.set(index);
                }
                return true;
            } 
            // 2. Arithmetic Conversion (bool, int, float -> "123", "3.14")
            else if constexpr (std::is_arithmetic_v<DecayedT>) {
                std::string strVal = std::to_string(value); // does not need truncation, as it can nevery grow beyond MAX_STRING_LENGTH
                if (*strPtr != strVal) {
                    *strPtr = std::move(strVal);
                    if(tracksChanges()) changes_.set(index);
                }
                return true;
            }
            // 3. Direct Assignment (captures char, initializer_list, etc.)
            else if constexpr (std::is_assignable_v<std::string&, const T&>) {
                if (*strPtr != value) {
                    *strPtr = value;
                    if(tracksChanges()) changes_.set(index);
                }
                return true;
            }
            return false;
        }

        // Generic handler for arithmetic types
        auto try_assign = [&]<typename ColType>() -> bool {
             if constexpr (std::is_assignable_v<ColType&, const DecayedT&>) {
                ColType* dst = reinterpret_cast<ColType*>(ptr);
                // Cast value to ColType before comparison to prevent false positives 
                // in change tracking due to type mismatch (e.g. 5 vs 5.1)
                // Note: We use static_cast for arithmetic types which is safe here 
                // because is_assignable checked compatibility.
                if (*dst != static_cast<ColType>(value)) {
                    // only assign if value has changed
                    *dst = static_cast<ColType>(value);
                    if(tracksChanges()) changes_.set(index);
                 }
                 return true;
             }
             return false;
        };

        switch(type) {
            case ColumnType::BOOL:   return try_assign.template operator()<bool>();
            case ColumnType::UINT8:  return try_assign.template operator()<uint8_t>();
            case ColumnType::UINT16: return try_assign.template operator()<uint16_t>();
            case ColumnType::UINT32: return try_assign.template operator()<uint32_t>();
            case ColumnType::UINT64: return try_assign.template operator()<uint64_t>();
            case ColumnType::INT8:   return try_assign.template operator()<int8_t>();
            case ColumnType::INT16:  return try_assign.template operator()<int16_t>();
            case ColumnType::INT32:  return try_assign.template operator()<int32_t>();
            case ColumnType::INT64:  return try_assign.template operator()<int64_t>();
            case ColumnType::FLOAT:  return try_assign.template operator()<float>();
            case ColumnType::DOUBLE: return try_assign.template operator()<double>();
            default: return false;
        }
    }

    /** Vectorized set of multiple columns of same type */
    template<typename T>
    inline void Row::set(size_t index, std::span<const T> values)
    {
        static_assert(std::is_arithmetic_v<T>, "Only primitive arithmetic types are supported for vectorized set");

        if constexpr (RANGE_CHECKING) {
            // combined range (touch each column) and type check
            for (size_t i = 0; i < values.size(); ++i) {
                const ColumnType &targetType = layout_.columnType(index + i);
                if (toColumnType<T>() != targetType) [[unlikely]]{
                    throw std::runtime_error("vectorized set() types must match exactly");
                }
            }
        }

        // Alignment of data is guaranteed by Row Constructor and Row layout (immutable layout for row's lifetime)
        auto dst = reinterpret_cast<T*>(data_.data() + offsets_[index]);
        if(tracksChanges()) {
            // Element-wise check and set to track changes precisely
            for (size_t i = 0; i < values.size(); ++i) {
                if (dst[i] != values[i]) {
                    dst[i] = values[i];
                    changes_.set(index + i);
                }
            }
        } else {
            std::memcpy(dst, values.data(), values.size() * sizeof(T));
        }
    }

    /** Serialize the row into the provided buffer, appending data to the end of the buffer.
    * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
    * @return A span pointing to the serialized row data within the buffer.
    */
    inline std::span<std::byte> Row::serializeTo(ByteBuffer& buffer) const  {
        size_t  off_row = buffer.size();            // offset to the begin of this row
        size_t  off_fix = buffer.size();            // offset within the fixed section of the buffer. Starts at the begin of this row (both fixed and variable data)
        size_t  off_var = offset_var_;  // offset within the variable section of the buffer. Starts just after the fixed section for this row.
        
        // Resize to hold fixed section
        buffer.resize(off_fix + off_var);

        size_t count = layout_.columnCount();
        for(size_t i=0; i<count; ++i) {
            ColumnType type = layout_.columnType(i);
            const std::byte* ptr = data_.data() + offsets_[i];
            
            if (type == ColumnType::STRING) {
                const std::string& str = *reinterpret_cast<const std::string*>(ptr);
                size_t strLen = std::min(str.length(), MAX_STRING_LENGTH);
                
                // Address relative to start of row
                // Variable data starts at current end of buffer
                StringAddr strAddr(off_var, static_cast<uint16_t>(strLen));
                
                // Write StringAddr to fixed section
                std::byte* dst = buffer.data() + off_fix;
                std::memcpy(dst, &strAddr, sizeof(strAddr));
                off_fix += sizeof(strAddr);  // advance fixed offset
                
                // Append string data to variable section
                buffer.resize(off_var + strLen);
                dst = buffer.data() + off_var;
                std::memcpy(dst, str.data(), strLen);
                off_var += strLen;           // advance variable offset
                
            } else {
                size_t len = binaryFieldLength(type);
                std::byte* dst = buffer.data() + off_fix;
                std::memcpy(dst, ptr, len);
                off_fix += len; // advance fixed offset
            }
        }
        return {buffer.data() + off_row, buffer.size() - off_row};
    }

    inline void Row::deserializeFrom(const std::span<const std::byte> buffer)
    {
        size_t off_fix = 0;           // offset within the fixed section of the buffer. Starts at the begin of this row. We expect that span only contains data for a single row.
        size_t off_var = offset_var_; // offset within the variable section of the buffer. Starts just after the fixed section for this row.
        
        // Safety check: ensure buffer is large enough to contain fixed section
        if (off_var > buffer.size()) [[unlikely]] {
            throw std::runtime_error("Row::deserializeFrom failed as buffer is too short");
        }
        
        size_t count = layout_.columnCount();
        for(size_t i = 0; i < count; ++i) {
            ColumnType          type    = layout_.columnType(i);
            const std::byte*    src     = buffer.data() + off_fix;
            size_t              len     = binaryFieldLength(type);
            std::byte*          dst     = data_.data() + offsets_[i];
            
            if (type == ColumnType::STRING) {
                StringAddr strAddr;
                std::memcpy(&strAddr, src, sizeof(strAddr));
                auto [strOff, strLen] = strAddr.unpack();
                
                if (strOff + strLen > buffer.size()) {
                    throw std::runtime_error("Row::deserializeFrom String payload extends beyond buffer");
                }
                
                std::string& str = *reinterpret_cast<std::string*>(dst);
                str.assign(reinterpret_cast<const char*>(buffer.data() + strOff), strLen);
            } else {
                 std::memcpy(dst, src, len);
            }
            off_fix += len; // advance fixed offset
        }
    }

    /** Serialize the row into the provided buffer using Zero-Order-Hold (ZoH) compression.
     * Only the columns that have changed (as indicated by the change tracking bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change bitset.
     * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
     * @return A span pointing to the serialized row data within the buffer.
     */
    inline std::span<std::byte> Row::serializeToZoH(ByteBuffer& buffer) const {
        assert(tracksChanges() && "tracking must be enabled for ZoH serialization");

        // reserve space to store change bitset
        size_t off_row = buffer.size();      // offset to the begin of this row

        if(!hasAnyChanges()) {
            // nothing to serialize --> skip and exit early
            return {buffer.data() + off_row , 0}; 
        }

        // reserve space to store change bitset
        buffer.resize(buffer.size() + changes_.sizeBytes());
        
        // Serialize each element that has changed
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);           
            if (type == ColumnType::BOOL) {
                // Special handling for bools: always serialize but store as single bit in changes_
                // Get generic bool value (Layout handles BOOL types in buffer)
                bool val = *reinterpret_cast<const bool*>(data_.data() + offsets_[i]);
                changes_.set(i, val);
            } else if (changes_.test(i)) {
                size_t off = buffer.size();
                if(type == ColumnType::STRING) {
                    // Special handling for strings - encoding in ZoH mode happens in place
                    // Indices in string vector
                    const std::string& str = *reinterpret_cast<const std::string*>(data_.data() + offsets_[i]);
                    uint16_t strLength = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
                    buffer.resize(buffer.size() + sizeof(strLength) + strLength);
                    std::memcpy(buffer.data() + off, &strLength, sizeof(strLength));
                    std::memcpy(buffer.data() + off + sizeof(strLength), str.data(), strLength);
                } else {
                    size_t len = binaryFieldLength(type);
                    buffer.resize(buffer.size() + len);
                    std::memcpy(buffer.data() + off, data_.data() + offsets_[i], len);
                }
            }
        }

        // Write change bitset to reserved location
        std::memcpy(buffer.data() + off_row, changes_.data(), changes_.sizeBytes());
        return {buffer.data() + off_row, buffer.size() - off_row};
    }

    inline void Row::deserializeFromZoH(const std::span<const std::byte> buffer) {
        trackChanges(true); // ensure change tracking is enabled
        
        // buffer starts with the change bitset, followed by the actual row data.
        if (buffer.size() >= changes_.sizeBytes()) {
            std::memcpy(changes_.data(), buffer.data(), changes_.sizeBytes());  
        } else [[unlikely]] {
            throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small to contain change bitset.");
        }        
        auto buf = buffer.subspan(changes_.sizeBytes());
        
        // Deserialize each element that has changed
        size_t offset = 0;
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            if (type == ColumnType::BOOL) {
                // always deserialize bools from bitset
                *reinterpret_cast<bool*>(data_.data() + offsets_[i]) = changes_.test(i);
            
            } else if (changes_.test(i)) {
                // deserilialize other types only if they have changed
                std::byte* ptr = data_.data() + offsets_[i];
                if(type == ColumnType::STRING) {
                    // Special handling of strings: note ZoH encoding places string data next to string length (in place encoding)
                    // read string length
                    uint16_t strLength; 
                    if (offset + sizeof(strLength) > buf.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    std::memcpy(&strLength, buf.data() + offset, sizeof(strLength));
                    offset += sizeof(strLength);
                    
                    // read string data
                    if (offset + strLength > buf.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    std::string& str = *reinterpret_cast<std::string*>(ptr);
                    str.assign(reinterpret_cast<const char*>(buf.data() + offset), strLength);
                    offset += strLength;
                } else {
                    size_t len = binaryFieldLength(type);
                    if (offset + len > buf.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    
                    std::memcpy(ptr, buf.data() + offset, len);
                    offset += len;
                }
            }
        }
    }


    // ========================================================================
    // RowView Implementation
    // ========================================================================

    inline RowView::RowView(const Layout& layout, std::span<std::byte> buffer)
        : layout_(layout), buffer_(buffer), offsets_(), offset_var_(0)
    {
        //cast away constness of offsets_ and offset_var_ to initialize them here
        auto &offsets = const_cast<std::vector<uint32_t>&>(offsets_);
        auto &offset_var = const_cast<uint32_t&>(offset_var_);

        // update offsets_ based on layout, to enable fast access to fields
        size_t columnCount = layout_.columnCount();
        offsets.resize( columnCount);
        
        offset_var = 0; // start of variable section (string content), within flat binary buffer
        for(size_t i = 0; i < columnCount; ++i) {
            offsets[i] = offset_var;
            ColumnType type = layout_.columnType(i);
            offset_var += binaryFieldLength(type);
        }
    }

    /** Get a pointer to the value at the specified column index. 
        Note: 
            - No alignment guarantees are provided!
            - For strings we provide a pointer into the actual string data! No null terminator is included. 
    */
    inline std::span<const std::byte> RowView::get(size_t index) const {
        assert(layout_.columnCount() == offsets_.size());

        // 1. Check Index Bounds FIRST to prevent vector out-of-range in asserts/access
        size_t colCount = layout_.columnCount();
        if (index >= colCount) [[unlikely]] {
            assert(false && "RowView::get index out of bounds");
            return {};
        }

        // 2. Validate Buffer
        if (buffer_.empty()) [[unlikely]] {
            return {};
        }

        // 3. Cache checks variables (Access vector only once)
        uint32_t offset = offsets_[index];
        ColumnType type = layout_.columnType(index);
        size_t fieldLen = binaryFieldLength(type);


        // 4. Validate Fixed Section Bounds
        // Ensure the fixed field (or the StringAddress) fits in the buffer
        if (offset + fieldLen > buffer_.size()) [[unlikely]] {
             return {}; 
        }

        const std::byte* ptr = buffer_.data() + offset;

        // 5. Handle String vs Primitive
        if (type == ColumnType::STRING) {
            // Unpack string address safely using memcpy (alignment safe)
            StringAddr strAddr;
            std::memcpy(&strAddr, ptr, sizeof(strAddr));
            
            auto [strOff, strLen] = strAddr.unpack();
            
            // 6. Validate Variable Section Bounds
            if (strOff + strLen > buffer_.size()) [[unlikely]] {
                return {}; // string data extends beyond buffer
            }

            // Return span covering the actual string payload
            return { buffer_.data() + strOff, strLen };
        } else {
            // Return span covering the primitive value
            return { ptr, fieldLen };
        }
    }
   

    /** Vectorized access to multiple columns of same type. Only arithmetic types supported. */
    template<typename T>
    inline bool RowView::get(size_t index, std::span<T> &dst) const
    {
        static_assert(std::is_arithmetic_v<T>, "vectorized get() supports arithmetic types only");

        // 1. check we have a valid buffer
        if(buffer_.data() == nullptr || buffer_.size() < offset_var_) [[unlikely]] {
            return false;
        }

        // 2. combined range and type check
        if (RANGE_CHECKING) {
            constexpr auto targetType = toColumnType<T>();
            size_t iMax = index + dst.size();
            for (size_t i = index; i < iMax; ++i) {
                const ColumnType &sourceType = layout_.columnType(i);
                assert(targetType == sourceType && "RowView::get() type mismatch");
                if (targetType != sourceType) [[unlikely]]{
                    return false;
                }
            }
        }

        // Note: We know that there is no padding between fields of the same type in the serialized data(wire format), nor in the destination span. Thus we can copy a continuous block of memory. 
        // Note: Yes we know that alignment is not guaranteed by the bcsv::wire format (because we don't pad in serialized data!), but memcpy handles this safely.
        memcpy(dst.data(), buffer_.data() + offsets_[index], dst.size() * sizeof(T));
        return true;
    }

    /** Strict Typed Access.
     *  Returns primitives by value.
     *  Returns strings as std::string_view (Zero Copy).
     *  Throws std::runtime_error on type mismatch or bounds error.
     */
    template<typename T>
    inline T RowView::get(size_t index) const {
        assert(layout_.columnCount() == offsets_.size());
        assert(index < layout_.columnCount() && "RowView::get<T> index out of bounds");

        // 1. Get Column Info, offset, length (includes bounds check via layout_)
        ColumnType type = layout_.columnType(index);
        size_t offset = offsets_[index];
        size_t length = binaryFieldLength(type);

        // 2. Check Buffer Access
        if (buffer_.data() == nullptr || offset + length > buffer_.size()) {
            throw std::out_of_range("RowView::get<T> Invalid buffer");
        }

        // 3. Type Check
        if (type == ColumnType::STRING) {
            StringAddr addr;
            std::memcpy(&addr, buffer_.data() + offset, sizeof(addr));
            auto [strOff, strLen] = addr.unpack();
            
            // Validate string bounds for payload
            if (strOff + strLen > buffer_.size()) {
                throw std::out_of_range("String payload out of bounds");
            }

            if constexpr (std::is_same_v<T, std::string_view>) {
                return std::string_view(reinterpret_cast<const char*>(buffer_.data() + strOff), strLen);
            } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                return std::span<const char>(reinterpret_cast<const char*>(buffer_.data() + strOff), strLen);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::string(reinterpret_cast<const char*>(buffer_.data() + strOff), strLen);
            } else {
                throw std::runtime_error("RowView::get<T> Type mismatch for STRING");
            }
        } else {
            // Primitive handling
            if (toColumnType<T>() != type) {
                throw std::runtime_error("RowView::get<T> Type mismatch");
            }
            T val;
            std::memcpy(&val, buffer_.data() + offset, sizeof(T));
            return val;
        }
        
        // Should be unreachable given type checks
        throw std::runtime_error("Unsupported type in RowView::get<T>");
    }

    template<typename T>
    inline bool RowView::get_as(size_t index, T &dst) const {
        assert(layout_.columnCount() == offsets_.size());

        // 1. Get Column Info, offset, length (includes bounds check via layout_)
        ColumnType type = layout_.columnType(index);
        size_t offset = offsets_[index];
        size_t length = binaryFieldLength(type);

        // 2. Check Buffer Access
        if (buffer_.data() == nullptr || offset + length > buffer_.size() || index >= layout_.columnCount()) {
            assert(false && "RowView::get_as buffer access failed");
            return false;
        }

        // 3. Functor to check compatibility and assign.
        // Uses 'if constexpr' to ensure only valid assignments are compiled.
        auto try_read = [&]<typename SrcType>() -> bool {
             if constexpr (std::is_assignable_v<T&, SrcType>) {
                // SAFELY READ UNALIGNED DATA
                // RowView buffer data is packed and potentially unaligned.
                // We must read into a local aligned variable first using memcpy.
                SrcType tempVal;
                assert(length == sizeof(SrcType) && "Mismatched length for SrcType");
                std::memcpy(&tempVal, buffer_.data() + offset, length);
                dst = tempVal; // Implicit conversion/assignment (e.g. int8 -> int)
                return true;
            }
            return false;
        };

        // 4. Dispatch based on actual column type
        switch(type) {
            case ColumnType::BOOL:   return try_read.template operator()<bool>();
            case ColumnType::UINT8:  return try_read.template operator()<uint8_t>();
            case ColumnType::UINT16: return try_read.template operator()<uint16_t>();
            case ColumnType::UINT32: return try_read.template operator()<uint32_t>();
            case ColumnType::UINT64: return try_read.template operator()<uint64_t>();
            case ColumnType::INT8:   return try_read.template operator()<int8_t>();
            case ColumnType::INT16:  return try_read.template operator()<int16_t>();
            case ColumnType::INT32:  return try_read.template operator()<int32_t>();
            case ColumnType::INT64:  return try_read.template operator()<int64_t>();
            case ColumnType::FLOAT:  return try_read.template operator()<float>();
            case ColumnType::DOUBLE: return try_read.template operator()<double>();
            
            // Special handling for STRING type, as it requires unpacking the StringAddr
            case ColumnType::STRING: {
                StringAddr addr;
                memcpy(&addr, buffer_.data() + offset, sizeof(addr));
                auto [strOff, strLen] = addr.unpack();
                if (strOff + strLen > buffer_.size()) {
                    assert(false && "RowView::get_as string payload out of bounds");
                    return false; // string data out of bounds
                }
                const char* strPtr = reinterpret_cast<const char*>(buffer_.data() + strOff);                
                if constexpr (std::is_same_v<T, std::string>) {
                    dst.assign(strPtr, strLen); // deep copy
                    return true;
                } else if constexpr (std::is_same_v<T, std::string_view>) {
                    dst = std::string_view(strPtr, strLen); // zero-copy view (careful with lifetime!)
                    return true;
                } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                    dst = std::span<const char>(strPtr, strLen); // zero-copy span
                    return true;
                } else if constexpr (std::is_assignable_v<T&, std::string>) {
                    // fallback for other assignable types (e.g. std::filesystem::path)
                    dst = std::string_view(strPtr, strLen);
                    return true;
                } else {
                    return false;
                }
            }
            default:
                return false;
        }
    }

    /** Sets the value at the specified column index. Types must match exactly. 
    Note: Only supports primitive types (arithmetic and bool). */
    template<typename T>
    inline bool RowView::set(size_t index, const T& value) {
        using DecayedT = std::decay_t<T>;
        static_assert(std::is_arithmetic_v<DecayedT>, "RowView::set<T> supports primitive arithmetic types and bool only");
        
        // 1. Explicit Bounds Check
        if (index >= layout_.columnCount()) [[unlikely]] {
            return false;
        }

        // 2. Strict Type Match
        auto type = layout_.columnType(index);
        if (toColumnType<DecayedT>() != type) [[unlikely]] {
            return false; // type mismatch
        }

        // 3. Buffer Validation
        auto data = buffer_.data();
        auto size = buffer_.size();
        if(data == nullptr) [[unlikely]] {
            return false;
        }

        // 4. Offset/Length Check
        // offsets_ is resized in constructor, safe to access after index check
        auto offset = offsets_[index];
        if(offset + sizeof(T) > size) [[unlikely]] {
            return false; // out of bounds
        }

        // 5. Unaligned Write Safety
        // RowView points to packed data; a direct assignment (*cast = val) is unsafe.
        // memcpy handles the unaligned write efficiently.
        memcpy(data + offset, &value, sizeof(T));
        return true;
    }


    /** Sets the value at the specified column index from a value provided performing minor conversions (assignable) 
    Note: Only supports primitive types (arithmetic and bool).
    */
    template<typename T>
    inline bool RowView::set_from(size_t index, const T& value) {

        // Strict compile-time check: only allow arithmetic types (int, float, bool, etc.)
        using DecayedT = std::decay_t<T>;
        static_assert(std::is_arithmetic_v<DecayedT>, 
            "RowView::set_from supports primitive arithmetic types and bool only");

        // Runtime bounds check
        if(index >= layout_.columnCount()) [[unlikely]] return false;

        ColumnType type = layout_.columnType(index);
        
        auto data = buffer_.data();
        auto size = buffer_.size();
        // Basic buffer integrity check (offset_var_ implicitly validates fixed section size)
        if(data == nullptr) [[unlikely]] {
            return false;
        }

        auto offset = offsets_[index];
        auto ptr = data + offset;

        // Primitive Types Converter Helper
        auto try_assign = [&]<typename ColType>() -> bool {
            // Check if value is convertible/assignable to target column type (e.g. double -> int)
            if constexpr (std::is_assignable_v<ColType&, const DecayedT&>) {
                
                // Strict Bounds check for the specific column size
                if (offset + sizeof(ColType) > size) [[unlikely]] {
                    return false; 
                }

                // 1. Convert to target type (handled by static_cast based on is_assignable)
                ColType tempVal = static_cast<ColType>(value);
                
                // 2. Safe Unaligned Write (using memcpy)
                std::memcpy(ptr, &tempVal, sizeof(ColType));
                return true;
            }
            return false;
        };

        switch(type) {
            case ColumnType::BOOL:   return try_assign.template operator()<bool>();
            case ColumnType::UINT8:  return try_assign.template operator()<uint8_t>();
            case ColumnType::UINT16: return try_assign.template operator()<uint16_t>();
            case ColumnType::UINT32: return try_assign.template operator()<uint32_t>();
            case ColumnType::UINT64: return try_assign.template operator()<uint64_t>();
            case ColumnType::INT8:   return try_assign.template operator()<int8_t>();
            case ColumnType::INT16:  return try_assign.template operator()<int16_t>();
            case ColumnType::INT32:  return try_assign.template operator()<int32_t>();
            case ColumnType::INT64:  return try_assign.template operator()<int64_t>();
            case ColumnType::FLOAT:  return try_assign.template operator()<float>();
            case ColumnType::DOUBLE: return try_assign.template operator()<double>();
            
            // Strings are strictly unsupported (return false at runtime if column is STRING)
            default: return false;
        }
    }

    /** Vectorized set of multiple columns of same type */
    template<typename T>
    inline bool RowView::set(size_t index, std::span<const T> src)
    {
        static_assert(std::is_arithmetic_v<T>, "supports primitive arithmetic types only");
        
        // 1. Basic Validity Checks
        auto data = buffer_.data();
        auto size = buffer_.size();
        if(data == nullptr) [[unlikely]] {
            return false;
        }

        // 2. Bounds Calculation
        // Must ensure index points to existing column offset
        if (index >= offsets_.size()) [[unlikely]] {
            return false;
        }
        auto offset = offsets_[index];
        auto len = src.size() * sizeof(T);

        // 3. CRITICAL: Buffer Safety Check (Always enforce this!)
        // Ensure the write operation fits entirely within the buffer
        if(offset + len > size) [[unlikely]] {
            return false; 
        }

        // 4. Type Validation (Optional: RANGE_CHECKING)
        // Ensure all target columns in the span match type T
        if constexpr (RANGE_CHECKING) {
            constexpr ColumnType targetType = toColumnType<T>();
            for (size_t i = 0; i < src.size(); ++i) {
                // Ensure layout has enough columns remaining
                if (index + i >= layout_.columnCount()) [[unlikely]] {
                    return false;
                }
                const ColumnType &sourceType = layout_.columnType(index + i);
                if (targetType != sourceType) [[unlikely]]{
                    return false;
                }
            }
        }

        // 5. Execute Write
        // Exploit that types are aligned within the packed buffer (no padding).
        std::memcpy(data + offset, src.data(), len);
        return true;
    }

    inline Row RowView::toRow() const
    {
        Row row(layout_);
        try {
            row.deserializeFrom(buffer_);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("RowView::toRow failed: ") + e.what());
        }
        return row;
    }

    inline bool RowView::validate(bool deepValidation) const 
    {
        // skip validation for empty layouts
        size_t col_count = layout_.columnCount();
        if (col_count == 0) {
            return true;
        }

        // is buffer available and big enough?
        auto data = buffer_.data();
        auto size = buffer_.size();
        if(data == nullptr || size < offset_var_) {
            return false;
        }
        
        if(deepValidation) {
            // special check for strings // variant length
            for(size_t i = 0; i < col_count; ++i) {
                if (layout_.columnType(i) == ColumnType::STRING) {
                    StringAddr strAddr;
                    assert(offsets_.size() == col_count && offsets_[i] + sizeof(strAddr) <= size && "RowView::validate internal error: offsets_ size mismatch or out of bounds");
                    memcpy(&strAddr, data + offsets_[i], sizeof(strAddr));
                    auto [strOff, strLen] = strAddr.unpack();
                    if (strOff + strLen > size) {
                        return false;
                    }
                }
            }    
        }
        return true;
    }

    // ========================================================================
    // RowStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...>::RowStatic(const LayoutType& layout) 
        : layout_(layout), data_() 
    {
        clear();
        changes_.reset();
    }

    /** Clear the row to its default state (default values) */
    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::clear()
    {
        if constexpr (Index < column_count) {
            std::get<Index>(data_) = defaultValueT<column_type<Index>>();
            if(change_tracking_) {
                changes_.set(Index);
            }
            clear<Index + 1>();
        }
    }

    /** Direct reference to column data. No overhead. */
    template<typename... ColumnTypes>
    template<size_t Index>
    const auto& RowStatic<ColumnTypes...>::get() const noexcept {
        static_assert(Index < column_count, "Index out of bounds");
        return std::get<Index>(data_);
    }

    /** Vectorized static access. 
    *  Copies 'Extent' elements starting from 'StartIndex' to 'dst'.
    *  Unrolled at compile time. 
    *  Performs implicit type conversion (e.g. read int column into double span).
    */
    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::get(std::span<T, Extent> &dst) const noexcept {
        // 1. Static Checks
        static_assert(Extent != std::dynamic_extent, "RowStatic: Static vectorized get requires fixed-extent span (std::span<T, N>)");
        static_assert(StartIndex + Extent <= column_count, "RowStatic: Access range exceeds column count");

        // 2. Unrolled Assignment using Fold Expression
        // Creates a compile-time sequence 0..Extent-1 to access tuple elements
        [&]<size_t... I>(std::index_sequence<I...>) {
            
            // a) Verify convertibility for ALL columns (Fold over &&)
            static_assert(((std::is_same_v<column_type<StartIndex + I>, T>) && ...), 
                "RowStatic::get(span) [Static]: Type mismatch. All columns in the range must match the Span type exactly.");

            // b) Assignment with conversion (Fold over comma operator)
            // Expands to: dst[0] = ...; dst[1] = ...; etc.
            ((dst[I] = static_cast<T>(std::get<StartIndex + I>(data_))), ...);

        }(std::make_index_sequence<Extent>{});
    }

    /** Vectorized static access. 
    *  Copies 'Extent' elements starting from 'StartIndex' to 'dst'.
    *  Unrolled at compile time. 
    *  Performs implicit type conversion (e.g. read int column into double span).
    */
    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::get_as(std::span<T, Extent> &dst) const noexcept {
        // 1. Static Checks
        static_assert(Extent != std::dynamic_extent, "RowStatic: Static vectorized get requires fixed-extent span (std::span<T, N>)");
        static_assert(StartIndex + Extent <= column_count, "RowStatic: Access range exceeds column count");

        // 2. Unrolled Assignment using Fold Expression
        // Creates a compile-time sequence 0..Extent-1 to access tuple elements
        [&]<size_t... I>(std::index_sequence<I...>) {
            
            // a) Verify convertibility for ALL columns (Fold over &&)
            static_assert((std::is_assignable_v<T&, const column_type<StartIndex + I>&> && ...), 
                "RowStatic: Column type is not assignable to destination span type");

            // b) Assignment with conversion (Fold over comma operator)
            // Expands to: dst[0] = ...; dst[1] = ...; etc.
            ((dst[I] = static_cast<T>(std::get<StartIndex + I>(data_))), ...);

        }(std::make_index_sequence<Extent>{});
    }

    /** Get raw pointer (void*). Returns nullptr if index invalid.
     *  Resolves the tuple element address at runtime using a fold expression.
     *  Note: The returned pointer is guaranteed to be aligned for the column's type.
     */
    template<typename... ColumnTypes>
    const void* RowStatic<ColumnTypes...>::get(size_t index) const noexcept {
        // 1. Bounds check
        assert(index < column_count && "RowStatic::get(index): Index out of bounds");
        if constexpr (RANGE_CHECKING) {
            if (index >= column_count) [[unlikely]] {
                return nullptr;
            }
        }

        // 2. Define Function Pointer Signature
        using GetterFunc = const void* (*)(const RowStatic&);

        // 3. Generate Jump Table (Static Constexpr)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, column_count>{
                // Lambda capturing behavior for index I
                +[](const RowStatic& self) -> const void* {
                    return &std::get<I>(self.data_);
                }...
            };
        }(std::make_index_sequence<column_count>{});

        // 4. O(1) Dispatch
        return handlers[index](*this);
    }

    /** Get typed reference (strict). Throws if type mismatch or index invalid. 
     *  Matches Row::get<T>(index) interface and behavior.
     */
    template<typename... ColumnTypes>
    template<typename T>
    const T& RowStatic<ColumnTypes...>::get(size_t index) const {
        // 1. Reuse raw pointer lookup
        // This makes the code cleaner by isolating the tuple traversal logic in one place.
        const void* ptr = get(index);
        
        if (ptr == nullptr) [[unlikely]] {
             throw std::out_of_range("Invalid column index");
        }

        // 2. Strict Type Check
        // We ensure the requested C++ type T maps to the same ColumnType as the actual data.
        // RowStatic's layout_ provides efficient O(1) type lookup.
        if (layout_.columnType(index) != toColumnType<T>()) [[unlikely]] {
             throw std::runtime_error("Type mismatch in RowStatic::get<T>");
        }

        // 3. Safe Cast
        // Since we verified the type (via toColumnType) and the address, this cast is safe.
        return *static_cast<const T*>(ptr);
    }

    /** Vectorized runtime access. 
     *  Copies data from the row starting at 'index' into the destination span.
     *  Iterates through the range and strictly validates type compatibility for each element.
     *  
     *  Throws std::out_of_range if the range exceeds column count.
     *  Throws std::runtime_error (via get<T>) if any column type does not match T.
     */
    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::get(size_t index, std::span<T, Extent> &dst) const {
        // 1. Range Check
        if (index + dst.size() > column_count) [[unlikely]] {
            throw std::out_of_range("RowStatic::get(span): Access range exceeds column count");
        }

        // 2. Element-wise Copy
        // We leverage the existing strictly-typed scalar accessor.
        // This inherently checks that layout_.columnType(index + i) == toColumnType<T>()
        // for every column involved. 
        for (size_t i = 0; i < dst.size(); ++i) {
            dst[i] = this->get<T>(index + i);
        }
    }

    /** Flexible copy access (runtime index). 
     *  Performs assignment/conversion if possible. 
     */
    template<typename... ColumnTypes>
    template<typename T>
    bool RowStatic<ColumnTypes...>::get_as(size_t index, T& dst) const noexcept {
        // 1. Bounds Check
        if (index >= column_count) [[unlikely]] {
            return false;
        }

        // 2. Define Function Ptr Type
        using HandlerFunc = bool (*)(const RowStatic&, T&);

        // 3. Create the Jump Table (static constexpr means computed once at compile-time)
        // We use a lambda to immediately invoke and return the populated array 
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<HandlerFunc, column_count>{
                // Unary '+' forces generic lambda to function pointer conversion
                +[](const RowStatic& self, T& target) -> bool {
                    const auto& srcVal = std::get<I>(self.data_);
                    using SrcType = std::decay_t<decltype(srcVal)>;

                    // --- Exact same logic as before, now isolated per column ---
                    if constexpr (std::is_assignable_v<T&, const SrcType&>) {
                         if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<SrcType>) {
                             target = static_cast<T>(srcVal);
                         } else {
                             target = srcVal;
                         }
                         return true;
                    }
                    else if constexpr (std::is_same_v<SrcType, std::string>) {
                        if constexpr (std::is_same_v<T, std::string_view>) {
                            target = std::string_view(srcVal);
                            return true;
                        }
                        else if constexpr (std::is_same_v<T, std::span<const char>>) {
                            target = std::span<const char>(srcVal);
                            return true;
                        }
                    }
                    return false; 
                }... // Pack expansion populates the array
            };
        }(std::make_index_sequence<column_count>{});

        // 4. O(1) Execution
        // No loops, no if-else chains. Direct jump to the correct code block.
        return handlers[index](*this, dst);
    }

    
    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::set(const T& value) {
        static_assert(Index < column_count, "Index out of bounds");

        // 1. Unwrap Variants (including ValueType) --> recurse into set<>
        using DecayedT = std::decay_t<T>;
        if constexpr (detail::is_variant_v<DecayedT>) {
            std::visit([this](auto&& v) {
                this->set<Index>(v);
            }, value);
            return;
        }

        // 2. Handle Spans (must convert manually to string_view) --> recurse into set<>
        else if constexpr (std::is_same_v<DecayedT, std::span<char>> || std::is_same_v<DecayedT, std::span<const char>>) {
            this->set<Index>(std::string_view(value.data(), value.size()));
            return;
        }

        // 3. Handle String Columns
        else if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            auto& currentVal = std::get<Index>(data_);

            // Case 3a: Convertible to string_view (std::string, string_view, const char*, etc.)
            // Note: Creating a string_view from std::string is extremely cheap (pointer copy, no allocation).
            // It allows us to perform zero-allocation truncation via substr(), which is strictly faster 
            // than handling std::string directly where truncation would require creating a temporary string.
            if constexpr (std::is_convertible_v<T, std::string_view>) {
                std::string_view sv = value;
                if (sv.size() > MAX_STRING_LENGTH) {
                    sv = sv.substr(0, MAX_STRING_LENGTH);
                }
                if (currentVal != sv) {
                    currentVal.assign(sv); 
                    changes_.set(Index);
                }
            } 

            // Case 3b: Arithmetic types -> convert to string
            else if constexpr (std::is_arithmetic_v<DecayedT>) {
                std::string strVal = std::to_string(value); // cannot extend max length here               
                if (currentVal != strVal) {
                    currentVal = std::move(strVal);
                    changes_.set(Index);
                }
            }
            else {
                static_assert(false, "RowStatic::set<Index>: Unsupported type to assign to string column");
            }
        }

        // 4. Handle Primitive Types
        else {
                static_assert(std::is_assignable_v<column_type<Index>&, const T&>, "Column type cannot be assigned from the provided value");
                auto& currentVal = std::get<Index>(data_);
                // Check equality after casting to avoid spurious changes (e.g. 5 != 5.1)
                if (currentVal != static_cast<column_type<Index>>(value)) {
                    currentVal = static_cast<column_type<Index>>(value);
                    changes_.set(Index);
                }
        }
    } 

    template<typename... ColumnTypes>
    template<typename T>
    void RowStatic<ColumnTypes...>::set(size_t index, const T& value)  {
        // 1. Bounds Check (Runtime)
        if constexpr (RANGE_CHECKING) {
            if (index >= column_count) [[unlikely]] {
                throw std::out_of_range("RowStatic::set(): Invalid column index");
            }
        }

        // 2. Define Function Pointer Signature
        using SetterFunc = void (*)(RowStatic&, const T&);
        
        // 3. Generate Jump Table (Static Constexpr), using lambda to populate array
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<SetterFunc, column_count>{
                +[](RowStatic& self, const T& v) {
                    self.template set<I>(v); 
                }...
            };
        }(std::make_index_sequence<column_count>{});

        // 4. O(1) Dispatch
        handlers[index](*this, value);
    }

    /** Vectorized static set (Compile-Time) */
    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::set(std::span<const T, Extent> values) {
        static_assert(StartIndex + Extent <= column_count, "RowStatic: Range exceeds column count");
        
        [&]<size_t... I>(std::index_sequence<I...>) {
            // Fold set: set<StartIndex+0>(values[0]); ...
            (this->template set<StartIndex + I>(values[I]), ...);
        }(std::make_index_sequence<Extent>{});
    }

    /** Runtime vectorized set with span */
    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::set(size_t index, std::span<const T, Extent> values) {

        // ToDo: Currently its a simple loop delegating to scalar set(). Should be optimized as we know that elements are contiguous and hence no padding in between.

        // 1. Access Check
        if (index + values.size() > column_count) {
            throw std::out_of_range("Span exceeds column count");
        }

        // 2. Iterative Set
        // Delegating to the scalar set(index, val) reuses the switch/jump-table logic 
        // efficiently without needing complex recursion here.
        for(size_t i = 0; i < values.size(); ++i) {
            this->set(index + i, values[i]);
        }
    }

    // ========================================================================
    // RowStatic Vectorized Access - Runtime Indexed
    // ========================================================================

    /** Serialize the row into the provided buffer, appending data to the end of the buffer.
    * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
    * @return A span pointing to the serialized row data within the buffer.
    */
    template<typename... ColumnTypes>
    std::span<std::byte> RowStatic<ColumnTypes...>::serializeTo(ByteBuffer& buffer) const {
        size_t offRow = buffer.size();               // remember where this row starts
        size_t offVar = offset_var_;                 // offset to the begin of variable-size data section (relative to row start)
        buffer.resize(buffer.size() + offset_var_);  // ensure buffer is large enough to hold fixed-size data

        // serialize each tuple element using compile-time recursion
        serializeElements<0>(buffer, offRow, offVar);
        return {buffer.data() + offRow, buffer.size() - offRow};
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::serializeElements(ByteBuffer& buffer, const size_t& offRow, size_t& offVar) const {
        if constexpr (Index < column_count) {
            constexpr size_t lenFix = column_lengths[Index];
            constexpr size_t offFix = column_offsets[Index];
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t lenVar = std::min(std::get<Index>(data_).size(), MAX_STRING_LENGTH);
                StringAddr strAddr(offVar, lenVar);                                                   // Make address relative to row start
                buffer.resize(offRow + offVar + lenVar);                                                   // Ensure buffer is large enough to hold string payload
                std::memcpy(buffer.data() + offRow + offFix, &strAddr, sizeof(strAddr));                // write string address
                std::memcpy(buffer.data() + offRow + offVar, std::get<Index>(data_).c_str(), lenVar);   // write string payload
                offVar += lenVar;
            } else {
                std::memcpy(buffer.data() + offRow + offFix, &std::get<Index>(data_), lenFix);
            }
            // Recursively process next element
            serializeElements<Index + 1>(buffer, offRow, offVar);
        }
    }

    /** Serialize the row into the provided buffer using Zero-Order-Hold (ZoH) compression.
     * Only the columns that have changed (as indicated by the change tracking bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change bitset.
     * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
     * @return A span pointing to the serialized row data within the buffer.
     */
    template<typename... ColumnTypes>
    std::span<std::byte> RowStatic<ColumnTypes...>::serializeToZoH(ByteBuffer& buffer) const {
        assert(change_tracking_ && "RowStatic::serializeToZoH() requires change tracking to be enabled.");

        // remember where this row starts, (as we are appending to the buffer)
        size_t old_size = buffer.size();               
        auto rowStart = buffer.data() + buffer.size();

        // skips if there is nothing to serialize
        if(!hasAnyChanges()) {
            return {rowStart, old_size}; 
        }

        // reserve space to store change bitset
        buffer.resize(buffer.size() + changes_.sizeBytes());
        auto bit_section = new(rowStart) bitset<column_count>(changes_); // place a copy of changes_ to the begin of the row buffer, we are going to modify only a few bits (bool columns) during serialization. The rest stays as is.
        
        // Serialize each tuple element using compile-time recursion
        serializeElementsZoH<0>(buffer, *bit_section);
        return {rowStart, buffer.size() - old_size};
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::serializeElementsZoH(ByteBuffer& buffer, bitset<column_count>& bitHeader) const 
    {
        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, bool>) {
                // store as single bit within serialization_bits
                bool value = std::get<Index>(data_);
                bitHeader.set(Index, value); // mark as changed
            } else if (changes_.test(Index)) {
                // all other types: only serialize if marked as changed)
                size_t old_size = buffer.size();
                if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                    // special handling for strings, as we need to determine string length
                    // encoding in ZoH mode happens in place. Therefore no string offset is required!
                    const auto& value = std::get<Index>(data_);
                    uint16_t strLength = static_cast<uint16_t>(std::min(value.size(), MAX_STRING_LENGTH));
                    const std::byte* strLengthPtr = reinterpret_cast<const std::byte*>(&strLength);
                    const std::byte* strDataPtr = reinterpret_cast<const std::byte*>(value.c_str());

                    buffer.resize(buffer.size() + sizeof(uint16_t) + strLength);
                    memcpy(buffer.data() + old_size, strLengthPtr, sizeof(uint16_t));
                    memcpy(buffer.data() + old_size + sizeof(uint16_t), strDataPtr, strLength);
                } else {
                    // for all other types, we append directly to the end of the buffer
                    const auto& value = std::get<Index>(data_);
                    const std::byte* dataPtr = reinterpret_cast<const std::byte*>(&value);
                
                    buffer.resize(buffer.size() + sizeof(column_type<Index>));
                    memcpy(buffer.data() + old_size, dataPtr, sizeof(column_type<Index>));
                }
            }
            // Recursively process next element
            return serializeElementsZoH<Index + 1>(buffer, bitHeader); 
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::deserializeFrom(const std::span<const std::byte> buffer)  {
        //we expect the buffer, starts with the first byte of the row and ends with the last byte of the row (no change bitset)
        deserializeElements<0>(buffer);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::deserializeElements(const std::span<const std::byte> &buffer) 
    {
        if constexpr (Index < column_count) {
            constexpr size_t len = binaryFieldLength<column_type<Index> >();
            constexpr size_t off = column_offsets[Index];

            if (off + len > buffer.size()) {
                throw std::runtime_error("RowStatic::deserializeElements() failed! Buffer overflow while reading.");
            }

            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                StringAddr strAddr;
                std::memcpy(&strAddr, buffer.data() + off, sizeof(strAddr));
                auto [strOff, strLen] = strAddr.unpack();
                if (strOff + strLen > buffer.size()) {
                    throw std::runtime_error("RowStatic::deserializeElements() failed! Buffer overflow while reading.");
                }
                std::get<Index>(data_).assign(reinterpret_cast<const char*>(buffer.data() + strOff), strLen);
            } else {
                std::memcpy(&std::get<Index>(data_), buffer.data() + off, len);
            }
            
            // Recursively process next element
            deserializeElements<Index + 1>(buffer);
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::deserializeFromZoH(const std::span<const std::byte> buffer)  
    {
        // we expect the buffer to start with the change bitset, followed by the actual row data
        if (buffer.size() < changes_.sizeBytes()) {
            throw std::runtime_error("RowStatic::deserializeFromZoH() failed! Buffer too small to contain change bitset.");
        } else {
            // read change bitset from beginning of buffer
            std::memcpy(changes_.data(), buffer.data(), changes_.sizeBytes());
        }
        auto dataBuffer = buffer.subspan(changes_.sizeBytes());
        deserializeElementsZoH<0>(dataBuffer);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::deserializeElementsZoH(std::span<const std::byte> &buffer) {
        // we expect the buffer to have the next element at the current position
        // thus the buffer needs to get shorter as we read elements
        // We also expect that the change bitset has been read already!

        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, bool>) {
                // Special handling for bools: 
                //  - always deserialize!
                //  - but stored as single bit within changes_
                bool value = changes_.test(Index);
                std::get<Index>(data_) = value;
            } else if(changes_.test(Index)) {
                // all other types: only deserialize if marked as changed
                if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                    // special handling for strings, as we need to determine string length
                    if (buffer.size() < sizeof(uint16_t)) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string length.");
                    }
                    uint16_t strLength;
                    std::memcpy(&strLength, buffer.data(), sizeof(uint16_t));
                    
                    if (buffer.size() < sizeof(uint16_t) + strLength) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string payload.");
                    }
                    std::string value;
                    value.assign(reinterpret_cast<const char*>(buffer.data() + sizeof(uint16_t)), strLength);
                    std::get<Index>(data_) = value;
                    buffer = buffer.subspan(sizeof(uint16_t) + strLength);
                } else {
                    // for all other types, we read directly from the start of the buffer
                    if (buffer.size() < sizeof(column_type<Index>)) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain element.");
                    }
                    std::memcpy(&std::get<Index>(data_), buffer.data(), sizeof(column_type<Index>));
                    buffer = buffer.subspan(sizeof(column_type<Index>));
                }
            }
            // Column not changed - keeping previous value (no action needed)
            deserializeElementsZoH<Index + 1>(buffer);
        }
    }



    // ========================================================================
    // RowStaticView Implementation
    // ========================================================================


    /** Get value by Static Index.
     *  for Primitives: Returns T by value (via memcpy).
     *  for Strings:    Returns std::string_view pointing into buffer (Zero Copy).
     */
    template<typename... ColumnTypes>
    template<size_t Index>
    auto RowViewStatic<ColumnTypes...>::get() const {
        static_assert(Index < column_count, "Index out of bounds");
        
        // check buffer validity
        if (buffer_.data() == nullptr) [[unlikely]] {
             throw std::runtime_error("RowViewStatic::get<I>() buffer not set");
        }

        constexpr size_t length = column_lengths[Index];
        constexpr size_t offset = column_offsets[Index];
        using T = column_type<Index>;
        if (offset + length > buffer_.size()) [[unlikely]] {
            throw std::out_of_range("RowViewStatic::get<I>() Buffer too small");
        }

        if constexpr (std::is_same_v<T, std::string>) {
            // Decode StringAddr from fixed section
            StringAddr addr;
            std::memcpy(&addr, buffer_.data() + offset, length);
            auto [strOff, strLen] = addr.unpack();

            // Check payload bounds
            if (strOff + strLen > buffer_.size()) [[unlikely]] {
                throw std::out_of_range("RowViewStatic::get<I>() Buffer too small");
            }
            
            // Return string_view pointing directly into the buffer payload
            return std::string_view(reinterpret_cast<const char*>(buffer_.data() + strOff), strLen);
        } else {
            // Primitives: Read value
            T val;
            std::memcpy(&val, buffer_.data() + offset, length);
            return val;
        }
    }

    /** Vectorized static access (Compile-Time) 
     *  Optimized for contiguous block copy. 
     *  Enforces strict type matching and fixed-extent spans at compile time.
     */
    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    bool RowViewStatic<ColumnTypes...>::get(std::span<T, Extent>& dst) const noexcept {
        static_assert(Extent != std::dynamic_extent, "RowViewStatic::get(span) [Static] requires fixed-extent span");
        static_assert(std::is_arithmetic_v<T>, "RowViewStatic::get(span) [Static] supports primitive types only");
        static_assert(StartIndex + Extent <= column_count, "RowViewStatic::get(span) [Static] range out of bounds");
        
        // 1. Strict Type Integrity (Compile-Time)
        // verify that every column in the target range matches T exactly.
        // This guarantees that the source data is a contiguous block of sizeof(T) * Extent bytes.
        [&]<size_t... I>(std::index_sequence<I...>) {
            static_assert(((std::is_same_v<column_type<StartIndex + I>, T>) && ...), 
                "RowViewStatic::get(span) [Static]: Type mismatch. All columns in range must match Span type.");
        }(std::make_index_sequence<Extent>{});

        // 2. Buffer Validity Check
        if (buffer_.empty()) [[unlikely]] return false;

        // 3. Calculate Offsets (Compile-Time Constants)
        // These resolve to immediate values in the assembly code.
        constexpr size_t offset = column_offsets[StartIndex];
        constexpr size_t length = Extent * sizeof(T);

        // 4. Buffer Boundary Check
        // Single runtime check covers the entire block.
        if (offset + length > buffer_.size()) [[unlikely]] {
            return false;
        }

        // 5. Fast Block Copy
        // We know from step 1 that types are identical and packed, allowing safe memcpy.
        std::memcpy(dst.data(), buffer_.data() + offset, length);
        return true;
    }

    /** Get raw span by runtime index.
     *  Returns a span covering the data (primitive value or string payload).
     */
    template<typename... ColumnTypes>
    std::span<const std::byte> RowViewStatic<ColumnTypes...>::get(size_t index) const noexcept {
        assert(index < column_count && "RowViewStatic<ColumnTypes...>::get(index): Index out of bounds");
        
        if (index >= column_count || buffer_.empty()) 
            return {};

        using GetterFunc = std::span<const std::byte> (*)(const RowViewStatic&);

        // O(1) Jump Table to handle type-specific logic (StringAddr vs Primitive)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, column_count>{
                +[](const RowViewStatic& self) -> std::span<const std::byte> {
                    constexpr size_t offset = column_offsets[I];
                    constexpr size_t length    = column_lengths[I]; // Size of fixed field (primitives or StringAddr)
                    
                    if (self.buffer_.size() < offset + length) return {};
                    
                    const std::byte* ptr = self.buffer_.data() + offset;
                    using T = column_type<I>;

                    if constexpr (std::is_same_v<T, std::string>) {
                        // For strings, jump to payload
                        StringAddr addr;
                        std::memcpy(&addr, ptr, length);
                        auto [strOff, strLen] = addr.unpack();
                        
                        if (strOff + strLen > self.buffer_.size()) return {};
                        return { self.buffer_.data() + strOff, strLen };
                    } else {
                        // For primitives, return fixed field
                        return { ptr, length };
                    }
                }...
            };
        }(std::make_index_sequence<column_count>{});

        return handlers[index](*this);
    }

    /** Runtime vectorized access (Compile-Time) */
    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    bool RowViewStatic<ColumnTypes...>::get(size_t index, std::span<T, Extent>& dst) const noexcept {
        static_assert(std::is_arithmetic_v<T>, "RowViewStatic::get(span) supports primitive types only (no strings)");
        
        // 1. Valid Buffer Check
        if (buffer_.empty()) [[unlikely]] return false;

        // 2. Access Check
        size_t iMax = index + dst.size();
        if (iMax > column_count) return false;
        
        // 3. Type Consistency Check (Fast Fail)
        auto dstType = toColumnType<T>();
        for (size_t i = index; i < iMax; ++i) {
             if (layout_.columnType(i) != dstType) [[unlikely]] {
                 return false; 
             }
        }

        // 4. check buffer size for last column
        const size_t offset = column_offsets[index];
        const size_t length = binaryFieldLength(dstType) * dst.size();
        assert(length == sizeof(T) * dst.size());
        if (offset + length > buffer_.size()) [[unlikely]] {
            return false;
        }

        // 5. Bulk Copy, we know src and dst are continouse blocks of same type, without padding in between
        memcpy(dst.data(), buffer_.data() + offset, length);
        return true;
    }


    template<typename... ColumnTypes>
    template<typename T>
    bool RowViewStatic<ColumnTypes...>::get_as(size_t index, T& dst) const noexcept {
        // 1. Bounds Check (Runtime)
        if constexpr (RANGE_CHECKING) {
            assert(index < column_count && "RowViewStatic<ColumnTypes...>::get_as(index): Index out of bounds");
            if (index >= column_count) [[unlikely]] 
                return false;
        }

        // 2. Empty Buffer Check
        if (buffer_.empty()) 
            return false;

        // 3. Define Function Pointer Signature
        using GetterFunc = bool (*)(const RowViewStatic&, T&);

        // 4. Generate Jump Table (Static Constexpr)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, column_count>{
                +[](const RowViewStatic& self, T& target) -> bool {
                    using ColT = column_type<I>; 
                    
                    // Case A: String handling
                    if constexpr (std::is_same_v<ColT, std::string>) {
                         if constexpr (std::is_same_v<T, std::string_view>) {
                             try { target = self.template get<I>(); return true; } catch(...) {}
                         } else if constexpr (std::is_same_v<T, std::string>) {
                             try { target = std::string(self.template get<I>()); return true; } catch(...) {} 
                         }
                    }
                    // Case B: Arithmetic conversion
                    else if constexpr (std::is_arithmetic_v<ColT> && std::is_arithmetic_v<T>) {
                         try {
                             target = static_cast<T>(self.template get<I>()); 
                             return true;
                         } catch (...) {}
                    }
                    return false;
                }...
            };
        }(std::make_index_sequence<column_count>{});

        // 5. O(1) Dispatch
        return handlers[index](*this, dst);
    }

    /* Scalar static set (Compile-Time) */
    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowViewStatic<ColumnTypes...>::set(const T& value) noexcept {
        static_assert(Index < column_count, "Index out of bounds");
        using ColT = column_type<Index>;

        static_assert(std::is_same_v<ColT, T> && std::is_arithmetic_v<T>, 
            "RowViewStatic::set<I> only supports matching primitive types. Strings not supported.");

        constexpr size_t offset = column_offsets[Index];
        
        // Safety check for buffer size
        if (offset + sizeof(T) > buffer_.size()) [[unlikely]] {
             return; 
        }

        // Safe unaligned write
        std::memcpy(buffer_.data() + offset, &value, sizeof(T));
    }

    /** Vectorized static set (Compile-Time) */
    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowViewStatic<ColumnTypes...>::set(std::span<const T, Extent> values) noexcept {
        static_assert(StartIndex + Extent <= column_count, "Out of bounds");
        
        [&]<size_t... I>(std::index_sequence<I...>) {
            (this->template set<StartIndex + I>(values[I]), ...);
        }(std::make_index_sequence<Extent>{});
    }

    template<typename... ColumnTypes>
    template<typename T>
    void RowViewStatic<ColumnTypes...>::set(size_t index, const T& value) noexcept {
        static_assert(std::is_arithmetic_v<T>, "RowViewStatic::set supports primitives only");

        // 1. Bounds Check (Runtime)
        if constexpr (RANGE_CHECKING) {
            assert(index < column_count && "RowViewStatic::set(): Index out of bounds");
            if (index >= column_count) 
                return;
        }
        
        // 2. Define Function Pointer Signature
        using SetterFunc = void (*)(RowViewStatic&, const T&);

        // 3. Generate Jump Table (Static Constexpr)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<SetterFunc, column_count>{
                +[](RowViewStatic& self, const T& v) {
                    using ColT = column_type<I>;
                    if constexpr (std::is_same_v<ColT, T>) {
                        self.template set<I>(v);
                    }
                }...
            };
        }(std::make_index_sequence<column_count>{});

        // 4. O(1) Dispatch
        handlers[index](*this, value);
    }
    
    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowViewStatic<ColumnTypes...>::set(size_t index, std::span<const T, Extent> values) noexcept {
        if (index + values.size() > column_count) return;
        
        for (size_t i = 0; i < values.size(); ++i) {
             this->set(index + i, values[i]);
        }
    }

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...> RowViewStatic<ColumnTypes...>::toRow() const
    {
        RowStatic<ColumnTypes...> row(layout_);
        // Use fold expression to copy every column I=0..N from View to Row
        [&]<size_t... I>(std::index_sequence<I...>) {
             (row.template set<I>(this->template get<I>()), ...);
        }(std::make_index_sequence<column_count>{});
        
        return row;
    }

    template<typename... ColumnTypes>
    bool RowViewStatic<ColumnTypes...>::validate() const noexcept 
    {
        if(buffer_.empty()) {
            return false;
        }

        if constexpr (column_count == 0) {
            return true; // Nothing to validate
        }

        // Check if buffer is large enough for all fixed fields
        if (LayoutType::column_offsets[column_count-1] + LayoutType::column_lengths[column_count-1] > buffer_.size()) {
            return false;
        }

        // Validate string payloads using template recursion
        return validateStringPayloads<0>();
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowViewStatic<ColumnTypes...>::validateStringPayloads() const {
        if constexpr (Index >= column_count) {
            return true;
        } else {
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                StringAddr strAddr;
                std::memcpy(&strAddr, buffer_.data() + LayoutType::column_offsets[Index], sizeof(strAddr));
                auto [strOff, strLen] = strAddr.unpack();
                if (strOff + strLen > buffer_.size()) {
                    return false;
                }
            }
            return validateStringPayloads<Index + 1>();
        }
    }

} // namespace bcsv