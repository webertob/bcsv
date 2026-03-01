/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
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
#include "../row.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Primary template: RowCodecFlat001<Layout> — dynamic layout
// ════════════════════════════════════════════════════════════════════════════

// ── Lifecycle ────────────────────────────────────────────────────────────────

template<typename LayoutType>
inline void RowCodecFlat001<LayoutType>::setup(const LayoutType& layout)
{
    layout_ = &layout;
    guard_ = LayoutGuard(layout.data());  // Lock layout against structural mutations
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

template<typename LayoutType>
inline std::span<std::byte> RowCodecFlat001<LayoutType>::serialize(
    const RowType& row, ByteBuffer& buffer) const
{
    assert(layout_ && "RowCodecFlat001::serialize() called before setup()");

    const size_t   off_row  = buffer.size();
    const uint32_t bits_sz  = rowHeaderSize();
    const uint32_t data_sz  = wire_data_size_;
    const uint32_t fixed_sz = wireFixedSize();
    const size_t   count    = layout_->columnCount();

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

    // bits_ is sequentially packed bool values — bulk copy
    if (bits_sz > 0) {
        std::memcpy(&buffer[off_row], row.bits_.data(), bits_sz);
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

template<typename LayoutType>
inline void RowCodecFlat001<LayoutType>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecFlat001::deserialize() called before setup()");

    const uint32_t bits_sz  = rowHeaderSize();
    const uint32_t data_sz  = wire_data_size_;
    const uint32_t fixed_sz = wireFixedSize();
    const size_t   count    = layout_->columnCount();

    if (fixed_sz > buffer.size()) [[unlikely]] {
        throw std::runtime_error("RowCodecFlat001::deserialize() failed: buffer too short");
    }

    const ColumnType* types   = layout_->columnTypes().data();
    const uint32_t*   offsets = layout_->columnOffsets().data();

    // bits_ is sequentially packed bool values — bulk copy
    if (bits_sz > 0) {
        std::memcpy(row.bits_.data(), &buffer[0], bits_sz);
    }

    size_t bool_idx = 0;
    size_t wire_off = bits_sz;
    size_t len_off  = bits_sz + data_sz;
    size_t pay_off  = fixed_sz;

    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        const uint32_t   off  = offsets[i];

        if (type == ColumnType::BOOL) {
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
}


// ════════════════════════════════════════════════════════════════════════════
// Partial specialization: RowCodecFlat001<LayoutStatic<Ts...>>
// ════════════════════════════════════════════════════════════════════════════

// ── Bulk serialize (static Row → wire bytes) ─────────────────────────────────

template<typename... ColumnTypes>
inline std::span<std::byte> RowCodecFlat001<LayoutStatic<ColumnTypes...>>::serialize(
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
    if constexpr (ROW_HEADER_SIZE > 0) {
        std::memset(&buffer[offRow], 0, ROW_HEADER_SIZE);
    }

    // Serialize each column using compile-time recursion
    size_t boolIdx = 0;
    size_t dataOff = offRow + ROW_HEADER_SIZE;
    size_t lenOff  = offRow + ROW_HEADER_SIZE + WIRE_DATA_SIZE;
    size_t payOff  = offRow + WIRE_FIXED_SIZE;
    serializeElements<0>(row, buffer, offRow, boolIdx, dataOff, lenOff, payOff);

    return {&buffer[offRow], buffer.size() - offRow};
}

template<typename... ColumnTypes>
template<size_t Index>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>>::serializeElements(
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

template<typename... ColumnTypes>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecFlat001::deserialize() called before setup()");

    if (WIRE_FIXED_SIZE > buffer.size()) [[unlikely]] {
        throw std::runtime_error("RowCodecFlat001::deserialize() failed: buffer too short for fixed section");
    }
    size_t boolIdx = 0;
    size_t dataOff = ROW_HEADER_SIZE;
    size_t lenOff  = ROW_HEADER_SIZE + WIRE_DATA_SIZE;
    size_t payOff  = WIRE_FIXED_SIZE;
    deserializeElements<0>(buffer, row, boolIdx, dataOff, lenOff, payOff);
}

template<typename... ColumnTypes>
template<size_t Index>
inline void RowCodecFlat001<LayoutStatic<ColumnTypes...>>::deserializeElements(
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

} // namespace bcsv
