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

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::RowImpl(const Layout& layout)
        : layout_(layout)
        , bits_(0)
        , data_()
        , strg_()
    {
        // Register as observer for layout changes
        layout_.registerCallback(this, {
               [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
            }
        );

        const size_t colCount = layout_.columnCount();

        // Count bool and string columns to size bits_ and strg_
        size_t boolCount = 0;
        size_t strgCount = 0;
        uint32_t dataSize = 0;
        for (size_t i = 0; i < colCount; ++i) {
            ColumnType type = layout_.columnType(i);
            if (type == ColumnType::BOOL)   ++boolCount;
            else if (type == ColumnType::STRING) ++strgCount;
        }

        // Compute dataSize from layout offsets (last scalar offset + its size, or iterate)
        // Use the static helper to get the total data_ size
        {
            std::vector<uint32_t> tmpOffsets;
            Layout::Data::computeOffsets(layout_.columnTypes(), tmpOffsets, dataSize);
            // offsets should match layout's offsets (they were computed from the same types)
        }

        // Initialize storage
        data_.resize(dataSize);  // zero-initialized = scalar defaults (0 for all arithmetic types)
        strg_.resize(strgCount); // default-constructed empty strings

        if constexpr (isTrackingEnabled(Policy)) {
            bits_.resize(colCount);
            bits_.reset(); // all bits 0 (bools = false)
            // Mark non-BOOL columns as changed (so first ZoH serialize includes all values)
            bits_ |= layout_.trackedMask();
        } else {
            bits_.resize(boolCount, false);  // just bool values, default false
        }
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::RowImpl(const RowImpl& other)
        : layout_(other.layout_)  // Share layout (shallow copy of shared_ptr inside)
        , bits_(other.bits_)
        , data_(other.data_)      // trivial byte copy (no string objects inside!)
        , strg_(other.strg_)      // vector copy handles deep string copies automatically
    {
        // Register as observer for layout changes (independent from 'other')
        layout_.registerCallback(this, {
               [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
            }
        );
    }

    template<TrackingPolicy Policy>
    inline RowImpl<Policy>::RowImpl(RowImpl&& other) noexcept
        : layout_(other.layout_)
        , bits_(std::move(other.bits_))
        , data_(std::move(other.data_))
        , strg_(std::move(other.strg_))
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
        // No manual cleanup needed — strg_ vector handles string destruction,
        // data_ handles byte buffer, bits_ handles Bitset.
    }

    /** Clear the row to its default state (default values) */
    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::clear()
    {
        // Zero all scalar data
        std::memset(data_.data(), 0, data_.size());

        // Clear all strings
        for (auto& s : strg_) {
            s.clear();
        }

        // Reset all bits (bools = false, change flags = cleared)
        bits_.reset();

        // For tracking: mark all non-BOOL columns as changed
        if constexpr (isTrackingEnabled(Policy)) {
            bits_ |= layout_.trackedMask();
        }
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::setChanges() noexcept {
        if constexpr (isTrackingEnabled(Policy)) {
            // Set all change flags (non-BOOL). BOOL value bits are preserved.
            bits_ |= layout_.trackedMask();
        }
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::resetChanges() noexcept {
        if constexpr (isTrackingEnabled(Policy)) {
            // Clear all change flags (non-BOOL). BOOL value bits are preserved.
            bits_ &= layout_.boolMask();
        }
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
        ColumnType type = layout_.columnType(index);
        uint32_t offset = layout_.columnOffset(index);
        if (type == ColumnType::BOOL) {
            // WARNING: Returns pointer to thread-local storage. The pointer is only valid until
            // the next get(size_t) call for a BOOL column on the same thread. Two consecutive
            // bool get() calls will alias. Prefer get<bool>(index) for safe by-value access.
            static thread_local bool tl_bool[2];
            static thread_local unsigned tl_idx = 0;
            tl_idx = 1 - tl_idx;  // alternate between [0] and [1]
            tl_bool[tl_idx] = bits_[bitsIndex(index)];
            return &tl_bool[tl_idx];
        } else if (type == ColumnType::STRING) {
            return &strg_[offset];
        } else {
            return &data_[offset];
        }
    }
   
    // =========================================================================
    // 2. TYPED CONST ACCESS (Row Only) - STRICT, ZERO-COPY
    // Returns const T& for scalars and strings, bool by value for bools.
    // String views (string_view, span<const char>) return lightweight views.
    // Type must match exactly (no conversions).
    // For flexible access with type conversions, use get(index, T& dst) instead.
    // =========================================================================
    template<TrackingPolicy Policy>
    template<typename T>
    inline decltype(auto) RowImpl<Policy>::get(size_t index) const {
        // Compile-time type validation
        static_assert(
            std::is_trivially_copyable_v<T> || 
            std::is_same_v<T, std::string> || 
            std::is_same_v<T, std::string_view> || 
            std::is_same_v<T, std::span<const char>>,
            "Row::get<T>: Type T must be either trivially copyable (primitives) or string-related (std::string, std::string_view, std::span<const char>)"
        );

        if constexpr (RANGE_CHECKING) {
            if (index >= layout_.columnCount()) [[unlikely]] {
                throw std::out_of_range("Row::get<T>: Index out of range");
            }
            if (toColumnType<T>() != layout_.columnType(index)) [[unlikely]] {
                throw std::runtime_error(
                    "Row::get<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Requested: " + std::string(toString(toColumnType<T>())) + 
                    ", Actual: " + std::string(toString(layout_.columnType(index))));
            }
        }
        
        uint32_t offset = layout_.columnOffset(index);

        if constexpr (std::is_same_v<T, bool>) {
            return bits_[bitsIndex(index)];  // bool by value (Bitset has no addressable storage)
        } else if constexpr (std::is_same_v<T, std::string>) {
            return static_cast<const std::string&>(strg_[offset]);  // const ref, zero-copy
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            return std::string_view(strg_[offset]);  // lightweight view into internal storage
        } else if constexpr (std::is_same_v<T, std::span<const char>>) {
            return std::span<const char>(strg_[offset].data(), strg_[offset].size());
        } else {
            return static_cast<const T&>(*reinterpret_cast<const T*>(&data_[offset]));  // const ref, zero-copy
        }
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
            for (size_t i = index; i < index+dst.size(); ++i) {
                if (targetType != layout_.columnType(i)) [[unlikely]]{
                    throw std::runtime_error("vectorized get() types must match exactly");
                }
            }
        }

        if constexpr (std::is_same_v<T, bool>) {
            // Bit-by-bit extraction from bits_
            for (size_t i = 0; i < dst.size(); ++i) {
                dst[i] = bits_[bitsIndex(index + i)];
            }
        } else {
            // Consecutive same-type scalars are contiguous in aligned data_
            uint32_t offset = layout_.columnOffset(index);
            memcpy(dst.data(), &data_[offset], dst.size() * sizeof(T));
        }
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
    inline decltype(auto) RowImpl<Policy>::ref(size_t index) {
        if constexpr (RANGE_CHECKING) {
            if (index >= layout_.columnCount()) [[unlikely]] {
                throw std::out_of_range("Row::ref<T>: Index out of range");
            }
            if (toColumnType<T>() != layout_.columnType(index)) [[unlikely]] {
                throw std::runtime_error("Row::ref<T>: Type mismatch at index " + std::to_string(index));
            }
        }

        uint32_t offset = layout_.columnOffset(index);

        if constexpr (std::is_same_v<T, bool>) {
            // Mark changed if tracking
            if constexpr (isTrackingEnabled(Policy)) {
                // For bools, the bit IS the value, change is implicit via write-through
                // No separate change flag for bools — caller writes directly
            }
            return bits_[bitsIndex(index)];  // returns Bitset<>::reference proxy
        } else if constexpr (std::is_same_v<T, std::string>) {
            if constexpr (isTrackingEnabled(Policy)) {
                bits_.set(index);
            }
            return static_cast<std::string&>(strg_[offset]);
        } else {
            if constexpr (isTrackingEnabled(Policy)) {
                bits_.set(index);
            }
            return *reinterpret_cast<T*>(&data_[offset]);
        }
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

    // Helper: detect bool proxy types (e.g. std::vector<bool>::reference / std::_Bit_reference)
    constexpr bool isBoolLike = std::is_same_v<T, bool> || 
        (std::is_convertible_v<T, bool> && !std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>);

    // Helper: detect string-like types (std::string, std::string_view, const char*, etc.)
    constexpr bool isStringLike = detail::is_string_like_v<T> || 
        (std::is_convertible_v<T, std::string_view> && !detail::is_primitive_v<T> && !isBoolLike);

    // Direct dispatch — compile-time type check, no runtime switch
    if constexpr (RANGE_CHECKING) {
        if (index >= layout_.columnCount()) [[unlikely]] {
            throw std::out_of_range("Row::set<T>: Index out of range");
        }
        if constexpr (isBoolLike) {
            if (layout_.columnType(index) != ColumnType::BOOL) [[unlikely]] {
                throw std::runtime_error("Row::set<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Expected BOOL, actual " + std::string(toString(layout_.columnType(index))));
            }
        } else if constexpr (isStringLike) {
            if (layout_.columnType(index) != ColumnType::STRING) [[unlikely]] {
                throw std::runtime_error("Row::set<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Expected STRING, actual " + std::string(toString(layout_.columnType(index))));
            }
        } else {
            if (toColumnType<T>() != layout_.columnType(index)) [[unlikely]] {
                throw std::runtime_error("Row::set<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Cannot assign " + std::string(toString(toColumnType<T>())) + 
                    " to " + std::string(toString(layout_.columnType(index))));
            }
        }
    }

    uint32_t offset = layout_.columnOffset(index);

    if constexpr (isBoolLike) {
        // Bool (or bool proxy like std::vector<bool>::reference): write to Bitset
        bits_[bitsIndex(index)] = static_cast<bool>(value);
        // No separate change tracking for bools (the bit IS the value)
    } else if constexpr (isStringLike) {
        // String: write to strg_, with change tracking and truncation
        std::string& str = strg_[offset];
        bool changed = (str != value);
        str = value;
        if (str.size() > MAX_STRING_LENGTH) {
            str.resize(MAX_STRING_LENGTH);
        }
        if constexpr (isTrackingEnabled(Policy)) {
            bits_[index] |= changed;
        }
    } else {
        // Scalar: direct write to data_
        T& colValue = *reinterpret_cast<T*>(&data_[offset]);
        bool changed = (colValue != value);
        colValue = value;
        if constexpr (isTrackingEnabled(Policy)) {
            bits_[index] |= changed;
        }
    }
}



    /** Vectorized set of multiple columns of same type */
    template<TrackingPolicy Policy>
    template<typename T>
    inline void RowImpl<Policy>::set(size_t index, std::span<const T> values)
    {
        static_assert(std::is_arithmetic_v<T>, "Only primitive arithmetic types are supported for vectorized set");

        if constexpr (RANGE_CHECKING) {
            for (size_t i = 0; i < values.size(); ++i) {
                const ColumnType &targetType = layout_.columnType(index + i);
                if (toColumnType<T>() != targetType) [[unlikely]]{
                    throw std::runtime_error("vectorized set() types must match exactly");
                }
            }
        }

        if constexpr (std::is_same_v<T, bool>) {
            // Bit-by-bit write to bits_
            for (size_t i = 0; i < values.size(); ++i) {
                size_t bi = bitsIndex(index + i);
                if constexpr (isTrackingEnabled(Policy)) {
                    // For bools, the bit IS the value — just write it
                    bits_[bi] = values[i];
                } else {
                    bits_[bi] = values[i];
                }
            }
        } else {
            uint32_t offset = layout_.columnOffset(index);
            T* dst = reinterpret_cast<T*>(&data_[offset]);
            if constexpr (isTrackingEnabled(Policy)) {
                // Element-wise check and set to track changes precisely
                for (size_t i = 0; i < values.size(); ++i) {
                    if (dst[i] != values[i]) {
                        dst[i] = values[i];
                        bits_.set(index + i);
                    }
                }
            } else {
                std::memcpy(dst, values.data(), values.size() * sizeof(T));
            }
        }
    }

    /** Serialize the row into the provided buffer, appending data to the end of the buffer.
    * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
    * @return A span pointing to the serialized row data within the buffer.
    */
    template<TrackingPolicy Policy>
    inline std::span<std::byte> RowImpl<Policy>::serializeTo(ByteBuffer& buffer) const  {
        const size_t  off_row = buffer.size();          // offset to the begin of this row
        size_t        off_fix = off_row;                // tracking offset(position) within the fixed section

        // Compute offset_var (wire-format fixed section size) on the fly
        uint32_t offset_var = 0;
        const size_t count = layout_.columnCount();
        for (size_t i = 0; i < count; ++i) {
            offset_var += wireSizeOf(layout_.columnType(i));
        }

        size_t off_var = offset_var;            // tracking offset(position) within the variable section
        
        // resize buffer to have enough room to hold the entire fixed section of the row
        buffer.resize(buffer.size() + offset_var);

        for(size_t i = 0; i < count; ++i) {
            ColumnType type = layout_.columnType(i);
            uint32_t offset = layout_.columnOffset(i);

            if (type == ColumnType::BOOL) {
                bool val = bits_[bitsIndex(i)];
                std::memcpy(&buffer[off_fix], &val, sizeof(bool));
                off_fix += wireSizeOf(type);
            } else if (type == ColumnType::STRING) {
                const std::string& str = strg_[offset];               
                StringAddr strAddr(off_var, str.length());
                
                std::memcpy(&buffer[off_fix], &strAddr, sizeof(strAddr));
                off_fix += sizeof(strAddr);
                
                if (strAddr.length() > 0) {
                    buffer.resize(off_row + off_var + strAddr.length());
                    std::memcpy(&buffer[off_row + off_var], str.data(), strAddr.length());
                    off_var += strAddr.length();
                }
            } else {
                size_t len = wireSizeOf(type);
                std::memcpy(&buffer[off_fix], &data_[offset], len);
                off_fix += len;
            }
        }
        return {&buffer[off_row], buffer.size() - off_row};
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::deserializeFrom(const std::span<const std::byte> buffer)
    {
        // Compute offset_var (wire-format fixed section size) on the fly
        uint32_t offset_var = 0;
        const size_t count = layout_.columnCount();
        for (size_t i = 0; i < count; ++i) {
            offset_var += wireSizeOf(layout_.columnType(i));
        }

        size_t off_fix = 0;
        
        // Safety check: ensure buffer is large enough to contain fixed section
        if (offset_var > buffer.size()) [[unlikely]] {
            throw std::runtime_error("Row::deserializeFrom failed as buffer is too short");
        }
        
        for(size_t i = 0; i < count; ++i) {
            ColumnType type = layout_.columnType(i);
            uint32_t offset = layout_.columnOffset(i);
            
            if (type == ColumnType::BOOL) {
                bool val = false;
                std::memcpy(&val, &buffer[off_fix], sizeof(bool));
                bits_[bitsIndex(i)] = val;
            } else if (type == ColumnType::STRING) {
                StringAddr strAddr;
                std::memcpy(&strAddr, &buffer[off_fix], sizeof(strAddr));                
                if (strAddr.offset() + strAddr.length() > buffer.size()) {
                    throw std::runtime_error("Row::deserializeFrom String payload extends beyond buffer");
                }
                
                std::string& str = strg_[offset];
                if (strAddr.length() > 0) {
                    str.assign(reinterpret_cast<const char*>(&buffer[strAddr.offset()]), strAddr.length());
                } else {
                    str.clear();
                }
            } else {
                std::memcpy(&data_[offset], &buffer[off_fix], wireSizeOf(type));
            }
            off_fix += wireSizeOf(type); // advance fixed offset
        }
    }

    /** Serialize the row into the provided buffer using Zero-Order-Hold (ZoH) compression.
     * Only the columns that have changed (as indicated by the change tracking Bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change Bitset.
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

        // Early exit if no changes at all (no bool=true, no changed columns)
        if(!bits_.any()) {
            return std::span<std::byte>{};
        }

        // bits_ IS the ZoH wire format — write it directly to the buffer (no copy needed).
        // Bool values sit at bool positions, change flags at non-bool positions.
        const size_t headerSize = bits_.sizeBytes();
        buffer.resize(buffer.size() + headerSize);
        std::memcpy(&buffer[bufferSizeOld], bits_.data(), headerSize);

        // If no non-BOOL columns have changed, the header (with bool values) is all we need.
        // Skip the column loop entirely — a significant saving for bool-heavy / static rows.
        // Note: Future delta encoding of bool bits (flip-detection) will be handled by
        // Writer/Serializer, which tracks previous-row state.
        if (!bits_.any(layout_.trackedMask())) {
            return {&buffer[bufferSizeOld], headerSize};
        }

        // Serialize each non-bool element that has changed (sequential, forward-only writes)
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            uint32_t offset = layout_.columnOffset(i);

            if (type == ColumnType::BOOL) {
                // Bool value is already encoded in the header (bits_[i]) — nothing else to do
            } else if (bits_[i]) {
                size_t off = buffer.size();
                if(type == ColumnType::STRING) {
                    const std::string& str = strg_[offset];
                    uint16_t strLength = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
                    buffer.resize(buffer.size() + sizeof(strLength) + strLength);
                    std::memcpy(&buffer[off], &strLength, sizeof(strLength));
                    if (strLength > 0) {
                        std::memcpy(&buffer[off + sizeof(strLength)], str.data(), strLength);
                    }
                } else {
                    size_t len = wireSizeOf(type);
                    buffer.resize(buffer.size() + len);
                    std::memcpy(&buffer[off], &data_[offset], len);
                }
            }
        }

        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::deserializeFromZoH(const std::span<const std::byte> buffer) {
        if constexpr (!isTrackingEnabled(Policy)) {
            throw std::runtime_error("Row::deserializeFromZoH() requires tracking policy enabled");
        }
        
        // buffer starts with the bits_ Bitset (bool values + change flags), followed by the actual row data.
        if (buffer.size() >= bits_.sizeBytes()) {
            std::memcpy(bits_.data(), &buffer[0], bits_.sizeBytes());  
        } else [[unlikely]] {
            throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small to contain change Bitset.");
        }        
        
        // Deserialize each element that has changed
        size_t dataOffset = bits_.sizeBytes();
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            uint32_t offset = layout_.columnOffset(i);

            if (type == ColumnType::BOOL) {
                // Bool value already decoded — bit i in bits_ IS the value. Nothing else to do.
            } else if (bits_[i]) {
                // Deserialize other types only if they have changed
                if(type == ColumnType::STRING) {
                    uint16_t strLength; 
                    if (dataOffset + sizeof(strLength) > buffer.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    std::memcpy(&strLength, &buffer[dataOffset], sizeof(strLength));
                    dataOffset += sizeof(strLength);
                    
                    if (dataOffset + strLength > buffer.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    std::string& str = strg_[offset];
                    if (strLength > 0) {
                        str.assign(reinterpret_cast<const char*>(&buffer[dataOffset]), strLength);
                    } else {
                        str.clear();
                    }
                    dataOffset += strLength;
                } else {
                    size_t len = wireSizeOf(type);
                    if (dataOffset + len > buffer.size()) [[unlikely]]
                        throw std::runtime_error("buffer too small");
                    
                    std::memcpy(&data_[offset], &buffer[dataOffset], len);
                    dataOffset += len;
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
            uint32_t offset = layout_.columnOffset(i);
            
            // Dispatch based on column type
            switch(type) {
                case ColumnType::BOOL: {
                    bool val = bits_[bitsIndex(i)];
                    visitor(i, val);
                    break;
                }
                case ColumnType::INT8:
                    visitor(i, *reinterpret_cast<const int8_t*>(&data_[offset]));
                    break;
                case ColumnType::INT16:
                    visitor(i, *reinterpret_cast<const int16_t*>(&data_[offset]));
                    break;
                case ColumnType::INT32:
                    visitor(i, *reinterpret_cast<const int32_t*>(&data_[offset]));
                    break;
                case ColumnType::INT64:
                    visitor(i, *reinterpret_cast<const int64_t*>(&data_[offset]));
                    break;
                case ColumnType::UINT8:
                    visitor(i, *reinterpret_cast<const uint8_t*>(&data_[offset]));
                    break;
                case ColumnType::UINT16:
                    visitor(i, *reinterpret_cast<const uint16_t*>(&data_[offset]));
                    break;
                case ColumnType::UINT32:
                    visitor(i, *reinterpret_cast<const uint32_t*>(&data_[offset]));
                    break;
                case ColumnType::UINT64:
                    visitor(i, *reinterpret_cast<const uint64_t*>(&data_[offset]));
                    break;
                case ColumnType::FLOAT:
                    visitor(i, *reinterpret_cast<const float*>(&data_[offset]));
                    break;
                case ColumnType::DOUBLE:
                    visitor(i, *reinterpret_cast<const double*>(&data_[offset]));
                    break;
                case ColumnType::STRING:
                    visitor(i, strg_[offset]);
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
            ColumnType type = layout_.columnType(i);
            uint32_t offset = layout_.columnOffset(i);

            if (type == ColumnType::BOOL) {
                // Materialize bool from Bitset, let visitor modify, write back
                bool val = bits_[bitsIndex(i)];
                if constexpr (isTrackingEnabled(Policy)) {
                    static_assert(std::is_invocable_v<Visitor, size_t, bool&, bool&>,
                                  "Row::visit() with tracking requires (size_t, T&, bool&)");
                    bool changed = true;
                    visitor(i, val, changed);
                } else {
                    if constexpr (std::is_invocable_v<Visitor, size_t, bool&, bool&>) {
                        bool changed = true;
                        visitor(i, val, changed);
                    } else {
                        static_assert(std::is_invocable_v<Visitor, size_t, bool&>,
                                      "Row::visit() requires (size_t, T&) or (size_t, T&, bool&)");
                        visitor(i, val);
                    }
                }
                bits_[bitsIndex(i)] = val;  // Write back (no separate change tracking for bools)
            } else if (type == ColumnType::STRING) {
                std::string& str = strg_[offset];
                if constexpr (isTrackingEnabled(Policy)) {
                    static_assert(std::is_invocable_v<Visitor, size_t, std::string&, bool&>,
                                  "Row::visit() with tracking requires (size_t, T&, bool&)");
                    bool changed = true;
                    visitor(i, str, changed);
                    bits_[i] |= changed;
                } else {
                    if constexpr (std::is_invocable_v<Visitor, size_t, std::string&, bool&>) {
                        bool changed = true;
                        visitor(i, str, changed);
                    } else {
                        visitor(i, str);
                    }
                }
                if (str.size() > MAX_STRING_LENGTH) {
                    str.resize(MAX_STRING_LENGTH);
                }
            } else {
                // Scalar types: dispatch via lambda
                auto dispatch = [&](auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (isTrackingEnabled(Policy)) {
                        static_assert(std::is_invocable_v<Visitor, size_t, T&, bool&>,
                                      "Row::visit() with tracking requires (size_t, T&, bool&)");
                        bool changed = true;
                        visitor(i, value, changed);
                        bits_[i] |= changed;
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

                switch(type) {
                    case ColumnType::INT8:
                        dispatch(*reinterpret_cast<int8_t*>(&data_[offset]));
                        break;
                    case ColumnType::INT16:
                        dispatch(*reinterpret_cast<int16_t*>(&data_[offset]));
                        break;
                    case ColumnType::INT32:
                        dispatch(*reinterpret_cast<int32_t*>(&data_[offset]));
                        break;
                    case ColumnType::INT64:
                        dispatch(*reinterpret_cast<int64_t*>(&data_[offset]));
                        break;
                    case ColumnType::UINT8:
                        dispatch(*reinterpret_cast<uint8_t*>(&data_[offset]));
                        break;
                    case ColumnType::UINT16:
                        dispatch(*reinterpret_cast<uint16_t*>(&data_[offset]));
                        break;
                    case ColumnType::UINT32:
                        dispatch(*reinterpret_cast<uint32_t*>(&data_[offset]));
                        break;
                    case ColumnType::UINT64:
                        dispatch(*reinterpret_cast<uint64_t*>(&data_[offset]));
                        break;
                    case ColumnType::FLOAT:
                        dispatch(*reinterpret_cast<float*>(&data_[offset]));
                        break;
                    case ColumnType::DOUBLE:
                        dispatch(*reinterpret_cast<double*>(&data_[offset]));
                        break;
                    default: [[unlikely]]
                        throw std::runtime_error("Row::visit() unsupported column type");
                }
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
     * - **With tracking enabled**: Changed columns are marked in internal Bitset
     * - **Respects `changed` flag**: Only columns with `changed=true` are marked
    * - **Without tracking**: No overhead, changes not recorded
     * 
     * @warning Only modifies columns through set() if you need type conversion or validation.
     *          Direct modification via visit() bypasses those checks but is more efficient.
     * 
     * @see row_visitors.h for concepts, helper types, and more examples
     * @see Row::visit(Visitor&&) const for read-only version
     * @see Row::visit(size_t, Visitor&&, size_t) for visiting a range or single column
     * @see Row::changes() to access the change tracking Bitset
     */
    template<TrackingPolicy Policy>
    template<RowVisitor Visitor>
    inline void RowImpl<Policy>::visit(Visitor&& visitor) {
        // Delegate to range-based visitor for all columns
        visit(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    // ========================================================================
    // Type-Safe visit<T>() — Compile-Time Dispatch (No Runtime Switch)
    // ========================================================================

    /** @brief Type-safe mutable visit: iterate columns of known type T
     * 
     * Unlike the untyped visit(), this overload uses compile-time dispatch
     * via if-constexpr, eliminating the runtime ColumnType switch entirely.
     * All columns in [startIndex, startIndex+count) must be of type T.
     * 
     * Performance: identical to ref<T>() — no columnType() lookup, no switch.
     * Safety: runtime type check verifies each column matches T (in debug/RANGE_CHECKING).
     * 
     * @tparam T         The expected column type (must be a BCSV-supported type)
     * @tparam Visitor    Callable accepting (size_t, T&, bool&) or (size_t, T&)
     * @param startIndex  First column index to visit
     * @param visitor     Callable for modification with optional change tracking
     * @param count       Number of consecutive columns to visit (default 1)
     * 
     * @par Example — Calibrate 5 consecutive double sensor channels
     * @code
     * row.visit<double>(10, [&](size_t i, double& val, bool& changed) {
     *     val += calibration[i - 10];
     *     changed = true;
     * }, 5);
     * @endcode
     * 
     * @throws std::out_of_range if startIndex + count > columnCount (when RANGE_CHECKING)
     * @throws std::runtime_error if any column in range is not of type T (when RANGE_CHECKING)
     * 
     * @see visit(size_t, Visitor&&, size_t) for untyped (heterogeneous) iteration
     * @see visitConst<T>() for read-only typed iteration
     */
    template<TrackingPolicy Policy>
    template<typename T, typename Visitor>
        requires TypedRowVisitor<Visitor, T>
    inline void RowImpl<Policy>::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;

        size_t endIndex = startIndex + count;

        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("Row::visit<T>: Range [" + std::to_string(startIndex) + 
                    ", " + std::to_string(endIndex) + ") exceeds column count " + 
                    std::to_string(layout_.columnCount()));
            }
            constexpr ColumnType expectedType = toColumnType<T>();
            for (size_t i = startIndex; i < endIndex; ++i) {
                if (layout_.columnType(i) != expectedType) [[unlikely]] {
                    throw std::runtime_error("Row::visit<T>: Type mismatch at column " + std::to_string(i) +
                        ". Expected " + std::string(toString(expectedType)) +
                        ", actual " + std::string(toString(layout_.columnType(i))));
                }
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "Row::visit<T>: Range out of bounds");
        }

        for (size_t i = startIndex; i < endIndex; ++i) {
            uint32_t offset = layout_.columnOffset(i);

            if constexpr (std::is_same_v<T, bool>) {
                // Materialize bool from Bitset, let visitor modify, write back
                bool val = bits_[bitsIndex(i)];
                if constexpr (std::is_invocable_v<Visitor, size_t, bool&, bool&>) {
                    bool changed = true;
                    visitor(i, val, changed);
                } else {
                    visitor(i, val);
                }
                bits_[bitsIndex(i)] = val;
            } else if constexpr (std::is_same_v<T, std::string>) {
                std::string& str = strg_[offset];
                if constexpr (std::is_invocable_v<Visitor, size_t, std::string&, bool&>) {
                    bool changed = true;
                    visitor(i, str, changed);
                    if constexpr (isTrackingEnabled(Policy)) {
                        bits_[i] |= changed;
                    }
                } else {
                    visitor(i, str);
                    if constexpr (isTrackingEnabled(Policy)) {
                        bits_.set(i);
                    }
                }
                if (str.size() > MAX_STRING_LENGTH) {
                    str.resize(MAX_STRING_LENGTH);
                }
            } else {
                // Scalar: direct reinterpret_cast, no switch
                T& value = *reinterpret_cast<T*>(&data_[offset]);
                if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                    bool changed = true;
                    visitor(i, value, changed);
                    if constexpr (isTrackingEnabled(Policy)) {
                        bits_[i] |= changed;
                    }
                } else {
                    visitor(i, value);
                    if constexpr (isTrackingEnabled(Policy)) {
                        bits_.set(i);
                    }
                }
            }
        }
    }

    /** @brief Type-safe const visit: iterate columns of known type T (read-only)
     * 
     * Read-only counterpart of visit<T>(). Uses compile-time dispatch,
     * no runtime ColumnType switch. Zero-copy for scalars and strings.
     * 
     * @tparam T         The expected column type
     * @tparam Visitor    Callable accepting (size_t, const T&)
     * @param startIndex  First column index to visit
     * @param visitor     Read-only callable
     * @param count       Number of consecutive columns to visit (default 1)
     * 
     * @par Example — Read 3 consecutive int32 columns
     * @code
     * double sum = 0;
     * row.visitConst<int32_t>(0, [&](size_t, const int32_t& val) {
     *     sum += val;
     * }, 3);
     * @endcode
     */
    template<TrackingPolicy Policy>
    template<typename T, typename Visitor>
        requires TypedRowVisitorConst<Visitor, T>
    inline void RowImpl<Policy>::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) return;

        size_t endIndex = startIndex + count;

        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("Row::visitConst<T>: Range [" + std::to_string(startIndex) + 
                    ", " + std::to_string(endIndex) + ") exceeds column count " + 
                    std::to_string(layout_.columnCount()));
            }
            constexpr ColumnType expectedType = toColumnType<T>();
            for (size_t i = startIndex; i < endIndex; ++i) {
                if (layout_.columnType(i) != expectedType) [[unlikely]] {
                    throw std::runtime_error("Row::visitConst<T>: Type mismatch at column " + std::to_string(i) +
                        ". Expected " + std::string(toString(expectedType)) +
                        ", actual " + std::string(toString(layout_.columnType(i))));
                }
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "Row::visitConst<T>: Range out of bounds");
        }

        for (size_t i = startIndex; i < endIndex; ++i) {
            uint32_t offset = layout_.columnOffset(i);

            if constexpr (std::is_same_v<T, bool>) {
                const bool val = bits_[bitsIndex(i)];
                visitor(i, val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                visitor(i, static_cast<const std::string&>(strg_[offset]));
            } else {
                visitor(i, static_cast<const T&>(*reinterpret_cast<const T*>(&data_[offset])));
            }
        }
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

        // Save old state (layout_ still has pre-mutation types/offsets)
        Bitset<> oldBits = bits_;
        std::vector<std::byte> oldData = data_;
        std::vector<std::string> oldStrg = strg_;

        const size_t oldColCount = layout_.columnCount();
        std::vector<uint32_t> oldOffsets(oldColCount);
        std::vector<ColumnType> oldTypes(oldColCount);
        for (size_t i = 0; i < oldColCount; ++i) {
            oldOffsets[i] = layout_.columnOffset(i);
            oldTypes[i] = layout_.columnType(i);
        }

        // Build new column types and mapping (newIndex → oldIndex, -1 if new)
        std::vector<ColumnType> newTypes;
        std::vector<int> newToOld;

        if (changes.size() == 1) {
            const auto& c = changes[0];
            if (c.old_type == ColumnType::VOID && c.new_type != ColumnType::VOID) {
                // addColumn at position c.index
                newTypes.reserve(oldColCount + 1);
                newToOld.reserve(oldColCount + 1);
                for (size_t i = 0; i < c.index && i < oldColCount; ++i) {
                    newTypes.push_back(oldTypes[i]);
                    newToOld.push_back(static_cast<int>(i));
                }
                newTypes.push_back(c.new_type);
                newToOld.push_back(-1); // new column
                for (size_t i = c.index; i < oldColCount; ++i) {
                    newTypes.push_back(oldTypes[i]);
                    newToOld.push_back(static_cast<int>(i));
                }
            } else if (c.new_type == ColumnType::VOID && c.old_type != ColumnType::VOID) {
                // removeColumn at index c.index
                newTypes.reserve(oldColCount > 0 ? oldColCount - 1 : 0);
                newToOld.reserve(oldColCount > 0 ? oldColCount - 1 : 0);
                for (size_t i = 0; i < oldColCount; ++i) {
                    if (i != c.index) {
                        newTypes.push_back(oldTypes[i]);
                        newToOld.push_back(static_cast<int>(i));
                    }
                }
            } else {
                // setColumnType at index c.index
                newTypes.reserve(oldColCount);
                newToOld.reserve(oldColCount);
                for (size_t i = 0; i < oldColCount; ++i) {
                    newTypes.push_back(i == c.index ? c.new_type : oldTypes[i]);
                    newToOld.push_back(static_cast<int>(i));
                }
            }
        } else {
            // setColumns (full replacement)
            newTypes.reserve(changes.size());
            newToOld.reserve(changes.size());
            for (const auto& c : changes) {
                if (c.new_type != ColumnType::VOID) {
                    newTypes.push_back(c.new_type);
                    newToOld.push_back(c.old_type != ColumnType::VOID ? static_cast<int>(c.index) : -1);
                }
            }
        }

        // Compute new offsets
        const size_t newColCount = newTypes.size();
        std::vector<uint32_t> newOffsets(newColCount);
        uint32_t dataSize = 0;
        Layout::Data::computeOffsets(newTypes, newOffsets, dataSize);

        // Count bools and strings for container sizing
        size_t newBoolCount = 0, newStrCount = 0;
        for (auto t : newTypes) {
            if (t == ColumnType::BOOL) ++newBoolCount;
            else if (t == ColumnType::STRING) ++newStrCount;
        }

        // Allocate new storage (zero-initialized)
        size_t bitsSize = isTrackingEnabled(Policy) ? newColCount : newBoolCount;
        bits_.resize(bitsSize);
        bits_.reset(); // All bools false, all change flags clear
        data_.assign(dataSize, std::byte{0});
        strg_.clear();
        strg_.resize(newStrCount);

        // Mark all non-bool columns as changed (new default values)
        if constexpr (isTrackingEnabled(Policy)) {
            for (size_t ni = 0; ni < newColCount; ++ni) {
                if (newTypes[ni] != ColumnType::BOOL) {
                    bits_[ni] = true;
                }
            }
        }

        // Preserve old values where types match
        for (size_t ni = 0; ni < newColCount; ++ni) {
            int oi = newToOld[ni];
            if (oi < 0 || static_cast<size_t>(oi) >= oldColCount) continue;
            if (oldTypes[oi] != newTypes[ni]) continue; // type changed → keep default

            uint32_t oOff = oldOffsets[oi];
            uint32_t nOff = newOffsets[ni];

            if (newTypes[ni] == ColumnType::BOOL) {
                // Copy bool value from old bits to new bits
                size_t oldBitsIdx = isTrackingEnabled(Policy) ? static_cast<size_t>(oi) : oOff;
                size_t newBitsIdx = isTrackingEnabled(Policy) ? ni : nOff;
                bits_[newBitsIdx] = oldBits[oldBitsIdx];
            } else if (newTypes[ni] == ColumnType::STRING) {
                strg_[nOff] = std::move(oldStrg[oOff]);
                if constexpr (isTrackingEnabled(Policy)) {
                    bits_[ni] = false; // Preserved value — not changed
                }
            } else {
                std::memcpy(&data_[nOff], &oldData[oOff], sizeOf(newTypes[ni]));
                if constexpr (isTrackingEnabled(Policy)) {
                    bits_[ni] = false; // Preserved value — not changed
                }
            }
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
            uint32_t offset = layout_.columnOffset(i);
            
            if constexpr (std::is_same_v<T, bool>) {
                size_t idx = bitsIndex(i);
                bool oldVal = bits_[idx];
                if (oldVal != newValue) {
                    bits_[idx] = newValue;
                    // No separate change tracking for bools
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                std::string& current = strg_[offset];
                if (current != newValue) {
                    current = newValue;
                    if constexpr (isTrackingEnabled(Policy)) {
                        bits_[i] = true;
                    }
                }
            } else {
                T& current = *reinterpret_cast<T*>(&data_[offset]);
                if (current != newValue) {
                    current = newValue;
                    if constexpr (isTrackingEnabled(Policy)) {
                        bits_[i] = true;
                    }
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
        
        // Unregister from current layout
        layout_.unregisterCallback(this);
        
        // Move other's data (no manual destruction needed — vectors/Bitset clean up themselves)
        layout_ = other.layout_;
        bits_ = std::move(other.bits_);
        data_ = std::move(other.data_);
        strg_ = std::move(other.strg_);
        
        // Re-register with new this pointer
        layout_.registerCallback(this, {
            [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
        });
        
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

    /** @brief Type-safe visit: iterate RowView columns of known type T (compile-time dispatch)
     *
     * Eliminates the runtime ColumnType switch for homogeneous column ranges.
     * For scalar types (int32_t, double, etc.) the visitor receives a mutable reference
     * directly into the serialized buffer. For bool, the visitor also gets a mutable reference.
     * For strings (T = std::string_view), the visitor receives a read-only string_view
     * pointing into the buffer (cannot resize).
     *
     * No change tracking — RowView has no tracking infrastructure. The bool& changed
     * parameter is accepted but ignored.
     *
     * @tparam T         The expected column type (use std::string_view for STRING columns)
     * @tparam Visitor    Callable accepting (size_t, T&, bool&) or (size_t, T&)
     * @param startIndex  First column index to visit
     * @param visitor     Callable for in-place access
     * @param count       Number of consecutive columns to visit (default 1)
     *
     * @par Example — Scale 100 double columns in-place
     * @code
     * rowView.visit<double>(0, [](size_t, double& v, bool&) {
     *     v *= 2.0;
     * }, 100);
     * @endcode
     */
    template<typename T, typename Visitor>
        requires TypedRowVisitor<Visitor, T>
    inline void RowView::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;

        size_t endIndex = startIndex + count;

        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("RowView::visit<T>: Range [" + std::to_string(startIndex) +
                    ", " + std::to_string(endIndex) + ") exceeds column count " +
                    std::to_string(layout_.columnCount()));
            }
            constexpr ColumnType expectedType = toColumnType<T>();
            for (size_t i = startIndex; i < endIndex; ++i) {
                if (layout_.columnType(i) != expectedType) [[unlikely]] {
                    throw std::runtime_error("RowView::visit<T>: Type mismatch at column " + std::to_string(i) +
                        ". Expected " + std::string(toString(expectedType)) +
                        ", actual " + std::string(toString(layout_.columnType(i))));
                }
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "RowView::visit<T>: Range out of bounds");
        }

        for (size_t i = startIndex; i < endIndex; ++i) {
            uint32_t offset = offsets_[i];
            std::byte* ptr = &buffer_[offset];

            if constexpr (std::is_same_v<T, bool>) {
                // Bool: mutable reference into buffer
                bool& value = *reinterpret_cast<bool*>(ptr);
                if constexpr (std::is_invocable_v<Visitor, size_t, bool&, bool&>) {
                    bool changed = true;
                    visitor(i, value, changed);
                } else {
                    visitor(i, value);
                }
            } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
                // Strings: read-only (buffer is fixed-size, cannot resize)
                StringAddr strAddr;
                std::memcpy(&strAddr, ptr, sizeof(StringAddr));
                size_t strOffset = strAddr.offset();
                size_t strLength = strAddr.length();

                if constexpr (RANGE_CHECKING) {
                    if (strOffset + strLength > buffer_.size()) [[unlikely]] {
                        throw std::runtime_error("RowView::visit<T>() string payload out of bounds");
                    }
                }

                // Provide as string_view (mutable visit still read-only for strings)
                std::string_view sv(
                    reinterpret_cast<const char*>(&buffer_[strOffset]),
                    strLength
                );
                // Note: visitor gets string_view by reference but modifying the view
                // only changes the local view, not the buffer content
                if constexpr (std::is_invocable_v<Visitor, size_t, std::string_view&, bool&>) {
                    bool changed = true;
                    visitor(i, sv, changed);
                } else if constexpr (std::is_invocable_v<Visitor, size_t, std::string_view&>) {
                    visitor(i, sv);
                } else if constexpr (std::is_invocable_v<Visitor, size_t, std::string&, bool&>) {
                    // Caller requested std::string — create temporary
                    std::string tmp(sv);
                    bool changed = true;
                    visitor(i, tmp, changed);
                } else {
                    std::string tmp(sv);
                    visitor(i, tmp);
                }
            } else {
                // Scalar: direct mutable reference into buffer (no switch needed)
                T& value = *reinterpret_cast<T*>(ptr);
                if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                    bool changed = true;
                    visitor(i, value, changed);
                } else {
                    visitor(i, value);
                }
            }
        }
    }

    /** @brief Type-safe const visit: iterate RowView columns of known type T (read-only, compile-time dispatch)
     *
     * Read-only counterpart of visit<T>(). Zero-copy for both scalars and strings.
     * Scalars are accessed via memcpy (safe for unaligned access).
     * Strings yield std::string_view directly into the buffer.
     *
     * @tparam T         The expected column type (use std::string_view for STRING columns)
     * @tparam Visitor    Callable accepting (size_t, const T&)
     * @param startIndex  First column index to visit
     * @param visitor     Read-only callable
     * @param count       Number of consecutive columns to visit (default 1)
     *
     * @par Example — Sum 50 consecutive int32 columns
     * @code
     * int64_t sum = 0;
     * rowView.visitConst<int32_t>(0, [&](size_t, const int32_t& v) {
     *     sum += v;
     * }, 50);
     * @endcode
     */
    template<typename T, typename Visitor>
        requires TypedRowVisitorConst<Visitor, T>
    inline void RowView::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) return;

        size_t endIndex = startIndex + count;

        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("RowView::visitConst<T>: Range [" + std::to_string(startIndex) +
                    ", " + std::to_string(endIndex) + ") exceeds column count " +
                    std::to_string(layout_.columnCount()));
            }
            constexpr ColumnType expectedType = toColumnType<T>();
            for (size_t i = startIndex; i < endIndex; ++i) {
                if (layout_.columnType(i) != expectedType) [[unlikely]] {
                    throw std::runtime_error("RowView::visitConst<T>: Type mismatch at column " + std::to_string(i) +
                        ". Expected " + std::string(toString(expectedType)) +
                        ", actual " + std::string(toString(layout_.columnType(i))));
                }
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "RowView::visitConst<T>: Range out of bounds");
        }

        for (size_t i = startIndex; i < endIndex; ++i) {
            uint32_t offset = offsets_[i];
            const std::byte* ptr = &buffer_[offset];

            if constexpr (std::is_same_v<T, bool>) {
                bool value;
                std::memcpy(&value, ptr, sizeof(bool));
                visitor(i, value);
            } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
                // Zero-copy string access via string_view
                StringAddr strAddr;
                std::memcpy(&strAddr, ptr, sizeof(StringAddr));
                size_t strOffset = strAddr.offset();
                size_t strLength = strAddr.length();

                if constexpr (RANGE_CHECKING) {
                    if (strOffset + strLength > buffer_.size()) [[unlikely]] {
                        throw std::runtime_error("RowView::visitConst<T>() string payload out of bounds");
                    }
                }

                const std::string_view sv(
                    reinterpret_cast<const char*>(&buffer_[strOffset]),
                    strLength
                );
                if constexpr (std::is_same_v<T, std::string_view>) {
                    visitor(i, sv);
                } else {
                    // T = std::string — create temporary for the visitor
                    const std::string tmp(sv);
                    visitor(i, tmp);
                }
            } else {
                // Scalar: memcpy for safe unaligned access
                T value;
                std::memcpy(&value, ptr, sizeof(T));
                visitor(i, value);
            }
        }
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
        if constexpr (Index < COLUMN_COUNT) {
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
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");
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
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "RowStatic: Access range exceeds column count");

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
            if (index >= COLUMN_COUNT) [[unlikely]] {
                return nullptr;
            }
        } else {
            assert(index < COLUMN_COUNT && "RowStatic::get(index): Index out of bounds");
        }

        // 2. Define Function Pointer Signature
        using Self = RowStaticImpl<Policy, ColumnTypes...>;
        using GetterFunc = const void* (*)(const Self&);

        // 3. Generate Jump Table (Static Constexpr)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, COLUMN_COUNT>{
                // Lambda capturing behavior for index I
                +[](const Self& self) -> const void* {
                    return &std::get<I>(self.data_);
                }...
            };
        }(std::make_index_sequence<COLUMN_COUNT>{});

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
        if (index + dst.size() > COLUMN_COUNT) [[unlikely]] {
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
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");


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
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "RowStatic: Range exceeds column count");
        
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
            if (index + values.size() > COLUMN_COUNT) {
                throw std::out_of_range("RowStatic::set(span): Span exceeds column count");
            }
        } else {
            assert(index + values.size() <= COLUMN_COUNT && "RowStatic::set(span): Span exceeds column count");
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
        size_t offVar = OFFSET_VAR;                 // offset to the begin of variable-size data section (relative to row start)
        buffer.resize(buffer.size() + OFFSET_VAR);  // ensure buffer is large enough to hold fixed-size data

        // serialize each tuple element using compile-time recursion
        serializeElements<0>(buffer, offRow, offVar);
        return {&buffer[offRow], buffer.size() - offRow};
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::serializeElements(ByteBuffer& buffer, const size_t& offRow, size_t& offVar) const {
        if constexpr (Index < COLUMN_COUNT) {
            constexpr size_t lenFix = COLUMN_LENGTHS[Index];
            constexpr size_t offFix = COLUMN_OFFSETS[Index];
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
     * Only the columns that have changed (as indicated by the change tracking Bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change Bitset.
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

        Bitset<COLUMN_COUNT> rowHeader = changes_;              // make a copy to modify for bools (changes_ are const!)
        buffer.resize(buffer.size() + rowHeader.sizeBytes());    // reserve space for rowHeader (Bitset) at the begin of the row
        
        // Serialize each tuple element using compile-time recursion
        serializeElementsZoH<0>(buffer, rowHeader);

        // after serializing the elements, write the rowHeader to the begin of the row
        // Calculate pointer after all resizes to avoid dangling pointer from vector reallocation
        std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());
        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::serializeElementsZoH(ByteBuffer& buffer, Bitset<COLUMN_COUNT>& rowHeader) const 
    {
        if constexpr (Index < COLUMN_COUNT) {
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
        //we expect the buffer, starts with the first byte of the row and ends with the last byte of the row (no change Bitset)
        deserializeElements<0>(buffer);
    }

    template<TrackingPolicy Policy, typename... ColumnTypes>
    template<size_t Index>
    void RowStaticImpl<Policy, ColumnTypes...>::deserializeElements(const std::span<const std::byte> &buffer) 
    {
        if constexpr (Index < COLUMN_COUNT) {
            constexpr size_t len = wireSizeOf<column_type<Index>>();
            constexpr size_t off = COLUMN_OFFSETS[Index];

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
        // we expect the buffer to start with the change Bitset, followed by the actual row data
        if (buffer.size() < changes_.sizeBytes()) {
            throw std::runtime_error("RowStatic::deserializeFromZoH() failed! Buffer too small to contain change Bitset.");
        } else {
            // read change Bitset from beginning of buffer
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
        // We also expect that the change Bitset has been read already!

        if constexpr (Index < COLUMN_COUNT) {
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
        
        size_t endIndex = std::min(startIndex + count, COLUMN_COUNT);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= COLUMN_COUNT) {
                throw std::out_of_range("RowStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < COLUMN_COUNT && "RowStatic::visit: Start index out of bounds");
        }
        
        // Runtime loop with compile-time type dispatch
        for (size_t i = startIndex; i < endIndex; ++i) {
            // Use fold expression to dispatch to correct tuple element at runtime
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((I == i ? (visitor(I, std::get<I>(data_)), true) : false) || ...);
            }(std::make_index_sequence<COLUMN_COUNT>{});
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
        visitConst(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
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
        
        size_t endIndex = std::min(startIndex + count, COLUMN_COUNT);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= COLUMN_COUNT) {
                throw std::out_of_range("RowStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < COLUMN_COUNT && "RowStatic::visit: Start index out of bounds");
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
            }(std::make_index_sequence<COLUMN_COUNT>{});
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
        visit(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
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
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");
        
        // check buffer validity
        if (buffer_.data() == nullptr) [[unlikely]] {
             throw std::runtime_error("RowViewStatic::get<" + std::to_string(Index) + ">: Buffer not set");
        }

        constexpr size_t length = COLUMN_LENGTHS[Index];
        constexpr size_t offset = COLUMN_OFFSETS[Index];
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
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "RowViewStatic: Range exceeds column count");

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
            constexpr size_t start_offset = COLUMN_OFFSETS[StartIndex];
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
        assert(index < COLUMN_COUNT && "RowViewStatic<ColumnTypes...>::get(index): Index out of bounds");
        
        if (index >= COLUMN_COUNT || buffer_.empty()) 
            return {};

        using GetterFunc = std::span<const std::byte> (*)(const RowViewStatic&);

        // O(1) Jump Table to handle type-specific logic (StringAddr vs Primitive)
        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, COLUMN_COUNT>{
                +[](const RowViewStatic& self) -> std::span<const std::byte> {
                    constexpr size_t offset = COLUMN_OFFSETS[I];
                    constexpr size_t length    = COLUMN_LENGTHS[I]; // Size of fixed field (primitives or StringAddr)
                    
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
        }(std::make_index_sequence<COLUMN_COUNT>{});

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
        if (iMax > COLUMN_COUNT) return false;
        
        // 3. Type Consistency Check (Fast Fail)
        auto dstType = toColumnType<T>();
        for (size_t i = index; i < iMax; ++i) {
             if (layout_.columnType(i) != dstType) [[unlikely]] {
                 return false; 
             }
        }

        // 4. check buffer size for last column
        const size_t offset = COLUMN_OFFSETS[index];
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
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");
        using ColT = column_type<Index>;

        static_assert(std::is_same_v<ColT, T> && std::is_arithmetic_v<T>, 
            "RowViewStatic::set<I> only supports matching primitive types. Strings not supported.");

        constexpr size_t offset = COLUMN_OFFSETS[Index];
        
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
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "Out of bounds");
        
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
        if (index + values.size() > COLUMN_COUNT) return;
        
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
        }(std::make_index_sequence<COLUMN_COUNT>{});
        
        return row;
    }

    template<typename... ColumnTypes>
    bool RowViewStatic<ColumnTypes...>::validate() const noexcept 
    {
        if(buffer_.empty()) {
            return false;
        }

        if constexpr (COLUMN_COUNT == 0) {
            return true; // Nothing to validate
        }

        // Check if buffer is large enough for all fixed fields
        if (LayoutType::COLUMN_OFFSETS[COLUMN_COUNT-1] + LayoutType::COLUMN_LENGTHS[COLUMN_COUNT-1] > buffer_.size()) {
            return false;
        }

        // Validate string payloads using template recursion
        return validateStringPayloads<0>();
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowViewStatic<ColumnTypes...>::validateStringPayloads() const {
        if constexpr (Index >= COLUMN_COUNT) {
            return true;
        } else {
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                StringAddr addr;
                std::memcpy(&addr, buffer_.data() + LayoutType::COLUMN_OFFSETS[Index], sizeof(addr));
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
        
        size_t endIndex = std::min(startIndex + count, COLUMN_COUNT);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= COLUMN_COUNT) {
                throw std::out_of_range("RowViewStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < COLUMN_COUNT && "RowViewStatic::visit: Start index out of bounds");
        }
        
        // Runtime loop with compile-time type dispatch
        for (size_t i = startIndex; i < endIndex; ++i) {
            // Use fold expression to dispatch to correct column at runtime
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((I == i ? (visitor(I, get<I>()), true) : false) || ...);
            }(std::make_index_sequence<COLUMN_COUNT>{});
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
        visitConst(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
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
        
        size_t endIndex = std::min(startIndex + count, COLUMN_COUNT);
        
        if constexpr (RANGE_CHECKING) {
            if (startIndex >= COLUMN_COUNT) {
                throw std::out_of_range("RowViewStatic::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(startIndex < COLUMN_COUNT && "RowViewStatic::visit: Start index out of bounds");
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
                            constexpr size_t offset = COLUMN_OFFSETS[I];
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
            }(std::make_index_sequence<COLUMN_COUNT>{});
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
        visit(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
    }

} // namespace bcsv