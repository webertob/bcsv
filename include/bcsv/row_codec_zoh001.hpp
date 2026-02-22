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
 * ZoH wire format per row:
 *   [change_bitset][changed_column_data...]
 *
 * - change_bitset has 1 bit per column (rounded up to byte boundary).
 *   For BOOL columns: bit IS the value. For others: bit is the change flag.
 * - Only non-BOOL columns with their change bit set have data following.
 * - Data for changed columns is written sequentially in column order:
 *   scalars as raw bytes, strings as uint16_t length + payload.
 * - An empty span return from serialize() means "no changes" (all bits zero).
 *
 * Change detection uses a local copy of the previous row (double-buffer).
 * During serialize(), the codec compares the current row against the prev
 * copy to determine which columns changed, builds the wire change bitset,
 * and then updates the prev copy from the current row.
 */

#include "row_codec_zoh001.h"
#include "row.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Dynamic Layout — Primary template implementation
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType>
void RowCodecZoH001<LayoutType>::setup(const LayoutType& layout) {
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

    // Size the wire_bits_ to columnCount (one bit per column for change header)
    wire_bits_.resize(layout.columnCount());

    // Build non-bool mask: set bit i for every non-bool column
    non_bool_mask_.resize(layout.columnCount());
    for (size_t i = 0; i < count; ++i) {
        if (types[i] != ColumnType::BOOL) {
            non_bool_mask_.set(i, true);
        }
    }

    // Size prev-row containers to match row's internal structure
    size_t boolCount = 0;
    size_t strgCount = 0;
    uint32_t dataSize = 0;
    for (size_t i = 0; i < count; ++i) {
        ColumnType type = types[i];
        if (type == ColumnType::BOOL) ++boolCount;
        else if (type == ColumnType::STRING) ++strgCount;
    }
    {
        std::vector<uint32_t> tmpOffsets;
        Layout::Data::computeOffsets(types, tmpOffsets, dataSize);
    }
    prev_bits_.resize(boolCount, false);
    prev_data_.assign(dataSize, std::byte{0});
    prev_strg_.resize(strgCount);
}

