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
 * @file row_codec_delta002.h
 * @brief RowCodecDelta002 — Delta row codec with type-grouped loops.
 *
 * Three structural optimisations for high-throughput delta encoding:
 *
 *   1. Combined mode+length header field (fewer bits, no separate mode/length)
 *   2. Type-grouped column loops (compile-time sizeof, zero runtime type dispatch)
 *   3. Float XOR + leading-zero byte stripping (natural VLE of XOR result)
 *
 *   Wire layout: [row_header_][encoded_data...]
 *
 * Head bitset layout (fixed size per layout):
 *   Bits [0 .. boolCount):   Boolean VALUES (same as ZoH)
 *   Per numeric column:      combined code (type-dependent bit width)
 *   Per string column:       1 change-flag bit
 *
 * Combined code per numeric column:
 *   0         = ZoH    — value unchanged from previous row, no data.
 *   1         = FoC    — first-order-constant prediction matches, no data.
 *   2..N+1    = delta  — (code-1) bytes of delta data follow.  N = sizeof(T).
 *
 *   For 1-byte types:  codes 0-2  → 2 header bits
 *   For 2-byte types:  codes 0-3  → 2 header bits
 *   For 4-byte types:  codes 0-5  → 3 header bits
 *   For 8-byte types:  codes 0-9  → 4 header bits
 *
 * Integer deltas use zigzag encoding.
 * Float/double deltas use XOR of bit representations with VLE byte stripping
 * (leading zero bytes from MSB are omitted).  FoC prediction uses arithmetic
 * (p + g == c) for both integer and float types.
 *
 * Columns are grouped by C++ type (following ZoH's forEachScalarType pattern),
 * eliminating all runtime type dispatch in the hot serialize/deserialize loops.
 *
 * First row encodes as delta-from-zero (implicit prev=0), so there is no
 * separate "plain" wire mode — every numeric column is always ZoH, FoC, or
 * delta.
 *
 * @see row_codec_zoh001.h for the type-grouped loop pattern.
 */

#include "../bitset.h"
#include "../byte_buffer.h"
#include "../layout.h"
#include "../layout_guard.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// RowCodecDelta002 for dynamic Layout
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType>
class RowCodecDelta002 {
    using RowType = typename LayoutType::RowType;

public:
    RowCodecDelta002() = default;

    /// Copy constructor: acquires a new guard on the same layout data.
    RowCodecDelta002(const RowCodecDelta002& other)
        : guard_(other.layout_ ? LayoutGuard(other.layout_->data()) : LayoutGuard())
        , layout_(other.layout_)
        , row_header_(other.row_header_)
        , prev_data_(other.prev_data_)
        , prev_strg_(other.prev_strg_)
        , grad_data_(other.grad_data_)
        , bool_count_(other.bool_count_)
        , row_header_str_offset_(other.row_header_str_offset_)
        , cols_uint8_(other.cols_uint8_)
        , cols_uint16_(other.cols_uint16_)
        , cols_uint32_(other.cols_uint32_)
        , cols_uint64_(other.cols_uint64_)
        , cols_int8_(other.cols_int8_)
        , cols_int16_(other.cols_int16_)
        , cols_int32_(other.cols_int32_)
        , cols_int64_(other.cols_int64_)
        , cols_float_(other.cols_float_)
        , cols_double_(other.cols_double_)
        , str_offsets_(other.str_offsets_)
        , rows_seen_(other.rows_seen_)
    {}

    RowCodecDelta002& operator=(const RowCodecDelta002& other) {
        if (this != &other) {
            LayoutGuard newGuard = other.layout_ ? LayoutGuard(other.layout_->data()) : LayoutGuard();
            guard_.release();
            layout_       = other.layout_;
            row_header_         = other.row_header_;
            prev_data_    = other.prev_data_;
            prev_strg_    = other.prev_strg_;
            grad_data_    = other.grad_data_;
            bool_count_   = other.bool_count_;
            row_header_str_offset_= other.row_header_str_offset_;
            cols_uint8_   = other.cols_uint8_;
            cols_uint16_  = other.cols_uint16_;
            cols_uint32_  = other.cols_uint32_;
            cols_uint64_  = other.cols_uint64_;
            cols_int8_    = other.cols_int8_;
            cols_int16_   = other.cols_int16_;
            cols_int32_   = other.cols_int32_;
            cols_int64_   = other.cols_int64_;
            cols_float_   = other.cols_float_;
            cols_double_  = other.cols_double_;
            str_offsets_  = other.str_offsets_;
            rows_seen_    = other.rows_seen_;
            guard_ = std::move(newGuard);
        }
        return *this;
    }

    RowCodecDelta002(RowCodecDelta002&&) = default;
    RowCodecDelta002& operator=(RowCodecDelta002&&) = default;

    // ── Lifecycle ────────────────────────────────────────────────────────
    void setup(const LayoutType& layout);
    void reset() noexcept;

    // ── Bulk operations ──────────────────────────────────────────────────

    /// Serialize using optimised delta/VLE encoding.
    /// Always emits at least the header to keep gradient state synchronised.
    [[nodiscard]] std::span<std::byte> serialize(
        const RowType& row, ByteBuffer& buffer);

    /// Deserialize a delta-encoded buffer into the row.
    /// Non-const: updates internal state (prev, gradient) for next row.
    void deserialize(
        std::span<const std::byte> buffer, RowType& row);

private:
    LayoutGuard guard_;
    const LayoutType* layout_{nullptr};

    // Row header bitset: bools + combined codes + string change flags
    Bitset<> row_header_;

    // Previous row state
    std::vector<std::byte>   prev_data_;     ///< previous scalar data (same layout as row.data_)
    std::vector<std::string> prev_strg_;     ///< previous string values
    std::vector<std::byte>   grad_data_;     ///< gradient (curr - prev for ints, arithmetic for floats)

    size_t bool_count_{0};
    size_t row_header_str_offset_{0};                ///< bit offset where string change flags begin

    /// Per-column metadata for type-grouped iteration.
    struct ColMeta {
        uint32_t dataOffset;                 ///< byte offset into row.data_ / prev_data_
        uint32_t headOffset;                 ///< bit offset in row_header_ for combined code
    };

    // Per-type column vectors — hot-loop iteration with compile-time sizeof(T).
    std::vector<ColMeta> cols_uint8_;
    std::vector<ColMeta> cols_uint16_;
    std::vector<ColMeta> cols_uint32_;
    std::vector<ColMeta> cols_uint64_;
    std::vector<ColMeta> cols_int8_;
    std::vector<ColMeta> cols_int16_;
    std::vector<ColMeta> cols_int32_;
    std::vector<ColMeta> cols_int64_;
    std::vector<ColMeta> cols_float_;
    std::vector<ColMeta> cols_double_;

    // String column indices (into row.strg_ / prev_strg_)
    std::vector<uint32_t> str_offsets_;

    size_t rows_seen_{0};                    ///< 0=first row, >=2 means FoC valid

    // ── Compile-time helpers ─────────────────────────────────────────────

    /// Number of header bits for a column of type T.
    ///   1B → 2 bits (codes 0-2)   2B → 2 bits (codes 0-3)
    ///   4B → 3 bits (codes 0-5)   8B → 4 bits (codes 0-9)
    template<typename T>
    static constexpr size_t headerBits() noexcept {
        if constexpr (sizeof(T) <= 2) return 2;
        else if constexpr (sizeof(T) <= 4) return 3;
        else return 4;
    }

    /// Runtime variant for setup().
    static constexpr size_t headerBitsForSize(size_t typeSize) noexcept {
        switch (typeSize) {
            case 1: return 2;  case 2: return 2;
            case 4: return 3;  case 8: return 4;
            default: return 0;
        }
    }

    /// Invoke fn.template operator()<T>(cols_T) for each scalar ColumnType.
    template<typename Fn>
    void forEachScalarType(Fn&& fn) const;

    // ── Delta encoding helpers (static) ────────────────────────────────

    /// Byte count needed for a VLE-encoded value.
    static size_t vleByteCount(uint64_t absValue);

    /// Little-endian encode of delta bytes.  Returns byteCount.
    static size_t encodeDelta(std::byte* dst, uint64_t value, size_t byteCount);

    /// Little-endian decode of delta bytes.
    static uint64_t decodeDelta(const std::byte* src, size_t byteCount);

    template<typename T> static uint64_t computeIntDelta(const std::byte* curr, const std::byte* prev);
    template<typename T> static uint64_t computeFloatXorDelta(const std::byte* curr, const std::byte* prev);
    template<typename T> static void applyIntDelta(std::byte* dst, const std::byte* prev, uint64_t zigzag);
    template<typename T> static void applyFloatXorDelta(std::byte* dst, const std::byte* prev, uint64_t xor_delta);
    template<typename T> static void computeIntGradient(std::byte* grad, const std::byte* curr, const std::byte* prev);
    template<typename T> static void computeFloatGradient(std::byte* grad, const std::byte* curr, const std::byte* prev);
    template<typename T> static bool checkIntFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad);
    template<typename T> static bool checkFloatFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad);
};

