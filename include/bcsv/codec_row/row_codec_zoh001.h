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
 * @file row_codec_zoh001.h
 * @brief RowCodecZoH001 — codec for the Zero-Order-Hold wire format (version 001).
 *
 * ZoH wire layout: [row_header_][changed_data...]
 *
 * The row_header_ bitset has columnCount bits with a type-grouped layout:
 *   Bits [0 .. boolCount):              Boolean VALUES (same layout as row.bits_)
 *   Bits [boolCount .. columnCount):    Change flags grouped by ColumnType enum order:
 *       UINT8, UINT16, UINT32, UINT64, INT8, INT16, INT32, INT64,
 *       FLOAT, DOUBLE, STRING
 *
 * The data section follows the same type-grouped order as the change flags.
 * Only non-BOOL columns with their change flag set have data in the section.
 *
 * Bool values are bulk-copied between row.bits_ and row_header_ using
 * assignRange/equalRange for word-level performance.
 * Scalar change detection uses per-type offset vectors for tight inner loops.
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

#include "../definitions.h"
#include "../bitset.h"
#include "../byte_buffer.h"
#include "../layout.h"
#include "../layout_guard.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
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

    /// Copy constructor: acquires a new guard on the same layout data.
    RowCodecZoH001(const RowCodecZoH001& other)
        : guard_(other.layout_ ? LayoutGuard(other.layout_->data()) : LayoutGuard())
        , layout_(other.layout_)
        , row_header_(other.row_header_)
        , data_(other.data_)
        , strg_(other.strg_)
        , bool_count_(other.bool_count_)
        , off_uint8_(other.off_uint8_)
        , off_uint16_(other.off_uint16_)
        , off_uint32_(other.off_uint32_)
        , off_uint64_(other.off_uint64_)
        , off_int8_(other.off_int8_)
        , off_int16_(other.off_int16_)
        , off_int32_(other.off_int32_)
        , off_int64_(other.off_int64_)
        , off_float_(other.off_float_)
        , off_double_(other.off_double_)
        , off_string_(other.off_string_)
        , first_row_in_packet_(other.first_row_in_packet_)
    {}

    RowCodecZoH001& operator=(const RowCodecZoH001& other) {
        if (this != &other) {
            // Acquire new guard before releasing old — strong exception safety.
            LayoutGuard newGuard = other.layout_ ? LayoutGuard(other.layout_->data()) : LayoutGuard();
            guard_.release();
            layout_ = other.layout_;
            row_header_ = other.row_header_;
            data_ = other.data_;
            strg_ = other.strg_;
            bool_count_ = other.bool_count_;
            off_uint8_ = other.off_uint8_;
            off_uint16_ = other.off_uint16_;
            off_uint32_ = other.off_uint32_;
            off_uint64_ = other.off_uint64_;
            off_int8_ = other.off_int8_;
            off_int16_ = other.off_int16_;
            off_int32_ = other.off_int32_;
            off_int64_ = other.off_int64_;
            off_float_ = other.off_float_;
            off_double_ = other.off_double_;
            off_string_ = other.off_string_;
            first_row_in_packet_ = other.first_row_in_packet_;
            guard_ = std::move(newGuard);
        }
        return *this;
    }

    RowCodecZoH001(RowCodecZoH001&&) = default;
    RowCodecZoH001& operator=(RowCodecZoH001&&) = default;

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
    LayoutGuard guard_;                     // Prevents layout mutation while codec is active
    const LayoutType* layout_{nullptr};

    // Row header bitset (columnCount-sized):
    //   [0..bool_count_)           = bool VALUES (same layout as row.bits_)
    //   [bool_count_..columnCount) = change flags, type-grouped (ColumnType enum order)
    // Also serves as the previous-bool-value tracker (row_header_[0..bool_count_) persists
    // between serialize() calls for change detection via equalRange).
    mutable Bitset<> row_header_;

    // Local prev-row copy for change detection (double-buffer strategy)
    std::vector<std::byte> data_;           // Previous scalar data (aligned, same layout as row.data_)
    std::vector<std::string> strg_;         // Previous string values (same layout as row.strg_)

    size_t bool_count_{0};                  // Cached layout.columnCount(ColumnType::BOOL)

    // Per-type offset vectors: each holds byte offsets into row.data_ / data_
    // (for scalar types) or indices into row.strg_ / strg_ (for strings).
    // Populated in setup(), used for tight same-type inner loops in serialize/deserialize.
    std::vector<uint32_t> off_uint8_;
    std::vector<uint32_t> off_uint16_;
    std::vector<uint32_t> off_uint32_;
    std::vector<uint32_t> off_uint64_;
    std::vector<uint32_t> off_int8_;
    std::vector<uint32_t> off_int16_;
    std::vector<uint32_t> off_int32_;
    std::vector<uint32_t> off_int64_;
    std::vector<uint32_t> off_float_;
    std::vector<uint32_t> off_double_;
    std::vector<uint32_t> off_string_;

    bool first_row_in_packet_{true};

    // ── Helpers ─────────────────────────────────────────────────────────

    /// Invoke fn.template operator()<T>(offsets) for each scalar ColumnType.
    template<typename Fn>
    void forEachScalarType(Fn&& fn) const;

    // ── Serialize/Deserialize helpers (type-grouped scalar loops) ────────

    /// Serialize scalars of a given byte size: compare, set row_header_ bit, emit changed data.
    template<size_t TypeSize>
    void serializeScalars(const std::vector<uint32_t>& offsets,
                          const RowType& row, ByteBuffer& buffer,
                          size_t& head_idx, size_t& buf_idx, bool& any_change);

    /// Emit all scalars of a given byte size to buffer (first-row path, no data_ update).
    template<size_t TypeSize>
    void emitScalars(const std::vector<uint32_t>& offsets,
                     const RowType& row, ByteBuffer& buffer,
                     size_t& buf_idx);

    /// Deserialize scalars of a given byte size: read row_header_ bit, read data if set.
    template<size_t TypeSize>
    void deserializeScalars(const std::vector<uint32_t>& offsets,
                            RowType& row, std::span<const std::byte> buffer,
                            size_t& head_idx, size_t& data_offset) const;
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
    static constexpr size_t SCALAR_COUNT = COLUMN_COUNT - BOOL_COUNT - STRING_COUNT;

    // Wire header: columnCount bits = BOOL values + change flags
    static constexpr size_t ROW_HEADER_SIZE = (COLUMN_COUNT + 7) / 8;

    /// Maps column index → bit position in the row header.
    /// Bools get positions [0..BOOL_COUNT), non-bools get positions
    /// [BOOL_COUNT..COLUMN_COUNT) in ColumnType enum order.
    static constexpr std::array<size_t, COLUMN_COUNT> ROW_HEADER_BIT_INDEX = []() {
        constexpr auto& types = LayoutType::COLUMN_TYPES;
        std::array<size_t, COLUMN_COUNT> r{};

        // Assign bools first (in column order)
        size_t boolIdx = 0;
        for (size_t i = 0; i < COLUMN_COUNT; ++i) {
            if (types[i] == ColumnType::BOOL) r[i] = boolIdx++;
        }

        // Assign non-bools in ColumnType enum order (UINT8, UINT16, ..., STRING)
        size_t nonBoolIdx = BOOL_COUNT;
        for (uint8_t ct = static_cast<uint8_t>(ColumnType::UINT8);
             ct <= static_cast<uint8_t>(ColumnType::STRING); ++ct) {
            for (size_t i = 0; i < COLUMN_COUNT; ++i) {
                if (static_cast<uint8_t>(types[i]) == ct) r[i] = nonBoolIdx++;
            }
        }
        return r;
    }();

    /// Serialization order for non-bool columns: column indices grouped by
    /// ColumnType enum order (UINT8 first, ..., STRING last).
    static constexpr auto SERIALIZATION_ORDER = []() {
        constexpr auto& types = LayoutType::COLUMN_TYPES;
        std::array<size_t, COLUMN_COUNT - BOOL_COUNT> r{};
        size_t idx = 0;
        for (uint8_t ct = static_cast<uint8_t>(ColumnType::UINT8);
             ct <= static_cast<uint8_t>(ColumnType::STRING); ++ct) {
            for (size_t i = 0; i < COLUMN_COUNT; ++i) {
                if (static_cast<uint8_t>(types[i]) == ct) r[idx++] = i;
            }
        }
        return r;
    }();

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
    mutable Bitset<COLUMN_COUNT> row_header_;   // Row header (bool values + change flags)
    bool first_row_in_packet_{true};

    // Local prev-row copy for change detection (tuple-based storage)
    std::tuple<ColumnTypes...> prev_data_;

    // ── Compile-time helpers ─────────────────────────────────────────────

    /// Serialize non-bool elements in SERIALIZATION_ORDER, checking/setting
    /// row_header_ bits at ROW_HEADER_BIT_INDEX positions. Writes changed data to buffer.
    template<size_t OrderIdx>
    void serializeInOrder(const RowType& row, ByteBuffer& buffer,
                          size_t& writeOff);

    /// Serialize all non-bool elements unconditionally (first row in packet).
    template<size_t OrderIdx>
    void serializeAllInOrder(const RowType& row, ByteBuffer& buffer,
                             size_t& writeOff);

    /// Compute payload size for changed non-bool columns (using ROW_HEADER_BIT_INDEX).
    static size_t computePayloadSize(const RowType& row, const Bitset<COLUMN_COUNT>& header);

    /// Compute payload size when all non-bool columns are emitted.
    static size_t computePayloadSizeAll(const RowType& row);

    /// Deserialize non-bool elements in SERIALIZATION_ORDER.
    template<size_t OrderIdx>
    void deserializeInOrder(RowType& row, std::span<const std::byte>& buffer,
                            const Bitset<COLUMN_COUNT>& header) const;
};

} // namespace bcsv