template<typename LayoutType>
void RowCodecZoH001<LayoutType>::reset() noexcept {
    first_row_in_packet_ = true;
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — compares current row against prev-row copy to detect changes
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
std::span<std::byte> RowCodecZoH001<LayoutType>::serialize(
    const RowType& row, ByteBuffer& buffer)
{
    assert(layout_ && "RowCodecZoH001::serialize() called before setup()");

    const size_t count = layout_->columnCount();
    const ColumnType* types   = layout_->columnTypes().data();
    const uint32_t*   offsets = layout_->columnOffsets().data();

    // Build wire change bitset: compare current row against prev
    wire_bits_.reset();

    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        const uint32_t offset = offsets[i];

        if (type == ColumnType::BOOL) {
            // BOOL: bit IS the value (always in header)
            bool val = row.bits_[offset];
            wire_bits_.set(i, val);
        } else if (first_row_in_packet_) {
            // First row: mark all non-BOOL as changed (full-row emit)
            wire_bits_.set(i, true);
            // Copy prev inline: scalars + strings
            if (type == ColumnType::STRING) {
                prev_strg_[offset] = row.strg_[offset];
            } else {
                const size_t sz = sizeOf(type);
                std::memcpy(&prev_data_[offset], &row.data_[offset], sz);
            }
        } else if (type == ColumnType::STRING) {
            // Compare strings
            const std::string& cur = row.strg_[offset];
            if (cur != prev_strg_[offset]) {
                wire_bits_.set(i, true);
                prev_strg_[offset] = cur;  // update prev only when changed
            }
        } else {
            // Compare scalar data
            const size_t sz = sizeOf(type);
            if (std::memcmp(&row.data_[offset], &prev_data_[offset], sz) != 0) {
                wire_bits_.set(i, true);
                std::memcpy(&prev_data_[offset], &row.data_[offset], sz);  // update prev only when changed
            }
        }
    }

    const bool wasFirstRow = first_row_in_packet_;
    first_row_in_packet_ = false;

    // Check if any non-bool column changed.
    // Bool values are always encoded in the wire header — they are not "changes" but values.
    // We skip emission only when:
    //   1. This is NOT the first row in the packet (first row must always be emitted)
    //   2. No non-bool column changed
    //   3. No bool value actually changed from its previous state
    // Note: we cannot just check if all bools are false — a true→false transition
    // must still produce output so the reader sees the updated values.
    if (!wasFirstRow && !wire_bits_.any(non_bool_mask_)) {
        // No non-bool column changed. Compare bools against previous.
        bool boolChanged = false;
        for (size_t i = 0; i < count; ++i) {
            if (types[i] == ColumnType::BOOL) {
                bool cur = row.bits_[offsets[i]];
                if (cur != prev_bits_[offsets[i]]) {
                    boolChanged = true;
                    break;
                }
            }
        }
        if (!boolChanged) {
            return std::span<std::byte>{};
        }
    }

    // Update prev bool values for next comparison
    for (size_t i = 0; i < count; ++i) {
        if (types[i] == ColumnType::BOOL) {
            prev_bits_.set(offsets[i], row.bits_[offsets[i]]);
        }
    }

    // Compute payload size
    const size_t bufferSizeOld = buffer.size();
    const size_t headerSize = wire_bits_.sizeBytes();
    size_t payloadSize = 0;
    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        if (type != ColumnType::BOOL && wire_bits_[i]) {
            const uint32_t offset = offsets[i];
            if (type == ColumnType::STRING) {
                const std::string& str = row.strg_[offset];
                payloadSize += sizeof(uint16_t) + std::min(str.size(), MAX_STRING_LENGTH);
            } else {
                payloadSize += sizeOf(type);
            }
        }
    }

    buffer.resize(bufferSizeOld + headerSize + payloadSize);

    // Write header
    std::memcpy(&buffer[bufferSizeOld], wire_bits_.data(), headerSize);

    // Write changed column data
    size_t writeOff = bufferSizeOld + headerSize;
    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        if (type != ColumnType::BOOL && wire_bits_[i]) {
            const uint32_t offset = offsets[i];
            if (type == ColumnType::STRING) {
                const std::string& str = row.strg_[offset];
                uint16_t strLength = static_cast<uint16_t>(
                    std::min(str.size(), MAX_STRING_LENGTH));
                std::memcpy(&buffer[writeOff], &strLength, sizeof(strLength));
                writeOff += sizeof(strLength);
                if (strLength > 0) {
                    std::memcpy(&buffer[writeOff], str.data(), strLength);
                    writeOff += strLength;
                }
            } else {
                const size_t wireSize = sizeOf(type);
                std::memcpy(&buffer[writeOff], &row.data_[offset], wireSize);
                writeOff += wireSize;
            }
        }
    }

    return {&buffer[bufferSizeOld], writeOff - bufferSizeOld};
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — reads wire change bitset, updates only changed columns
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
void RowCodecZoH001<LayoutType>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecZoH001::deserialize() called before setup()");

    // Decode wire header into wire_bits_ (columnCount-sized)
    if (buffer.size() < wire_bits_.sizeBytes()) [[unlikely]] {
        throw std::runtime_error(
            "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
    }
    std::memcpy(wire_bits_.data(), &buffer[0], wire_bits_.sizeBytes());

    const size_t count = layout_->columnCount();
    const ColumnType* types   = layout_->columnTypes().data();
    const uint32_t*   offsets = layout_->columnOffsets().data();
    size_t dataOffset = wire_bits_.sizeBytes();

    for (size_t i = 0; i < count; ++i) {
        const ColumnType type = types[i];
        const uint32_t offset = offsets[i];
        if (type == ColumnType::BOOL) {
            // Translate: wire_bits_[column_i] → row.bits_[sequential_bool_index]
            row.bits_.set(offset, wire_bits_[i]);
        } else if (wire_bits_[i]) {
            if (type == ColumnType::STRING) {
                uint16_t strLength;
                if (dataOffset + sizeof(strLength) > buffer.size()) [[unlikely]]
                    throw std::runtime_error("buffer too small");
                std::memcpy(&strLength, &buffer[dataOffset], sizeof(strLength));
                dataOffset += sizeof(strLength);

                if (dataOffset + strLength > buffer.size()) [[unlikely]]
                    throw std::runtime_error("buffer too small");
                std::string& str = row.strg_[offset];
                if (strLength > 0) {
                    str.assign(reinterpret_cast<const char*>(&buffer[dataOffset]), strLength);
                } else {
                    str.clear();
                }
                dataOffset += strLength;
            } else {
                const size_t len = sizeOf(type);
                if (dataOffset + len > buffer.size()) [[unlikely]]
                    throw std::runtime_error("buffer too small");
                std::memcpy(&row.data_[offset], &buffer[dataOffset], len);
                dataOffset += len;
            }
        }
        // Unchanged non-BOOL columns: row retains previous value (ZoH hold)
    }
}


