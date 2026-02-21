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
 * @file row_codec_flat001.hpp
 * @brief RowCodecFlat001 implementation — flat binary wire format (version 001).
 *
 * Wire layout: [bits_][data_][strg_lengths][strg_data]
 */

#include "row_codec_flat001.h"
#include "row.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Primary template: RowCodecFlat001<Layout, Policy> — dynamic layout
// ════════════════════════════════════════════════════════════════════════════

// ── Lifecycle ────────────────────────────────────────────────────────────────

template<typename LayoutType, TrackingPolicy Policy>
inline void RowCodecFlat001<LayoutType, Policy>::setup(const LayoutType& layout)
{
    layout_ = &layout;
    wire_data_size_ = 0;
    packed_offsets_ = &layout.columnOffsetsPacked();
    
    const size_t count = layout.columnCount();
    const auto& types = layout.columnTypes();
    const auto& packed_offsets = *packed_offsets_;
    for (size_t i = count; i-- > 0;) {
        const ColumnType type = types[i];
        if (type == ColumnType::BOOL || type == ColumnType::STRING) {
            continue;
        }
        wire_data_size_ = packed_offsets[i] + static_cast<uint32_t>(sizeOf(type));
        break;
    }
    offsets_ptr_ = packed_offsets.data();
}


// ── Bulk serialize (Row → wire bytes) ────────────────────────────────────────

template<typename LayoutType, TrackingPolicy Policy>
inline std::span<std::byte> RowCodecFlat001<LayoutType, Policy>::serialize(
    const RowType& row, ByteBuffer& buffer) const
{
    assert(layout_ && "RowCodecFlat001::serialize() called before setup()");

    const size_t   off_row  = buffer.size();
    const uint32_t bits_sz  = wireBitsSize();
    const uint32_t data_sz  = wire_data_size_;
    const uint32_t fixed_sz = wireFixedSize();
    const size_t   count    = layout_->columnCount();

    // Local pointers — one indirection per vector, amortised over the loop
    const ColumnType* types   = layout_->columnTypes().data();
    const uint32_t*   offsets = layout_->columnOffsets().data();

    // ── Pre-scan: sum string payload sizes for a single buffer.resize() ──
    size_t strg_payload = 0;
    for (size_t i = 0; i < count; ++i) {
        if (types[i] == ColumnType::STRING) {
            strg_payload += row.strg_[offsets[i]].size();
        }
    }

    // Resize buffer once for the entire row
    buffer.resize(off_row + fixed_sz + strg_payload);

    // Clear bits section (padding bits must be zero)
    if (bits_sz > 0) {
        if constexpr (!isTrackingEnabled(Policy)) {
            // Non-tracking: bits_ is already sequentially packed, bulk copy
            std::memcpy(&buffer[off_row], row.bits_.data(), bits_sz);
        } else {
            std::memset(&buffer[off_row], 0, bits_sz);
        }
    }

    // ── Single-pass: serialize all sections in one column loop ─────
    size_t bool_idx = 0;
    size_t wire_off = off_row + bits_sz;
    size_t len_off  = off_row + bits_sz + data_sz;
    size_t pay_off  = off_row + fixed_sz;

    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        const uint32_t   off  = offsets[i];

        if (type == ColumnType::BOOL) {
            if constexpr (isTrackingEnabled(Policy)) {
                if (row.bits_[i]) {
                    buffer[off_row + (bool_idx >> 3)] |= std::byte{1} << (bool_idx & 7);
                }
            }
            ++bool_idx;
        } else if (type == ColumnType::STRING) {
            const std::string& str = row.strg_[off];
            const uint16_t len = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
            std::memcpy(&buffer[len_off], &len, sizeof(uint16_t));
            len_off += sizeof(uint16_t);
            if (len > 0) {
                std::memcpy(&buffer[pay_off], str.data(), len);
                pay_off += len;
            }
        } else {
            const size_t len = sizeOf(type);
            std::memcpy(&buffer[wire_off], &row.data_[off], len);
            wire_off += len;
        }
    }

    return {&buffer[off_row], buffer.size() - off_row};
}


// ── Bulk deserialize (wire bytes → Row) ──────────────────────────────────────

