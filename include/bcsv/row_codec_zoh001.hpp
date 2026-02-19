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
 * @file row_codec_zoh001.hpp
 * @brief Implementation of RowCodecZoH001 — Zero-Order-Hold codec.
 *
 * Extracted from RowImpl::serializeToZoH() / deserializeFromZoH() and
 * RowStaticImpl::serializeToZoH() / deserializeFromZoH().
 *
 * ZoH wire format per row:
 *   [change_bitset][changed_column_data...]
 *
 * - change_bitset has 1 bit per column (rounded up to byte boundary).
 *   For BOOL columns: bit IS the value. For others: bit is the change flag.
 * - Only non-BOOL columns with their change bit set have data following.
 * - Data for changed columns is written sequentially in column order:
 *   scalars as raw bytes, strings as uint16_t length + payload.
 * - An empty span return from serialize() means "no changes" (all bits zero).
 */

#include "row_codec_zoh001.h"
#include "row.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Dynamic Layout — Primary template implementation
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType, TrackingPolicy Policy>
void RowCodecZoH001<LayoutType, Policy>::setup(const LayoutType& layout) {
    layout_ = &layout;
    flat_.setup(layout);
    wire_bits_.resize(layout.columnCount());
    prev_row_ = std::make_unique<RowType>(layout);
    prev_row_->clear();
    has_prev_ = false;
}