// ════════════════════════════════════════════════════════════════════════════
// LayoutStatic specialization — implementation
// ════════════════════════════════════════════════════════════════════════════

template<typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::setup(const LayoutType& layout) {
    layout_ = &layout;
    // Initialize prev_data_ to defaults
    prev_data_ = std::tuple<ColumnTypes...>{};
}

template<typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::reset() noexcept {
    first_row_in_packet_ = true;
}

// ────────────────────────────────────────────────────────────────────────────
// Payload size computation (compile-time fold expressions for single-resize)
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
size_t RowCodecZoH001<LayoutStatic<ColumnTypes...>>::computePayloadSizeAll(const RowType& row) {
    size_t total = 0;
    [&]<size_t... I>(std::index_sequence<I...>) {
        ([&] {
            using T = typename RowType::template column_type<I>;
            if constexpr (std::is_same_v<T, bool>) {
                // Bools are in header, no payload
            } else if constexpr (std::is_same_v<T, std::string>) {
                total += sizeof(uint16_t) + std::min(std::get<I>(row.data_).size(), MAX_STRING_LENGTH);
            } else {
                total += sizeof(T);
            }
        }(), ...);
    }(std::make_index_sequence<COLUMN_COUNT>{});
    return total;
}

template<typename... ColumnTypes>
size_t RowCodecZoH001<LayoutStatic<ColumnTypes...>>::computePayloadSize(
    const RowType& row, const Bitset<COLUMN_COUNT>& header) {
    size_t total = 0;
    [&]<size_t... I>(std::index_sequence<I...>) {
        ([&] {
            using T = typename RowType::template column_type<I>;
            if constexpr (std::is_same_v<T, bool>) {
                // Bools are in header, no payload
            } else if (header.test(I)) {
                if constexpr (std::is_same_v<T, std::string>) {
                    total += sizeof(uint16_t) + std::min(std::get<I>(row.data_).size(), MAX_STRING_LENGTH);
                } else {
                    total += sizeof(T);
                }
            }
        }(), ...);
    }(std::make_index_sequence<COLUMN_COUNT>{});
    return total;
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — compares row.data_ against prev_data_ to detect changes
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
std::span<std::byte>
RowCodecZoH001<LayoutStatic<ColumnTypes...>>::serialize(
    const RowType& row, ByteBuffer& buffer)
{
    const size_t bufferSizeOld = buffer.size();

    if (first_row_in_packet_) {
        // First row: mark all as changed (full-row emit)
        Bitset<COLUMN_COUNT> rowHeader;
        rowHeader.set();  // all columns "changed"

        const size_t headerSize = rowHeader.sizeBytes();
        const size_t payloadSize = computePayloadSizeAll(row);
        buffer.resize(bufferSizeOld + headerSize + payloadSize);

        size_t writeOff = bufferSizeOld + headerSize;
        serializeElementsZoHAllChanged<0>(row, buffer, rowHeader, writeOff);

        std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), headerSize);

        // Copy current row to prev
        copyRowToPrev<0>(row);
        first_row_in_packet_ = false;

        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }

    // Compare current row against prev to build change header — copy prev inline
    Bitset<COLUMN_COUNT> rowHeader;
    bool hasAnyChange = false;

    [&]<size_t... I>(std::index_sequence<I...>) {
        ([&] {
            using T = typename RowType::template column_type<I>;
            if constexpr (std::is_same_v<T, bool>) {
                // BOOL: bit IS the value
                rowHeader.set(I, std::get<I>(row.data_));
                // Bools always contribute (they are the value, not a change flag)
            } else {
                // Compare against prev
                if (std::get<I>(row.data_) != std::get<I>(prev_data_)) {
                    rowHeader.set(I, true);
                    hasAnyChange = true;
                    std::get<I>(prev_data_) = std::get<I>(row.data_);  // update prev only when changed
                }
            }
        }(), ...);
    }(std::make_index_sequence<COLUMN_COUNT>{});

    // Check if anything changed at all
    // hasAnyChange tracks non-bool changes. For bools, we must compare
    // against previous values — a true→false transition produces an all-zero
    // header but still represents a change that must be emitted.
    if (!hasAnyChange) {
        bool boolChanged = false;
        [&]<size_t... I>(std::index_sequence<I...>) {
            ((std::is_same_v<typename RowType::template column_type<I>, bool> &&
              std::get<I>(row.data_) != std::get<I>(prev_data_) &&
              (boolChanged = true)), ...);
        }(std::make_index_sequence<COLUMN_COUNT>{});

        if (!boolChanged) {
            // No non-bool changes and no bool changes → truly nothing to emit
            return std::span<std::byte>{};
        }
    }

    // Update prev for bool columns
    [&]<size_t... I>(std::index_sequence<I...>) {
        ((std::is_same_v<typename RowType::template column_type<I>, bool> &&
          (std::get<I>(prev_data_) = std::get<I>(row.data_), true)), ...);
    }(std::make_index_sequence<COLUMN_COUNT>{});

    buffer.resize(bufferSizeOld + rowHeader.sizeBytes() + computePayloadSize(row, rowHeader));

    if (hasAnyChange) {
        size_t writeOff = bufferSizeOld + rowHeader.sizeBytes();
        serializeElementsZoH<0>(row, buffer, rowHeader, writeOff);
    }

    std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());

    return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
}