template<typename LayoutType, TrackingPolicy Policy>
inline void RowCodecFlat001<LayoutType, Policy>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecFlat001::deserialize() called before setup()");

    const uint32_t bits_sz  = wireBitsSize();
    const uint32_t data_sz  = wire_data_size_;
    const uint32_t fixed_sz = wireFixedSize();
    const size_t   count    = layout_->columnCount();

    if (fixed_sz > buffer.size()) [[unlikely]] {
        throw std::runtime_error("RowCodecFlat001::deserialize() failed: buffer too short");
    }

    const ColumnType* types   = layout_->columnTypes().data();
    const uint32_t*   offsets = layout_->columnOffsets().data();

    if constexpr (!isTrackingEnabled(Policy)) {
        if (bits_sz > 0) {
            std::memcpy(row.bits_.data(), &buffer[0], bits_sz);
        }
    }

    size_t bool_idx = 0;
    size_t wire_off = bits_sz;
    size_t len_off  = bits_sz + data_sz;
    size_t pay_off  = fixed_sz;

    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        const uint32_t   off  = offsets[i];

        if (type == ColumnType::BOOL) {
            if constexpr (isTrackingEnabled(Policy)) {
                bool val = (buffer[bool_idx >> 3] & (std::byte{1} << (bool_idx & 7))) != std::byte{0};
                row.bits_[i] = val;
            }
            ++bool_idx;
        } else if (type == ColumnType::STRING) {
            uint16_t len = 0;
            std::memcpy(&len, &buffer[len_off], sizeof(uint16_t));
            len_off += sizeof(uint16_t);

            if (pay_off + len > buffer.size()) [[unlikely]] {
                throw std::runtime_error("RowCodecFlat001::deserialize() string payload overflow");
            }

            std::string& str = row.strg_[off];
            if (len > 0) {
                str.assign(reinterpret_cast<const char*>(&buffer[pay_off]), len);
                pay_off += len;
            } else {
                str.clear();
            }
        } else {
            const size_t len = sizeOf(type);
            std::memcpy(&row.data_[off], &buffer[wire_off], len);
            wire_off += len;
        }
    }

    // When tracking is enabled, mark all non-BOOL columns as changed.
    // Flat format carries full row data — every column is "changed".
    if constexpr (isTrackingEnabled(Policy)) {
        row.trackingSetAllChanged();
    }
}


// ── Per-column read (RowView — sparse/lazy path) ────────────────────────────

template<typename LayoutType, TrackingPolicy Policy>
inline std::span<const std::byte> RowCodecFlat001<LayoutType, Policy>::readColumn(
    std::span<const std::byte> buffer, size_t col,
    bool& boolScratch) const
{
    assert(layout_ && "RowCodecFlat001::readColumn() called before setup()");
    assert(col < layout_->columnCount());

    const uint32_t fixed_sz = wireFixedSize();
    if (buffer.empty() || buffer.size() < fixed_sz) [[unlikely]] {
        return {};
    }

    const ColumnType type = layout_->columnType(col);
    const uint32_t off = offsets_ptr_[col];

    if (type == ColumnType::BOOL) {
        size_t bytePos = off >> 3;
        size_t bitPos  = off & 7;
        boolScratch = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
        return { reinterpret_cast<const std::byte*>(&boolScratch), sizeof(bool) };
    } else if (type == ColumnType::STRING) {
        // Scan strg_lengths to find payload offset for string index 'off'
        size_t lens_cursor = wireBitsSize() + wire_data_size_;
        size_t pay_cursor  = fixed_sz;
        for (uint32_t s = 0; s < off; ++s) {
            uint16_t len = unalignedRead<uint16_t>(&buffer[lens_cursor]);
            lens_cursor += sizeof(uint16_t);
            pay_cursor += len;
            if (pay_cursor > buffer.size()) [[unlikely]] return {};
        }
        uint16_t len = unalignedRead<uint16_t>(&buffer[lens_cursor]);
        if (pay_cursor + len > buffer.size()) [[unlikely]] return {};
        return { buffer.data() + pay_cursor, len };
    } else {
        // Scalar: absolute position = wire_bits_size_ + section-relative offset
        size_t absOff = wireBitsSize() + off;
        size_t fieldLen = sizeOf(type);
        return { buffer.data() + absOff, fieldLen };
    }
}

template<typename LayoutType, TrackingPolicy Policy>
inline void RowCodecFlat001<LayoutType, Policy>::validateSparseRange(
    std::span<const std::byte> buffer,
    size_t startIndex,
    size_t count,
    const char* fnName) const
{
    assert(layout_ && "RowCodecFlat001::validateSparseRange() called before setup()");

    if (count == 0) {
        return;
    }

    const size_t endIndex = startIndex + count;
    if constexpr (RANGE_CHECKING) {
        if (endIndex > layout_->columnCount()) {
            throw std::out_of_range(std::string(fnName) + ": Range [" + std::to_string(startIndex) +
                ", " + std::to_string(endIndex) + ") exceeds column count " +
                std::to_string(layout_->columnCount()));
        }
    } else {
        assert(endIndex <= layout_->columnCount() && "RowView sparse range out of bounds");
    }

    if (buffer.size() < wireFixedSize()) [[unlikely]] {
        throw std::runtime_error(std::string(fnName) + "() buffer too small for fixed wire section");
    }
}

template<typename LayoutType, TrackingPolicy Policy>
template<typename T>
inline void RowCodecFlat001<LayoutType, Policy>::validateSparseTypedRange(
    std::span<const std::byte> buffer,
    size_t startIndex,
    size_t count,
    const char* fnName) const
{
    validateSparseRange(buffer, startIndex, count, fnName);
    if (count == 0) {
        return;
    }

    constexpr ColumnType expectedType = toColumnType<T>();
    const ColumnType* types = layout_->columnTypes().data();
    const size_t endIndex = startIndex + count;
    for (size_t i = startIndex; i < endIndex; ++i) {
        if (types[i] != expectedType) [[unlikely]] {
            throw std::runtime_error(std::string(fnName) + ": Type mismatch at column " + std::to_string(i) +
                ". Expected " + std::string(toString(expectedType)) +
                ", actual " + std::string(toString(types[i])));
        }
    }
}

