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

#include <cstddef>
#include <cstdint>
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
    inline bool RowImpl<Policy>::trackingAnyChanged() const noexcept {
        if constexpr (isTrackingEnabled(Policy)) {
            return bits_.any(layout_.trackedMask()); // check only change flags (non-BOOL columns)
        } else {
            return true; // always "changed" when tracking disabled
        }
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::trackingResetChanged() noexcept {
        if constexpr (isTrackingEnabled(Policy)) {
            // Clear all change flags (non-BOOL). BOOL value bits are preserved.
            bits_ &= layout_.boolMask();
        }
    }

    template<TrackingPolicy Policy>
    inline void RowImpl<Policy>::trackingSetAllChanged() noexcept {
        if constexpr (isTrackingEnabled(Policy)) {
            // Set all change flags (non-BOOL). BOOL value bits are preserved.
            bits_ |= layout_.trackedMask();
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

        if (index + dst.size() > layout_.columnCount()) [[unlikely]] {
            throw std::out_of_range("vectorized get(): range out of bounds");
        }

        if (RANGE_CHECKING) {
            const ColumnType* types = layout_.columnTypes().data();
            for (size_t i = index; i < index+dst.size(); ++i) {
                if (targetType != types[i]) [[unlikely]]{
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
        
        // Pre-fetch raw arrays — avoids per-iteration data_-> pointer chase and checkRange()
        const ColumnType* types   = layout_.columnTypes().data();
        const uint32_t*   offsets = layout_.columnOffsets().data();

        // Core implementation: iterate and dispatch for each column
        for (size_t i = startIndex; i < endIndex; ++i) {
            const ColumnType type = types[i];
            const uint32_t offset = offsets[i];
            
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
        
        // Pre-fetch raw arrays — avoids per-iteration data_-> pointer chase and checkRange()
        const ColumnType* types   = layout_.columnTypes().data();
        const uint32_t*   offsets = layout_.columnOffsets().data();

        // Core implementation: iterate and dispatch for each column
        for (size_t i = startIndex; i < endIndex; ++i) {
            const ColumnType type = types[i];
            const uint32_t offset = offsets[i];

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
        if (count == 0) return; // NOOP

        size_t endIndex = startIndex + count;
        // Pre-fetch raw arrays — avoids per-iteration data_-> pointer chase and checkRange()
        const ColumnType* types   = layout_.columnTypes().data();
        const uint32_t*   offsets = layout_.columnOffsets().data();
        for (size_t i = startIndex; i < endIndex; ++i) {
            const uint32_t offset = offsets[i];
            if constexpr (RANGE_CHECKING) {
                if (i >= layout_.columnCount()) [[unlikely]] {
                    throw std::out_of_range("Row::visit<T>: Column index out of range");
                }
                if (types[i] != toColumnType<T>()) [[unlikely]] {
                    throw std::runtime_error("Row::visit<T>: Type mismatch at column " + std::to_string(i) +
                        ". Expected " + std::string(toString(toColumnType<T>())) +
                        ", actual " + std::string(toString(types[i])));
                }
            }

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
        if (count == 0) return; //NOOP

        size_t endIndex = startIndex + count;
        // Pre-fetch raw arrays — avoids per-iteration data_-> pointer chase and checkRange()
        const ColumnType* types   = layout_.columnTypes().data();
        const uint32_t*   offsets = layout_.columnOffsets().data();
        for (size_t i = startIndex; i < endIndex; ++i) {
            const uint32_t offset = offsets[i];
            if constexpr (RANGE_CHECKING) {
                if (i >= layout_.columnCount()) [[unlikely]] {
                    throw std::out_of_range("Row::visitConst<T>: Column index out of range");
                }
                if (types[i] != toColumnType<T>()) [[unlikely]] {
                    throw std::runtime_error("Row::visitConst<T>: Type mismatch at column " + std::to_string(i) +
                        ". Expected " + std::string(toString(toColumnType<T>())) +
                        ", actual " + std::string(toString(types[i])));
                }
            }
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
    // RowStatic Visit
    // ========================================================================

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
        
        size_t endIndex = startIndex + count;
        
        if constexpr (RANGE_CHECKING) {
            if (endIndex > COLUMN_COUNT) {
                throw std::out_of_range("RowStatic::visit: Range [" + std::to_string(startIndex) +
                    ", " + std::to_string(endIndex) + ") exceeds column count " +
                    std::to_string(COLUMN_COUNT));
            }
        } else {
            assert(endIndex <= COLUMN_COUNT && "RowStatic::visit: Range out of bounds");
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
        
        size_t endIndex = startIndex + count;
        
        if constexpr (RANGE_CHECKING) {
            if (endIndex > COLUMN_COUNT) {
                throw std::out_of_range("RowStatic::visit: Range [" + std::to_string(startIndex) +
                    ", " + std::to_string(endIndex) + ") exceeds column count " +
                    std::to_string(COLUMN_COUNT));
            }
        } else {
            assert(endIndex <= COLUMN_COUNT && "RowStatic::visit: Range out of bounds");
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

} // namespace bcsv
