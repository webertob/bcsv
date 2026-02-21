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

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Dynamic Layout — Primary template implementation
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType, TrackingPolicy Policy>
void RowCodecZoH001<LayoutType, Policy>::setup(const LayoutType& layout) {
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
    if constexpr (!isTrackingEnabled(Policy)) {
        wire_bits_.resize(layout.columnCount());
    }
}

template<typename LayoutType, TrackingPolicy Policy>
void RowCodecZoH001<LayoutType, Policy>::reset() noexcept {
    first_row_in_packet_ = true;
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — extracted from RowImpl<Policy>::serializeToZoH()
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
std::span<std::byte> RowCodecZoH001<LayoutType, Policy>::serialize(
    RowType& row, ByteBuffer& buffer)
{
    assert(layout_ && "RowCodecZoH001::serialize() called before setup()");

    if constexpr (isTrackingEnabled(Policy)) {
        // ── SHORTCUT: row.bits_ IS the wire change header ────────────────
        // When Enabled, row.bits_ is columnCount-sized with bool values at
        // bool positions and change flags at non-bool positions — exactly
        // the ZoH wire format. Use it directly (zero intermediate copy).
        auto& wireBits = row.bits_;

        if (first_row_in_packet_) {
            row.trackingSetAllChanged();
            first_row_in_packet_ = false;
        }

        const size_t bufferSizeOld = buffer.size();

        if (!wireBits.any()) {
            row.trackingResetChanged();
            return std::span<std::byte>{};
        }

        const size_t headerSize = wireBits.sizeBytes();
        const size_t count = layout_->columnCount();
        size_t payloadSize = 0;
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            if (type != ColumnType::BOOL && wireBits[i]) {
                const uint32_t offset = layout_->columnOffset(i);
                if (type == ColumnType::STRING) {
                    const std::string& str = row.strg_[offset];
                    payloadSize += sizeof(uint16_t) + std::min(str.size(), MAX_STRING_LENGTH);
                } else {
                    payloadSize += sizeOf(type);
                }
            }
        }

        buffer.resize(bufferSizeOld + headerSize + payloadSize);
        std::memcpy(&buffer[bufferSizeOld], wireBits.data(), headerSize);

        if (!wireBits.any(layout_->trackedMask())) {
            row.trackingResetChanged();
            return {&buffer[bufferSizeOld], headerSize};
        }

        size_t writeOff = bufferSizeOld + headerSize;
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            if (type != ColumnType::BOOL && wireBits[i]) {
                const uint32_t offset = layout_->columnOffset(i);
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
        row.trackingResetChanged();
        return {&buffer[bufferSizeOld], writeOff - bufferSizeOld};

    } else {
        // ── GENERAL: no change tracking → mark all non-BOOL as changed ──
        // row.bits_ is boolCount-sized (bool values only). Build wire_bits_
        // (columnCount-sized): BOOL positions get their values from
        // row.bits_[columnOffset(i)], non-BOOL positions are set (all changed).
        wire_bits_.reset();
        const size_t count = layout_->columnCount();
        bool hasAny = false;
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            const uint32_t offset = layout_->columnOffset(i);
            if (type == ColumnType::BOOL) {
                bool val = row.bits_[offset];
                wire_bits_.set(i, val);
                if (val) hasAny = true;
            } else {
                wire_bits_.set(i, true);
                hasAny = true;
            }
        }
        if (!hasAny) return std::span<std::byte>{};

        const size_t bufferSizeOld = buffer.size();
        const size_t headerSize = wire_bits_.sizeBytes();
        size_t payloadSize = 0;
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            if (type == ColumnType::BOOL) {
                continue;
            }
            const uint32_t offset = layout_->columnOffset(i);
            if (type == ColumnType::STRING) {
                const std::string& str = row.strg_[offset];
                payloadSize += sizeof(uint16_t) + std::min(str.size(), MAX_STRING_LENGTH);
            } else {
                payloadSize += sizeOf(type);
            }
        }

        buffer.resize(bufferSizeOld + headerSize + payloadSize);
        std::memcpy(&buffer[bufferSizeOld], wire_bits_.data(), headerSize);

        // Serialize ALL non-BOOL columns (no tracking → always serialize).
        size_t writeOff = bufferSizeOld + headerSize;
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            if (type == ColumnType::BOOL) {
                // Already in header.
            } else {
                const uint32_t offset = layout_->columnOffset(i);
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
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — extracted from RowImpl<Policy>::deserializeFromZoH()
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy>
void RowCodecZoH001<LayoutType, Policy>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecZoH001::deserialize() called before setup()");

    if constexpr (isTrackingEnabled(Policy)) {
        // ── SHORTCUT: decode wire header directly into row.bits_ ─────────
        // When Enabled, row.bits_ is columnCount-sized = wire format.
        // memcpy the wire change header directly — zero intermediate copy.
        auto& wireBits = row.bits_;

        if (buffer.size() >= wireBits.sizeBytes()) {
            std::memcpy(wireBits.data(), &buffer[0], wireBits.sizeBytes());
        } else [[unlikely]] {
            throw std::runtime_error(
                "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
        }

        const size_t count = layout_->columnCount();
        size_t dataOffset = wireBits.sizeBytes();
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            const uint32_t offset = layout_->columnOffset(i);
            if (type == ColumnType::BOOL) {
                // Bool value already decoded — bit i in wireBits IS the value.
            } else if (wireBits[i]) {
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
        }

    } else {
        // ── GENERAL: decode into wire_bits_, translate to row ────────────
        // wire_bits_ is columnCount-sized (wire format). row.bits_ is
        // boolCount-sized (bool values only, indexed by sequential offset).
        if (buffer.size() < wire_bits_.sizeBytes()) [[unlikely]] {
            throw std::runtime_error(
                "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
        }
        std::memcpy(wire_bits_.data(), &buffer[0], wire_bits_.sizeBytes());

        const size_t count = layout_->columnCount();
        size_t dataOffset = wire_bits_.sizeBytes();
        for (size_t i = 0; i < count; ++i) {
            const ColumnType type = layout_->columnType(i);
            const uint32_t offset = layout_->columnOffset(i);
            if (type == ColumnType::BOOL) {
                // Translate: wire_bits_[column_i] → row.bits_[sequential_bool_index].
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
        }
    }
}


// ════════════════════════════════════════════════════════════════════════════
// LayoutStatic specialization — implementation
// ════════════════════════════════════════════════════════════════════════════

template<TrackingPolicy Policy, typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::setup(const LayoutType& layout) {
    layout_ = &layout;
}

template<TrackingPolicy Policy, typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::reset() noexcept {
    first_row_in_packet_ = true;
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — extracted from RowStaticImpl::serializeToZoH()
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
std::span<std::byte>
RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::serialize(
    RowType& row, ByteBuffer& buffer)
{
    if constexpr (isTrackingEnabled(Policy)) {
        // ── SHORTCUT: row.changes_ is columnCount-sized = wire-compatible ──
        // Copy to local rowHeader (needed because bool positions get
        // overwritten with actual values during serialization).
        if (first_row_in_packet_) {
            row.trackingSetAllChanged();
            first_row_in_packet_ = false;
        }

        const size_t bufferSizeOld = buffer.size();

        if (!row.trackingAnyChanged()) {
            row.trackingResetChanged();
            return std::span<std::byte>{};
        }

        Bitset<COLUMN_COUNT> rowHeader = row.changes_;
        buffer.resize(buffer.size() + rowHeader.sizeBytes());

        serializeElementsZoH<0>(row, buffer, rowHeader);

        std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());

    row.trackingResetChanged();
        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};

    } else {
        // ── GENERAL: no change tracking → mark all columns as changed ──
        // rowHeader with all bits set — serializeElementsZoH will overwrite
        // BOOL positions with actual values, non-BOOL will all be serialized.
        const size_t bufferSizeOld = buffer.size();

        Bitset<COLUMN_COUNT> rowHeader;
        rowHeader.set();  // all columns "changed"
        buffer.resize(buffer.size() + rowHeader.sizeBytes());

        serializeElementsZoHAllChanged<0>(row, buffer, rowHeader);

        std::memcpy(&buffer[bufferSizeOld], rowHeader.data(), rowHeader.sizeBytes());

        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }
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

template<TrackingPolicy Policy, typename... ColumnTypes>
template<size_t Index>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::serializeElementsZoHAllChanged(
    const RowType& row, ByteBuffer& buffer,
    Bitset<COLUMN_COUNT>& rowHeader) const
{
    if constexpr (Index < COLUMN_COUNT) {
        using T = typename RowType::template column_type<Index>;

        if constexpr (std::is_same_v<T, bool>) {
            rowHeader.set(Index, std::get<Index>(row.data_));
        } else if constexpr (std::is_same_v<T, std::string>) {
            const auto& value = std::get<Index>(row.data_);
            uint16_t strLength = static_cast<uint16_t>(
                std::min(value.size(), MAX_STRING_LENGTH));
            const size_t old_size = buffer.size();

            buffer.resize(buffer.size() + sizeof(uint16_t) + strLength);
            std::memcpy(&buffer[old_size], &strLength, sizeof(uint16_t));
            if (strLength > 0) {
                std::memcpy(&buffer[old_size + sizeof(uint16_t)], value.data(), strLength);
            }
        } else {
            const auto& value = std::get<Index>(row.data_);
            const size_t old_size = buffer.size();
            buffer.resize(buffer.size() + sizeof(T));
            std::memcpy(&buffer[old_size], &value, sizeof(T));
        }

        serializeElementsZoHAllChanged<Index + 1>(row, buffer, rowHeader);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — extracted from RowStaticImpl::deserializeFromZoH()
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    if constexpr (isTrackingEnabled(Policy)) {
        // ── SHORTCUT: decode wire header directly into row.changes_ ──────
        // When Enabled, row.changes_ is COLUMN_COUNT-sized = wire format.
        if (buffer.size() < row.changes_.sizeBytes()) {
            throw std::runtime_error(
                "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
        }
        std::memcpy(row.changes_.data(), buffer.data(), row.changes_.sizeBytes());

        auto dataBuffer = buffer.subspan(row.changes_.sizeBytes());
        deserializeElementsZoH<0>(row, dataBuffer, row.changes_);

    } else {
        // ── GENERAL: decode into wire_bits_, pass to helper ──────────────
        // wire_bits_ is COLUMN_COUNT-sized. deserializeElementsZoH reads
        // the header to determine which columns to decode. Bool values are
        // stored directly as header bits; non-BOOL flagged columns have
        // data following the header.
        if (buffer.size() < wire_bits_.sizeBytes()) {
            throw std::runtime_error(
                "RowCodecZoH001::deserialize() failed! Buffer too small for change Bitset.");
        }
        std::memcpy(wire_bits_.data(), buffer.data(), wire_bits_.sizeBytes());

        auto dataBuffer = buffer.subspan(wire_bits_.sizeBytes());
        deserializeElementsZoH<0>(row, dataBuffer, wire_bits_);
    }
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