template<typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::serializeElementsZoH(
    const RowType& row, ByteBuffer& buffer,
    const Bitset<COLUMN_COUNT>& rowHeader, size_t& writeOff) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;
        if constexpr (std::is_same_v<T, bool>) {
            // Bool value is stored in header — already set above
        } else if (rowHeader.test(Index)) {
            // Non-bool changed: serialize data.
            if constexpr (std::is_same_v<T, std::string>) {
                const auto& value = std::get<Index>(row.data_);
                uint16_t strLength = static_cast<uint16_t>(
                    std::min(value.size(), MAX_STRING_LENGTH));
                std::memcpy(&buffer[writeOff], &strLength, sizeof(uint16_t));
                writeOff += sizeof(uint16_t);
                if (strLength > 0) {
                    std::memcpy(&buffer[writeOff], value.c_str(), strLength);
                    writeOff += strLength;
                }
            } else {
                const auto& value = std::get<Index>(row.data_);
                std::memcpy(&buffer[writeOff], &value, sizeof(T));
                writeOff += sizeof(T);
            }
        }
        serializeElementsZoH<Index + 1>(row, buffer, rowHeader, writeOff);
    }
}

template<typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::serializeElementsZoHAllChanged(
    const RowType& row, ByteBuffer& buffer,
    Bitset<COLUMN_COUNT>& rowHeader, size_t& writeOff) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;

        if constexpr (std::is_same_v<T, bool>) {
            rowHeader.set(Index, std::get<Index>(row.data_));
        } else if constexpr (std::is_same_v<T, std::string>) {
            const auto& value = std::get<Index>(row.data_);
            uint16_t strLength = static_cast<uint16_t>(
                std::min(value.size(), MAX_STRING_LENGTH));

            std::memcpy(&buffer[writeOff], &strLength, sizeof(uint16_t));
            writeOff += sizeof(uint16_t);
            if (strLength > 0) {
                std::memcpy(&buffer[writeOff], value.data(), strLength);
                writeOff += strLength;
            }
        } else {
            const auto& value = std::get<Index>(row.data_);
            std::memcpy(&buffer[writeOff], &value, sizeof(T));
            writeOff += sizeof(T);
        }

        serializeElementsZoHAllChanged<Index + 1>(row, buffer, rowHeader, writeOff);
    }
}

template<typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::copyRowToPrev(
    const RowType& row)
{
    if constexpr (Index < COLUMN_COUNT) {
        std::get<Index>(prev_data_) = std::get<Index>(row.data_);
        copyRowToPrev<Index + 1>(row);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — reads wire change bitset, updates only changed fields
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    // Decode wire header into wire_bits_
    if (buffer.size() < wire_bits_.sizeBytes()) {
        throw std::runtime_error(
            "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
    }
    std::memcpy(wire_bits_.data(), buffer.data(), wire_bits_.sizeBytes());

    auto dataBuffer = buffer.subspan(wire_bits_.sizeBytes());
    deserializeElementsZoH<0>(row, dataBuffer, wire_bits_);
}

template<typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::deserializeElementsZoH(
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