template<typename LayoutType, TrackingPolicy Policy>
inline void RowCodecFlat001<LayoutType, Policy>::initializeSparseStringCursors(
    std::span<const std::byte> buffer,
    size_t startIndex,
    size_t& strLensCursor,
    size_t& strPayCursor,
    const char* fnName) const
{
    assert(layout_ && "RowCodecFlat001::initializeSparseStringCursors() called before setup()");

    strLensCursor = wireBitsSize() + wireDataSize();
    strPayCursor  = wireFixedSize();

    const ColumnType* types = layout_->columnTypes().data();
    for (size_t j = 0; j < startIndex; ++j) {
        if (types[j] == ColumnType::STRING) {
            const uint16_t len = unalignedRead<uint16_t>(&buffer[strLensCursor]);
            strLensCursor += sizeof(uint16_t);
            strPayCursor += len;
            if (strPayCursor > buffer.size()) [[unlikely]] {
                throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
            }
        }
    }
}

template<typename LayoutType, TrackingPolicy Policy>
template<typename Visitor>
inline void RowCodecFlat001<LayoutType, Policy>::visitConstSparse(
    size_t startIndex,
    size_t count,
    Visitor&& visitor,
    const char* fnName) const
{
    if (count == 0) {
        return;
    }

    const auto buffer = std::span<const std::byte>(buffer_.data(), buffer_.size());
    validateSparseRange(buffer, startIndex, count, fnName);
    const size_t endIndex = startIndex + count;

    const ColumnType* types = layout_->columnTypes().data();
    const uint32_t wire_data_off = wireBitsSize();
    size_t str_lens_cursor = 0;
    size_t str_pay_cursor  = 0;
    initializeSparseStringCursors(buffer, startIndex, str_lens_cursor, str_pay_cursor, fnName);

    for (size_t i = startIndex; i < endIndex; ++i) {
        const ColumnType type = types[i];
        const uint32_t off = offsets_ptr_[i];

        switch(type) {
            case ColumnType::BOOL: {
                const size_t bytePos = off >> 3;
                const size_t bitPos  = off & 7;
                const bool value = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
                visitor(i, value);
                break;
            }
            case ColumnType::INT8: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int8_t value;
                std::memcpy(&value, ptr, sizeof(int8_t));
                visitor(i, value);
                break;
            }
            case ColumnType::INT16: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int16_t value;
                std::memcpy(&value, ptr, sizeof(int16_t));
                visitor(i, value);
                break;
            }
            case ColumnType::INT32: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int32_t value;
                std::memcpy(&value, ptr, sizeof(int32_t));
                visitor(i, value);
                break;
            }
            case ColumnType::INT64: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int64_t value;
                std::memcpy(&value, ptr, sizeof(int64_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT8: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint8_t value;
                std::memcpy(&value, ptr, sizeof(uint8_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT16: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint16_t value;
                std::memcpy(&value, ptr, sizeof(uint16_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT32: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint32_t value;
                std::memcpy(&value, ptr, sizeof(uint32_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT64: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint64_t value;
                std::memcpy(&value, ptr, sizeof(uint64_t));
                visitor(i, value);
                break;
            }
            case ColumnType::FLOAT: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                float value;
                std::memcpy(&value, ptr, sizeof(float));
                visitor(i, value);
                break;
            }
            case ColumnType::DOUBLE: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                double value;
                std::memcpy(&value, ptr, sizeof(double));
                visitor(i, value);
                break;
            }
            case ColumnType::STRING: {
                const uint16_t strLength = unalignedRead<uint16_t>(&buffer[str_lens_cursor]);
                str_lens_cursor += sizeof(uint16_t);

                if (str_pay_cursor + strLength > buffer.size()) [[unlikely]] {
                    throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
                }

                const std::string_view strValue(
                    reinterpret_cast<const char*>(&buffer[str_pay_cursor]),
                    strLength
                );
                str_pay_cursor += strLength;
                visitor(i, strValue);
                break;
            }
            default: [[unlikely]]
                throw std::runtime_error(std::string(fnName) + "() unsupported column type");
        }
    }
}

template<typename LayoutType, TrackingPolicy Policy>
template<typename Visitor>
inline void RowCodecFlat001<LayoutType, Policy>::visitSparse(
    size_t startIndex,
    size_t count,
    Visitor&& visitor,
    const char* fnName)
{
    if (count == 0) {
        return;
    }

    auto& buffer = buffer_;
    validateSparseRange(std::span<const std::byte>(buffer.data(), buffer.size()), startIndex, count, fnName);
    const size_t endIndex = startIndex + count;

    const ColumnType* types = layout_->columnTypes().data();
    const uint32_t wire_data_off = wireBitsSize();
    size_t str_lens_cursor = 0;
    size_t str_pay_cursor  = 0;
    initializeSparseStringCursors(std::span<const std::byte>(buffer.data(), buffer.size()), startIndex, str_lens_cursor, str_pay_cursor, fnName);

    for (size_t i = startIndex; i < endIndex; ++i) {
        const ColumnType type = types[i];
        const uint32_t off = offsets_ptr_[i];

        auto dispatch = [&](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                bool changed = true;
                visitor(i, value, changed);
                return changed;
            } else {
                static_assert(std::is_invocable_v<Visitor, size_t, T&>,
                              "RowView::visit() requires (size_t, T&) or (size_t, T&, bool&)");
                visitor(i, value);
                return true;
            }
        };

        auto visitScalar = [&](auto type_tag) {
            using Scalar = decltype(type_tag);
            const size_t pos = wire_data_off + off;
            Scalar value = unalignedRead<Scalar>(buffer.data() + pos);
            const bool changed = dispatch(value);
            if (changed) {
                unalignedWrite<Scalar>(buffer.data() + pos, value);
            }
        };

        switch(type) {
            case ColumnType::BOOL: {
                const size_t bytePos = off >> 3;
                const size_t bitPos  = off & 7;
                bool value = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
                const bool changed = dispatch(value);
                if (changed) {
                    if (value) buffer[bytePos] |= std::byte{1} << bitPos;
                    else       buffer[bytePos] &= ~(std::byte{1} << bitPos);
                }
                break;
            }
            case ColumnType::INT8:
                visitScalar(int8_t{});
                break;
            case ColumnType::INT16:
                visitScalar(int16_t{});
                break;
            case ColumnType::INT32:
                visitScalar(int32_t{});
                break;
            case ColumnType::INT64:
                visitScalar(int64_t{});
                break;
            case ColumnType::UINT8:
                visitScalar(uint8_t{});
                break;
            case ColumnType::UINT16:
                visitScalar(uint16_t{});
                break;
            case ColumnType::UINT32:
                visitScalar(uint32_t{});
                break;
            case ColumnType::UINT64:
                visitScalar(uint64_t{});
                break;
            case ColumnType::FLOAT:
                visitScalar(float{});
                break;
            case ColumnType::DOUBLE:
                visitScalar(double{});
                break;
            case ColumnType::STRING: {
                const uint16_t strLength = unalignedRead<uint16_t>(&buffer[str_lens_cursor]);
                str_lens_cursor += sizeof(uint16_t);

                if (str_pay_cursor + strLength > buffer.size()) [[unlikely]] {
                    throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
                }

                std::string_view strValue(
                    reinterpret_cast<const char*>(&buffer[str_pay_cursor]),
                    strLength
                );
                str_pay_cursor += strLength;

                dispatch(strValue);
                break;
            }
            default: [[unlikely]]
                throw std::runtime_error(std::string(fnName) + "() unsupported column type");
        }
    }
}

template<typename LayoutType, TrackingPolicy Policy>
template<typename T, typename Visitor>
inline void RowCodecFlat001<LayoutType, Policy>::visitSparseTyped(
    size_t startIndex,
    size_t count,
    Visitor&& visitor,
    const char* fnName)
{
    if (count == 0) {
        return;
    }

    auto& buffer = buffer_;
    validateSparseTypedRange<T>(std::span<const std::byte>(buffer.data(), buffer.size()), startIndex, count, fnName);
    const size_t endIndex = startIndex + count;

    const uint32_t wire_data_off = wireBitsSize();
    size_t str_lens_cursor = 0;
    size_t str_pay_cursor  = 0;
    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
        initializeSparseStringCursors(std::span<const std::byte>(buffer.data(), buffer.size()), startIndex, str_lens_cursor, str_pay_cursor, fnName);
    }

    for (size_t i = startIndex; i < endIndex; ++i) {
        const uint32_t off = offsets_ptr_[i];

        if constexpr (std::is_same_v<T, bool>) {
            const size_t bytePos = off >> 3;
            const size_t bitPos  = off & 7;
            bool value = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
            bool changed = true;
            if constexpr (std::is_invocable_v<Visitor, size_t, bool&, bool&>) {
                visitor(i, value, changed);
            } else {
                visitor(i, value);
            }
            if (changed) {
                if (value) buffer[bytePos] |= std::byte{1} << bitPos;
                else       buffer[bytePos] &= ~(std::byte{1} << bitPos);
            }
        } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
            const uint16_t strLength = unalignedRead<uint16_t>(&buffer[str_lens_cursor]);
            str_lens_cursor += sizeof(uint16_t);

            if (str_pay_cursor + strLength > buffer.size()) [[unlikely]] {
                throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
            }

            std::string_view sv(
                reinterpret_cast<const char*>(&buffer[str_pay_cursor]),
                strLength
            );
            str_pay_cursor += strLength;

            if constexpr (std::is_invocable_v<Visitor, size_t, std::string_view&, bool&>) {
                bool changed = true;
                visitor(i, sv, changed);
            } else if constexpr (std::is_invocable_v<Visitor, size_t, std::string_view&>) {
                visitor(i, sv);
            } else if constexpr (std::is_invocable_v<Visitor, size_t, std::string&, bool&>) {
                std::string tmp(sv);
                bool changed = true;
                visitor(i, tmp, changed);
            } else {
                std::string tmp(sv);
                visitor(i, tmp);
            }
        } else {
            const size_t pos = wire_data_off + off;
            T value = unalignedRead<T>(buffer.data() + pos);
            bool changed = true;
            if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                visitor(i, value, changed);
            } else {
                visitor(i, value);
            }
            if (changed) {
                unalignedWrite<T>(buffer.data() + pos, value);
            }
        }
    }
}