template<typename LayoutType, TrackingPolicy Policy>
void RowCodecZoH001<LayoutType, Policy>::reset() noexcept {
    has_prev_ = false;
    if (prev_row_) {
        prev_row_->clear();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — extracted from RowImpl<Policy>::serializeToZoH()
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
std::span<std::byte> RowCodecZoH001<LayoutType, Policy>::serialize(
    const RowType& row, ByteBuffer& buffer) const
{
    if (!prev_row_) {
        throw std::runtime_error("RowCodecZoH001::serialize() called before setup()");
    }

    const size_t bufferSizeOld = buffer.size();
    const size_t headerSize = wire_bits_.sizeBytes();
    buffer.resize(buffer.size() + headerSize);

    wire_bits_.reset();

    const auto& types = layout_->columnTypes();
    const auto& offsets = layout_->columnOffsets();
    const size_t colCount = types.size();
    bool hasPayload = false;

    for (size_t i = 0; i < colCount; ++i) {
        const ColumnType type = types[i];
        const uint32_t offset = offsets[i];

        if (type == ColumnType::BOOL) {
            const bool val = row.bits_[offset];
            wire_bits_.set(i, val);
            (*prev_row_).bits_.set(offset, val);
            continue;
        }

        bool changed = !has_prev_;
        if (has_prev_) {
            if (type == ColumnType::STRING) {
                changed = (row.strg_[offset] != (*prev_row_).strg_[offset]);
            } else {
                const size_t len = wireSizeOf(type);
                changed = std::memcmp(&row.data_[offset], &(*prev_row_).data_[offset], len) != 0;
            }
        }
        wire_bits_.set(i, changed);

        if (!changed) {
            continue;
        }

        hasPayload = true;

        const size_t off = buffer.size();
        if (type == ColumnType::STRING) {
            const std::string& str = row.strg_[offset];
            uint16_t strLength = static_cast<uint16_t>(
                std::min(str.size(), MAX_STRING_LENGTH));
            buffer.resize(buffer.size() + sizeof(strLength) + strLength);
            std::memcpy(&buffer[off], &strLength, sizeof(strLength));
            if (strLength > 0) {
                std::memcpy(&buffer[off + sizeof(strLength)], str.data(), strLength);
            }
            if (strLength == str.size()) {
                (*prev_row_).strg_[offset] = str;
            } else {
                (*prev_row_).strg_[offset].assign(str.data(), strLength);
            }
        } else {
            const size_t len = wireSizeOf(type);
            buffer.resize(buffer.size() + len);
            std::memcpy(&buffer[off], &row.data_[offset], len);
            std::memcpy(&(*prev_row_).data_[offset], &row.data_[offset], len);
        }
    }

    std::memcpy(&buffer[bufferSizeOld], wire_bits_.data(), headerSize);
    has_prev_ = true;

    if (!hasPayload) {
        return {&buffer[bufferSizeOld], headerSize};
    }

    return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — extracted from RowImpl<Policy>::deserializeFromZoH()
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
void RowCodecZoH001<LayoutType, Policy>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    if (!layout_ || !flat_.isSetup()) [[unlikely]] {
        throw std::runtime_error("RowCodecZoH001::deserialize() called before setup()");
    }

    if (buffer.size() < wire_bits_.sizeBytes()) [[unlikely]] {
        throw std::runtime_error(
            "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
    }
    std::memcpy(wire_bits_.data(), &buffer[0], wire_bits_.sizeBytes());

    const auto& types = layout_->columnTypes();
    const auto& offsets = layout_->columnOffsets();
    const size_t colCount = types.size();

    size_t dataOffset = wire_bits_.sizeBytes();
    for (size_t i = 0; i < colCount; ++i) {
        const ColumnType type = types[i];
        const uint32_t offset = offsets[i];

        if (type == ColumnType::BOOL) {
            const bool newVal = wire_bits_[i];
            row.bits_.set(offset, newVal);
        } else {
            if (!wire_bits_[i]) {
                continue;
            }

            if (type == ColumnType::STRING) {
                uint16_t strLength;
                if (dataOffset + sizeof(strLength) > buffer.size()) [[unlikely]]
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for string length.");
                std::memcpy(&strLength, &buffer[dataOffset], sizeof(strLength));
                dataOffset += sizeof(strLength);

                if (dataOffset + strLength > buffer.size()) [[unlikely]]
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for string payload.");
                std::string& str = row.strg_[offset];
                if (strLength > 0) {
                    str.assign(reinterpret_cast<const char*>(&buffer[dataOffset]), strLength);
                } else {
                    str.clear();
                }
                dataOffset += strLength;
            } else {
                const size_t len = wireSizeOf(type);
                if (dataOffset + len > buffer.size()) [[unlikely]]
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for scalar payload.");
                std::memcpy(&row.data_[offset], &buffer[dataOffset], len);
                dataOffset += len;
            }
        }
    }
}


// ════════════════════════════════════════════════════════════════════════════
// LayoutStatic specialization — implementation
// ════════════════════════════════════════════════════════════════════════════

template<TrackingPolicy Policy, typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::setup(const LayoutType& layout) {
    layout_ = &layout;
    flat_.setup(layout);
    prev_row_ = std::make_unique<RowType>(layout);
    prev_row_->clear();
    has_prev_ = false;
}

template<TrackingPolicy Policy, typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::reset() noexcept {
    has_prev_ = false;
    if (prev_row_) {
        prev_row_->clear();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — extracted from RowStaticImpl::serializeToZoH()
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
std::span<std::byte>
RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::serialize(
    const RowType& row, ByteBuffer& buffer) const
{
    if (!prev_row_) {
        throw std::runtime_error("RowCodecZoH001::serialize() called before setup()");
    }

    const size_t bufferSizeOld = buffer.size();
    Bitset<COLUMN_COUNT> rowHeader;
    rowHeader.reset();

    auto setHeaderByCompare = [this, &row, &rowHeader]<size_t... I>(std::index_sequence<I...>) {
        (([&] {
            using T = typename RowType::template column_type<I>;
            if constexpr (std::is_same_v<T, bool>) {
                rowHeader.set(I, std::get<I>(row.data_));
            } else if (!has_prev_) {
                rowHeader.set(I, true);
            } else {
                rowHeader.set(I, std::get<I>(row.data_) != std::get<I>((*prev_row_).data_));
            }
        }()), ...);
    };
    setHeaderByCompare(std::make_index_sequence<COLUMN_COUNT>{});

    buffer.resize(buffer.size() + rowHeader.sizeBytes());
    serializeElementsZoH<0>(row, buffer, rowHeader);
    std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());

    auto updatePrevByHeader = [this, &row, &rowHeader]<size_t... I>(std::index_sequence<I...>) {
        (([&] {
            using T = typename RowType::template column_type<I>;
            if constexpr (std::is_same_v<T, bool>) {
                std::get<I>((*prev_row_).data_) = std::get<I>(row.data_);
            } else if (rowHeader.test(I)) {
                if constexpr (std::is_same_v<T, std::string>) {
                    const auto& src = std::get<I>(row.data_);
                    if (src.size() <= MAX_STRING_LENGTH) {
                        std::get<I>((*prev_row_).data_) = src;
                    } else {
                        std::get<I>((*prev_row_).data_).assign(src.data(), MAX_STRING_LENGTH);
                    }
                } else {
                    std::get<I>((*prev_row_).data_) = std::get<I>(row.data_);
                }
            }
        }()), ...);
    };
    updatePrevByHeader(std::make_index_sequence<COLUMN_COUNT>{});

    has_prev_ = true;

    return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::serializeElementsZoH(
    const RowType& row, ByteBuffer& buffer,
    Bitset<COLUMN_COUNT>& rowHeader) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;
        if constexpr (std::is_same_v<T, bool>) {
            // Store as single bit within header.
            bool value = std::get<Index>(row.data_);
            rowHeader.set(Index, value);
        } else if (rowHeader.test(Index)) {
            // Non-bool changed: serialize data.
            const size_t old_size = buffer.size();
            if constexpr (std::is_same_v<T, std::string>) {
                const auto& value = std::get<Index>(row.data_);
                uint16_t strLength = static_cast<uint16_t>(
                    std::min(value.size(), MAX_STRING_LENGTH));
                const std::byte* strLengthPtr =
                    reinterpret_cast<const std::byte*>(&strLength);
                const std::byte* strDataPtr =
                    reinterpret_cast<const std::byte*>(value.c_str());

                buffer.resize(buffer.size() + sizeof(uint16_t) + strLength);
                std::memcpy(&buffer[old_size], strLengthPtr, sizeof(uint16_t));
                if (strLength > 0) {
                    std::memcpy(&buffer[old_size + sizeof(uint16_t)], strDataPtr, strLength);
                }
            } else {
                const auto& value = std::get<Index>(row.data_);
                const std::byte* dataPtr =
                    reinterpret_cast<const std::byte*>(&value);
                buffer.resize(buffer.size() + sizeof(T));
                std::memcpy(&buffer[old_size], dataPtr, sizeof(T));
            }
        }
        // Recurse to next element.
        serializeElementsZoH<Index + 1>(row, buffer, rowHeader);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — extracted from RowStaticImpl::deserializeFromZoH()
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    if (!layout_ || !flat_.isSetup()) {
        throw std::runtime_error("RowCodecZoH001::deserialize() called before setup()");
    }

    if (buffer.size() < wire_bits_.sizeBytes()) {
        throw std::runtime_error(
            "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
    }
    std::memcpy(wire_bits_.data(), buffer.data(), wire_bits_.sizeBytes());

    auto dataBuffer = buffer.subspan(wire_bits_.sizeBytes());
    deserializeElementsZoH<0>(row, dataBuffer, wire_bits_);
}

template<TrackingPolicy Policy, typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::deserializeElementsZoH(
    RowType& row, std::span<const std::byte>& buffer,
    const Bitset<COLUMN_COUNT>& header) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;

        if constexpr (std::is_same_v<T, bool>) {
            // Always deserialized — stored as single bit in header.
            std::get<Index>(row.data_) = header.test(Index);
        } else if (header.test(Index)) {
            if constexpr (std::is_same_v<T, std::string>) {
                if (buffer.size() < sizeof(uint16_t)) {
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for string length.");
                }
                uint16_t strLength;
                std::memcpy(&strLength, &buffer[0], sizeof(uint16_t));
                if (buffer.size() < sizeof(uint16_t) + strLength) {
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for string payload.");
                }
                if (strLength > 0) {
                    std::get<Index>(row.data_).assign(
                        reinterpret_cast<const char*>(&buffer[sizeof(uint16_t)]), strLength);
                } else {
                    std::get<Index>(row.data_).clear();
                }
                buffer = buffer.subspan(sizeof(uint16_t) + strLength);
            } else {
                if (buffer.size() < sizeof(T)) {
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for element.");
                }
                std::memcpy(&std::get<Index>(row.data_), buffer.data(), sizeof(T));
                buffer = buffer.subspan(sizeof(T));
            }
        }
        // Column not changed — keeping previous value.
        deserializeElementsZoH<Index + 1>(row, buffer, header);
    }
}

} // namespace bcsv
