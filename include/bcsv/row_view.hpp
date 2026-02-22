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
 * @file row_view.hpp
 * @brief ARCHIVED — RowView and RowViewStatic implementations.
 *
 * These classes are NOT part of the active bcsv API. They are preserved here
 * for potential future sparse-access work. RowView did not deliver the intended
 * speed gains for sparse read/write access while adding significant complexity.
 *
 * NOTE: The codec sparse-path methods (readColumn, visitSparse, visitConstSparse,
 *       visitSparseTyped, visitConstSparseTyped) that these classes depend on
 *       were also removed from the active RowCodecFlat001. If reintroducing
 *       RowView, those codec methods must be restored as well.
 *
 * Last commit containing the active codec sparse-path: 8575319 (git log -1 8575319)
 */

#include "row_view.h"
#include "row_codec_flat001.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace bcsv {

    // ========================================================================
    // RowView Implementation
    // ========================================================================

    inline RowView::RowView(const Layout& layout, std::span<std::byte> buffer)
                : layout_(layout), 
                    codec_()
    {
        codec_.setup(layout_);
                codec_.setBuffer(buffer);
    }

    inline RowView::RowView(const RowView& other)
        : layout_(other.layout_),
                    codec_(other.codec_)
    {
        codec_.setup(layout_);
                codec_.setBuffer(other.buffer());
    }

    inline RowView::RowView(RowView&& other) noexcept
        : layout_(std::move(other.layout_)),
                    codec_(std::move(other.codec_))
    {
        codec_.setup(layout_);
    }

    inline RowView& RowView::operator=(const RowView& other) {
        if (this != &other) {
            layout_ = other.layout_;
            codec_ = other.codec_;
            codec_.setup(layout_);
            codec_.setBuffer(other.buffer());
        }
        return *this;
    }

    inline RowView& RowView::operator=(RowView&& other) noexcept {
        if (this != &other) {
            layout_ = std::move(other.layout_);
            codec_ = std::move(other.codec_);
            codec_.setup(layout_);
        }
        return *this;
    }

    inline std::span<const std::byte> RowView::get(size_t index) const {
        return codec_.readColumn(index);
    }

    template<typename T>
    inline bool RowView::get(size_t index, std::span<T> &dst) const
    {
        static_assert(std::is_arithmetic_v<T>, "vectorized get() supports arithmetic types only");

        if (dst.empty()) {
            return true;
        }

        const auto& buffer = codec_.buffer();
        const uint32_t* offsets = codec_.columnOffsetsPtr();

        if(buffer.data() == nullptr || buffer.size() < codec_.wireFixedSize()) [[unlikely]] {
            return false;
        }

        if (index + dst.size() > layout_.columnCount()) [[unlikely]] {
            throw std::out_of_range("RowView::get(span): range out of bounds");
        }

        if (RANGE_CHECKING) {
            constexpr auto targetType = toColumnType<T>();
            const ColumnType* types = layout_.columnTypes().data();
            size_t iMax = index + dst.size();
            for (size_t i = index; i < iMax; ++i) {
                const ColumnType sourceType = types[i];
                assert(targetType == sourceType && "RowView::get() type mismatch");
                if (targetType != sourceType) [[unlikely]]{
                    throw std::runtime_error("RowView::get(span): type mismatch");
                }
            }
        }

        if constexpr (std::is_same_v<T, bool>) {
            for (size_t i = 0; i < dst.size(); ++i) {
                size_t bitIdx = offsets[index + i];
                dst[i] = (buffer[bitIdx >> 3] & (std::byte{1} << (bitIdx & 7))) != std::byte{0};
            }
        } else {
            size_t absOff = codec_.wireBitsSize() + offsets[index];
            size_t len = dst.size() * sizeof(T);
            memcpy(dst.data(), &buffer[absOff], len);
        }
        return true;
    }

    template<typename T>
    inline T RowView::get(size_t index) const {
        static_assert(
            std::is_trivially_copyable_v<T> || 
            std::is_same_v<T, std::string> || 
            std::is_same_v<T, std::string_view> || 
            std::is_same_v<T, std::span<const char>>,
            "RowView::get<T>: Type T must be either trivially copyable (primitives) or string-related"
        );
        
        T result;
        bool found = false;
        
        visitConst(index, [&](size_t, auto&& value) {
            using ValueType = std::decay_t<decltype(value)>;
            
            if constexpr (std::is_same_v<T, std::string> || 
                          std::is_same_v<T, std::string_view> || 
                          std::is_same_v<T, std::span<const char>>) {
                if constexpr (std::is_same_v<ValueType, std::string_view>) {
                    if constexpr (std::is_same_v<T, std::string_view>) {
                        result = value;
                    } else if constexpr (std::is_same_v<T, std::span<const char>>) {
                        result = std::span<const char>(value.data(), value.size());
                    } else {
                        result = std::string(value);
                    }
                    found = true;
                }
            } else {
                if constexpr (std::is_same_v<ValueType, std::string_view>) {
                } else if constexpr (std::is_same_v<T, ValueType>) {
                    result = value;
                    found = true;
                }
            }
        }, 1);
        
        if (!found) {
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

    template<typename T>
    inline bool RowView::get(size_t index, T &dst) const {
        bool success = false;
        
        try {
            visitConst(index, [&](size_t, auto&& value) {
                using ValueType = std::decay_t<decltype(value)>;
                
                if constexpr (std::is_assignable_v<T&, ValueType>) {
                    dst = value;
                    success = true;
                } else if constexpr (std::is_same_v<ValueType, std::string_view>) {
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

    template<typename T>
    inline bool RowView::set(size_t index, const T& value) {
        using DecayedT = std::decay_t<T>;
        static_assert(std::is_arithmetic_v<DecayedT>, "RowView::set<T> supports primitive arithmetic types and bool only");
        
        bool success = false;
        
        try {
            visit(index, [&](size_t, auto& colValue, bool&) {
                using ColType = std::decay_t<decltype(colValue)>;
                
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

    template<typename T>
    inline bool RowView::set(size_t index, std::span<const T> src)
    {
        static_assert(std::is_arithmetic_v<T>, "supports primitive arithmetic types only");

        if (src.empty()) {
            return true;
        }
        
        const auto& buffer = codec_.buffer();
        const uint32_t* offsets = codec_.columnOffsetsPtr();
        auto data = buffer.data();
        auto size = buffer.size();
        if(data == nullptr) [[unlikely]] {
            return false;
        }

        if (index + src.size() > layout_.columnCount()) [[unlikely]] {
            throw std::out_of_range("RowView::set(span): range out of bounds");
        }

        if constexpr (RANGE_CHECKING) {
            constexpr ColumnType targetType = toColumnType<T>();
            for (size_t i = 0; i < src.size(); ++i) {
                const ColumnType &sourceType = layout_.columnType(index + i);
                if (targetType != sourceType) [[unlikely]]{
                    throw std::runtime_error("RowView::set(span): type mismatch");
                }
            }
        }

        if constexpr (std::is_same_v<T, bool>) {
            for (size_t i = 0; i < src.size(); ++i) {
                size_t bitIdx = offsets[index + i];
                size_t bytePos = bitIdx >> 3;
                size_t bitPos  = bitIdx & 7;
                if (bytePos >= size) [[unlikely]] return false;
                if (src[i]) data[bytePos] |= std::byte{1} << bitPos;
                else        data[bytePos] &= ~(std::byte{1} << bitPos);
            }
        } else {
            size_t absOff = codec_.wireBitsSize() + offsets[index];
            auto len = src.size() * sizeof(T);
            if (absOff + len > size) [[unlikely]] return false;
            std::memcpy(data + absOff, src.data(), len);
        }
        return true;
    }

    inline Row RowView::toRow() const
    {
        Row row(layout());
        try {
            RowCodecFlat001<Layout, TrackingPolicy::Disabled> codec;
            codec.setup(layout_);
            codec.deserialize(codec_.buffer(), row);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("RowView::toRow failed: ") + e.what());
        }
        return row;
    }

    inline bool RowView::validate(bool deepValidation) const 
    {
        size_t col_count = layout_.columnCount();
        if (col_count == 0) {
            return true;
        }

        const auto& buffer = codec_.buffer();
        auto data = buffer.data();
        auto size = buffer.size();
        if(data == nullptr || size < codec_.wireFixedSize()) {
            return false;
        }
        
        if(deepValidation) {
            const ColumnType* types = layout_.columnTypes().data();
            size_t lens_cursor = codec_.wireBitsSize() + codec_.wireDataSize();
            size_t pay_cursor  = codec_.wireFixedSize();
            for(size_t i = 0; i < col_count; ++i) {
                if (types[i] == ColumnType::STRING) {
                    if (lens_cursor + sizeof(uint16_t) > size) return false;
                    uint16_t len = unalignedRead<uint16_t>(data + lens_cursor);
                    lens_cursor += sizeof(uint16_t);
                    if (pay_cursor + len > size) {
                        return false;
                    }
                    pay_cursor += len;
                }
            }    
        }
        return true;
    }

    template<RowVisitorConst Visitor>
    inline void RowView::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        codec_.visitConstSparse(startIndex, count, std::forward<Visitor>(visitor), "RowView::visitConst");
    }

    template<RowVisitorConst Visitor>
    inline void RowView::visitConst(Visitor&& visitor) const {
        visitConst(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    template<RowVisitor Visitor>
    inline void RowView::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        codec_.visitSparse(startIndex, count, std::forward<Visitor>(visitor), "RowView::visit");
    }

    template<RowVisitor Visitor>
    inline void RowView::visit(Visitor&& visitor) {
        visit(0, std::forward<Visitor>(visitor), layout_.columnCount());
    }

    template<typename T, typename Visitor>
        requires TypedRowVisitor<Visitor, T>
    inline void RowView::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        codec_.template visitSparseTyped<T>(startIndex, count, std::forward<Visitor>(visitor), "RowView::visit<T>");
    }

    template<typename T, typename Visitor>
        requires TypedRowVisitorConst<Visitor, T>
    inline void RowView::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        codec_.template visitConstSparseTyped<T>(startIndex, count, std::forward<Visitor>(visitor), "RowView::visitConst<T>");
    }

    // ========================================================================
    // RowViewStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    RowViewStatic<ColumnTypes...>::RowViewStatic(const RowViewStatic& other)
        : layout_(other.layout_), codec_(other.codec_)
    {
        codec_.setup(layout_);
        codec_.setBuffer(other.buffer());
    }

    template<typename... ColumnTypes>
    RowViewStatic<ColumnTypes...>::RowViewStatic(RowViewStatic&& other) noexcept
        : layout_(std::move(other.layout_)), codec_(std::move(other.codec_))
    {
        codec_.setup(layout_);
    }

    template<typename... ColumnTypes>
    RowViewStatic<ColumnTypes...>& RowViewStatic<ColumnTypes...>::operator=(const RowViewStatic& other) noexcept {
        if (this != &other) {
            layout_ = other.layout_;
            codec_ = other.codec_;
            codec_.setup(layout_);
            codec_.setBuffer(other.buffer());
        }
        return *this;
    }

    template<typename... ColumnTypes>
    RowViewStatic<ColumnTypes...>& RowViewStatic<ColumnTypes...>::operator=(RowViewStatic&& other) noexcept {
        if (this != &other) {
            layout_ = std::move(other.layout_);
            codec_ = std::move(other.codec_);
            codec_.setup(layout_);
        }
        return *this;
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    auto RowViewStatic<ColumnTypes...>::get() const {
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");
        const auto& buffer = codec_.buffer();
        
        if (buffer.data() == nullptr || buffer.size() < WIRE_FIXED_SIZE) [[unlikely]] {
             throw std::runtime_error("RowViewStatic::get<" + std::to_string(Index) + ">: Buffer not set");
        }

        using T = column_type<Index>;
        constexpr size_t off = WIRE_OFFSETS[Index];

        if constexpr (std::is_same_v<T, bool>) {
            constexpr size_t bytePos = off >> 3;
            constexpr size_t bitPos  = off & 7;
            return (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
        } else if constexpr (std::is_same_v<T, std::string>) {
            constexpr size_t strIdx = off;
            size_t lens_cursor = WIRE_BITS_SIZE + WIRE_DATA_SIZE;
            size_t pay_cursor  = WIRE_FIXED_SIZE;
            for (size_t s = 0; s < strIdx; ++s) {
                uint16_t len = unalignedRead<uint16_t>(buffer.data() + lens_cursor);
                lens_cursor += sizeof(uint16_t);
                pay_cursor += len;
            }
            uint16_t len = unalignedRead<uint16_t>(buffer.data() + lens_cursor);
            if (pay_cursor + len > buffer.size()) [[unlikely]] {
                throw std::out_of_range("RowViewStatic::get<" + std::to_string(Index) + ">: String payload out of bounds");
            }
            return std::string_view(reinterpret_cast<const char*>(buffer.data() + pay_cursor), len);
        } else {
            constexpr size_t absOff = WIRE_BITS_SIZE + off;
            return unalignedRead<T>(buffer.data() + absOff);
        }
    }

    template<typename... ColumnTypes>
    template<size_t StartIndex, typename T, size_t Extent>
    void RowViewStatic<ColumnTypes...>::get(std::span<T, Extent>& dst) const {
        static_assert(Extent != std::dynamic_extent, "RowViewStatic: requires fixed-extent span");
        static_assert(StartIndex + Extent <= COLUMN_COUNT, "RowViewStatic: Range exceeds column count");

        const auto& buffer = codec_.buffer();
        if (buffer.empty()) {
            throw std::runtime_error("RowViewStatic::get<" + std::to_string(StartIndex) + ", " + std::to_string(Extent) + ">: Buffer is empty");
        }

        [&]<size_t... I>(std::index_sequence<I...>) {
            static_assert((std::is_assignable_v<T&, column_type<StartIndex + I>> && ...), 
                "RowViewStatic::get: Column types are not assignable to destination span type");
        }(std::make_index_sequence<Extent>{});

        constexpr bool all_types_match = []<size_t... I>(std::index_sequence<I...>) {
            return ((std::is_same_v<T, column_type<StartIndex + I>>) && ...);
        }(std::make_index_sequence<Extent>{});
        
        constexpr bool is_scalar_type = !std::is_same_v<T, bool> && !std::is_same_v<T, std::string>;

        if constexpr (all_types_match && is_scalar_type) {
            constexpr size_t start_offset = WIRE_BITS_SIZE + WIRE_OFFSETS[StartIndex];
            constexpr size_t total_bytes = Extent * sizeof(T);
            
            if (start_offset + total_bytes > buffer.size()) {
                throw std::out_of_range("RowViewStatic::get<" + std::to_string(StartIndex) + ", " + std::to_string(Extent) + ">: " +
                                        "Buffer access out of range (required: " + std::to_string(start_offset + total_bytes) + 
                                        ", available: " + std::to_string(buffer.size()) + ")");
            }
            
            std::memcpy(dst.data(), buffer.data() + start_offset, total_bytes);
        } else {
            [&]<size_t... I>(std::index_sequence<I...>) {
                ((dst[I] = static_cast<T>(this->template get<StartIndex + I>())), ...);
            }(std::make_index_sequence<Extent>{});
        }
    }

    template<typename... ColumnTypes>
    std::span<const std::byte> RowViewStatic<ColumnTypes...>::get(size_t index) const noexcept {
        assert(index < COLUMN_COUNT && "RowViewStatic<ColumnTypes...>::get(index): Index out of bounds");
        
        if (index >= COLUMN_COUNT) 
            return {};
        return codec_.readColumn(index);
    }

    template<typename... ColumnTypes>
    template<typename T, size_t Extent>
    bool RowViewStatic<ColumnTypes...>::get(size_t index, std::span<T, Extent>& dst) const noexcept {
        static_assert(std::is_arithmetic_v<T>, "RowViewStatic::get(span) supports primitive types only (no strings)");
        
        const auto& buffer = codec_.buffer();
        if (buffer.size() < WIRE_FIXED_SIZE) [[unlikely]] return false;

        size_t iMax = index + dst.size();
        if (iMax > COLUMN_COUNT) return false;
        
        auto dstType = toColumnType<T>();
        for (size_t i = index; i < iMax; ++i) {
             if (layout_.columnType(i) != dstType) [[unlikely]] {
                 return false; 
             }
        }

        if constexpr (std::is_same_v<T, bool>) {
            for (size_t i = 0; i < dst.size(); ++i) {
                auto span = get(index + i);
                if (span.empty()) return false;
                dst[i] = unalignedRead<bool>(span.data());
            }
        } else {
            auto span0 = get(index);
            if (span0.empty()) return false;
            const size_t length = sizeof(T) * dst.size();
            const std::byte* start = span0.data();
            if (start + length > buffer.data() + buffer.size()) [[unlikely]] return false;
            memcpy(dst.data(), start, length);
        }
        return true;
    }

    template<typename... ColumnTypes>
    template<typename T>
    bool RowViewStatic<ColumnTypes...>::get(size_t index, T& dst) const noexcept {
        bool success = false;
        
        try {
            visitConst(index, [&](size_t, const auto& value) {
                using ColType = std::decay_t<decltype(value)>;
                
                if constexpr (std::is_same_v<ColType, std::string_view>) {
                    if constexpr (std::is_same_v<T, std::string_view>) {
                        dst = value;
                        success = true;
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        dst = std::string(value);
                        success = true;
                    }
                }
                else if constexpr (std::is_arithmetic_v<ColType> && std::is_arithmetic_v<T>) {
                    dst = static_cast<T>(value);
                    success = true;
                }
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

    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowViewStatic<ColumnTypes...>::set(const T& value) noexcept {
        static_assert(Index < COLUMN_COUNT, "Index out of bounds");
        using ColT = column_type<Index>;

        static_assert(std::is_same_v<ColT, T> && std::is_arithmetic_v<T>, 
            "RowViewStatic::set<I> only supports matching primitive types. Strings not supported.");

        constexpr size_t off = WIRE_OFFSETS[Index];
        auto buffer = codec_.buffer();

        if constexpr (std::is_same_v<T, bool>) {
            constexpr size_t bytePos = off >> 3;
            constexpr size_t bitPos  = off & 7;
            if (bytePos >= buffer.size()) [[unlikely]] return;
            if (value) buffer[bytePos] |= std::byte{1} << bitPos;
            else       buffer[bytePos] &= ~(std::byte{1} << bitPos);
        } else {
            constexpr size_t absOff = WIRE_BITS_SIZE + off;
            if (absOff + sizeof(T) > buffer.size()) [[unlikely]] return;
            std::memcpy(buffer.data() + absOff, &value, sizeof(T));
        }
    }

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

        try {
            visit(index, [&](size_t, auto& colValue, bool&) {
                using ColType = std::decay_t<decltype(colValue)>;
                
                if constexpr (std::is_same_v<ColType, T>) {
                    colValue = value;
                }
            }, 1);
        } catch (...) {
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
        [&]<size_t... I>(std::index_sequence<I...>) {
             (row.template set<I>(this->template get<I>()), ...);
        }(std::make_index_sequence<COLUMN_COUNT>{});
        
        return row;
    }

    template<typename... ColumnTypes>
    bool RowViewStatic<ColumnTypes...>::validate() const noexcept 
    {
        const auto& buffer = codec_.buffer();
        if(buffer.empty()) {
            return false;
        }

        if constexpr (COLUMN_COUNT == 0) {
            return true;
        }

        if (WIRE_FIXED_SIZE > buffer.size()) {
            return false;
        }

        return validateStringPayloads<0>();
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowViewStatic<ColumnTypes...>::validateStringPayloads() const {
        if constexpr (Index >= COLUMN_COUNT) {
            return true;
        } else {
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                try {
                    [[maybe_unused]] auto sv = this->template get<Index>();
                } catch (...) {
                    return false;
                }
            }
            return validateStringPayloads<Index + 1>();
        }
    }

    template<typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowViewStatic<ColumnTypes...>::visitConst(size_t startIndex, Visitor&& visitor, size_t count) const {
        codec_.visitConstSparse(startIndex, count, std::forward<Visitor>(visitor), "RowViewStatic::visitConst");
    }

    template<typename... ColumnTypes>
    template<RowVisitorConst Visitor>
    void RowViewStatic<ColumnTypes...>::visitConst(Visitor&& visitor) const {
        visitConst(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
    }

    template<typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowViewStatic<ColumnTypes...>::visit(size_t startIndex, Visitor&& visitor, size_t count) {
        codec_.visitSparse(startIndex, count, std::forward<Visitor>(visitor), "RowViewStatic::visit");
    }

    template<typename... ColumnTypes>
    template<RowVisitor Visitor>
    void RowViewStatic<ColumnTypes...>::visit(Visitor&& visitor) {
        visit(0, std::forward<Visitor>(visitor), COLUMN_COUNT);
    }

} // namespace bcsv
