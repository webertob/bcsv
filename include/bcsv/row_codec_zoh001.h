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
 * Composes RowCodecFlat001 internally — first-row-in-packet is a full flat row,
 * subsequent rows are ZoH-delta encoded. Per-column access (readColumn) delegates
 * to the flat codec since ZoH is only a transport optimization.
 *
 * TrackingPolicy and codec are orthogonal axes:
 *   - Enabled: row.bits_ (dynamic) / row.changes_ (static) are columnCount-sized
 *     and can be used directly as the wire change header (fast path, no copy).
 *   - Disabled: row.bits_ is boolCount-sized. The codec uses an internal
 *     wire_bits_ member to hold the columnCount-sized wire change header,
 *     translating to/from the row as needed.
 *
 * The Writer is responsible for:
 *   - Calling reset() at packet boundaries
 *   - Calling row.changesSet() before the first row in a packet (Enabled only)
 *   - Calling row.changesReset() after each serialize (Enabled only)
 *   - Detecting ZoH repeats (byte-identical serialized rows → write length 0)
 *
 * Template parameters:
 *   LayoutType     — bcsv::Layout (dynamic) or bcsv::LayoutStatic<Ts...>
 *   Policy         — TrackingPolicy::Disabled or TrackingPolicy::Enabled
 *
 * @see row_codec_flat001.h for the flat codec this composes.
 * @see ITEM_11_PLAN.md for architecture and design rationale.
 */

#include "row_codec_flat001.h"
#include "definitions.h"
#include "bitset.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// Primary template: RowCodecZoH001 for dynamic Layout
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy = TrackingPolicy::Enabled>
class RowCodecZoH001 {
    using RowType = typename LayoutType::template RowType<Policy>;

public:
    RowCodecZoH001() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept; // Clears inter-row state for new packet.

    // ── Bulk operations (Row — throughput path) ──────────────────────────

    /// Serialize using ZoH encoding.
    /// Returns empty span if no changes (ZoH "all unchanged" — caller writes length 0).
    /// The caller (Writer) is responsible for detecting byte-identical repeats
    /// of the returned span and writing length 0 for those as well.
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer) const;

    /// Deserialize a ZoH-encoded buffer into the row.
    /// Only changed columns are updated — unchanged columns retain their
    /// previous values (the caller must not clear the row between calls).
    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

    // ── Per-column access (RowView — sparse/lazy path) ──────────────────
    // ZoH is a transport optimization — per-column access uses the flat format.
    // These delegate directly to the internal flat codec.
    [[nodiscard]] std::span<const std::byte> readColumn(
        std::span<const std::byte> buffer, size_t col,
        bool& boolScratch) const {
        return flat_.readColumn(buffer, col, boolScratch);
    }

    // ── Wire metadata (delegates to flat codec) ─────────────────────────
    uint32_t wireBitsSize()  const noexcept { return flat_.wireBitsSize(); }
    uint32_t wireDataSize()  const noexcept { return flat_.wireDataSize(); }
    uint32_t wireStrgCount() const noexcept { return flat_.wireStrgCount(); }
    uint32_t wireFixedSize() const noexcept { return flat_.wireFixedSize(); }
    uint32_t columnOffset(size_t col) const noexcept { return flat_.columnOffset(col); }
    bool isSetup() const noexcept { return flat_.isSetup(); }

    // ── Access to inner flat codec ──────────────────────────────────────
    const RowCodecFlat001<LayoutType, Policy>& flat() const noexcept { return flat_; }

private:
    RowCodecFlat001<LayoutType, Policy> flat_;   // composition
    const LayoutType* layout_{nullptr};
    mutable Bitset<> wire_bits_;  // Wire change header (columnCount-sized).
                                  // Shortcut when Enabled: unused — row.bits_ IS wire format.
                                  // General when Disabled: intermediate for value comparison.
};


// ────────────────────────────────────────────────────────────────────────────
// Partial specialization: RowCodecZoH001 for LayoutStatic<ColumnTypes...>
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
class RowCodecZoH001<LayoutStatic<ColumnTypes...>, Policy> {
    using LayoutType = LayoutStatic<ColumnTypes...>;
    using RowType = typename LayoutType::template RowType<Policy>;

public:
    static constexpr size_t COLUMN_COUNT = sizeof...(ColumnTypes);

    RowCodecZoH001() = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept;

    // ── Bulk operations (Row — throughput path) ──────────────────────────
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer) const;

    void deserialize(
        std::span<const std::byte> buffer, RowType& row) const;

    // ── Per-column access (delegates to flat codec) ─────────────────────
    [[nodiscard]] std::span<const std::byte> readColumn(
        std::span<const std::byte> buffer, size_t col,
        bool& boolScratch) const {
        return flat_.readColumn(buffer, col, boolScratch);
    }

    // ── Wire metadata (delegates to flat codec) ─────────────────────────
    static constexpr uint32_t wireBitsSize()  noexcept { return decltype(flat_)::wireBitsSize(); }
    static constexpr uint32_t wireDataSize()  noexcept { return decltype(flat_)::wireDataSize(); }
    static constexpr uint32_t wireStrgCount() noexcept { return decltype(flat_)::wireStrgCount(); }
    static constexpr uint32_t wireFixedSize() noexcept { return decltype(flat_)::wireFixedSize(); }
    static constexpr size_t   columnOffset(size_t col) noexcept { return decltype(flat_)::columnOffset(col); }
    bool isSetup() const noexcept { return flat_.isSetup(); }

    const RowCodecFlat001<LayoutType, Policy>& flat() const noexcept { return flat_; }

private:
    RowCodecFlat001<LayoutType, Policy> flat_;   // composition
    const LayoutType* layout_{nullptr};
    mutable Bitset<COLUMN_COUNT> wire_bits_;  // Wire change header.
                                               // Shortcut when Enabled: unused — row.changes_ IS wire format.
                                               // General when Disabled: intermediate for value comparison.

    // ── Compile-time recursive helpers ───────────────────────────────────
    template<size_t Index>
    void serializeElementsZoH(const RowType& row, ByteBuffer& buffer,
                               Bitset<COLUMN_COUNT>& rowHeader) const;

    template<size_t Index>
    void deserializeElementsZoH(RowType& row, std::span<const std::byte>& buffer,
                                 const Bitset<COLUMN_COUNT>& header) const;
};

} // namespace bcsv
