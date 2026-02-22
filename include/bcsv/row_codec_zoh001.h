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
 * @file row_codec_zoh001.h
 * @brief RowCodecZoH001 — codec for the Zero-Order-Hold wire format (version 001).
 *
 * ZoH wire layout: [change_bitset][changed_data...]
 *
 * The change bitset has per-column semantics:
 *   - BOOL columns: bit is the boolean VALUE (always present)
 *   - Non-BOOL columns: bit is the CHANGE FLAG (1 = column data follows)
 *
 * First row in packet is full-row emit semantics; subsequent rows are ZoH-delta
 * encoded. The codec maintains a local copy of the previous row to detect
 * changes during serialization (double-buffer strategy to avoid allocations).
 *
 * The Writer is responsible for:
 *   - Calling reset() at packet boundaries
 *   - Detecting ZoH repeats (byte-identical serialized rows → write length 0)
 *
 * Template parameters:
 *   LayoutType     — bcsv::Layout (dynamic) or bcsv::LayoutStatic<Ts...>
 *
 * @see ITEM_11_PLAN.md for architecture and design rationale.
 */

#include "definitions.h"
#include "bitset.h"
#include "layout.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// Primary template: RowCodecZoH001 for dynamic Layout
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
class RowCodecZoH001 {
    using RowType = typename LayoutType::RowType;

public:
    RowCodecZoH001() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept; // Clears inter-row state for new packet.

    // ── Bulk operations (Row — throughput path) ──────────────────────────

    /// Serialize using ZoH encoding.
    /// Compares current row against local prev-row copy to detect changes.
    /// Returns empty span if no changes (ZoH "all unchanged" — caller writes length 0).
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer);

    /// Deserialize a ZoH-encoded buffer into the row.
    /// Only changed columns are updated — unchanged columns retain their
    /// previous values (the caller must not clear the row between calls).
    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

private:
    const LayoutType* layout_{nullptr};
    uint32_t wire_data_size_{0};
    const std::vector<uint32_t>* packed_offsets_{nullptr};

    // Wire change header (columnCount-sized bitset for building ZoH wire format)
    mutable Bitset<> wire_bits_;

    // Mask with bits set only for non-bool columns (used for ZoH "nothing changed" check)
    Bitset<> non_bool_mask_;

    // Local prev-row copy for change detection (double-buffer strategy)
    Bitset<> prev_bits_;                    // Previous bool values (boolCount-sized)
    std::vector<std::byte> prev_data_;      // Previous scalar data
    std::vector<std::string> prev_strg_;    // Previous string values
    bool first_row_in_packet_{true};
};


// ────────────────────────────────────────────────────────────────────────────
// Partial specialization: RowCodecZoH001 for LayoutStatic<ColumnTypes...>
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
class RowCodecZoH001<LayoutStatic<ColumnTypes...>> {
    using LayoutType = LayoutStatic<ColumnTypes...>;
    using RowType = typename LayoutType::RowType;

public:
    static constexpr size_t COLUMN_COUNT = sizeof...(ColumnTypes);
    static constexpr size_t BOOL_COUNT   = LayoutType::COLUMN_COUNT_BOOL;
    static constexpr size_t STRING_COUNT = LayoutType::COLUMN_COUNT_STRINGS;

    static constexpr size_t WIRE_BITS_SIZE  = (BOOL_COUNT + 7) / 8;
    static constexpr size_t WIRE_DATA_SIZE  = (0 + ... + (!std::is_same_v<ColumnTypes, bool> && !std::is_same_v<ColumnTypes, std::string> ? sizeof(ColumnTypes) : 0));
    static constexpr size_t WIRE_STRG_COUNT = STRING_COUNT;
    static constexpr size_t WIRE_FIXED_SIZE = WIRE_BITS_SIZE + WIRE_DATA_SIZE + WIRE_STRG_COUNT * sizeof(uint16_t);

    static constexpr auto WIRE_OFFSETS = LayoutType::COLUMN_OFFSETS_PACKED;

    RowCodecZoH001() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept;

    // ── Bulk operations (Row — throughput path) ──────────────────────────
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer);

    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

private:
    const LayoutType* layout_{nullptr};
    mutable Bitset<COLUMN_COUNT> wire_bits_;  // Wire change header
    bool first_row_in_packet_{true};

    // Local prev-row copy for change detection (tuple-based storage)
    std::tuple<ColumnTypes...> prev_data_;

    // ── Compile-time recursive helpers ───────────────────────────────────
    template<size_t Index>
    void serializeElementsZoH(const RowType& row, ByteBuffer& buffer,
                               const Bitset<COLUMN_COUNT>& rowHeader, size_t& writeOff) const;

    template<size_t Index>
    void serializeElementsZoHAllChanged(const RowType& row, ByteBuffer& buffer,
                                        Bitset<COLUMN_COUNT>& rowHeader, size_t& writeOff) const;

    /// Compute payload size for changed non-bool columns (fold expression).
    static size_t computePayloadSize(const RowType& row, const Bitset<COLUMN_COUNT>& header);

    /// Compute payload size when all non-bool columns are emitted.
    static size_t computePayloadSizeAll(const RowType& row);

    template<size_t Index>
    void deserializeElementsZoH(RowType& row, std::span<const std::byte>& buffer,
                                 const Bitset<COLUMN_COUNT>& header) const;

    template<size_t Index>
    void copyRowToPrev(const RowType& row);
};

} // namespace bcsv
