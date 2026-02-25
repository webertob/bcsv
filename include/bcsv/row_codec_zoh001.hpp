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
 * @file row_codec_zoh001.hpp
 * @brief Implementation of RowCodecZoH001 — Zero-Order-Hold codec.
 *
 * ZoH wire format per row:
 *   [head_][changed_column_data...]
 *
 * head_ is a columnCount-sized bitset with type-grouped layout:
 *   Bits [0..boolCount):              Bool VALUES (identical to row.bits_ layout)
 *   Bits [boolCount..columnCount):    Change flags in ColumnType enum order:
 *       UINT8, UINT16, UINT32, UINT64, INT8, INT16, INT32, INT64,
 *       FLOAT, DOUBLE, STRING
 *
 * Bool values are bulk-copied via assignRange/equalRange (word-level ops).
 * Scalar change detection uses per-type offset vectors for tight inner loops.
 * Data section follows the same type-grouped order as the change flags.
 *
 * An empty span return from serialize() means "no changes" (caller writes
 * length 0 for ZoH repeat).
 *
 * Change detection uses a local copy of the previous row (double-buffer).
 * head_[0..boolCount) also serves as previous-bool-value storage between
 * serialize() calls — equalRange detects bool changes, then assignRange
 * updates from the current row.
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
    guard_ = LayoutGuard(layout.data());  // Lock layout against structural mutations

    const size_t count = layout.columnCount();
    const auto& types = layout.columnTypes();
    const auto& offsets = layout.columnOffsets();

    // Cache bool count
    bool_count_ = layout.columnCount(ColumnType::BOOL);

    // Size the head_ to columnCount (bool values + type-grouped change flags)
    head_.resize(count);
    head_.reset();

    // Build per-type offset vectors
    off_uint8_.clear();  off_uint16_.clear(); off_uint32_.clear(); off_uint64_.clear();
    off_int8_.clear();   off_int16_.clear();  off_int32_.clear();  off_int64_.clear();
    off_float_.clear();  off_double_.clear(); off_string_.clear();

    for (size_t i = 0; i < count; ++i) {
        const uint32_t off = offsets[i];
        switch (types[i]) {
            case ColumnType::BOOL:   break; // handled via head_ bits
            case ColumnType::UINT8:  off_uint8_.push_back(off);  break;
            case ColumnType::UINT16: off_uint16_.push_back(off); break;
            case ColumnType::UINT32: off_uint32_.push_back(off); break;
            case ColumnType::UINT64: off_uint64_.push_back(off); break;
            case ColumnType::INT8:   off_int8_.push_back(off);   break;
            case ColumnType::INT16:  off_int16_.push_back(off);  break;
            case ColumnType::INT32:  off_int32_.push_back(off);  break;
            case ColumnType::INT64:  off_int64_.push_back(off);  break;
            case ColumnType::FLOAT:  off_float_.push_back(off);  break;
            case ColumnType::DOUBLE: off_double_.push_back(off); break;
            case ColumnType::STRING: off_string_.push_back(off); break;
            default: break;
        }
    }

    // Leave data_ and strg_ empty — they will be bulk-copied from
    // the first incoming row in serialize() (avoids unnecessary zero-fill).
    data_.clear();
    strg_.clear();
}

template<typename LayoutType>
void RowCodecZoH001<LayoutType>::reset() noexcept {
    first_row_in_packet_ = true;
}

// ────────────────────────────────────────────────────────────────────────────
// forEachScalarType — dispatch over all scalar ColumnTypes
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
template<typename Fn>
void RowCodecZoH001<LayoutType>::forEachScalarType(Fn&& fn) const {
    fn.template operator()<uint8_t>(off_uint8_);
    fn.template operator()<uint16_t>(off_uint16_);
    fn.template operator()<uint32_t>(off_uint32_);
    fn.template operator()<uint64_t>(off_uint64_);
    fn.template operator()<int8_t>(off_int8_);
    fn.template operator()<int16_t>(off_int16_);
    fn.template operator()<int32_t>(off_int32_);
    fn.template operator()<int64_t>(off_int64_);
    fn.template operator()<float>(off_float_);
    fn.template operator()<double>(off_double_);
}