template<typename LayoutType, TrackingPolicy Policy>
template<typename T, typename Visitor>
inline void RowCodecFlat001<LayoutType, Policy>::visitConstSparseTyped(
    size_t startIndex,
    size_t count,
    Visitor&& visitor,
    const char* fnName) const
{
    if (count == 0) {
        return;
    }

    const auto buffer = std::span<const std::byte>(buffer_.data(), buffer_.size());
    validateSparseTypedRange<T>(buffer, startIndex, count, fnName);
    const size_t endIndex = startIndex + count;

    const uint32_t wire_data_off = wireBitsSize();
    size_t str_lens_cursor = 0;
    size_t str_pay_cursor  = 0;
    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
        initializeSparseStringCursors(buffer, startIndex, str_lens_cursor, str_pay_cursor, fnName);
    }

    for (size_t i = startIndex; i < endIndex; ++i) {
        const uint32_t off = offsets_ptr_[i];

        if constexpr (std::is_same_v<T, bool>) {
            const size_t bytePos = off >> 3;
            const size_t bitPos  = off & 7;
            const bool value = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
            visitor(i, value);
        } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
            const uint16_t strLength = unalignedRead<uint16_t>(&buffer[str_lens_cursor]);
            str_lens_cursor += sizeof(uint16_t);

            if (str_pay_cursor + strLength > buffer.size()) [[unlikely]] {
                throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
            }

            const std::string_view sv(
                reinterpret_cast<const char*>(&buffer[str_pay_cursor]),
                strLength
            );
            str_pay_cursor += strLength;

            if constexpr (std::is_same_v<T, std::string_view>) {
                visitor(i, sv);
            } else {
                const std::string tmp(sv);
                visitor(i, tmp);
            }
        } else {
            const std::byte* ptr = &buffer[wire_data_off + off];
            T value = unalignedRead<T>(ptr);
            visitor(i, value);
        }
    }
}


