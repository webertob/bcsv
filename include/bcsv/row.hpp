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

    inline Row::Row(const Layout& layout)
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

        // Compute dataSize from layout offsets
        {
            std::vector<uint32_t> tmpOffsets;
            Layout::Data::computeOffsets(layout_.columnTypes(), tmpOffsets, dataSize);
        }

        // Initialize storage
        data_.resize(dataSize);  // zero-initialized = scalar defaults (0 for all arithmetic types)
        strg_.resize(strgCount); // default-constructed empty strings
        bits_.resize(boolCount, false);  // just bool values, default false
    }

    inline Row::Row(const Row& other)
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

    inline Row::Row(Row&& other)
        : layout_(other.layout_)
        , bits_(std::move(other.bits_))
        , data_(std::move(other.data_))
        , strg_(std::move(other.strg_))
    {
        layout_.registerCallback(this, {
            [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
        });
    }

    inline Row::~Row()
    {
        // Unregister from layout callbacks
        layout_.unregisterCallback(this);
    }

    /** Clear the row to its default state (default values) */
    inline void Row::clear()
    {
        // Zero all scalar data
        std::memset(data_.data(), 0, data_.size());

        // Clear all strings
        for (auto& s : strg_) {
            s.clear();
        }

        // Reset all bits (bools = false)
        bits_.reset();
    }

    // =========================================================================
    // 1. RAW POINTER ACCESS (caller must ensure type safety)
    // =========================================================================
    inline const void* Row::get(size_t index) const {
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
            tl_bool[tl_idx] = bits_[offset];
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
    template<typename T>
    inline decltype(auto) Row::get(size_t index) const {
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
            return bits_[offset];  // bool by value (Bitset has no addressable storage)
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

    /** Vectorized access to multiple columns of same type - STRICT. */
    template<typename T>
    inline void Row::get(size_t index, std::span<T> &dst) const
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
                dst[i] = bits_[layout_.columnOffset(index + i)];
            }
        } else {
            // Consecutive same-type scalars are contiguous in aligned data_
            uint32_t offset = layout_.columnOffset(index);
            memcpy(dst.data(), &data_[offset], dst.size() * sizeof(T));
        }
    }

    // =========================================================================
    // 3. FLEXIBLE / CONVERTING ACCESS
    // =========================================================================
    template<typename T>
    inline bool Row::get(size_t index, T &dst) const {
        bool success = false;
        
        visitConst(index, [&dst, &success](size_t, const auto& value) {
            using SrcType = std::decay_t<decltype(value)>;
            
            if constexpr (std::is_assignable_v<T&, SrcType>) {
                dst = value;
                success = true;
            } else if constexpr (std::is_same_v<SrcType, std::string>) {
                if constexpr (std::is_same_v<T, std::string>) {
                    dst = value;
                    success = true;
                } else if constexpr (std::is_same_v<T, std::string_view>) {
                    dst = std::string_view(value);
                    success = true;
                } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                    dst = std::span<const char>(value);
                    success = true;
                } else if constexpr (std::is_assignable_v<T&, std::string>) {
                    dst = value;
                    success = true;
                }
            }
        });
        
        return success;
    }

    template<typename T>
    inline decltype(auto) Row::ref(size_t index) {
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
            return bits_[offset];  // returns Bitset<>::reference proxy
        } else if constexpr (std::is_same_v<T, std::string>) {
            return static_cast<std::string&>(strg_[offset]);
        } else {
            return *reinterpret_cast<T*>(&data_[offset]);
        }
    } 

    /** 
     * Set the value at the specified column index.
     */
    template<detail::BcsvAssignable T>
    inline void Row::set(size_t index, const T& value) {

    // Helper: detect bool proxy types
    constexpr bool isBoolLike = std::is_same_v<T, bool> || 
        (std::is_convertible_v<T, bool> && !std::is_arithmetic_v<T> && !std::is_same_v<T, std::string>);

    // Helper: detect string-like types
    constexpr bool isStringLike = detail::is_string_like_v<T> || 
        (std::is_convertible_v<T, std::string_view> && !detail::is_primitive_v<T> && !isBoolLike);

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
        bits_[offset] = static_cast<bool>(value);
    } else if constexpr (isStringLike) {
        std::string& str = strg_[offset];
        str = value;
        if (str.size() > MAX_STRING_LENGTH) {
            str.resize(MAX_STRING_LENGTH);
        }
    } else {
        *reinterpret_cast<T*>(&data_[offset]) = value;
    }
}



    /** Vectorized set of multiple columns of same type */
    template<typename T>
    inline void Row::set(size_t index, std::span<const T> values)
    {
        static_assert(std::is_arithmetic_v<T>, "Only primitive arithmetic types are supported for vectorized set");

        if constexpr (RANGE_CHECKING) {
            if (index + values.size() > layout_.columnCount()) [[unlikely]] {
                throw std::out_of_range("vectorized set() range [" + std::to_string(index) + ", "
                    + std::to_string(index + values.size()) + ") exceeds columnCount " + std::to_string(layout_.columnCount()));
            }
            for (size_t i = 0; i < values.size(); ++i) {
                const ColumnType &targetType = layout_.columnType(index + i);
                if (toColumnType<T>() != targetType) [[unlikely]]{
                    throw std::runtime_error("vectorized set() types must match exactly");
                }
            }
        }

        if constexpr (std::is_same_v<T, bool>) {
            for (size_t i = 0; i < values.size(); ++i) {
                bits_[layout_.columnOffset(index + i)] = values[i];
            }
        } else {
            uint32_t offset = layout_.columnOffset(index);
            std::memcpy(reinterpret_cast<T*>(&data_[offset]), values.data(), values.size() * sizeof(T));
        }
    }

    // =========================================================================
    // Visit (const)
    // =========================================================================

    template<RowVisitorConst Visitor>
    inline void Row::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) 
            return;
        
        size_t endIndex = startIndex + count;
        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("Row::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "Row::visit: Start index out of bounds");
        }
        
        const ColumnType* types   = layout_.columnTypes().data();
        const uint32_t*   offsets = layout_.columnOffsets().data();

        for (size_t i = startIndex; i < endIndex; ++i) {
            const ColumnType type = types[i];
            const uint32_t offset = offsets[i];
            
            switch(type) {
                case ColumnType::BOOL: {
                    bool val = bits_[offset];
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

    template<RowVisitorConst Visitor>
    inline void Row::visitConst(Visitor&& visitor) const {
        visitConst(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    // =========================================================================
    // Visit (mutable)
    // =========================================================================

    template<RowVisitor Visitor>
    inline void Row::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;
        
        size_t endIndex = startIndex + count;
        
        if constexpr (RANGE_CHECKING) {
            if (endIndex > layout_.columnCount()) {
                throw std::out_of_range("Row::visit: Invalid start index " + std::to_string(startIndex));
            }
        } else {
            assert(endIndex <= layout_.columnCount() && "Row::visit: Start index out of bounds");
        }
        
        const ColumnType* types   = layout_.columnTypes().data();
        const uint32_t*   offsets = layout_.columnOffsets().data();

        for (size_t i = startIndex; i < endIndex; ++i) {
            const ColumnType type = types[i];
            const uint32_t offset = offsets[i];

            if (type == ColumnType::BOOL) {
                bool val = bits_[offset];
                visitor(i, val);
                bits_[offset] = val;  // Write back
            } else if (type == ColumnType::STRING) {
                std::string& str = strg_[offset];
                visitor(i, str);
                if (str.size() > MAX_STRING_LENGTH) {
                    str.resize(MAX_STRING_LENGTH);
                }
            } else {
                auto dispatch = [&](auto& value) {
                    visitor(i, value);
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

    template<RowVisitor Visitor>
    inline void Row::visit(Visitor&& visitor) {
        visit(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    // ========================================================================
    // Type-Safe visit<T>() — Compile-Time Dispatch (No Runtime Switch)
    // ========================================================================

    template<typename T, typename Visitor>
        requires TypedRowVisitor<Visitor, T>
    inline void Row::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;

        size_t endIndex = startIndex + count;
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
                bool val = bits_[offset];
                visitor(i, val);
                bits_[offset] = val;
            } else if constexpr (std::is_same_v<T, std::string>) {
                std::string& str = strg_[offset];
                visitor(i, str);
                if (str.size() > MAX_STRING_LENGTH) {
                    str.resize(MAX_STRING_LENGTH);
                }
            } else {
                T& value = *reinterpret_cast<T*>(&data_[offset]);
                visitor(i, value);
            }
        }
    }

    template<typename T, typename Visitor>
        requires TypedRowVisitorConst<Visitor, T>
    inline void Row::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) return;

        size_t endIndex = startIndex + count;
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
                const bool val = bits_[offset];
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

    inline void Row::onLayoutUpdate(const std::vector<Layout::Data::Change>& changes) {
        if (changes.empty()) {
            return;
        }

        // Save old state
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

        // Build new column types and mapping (newIndex -> oldIndex, -1 if new)
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
        bits_.resize(newBoolCount);
        bits_.reset();
        data_.assign(dataSize, std::byte{0});
        strg_.clear();
        strg_.resize(newStrCount);

        // Preserve old values where types match
        for (size_t ni = 0; ni < newColCount; ++ni) {
            int oi = newToOld[ni];
            if (oi < 0 || static_cast<size_t>(oi) >= oldColCount) continue;
            if (oldTypes[oi] != newTypes[ni]) continue; // type changed -> keep default

            uint32_t oOff = oldOffsets[oi];
            uint32_t nOff = newOffsets[ni];

            if (newTypes[ni] == ColumnType::BOOL) {
                bits_[nOff] = oldBits[oOff];
            } else if (newTypes[ni] == ColumnType::STRING) {
                strg_[nOff] = std::move(oldStrg[oOff]);
            } else {
                std::memcpy(&data_[nOff], &oldData[oOff], sizeOf(newTypes[ni]));
            }
        }
    }

    inline Row& Row::operator=(const Row& other)
    {
        if(this == &other) {
            return *this;
        }

        if(!layout_.isCompatible(other.layout())) [[unlikely]] {
            throw std::invalid_argument(
                "Row::operator=: Cannot assign between incompatible layouts. "
                "Layouts must have the same column types in the same order."
            );
        }
        
        // Copy data directly
        bits_ = other.bits_;
        data_ = other.data_;
        strg_ = other.strg_;
        
        return *this;
    }

    inline Row& Row::operator=(Row&& other) 
    {
        if (this == &other) 
            return *this;
        
        layout_.unregisterCallback(this);
        
        layout_ = other.layout_;
        bits_ = std::move(other.bits_);
        data_ = std::move(other.data_);
        strg_ = std::move(other.strg_);
        
        layout_.registerCallback(this, {
            [this](const std::vector<Layout::Data::Change>& changes) { onLayoutUpdate(changes); }
        });
        
        return *this;
    }

    // ========================================================================
    // RowStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...>::RowStatic(const LayoutType& layout) 
        : layout_(layout), data_() 
    {
        clear();
    }

    /** Clear the row to its default state (default values) */
    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::clear()
    {
        if constexpr (Index < COLUMN_COUNT) {
            std::get<Index>(data_) = defaultValueT<column_type<Index>>();
            clear<Index + 1>();
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    const auto& RowStatic<ColumnTypes...>::get() const noexcept {
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::get(std::span<T, Extent> &dst) const {
        static_assert(Extent != std::dynamic_extent, "RowStatic: Static vectorized get requires fixed-extent span (std::span<T, N>)");
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "RowStatic: Access range exceeds column count");

        [&]<size_t... I>(std::index_sequence<I...>) {
            static_assert((std::is_assignable_v<T&, const column_type<StartIndex + I>&> && ...), 
                "RowStatic::get(span): Column types are not assignable to destination span type");
        }(std::make_index_sequence<Extent>{});

        constexpr bool all_types_match = []<size_t... I>(std::index_sequence<I...>) {
            return ((std::is_same_v<T, column_type<StartIndex + I>>) && ...);
        }(std::make_index_sequence<Extent>{});
        
        if constexpr (all_types_match) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((dst[I] = std::get<StartIndex + I>(data_)), ...);
            }(std::make_index_sequence<Extent>{});
        } else {
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((dst[I] = static_cast<T>(std::get<StartIndex + I>(data_))), ...);
            }(std::make_index_sequence<Extent>{});
        }
    }

    template<typename... ColumnTypes>
    const void* RowStatic<ColumnTypes...>::get(size_t index) const noexcept {
        if constexpr (RANGE_CHECKING) {
            if (index >= COLUMN_COUNT) [[unlikely]] {
                return nullptr;
            }
        } else {
            assert(index < COLUMN_COUNT && "RowStatic::get(index): Index out of bounds");
        }

        using Self = RowStatic<ColumnTypes...>;
        using GetterFunc = const void* (*)(const Self&);

        static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
            return std::array<GetterFunc, COLUMN_COUNT>{
                +[](const Self& self) -> const void* {
                    return &std::get<I>(self.data_);
                }...
            };
        }(std::make_index_sequence<COLUMN_COUNT>{});

        return handlers[index](*this);
    }

    template<typename... ColumnTypes>
    template<typename T>
    const T& RowStatic<ColumnTypes...>::get(size_t index) const {
        static_assert(
            std::is_trivially_copyable_v<T> || 
            std::is_same_v<T, std::string> || 
            std::is_same_v<T, std::string_view> || 
            std::is_same_v<T, std::span<const char>>,
            "RowStatic::get<T>: Type T must be either trivially copyable (primitives) or string-related"
        );
        
        const void* ptr = get(index);
        
        if (ptr == nullptr) [[unlikely]] {
             throw std::out_of_range("RowStatic::get<T>: Invalid column index " + std::to_string(index));
        }

        ColumnType actualType = layout_.columnType(index);
        
        if constexpr (std::is_same_v<T, std::string> || 
                      std::is_same_v<T, std::string_view> || 
                      std::is_same_v<T, std::span<const char>>) {
            if (actualType != ColumnType::STRING) [[unlikely]] {
                throw std::runtime_error(
                    "RowStatic::get<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Requested: string type, Actual: " + std::string(toString(actualType)));
            }
        } else {
            constexpr ColumnType requestedType = toColumnType<T>();
            if (requestedType != actualType) [[unlikely]] {
                throw std::runtime_error(
                    "RowStatic::get<T>: Type mismatch at index " + std::to_string(index) + 
                    ". Requested: " + std::string(toString(requestedType)) + 
                    ", Actual: " + std::string(toString(actualType)));
            }
        }

        return *static_cast<const T*>(ptr);
    }

    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::get(size_t index, std::span<T, Extent> &dst) const {
        if (index + dst.size() > COLUMN_COUNT) [[unlikely]] {
            throw std::out_of_range("RowStatic::get(span): Access range exceeds column count");
        }
        for (size_t i = 0; i < dst.size(); ++i) {
            dst[i] = this->get<T>(index + i);
        }
    }

    template<typename... ColumnTypes>
    template<typename T>
    bool RowStatic<ColumnTypes...>::get(size_t index, T& dst) const noexcept {
        bool success = false;
        
        try {
            visitConst(index, [&](size_t, const auto& value) {
                using SrcType = std::decay_t<decltype(value)>;
                
                if constexpr (std::is_assignable_v<T&, const SrcType&>) {
                    if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<SrcType>) {
                        dst = static_cast<T>(value);
                    } else {
                        dst = value;
                    }
                    success = true;
                } else if constexpr (std::is_same_v<SrcType, std::string>) {
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

    template<typename... ColumnTypes>
    template<typename T>
    T& RowStatic<ColumnTypes...>::ref(size_t index) {
        const T &r = get<T>(index);
        return const_cast<T&>(r);
    } 
    
    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::set(const T& value) {
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");

        using DecayedT = std::decay_t<T>;
        if constexpr (std::is_same_v<DecayedT, std::span<char>> || std::is_same_v<DecayedT, std::span<const char>>) {
            this->set<Index>(std::string_view(value.data(), value.size()));
            return;
        }

        else if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            auto& currentVal = std::get<Index>(data_);

            if constexpr (std::is_convertible_v<T, std::string_view>) {
                std::string_view sv = value;
                if (sv.size() > MAX_STRING_LENGTH) {
                    sv = sv.substr(0, MAX_STRING_LENGTH);
                }
                currentVal.assign(sv);
            } 
            else if constexpr (std::is_arithmetic_v<DecayedT>) {
                currentVal = std::to_string(value);
            }
            else if constexpr (std::is_same_v<DecayedT, char>) {
                currentVal.assign(1, value);
            }
            else {
                static_assert(false, "RowStatic::set<Index>: Unsupported type to assign to string column");
            }
        }

        else {
                static_assert(std::is_assignable_v<column_type<Index>&, const T&>, "Column type cannot be assigned from the provided value");
                std::get<Index>(data_) = static_cast<column_type<Index>>(value);
        }
    } 

    template<typename... ColumnTypes>
    template<typename T>
    void RowStatic<ColumnTypes...>::set(size_t index, const T& value)  {
        visit(index, [&](size_t, auto& colValue) {
            using ColType = std::decay_t<decltype(colValue)>;
            using DecayedT = std::decay_t<T>;
            
            if constexpr (std::is_assignable_v<ColType&, const DecayedT&> || 
                          std::is_same_v<DecayedT, std::span<char>> ||
                          std::is_same_v<DecayedT, std::span<const char>>) {
                colValue = value;
            } else if constexpr (std::is_same_v<DecayedT, std::string_view> && std::is_same_v<ColType, std::string>) {
                colValue = std::string(value);
            } else {
                throw std::runtime_error("RowStatic::set: Type mismatch or unsupported conversion");
            }
        }, 1);
    }

    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::set(std::span<const T, Extent> values) {
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "RowStatic: Range exceeds column count");
        
        [&]<size_t... I>(std::index_sequence<I...>) {
            (this->template set<StartIndex + I>(values[I]), ...);
        }(std::make_index_sequence<Extent>{});
    }

    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    void RowStatic<ColumnTypes...>::set(size_t index, std::span<const T, Extent> values) {
        if constexpr (RANGE_CHECKING) {
            if (index + values.size() > COLUMN_COUNT) {
                throw std::out_of_range("RowStatic::set(span): Span exceeds column count");
            }
        } else {
            assert(index + values.size() <= COLUMN_COUNT && "RowStatic::set(span): Span exceeds column count");
        }

        for(size_t i = 0; i < values.size(); ++i) {
            this->set(index + i, values[i]);
        }
    }

    // ========================================================================
    // RowStatic Visit
    // ========================================================================

    template<typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowStatic<ColumnTypes...>::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        if (count == 0) return;
        
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
        
        for (size_t i = startIndex; i < endIndex; ++i) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((I == i ? (visitor(I, std::get<I>(data_)), true) : false) || ...);
            }(std::make_index_sequence<COLUMN_COUNT>{});
        }
    }

    template<typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowStatic<ColumnTypes...>::visitConst(Visitor&& visitor) const {
        visitConst(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
    }

    template<typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowStatic<ColumnTypes...>::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        if (count == 0) return;
        
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
        
        for (size_t i = startIndex; i < endIndex; ++i) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                ([&] {
                    if (I == i) {
                        visitor(I, std::get<I>(data_));
                    }
                }(), ...);
            }(std::make_index_sequence<COLUMN_COUNT>{});
        }
    }

    template<typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowStatic<ColumnTypes...>::visit(Visitor&& visitor) {
        visit(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
    }

} // namespace bcsv