// ────────────────────────────────────────────────────────────────────────────
// serializeScalars — tight inner loop for one scalar type (change-detect path)
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
template<size_t TypeSize>
void RowCodecZoH001<LayoutType>::serializeScalars(
    const std::vector<uint32_t>& offsets,
    const RowType& row, ByteBuffer& buffer,
    size_t& head_idx, size_t& buf_idx, bool& any_change)
{
    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint32_t off = offsets[i];
        if (std::memcmp(&data_[off], &row.data_[off], TypeSize) != 0) {
            head_.set(head_idx, true);
            any_change = true;
            std::memcpy(&data_[off], &row.data_[off], TypeSize);
            std::memcpy(&buffer[buf_idx], &row.data_[off], TypeSize);
            buf_idx += TypeSize;
        } else {
            head_.set(head_idx, false);
        }
        ++head_idx;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// emitScalars — write all scalars of one type to buffer (first-row path)
// Head bits and prev-row copy are handled by the caller via bulk operations.
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
template<size_t TypeSize>
void RowCodecZoH001<LayoutType>::emitScalars(
    const std::vector<uint32_t>& offsets,
    const RowType& row, ByteBuffer& buffer,
    size_t& buf_idx)
{
    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint32_t off = offsets[i];
        std::memcpy(&buffer[buf_idx], &row.data_[off], TypeSize);
        buf_idx += TypeSize;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// deserializeScalars — tight inner loop for one scalar type
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
template<size_t TypeSize>
void RowCodecZoH001<LayoutType>::deserializeScalars(
    const std::vector<uint32_t>& offsets,
    RowType& row, std::span<const std::byte> buffer,
    size_t& head_idx, size_t& data_offset) const
{
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (head_[head_idx]) {
            const uint32_t off = offsets[i];
            if (data_offset + TypeSize > buffer.size()) [[unlikely]]
                throw std::runtime_error(
                    "RowCodecZoH001::deserialize() failed! Buffer too small for scalar.");
            std::memcpy(&row.data_[off], &buffer[data_offset], TypeSize);
            data_offset += TypeSize;
        }
        ++head_idx;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// serialize — compares current row against prev-row copy to detect changes
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
std::span<std::byte> RowCodecZoH001<LayoutType>::serialize(
    const RowType& row, ByteBuffer& buffer)
{
    assert(layout_ && "RowCodecZoH001::serialize() called before setup()");

    const size_t buf_old_size = buffer.size();
    size_t buf_idx = buf_old_size;

    // Pessimistic reserve: head + all scalars + string length prefixes.
    // String payloads grow on demand; buffer is trimmed at the end.
    size_t pessimistic = head_.sizeBytes();
    forEachScalarType([&]<typename T>(const auto& offsets) {
        pessimistic += offsets.size() * sizeof(T);
    });
    pessimistic += off_string_.size() * sizeof(uint16_t);

    if (first_row_in_packet_) {
        // ── First row in packet ──────────────────────────────────────────
        // All columns are "changed". Set all head_ bits, then overwrite the
        // bool range with actual values. Bulk-copy prev-row state.
        first_row_in_packet_ = false;

        head_.set();
        if (bool_count_ > 0) {
            head_.assignRange(row.bits_, 0, bool_count_);
        }

        buffer.resize(buf_old_size + pessimistic);
        buf_idx += head_.sizeBytes();

        // Bulk-copy prev-row state (no per-field loop needed)
        data_ = row.data_;
        strg_ = row.strg_;

        // Scalars — emit to buffer only (prev copy already taken above)
        forEachScalarType([&]<typename T>(const auto& offsets) {
            emitScalars<sizeof(T)>(offsets, row, buffer, buf_idx);
        });

        // Strings — emit to buffer, grow for payloads
        for (size_t i = 0; i < off_string_.size(); ++i) {
            const uint32_t off = off_string_[i];
            const std::string& str = row.strg_[off];
            uint16_t strLength = static_cast<uint16_t>(
                std::min(str.size(), MAX_STRING_LENGTH));
            const size_t needed = buf_idx + sizeof(uint16_t) + strLength;
            if (needed > buffer.size()) buffer.resize(needed);
            std::memcpy(&buffer[buf_idx], &strLength, sizeof(uint16_t));
            buf_idx += sizeof(uint16_t);
            if (strLength > 0) {
                std::memcpy(&buffer[buf_idx], str.data(), strLength);
                buf_idx += strLength;
            }
        }

        buffer.resize(buf_idx);
        head_.writeTo(&buffer[buf_old_size], head_.sizeBytes());
        return {&buffer[buf_old_size], buf_idx - buf_old_size};
    }

    // ── Subsequent rows: delta encoding ──────────────────────────────────

    // Bool bulk: compare head_[0..bool_count_) with row.bits_, then assign
    bool any_change = (bool_count_ > 0) && !head_.equalRange(row.bits_, 0, bool_count_);
    if (bool_count_ > 0) {
        head_.assignRange(row.bits_, 0, bool_count_);
    }

    buffer.resize(buf_old_size + pessimistic);
    buf_idx += head_.sizeBytes();

    size_t head_idx = bool_count_;

    // Scalars — per-type tight loops (compare + conditional emit)
    forEachScalarType([&]<typename T>(const auto& offsets) {
        serializeScalars<sizeof(T)>(offsets, row, buffer, head_idx, buf_idx, any_change);
    });

    // Strings — compare against prev, grow for payloads
    for (size_t i = 0; i < off_string_.size(); ++i) {
        const uint32_t off = off_string_[i];
        const std::string& cur = row.strg_[off];
        if (cur != strg_[off]) {
            head_.set(head_idx, true);
            any_change = true;
            strg_[off] = cur;
            uint16_t strLength = static_cast<uint16_t>(
                std::min(cur.size(), MAX_STRING_LENGTH));
            const size_t needed = buf_idx + sizeof(uint16_t) + strLength;
            if (needed > buffer.size()) buffer.resize(needed);
            std::memcpy(&buffer[buf_idx], &strLength, sizeof(uint16_t));
            buf_idx += sizeof(uint16_t);
            if (strLength > 0) {
                std::memcpy(&buffer[buf_idx], cur.data(), strLength);
                buf_idx += strLength;
            }
        } else {
            head_.set(head_idx, false);
        }
        ++head_idx;
    }

    if (any_change) {
        buffer.resize(buf_idx);
        head_.writeTo(&buffer[buf_old_size], head_.sizeBytes());
        return {&buffer[buf_old_size], buf_idx - buf_old_size};
    } else {
        buffer.resize(buf_old_size);
        return std::span<std::byte>{};
    }
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — reads head_ bitset, updates only changed columns
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
void RowCodecZoH001<LayoutType>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    assert(layout_ && "RowCodecZoH001::deserialize() called before setup()");

    // Decode head_ from buffer
    if (buffer.size() < head_.sizeBytes()) [[unlikely]] {
        throw std::runtime_error(
            "RowCodecZoH001::deserialize() failed! Buffer too small for head Bitset.");
    }
    head_.readFrom(buffer.data(), head_.sizeBytes());

    // ── Bool bulk extract ────────────────────────────────────────────────
    // head_[0..bool_count_) → row.bits_[0..bool_count_)
    if (bool_count_ > 0) {
        row.bits_.assignRange(head_, 0, bool_count_);
    }

    size_t head_idx = bool_count_;
    size_t data_offset = head_.sizeBytes();

    // ── Scalars — per-type tight loops ───────────────────────────────────
    forEachScalarType([&]<typename T>(const auto& offsets) {
        deserializeScalars<sizeof(T)>(offsets, row, buffer, head_idx, data_offset);
    });

    // ── Strings ──────────────────────────────────────────────────────────
    for (size_t i = 0; i < off_string_.size(); ++i) {
        if (head_[head_idx]) {
            const uint32_t off = off_string_[i];
            if (data_offset + sizeof(uint16_t) > buffer.size()) [[unlikely]]
                throw std::runtime_error(
                    "RowCodecZoH001::deserialize() failed! Buffer too small for string length.");
            uint16_t strLength;
            std::memcpy(&strLength, &buffer[data_offset], sizeof(uint16_t));
            data_offset += sizeof(uint16_t);

            if (data_offset + strLength > buffer.size()) [[unlikely]]
                throw std::runtime_error(
                    "RowCodecZoH001::deserialize() failed! Buffer too small for string payload.");
            std::string& str = row.strg_[off];
            if (strLength > 0) {
                str.assign(reinterpret_cast<const char*>(&buffer[data_offset]), strLength);
            } else {
                str.clear();
            }
            data_offset += strLength;
        }
        ++head_idx;
    }
}


// ════════════════════════════════════════════════════════════════════════════
// LayoutStatic specialization — implementation
// ════════════════════════════════════════════════════════════════════════════

template<typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::setup(const LayoutType& layout) {
    layout_ = &layout;
    // prev_data_ is value-initialized by its default constructor.
    // It will be bulk-copied from the first incoming row in serialize().
}

template<typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::reset() noexcept {
    first_row_in_packet_ = true;
}

// ────────────────────────────────────────────────────────────────────────────
// Payload size computation (compile-time fold expressions)
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
size_t RowCodecZoH001<LayoutStatic<ColumnTypes...>>::computePayloadSizeAll(const RowType& row) {
    size_t total = 0;
    [&]<size_t... Idx>(std::index_sequence<Idx...>) {
        ([&] {
            constexpr size_t ColIdx = SERIALIZATION_ORDER[Idx];
            using T = typename RowType::template column_type<ColIdx>;
            if constexpr (std::is_same_v<T, std::string>) {
                total += sizeof(uint16_t) + std::min(std::get<ColIdx>(row.data_).size(), MAX_STRING_LENGTH);
            } else {
                total += sizeof(T);
            }
        }(), ...);
    }(std::make_index_sequence<SERIALIZATION_ORDER.size()>{});
    return total;
}

template<typename... ColumnTypes>
size_t RowCodecZoH001<LayoutStatic<ColumnTypes...>>::computePayloadSize(
    const RowType& row, const Bitset<COLUMN_COUNT>& header) {
    size_t total = 0;
    [&]<size_t... Idx>(std::index_sequence<Idx...>) {
        ([&] {
            constexpr size_t ColIdx = SERIALIZATION_ORDER[Idx];
            using T = typename RowType::template column_type<ColIdx>;
            if (header.test(WIRE_BIT_INDEX[ColIdx])) {
                if constexpr (std::is_same_v<T, std::string>) {
                    total += sizeof(uint16_t) + std::min(std::get<ColIdx>(row.data_).size(), MAX_STRING_LENGTH);
                } else {
                    total += sizeof(T);
                }
            }
        }(), ...);
    }(std::make_index_sequence<SERIALIZATION_ORDER.size()>{});
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
        // First row: all columns are "changed". Set all head_ bits, then
        // overwrite the bool range with actual values.
        first_row_in_packet_ = false;

        head_.set();
        [&]<size_t... I>(std::index_sequence<I...>) {
            ([&] {
                if constexpr (std::is_same_v<typename RowType::template column_type<I>, bool>) {
                    head_.set(WIRE_BIT_INDEX[I], std::get<I>(row.data_));
                }
            }(), ...);
        }(std::make_index_sequence<COLUMN_COUNT>{});

        const size_t headerSize = head_.sizeBytes();
        const size_t payloadSize = computePayloadSizeAll(row);
        buffer.resize(bufferSizeOld + headerSize + payloadSize);

        size_t writeOff = bufferSizeOld + headerSize;
        serializeAllInOrder<0>(row, buffer, writeOff);

        std::memcpy(&buffer[bufferSizeOld], head_.data(), headerSize);

        // Bulk-copy current row to prev (tuple assignment)
        prev_data_ = row.data_;

        return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
    }

    // Compare current row against prev to build change header
    head_.reset();
    bool hasAnyChange = false;

    [&]<size_t... I>(std::index_sequence<I...>) {
        ([&] {
            using T = typename RowType::template column_type<I>;
            if constexpr (std::is_same_v<T, bool>) {
                head_.set(WIRE_BIT_INDEX[I], std::get<I>(row.data_));
                if (std::get<I>(row.data_) != std::get<I>(prev_data_)) {
                    hasAnyChange = true;
                    std::get<I>(prev_data_) = std::get<I>(row.data_);
                }
            } else {
                if (std::get<I>(row.data_) != std::get<I>(prev_data_)) {
                    head_.set(WIRE_BIT_INDEX[I], true);
                    hasAnyChange = true;
                    std::get<I>(prev_data_) = std::get<I>(row.data_);
                }
            }
        }(), ...);
    }(std::make_index_sequence<COLUMN_COUNT>{});

    if (!hasAnyChange) {
        return std::span<std::byte>{};
    }

    buffer.resize(bufferSizeOld + head_.sizeBytes() + computePayloadSize(row, head_));

    size_t writeOff = bufferSizeOld + head_.sizeBytes();
    serializeInOrder<0>(row, buffer, writeOff);

    std::memcpy(&buffer[bufferSizeOld], head_.data(), head_.sizeBytes());

    return {&buffer[bufferSizeOld], buffer.size() - bufferSizeOld};
}

// ────────────────────────────────────────────────────────────────────────────
// serializeInOrder — iterate SERIALIZATION_ORDER, write changed non-bools
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
template<size_t OrderIdx>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::serializeInOrder(
    const RowType& row, ByteBuffer& buffer,
    size_t& writeOff)
{
    if constexpr (OrderIdx < SERIALIZATION_ORDER.size()) {
        constexpr size_t ColIdx = SERIALIZATION_ORDER[OrderIdx];
        using T = typename RowType::template column_type<ColIdx>;

        if (head_.test(WIRE_BIT_INDEX[ColIdx])) {
            if constexpr (std::is_same_v<T, std::string>) {
                const auto& value = std::get<ColIdx>(row.data_);
                uint16_t strLength = static_cast<uint16_t>(
                    std::min(value.size(), MAX_STRING_LENGTH));
                std::memcpy(&buffer[writeOff], &strLength, sizeof(uint16_t));
                writeOff += sizeof(uint16_t);
                if (strLength > 0) {
                    std::memcpy(&buffer[writeOff], value.data(), strLength);
                    writeOff += strLength;
                }
            } else {
                const auto& value = std::get<ColIdx>(row.data_);
                std::memcpy(&buffer[writeOff], &value, sizeof(T));
                writeOff += sizeof(T);
            }
        }
        serializeInOrder<OrderIdx + 1>(row, buffer, writeOff);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// serializeAllInOrder — all non-bools unconditionally (first row)
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
template<size_t OrderIdx>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::serializeAllInOrder(
    const RowType& row, ByteBuffer& buffer, size_t& writeOff)
{
    if constexpr (OrderIdx < SERIALIZATION_ORDER.size()) {
        constexpr size_t ColIdx = SERIALIZATION_ORDER[OrderIdx];
        using T = typename RowType::template column_type<ColIdx>;

        // Head bits already set by caller via head_.set()

        if constexpr (std::is_same_v<T, std::string>) {
            const auto& value = std::get<ColIdx>(row.data_);
            uint16_t strLength = static_cast<uint16_t>(
                std::min(value.size(), MAX_STRING_LENGTH));
            std::memcpy(&buffer[writeOff], &strLength, sizeof(uint16_t));
            writeOff += sizeof(uint16_t);
            if (strLength > 0) {
                std::memcpy(&buffer[writeOff], value.data(), strLength);
                writeOff += strLength;
            }
        } else {
            const auto& value = std::get<ColIdx>(row.data_);
            std::memcpy(&buffer[writeOff], &value, sizeof(T));
            writeOff += sizeof(T);
        }

        serializeAllInOrder<OrderIdx + 1>(row, buffer, writeOff);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize — reads head_ bitset, updates only changed fields
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::deserialize(
    std::span<const std::byte> buffer, RowType& row) const
{
    // Decode head_ from buffer
    if (buffer.size() < head_.sizeBytes()) {
        throw std::runtime_error(
            "RowCodecZoH001::deserialize() failed! Buffer too small for head Bitset.");
    }
    std::memcpy(head_.data(), buffer.data(), head_.sizeBytes());

    // Extract bool values from header
    // TODO: Replace && short-circuit with if constexpr in lambda (see ToDo.txt).
    //       Current pattern works but is fragile — relies on implicit bool→T
    //       conversion being valid for all T to compile, even when short-circuited.
    [&]<size_t... I>(std::index_sequence<I...>) {
        ((std::is_same_v<typename RowType::template column_type<I>, bool> &&
          (std::get<I>(row.data_) = head_.test(WIRE_BIT_INDEX[I]), true)), ...);
    }(std::make_index_sequence<COLUMN_COUNT>{});

    auto dataBuffer = buffer.subspan(head_.sizeBytes());
    deserializeInOrder<0>(row, dataBuffer, head_);
}

// ────────────────────────────────────────────────────────────────────────────
// deserializeInOrder — iterate SERIALIZATION_ORDER, read changed non-bools
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
template<size_t OrderIdx>
void RowCodecZoH001<LayoutStatic<ColumnTypes...>>::deserializeInOrder(
    RowType& row, std::span<const std::byte>& buffer,
    const Bitset<COLUMN_COUNT>& header) const
{
    if constexpr (OrderIdx < SERIALIZATION_ORDER.size()) {
        constexpr size_t ColIdx = SERIALIZATION_ORDER[OrderIdx];
        using T = typename RowType::template column_type<ColIdx>;

        if (header.test(WIRE_BIT_INDEX[ColIdx])) {
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
                    std::get<ColIdx>(row.data_).assign(
                        reinterpret_cast<const char*>(&buffer[sizeof(uint16_t)]), strLength);
                } else {
                    std::get<ColIdx>(row.data_).clear();
                }
                buffer = buffer.subspan(sizeof(uint16_t) + strLength);
            } else {
                if (buffer.size() < sizeof(T)) {
                    throw std::runtime_error(
                        "RowCodecZoH001::deserialize() failed! Buffer too small for element.");
                }
                std::memcpy(&std::get<ColIdx>(row.data_), buffer.data(), sizeof(T));
                buffer = buffer.subspan(sizeof(T));
            }
        }
        // Column not changed — keeping previous value.
        deserializeInOrder<OrderIdx + 1>(row, buffer, header);
    }
}

} // namespace bcsv