// ════════════════════════════════════════════════════════════════════════════
// Partial specialization: RowCodecFlat001<LayoutStatic<Ts...>, Policy>
// ════════════════════════════════════════════════════════════════════════════

// ── Bulk serialize (static Row → wire bytes) ─────────────────────────────────

template<TrackingPolicy Policy, typename... ColumnTypes>
inline std::span<std::byte> RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::serialize(
    const RowType& row, ByteBuffer& buffer) const
{
    assert(layout_ && "RowCodecFlat001::serialize() called before setup()");

    const size_t offRow = buffer.size();

    // Compute total string payload via fold expression
    size_t strg_payload = 0;
    [&]<size_t... I>(std::index_sequence<I...>) {
        (([&] {
            if constexpr (std::is_same_v<typename RowType::template column_type<I>, std::string>) {
                strg_payload += std::get<I>(row.data_).size();
            }
        }()), ...);
    }(std::index_sequence_for<ColumnTypes...>{});

    buffer.resize(offRow + WIRE_FIXED_SIZE + strg_payload);

    // Zero bits section
    if constexpr (WIRE_BITS_SIZE > 0) {
        std::memset(&buffer[offRow], 0, WIRE_BITS_SIZE);
    }

    // Serialize each column using compile-time recursion
    size_t boolIdx = 0;
    size_t dataOff = offRow + WIRE_BITS_SIZE;
    size_t lenOff  = offRow + WIRE_BITS_SIZE + WIRE_DATA_SIZE;
    size_t payOff  = offRow + WIRE_FIXED_SIZE;
    serializeElements<0>(row, buffer, offRow, boolIdx, dataOff, lenOff, payOff);

    return {&buffer[offRow], buffer.size() - offRow};
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<size_t Index>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::serializeElements(
    const RowType& row, ByteBuffer& buffer, size_t offRow,
    size_t& boolIdx, size_t& dataOff, size_t& lenOff, size_t& payOff) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;
        if constexpr (std::is_same_v<T, bool>) {
            if (std::get<Index>(row.data_)) {
                buffer[offRow + (boolIdx >> 3)] |= std::byte{1} << (boolIdx & 7);
            }
            ++boolIdx;
        } else if constexpr (std::is_same_v<T, std::string>) {
            const std::string& str = std::get<Index>(row.data_);
            const uint16_t len = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
            if (lenOff + sizeof(uint16_t) > buffer.size()) [[unlikely]] {
                throw std::runtime_error("RowCodecFlat001::serialize() string length overflow");
            }
            std::memcpy(&buffer[lenOff], &len, sizeof(uint16_t));
            lenOff += sizeof(uint16_t);
            if (len > 0) {
                if (payOff + len > buffer.size()) [[unlikely]] {
                    throw std::runtime_error("RowCodecFlat001::serialize() string payload overflow");
                }
                std::memcpy(&buffer[payOff], str.data(), len);
                payOff += len;
            }
        } else {
            if (dataOff + sizeof(T) > buffer.size()) [[unlikely]] {
                throw std::runtime_error("RowCodecFlat001::serialize() scalar payload overflow");
            }
            std::memcpy(&buffer[dataOff], &std::get<Index>(row.data_), sizeof(T));
            dataOff += sizeof(T);
        }
        serializeElements<Index + 1>(row, buffer, offRow, boolIdx, dataOff, lenOff, payOff);
    }
}


