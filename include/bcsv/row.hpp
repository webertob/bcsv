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

#include "bcsv/bitset.h"
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
#include <type_traits>

namespace bcsv {

    // ========================================================================
    // Row Implementation
    // ========================================================================

    // Helper: Construct default value at memory location based on ColumnType
    inline void constructDefaultAt(std::byte* ptr, ColumnType type) {
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



    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::RowImpl(const Layout& layout)
        : layout_(layout)
        , data_()
        , ptr_(layout.columnCount(), nullptr)
        , changes_(0)
        // deprceated:
        , offsets_()
        , offset_var_(0)
    {
        // Register as observer for layout changes
        layout_.registerCallback(this, {
               [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
            }
        );

        size_t columnCount = layout_.columnCount();

        // Build row's internal data structures based on layout
        // Calculate offsets considering alignment requirements
        uint32_t offset = 0;
        std::vector<uint32_t> offsets(columnCount);
        for(size_t i = 0; i < columnCount; ++i) {
            ColumnType type  = layout_.columnType(i);
            size_t alignment = alignOf(type);
            // align offset to match type's alignment requirement
            // Assumes alignment is a power of 2, which is true for all standard types
            offset = (offset + (alignment - 1)) & ~(alignment - 1);
            offsets[i] = offset;
            offset += sizeOf(type); //advance offset for next column
        }

        // construct all types within data_ using their default values
        data_.resize(offset);
        for(size_t i = 0; i < columnCount; ++i) {
            ptr_[i] = &data_[offsets[i]];
            constructDefaultAt(ptr_[i], layout_.columnType(i));
        }        
        if constexpr (isTrackingEnabled(Policy)) {
            changes_.resize(columnCount, true);
        }

        // deprecated offsets_ - convert vector<size_t> to vector<uint32_t>
        offsets_ = std::move(offsets);
        offset_var_ = 0; // start of variable section (string content), within flat binary buffer
        for(size_t i = 0; i < columnCount; ++i) {
            ColumnType type = layout_.columnType(i);
            offset_var_ += wireSizeOf(type);
        }
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::RowImpl(const RowImpl& other)
        : layout_(other.layout_)  // Share layout (shallow copy of shared_ptr inside)
        , data_(other.data_) // Allocate our own data buffer
        , ptr_(other.ptr_.size(), nullptr) // We will set up our own pointers
        , changes_(other.changes_)
        // deprceated:
        , offsets_(other.offsets_)
        , offset_var_(other.offset_var_)
    {
        // update our pointers, as they point into our own data buffer, not other's
        size_t columnCount = layout_.columnCount();
        for(size_t i = 0; i < columnCount; ++i) {
            size_t offset = other.ptr_[i] - other.data_.data();
            assert(offset < data_.size() && "Row copy constructor: calculated offset should be within our data_ buffer");
            ptr_[i] = data_.data() + offset;
        }

        // deep copy strings, as they manage their own memory and we want independent rows
        for(size_t i = 0; i < columnCount; ++i) {
            if(layout_.columnType(i) == ColumnType::STRING) {
                new (ptr_[i]) std::string(*reinterpret_cast<const std::string*>(other.ptr_[i])); // placement new with copy constructor
            }
        }

        // Register as observer for layout changes (independent from 'other')
        layout_.registerCallback(this, {
               [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
            }
        );
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::RowImpl(RowImpl&& other) noexcept
        : layout_(other.layout_)
        , data_(std::move(other.data_))     // ✓ Transfers heap buffer ownership -> zero-copy move, data stays in place
        , ptr_(std::move(other.ptr_))       // ✓ Pointers remain valid!
        , changes_(std::move(other.changes_))
        // deprceated:
        , offsets_(std::move(other.offsets_))
        , offset_var_(other.offset_var_)
    {
        layout_.registerCallback(this, {
            [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
        });
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::~RowImpl()
    {
        // Unregister from layout callbacks
        layout_.unregisterCallback(this);

        if(ptr_.empty()) {
            return; // Nothing to clean up
        }

        // Ensure pointer are still valid and point into our data buffer (they should, as we moved the vector which owns the buffer)
        size_t columnCount = layout_.columnCount();
        for(size_t i = 0; i < columnCount; ++i) {
            auto type = layout_.columnType(i);
            if(type == ColumnType::STRING) {
                // Manually call destructor for string, as it manages its own memory
                reinterpret_cast<std::string*>(ptr_[i])->~string();
            }
        }
    }

    /** Clear the row to its default state (default values) */
    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::clear()
    {        
        // Use visitor to set all columns to their default values
        visit([](size_t, auto& value, bool&) {
            using T = std::decay_t<decltype(value)>;
            value = defaultValueT<T>();
        });
        // Visitor marks all as changed, which is correct for clear()
    }

    // =========================================================================
    // 1. RAW POINTER ACCESS (caller must ensure type safety)
    // =========================================================================
    template<TrackingPolicy Policy>
    inline const void* RowImpl<Policy>::get(size_t index) const {
        if constexpr (RANGE_CHECKING) {
            if (index >= layout_.columnCount()) {
                return nullptr;
            }
        } else {
            assert(index < layout_.columnCount() && "Row::get(index): Index out of bounds");
        }
        auto ptr = ptr_[index];
        assert( (ptr - data_.data() + sizeOf(layout_.columnType(index)) <= data_.size()) && "Row::get(index): ptr_ should point into data_ buffer");
        return ptr;
    }
   
    // =========================================================================
    // 2. TYPED REFERENCE ACCESS (Row Only) - STRICT
    // Returns const T& (reference) to column value. Row owns aligned memory, so references are safe.
    // Type must match exactly (no conversions).
    // For flexible access with type conversions, use get(index, T& dst) instead.
    // =========================================================================
    template<TrackingPolicy Policy>
    template<typename T>
    inline const T& RowImpl<Policy>::get(size_t index) const {
        // Compile-time type validation
        static_assert(
            std::is_trivially_copyable_v<T> || 
            std::is_same_v<T, std::string> || 
            std::is_same_v<T, std::string_view> || 
            std::is_same_v<T, std::span<const char>>,
            "Row::get<T>: Type T must be either trivially copyable (primitives) or string-related (std::string, std::string_view, std::span<const char>)"
        );
        
        // Use visitor pattern for type-safe access
        const T* result = nullptr;
        
        visitConst(index, [&result](size_t, const auto& value) {
            using ValueType = std::decay_t<decltype(value)>;
            
            // Type must match exactly (compile-time check via if constexpr)
            if constexpr (std::is_same_v<T, ValueType>) {
                result = &value;
            } else {
                // Type mismatch - will throw below
            }
        });
        
        if (!result) [[unlikely]] {
            throw std::runtime_error(
                "Row::get<T>: Type mismatch at index " + std::to_string(index) + 
                ". Requested: " + std::string(toString(toColumnType<T>())) + 
                ", Actual: " + std::string(toString(layout_.columnType(index))));
        }
        
        return *result;
    }

    /** Vectorized access to multiple columns of same type - STRICT.
     *  Types must match exactly (no conversions). Only supports primitive types.
     *  For flexible access with type conversions, use get(index, T& dst) instead.
     */
    template<TrackingPolicy Policy>
    template<typename T>
    inline void RowImpl<Policy>::get(size_t index, std::span<T> &dst) const
    {
        static_assert(std::is_arithmetic_v<T>, "vectorized get() supports arithmetic types only");
        constexpr ColumnType targetType = toColumnType<T>();

        if (RANGE_CHECKING) {
            // combined range (touch each column) and type check
            for (size_t i = index; i < index+dst.size(); ++i) {
                if (targetType != layout_.columnType(i)) [[unlikely]]{
                    throw std::runtime_error("vectorized get() types must match exactly");
                }
            }
        }

        // All type checks passed, safe to get values
        // Exploit the fact that types are aligned within the row data_ and that we don't have to expect padding bytes between the values. Thus we can do a fast copy.
        assert(ptr_[index] >= data_.data() && ptr_[index] + dst.size() * sizeof(T) <= data_.data() + data_.size() && "Row::get(span): ptr_ should point into data_ buffer");
        memcpy(dst.data(), ptr_[index], dst.size() * sizeof(T));
    }

    // =========================================================================
    // 3. FLEXIBLE / CONVERTING ACCESS (Best for Generic/Shared Interfaces)
    // Supports implicit type conversions (e.g., int8→int, float→double, string→string_view).
    // Returns false if conversion not possible, true on success.
    // For strict access without conversions, use get<T>(index) instead.
    // =========================================================================
    template<TrackingPolicy Policy>
    template<typename T>
    inline bool RowImpl<Policy>::get(size_t index, T &dst) const {
        // Use visitor pattern for flexible type conversion
        bool success = false;
        
        visitConst(index, [&dst, &success](size_t, const auto& value) {
            using SrcType = std::decay_t<decltype(value)>;
            
            // Check if C++ allows assignment (e.g. int = int8_t)
            if constexpr (std::is_assignable_v<T&, SrcType>) {
                dst = value;
                success = true;
            } else if constexpr (std::is_same_v<SrcType, std::string>) {
                // Special string conversions
                if constexpr (std::is_same_v<T, std::string>) {
                    dst = value; // deep copy
                    success = true;
                } else if constexpr (std::is_same_v<T, std::string_view>) {
                    dst = std::string_view(value); // zero-copy view
                    success = true;
                } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                    dst = std::span<const char>(value); // zero-copy span
                    success = true;
                } else if constexpr (std::is_assignable_v<T&, std::string>) {
                    dst = value; // fallback for other assignable types
                    success = true;
                }
            }
        });
        
        return success;
    }

    template<TrackingPolicy Policy>
    template<typename T>
    inline const T& RowImpl<Policy>::ref(size_t index) const
    {
        const T &r = get<T>(index);
        return r;
    }

    template<TrackingPolicy Policy>
    template<typename T>
    inline T& RowImpl<Policy>::ref(size_t index) {
        const T &r = get<T>(index);
        // marks column as changed
        if constexpr (isTrackingEnabled(Policy)) {
            changes_.set(index);
        }
        return const_cast<T&>(r);
    } 

    /** 
     * Set the value at the specified column index.
     * 
     * Uses C++20 concepts to constrain acceptable types at compile-time,
     * providing clear error messages for unsupported types.
     * 
     * @param index Column index
     * @param value Value to set (primitives, strings, or convertible types)
     * @return true on success, throws on type mismatch
     */
    template<TrackingPolicy Policy>
    template<detail::BcsvAssignable T>
    inline void RowImpl<Policy>::set(size_t index, const T& value) {

    // Strict type matching via visit()
    visit(index, [value, index](size_t, auto& colValue, bool& changed) {
        using ColType = std::decay_t<decltype(colValue)>;

        if constexpr (std::is_same_v<ColType, T>) {
            // Exact type match - direct assignment
            changed = colValue != value;
            colValue = value;
        } else if constexpr (std::is_same_v<ColType, std::string> && std::is_convertible_v<T, std::string>) {
            changed = colValue != value;
            colValue = value; // Move assignment for efficiency
        } else if constexpr (std::is_assignable_v<ColType&, const T&>) {
            ColType oldValue = colValue;
            colValue = value;
            changed = colValue != oldValue;
        } else {
            throw std::runtime_error("Row::set<T>: Type mismatch at index " + std::to_string(index) + 
                                     ". Cannot assign value of type " + std::string(toString(toColumnType<T>())) + 
                                     " to column of type " + std::string(toString(toColumnType<ColType>())));
        }
    });
}



    /** Vectorized set of multiple columns of same type */
    template<TrackingPolicy Policy>
    template<typename T>
    inline void RowImpl<Policy>::set(size_t index, std::span<const T> values)
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

        T* dst = reinterpret_cast<T*>(ptr_[index]);
        assert(reinterpret_cast<std::byte*>(dst) >= data_.data() && 
               reinterpret_cast<std::byte*>(dst + values.size()) <= data_.data() + data_.size() && 
               "Row::set(span): ptr_ should point into data_ buffer");
        if constexpr (isTrackingEnabled(Policy)) {
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
    template<TrackingPolicy Policy>
    inline std::span<std::byte> RowImpl<Policy>::serializeTo(ByteBuffer& buffer) const  {
        const size_t  off_row = buffer.size();          // offset to the begin of this row
        size_t        off_fix = off_row;                // tracking offset(position) within the fixed section of the buffer. Starts at the begin of this row, used to write fixed data.
        size_t        off_var = offset_var_;            // tracking offset(position) within the variable section of the buffer. Starts just after the fixed section for this row.
        
        // resize buffer to have enough room to hold the entire fixed section of the row
        buffer.resize(buffer.size() + offset_var_);
        size_t count = layout_.columnCount();
        for(size_t i=0; i<count; ++i) {
            ColumnType type = layout_.columnType(i);
            const std::byte* ptr = ptr_[i];
            assert(ptr >= data_.data() && ptr + sizeOf(type) <= data_.data() + data_.size() && "Row::serializeTo: ptr_ should point into data_ buffer");

            if (type == ColumnType::STRING) {
                const std::string& str = *reinterpret_cast<const std::string*>(ptr);               
                // Create StringAddr pointing to string data in variable section, handles truncation
                StringAddr strAddr(off_var, str.length());
                
                // Write StringAddr to fixed section
                std::memcpy(&buffer[off_fix], &strAddr, sizeof(strAddr));
                off_fix += sizeof(strAddr);  // advance fixed offset
                
                // Append actual string data to variable section (only if non-empty)
                if (strAddr.length() > 0) {
                    buffer.resize(off_row + off_var + strAddr.length());
                    std::memcpy(&buffer[off_row + off_var], str.data(), strAddr.length());
                    off_var += strAddr.length();           // advance variable offset
                }
                
            } else {
                size_t len = wireSizeOf(type);
                std::memcpy(&buffer[off_fix], ptr, len);
                off_fix += len; // advance fixed offset
            }
        }
        return {&buffer[off_row], buffer.size() - off_row};
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::deserializeFrom(const std::span<const std::byte> buffer)
    {
        size_t off_fix = 0;           // offset within the fixed section of the buffer. Starts at the begin of this row. We expect that span only contains data for a single row.
        size_t off_var = offset_var_; // offset within the variable section of the buffer. Starts just after the fixed section for this row.
        
        // Safety check: ensure buffer is large enough to contain fixed section
        if (off_var > buffer.size()) [[unlikely]] {
            throw std::runtime_error("Row::deserializeFrom failed as buffer is too short");
        }
        
        size_t count = layout_.columnCount();
        for(size_t i = 0; i < count; ++i) {
            ColumnType type = layout_.columnType(i);
            std::byte* ptr = ptr_[i];
            assert(ptr >= data_.data() && ptr + sizeOf(type) <= data_.data() + data_.size() && "Row::deserializeFrom: ptr_ should point into data_ buffer");
            
            if (type == ColumnType::STRING) {
                StringAddr strAddr;
                assert(wireSizeOf(type) == sizeof(strAddr));
                std::memcpy(&strAddr, &buffer[off_fix], sizeof(strAddr));                
                if (strAddr.offset() + strAddr.length() > buffer.size()) {
                    throw std::runtime_error("Row::deserializeFrom String payload extends beyond buffer");
                }
                
                std::string& str = *reinterpret_cast<std::string*>(ptr);
                if (strAddr.length() > 0) {
                    str.assign(reinterpret_cast<const char*>(&buffer[strAddr.offset()]), strAddr.length());
                } else {
                    str.clear();
                }
            } else {
                assert(wireSizeOf(type) == sizeOf(type));
                std::memcpy(ptr, &buffer[off_fix], wireSizeOf(type));
            }
            off_fix += wireSizeOf(type); // advance fixed offset
        }
    }

    /** Serialize the row into the provided buffer using Zero-Order-Hold (ZoH) compression.
     * Only the columns that have changed (as indicated by the change tracking bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change bitset.
     * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
     * @return A span pointing to the serialized row data within the buffer.
     */
    template<TrackingPolicy Policy>
    inline std::span<std::byte> RowImpl<Policy>::serializeToZoH(ByteBuffer& buffer) const {
        if constexpr (!isTrackingEnabled(Policy)) {
            throw std::runtime_error("Row::serializeToZoH() requires tracking policy enabled");
        }

        // preserve offset to beginning of row  
        size_t bufferSizeOld = buffer.size();

        // Early exit if no changes
        if(!hasAnyChanges()) {
            // nothing to serialize --> skip and exit early
            // Return empty span without accessing buffer elements
            return std::span<std::byte>{}; 
        }


        // reserve space for rowHeader (bitset) at the begin of the row
        bitset<> rowHeader = changes_; // make a copy to modify for bools
        buffer.resize(buffer.size() + rowHeader.sizeBytes());

        // Serialize each element that has changed
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            const std::byte* ptr = ptr_[i];
            assert(ptr >= data_.data() && ptr + sizeOf(type) <= data_.data() + data_.size() && "Row::serializeToZoH: ptr_ should point into data_ buffer");      
            if (type == ColumnType::BOOL) {
                // Special handling for bools: always serialize to single bit in the rowHeader
                bool val = *reinterpret_cast<const bool*>(ptr);
                rowHeader[i] = val; // set bit in rowHeader to represent bool value
            } else if (changes_.test(i)) {
                size_t off = buffer.size();
                if(type == ColumnType::STRING) {
                    // Special handling for strings - encoding in ZoH mode happens in place
                    // Indices in string vector
                    const std::string& str = *reinterpret_cast<const std::string*>(ptr);
                    uint16_t strLength = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
                    buffer.resize(buffer.size() + sizeof(strLength) + strLength);
                    std::memcpy(&buffer[off], &strLength, sizeof(strLength));
                    if (strLength > 0) {
                        std::memcpy(&buffer[off + sizeof(strLength)], str.data(), strLength);
                    }
                } else {
                    size_t len = wireSizeOf(type);
                    assert(len == sizeOf(type));
                    buffer.resize(buffer.size() + len);
                    std::memcpy(&buffer[off], ptr, len);
                }
            }
        }

        // Write rowHeader to the begin of the row
        memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());
        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::deserializeFromZoH(const std::span<const std::byte> buffer) {
        if constexpr (!isTrackingEnabled(Policy)) {
            throw std::runtime_error("Row::deserializeFromZoH() requires tracking policy enabled");
        }
        
        // buffer starts with the change bitset, followed by the actual row data.
        if (buffer.size() >= changes_.sizeBytes()) {
            std::memcpy(changes_.data(), &buffer[0], changes_.sizeBytes());  
        } else [[unlikely]] {
            throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small to contain change bitset.");
        }        
        
        // Deserialize each element that has changed
        size_t offset = changes_.sizeBytes();
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            std::byte* ptr = ptr_[i];
            if (type == ColumnType::BOOL) {
                // always deserialize bools from bitset
                *reinterpret_cast<bool*>(ptr) = changes_[i];
            
            } else if (changes_.test(i)) {
                // deserilialize other types only if they have changed
                if(type == ColumnType::STRING) {
                    // Special handling of strings: note ZoH encoding places string data next to string length (in place encoding)
                    // read string length
                    uint16_t strLength; 
                    if (offset + sizeof(strLength) > buffer.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    std::memcpy(&strLength, &buffer[offset], sizeof(strLength));
                    offset += sizeof(strLength);
                    
                    // read string data
                    if (offset + strLength > buffer.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    std::string& str = *reinterpret_cast<std::string*>(ptr);
                    if (strLength > 0) {
                        str.assign(reinterpret_cast<const char*>(&buffer[offset]), strLength);
                    } else {
                        str.clear();
                    }
                    offset += strLength;
                } else {
                    size_t len = wireSizeOf(type);
                    if (offset + len > buffer.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    
                    std::memcpy(ptr, &buffer[offset], len);
                    offset += len;
                }
            }
        }
    }

    /** @brief Visit a range of columns with read-only access
     * 
     * Invokes the visitor callable for columns in range [startIndex, startIndex + count).
     * 
     * @tparam Visitor Callable type
     * @param startIndex First column index to visit
     * @param visitor Callable accepting `(size_t index, const T& value)`
     * @param count Number of columns to visit (default: 1 for single column, 0 visits nothing)
     * 
     * @par Example - Visit single column
     * @code
     * row.visit(2, [](size_t idx, const auto& value) {
     *     std::cout << "Column " << idx << ": " << value << std::endl;
     * });  // Default count=1
     * @endcode
     * 
     * @par Example - Visit multiple columns
     * @code
     * row.visit(3, [](size_t idx, const auto& value) {
     *     std::cout << value << " ";
     * }, 5);  // Visit columns 3-7
     * @endcode
     * 
     * @see Row::visitConst(Visitor&&) const for visiting all columns
     */
    template<TrackingPolicy Policy>
    template<RowVisitorConst Visitor>
    inline void RowImpl<Policy>::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) 
            return;  // Nothing to visit
        
        size_t endIndex = startIndex + count;
        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("Row::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "Row::visit: Start index out of bounds");
        }
        
        // Core implementation: iterate and dispatch for each column
        for (size_t i = startIndex; i < endIndex; ++i) {
            ColumnType type = layout_.columnType(i);
            const std::byte* ptr = ptr_[i];
            
            // Dispatch based on column type
            switch(type) {
                case ColumnType::BOOL:
                    visitor(i, *reinterpret_cast<const bool*>(ptr));
                    break;
                case ColumnType::INT8:
                    visitor(i, *reinterpret_cast<const int8_t*>(ptr));
                    break;
                case ColumnType::INT16:
                    visitor(i, *reinterpret_cast<const int16_t*>(ptr));
                    break;
                case ColumnType::INT32:
                    visitor(i, *reinterpret_cast<const int32_t*>(ptr));
                    break;
                case ColumnType::INT64:
                    visitor(i, *reinterpret_cast<const int64_t*>(ptr));
                    break;
                case ColumnType::UINT8:
                    visitor(i, *reinterpret_cast<const uint8_t*>(ptr));
                    break;
                case ColumnType::UINT16:
                    visitor(i, *reinterpret_cast<const uint16_t*>(ptr));
                    break;
                case ColumnType::UINT32:
                    visitor(i, *reinterpret_cast<const uint32_t*>(ptr));
                    break;
                case ColumnType::UINT64:
                    visitor(i, *reinterpret_cast<const uint64_t*>(ptr));
                    break;
                case ColumnType::FLOAT:
                    visitor(i, *reinterpret_cast<const float*>(ptr));
                    break;
                case ColumnType::DOUBLE:
                    visitor(i, *reinterpret_cast<const double*>(ptr));
                    break;
                case ColumnType::STRING:
                    visitor(i, *reinterpret_cast<const std::string*>(ptr));
                    break;
                default: [[unlikely]]
                    throw std::runtime_error("Row::visit() unsupported column type");
            }
        }
    }

    /** @brief Visit all columns with read-only access
     * 
     * Iterates through all columns in order, invoking the visitor callable for each.
     * 
     * @tparam Visitor Callable type - see row_visitors.h for concepts and examples
     * 
     * @par Visitor Signature (const version)
     * Must accept: `(size_t index, const T& value)`
     * - `index` - Column index (0 to columnCount-1)
     * - `value` - Column value (type depends on column: int32_t, double, std::string, etc.)
     * 
     * @par Example - CSV output
     * @code
     * row.visit([&](size_t index, const auto& value) {
     *     if (index > 0) std::cout << ",";
     *     std::cout << value;
     * });
     * @endcode
     * 
     * @par Example - Type-specific processing
     * @code
     * row.visit([](size_t index, const auto& value) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_arithmetic_v<T>) {
     *         // Process numeric values
     *     } else if constexpr (std::is_same_v<T, std::string>) {
     *         // Process strings
     *     }
     * });
     * @endcode
     * 
     * @par Example - Using helper types
     * @code
     * #include <bcsv/row_visitors.h>
     * row.visit(bcsv::visitors::csv_visitor{std::cout});
     * @endcode
     * 
     * @see row_visitors.h for concepts, helper types, and more examples
     * @see Row::visit(Visitor&&) for mutable version
     * @see Row::visit(size_t, Visitor&&, size_t) const for visiting a range or single column
     */
    template<TrackingPolicy Policy>
    template<RowVisitorConst Visitor>
    inline void RowImpl<Policy>::visitConst(Visitor&& visitor) const {
        // Delegate to range-based visitor for all columns
        visitConst(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    /** @brief Visit a range of columns with mutable access and change tracking
     * 
     * Invokes the visitor callable for columns in range [startIndex, startIndex + count).
     * 
     * @tparam Visitor Callable type
     * @param startIndex First column index to visit
    * @param visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * @param count Number of columns to visit (default: 1 for single column, 0 visits nothing)
     * 
    * @par Visitor Signatures
    * - **Tracking enabled**: `(size_t index, T& value, bool& changed)`
    * - **Tracking disabled**: `(size_t index, T& value)` or `(size_t index, T& value, bool& changed)`
    *
    * @par Example - Modify single column
     * @code
     * row.visit(2, [](size_t idx, auto& value, bool& changed) {
     *     auto old = value;
     *     value *= 2;
     *     changed = (value != old);
     * });  // Default count=1
     * @endcode
     * 
     * @par Example - Modify multiple columns
     * @code
     * row.visit(3, [](size_t idx, auto& value) {
     *     value = 0;  // Reset to zero
     * }, 5);  // Visit columns 3-7
     * @endcode
     * 
     * @see Row::visit(Visitor&&) for visiting all columns
     */
    template<TrackingPolicy Policy>
    template<RowVisitor Visitor>
    inline void RowImpl<Policy>::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;  // Nothing to visit
        
        size_t endIndex = startIndex + count;
        
        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("Row::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "Row::visit: Start index out of bounds");
        }
        
        // Core implementation: iterate and dispatch for each column
        for (size_t i = startIndex; i < endIndex; ++i) {
            std::byte* ptr = ptr_[i];
            ColumnType type = layout_.columnType(i);

            auto dispatch = [&](auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (isTrackingEnabled(Policy)) {
                    static_assert(std::is_invocable_v<Visitor, size_t, T&, bool&>,
                                  "Row::visit() with tracking requires (size_t, T&, bool&)");
                    bool changed = true;
                    visitor(i, value, changed);
                    changes_[i] |= changed;
                } else {
                    if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                        bool changed = true;
                        visitor(i, value, changed);
                    } else {
                        static_assert(std::is_invocable_v<Visitor, size_t, T&>,
                                      "Row::visit() requires (size_t, T&) or (size_t, T&, bool&)");
                        visitor(i, value);
                    }
                }
            };

            // Dispatch based on column type
            switch(type) {
                case ColumnType::BOOL:
                    dispatch(*reinterpret_cast<bool*>(ptr));
                    break;
                case ColumnType::INT8:
                    dispatch(*reinterpret_cast<int8_t*>(ptr));
                    break;
                case ColumnType::INT16:
                    dispatch(*reinterpret_cast<int16_t*>(ptr));
                    break;
                case ColumnType::INT32:
                    dispatch(*reinterpret_cast<int32_t*>(ptr));
                    break;
                case ColumnType::INT64:
                    dispatch(*reinterpret_cast<int64_t*>(ptr));
                    break;
                case ColumnType::UINT8:
                    dispatch(*reinterpret_cast<uint8_t*>(ptr));
                    break;
                case ColumnType::UINT16:
                    dispatch(*reinterpret_cast<uint16_t*>(ptr));
                    break;
                case ColumnType::UINT32:
                    dispatch(*reinterpret_cast<uint32_t*>(ptr));
                    break;
                case ColumnType::UINT64:
                    dispatch(*reinterpret_cast<uint64_t*>(ptr));
                    break;
                case ColumnType::FLOAT:
                    dispatch(*reinterpret_cast<float*>(ptr));
                    break;
                case ColumnType::DOUBLE:
                    dispatch(*reinterpret_cast<double*>(ptr));
                    break;
                case ColumnType::STRING: {
                    std::string* str = reinterpret_cast<std::string*>(ptr);
                    dispatch(*str);
                    if (str->size() > MAX_STRING_LENGTH) {
                        // Ensure string length does not exceed maximum allowed (for serialization safety)
                        str->resize(MAX_STRING_LENGTH);
                    }
                    break;
                }
                default: [[unlikely]]
                    throw std::runtime_error("Row::visit() unsupported column type");
            }
        }
    }

    /** @brief Visit all columns with mutable access and change tracking
     * 
     * Iterates through all columns, allowing modification. Supports fine-grained
     * change tracking for optimal ZoH (Zero-Order-Hold) compression.
     * 
     * @tparam Visitor Callable type - see row_visitors.h for concepts and examples
     * 
     * @par Visitor Signature (non-const version)
     * 
    * **Tracking enabled signature:**
    * ```cpp
    * (size_t index, T& value, bool& changed)
    * ```
    * **Tracking disabled signature:**
    * ```cpp
    * (size_t index, T& value)
    * ```
     * - `index` - Column index
     * - `value` - Mutable reference to column value
     * - `changed` - Output: set to `true` if modified, `false` to skip marking
     * 
     * **Note:** You can ignore the `changed` parameter by omitting its name:
     * ```cpp
     * [](size_t index, auto& value, bool&) { value *= 2; }
     * ```
     * 
     * @par Example - Fine-grained change tracking
     * @code
     * row.visit([](size_t index, auto& value, bool& changed) {
     *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
     *         auto old = value;
     *         value *= 2;
     *         changed = (value != old);  // Only mark if actually modified
     *     } else {
     *         changed = false;  // Don't mark strings as changed
     *     }
     * });
     * @endcode
     * 
     * @par Example - Ignoring change parameter
     * @code
     * row.visit([](size_t index, auto& value, bool&) {
     *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
     *         value *= 2;  // Changed parameter not used
     *     }
     * });
     * @endcode
     * 
    * @par Change Tracking Behavior
     * - **With tracking enabled**: Changed columns are marked in internal bitset
     * - **Respects `changed` flag**: Only columns with `changed=true` are marked
    * - **Without tracking**: No overhead, changes not recorded
     * 
     * @warning Only modifies columns through set() if you need type conversion or validation.
     *          Direct modification via visit() bypasses those checks but is more efficient.
     * 
     * @see row_visitors.h for concepts, helper types, and more examples
     * @see Row::visit(Visitor&&) const for read-only version
     * @see Row::visit(size_t, Visitor&&, size_t) for visiting a range or single column
     * @see Row::hasAnyChanges() to check if any columns were modified
     */
    template<TrackingPolicy Policy>
    template<RowVisitor Visitor>
    inline void RowImpl<Policy>::visit(Visitor&& visitor) {
        // Delegate to range-based visitor for all columns
        visit(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    // ========================================================================
    // Observer Callbacks (Layout Mutation Notifications)
    // ========================================================================

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::onLayoutUpdate(const std::vector<Layout::Data::Change>& changes) {
        // Handle empty changes (no-op)
        if (changes.empty()) {
            return;
        }

        // safe old data
        std::vector<std::byte> oldData = std::move(data_);
        std::vector<std::byte*> oldPtr = std::move(ptr_);
        size_t oldColumnCount = layout_.columnCount();
        assert(oldPtr.size() == oldColumnCount);
        for(size_t i = 0; i < oldColumnCount; ++i) {
            auto oldType = layout_.columnType(i);
            // for now we simply scrap the old data --> call destructors
            if(oldType == ColumnType::STRING) {
                reinterpret_cast<std::string*>(oldPtr[i])->~string();
            }
        }

        // For simplicity, we will rebuild the entire row based on the new layout.
        std::vector<ColumnType> newTypes;
        for (const auto& change : changes) {
            if (change.newType != ColumnType::VOID) {
                newTypes.push_back(change.newType);
            }
        }
        
        // Calculate new offsets
        uint32_t offset = 0;
        size_t newColumnCount = newTypes.size();
        std::vector<uint32_t> newOffsets(newColumnCount);
        for (size_t i = 0; i < newColumnCount; ++i) {
            uint32_t alignment = alignOf(newTypes[i]);
            offset = (offset + (alignment - 1)) & ~(alignment - 1);
            newOffsets[i] = offset;
            offset += sizeOf(newTypes[i]);
        }
        
        // Rebuild storage
        data_.clear();
        data_.resize(offset);
        ptr_.resize(newColumnCount);
        
        // Initialize all columns with default values
        for (size_t i = 0; i < newColumnCount; ++i) {
            ptr_[i] = &data_[newOffsets[i]];
            constructDefaultAt(ptr_[i], newTypes[i]);
        }
        
        // Reset change tracking
        if constexpr (isTrackingEnabled(Policy)) {
            changes_.resize(newColumnCount);
            changes_.set();  // Mark all as changed
        }
        
        // Update deprecated offsets_ and offset_var_
        offsets_ = std::move(newOffsets);
        offset_var_ = 0;
        for (size_t i = 0; i < newColumnCount; ++i) {
            offset_var_ += wireSizeOf(newTypes[i]);
        }
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>& RowImpl<Policy>::operator=(const RowImpl& other)
    {
        if(this == &other) {
            return *this; // self-assignment check
        }

        // Require compatible layouts - incompatible assignments not supported
        if(!layout_.isCompatible(other.layout())) [[unlikely]] {
            throw std::invalid_argument(
                "Row::operator=: Cannot assign between incompatible layouts. "
                "Layouts must have the same column types in the same order."
            );
        }
        
        // Use visitor to copy values and detect actual changes
        other.visitConst([this](size_t i, const auto& newValue) {
            using T = std::decay_t<decltype(newValue)>;
            
            // Get current value for comparison
            const T& currentValue = this->get<T>(i);
            
            // Check if value actually changed
            if(currentValue != newValue) {
                // Assign new value (works for all types including strings)
                *reinterpret_cast<T*>(ptr_[i]) = newValue;
                if constexpr (isTrackingEnabled(Policy)) {
                    changes_[i] = true; // Mark this column as changed
                }
            } 
        });
        
        return *this;
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>& RowImpl<Policy>::operator=(RowImpl&& other) noexcept 
    {
        if (this == &other) 
            return *this;
        
        // Clean up current state
        layout_.unregisterCallback(this);
        if(!ptr_.empty()) {
            for(size_t i = 0; i < layout_.columnCount(); ++i) {
                if(layout_.columnType(i) == ColumnType::STRING) {
                    reinterpret_cast<std::string*>(ptr_[i])->~string();
                }
            }
        }
        
        // Move other's data
        layout_ = other.layout_;
        data_ = std::move(other.data_);     // Transfers heap buffer ownership -> zero-copy move
        ptr_  = std::move(other.ptr_);       // Pointers remain valid (point into same heap buffer now owned by this->data_)
        changes_ = std::move(other.changes_);
        
        // Re-register with new this pointer
        layout_.registerCallback(this, {
            [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
        });
        
        // dependency: offsets_ and offset_var_ are derived from layout and data, so we need to reconstruct them based on the new layout and data after move
        offsets_ = std::move(other.offsets_);
        offset_var_ = other.offset_var_;
        return *this;
    }

    // ========================================================================
    // RowView Implementation
    // ========================================================================

    inline RowView::RowView(const Layout& layout, std::span<std::byte> buffer)
        : layout_(layout), offsets_(layout_.columnCount()), offset_var_(0), buffer_(buffer)
    {        
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            offsets_[i] = offset_var_;
            ColumnType type = layout_.columnType(i);
            offset_var_ += wireSizeOf(type);
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
        size_t fieldLen = wireSizeOf(type);


        // 4. Validate Fixed Section Bounds
        // Ensure the fixed field (or the StringAddress) fits in the buffer
        if (offset + fieldLen > buffer_.size()) [[unlikely]] {
             return {}; 
        }

        // 5. Handle String vs Primitive
        if (type == ColumnType::STRING) {
            // Unpack string address safely using memcpy (alignment safe)
            StringAddr strAddr;
            std::memcpy(&strAddr, &buffer_[offset], sizeof(strAddr));
            
            // 6. Validate Variable Section Bounds
            if (strAddr.offset() + strAddr.length() > buffer_.size()) [[unlikely]] {
                return {}; // string data extends beyond buffer
            }

            // Return span covering the actual string payload
            return { &buffer_[strAddr.offset()], strAddr.length() };
        } else {
            // Return span covering the primitive value
            return { &buffer_[offset], fieldLen };
        }
    }
   

    /** Vectorized access to multiple columns of same type - STRICT.
     *  Types must match exactly (no conversions). Only arithmetic types supported.
     *  Returns false on type mismatch or bounds error.
     *  For flexible access with type conversions, use get(index, T& dst) instead.
     */
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
        memcpy(dst.data(), &buffer_[offsets_[index]], dst.size() * sizeof(T));
        return true;
    }

    /** Strict Typed Access - STRICT.
     *  Returns T by value (not reference) - buffer data may not be aligned, so we cannot safely return references to primitives.
     *  Primitives: Returned by value (copied via memcpy, handles misalignment safely).
     *  Strings: Returns std::string/std::string_view/std::span<const char> (zero-copy, alignment not required for byte arrays).
     *  Type must match exactly (no implicit conversions for primitives).
     *  Throws std::runtime_error on type mismatch or bounds error.
     *  For flexible access with type conversions, use get(index, T& dst) instead.
     */
    template<typename T>
    inline T RowView::get(size_t index) const {
        // Compile-time check: std::string types must only be used with STRING columns (checked at runtime)
        static_assert(
            std::is_trivially_copyable_v<T> || 
            std::is_same_v<T, std::string> || 
            std::is_same_v<T, std::string_view> || 
            std::is_same_v<T, std::span<const char>>,
            "RowView::get<T>: Type T must be either trivially copyable (primitives) or string-related"
        );
        
        // Use visitor pattern for type-safe access with strict type matching
        T result;
        bool found = false;
        
        visitConst(index, [&](size_t, auto&& value) {
            using ValueType = std::decay_t<decltype(value)>;
            
            // String type handling
            if constexpr (std::is_same_v<T, std::string> || 
                          std::is_same_v<T, std::string_view> || 
                          std::is_same_v<T, std::span<const char>>) {
                if constexpr (std::is_same_v<ValueType, std::string_view>) {
                    // String type requested and column is STRING
                    if constexpr (std::is_same_v<T, std::string_view>) {
                        result = value;
                    } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                        result = std::span<const char>(value.data(), value.size());
                    } else { // std::string
                        result = std::string(value);
                    }
                    found = true;
                } else {
                    // String type requested but column is not STRING - error
                }
            } else {
                // Primitive type requested
                if constexpr (std::is_same_v<ValueType, std::string_view>) {
                    // Primitive type requested but column is STRING - error
                } else if constexpr (std::is_same_v<T, ValueType>) {
                    // Exact type match
                    result = value;
                    found = true;
                }
            }
        }, 1);
        
        if (!found) {
            // Type mismatch occurred
            ColumnType actualType = layout_.columnType(index);
            ColumnType requestedType = std::is_same_v<T, std::string> || 
                                        std::is_same_v<T, std::string_view> || 
                                        std::is_same_v<T, std::span<const char>> 
                                        ? ColumnType::STRING 
                                        : toColumnType<T>();
            throw std::runtime_error("RowView::get<T>: Type mismatch at index " + 
                                     std::to_string(index) + 
                                     ". Requested: " + std::string(toString(requestedType)) + 
                                     ", Actual: " + std::string(toString(actualType)));
        }
        
        return result;
    }

    /** Flexible access with type conversions - FLEXIBLE.
     *  Supports implicit type conversions (e.g., int8→int, float→double, string→string_view).
     *  Returns false if conversion not possible or on bounds error, true on success.
     *  For strict access without conversions, use get<T>(index) instead.
     */
    template<typename T>
    inline bool RowView::get(size_t index, T &dst) const {
        // Use visitor pattern for flexible type conversions
        bool success = false;
        
        try {
            visitConst(index, [&](size_t, auto&& value) {
                using ValueType = std::decay_t<decltype(value)>;
                
                // Try to assign value to dst (compiler handles implicit conversions)
                if constexpr (std::is_assignable_v<T&, ValueType>) {
                    dst = value;
                    success = true;
                } else if constexpr (std::is_same_v<ValueType, std::string_view>) {
                    // Special handling for string types
                    if constexpr (std::is_same_v<T, std::string>) {
                        dst.assign(value.data(), value.size());
                        success = true;
                    } else if constexpr (std::is_same_v<T, std::string_view>) {
                        dst = value;
                        success = true;
                    } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                        dst = std::span<const char>(value.data(), value.size());
                        success = true;
                    } else if constexpr (std::is_assignable_v<T&, std::string_view>) {
                        dst = value;
                        success = true;
                    }
                }
            }, 1);
        } catch (...) {
            return false;
        }
        
        return success;
    }

    /** Sets the value at the specified column index. Types must match exactly. 
    Note: Only supports primitive types (arithmetic and bool). */
    template<typename T>
    inline bool RowView::set(size_t index, const T& value) {
        using DecayedT = std::decay_t<T>;
        static_assert(std::is_arithmetic_v<DecayedT>, "RowView::set<T> supports primitive arithmetic types and bool only");
        
        // Use visitor pattern for strict type-matched write
        bool success = false;
        
        try {
            visit(index, [&](size_t, auto& colValue, bool&) {
                using ColType = std::decay_t<decltype(colValue)>;
                
                // Strict type match required
                if constexpr (std::is_same_v<DecayedT, ColType>) {
                    colValue = value;
                    success = true;
                }
            }, 1);
        } catch (...) {
            return false;
        }
        
        return success;
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
        Row row(layout());
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
                    if (strAddr.offset() + strAddr.length() > size) {
                        return false;
                    }
                }
            }    
        }
        return true;
    }

    /** @brief Visit a range of columns with read-only access (zero-copy view)
     * 
     * Provides zero-copy access to serialized data through string_view for strings
     * and value copies for primitives. No deserialization overhead for strings.
     * 
     * @tparam Visitor Callable type
     * @param startIndex First column index to visit
     * @param visitor Callable accepting `(size_t index, const T& value)` where T is the column type or string_view for strings
     * @param count Number of columns to visit (default: 1 for single column, 0 visits nothing)
     * 
     * @par Example - Zero-copy string access
     * @code
     * rowView.visit(1, [](size_t idx, std::string_view str) {
     *     std::cout << str;  // No string copy, direct buffer access
     * });
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     * @see RowView::visitConst(Visitor&&) const for visiting all columns
     */
    template<RowVisitorConst Visitor>
    inline void RowView::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) 
            return;  // Nothing to visit
        
        size_t endIndex = startIndex + count;;
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= layout_.columnCount()) {
                throw std::out_of_range("RowView::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < layout_.columnCount() && "RowView::visit: Start index out of bounds");
        }
        
        // Core implementation: iterate and dispatch for each column
        for (size_t i = startIndex; i < endIndex; ++i) {
            ColumnType type = layout_.columnType(i);
            uint32_t offset = offsets_[i];
            
            // Bounds check
            if (buffer_.empty() || offset >= buffer_.size()) [[unlikely]] {
                throw std::runtime_error("RowView::visit() buffer out of bounds");
            }
            
            const std::byte* ptr = &buffer_[offset];
            
            // Dispatch based on column type
            switch(type) {
                case ColumnType::BOOL: {
                    bool value;
                    std::memcpy(&value, ptr, sizeof(bool));
                    visitor(i, value);
                    break;
                }
                case ColumnType::INT8: {
                    int8_t value;
                    std::memcpy(&value, ptr, sizeof(int8_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::INT16: {
                    int16_t value;
                    std::memcpy(&value, ptr, sizeof(int16_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::INT32: {
                    int32_t value;
                    std::memcpy(&value, ptr, sizeof(int32_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::INT64: {
                    int64_t value;
                    std::memcpy(&value, ptr, sizeof(int64_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::UINT8: {
                    uint8_t value;
                    std::memcpy(&value, ptr, sizeof(uint8_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::UINT16: {
                    uint16_t value;
                    std::memcpy(&value, ptr, sizeof(uint16_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::UINT32: {
                    uint32_t value;
                    std::memcpy(&value, ptr, sizeof(uint32_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::UINT64: {
                    uint64_t value;
                    std::memcpy(&value, ptr, sizeof(uint64_t));
                    visitor(i, value);
                    break;
                }
                case ColumnType::FLOAT: {
                    float value;
                    std::memcpy(&value, ptr, sizeof(float));
                    visitor(i, value);
                    break;
                }
                case ColumnType::DOUBLE: {
                    double value;
                    std::memcpy(&value, ptr, sizeof(double));
                    visitor(i, value);
                    break;
                }
                case ColumnType::STRING: {
                    // Decode StringAddr
                    StringAddr strAddr;
                    std::memcpy(&strAddr, ptr, sizeof(StringAddr));
                    
                    size_t strOffset = strAddr.offset();
                    size_t strLength = strAddr.length();
                    
                    // Bounds check for string payload
                    if (strOffset + strLength > buffer_.size()) [[unlikely]] {
                        throw std::runtime_error("RowView::visit() string payload out of bounds");
                    }
                    
                    // Create string_view pointing into buffer (zero-copy)
                    std::string_view strValue(
                        reinterpret_cast<const char*>(&buffer_[strOffset]), 
                        strLength
                    );
                    visitor(i, strValue);
                    break;
                }
                default: [[unlikely]]
                    throw std::runtime_error("RowView::visit() unsupported column type");
            }
        }
    }

    /** @brief Visit all columns with read-only access (zero-copy view)
     * 
     * Efficient iteration through serialized data with zero-copy string views.
     * Primitives are copied, strings are accessed via std::string_view into buffer.
     * 
     * @tparam Visitor Callable accepting `(size_t index, const T& value)` - T varies by column
     * 
     * @par Example - Mixed type processing
     * @code
     * rowView.visit([](size_t idx, const auto& value) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_same_v<T, std::string_view>) {
     *         // Zero-copy string access
     *     } else {
     *         // Primitive value copy
     *     }
     * });
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     * @see RowView::visitConst(size_t, Visitor&&, size_t) const for range access
     */
    template<RowVisitorConst Visitor>
    inline void RowView::visitConst(Visitor&& visitor) const {
        // Delegate to range-based visitor for all columns
        visitConst(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    /** @brief Visit a range of columns with mutable access (primitives only)
     * 
     * Allows in-place modification of primitives in the serialized buffer.
    * Strings are READ-ONLY - cannot resize buffer. Change tracking parameter
    * is accepted but ignored (always treated as changed).
     * 
    * @tparam Visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * @param startIndex First column index to visit
     * @param visitor Callable for in-place modification
     * @param count Number of columns to visit
     * 
    * @par Visitor Signatures
    * - `(size_t index, T& value)`
    * - `(size_t index, T& value, bool& changed)`
    *
    * Note: For string columns, the visitor receives `std::string_view`.
    *
    * @par Example - Modify primitives in-place
     * @code
     * rowView.visit(0, [](size_t idx, auto& value) {
     *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
     *         value *= 2;  // In-place modification
     *     }
     * }, 5);
     * @endcode
     * 
     * @warning Strings cannot be modified through RowView (buffer size fixed)
     * @see row_visitors.h for concepts and helper types
     */
    template<RowVisitor Visitor>
    inline void RowView::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) 
            return;  // Nothing to visit
        
        size_t endIndex = startIndex + count;
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= layout_.columnCount()) {
                throw std::out_of_range("RowView::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < layout_.columnCount() && "RowView::visit: Start index out of bounds");
        }
        
        // Core implementation: iterate and dispatch for each column
        for (size_t i = startIndex; i < endIndex; ++i) {
            ColumnType type = layout_.columnType(i);
            uint32_t offset = offsets_[i];

            // Bounds check
            if (buffer_.empty() || offset >= buffer_.size()) [[unlikely]] {
                throw std::runtime_error("RowView::visit() buffer out of bounds");
            }

            std::byte* ptr = &buffer_[offset];

            auto dispatch = [&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                    bool changed = true;
                    visitor(i, value, changed);
                } else {
                    static_assert(std::is_invocable_v<Visitor, size_t, T&>,
                                  "RowView::visit() requires (size_t, T&) or (size_t, T&, bool&)");
                    visitor(i, value);
                }
            };

            // Dispatch based on column type - primitives only
            switch(type) {
                case ColumnType::BOOL:
                    dispatch(*reinterpret_cast<bool*>(ptr));
                    break;
                case ColumnType::INT8:
                    dispatch(*reinterpret_cast<int8_t*>(ptr));
                    break;
                case ColumnType::INT16:
                    dispatch(*reinterpret_cast<int16_t*>(ptr));
                    break;
                case ColumnType::INT32:
                    dispatch(*reinterpret_cast<int32_t*>(ptr));
                    break;
                case ColumnType::INT64:
                    dispatch(*reinterpret_cast<int64_t*>(ptr));
                    break;
                case ColumnType::UINT8:
                    dispatch(*reinterpret_cast<uint8_t*>(ptr));
                    break;
                case ColumnType::UINT16:
                    dispatch(*reinterpret_cast<uint16_t*>(ptr));
                    break;
                case ColumnType::UINT32:
                    dispatch(*reinterpret_cast<uint32_t*>(ptr));
                    break;
                case ColumnType::UINT64:
                    dispatch(*reinterpret_cast<uint64_t*>(ptr));
                    break;
                case ColumnType::FLOAT:
                    dispatch(*reinterpret_cast<float*>(ptr));
                    break;
                case ColumnType::DOUBLE:
                    dispatch(*reinterpret_cast<double*>(ptr));
                    break;
                case ColumnType::STRING: {
                    // Strings are read-only in mutable visit (cannot resize buffer)
                    // Decode StringAddr and pass string_view
                    StringAddr strAddr;
                    std::memcpy(&strAddr, ptr, sizeof(StringAddr));

                    size_t strOffset = strAddr.offset();
                    size_t strLength = strAddr.length();

                    if (strOffset + strLength > buffer_.size()) [[unlikely]] {
                        throw std::runtime_error("RowView::visit() string payload out of bounds");
                    }

                    std::string_view strValue(
                        reinterpret_cast<const char*>(&buffer_[strOffset]),
                        strLength
                    );

                    dispatch(strValue);
                    break;
                }
                default: [[unlikely]]
                    throw std::runtime_error("RowView::visit() unsupported column type");
            }
        }
    }

    /** @brief Visit all columns with mutable access (primitives only)
     * 
     * Iterate through all columns allowing primitive modification.
     * Strings remain read-only. No change tracking.
     * 
     * @tparam Visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * 
     * @note Both 2-param and 3-param signatures are accepted, change flag is ignored
     * @see row_visitors.h for concepts and helper types
     * @see RowView::visit(size_t, Visitor&&, size_t) for range access
     */
    template<RowVisitor Visitor>
    inline void RowView::visit(Visitor&& visitor) {
        // Delegate to range-based visitor for all columns
        visit(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    // ========================================================================
    // RowStatic Implementation
    // ========================================================================

    template<TrackingPolicy Policy, typename... ColumnTypes>
    RowStaticImpl<Policy, ColumnTypes...>::RowStaticImpl(const LayoutType& layout) 
        : layout_(layout), data_() 
    {
        clear();
        changes_.reset();
    }

    /** Clear the row to its default state (default values) */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::clear()
    {
        if constexpr (Index < column_count) {
            std::get<Index>(data_) = defaultValueT<column_type<Index>>();
            if constexpr (isTrackingEnabled(Policy)) {
                changes_.set(Index);
            }
            clear<Index + 1>();
        }
    }

    /** Direct reference to column data (compile-time) - STRICT.
     *  Compile-time type-safe access. No overhead, no runtime checks.
     *  Returns const reference to the column value.
     *  For flexible runtime access with type conversions, use get(index, T& dst) instead.
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    const auto& RowStaticImpl<Policy, ColumnTypes...>::get() const noexcept {
        static_assert(Index < column_count, "Index out of bounds");
        return std::get<Index>(data_);
    }

    /** Vectorized static access with smart type handling.
    *  Copies 'Extent' elements starting from 'StartIndex' to 'dst'.
    *  Unrolled at compile time with two paths:
    *  - Fast path: Direct copy when types match exactly
    *  - Conversion path: Element-wise static_cast when types are assignable
    *  Compile-time error if types are not assignable.
    */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStaticImpl<Policy, ColumnTypes...>::get(std::span<T, Extent> &dst) const {
        // 1. Static Checks
        static_assert(Extent != std::dynamic_extent, "RowStatic: Static vectorized get requires fixed-extent span (std::span<T, N>)");
        static_assert(StartIndex + Extent <= column_count, "RowStatic: Access range exceeds column count");

        // 2. Check assignability (compile-time error if not convertible)
        [&]<size_t... I>(std::index_sequence<I...>) {
            static_assert((std::is_assignable_v<T&, const column_type<StartIndex + I>&> && ...), 
                "RowStatic::get(span): Column types are not assignable to destination span type");
        }(std::make_index_sequence<Extent>{});

        // 3. Choose fast path or conversion path at compile time
        constexpr bool all_types_match = []<size_t... I>(std::index_sequence<I...>) {
            return ((std::is_same_v<T, column_type<StartIndex + I>>) && ...);
        }(std::make_index_sequence<Extent>{});
        
        if constexpr (all_types_match) {
            // Fast path: All types match exactly - direct access
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((dst[I] = std::get<StartIndex + I>(data_)), ...);
            }(std::make_index_sequence<Extent>{});
        } else {
            // Conversion path: Types differ but are assignable - use static_cast
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((dst[I] = static_cast<T>(std::get<StartIndex + I>(data_))), ...);
            }(std::make_index_sequence<Extent>{});
        }
    }

    /** Get raw pointer (void*). Returns nullptr if index invalid.
     *  Resolves the tuple element address at runtime using a fold expression.
     *  Note: The returned pointer is guaranteed to be aligned for the column's type.
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    const void* RowStaticImpl<Policy, ColumnTypes...>::get(size_t index) const noexcept {
        // 1. Bounds check
        if constexpr (RANGE_CHECKING) {
            if (index >= column_count) [[unlikely]] {
                return nullptr;
            }
        } else {
            assert(index < column_count && "RowStatic::get(index): Index out of bounds");
        }

        // 2. Define Function Pointer Signature
        using Self = RowStaticImpl<Policy, ColumnTypes...>;
        using GetterFunc = const void* (*)(const Self&);

        // 3. Generate Jump Table (Static Constexpr)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, column_count>{
                // Lambda capturing behavior for index I
                +[](const Self& self) -> const void* {
                    return &std::get<I>(self.data_);
                }...
            };
        }(std::make_index_sequence<column_count>{});

        // 4. O(1) Dispatch
        return handlers[index](*this);
    }

    /** Get typed reference (strict) - STRICT.
     *  Returns const T& (reference) to column value. RowStatic owns aligned tuple storage, so references are safe.
     *  Type must match exactly (no conversions).
     *  Throws if type mismatch or index invalid.
     *  For flexible access with type conversions, use get(index, T& dst) instead.
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T>
    const T& RowStaticImpl<Policy, ColumnTypes...>::get(size_t index) const {
        // Compile-time type validation
        static_assert(
            std::is_trivially_copyable_v<T> || 
            std::is_same_v<T, std::string> || 
            std::is_same_v<T, std::string_view> || 
            std::is_same_v<T, std::span<const char>>,
            "RowStatic::get<T>: Type T must be either trivially copyable (primitives) or string-related (std::string, std::string_view, std::span<const char>)"
        );
        
        // 1. Reuse raw pointer lookup
        // This makes the code cleaner by isolating the tuple traversal logic in one place.
        const void* ptr = get(index);
        
        if (ptr == nullptr) [[unlikely]] {
             throw std::out_of_range("RowStatic::get<T>: Invalid column index " + std::to_string(index));
        }

        // 2. Strict Type Check - Branch on compile-time type T first for better optimization
        ColumnType actualType = layout_.columnType(index);
        
        if constexpr (std::is_same_v<T, std::string> || 
                      std::is_same_v<T, std::string_view> || 
                      std::is_same_v<T, std::span<const char>>) {
            // String type requested
            if (actualType != ColumnType::STRING) [[unlikely]] {
                throw std::runtime_error(
                    "RowStatic::get<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Requested: string type, Actual: " + std::string(toString(actualType)));
            }
        } else {
            // Primitive type requested
            constexpr ColumnType requestedType = toColumnType<T>();
            if (requestedType != actualType) [[unlikely]] {
                throw std::runtime_error(
                    "RowStatic::get<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Requested: " + std::string(toString(requestedType)) + 
                    ", Actual: " + std::string(toString(actualType)));
            }
        }

        // 3. Safe Cast
        // Since we verified the type (via toColumnType) and the address, this cast is safe.
        return *static_cast<const T*>(ptr);
    }

    /** Vectorized runtime access - STRICT.
     *  Copies data from the row starting at 'index' into the destination span.
     *  Type must match exactly for all columns (no conversions).
     *  Throws std::out_of_range if the range exceeds column count.
     *  Throws std::runtime_error (via get<T>) if any column type does not match T.
     *  For flexible access with type conversions, use get(index, T& dst) instead.
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowStaticImpl<Policy, ColumnTypes...>::get(size_t index, std::span<T, Extent> &dst) const {
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

    /** Flexible copy access with type conversion - FLEXIBLE.
     *  Supports implicit type conversions (e.g., int8→int, float→double, string→string_view).
     *  Returns false if conversion not possible, true on success.
     *  For strict access without conversions, use get<T>(index) instead.
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T>
    bool RowStaticImpl<Policy, ColumnTypes...>::get(size_t index, T& dst) const noexcept {
        // Use visitor pattern for flexible type conversions
        bool success = false;
        
        try {
            visitConst(index, [&](size_t, const auto& value) {
                using SrcType = std::decay_t<decltype(value)>;
                
                // Try to assign value to dst (compiler handles implicit conversions)
                if constexpr (std::is_assignable_v<T&, const SrcType&>) {
                    if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<SrcType>) {
                        dst = static_cast<T>(value);
                    } else {
                        dst = value;
                    }
                    success = true;
                } else if constexpr (std::is_same_v<SrcType, std::string>) {
                    // Special handling for string types
                    if constexpr (std::is_same_v<T, std::string_view>) {
                        dst = std::string_view(value);
                        success = true;
                    } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                        dst = std::span<const char>(value.data(), value.size());
                        success = true;
                    }
                }
            }, 1);
        } catch (...) {
            return false;
        }
        
        return success;
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T>
    const T& RowStaticImpl<Policy, ColumnTypes...>::ref(size_t index) const
    {
        const T &r = get<T>(index);
        return r;
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T>
    T& RowStaticImpl<Policy, ColumnTypes...>::ref(size_t index) {
        const T &r = get<T>(index);
        // marks column as changed
        if constexpr (isTrackingEnabled(Policy)) {
            changes_.set(index);
        }
        return const_cast<T&>(r);
    } 
    
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStaticImpl<Policy, ColumnTypes...>::set(const T& value) {
        static_assert(Index < column_count, "Index out of bounds");


        using DecayedT = std::decay_t<T>;
        if constexpr (std::is_same_v<DecayedT, std::span<char>> || std::is_same_v<DecayedT, std::span<const char>>) {
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
                    if constexpr (isTrackingEnabled(Policy)) {
                        changes_.set(Index);
                    }
                }
            } 

            // Case 3b: Arithmetic types -> convert to string
            else if constexpr (std::is_arithmetic_v<DecayedT>) {
                std::string strVal = std::to_string(value); // cannot extend max length here               
                if (currentVal != strVal) {
                    currentVal = std::move(strVal);
                    if constexpr (isTrackingEnabled(Policy)) {
                        changes_.set(Index);
                    }
                }
            }

            // Case 3c: Single char -> efficient conversion
            else if constexpr (std::is_same_v<DecayedT, char>) {
                if (currentVal.size() != 1 || currentVal[0] != value) {
                    currentVal.assign(1, value);
                    if constexpr (isTrackingEnabled(Policy)) {
                        changes_.set(Index);
                    }
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
                    if constexpr (isTrackingEnabled(Policy)) {
                        changes_.set(Index);
                    }
                }
        }
    } 

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T>
    void RowStaticImpl<Policy, ColumnTypes...>::set(size_t index, const T& value)  {
        // Use visitor pattern for runtime-indexed write
        visit(index, [&](size_t, auto& colValue, bool&) {
            using ColType = std::decay_t<decltype(colValue)>;
            using DecayedT = std::decay_t<T>;
            
            // Delegate to compile-time set which handles type conversions
            // This is necessary because the visitor gives us the actual column type
            // but we need to forward the user's value type to the compile-time version
            // which has all the conversion logic
            if constexpr (std::is_assignable_v<ColType&, const DecayedT&> || 
                          std::is_same_v<DecayedT, std::span<char>> ||
                          std::is_same_v<DecayedT, std::span<const char>>) {
                colValue = value;  // Direct assignment or implicit conversion
            } else if constexpr (std::is_same_v<DecayedT, std::string_view> && std::is_same_v<ColType, std::string>) {
                colValue = std::string(value);
            } else {
                throw std::runtime_error("RowStatic::set: Type mismatch or unsupported conversion");
            }
        }, 1);
    }

    /** Vectorized static set (Compile-Time) */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStaticImpl<Policy, ColumnTypes...>::set(std::span<const T, Extent> values) {
        static_assert(StartIndex + Extent <= column_count, "RowStatic: Range exceeds column count");
        
        [&]<size_t... I>(std::index_sequence<I...>) {
            // Fold set: set<StartIndex+0>(values[0]); ...
            (this->template set<StartIndex + I>(values[I]), ...);
        }(std::make_index_sequence<Extent>{});
    }

    /** Runtime vectorized set with span */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowStaticImpl<Policy, ColumnTypes...>::set(size_t index, std::span<const T, Extent> values) {

        // ToDo: Currently its a simple loop delegating to scalar set(). Should be optimized as we know that elements are contiguous and hence no padding in between.

        // 1. Access Check
        if constexpr (RANGE_CHECKING) {
            if (index + values.size() > column_count) {
                throw std::out_of_range("RowStatic::set(span): Span exceeds column count");
            }
        } else {
            assert(index + values.size() <= column_count && "RowStatic::set(span): Span exceeds column count");
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
    template<TrackingPolicy Policy, typename... ColumnTypes>
    std::span<std::byte> RowStaticImpl<Policy, ColumnTypes...>::serializeTo(ByteBuffer& buffer) const {
        size_t offRow = buffer.size();               // remember where this row starts
        size_t offVar = offset_var_;                 // offset to the begin of variable-size data section (relative to row start)
        buffer.resize(buffer.size() + offset_var_);  // ensure buffer is large enough to hold fixed-size data

        // serialize each tuple element using compile-time recursion
        serializeElements<0>(buffer, offRow, offVar);
        return {&buffer[offRow], buffer.size() - offRow};
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::serializeElements(ByteBuffer& buffer, const size_t& offRow, size_t& offVar) const {
        if constexpr (Index < column_count) {
            constexpr size_t lenFix = column_lengths[Index];
            constexpr size_t offFix = column_offsets[Index];
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t lenVar = std::get<Index>(data_).size();
                StringAddr strAddr(offVar, lenVar);                                                   // Make address relative to row start, handles truncation
                std::memcpy(&buffer[offRow + offFix], &strAddr, sizeof(strAddr));                // write string address
                if (strAddr.length() > 0) {
                    buffer.resize(offRow + offVar + strAddr.length());                                   // Ensure buffer is large enough to hold string payload
                    std::memcpy(&buffer[offRow + offVar], std::get<Index>(data_).c_str(), strAddr.length());   // write string payload
                    offVar += strAddr.length();
                }
            } else {
                std::memcpy(&buffer[offRow + offFix], &std::get<Index>(data_), lenFix);
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
    template<TrackingPolicy Policy, typename... ColumnTypes>
    std::span<std::byte> RowStaticImpl<Policy, ColumnTypes...>::serializeToZoH(ByteBuffer& buffer) const {
        if constexpr (!isTrackingEnabled(Policy)) {
            throw std::runtime_error("RowStatic::serializeToZoH() requires tracking policy enabled");
        }

        // remember where this row starts, (as we are appending to the buffer)
        size_t bufferSizeOld = buffer.size();

        // skips if there is nothing to serialize
        if(!hasAnyChanges()) {
            // Return empty span without accessing buffer elements
            return std::span<std::byte>{}; 
        }

        bitset<column_count> rowHeader = changes_;              // make a copy to modify for bools (changes_ are const!)
        buffer.resize(buffer.size() + rowHeader.sizeBytes());    // reserve space for rowHeader (bitset) at the begin of the row
        
        // Serialize each tuple element using compile-time recursion
        serializeElementsZoH<0>(buffer, rowHeader);

        // after serializing the elements, write the rowHeader to the begin of the row
        // Calculate pointer after all resizes to avoid dangling pointer from vector reallocation
        std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());
        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::serializeElementsZoH(ByteBuffer& buffer, bitset<column_count>& rowHeader) const 
    {
        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, bool>) {
                // store as single bit within serialization_bits
                bool value = std::get<Index>(data_);
                rowHeader.set(Index, value); // mark as changed
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
                    memcpy(&buffer[old_size], strLengthPtr, sizeof(uint16_t));
                    if (strLength > 0) {
                        memcpy(&buffer[old_size + sizeof(uint16_t)], strDataPtr, strLength);
                    }
                } else {
                    // for all other types, we append directly to the end of the buffer
                    const auto& value = std::get<Index>(data_);
                    const std::byte* dataPtr = reinterpret_cast<const std::byte*>(&value);
                
                    buffer.resize(buffer.size() + sizeof(column_type<Index>));
                    memcpy(&buffer[old_size], dataPtr, sizeof(column_type<Index>));
                }
            }
            // Recursively process next element
            return serializeElementsZoH<Index + 1>(buffer, rowHeader); 
        }
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    void RowStaticImpl<Policy, ColumnTypes...>::deserializeFrom(const std::span<const std::byte> buffer)  {
        //we expect the buffer, starts with the first byte of the row and ends with the last byte of the row (no change bitset)
        deserializeElements<0>(buffer);
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::deserializeElements(const std::span<const std::byte> &buffer) 
    {
        if constexpr (Index < column_count) {
            constexpr size_t len = wireSizeOf<column_type<Index>>();
            constexpr size_t off = column_offsets[Index];

            if (off + len > buffer.size()) {
                throw std::runtime_error("RowStatic::deserializeElements() failed! Buffer overflow while reading.");
            }

            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                StringAddr strAddr;
                std::memcpy(&strAddr, &buffer[off], sizeof(strAddr));
                if (strAddr.offset() + strAddr.length() > buffer.size()) {
                    throw std::runtime_error("RowStatic::deserializeElements() failed! Buffer overflow while reading.");
                }
                if (strAddr.length() > 0) {
                    std::get<Index>(data_).assign(reinterpret_cast<const char*>(&buffer[strAddr.offset()]), strAddr.length());
                } else {
                    std::get<Index>(data_).clear();
                }
            } else {
                std::memcpy(&std::get<Index>(data_), &buffer[off], len);
            }
            
            // Recursively process next element
            deserializeElements<Index + 1>(buffer);
        }
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    void RowStaticImpl<Policy, ColumnTypes...>::deserializeFromZoH(const std::span<const std::byte> buffer)  
    {
        if constexpr (!isTrackingEnabled(Policy)) {
            throw std::runtime_error("RowStatic::deserializeFromZoH() requires tracking policy enabled");
        }
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

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::deserializeElementsZoH(std::span<const std::byte> &buffer) {
        // we expect the buffer to have the next element at the current position
        // thus the buffer needs to get shorter as we read elements
        // We also expect that the change bitset has been read already!

        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, bool>) {
                // Special handling for bools: 
                //  - always deserialize!
                //  - but stored as single bit within changes_
                std::get<Index>(data_) = changes_.test(Index);
            } else if(changes_.test(Index)) {
                // all other types: only deserialize if marked as changed
                if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                    // special handling for strings, as we need to determine string length
                    if (buffer.size() < sizeof(uint16_t)) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string length.");
                    }
                    uint16_t strLength;
                    std::memcpy(&strLength, &buffer[0], sizeof(uint16_t));
                    
                    if (buffer.size() < sizeof(uint16_t) + strLength) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string payload.");
                    }
                    if (strLength > 0) {
                        std::get<Index>(data_).assign(reinterpret_cast<const char*>(&buffer[sizeof(uint16_t)]), strLength);
                    } else {
                        std::get<Index>(data_).clear();
                    }
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

    /** @brief Visit a range of columns with read-only access (compile-time optimized)
     * 
     * Compile-time type dispatch with runtime index iteration.
     * Zero overhead when compiler can optimize away the fold expression.
     * 
     * @tparam Visitor Callable type
     * @param startIndex First column index to visit
     * @param visitor Callable accepting `(size_t index, const T& value)` where T is determined at compile-time
     * @param count Number of columns to visit
     * 
     * @par Example - Type-safe iteration
     * @code
     * RowStatic<int32_t, double, std::string> row{layout};
     * row.visit(0, [](size_t idx, const auto& value) {
     *     std::cout << "Column " << idx << ": " << value << std::endl;
     * }, 2);
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     * @see RowStatic::visitConst(Visitor&&) const for visiting all columns
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowStaticImpl<Policy, ColumnTypes...>::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) return;  // Nothing to visit
        
        size_t endIndex = std::min(startIndex + count, column_count);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= column_count) {
                throw std::out_of_range("RowStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < column_count && "RowStatic::visit: Start index out of bounds");
        }
        
        // Runtime loop with compile-time type dispatch
        for (size_t i = startIndex; i < endIndex; ++i) {
            // Use fold expression to dispatch to correct tuple element at runtime
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((I == i ? (visitor(I, std::get<I>(data_)), true) : false) || ...);
            }(std::make_index_sequence<column_count>{});
        }
    }

    /** @brief Visit all columns with read-only access (compile-time optimized)
     * 
     * Compile-time type-safe iteration through tuple-based storage.
     * Uses fold expressions for optimal code generation.
     * 
     * @tparam Visitor Callable accepting `(size_t index, const T& value)`
     * 
     * @par Example - Compile-time type dispatch
     * @code
     * RowStatic<int, double, std::string> row{layout};
     * row.visit([](size_t idx, const auto& value) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_integral_v<T>) {
     *         // Integer processing (compile-time branch)
     *     } else if constexpr (std::is_floating_point_v<T>) {
     *         // Float processing (compile-time branch)
     *     } else {
     *         // String processing
     *     }
     * });
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     * @see RowStatic::visit(size_t, Visitor&&, size_t) const for range access
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowStaticImpl<Policy, ColumnTypes...>::visitConst(Visitor&& visitor) const {
        // Delegate to range-based visitor for all columns
        visitConst(0, std::forward<Visitor>(visitor), column_count);
    }

    /** @brief Visit a range of columns with mutable access and change tracking (compile-time optimized)
     * 
     * Compile-time type dispatch with fine-grained change tracking support.
     * Optimal code generation through fold expressions and type-safe tuple access.
     * 
     * @tparam Visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * @param startIndex First column index to visit
     * @param visitor Callable for modification with optional change tracking
     * @param count Number of columns to visit
     * 
     * @par Example - Fine-grained tracking
     * @code
     * RowStatic<int, double, std::string> row{layout};
     * row.visit(0, [](size_t idx, auto& value, bool& changed) {
     *     auto old = value;
     *     value = process(value);
     *     changed = (value != old);  // Fine-grained per-column tracking
     * }, 2);
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowStaticImpl<Policy, ColumnTypes...>::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;  // Nothing to visit
        
        size_t endIndex = std::min(startIndex + count, column_count);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= column_count) {
                throw std::out_of_range("RowStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < column_count && "RowStatic::visit: Start index out of bounds");
        }
        
        // Runtime loop with compile-time type dispatch and change tracking
        for (size_t i = startIndex; i < endIndex; ++i) {
            // Use fold expression to dispatch to correct tuple element at runtime
            [&]<size_t... I>(std::index_sequence<I...>) {
                ([&] {
                    if (I == i) {
                        // Check if visitor accepts change tracking parameter
                        if constexpr (std::is_invocable_v<Visitor, size_t, decltype(std::get<I>(data_))&, bool&>) {
                            bool changed = true;
                            visitor(I, std::get<I>(data_), changed);
                            if constexpr (isTrackingEnabled(Policy)) {
                                changes_[I] |= changed;
                            }
                        } else {
                            visitor(I, std::get<I>(data_));
                            if constexpr (isTrackingEnabled(Policy)) {
                                changes_[I] = true;
                            }
                        }
                    }
                }(), ...);
            }(std::make_index_sequence<column_count>{});
        }
    }

    /** @brief Visit all columns with mutable access and change tracking (compile-time optimized)
     * 
     * Type-safe iteration with compile-time optimization and optional change tracking.
     * Supports both 2-param and 3-param visitor signatures.
     * 
     * @tparam Visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * 
     * @par Visitor Signatures
     * 
     * **Fine-grained (3 params):** `(size_t index, T& value, bool& changed)`
     * **Legacy (2 params):** `(size_t index, T& value)` - marks all as changed
     * 
     * @par Example - Mixed signature visitor
     * @code
     * RowStatic<int, double, std::string> row{layout};
     * row.visit([](size_t idx, auto& value, bool& changed) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_arithmetic_v<T>) {
     *         auto old = value;
     *         value *= 2;
     *         changed = (value != old);  // Track arithmetic changes
     *     } else {
     *         changed = false;  // Don't track string changes
     *     }
     * });
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     * @see RowStatic::visit(size_t, Visitor&&, size_t) for range access
     */
    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowStaticImpl<Policy, ColumnTypes...>::visit(Visitor&& visitor) {
        // Delegate to range-based visitor for all columns
        visit(0, std::forward<Visitor>(visitor), column_count);
    }


    // ========================================================================
    // RowStaticView Implementation
    // ========================================================================


    /** Get value by Static Index (compile-time) - STRICT.
     *  Compile-time type-safe access with zero-copy for strings.
     *  Returns T by value (not reference) - buffer data may not be aligned.
     *  Primitives: Returns T by value (copied via memcpy, safely handles misalignment).
     *  Strings: Returns std::string_view pointing into buffer (zero-copy, alignment not required for byte arrays).
     *  For flexible runtime access with type conversions, use get(index, T& dst) instead.
     */
    template<typename... ColumnTypes>
    template<size_t Index>
    auto RowViewStatic<ColumnTypes...>::get() const {
        static_assert(Index < column_count, "Index out of bounds");
        
        // check buffer validity
        if (buffer_.data() == nullptr) [[unlikely]] {
             throw std::runtime_error("RowViewStatic::get<" + std::to_string(Index) + ">: Buffer not set");
        }

        constexpr size_t length = column_lengths[Index];
        constexpr size_t offset = column_offsets[Index];
        using T = column_type<Index>;
        if (offset + length > buffer_.size()) [[unlikely]] {
            throw std::out_of_range("RowViewStatic::get<" + std::to_string(Index) + ">: Buffer too small " +
                                    "(required: " + std::to_string(offset + length) + 
                                    ", available: " + std::to_string(buffer_.size()) + ")");
        }

        if constexpr (std::is_same_v<T, std::string>) {
            // Decode StringAddr from fixed section
            StringAddr addr;
            std::memcpy(&addr, buffer_.data() + offset, length);
            // Check payload bounds
            if (addr.offset() + addr.length() > buffer_.size()) [[unlikely]] {
                throw std::out_of_range("RowViewStatic::get<" + std::to_string(Index) + ">: String payload out of bounds " +
                                        "(offset: " + std::to_string(addr.offset()) + ", length: " + std::to_string(addr.length()) + ", buffer size: " + std::to_string(buffer_.size()) + ")");
            }
            
            // Return string_view pointing directly into the buffer payload
            return std::string_view(reinterpret_cast<const char*>(buffer_.data() + addr.offset()), addr.length());
        } else {
            // Primitives: Read value
            T val;
            std::memcpy(&val, buffer_.data() + offset, length);
            return val;
        }
    }

    /** Vectorized static access with smart type handling.
     *  Optimized for contiguous block copy when types match. 
     *  Falls back to element-wise conversion if types differ but are assignable.
     *  Compile-time error if types are not compatible.
     */
    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowViewStatic<ColumnTypes...>::get(std::span<T, Extent>& dst) const {
        static_assert(Extent != std::dynamic_extent, "RowViewStatic: requires fixed-extent span");
        static_assert(StartIndex + Extent <= column_count, "RowViewStatic: Range exceeds column count");

        // 1. Validate buffer at runtime
        if (buffer_.empty()) {
            throw std::runtime_error("RowViewStatic::get<" + std::to_string(StartIndex) + ", " + std::to_string(Extent) + ">: Buffer is empty");
        }

        // 2. Check assignability at compile time
        [&]<size_t... I>(std::index_sequence<I...>) {
            static_assert((std::is_assignable_v<T&, column_type<StartIndex + I>> && ...), 
                "RowViewStatic::get: Column types are not assignable to destination span type");
        }(std::make_index_sequence<Extent>{});

        // 3. Choose optimal path based on type matching
        constexpr bool all_types_match = []<size_t... I>(std::index_sequence<I...>) {
            return ((std::is_same_v<T, column_type<StartIndex + I>>) && ...);
        }(std::make_index_sequence<Extent>{});
        
        if constexpr (all_types_match) {
            // Fast path: Types match exactly, contiguous memory copy
            constexpr size_t start_offset = column_offsets[StartIndex];
            constexpr size_t total_bytes = Extent * sizeof(T);
            
            if (start_offset + total_bytes > buffer_.size()) {
                throw std::out_of_range("RowViewStatic::get<" + std::to_string(StartIndex) + ", " + std::to_string(Extent) + ">: " +
                                        "Buffer access out of range (required: " + std::to_string(start_offset + total_bytes) + 
                                        ", available: " + std::to_string(buffer_.size()) + ")");
            }
            
            std::memcpy(dst.data(), buffer_.data() + start_offset, total_bytes);
        } else {
            // Conversion path: Element-wise access with type conversion
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((dst[I] = static_cast<T>(this->template get<StartIndex + I>())), ...);
            }(std::make_index_sequence<Extent>{});
        }
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
                        if (addr.offset() + addr.length() > self.buffer_.size()) return {};
                        return { self.buffer_.data() + addr.offset(), addr.length() };
                    } else {
                        // For primitives, return fixed field
                        return { ptr, length };
                    }
                }...
            };
        }(std::make_index_sequence<column_count>{});

        return handlers[index](*this);
    }

    /** Runtime vectorized access - STRICT.
     *  Type must match exactly for all columns (no conversions).
     *  Returns false on type mismatch or bounds error.
     *  For flexible access with type conversions, use get(index, T& dst) instead.
     */
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
        const size_t length = wireSizeOf(dstType) * dst.size();
        assert(length == sizeof(T) * dst.size());
        if (offset + length > buffer_.size()) [[unlikely]] {
            return false;
        }

        // 5. Bulk Copy, we know src and dst are continouse blocks of same type, without padding in between
        memcpy(dst.data(), buffer_.data() + offset, length);
        return true;
    }

    /** Flexible access with type conversions - FLEXIBLE.
     *  Supports implicit type conversions (e.g., int8→int, float→double, string→string_view).
     *  Returns false if conversion not possible, true on success.
     *  For strict access without conversions, use get<Index>() (compile-time) instead.
     */
    template<typename... ColumnTypes>
    template<typename T>
    bool RowViewStatic<ColumnTypes...>::get(size_t index, T& dst) const noexcept {
        // Use visitor pattern for flexible type conversions
        bool success = false;
        
        try {
            visitConst(index, [&](size_t, const auto& value) {
                using ColType = std::decay_t<decltype(value)>;
                
                // String handling
                if constexpr (std::is_same_v<ColType, std::string_view>) {
                    if constexpr (std::is_same_v<T, std::string_view>) {
                        dst = value;
                        success = true;
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        dst = std::string(value);
                        success = true;
                    }
                }
                // Arithmetic conversion
                else if constexpr (std::is_arithmetic_v<ColType> && std::is_arithmetic_v<T>) {
                    dst = static_cast<T>(value);
                    success = true;
                }
                // Direct assignment
                else if constexpr (std::is_assignable_v<T&, const ColType&>) {
                    dst = value;
                    success = true;
                }
            }, 1);
        } catch (...) {
            return false;
        }
        
        return success;
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

        // Use visitor pattern for runtime-indexed write
        try {
            visit(index, [&](size_t, auto& colValue, bool&) {
                using ColType = std::decay_t<decltype(colValue)>;
                
                // Only allow exact type match for primitives
                if constexpr (std::is_same_v<ColType, T>) {
                    colValue = value;
                }
            }, 1);
        } catch (...) {
            // Silently fail on error (noexcept contract)
        }
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
                StringAddr addr;
                std::memcpy(&addr, buffer_.data() + LayoutType::column_offsets[Index], sizeof(addr));
                if (addr.offset() + addr.length() > buffer_.size()) {
                    return false;
                }
            }
            return validateStringPayloads<Index + 1>();
        }
    }

    /** @brief Visit a range of columns with read-only access (compile-time, zero-copy)
     * 
     * Combines compile-time type safety with zero-copy string access.
     * Optimal for sparse column access in serialized data with static layout.
     * 
     * @tparam Visitor Callable type
     * @param startIndex First column index to visit
     * @param visitor Callable accepting `(size_t index, const T& value)` where T is compile-time determined
     * @param count Number of columns to visit
     * 
     * @par Example - Compile-time zero-copy access
     * @code
     * RowViewStatic<int, std::string, double> view{layout, buffer};
     * view.visit(1, [](size_t idx, std::string_view str) {
     *     // Zero-copy string access, compile-time type dispatch
     *     std::cout << str;
     * });
     * @endcode
     * 
     * @note Strings are accessed as std::string_view (zero-copy)
     * @see row_visitors.h for concepts and helper types
     */
    template<typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowViewStatic<ColumnTypes...>::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) return;  // Nothing to visit
        
        size_t endIndex = std::min(startIndex + count, column_count);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= column_count) {
                throw std::out_of_range("RowViewStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < column_count && "RowViewStatic::visit: Start index out of bounds");
        }
        
        // Runtime loop with compile-time type dispatch
        for (size_t i = startIndex; i < endIndex; ++i) {
            // Use fold expression to dispatch to correct column at runtime
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((I == i ? (visitor(I, get<I>()), true) : false) || ...);
            }(std::make_index_sequence<column_count>{});
        }
    }

    /** @brief Visit all columns with read-only access (compile-time, zero-copy)
     * 
     * Optimal iteration combining compile-time optimization with zero-copy access.
     * Perfect for serialized data inspection without deserialization overhead.
     * 
     * @tparam Visitor Callable accepting `(size_t index, const T& value)`
     * 
     * @par Example - Type-aware processing
     * @code
     * RowViewStatic<int, std::string, double> view{layout, buffer};
     * view.visit([](size_t idx, const auto& value) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_same_v<T, std::string_view>) {
     *         // Zero-copy string access (compile-time specialization)
     *     } else {
     *         // Primitive value (compile-time optimized)
     *     }
     * });
     * @endcode
     * 
     * @see row_visitors.h for concepts and helper types
     */
    template<typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowViewStatic<ColumnTypes...>::visitConst(Visitor&& visitor) const {
        // Delegate to range-based visitor for all columns
        visitConst(0, std::forward<Visitor>(visitor), column_count);
    }

    /** @brief Visit a range of columns with mutable access (compile-time, primitives only)
     * 
     * In-place primitive modification with compile-time type safety.
     * Strings are read-only (std::string_view) - cannot resize buffer.
     * 
     * @tparam Visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * @param startIndex First column index to visit
     * @param visitor Callable for modification
     * @param count Number of columns to visit
     * 
     * @par Example - Modify primitives, read strings
     * @code
     * RowViewStatic<int, std::string, double> view{layout, buffer};
     * view.visit(0, [](size_t idx, auto& value) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_arithmetic_v<T>) {
     *         value *= 2;  // In-place primitive modification
     *     }
     *     // Strings passed as string_view (read-only)
     * }, 3);
     * @endcode
     * 
     * @warning Strings cannot be modified (buffer size is fixed)
     * @note Change tracking parameter accepted but has no effect
     * @see row_visitors.h for concepts and helper types
     */
    template<typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowViewStatic<ColumnTypes...>::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;  // Nothing to visit
        
        size_t endIndex = std::min(startIndex + count, column_count);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= column_count) {
                throw std::out_of_range("RowViewStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < column_count && "RowViewStatic::visit: Start index out of bounds");
        }
        
        // Runtime loop with compile-time type dispatch and change tracking
        for (size_t i = startIndex; i < endIndex; ++i) {
            // Use fold expression to dispatch to correct column at runtime
            [&]<size_t... I>(std::index_sequence<I...>) {
                ([&] {
                    if (I == i) {
                        using ColType = column_type<I>;
                        bool changed = true;  // Default: assume changed
                        
                        if constexpr (std::is_arithmetic_v<ColType> || std::is_same_v<ColType, bool>) {
                            // Primitives: get mutable reference and pass to visitor
                            constexpr size_t offset = column_offsets[I];
                            ColType& value = *reinterpret_cast<ColType*>(buffer_.data() + offset);
                            
                            if constexpr (std::is_invocable_v<Visitor, size_t, ColType&, bool&>) {
                                visitor(I, value, changed);
                            } else {
                                visitor(I, value);
                            }
                        } else if constexpr (std::is_same_v<ColType, std::string>) {
                            // Strings: read-only (cannot resize buffer), pass string_view
                            auto str_view = get<I>();
                            
                            if constexpr (std::is_invocable_v<Visitor, size_t, decltype(str_view), bool&>) {
                                visitor(I, str_view, changed);
                            } else {
                                visitor(I, str_view);
                            }
                        }
                    }
                }(), ...);
            }(std::make_index_sequence<column_count>{});
        }
    }

    /** @brief Visit all columns with mutable access (compile-time, primitives only)
     * 
     * Type-safe iteration with in-place primitive modification.
     * Strings remain read-only. No change tracking.
     * 
     * @tparam Visitor Callable accepting `(size_t index, T& value[, bool& changed])`
     * 
     * @par Example - Conditional modification
     * @code
     * RowViewStatic<int, std::string, double> view{layout, buffer};
     * view.visit([](size_t idx, auto& value, bool& changed) {
     *     using T = std::decay_t<decltype(value)>;
     *     if constexpr (std::is_arithmetic_v<T>) {
     *         auto old = value;
     *         value = process(value);  // Modify primitives
     *         changed = (value != old);
     *     } else {
     *         // String: read-only std::string_view
     *         changed = false;
     *     }
     * });
     * @endcode
     * 
     * @note Change parameter accepted but not tracked
     * @see row_visitors.h for concepts and helper types
     */
    template<typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowViewStatic<ColumnTypes...>::visit(Visitor&& visitor) {
        // Delegate to range-based visitor for all columns
        visit(0, std::forward<Visitor>(visitor), column_count);
    }

} // namespace bcsv