// ────────────────────────────────────────────────────────────────────────────
// Stub specialization for LayoutStatic — delta codec is dynamic-layout only
// ────────────────────────────────────────────────────────────────────────────
template<typename... ColumnTypes>
class RowCodecDelta002<LayoutStatic<ColumnTypes...>> {
    using LayoutType = LayoutStatic<ColumnTypes...>;
    using RowType    = typename LayoutType::RowType;

public:
    RowCodecDelta002() = default;
    RowCodecDelta002(const RowCodecDelta002&) = default;
    RowCodecDelta002& operator=(const RowCodecDelta002&) = default;
    RowCodecDelta002(RowCodecDelta002&&) = default;
    RowCodecDelta002& operator=(RowCodecDelta002&&) = default;

    void setup(const LayoutType&) {
        throw std::logic_error("RowCodecDelta002: not supported for LayoutStatic");
    }
    void reset() noexcept {}

    [[nodiscard]] std::span<std::byte> serialize(const RowType&, ByteBuffer&) {
        throw std::logic_error("RowCodecDelta002: not supported for LayoutStatic");
    }
    void deserialize(std::span<const std::byte>, RowType&) {
        throw std::logic_error("RowCodecDelta002: not supported for LayoutStatic");
    }
};

} // namespace bcsv
