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
 * @file row_codec_delta001.h
 * @brief RowCodecDelta001 — Delta + VLE row codec for time-series data.
 *
 * Extends the ZoH concept with delta encoding and first-order prediction:
 *
 *   Wire layout: [head_][encoded_data...]
 *   or empty span = all columns unchanged (ZoH shortcut, row length = 0).
 *
 * Head bitset layout (fixed size per layout):
 *   Bits [0 .. boolCount):   Boolean VALUES (same as ZoH)
 *   Per numeric column:      2 mode bits + type-dependent length bits
 *   Per string column:       1 change-flag bit
 *
 * Mode bits (2 bits per numeric column):
 *   00 = ZoH    — value unchanged from previous row, no data.
 *   01 = plain  — full raw value follows (sizeof(T) bytes).
 *   10 = FoC    — first-order-constant prediction matches, no data.
 *                  predicted = prev + gradient, where gradient = prev - prev_prev.
 *   11 = delta  — VLE-encoded delta follows.
 *                  For integers:  zigzag(current - prev), VLE variable bytes.
 *                  For floats:    bit_cast XOR delta, VLE variable bytes.
 *
 * Length field (only present for mode=11 delta):
 *   1-byte types: 0 extra bits (always 1 byte delta)
 *   2-byte types: 1 bit  (0=1B, 1=2B)
 *   4-byte types: 2 bits (00=1B, 01=2B, 10=3B, 11=4B)
 *   8-byte types: 3 bits (000=1B, ..., 111=8B)
 *
 * Bits per numeric column in header:
 *   1-byte types: 2 + 0 = 2 bits
 *   2-byte types: 2 + 1 = 3 bits
 *   4-byte types: 2 + 2 = 4 bits
 *   8-byte types: 2 + 3 = 5 bits
 *
 * State:
 *   - prev_data_ / prev_strg_: previous row values (same as ZoH)
 *   - grad_data_: gradient = prev(n-1) - prev(n-2) for integer columns
 *                 gradient = prev_bits(n-1) ^ prev_bits(n-2) for float columns
 *   - FoC prediction only valid from row ≥ 2 (after gradient is established)
 *
 * @see row_codec_zoh001.h for the ZoH codec this extends.
 * @see vle.hpp for variable-length encoding utilities.
 */

#include "definitions.h"
#include "bitset.h"
#include "byte_buffer.h"
#include "layout.h"
#include "layout_guard.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// RowCodecDelta001 for dynamic Layout
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
class RowCodecDelta001 {
    using RowType = typename LayoutType::RowType;

public:
    RowCodecDelta001() = default;

    /// Copy constructor
    RowCodecDelta001(const RowCodecDelta001& other)
        : guard_(other.layout_ ? LayoutGuard(other.layout_->data()) : LayoutGuard())
        , layout_(other.layout_)
        , head_(other.head_)
        , prev_data_(other.prev_data_)
        , prev_strg_(other.prev_strg_)
        , grad_data_(other.grad_data_)
        , bool_count_(other.bool_count_)
        , head_bits_(other.head_bits_)
        , col_head_offsets_(other.col_head_offsets_)
        , col_head_bits_(other.col_head_bits_)
        , col_data_offsets_(other.col_data_offsets_)
        , col_type_sizes_(other.col_type_sizes_)
        , col_is_signed_(other.col_is_signed_)
        , col_is_float_(other.col_is_float_)
        , str_offsets_(other.str_offsets_)
        , rows_seen_(other.rows_seen_)
    {}

    RowCodecDelta001& operator=(const RowCodecDelta001& other) {
        if (this != &other) {
            LayoutGuard newGuard = other.layout_ ? LayoutGuard(other.layout_->data()) : LayoutGuard();
            guard_.release();
            layout_ = other.layout_;
            head_ = other.head_;
            prev_data_ = other.prev_data_;
            prev_strg_ = other.prev_strg_;
            grad_data_ = other.grad_data_;
            bool_count_ = other.bool_count_;
            head_bits_ = other.head_bits_;
            col_head_offsets_ = other.col_head_offsets_;
            col_head_bits_ = other.col_head_bits_;
            col_data_offsets_ = other.col_data_offsets_;
            col_type_sizes_ = other.col_type_sizes_;
            col_is_signed_ = other.col_is_signed_;
            col_is_float_ = other.col_is_float_;
            str_offsets_ = other.str_offsets_;
            rows_seen_ = other.rows_seen_;
            guard_ = std::move(newGuard);
        }
        return *this;
    }

    RowCodecDelta001(RowCodecDelta001&&) = default;
    RowCodecDelta001& operator=(RowCodecDelta001&&) = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept;

    // ── Bulk operations ──────────────────────────────────────────────────

    /// Serialize using delta/VLE encoding.
    /// Always emits at least the header (no empty-span shortcut) to keep
    /// gradient state synchronised between writer and reader.
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer);

    /// Deserialize a delta-encoded buffer into the row.
    /// Non-const: updates internal state (prev, gradient) for next row.
    void deserialize(
        std::span<const std::byte> buffer, RowType& row);