// ── Bulk deserialize (wire bytes → static Row) ──────────────────────────────

template<TrackingPolicy Policy, typename... ColumnTypes>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecFlat001::deserialize() called before setup()");

    if (WIRE_FIXED_SIZE > buffer.size()) [[unlikely]] {
        throw std::runtime_error("RowCodecFlat001::deserialize() failed: buffer too short for fixed section");
    }
    size_t boolIdx = 0;
    size_t dataOff = WIRE_BITS_SIZE;
    size_t lenOff  = WIRE_BITS_SIZE + WIRE_DATA_SIZE;
    size_t payOff  = WIRE_FIXED_SIZE;
    deserializeElements<0>(buffer, row, boolIdx, dataOff, lenOff, payOff);

    // When tracking is enabled, mark all non-BOOL columns as changed.
    // Flat format carries full row data — every column is "changed".
    if constexpr (isTrackingEnabled(Policy)) {
        row.trackingSetAllChanged();
    }
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<size_t Index>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::deserializeElements(
    const std::span<const std::byte>& srcBuffer, RowType& row,
    size_t& boolIdx, size_t& dataOff, size_t& lenOff, size_t& payOff) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;
        if constexpr (std::is_same_v<T, bool>) {
            std::get<Index>(row.data_) = (srcBuffer[boolIdx >> 3] & (std::byte{1} << (boolIdx & 7))) != std::byte{0};
            ++boolIdx;
        } else if constexpr (std::is_same_v<T, std::string>) {
            uint16_t len = 0;
            std::memcpy(&len, &srcBuffer[lenOff], sizeof(uint16_t));
            lenOff += sizeof(uint16_t);
            if (payOff + len > srcBuffer.size()) [[unlikely]] {
                throw std::runtime_error("RowCodecFlat001::deserialize() string payload overflow");
            }
            if (len > 0) {
                std::get<Index>(row.data_).assign(reinterpret_cast<const char*>(&srcBuffer[payOff]), len);
                payOff += len;
            } else {
                std::get<Index>(row.data_).clear();
            }
        } else {
            if (dataOff + sizeof(T) > srcBuffer.size()) [[unlikely]] {
                throw std::runtime_error("RowCodecFlat001::deserialize() buffer overflow");
            }
            std::memcpy(&std::get<Index>(row.data_), &srcBuffer[dataOff], sizeof(T));
            dataOff += sizeof(T);
        }
        deserializeElements<Index + 1>(srcBuffer, row, boolIdx, dataOff, lenOff, payOff);
    }
}


// ── Per-column read (static RowView — sparse/lazy path) ──────────────────────

