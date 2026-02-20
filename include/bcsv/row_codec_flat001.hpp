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

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Primary template: RowCodecFlat001<Layout, Policy> — dynamic layout
// ════════════════════════════════════════════════════════════════════════════

// ── Lifecycle ────────────────────────────────────────────────────────────────

template<typename LayoutType, TrackingPolicy Policy>
inline void RowCodecFlat001<LayoutType, Policy>::setup(const LayoutType& layout)
{
    layout_ = &layout;
    offsets_ = layout.columnOffsetsPacked();
    wire_bits_size_  = static_cast<uint32_t>((layout.columnCount(ColumnType::BOOL) + 7u) / 8u);
    wire_data_size_ = 0;
    wire_strg_count_ = static_cast<uint32_t>(layout.columnCount(ColumnType::STRING));
    
    const size_t count = layout.columnCount();
    const auto& types = layout.columnTypes();
    const auto& packed_offsets = layout.columnOffsetsPacked();
    for (size_t i = count; i-- > 0;) {
        const ColumnType type = types[i];
        if (type == ColumnType::BOOL || type == ColumnType::STRING) {
            continue;
        }
        wire_data_size_ = packed_offsets[i] + static_cast<uint32_t>(sizeOf(type));
        break;
    }
    wire_fixed_size_ = wire_bits_size_ + wire_data_size_ + wire_strg_count_ * static_cast<uint32_t>(sizeof(uint16_t));
    offsets_ptr_ = layout.columnOffsetsPacked().data();
}


// ── Bulk serialize (Row → wire bytes) ────────────────────────────────────────

template<typename LayoutType, TrackingPolicy Policy>
inline std::span<std::byte> RowCodecFlat001<LayoutType, Policy>::serialize(
    const RowType& row, ByteBuffer& buffer) const
{
    assert(layout_ && "RowCodecFlat001::serialize() called before setup()");

    const size_t   off_row  = buffer.size();
    const uint32_t bits_sz  = wire_bits_size_;
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

    const uint32_t bits_sz  = wire_bits_size_;
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
    const uint32_t off = offsets_[col];

    if (type == ColumnType::BOOL) {
        size_t bytePos = off >> 3;
        size_t bitPos  = off & 7;
        boolScratch = (buffer[bytePos] & (std::byte{1} << bitPos)) != std::byte{0};
        return { reinterpret_cast<const std::byte*>(&boolScratch), sizeof(bool) };
    } else if (type == ColumnType::STRING) {
        // Scan strg_lengths to find payload offset for string index 'off'
        size_t lens_cursor = wire_bits_size_ + wire_data_size_;
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
        size_t absOff = wire_bits_size_ + off;
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

} // namespace bcsv