private:
    LayoutGuard guard_;
    const LayoutType* layout_{nullptr};

    // Wire header bitset: bools + (2+len bits per numeric col) + (1 bit per string col)
    Bitset<> head_;

    // Previous row state
    std::vector<std::byte> prev_data_;          // previous scalar data
    std::vector<std::string> prev_strg_;        // previous string values

    // Gradient state: stores delta between row(n-1) and row(n-2)
    // For integers: gradient_i = prev(n-1) - prev(n-2) (stored as signed 64-bit cast)
    // For floats: gradient is the XOR of bit representations
    // Stored as raw bytes, same layout as prev_data_
    std::vector<std::byte> grad_data_;

    size_t bool_count_{0};
    size_t head_bits_{0};                       // total bit count for header

    // Per non-bool non-string column metadata (ordered by ColumnType enum order):
    std::vector<size_t> col_head_offsets_;       // bit offset in head_ for this column's mode field
    std::vector<size_t> col_head_bits_;          // total header bits for this column (2 + len_bits)
    std::vector<uint32_t> col_data_offsets_;     // byte offset into row.data_ / prev_data_
    std::vector<uint8_t> col_type_sizes_;        // sizeof(T) for this column
    std::vector<bool> col_is_signed_;            // true for INT8..INT64
    std::vector<bool> col_is_float_;             // true for FLOAT, DOUBLE

    // String column offsets (into row.strg_ / prev_strg_)
    std::vector<uint32_t> str_offsets_;

    size_t rows_seen_{0};                        // 0=first row, 1=second row, 2+=gradient valid

    // ── Helpers ─────────────────────────────────────────────────────────

    /// Number of length bits in the header for a given type size.
    static constexpr size_t lenBitsForSize(size_t typeSize) noexcept {
        switch (typeSize) {
            case 1: return 0;
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            default: return 0;  // unreachable for valid column types
        }
    }

    /// Compute number of bytes needed to store a VLE-encoded value.
    static size_t vleByteCount(uint64_t absValue);

    /// Encode a delta value into the buffer. Returns bytes written.
    static size_t encodeDelta(std::byte* dst, uint64_t zigzagValue, size_t byteCount);

    /// Decode a delta value from the buffer. Returns the zigzag-encoded value.
    static uint64_t decodeDelta(const std::byte* src, size_t byteCount);

    /// Compute zigzag-encoded delta for integer types.
    template<typename T>
    static uint64_t computeIntDelta(const std::byte* curr, const std::byte* prev);

    /// Compute XOR delta for float types (Gorilla-style).
    template<typename T>
    static uint64_t computeFloatXorDelta(const std::byte* curr, const std::byte* prev);

    /// Apply integer delta to reconstruct value.
    template<typename T>
    static void applyIntDelta(std::byte* dst, const std::byte* prev, uint64_t zigzagDelta);

    /// Apply float XOR delta to reconstruct value.
    template<typename T>
    static void applyFloatXorDelta(std::byte* dst, const std::byte* prev, uint64_t xorDelta);

    /// Compute integer gradient for FoC prediction.
    template<typename T>
    static void computeIntGradient(std::byte* grad, const std::byte* curr, const std::byte* prev);

    /// Compute float XOR gradient for FoC prediction.
    template<typename T>
    static void computeFloatGradient(std::byte* grad, const std::byte* curr, const std::byte* prev);

    /// Check FoC prediction: does prev + gradient == current?
    template<typename T>
    static bool checkIntFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad);

    /// Check float FoC prediction.
    template<typename T>
    static bool checkFloatFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad);
};

// ────────────────────────────────────────────────────────────────────────────
// Stub specialization for LayoutStatic — delta codec is dynamic-layout only
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
class RowCodecDelta001<LayoutStatic<ColumnTypes...>> {
    using LayoutType = LayoutStatic<ColumnTypes...>;
    using RowType    = typename LayoutType::RowType;

public:
    RowCodecDelta001() = default;
    RowCodecDelta001(const RowCodecDelta001&) = default;
    RowCodecDelta001& operator=(const RowCodecDelta001&) = default;
    RowCodecDelta001(RowCodecDelta001&&) = default;
    RowCodecDelta001& operator=(RowCodecDelta001&&) = default;

    void setup(const LayoutType&) {
        throw std::logic_error("RowCodecDelta001: not supported for LayoutStatic");
    }
    void reset() noexcept {}

    [[nodiscard]] std::span<std::byte> serialize(const RowType&, ByteBuffer&) {
        throw std::logic_error("RowCodecDelta001: not supported for LayoutStatic");
    }
    void deserialize(std::span<const std::byte>, RowType&) {
        throw std::logic_error("RowCodecDelta001: not supported for LayoutStatic");
    }
};

} // namespace bcsv