template<TrackingPolicy Policy, typename... ColumnTypes>
inline std::span<const std::byte> RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::readColumn(
    std::span<const std::byte> buffer, size_t col,
    bool& boolScratch) const
{
    assert(col < COLUMN_COUNT);

    if (buffer.empty() || buffer.size() < WIRE_FIXED_SIZE) [[unlikely]] {
        return {};
    }

    using ReadColumnFn = std::span<const std::byte> (*)(std::span<const std::byte>, bool&);
    static constexpr auto handlers = []<size_t... I>(std::index_sequence<I...>) {
        return std::array<ReadColumnFn, COLUMN_COUNT>{
            +[](std::span<const std::byte> src, bool& scratch) -> std::span<const std::byte> {
                using T = std::tuple_element_t<I, std::tuple<ColumnTypes...>>;
                constexpr size_t off = WIRE_OFFSETS[I];

                if constexpr (std::is_same_v<T, bool>) {
                    constexpr size_t bytePos = off >> 3;
                    constexpr size_t bitPos  = off & 7;
                    scratch = (src[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
                    return { reinterpret_cast<const std::byte*>(&scratch), sizeof(bool) };
                } else if constexpr (std::is_same_v<T, std::string>) {
                    constexpr size_t strIdx = off;
                    size_t lens_cursor = WIRE_BITS_SIZE + WIRE_DATA_SIZE;
                    size_t pay_cursor  = WIRE_FIXED_SIZE;
                    for (size_t s = 0; s < strIdx; ++s) {
                        uint16_t len = unalignedRead<uint16_t>(&src[lens_cursor]);
                        lens_cursor += sizeof(uint16_t);
                        pay_cursor += len;
                        if (pay_cursor > src.size()) [[unlikely]] return {};
                    }
                    uint16_t len = unalignedRead<uint16_t>(&src[lens_cursor]);
                    if (pay_cursor + len > src.size()) [[unlikely]] return {};
                    return { src.data() + pay_cursor, len };
                } else {
                    constexpr size_t absOff = WIRE_BITS_SIZE + off;
                    return { src.data() + absOff, sizeof(T) };
                }
            }...
        };
    }(std::make_index_sequence<COLUMN_COUNT>{});

    return handlers[col](buffer, boolScratch);
}

template<TrackingPolicy Policy, typename... ColumnTypes>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::validateSparseRange(
    std::span<const std::byte> buffer,
    size_t startIndex,
    size_t count,
    const char* fnName) const
{
    assert(layout_ && "RowCodecFlat001::validateSparseRange() called before setup()");

    if (count == 0) {
        return;
    }

    const size_t endIndex = startIndex + count;
    if constexpr (RANGE_CHECKING) {
        if (endIndex > COLUMN_COUNT) {
            throw std::out_of_range(std::string(fnName) + ": Range [" + std::to_string(startIndex) +
                ", " + std::to_string(endIndex) + ") exceeds column count " +
                std::to_string(COLUMN_COUNT));
        }
    } else {
        assert(endIndex <= COLUMN_COUNT && "RowViewStatic sparse range out of bounds");
    }

    if (buffer.size() < wireFixedSize()) [[unlikely]] {
        throw std::runtime_error(std::string(fnName) + "() buffer too small for fixed wire section");
    }
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<typename T>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::validateSparseTypedRange(
    std::span<const std::byte> buffer,
    size_t startIndex,
    size_t count,
    const char* fnName) const
{
    validateSparseRange(buffer, startIndex, count, fnName);
    if (count == 0) {
        return;
    }

    constexpr ColumnType expectedType = toColumnType<T>();
    const ColumnType* types = layout_->columnTypes().data();
    const size_t endIndex = startIndex + count;
    for (size_t i = startIndex; i < endIndex; ++i) {
        if (types[i] != expectedType) [[unlikely]] {
            throw std::runtime_error(std::string(fnName) + ": Type mismatch at column " + std::to_string(i) +
                ". Expected " + std::string(toString(expectedType)) +
                ", actual " + std::string(toString(types[i])));
        }
    }
}

template<TrackingPolicy Policy, typename... ColumnTypes>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::initializeSparseStringCursors(
    std::span<const std::byte> buffer,
    size_t startIndex,
    size_t& strLensCursor,
    size_t& strPayCursor,
    const char* fnName) const
{
    assert(layout_ && "RowCodecFlat001::initializeSparseStringCursors() called before setup()");

    strLensCursor = wireBitsSize() + wireDataSize();
    strPayCursor  = wireFixedSize();

    const ColumnType* types = layout_->columnTypes().data();
    for (size_t j = 0; j < startIndex; ++j) {
        if (types[j] == ColumnType::STRING) {
            const uint16_t len = unalignedRead<uint16_t>(&buffer[strLensCursor]);
            strLensCursor += sizeof(uint16_t);
            strPayCursor += len;
            if (strPayCursor > buffer.size()) [[unlikely]] {
                throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
            }
        }
    }
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<typename Visitor>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::visitConstSparse(
    size_t startIndex,
    size_t count,
    Visitor&& visitor,
    const char* fnName) const
{
    if (count == 0) {
        return;
    }

    const auto buffer = std::span<const std::byte>(buffer_.data(), buffer_.size());
    validateSparseRange(buffer, startIndex, count, fnName);
    const size_t endIndex = startIndex + count;

    const ColumnType* types = layout_->columnTypes().data();
    const uint32_t wire_data_off = wireBitsSize();
    size_t str_lens_cursor = 0;
    size_t str_pay_cursor  = 0;
    initializeSparseStringCursors(buffer, startIndex, str_lens_cursor, str_pay_cursor, fnName);

    for (size_t i = startIndex; i < endIndex; ++i) {
        const ColumnType type = types[i];
        const uint32_t off = offsets_ptr_[i];

        switch(type) {
            case ColumnType::BOOL: {
                const size_t bytePos = off >> 3;
                const size_t bitPos  = off & 7;
                const bool value = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
                visitor(i, value);
                break;
            }
            case ColumnType::INT8: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int8_t value;
                std::memcpy(&value, ptr, sizeof(int8_t));
                visitor(i, value);
                break;
            }
            case ColumnType::INT16: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int16_t value;
                std::memcpy(&value, ptr, sizeof(int16_t));
                visitor(i, value);
                break;
            }
            case ColumnType::INT32: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int32_t value;
                std::memcpy(&value, ptr, sizeof(int32_t));
                visitor(i, value);
                break;
            }
            case ColumnType::INT64: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                int64_t value;
                std::memcpy(&value, ptr, sizeof(int64_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT8: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint8_t value;
                std::memcpy(&value, ptr, sizeof(uint8_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT16: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint16_t value;
                std::memcpy(&value, ptr, sizeof(uint16_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT32: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint32_t value;
                std::memcpy(&value, ptr, sizeof(uint32_t));
                visitor(i, value);
                break;
            }
            case ColumnType::UINT64: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                uint64_t value;
                std::memcpy(&value, ptr, sizeof(uint64_t));
                visitor(i, value);
                break;
            }
            case ColumnType::FLOAT: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                float value;
                std::memcpy(&value, ptr, sizeof(float));
                visitor(i, value);
                break;
            }
            case ColumnType::DOUBLE: {
                const std::byte* ptr = &buffer[wire_data_off + off];
                double value;
                std::memcpy(&value, ptr, sizeof(double));
                visitor(i, value);
                break;
            }
            case ColumnType::STRING: {
                const uint16_t strLength = unalignedRead<uint16_t>(&buffer[str_lens_cursor]);
                str_lens_cursor += sizeof(uint16_t);

                if (str_pay_cursor + strLength > buffer.size()) [[unlikely]] {
                    throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
                }

                const std::string_view strValue(
                    reinterpret_cast<const char*>(&buffer[str_pay_cursor]),
                    strLength
                );
                str_pay_cursor += strLength;
                visitor(i, strValue);
                break;
            }
            default: [[unlikely]]
                throw std::runtime_error(std::string(fnName) + "() unsupported column type");
        }
    }
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<typename Visitor>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy>::visitSparse(
    size_t startIndex,
    size_t count,
    Visitor&& visitor,
    const char* fnName)
{
    if (count == 0) {
        return;
    }

    auto& buffer = buffer_;
    validateSparseRange(std::span<const std::byte>(buffer.data(), buffer.size()), startIndex, count, fnName);
    const size_t endIndex = startIndex + count;

    const ColumnType* types = layout_->columnTypes().data();
    const uint32_t wire_data_off = wireBitsSize();
    size_t str_lens_cursor = 0;
    size_t str_pay_cursor  = 0;
    initializeSparseStringCursors(std::span<const std::byte>(buffer.data(), buffer.size()), startIndex, str_lens_cursor, str_pay_cursor, fnName);

    for (size_t i = startIndex; i < endIndex; ++i) {
        const ColumnType type = types[i];
        const uint32_t off = offsets_ptr_[i];

        auto dispatch = [&](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_invocable_v<Visitor, size_t, T&, bool&>) {
                bool changed = true;
                visitor(i, value, changed);
                return changed;
            } else {
                static_assert(std::is_invocable_v<Visitor, size_t, T&>,
                              "RowViewStatic::visit() requires (size_t, T&) or (size_t, T&, bool&)");
                visitor(i, value);
                return true;
            }
        };

        auto visitScalar = [&](auto type_tag) {
            using Scalar = decltype(type_tag);
            const size_t pos = wire_data_off + off;
            Scalar value = unalignedRead<Scalar>(buffer.data() + pos);
            const bool changed = dispatch(value);
            if (changed) {
                unalignedWrite<Scalar>(buffer.data() + pos, value);
            }
        };

        switch(type) {
            case ColumnType::BOOL: {
                const size_t bytePos = off >> 3;
                const size_t bitPos  = off & 7;
                bool value = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
                const bool changed = dispatch(value);
                if (changed) {
                    if (value) buffer[bytePos] |= std::byte{1} << bitPos;
                    else       buffer[bytePos] &= ~(std::byte{1} << bitPos);
                }
                break;
            }
            case ColumnType::INT8:
                visitScalar(int8_t{});
                break;
            case ColumnType::INT16:
                visitScalar(int16_t{});
                break;
            case ColumnType::INT32:
                visitScalar(int32_t{});
                break;
            case ColumnType::INT64:
                visitScalar(int64_t{});
                break;
            case ColumnType::UINT8:
                visitScalar(uint8_t{});
                break;
            case ColumnType::UINT16:
                visitScalar(uint16_t{});
                break;
            case ColumnType::UINT32:
                visitScalar(uint32_t{});
                break;
            case ColumnType::UINT64:
                visitScalar(uint64_t{});
                break;
            case ColumnType::FLOAT:
                visitScalar(float{});
                break;
            case ColumnType::DOUBLE:
                visitScalar(double{});
                break;
            case ColumnType::STRING: {
                const uint16_t strLength = unalignedRead<uint16_t>(&buffer[str_lens_cursor]);
                str_lens_cursor += sizeof(uint16_t);

                if (str_pay_cursor + strLength > buffer.size()) [[unlikely]] {
                    throw std::runtime_error(std::string(fnName) + "() string payload out of bounds");
                }

                std::string_view strValue(
                    reinterpret_cast<const char*>(&buffer[str_pay_cursor]),
                    strLength
                );
                str_pay_cursor += strLength;

                dispatch(strValue);
                break;
            }
            default: [[unlikely]]
                throw std::runtime_error(std::string(fnName) + "() unsupported column type");
        }
    }
}

} // namespace bcsv
