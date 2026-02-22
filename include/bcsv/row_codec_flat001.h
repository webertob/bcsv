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
 * @file row_codec_flat001.h
 * @brief RowCodecFlat001 — codec for the flat binary wire format (version 001).
 *
 * Wire layout: [bits_][data_][strg_lengths][strg_data]
 *
 * Provides:
 *   - Bulk serialize / deserialize (Row throughput path)
 *   - Wire metadata accessors
 *
 * Template parameters:
 *   LayoutType     — bcsv::Layout (dynamic) or bcsv::LayoutStatic<Ts...>
 *   Policy         — TrackingPolicy::Disabled or TrackingPolicy::Enabled
 *
 * @see ITEM_11_PLAN.md for architecture and design rationale.
 */

#include "definitions.h"
#include "byte_buffer.h"
#include "layout.h"

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// Primary template: RowCodecFlat001 for dynamic Layout
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy = TrackingPolicy::Disabled>
class RowCodecFlat001 {
    using RowType = typename LayoutType::template RowType<Policy>;

public:
    RowCodecFlat001() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept {} // Flat is stateless between rows — no-op.

    // ── Bulk operations (Row — throughput path) ──────────────────────────
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer) const;

    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

    // ── Wire metadata ───────────────────────────────────────────────────
    uint32_t wireBitsSize()  const noexcept {
        return layout_ ? static_cast<uint32_t>((layout_->columnCount(ColumnType::BOOL) + 7u) / 8u) : 0u;
    }
    uint32_t wireDataSize()  const noexcept { return wire_data_size_; }
    uint32_t wireStrgCount() const noexcept {
        return layout_ ? static_cast<uint32_t>(layout_->columnCount(ColumnType::STRING)) : 0u;
    }
    uint32_t wireFixedSize() const noexcept {
        return wireBitsSize() + wire_data_size_ + wireStrgCount() * static_cast<uint32_t>(sizeof(uint16_t));
    }
    uint32_t columnOffset(size_t col) const noexcept { return offsets_ptr_[col]; }
    const uint32_t* columnOffsetsPtr() const noexcept { return offsets_ptr_; }

private:
    const LayoutType* layout_{nullptr};
    uint32_t wire_data_size_{0};
    const std::vector<uint32_t>* packed_offsets_{nullptr};
    const uint32_t* offsets_ptr_{nullptr};
};


// ────────────────────────────────────────────────────────────────────────────
// Partial specialization: RowCodecFlat001 for LayoutStatic<ColumnTypes...>
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
class RowCodecFlat001<LayoutStatic<ColumnTypes...>, Policy> {
    using LayoutType = LayoutStatic<ColumnTypes...>;
    using RowType = typename LayoutType::template RowType<Policy>;

public:
    static constexpr size_t COLUMN_COUNT = LayoutType::columnCount();
    static constexpr size_t BOOL_COUNT   = LayoutType::COLUMN_COUNT_BOOL;
    static constexpr size_t STRING_COUNT = LayoutType::COLUMN_COUNT_STRINGS;

    static constexpr size_t WIRE_BITS_SIZE  = (BOOL_COUNT + 7) / 8;
    static constexpr size_t WIRE_DATA_SIZE  = (0 + ... + (!std::is_same_v<ColumnTypes, bool> && !std::is_same_v<ColumnTypes, std::string> ? sizeof(ColumnTypes) : 0));
    static constexpr size_t WIRE_STRG_COUNT = STRING_COUNT;
    static constexpr size_t WIRE_FIXED_SIZE = WIRE_BITS_SIZE + WIRE_DATA_SIZE + WIRE_STRG_COUNT * sizeof(uint16_t);

    static constexpr auto WIRE_OFFSETS = LayoutType::COLUMN_OFFSETS_PACKED;

    RowCodecFlat001() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout) {
        layout_ = &layout;
        offsets_ptr_ = WIRE_OFFSETS.data();
    }
    void reset() noexcept {} // Flat is stateless — no-op.

    // ── Bulk operations (Row — throughput path) ──────────────────────────
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer) const;

    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

    // ── Wire metadata ───────────────────────────────────────────────────
    static constexpr uint32_t wireBitsSize()  noexcept { return WIRE_BITS_SIZE; }
    static constexpr uint32_t wireDataSize()  noexcept { return WIRE_DATA_SIZE; }
    static constexpr uint32_t wireStrgCount() noexcept { return WIRE_STRG_COUNT; }
    static constexpr uint32_t wireFixedSize() noexcept { return WIRE_FIXED_SIZE; }
    static constexpr size_t   columnOffset(size_t col) noexcept { return WIRE_OFFSETS[col]; }
    const uint32_t* columnOffsetsPtr() const noexcept { return offsets_ptr_; }

private:
    const LayoutType* layout_{nullptr};
    const uint32_t* offsets_ptr_{WIRE_OFFSETS.data()};

    // ── Compile-time recursive helpers ───────────────────────────────────
    template<size_t Index>
    void serializeElements(const RowType& row, ByteBuffer& buffer, size_t offRow,
                           size_t& boolIdx, size_t& dataOff, size_t& lenOff, size_t& payOff) const;

    template<size_t Index>
    void deserializeElements(const std::span<const std::byte>& srcBuffer, RowType& row,
                             size_t& boolIdx, size_t& dataOff, size_t& lenOff, size_t& payOff) const;
};

} // namespace bcsv
