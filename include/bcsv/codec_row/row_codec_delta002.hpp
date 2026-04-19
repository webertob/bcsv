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
 * @file row_codec_delta002.hpp
 * @brief RowCodecDelta002 template implementations.
 *
 * Type-grouped delta codec with combined header codes and zero runtime
 * type dispatch.  See row_codec_delta002.h for the wire-format specification.
 */

#include "row_codec_delta002.h"
#include "../row.hpp"
#include "../vle.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <type_traits>

namespace bcsv {

// ════════════════════════════════════════════════════════════════════════════
// Static helpers
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType>
size_t RowCodecDelta002<LayoutType>::vleByteCount(uint64_t absValue) {
    if (absValue == 0) return 1;
    size_t bits = std::bit_width(absValue);
    return (bits + 7) / 8;
}

template<typename LayoutType>
size_t RowCodecDelta002<LayoutType>::encodeDelta(std::byte* dst, uint64_t value, size_t byteCount) {
    for (size_t i = 0; i < byteCount; ++i)
        dst[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFF);
    return byteCount;
}

template<typename LayoutType>
uint64_t RowCodecDelta002<LayoutType>::decodeDelta(const std::byte* src, size_t byteCount) {
    uint64_t result = 0;
    for (size_t i = 0; i < byteCount; ++i)
        result |= static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << (i * 8);
    return result;
}

// ── Integer delta helpers ─────────────────────────────────────────────────

template<typename LayoutType>
template<typename T>
uint64_t RowCodecDelta002<LayoutType>::computeIntDelta(const std::byte* curr, const std::byte* prev) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    U delta_u = static_cast<U>(c) - static_cast<U>(p);
    S delta;
    std::memcpy(&delta, &delta_u, sizeof(S));
    return static_cast<uint64_t>(zigzagEncode(delta));
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta002<LayoutType>::applyIntDelta(std::byte* dst, const std::byte* prev, uint64_t zigzag) {
    T p;
    std::memcpy(&p, prev, sizeof(T));
    using U = std::make_unsigned_t<T>;
    U delta_u;
    { auto decoded = zigzagDecode(static_cast<U>(zigzag)); std::memcpy(&delta_u, &decoded, sizeof(U)); }
    T result = static_cast<T>(static_cast<U>(p) + delta_u);
    std::memcpy(dst, &result, sizeof(T));
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta002<LayoutType>::computeIntGradient(std::byte* grad, const std::byte* curr, const std::byte* prev) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    U g_u = static_cast<U>(c) - static_cast<U>(p);
    S g;
    std::memcpy(&g, &g_u, sizeof(S));
    std::memcpy(grad, &g, sizeof(T));
}

template<typename LayoutType>
template<typename T>
bool RowCodecDelta002<LayoutType>::checkIntFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    S g;
    std::memcpy(&g, grad, sizeof(T));
    U g_u;
    std::memcpy(&g_u, &g, sizeof(U));
    T predicted = static_cast<T>(static_cast<U>(p) + g_u);
    return (c == predicted);
}

// ── Float/double XOR delta helpers ────────────────────────────────────────

template<typename LayoutType>
template<typename T>
uint64_t RowCodecDelta002<LayoutType>::computeFloatXorDelta(const std::byte* curr, const std::byte* prev) {
    static_assert(std::is_floating_point_v<T>);
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    U c_bits, p_bits;
    std::memcpy(&c_bits, curr, sizeof(T));
    std::memcpy(&p_bits, prev, sizeof(T));
    return static_cast<uint64_t>(c_bits ^ p_bits);
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta002<LayoutType>::applyFloatXorDelta(std::byte* dst, const std::byte* prev, uint64_t xor_delta) {
    static_assert(std::is_floating_point_v<T>);
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    U p_bits;
    std::memcpy(&p_bits, prev, sizeof(T));
    U result_bits = p_bits ^ static_cast<U>(xor_delta);
    std::memcpy(dst, &result_bits, sizeof(T));
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta002<LayoutType>::computeFloatGradient(std::byte* grad, const std::byte* curr, const std::byte* prev) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    T g = c - p;
    std::memcpy(grad, &g, sizeof(T));
}

template<typename LayoutType>
template<typename T>
bool RowCodecDelta002<LayoutType>::checkFloatFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad) {
    T c, p, g;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    std::memcpy(&g, grad, sizeof(T));
    T predicted = p + g;
    return std::memcmp(&c, &predicted, sizeof(T)) == 0;
}

// ════════════════════════════════════════════════════════════════════════════
// forEachScalarType — dispatch over all 10 scalar ColumnTypes
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType>
template<typename Fn>
void RowCodecDelta002<LayoutType>::forEachScalarType(Fn&& fn) const {
    fn.template operator()<uint8_t>(cols_uint8_);
    fn.template operator()<uint16_t>(cols_uint16_);
    fn.template operator()<uint32_t>(cols_uint32_);
    fn.template operator()<uint64_t>(cols_uint64_);
    fn.template operator()<int8_t>(cols_int8_);
    fn.template operator()<int16_t>(cols_int16_);
    fn.template operator()<int32_t>(cols_int32_);
    fn.template operator()<int64_t>(cols_int64_);
    fn.template operator()<float>(cols_float_);
    fn.template operator()<double>(cols_double_);
}

// ════════════════════════════════════════════════════════════════════════════
// setup / reset
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType>
void RowCodecDelta002<LayoutType>::setup(const LayoutType& layout) {
    guard_  = LayoutGuard(layout.data());
    layout_ = &layout;

    const size_t colCount = layout.columnCount();
    const auto&  types    = layout.columnTypes();
    const auto&  offsets  = layout.columnOffsets();

    bool_count_ = layout.columnCount(ColumnType::BOOL);

    // Clear per-type vectors
    cols_uint8_.clear();  cols_uint16_.clear();
    cols_uint32_.clear(); cols_uint64_.clear();
    cols_int8_.clear();   cols_int16_.clear();
    cols_int32_.clear();  cols_int64_.clear();
    cols_float_.clear();  cols_double_.clear();
    str_offsets_.clear();

    // Build per-type ColMeta vectors in ColumnType enum order.
    // Each numeric column gets headerBitsForSize(sizeof(T)) bits in the header.
    static constexpr ColumnType TYPE_ORDER[] = {
        ColumnType::UINT8,  ColumnType::UINT16, ColumnType::UINT32, ColumnType::UINT64,
        ColumnType::INT8,   ColumnType::INT16,  ColumnType::INT32,  ColumnType::INT64,
        ColumnType::FLOAT,  ColumnType::DOUBLE
    };

    size_t headPos = bool_count_;  // bools occupy bits [0, bool_count_)

    for (auto type : TYPE_ORDER) {
        const size_t hbits = headerBitsForSize(sizeOf(type));
        for (size_t i = 0; i < colCount; ++i) {
            if (types[i] != type) continue;

            ColMeta cm;
            cm.dataOffset = offsets[i];
            cm.headOffset = static_cast<uint32_t>(headPos);
            headPos += hbits;

            switch (type) {
                case ColumnType::UINT8:  cols_uint8_.push_back(cm);  break;
                case ColumnType::UINT16: cols_uint16_.push_back(cm); break;
                case ColumnType::UINT32: cols_uint32_.push_back(cm); break;
                case ColumnType::UINT64: cols_uint64_.push_back(cm); break;
                case ColumnType::INT8:   cols_int8_.push_back(cm);   break;
                case ColumnType::INT16:  cols_int16_.push_back(cm);  break;
                case ColumnType::INT32:  cols_int32_.push_back(cm);  break;
                case ColumnType::INT64:  cols_int64_.push_back(cm);  break;
                case ColumnType::FLOAT:  cols_float_.push_back(cm);  break;
                case ColumnType::DOUBLE: cols_double_.push_back(cm); break;
                default: break;
            }
        }
    }

    // String columns: 1 change-flag bit each
    row_header_str_offset_ = headPos;
    for (size_t i = 0; i < colCount; ++i) {
        if (types[i] == ColumnType::STRING) {
            str_offsets_.push_back(offsets[i]);
            headPos++;
        }
    }

    row_header_.resize(headPos);
    row_header_.reset();

    prev_data_.clear();
    prev_strg_.clear();
    grad_data_.clear();
    rows_seen_ = 0;
}

template<typename LayoutType>
void RowCodecDelta002<LayoutType>::reset() noexcept {
    rows_seen_ = 0;
}

// ════════════════════════════════════════════════════════════════════════════
// serialize
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType>
std::span<std::byte> RowCodecDelta002<LayoutType>::serialize(
    const RowType& row, ByteBuffer& buffer)
{
    const size_t headBytes  = row_header_.sizeBytes();
    const size_t numStrCols = str_offsets_.size();

    // Reset header to all zeros
    row_header_.reset();

    // ── Bool values ──────────────────────────────────────────────────────
    if (bool_count_ > 0) {
        assignRange(row_header_, 0, row.bits_, 0, bool_count_);
    }

    // ── First-row state initialisation ───────────────────────────────────
    if (rows_seen_ == 0) {
        prev_data_.assign(row.data_.size(), std::byte{0});
        prev_strg_.resize(row.strg_.size());
        grad_data_.assign(row.data_.size(), std::byte{0});
    }

    // ── Pessimistic buffer allocation ────────────────────────────────────
    size_t maxSize = headBytes;
    forEachScalarType([&]<typename T>(const std::vector<ColMeta>& cols) {
        maxSize += cols.size() * sizeof(T);
    });
    for (size_t s = 0; s < numStrCols; ++s)
        maxSize += 2 + row.strg_[str_offsets_[s]].size();
    buffer.resize(maxSize);

    size_t bufIdx = headBytes;

    // ── Numeric columns (type-grouped, zero runtime dispatch) ────────────
    forEachScalarType([&]<typename T>(const std::vector<ColMeta>& cols) {
        constexpr size_t HB = headerBits<T>();

        for (const auto& col : cols) {
            const uint32_t off = col.dataOffset;

            // ── ZoH check ────────────────────────────────────────────────
            if (std::memcmp(&row.data_[off], &prev_data_[off], sizeof(T)) == 0) {
                row_header_.encode(col.headOffset, HB, 0);         // code 0 = ZoH
                std::memset(&grad_data_[off], 0, sizeof(T));
                continue;
            }

            // ── FoC check (valid from row ≥ 2) ──────────────────────────
            if (rows_seen_ >= 2) {
                bool foc;
                if constexpr (std::is_floating_point_v<T>)
                    foc = checkFloatFoC<T>(&row.data_[off], &prev_data_[off], &grad_data_[off]);
                else
                    foc = checkIntFoC<T>(&row.data_[off], &prev_data_[off], &grad_data_[off]);

                if (foc) {
                    row_header_.encode(col.headOffset, HB, 1);     // code 1 = FoC
                    std::memcpy(&prev_data_[off], &row.data_[off], sizeof(T));
                    // Gradient unchanged — prediction is consistent
                    continue;
                }
            }

            // ── Delta encoding ───────────────────────────────────────────
            uint64_t delta;
            if constexpr (std::is_floating_point_v<T>)
                delta = computeFloatXorDelta<T>(&row.data_[off], &prev_data_[off]);
            else
                delta = computeIntDelta<T>(&row.data_[off], &prev_data_[off]);

            size_t deltaBytes = vleByteCount(delta);
            if (deltaBytes > sizeof(T)) deltaBytes = sizeof(T);  // safety clamp

            const uint8_t code = static_cast<uint8_t>(deltaBytes + 1);  // code 2..sizeof(T)+1
            row_header_.encode(col.headOffset, HB, code);
            encodeDelta(&buffer[bufIdx], delta, deltaBytes);
            bufIdx += deltaBytes;

            // ── Update gradient & prev ───────────────────────────────────
            if constexpr (std::is_floating_point_v<T>)
                computeFloatGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);
            else
                computeIntGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);

            std::memcpy(&prev_data_[off], &row.data_[off], sizeof(T));
        }
    });

    // ── String columns ───────────────────────────────────────────────────
    for (size_t s = 0; s < numStrCols; ++s) {
        const uint32_t strIdx = str_offsets_[s];
        const bool changed = (rows_seen_ == 0) || (row.strg_[strIdx] != prev_strg_[strIdx]);

        if (changed) {
            row_header_[row_header_str_offset_ + s] = true;
            prev_strg_[strIdx] = row.strg_[strIdx];

            const auto& str = row.strg_[strIdx];
            uint16_t len = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
            if (bufIdx + 2 + len > buffer.size()) buffer.resize(bufIdx + 2 + len);
            std::memcpy(&buffer[bufIdx], &len, 2);
            bufIdx += 2;
            if (len > 0) { std::memcpy(&buffer[bufIdx], str.data(), len); bufIdx += len; }
        } else {
            row_header_[row_header_str_offset_ + s] = false;
        }
    }

    row_header_.writeTo(buffer.data(), headBytes);
    assert(bufIdx <= buffer.size() && "Delta002 serialize: bufIdx exceeded pessimistic allocation");
    buffer.resize(bufIdx);
    rows_seen_++;
    return std::span<std::byte>(buffer.data(), bufIdx);
}

// ════════════════════════════════════════════════════════════════════════════
// deserialize
// ════════════════════════════════════════════════════════════════════════════

template<typename LayoutType>
void RowCodecDelta002<LayoutType>::deserialize(
    std::span<const std::byte> buffer, RowType& row)
{
    const size_t headBytes  = row_header_.sizeBytes();
    const size_t numStrCols = str_offsets_.size();

    if (buffer.size() < headBytes)
        throw std::runtime_error(
            "RowCodecDelta002::deserialize() failed! Buffer too small for head Bitset.");

    row_header_.readFrom(buffer.data(), headBytes);

    // ── Bool values ──────────────────────────────────────────────────────
    if (bool_count_ > 0) {
        assignRange(row.bits_, 0, row_header_, 0, bool_count_);
    }

    // ── First-row state initialisation ───────────────────────────────────
    if (rows_seen_ == 0) {
        prev_data_.assign(row.data_.size(), std::byte{0});
        prev_strg_.assign(row.strg_.size(), std::string{});
        grad_data_.assign(row.data_.size(), std::byte{0});
    }

    size_t dataOff = headBytes;

    // ── Numeric columns (type-grouped, zero runtime dispatch) ────────────
    forEachScalarType([&]<typename T>(const std::vector<ColMeta>& cols) {
        constexpr size_t HB = headerBits<T>();

        for (const auto& col : cols) {
            const uint32_t off = col.dataOffset;
            const uint8_t code = row_header_.decode(col.headOffset, HB);

            // ── code 0: ZoH ──────────────────────────────────────────────
            if (code == 0) {
                // Copy prev to row (necessary for first-row correctness
                // when prev is zero-initialised and value happens to be 0).
                std::memcpy(&row.data_[off], &prev_data_[off], sizeof(T));
                std::memset(&grad_data_[off], 0, sizeof(T));
                continue;
            }

            // ── code 1: FoC ──────────────────────────────────────────────
            if (code == 1) {
                if constexpr (std::is_floating_point_v<T>) {
                    T p, g;
                    std::memcpy(&p, &prev_data_[off], sizeof(T));
                    std::memcpy(&g, &grad_data_[off], sizeof(T));
                    T result = p + g;
                    std::memcpy(&row.data_[off], &result, sizeof(T));
                    std::memcpy(&prev_data_[off], &result, sizeof(T));
                } else {
                    using U = std::make_unsigned_t<T>;
                    using S = std::make_signed_t<T>;
                    T p; S g;
                    std::memcpy(&p, &prev_data_[off], sizeof(T));
                    std::memcpy(&g, &grad_data_[off], sizeof(T));
                    U g_u;
                    std::memcpy(&g_u, &g, sizeof(U));
                    T r = static_cast<T>(static_cast<U>(p) + g_u);
                    std::memcpy(&row.data_[off], &r, sizeof(T));
                    std::memcpy(&prev_data_[off], &r, sizeof(T));
                }
                // Gradient unchanged
                continue;
            }

            // ── code ≥ 2: delta with (code-1) bytes ─────────────────────
            const size_t deltaBytes = code - 1;

            if (dataOff + deltaBytes > buffer.size())
                throw std::runtime_error(
                    "RowCodecDelta002::deserialize() failed! Buffer too small for delta.");

            uint64_t deltaValue = decodeDelta(&buffer[dataOff], deltaBytes);

            if constexpr (std::is_floating_point_v<T>)
                applyFloatXorDelta<T>(&row.data_[off], &prev_data_[off], deltaValue);
            else
                applyIntDelta<T>(&row.data_[off], &prev_data_[off], deltaValue);

            // Update gradient
            if constexpr (std::is_floating_point_v<T>)
                computeFloatGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);
            else
                computeIntGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);

            std::memcpy(&prev_data_[off], &row.data_[off], sizeof(T));
            dataOff += deltaBytes;
        }
    });

    // ── String columns ───────────────────────────────────────────────────
    for (size_t s = 0; s < numStrCols; ++s) {
        if (row_header_[row_header_str_offset_ + s]) {
            const uint32_t strIdx = str_offsets_[s];

            if (dataOff + 2 > buffer.size())
                throw std::runtime_error(
                    "RowCodecDelta002::deserialize() failed! Buffer too small for string length.");
            uint16_t len;
            std::memcpy(&len, &buffer[dataOff], 2);
            dataOff += 2;

            if (len > 0) {
                if (dataOff + len > buffer.size())
                    throw std::runtime_error(
                        "RowCodecDelta002::deserialize() failed! Buffer too small for string payload.");
                row.strg_[strIdx].assign(reinterpret_cast<const char*>(&buffer[dataOff]), len);
                dataOff += len;
            } else {
                row.strg_[strIdx].clear();
            }
            prev_strg_[strIdx] = row.strg_[strIdx];
        }
    }

    rows_seen_++;
}

// ════════════════════════════════════════════════════════════════════════════
// LayoutStatic specialization — implementation
//
// Tuple-based state with type-grouped fold-expression processing.
// prev_data_ / grad_data_ are std::tuple<ColumnTypes...> — same type as
// RowStatic::data_. No copies between tuple and flat byte arrays.
// Per-type constexpr index arrays drive fold expansions that process
// same-type columns together for branch prediction and ILP.
// ════════════════════════════════════════════════════════════════════════════

// ── forEachScalarType ────────────────────────────────────────────────────

template<typename... ColumnTypes>
template<typename Fn>
void RowCodecDelta002<LayoutStatic<ColumnTypes...>>::forEachScalarType(Fn&& fn) {
    if constexpr (N_UINT8  > 0) fn.template operator()<uint8_t,  IDX_UINT8,  N_UINT8>();
    if constexpr (N_UINT16 > 0) fn.template operator()<uint16_t, IDX_UINT16, N_UINT16>();
    if constexpr (N_UINT32 > 0) fn.template operator()<uint32_t, IDX_UINT32, N_UINT32>();
    if constexpr (N_UINT64 > 0) fn.template operator()<uint64_t, IDX_UINT64, N_UINT64>();
    if constexpr (N_INT8   > 0) fn.template operator()<int8_t,   IDX_INT8,   N_INT8>();
    if constexpr (N_INT16  > 0) fn.template operator()<int16_t,  IDX_INT16,  N_INT16>();
    if constexpr (N_INT32  > 0) fn.template operator()<int32_t,  IDX_INT32,  N_INT32>();
    if constexpr (N_INT64  > 0) fn.template operator()<int64_t,  IDX_INT64,  N_INT64>();
    if constexpr (N_FLOAT  > 0) fn.template operator()<float,    IDX_FLOAT,  N_FLOAT>();
    if constexpr (N_DOUBLE > 0) fn.template operator()<double,   IDX_DOUBLE, N_DOUBLE>();
}

// ── Delta encoding helpers (typed — no byte-pointer indirection) ─────────

template<typename... ColumnTypes>
template<typename T>
uint64_t RowCodecDelta002<LayoutStatic<ColumnTypes...>>::computeIntDelta(
    const T& curr, const T& prev)
{
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    U delta_u = static_cast<U>(curr) - static_cast<U>(prev);
    S delta;
    std::memcpy(&delta, &delta_u, sizeof(S));
    return static_cast<uint64_t>(zigzagEncode(delta));
}

template<typename... ColumnTypes>
template<typename T>
T RowCodecDelta002<LayoutStatic<ColumnTypes...>>::applyIntDelta(
    const T& prev, uint64_t zigzag)
{
    using U = std::make_unsigned_t<T>;
    U delta_u;
    { auto decoded = zigzagDecode(static_cast<U>(zigzag)); std::memcpy(&delta_u, &decoded, sizeof(U)); }
    return static_cast<T>(static_cast<U>(prev) + delta_u);
}

template<typename... ColumnTypes>
template<typename T>
T RowCodecDelta002<LayoutStatic<ColumnTypes...>>::computeIntGradient(
    const T& curr, const T& prev)
{
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    U g_u = static_cast<U>(curr) - static_cast<U>(prev);
    S g;
    std::memcpy(&g, &g_u, sizeof(S));
    return g;
}

template<typename... ColumnTypes>
template<typename T>
bool RowCodecDelta002<LayoutStatic<ColumnTypes...>>::checkIntFoC(
    const T& curr, const T& prev, const T& grad)
{
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    S g;
    std::memcpy(&g, &grad, sizeof(S));
    U g_u;
    std::memcpy(&g_u, &g, sizeof(U));
    T predicted = static_cast<T>(static_cast<U>(prev) + g_u);
    return (curr == predicted);
}

template<typename... ColumnTypes>
template<typename T>
uint64_t RowCodecDelta002<LayoutStatic<ColumnTypes...>>::computeFloatXorDelta(
    const T& curr, const T& prev)
{
    static_assert(std::is_floating_point_v<T>);
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    U c_bits, p_bits;
    std::memcpy(&c_bits, &curr, sizeof(T));
    std::memcpy(&p_bits, &prev, sizeof(T));
    return static_cast<uint64_t>(c_bits ^ p_bits);
}

template<typename... ColumnTypes>
template<typename T>
T RowCodecDelta002<LayoutStatic<ColumnTypes...>>::applyFloatXorDelta(
    const T& prev, uint64_t xor_delta)
{
    static_assert(std::is_floating_point_v<T>);
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    U p_bits;
    std::memcpy(&p_bits, &prev, sizeof(T));
    U result_bits = p_bits ^ static_cast<U>(xor_delta);
    T result;
    std::memcpy(&result, &result_bits, sizeof(T));
    return result;
}

template<typename... ColumnTypes>
template<typename T>
T RowCodecDelta002<LayoutStatic<ColumnTypes...>>::computeFloatGradient(
    const T& curr, const T& prev)
{
    return curr - prev;
}

template<typename... ColumnTypes>
template<typename T>
bool RowCodecDelta002<LayoutStatic<ColumnTypes...>>::checkFloatFoC(
    const T& curr, const T& prev, const T& grad)
{
    T predicted = prev + grad;
    return std::memcmp(&curr, &predicted, sizeof(T)) == 0;
}

// ── setup / reset ────────────────────────────────────────────────────────

template<typename... ColumnTypes>
void RowCodecDelta002<LayoutStatic<ColumnTypes...>>::setup(const LayoutType& layout) {
    layout_ = &layout;
    prev_data_ = TupleType{};
    grad_data_ = TupleType{};
    rows_seen_ = 0;
}

template<typename... ColumnTypes>
void RowCodecDelta002<LayoutStatic<ColumnTypes...>>::reset() noexcept {
    rows_seen_ = 0;
}

// ── serialize ────────────────────────────────────────────────────────────

template<typename... ColumnTypes>
std::span<std::byte>
RowCodecDelta002<LayoutStatic<ColumnTypes...>>::serialize(
    const RowType& row, ByteBuffer& buffer)
{
    const size_t headBytes = row_header_.sizeBytes();

    // Reset header to all zeros
    row_header_.reset();

    // ── Bool values ──────────────────────────────────────────────────────
    if constexpr (BOOL_COUNT > 0) {
        [&]<size_t... S>(std::index_sequence<S...>) {
            ((row_header_[ROW_HEADER_BIT_INDEX[IDX_BOOL[S]]] =
                  std::get<IDX_BOOL[S]>(row.data_)), ...);
        }(std::make_index_sequence<BOOL_COUNT>{});
    }

    // ── First-row state initialisation ───────────────────────────────────
    if (rows_seen_ == 0) {
        prev_data_ = TupleType{};
        grad_data_ = TupleType{};
    }

    // ── Pessimistic buffer allocation ────────────────────────────────────
    size_t maxSize = headBytes + MAX_NUMERIC_DATA_SIZE;
    if constexpr (STRING_COUNT > 0) {
        [&]<size_t... S>(std::index_sequence<S...>) {
            ((maxSize += 2 + std::get<IDX_STRING[S]>(row.data_).size()), ...);
        }(std::make_index_sequence<STRING_COUNT>{});
    }
    buffer.resize(maxSize);

    size_t bufIdx = headBytes;

    // ── Numeric columns (type-grouped fold expansion) ────────────────────
    forEachScalarType([&]<typename T, const auto& Indices, size_t N>() {
        [[maybe_unused]] constexpr size_t HB = headerBits<T>();

        [&]<size_t... S>(std::index_sequence<S...>) {
            ([&] {
                constexpr size_t ColIdx = Indices[S];
                const T& curr = std::get<ColIdx>(row.data_);
                T& prev = std::get<ColIdx>(prev_data_);
                T& grad = std::get<ColIdx>(grad_data_);

                // ── ZoH check ────────────────────────────────────────────
                if (curr == prev) {
                    row_header_.encode(ROW_HEADER_BIT_INDEX[ColIdx], HB, 0);
                    grad = T{};
                    return;
                }

                // ── FoC check (valid from row ≥ 2) ──────────────────────
                if (rows_seen_ >= 2) {
                    bool foc;
                    if constexpr (std::is_floating_point_v<T>)
                        foc = checkFloatFoC(curr, prev, grad);
                    else
                        foc = checkIntFoC(curr, prev, grad);

                    if (foc) {
                        row_header_.encode(ROW_HEADER_BIT_INDEX[ColIdx], HB, 1);
                        return;
                    }
                }

                // ── Delta encoding ───────────────────────────────────────
                uint64_t delta;
                if constexpr (std::is_floating_point_v<T>)
                    delta = computeFloatXorDelta(curr, prev);
                else
                    delta = computeIntDelta(curr, prev);

                size_t deltaBytes = vleByteCount(delta);
                if (deltaBytes > sizeof(T)) deltaBytes = sizeof(T);

                const uint8_t code = static_cast<uint8_t>(deltaBytes + 1);
                row_header_.encode(ROW_HEADER_BIT_INDEX[ColIdx], HB, code);
                encodeDelta(&buffer[bufIdx], delta, deltaBytes);
                bufIdx += deltaBytes;

                // Update gradient
                if constexpr (std::is_floating_point_v<T>)
                    grad = computeFloatGradient(curr, prev);
                else
                    grad = computeIntGradient(curr, prev);
            }(), ...);
        }(std::make_index_sequence<N>{});
    });

    // ── String columns ───────────────────────────────────────────────────
    if constexpr (STRING_COUNT > 0) {
        [&]<size_t... S>(std::index_sequence<S...>) {
            ([&] {
                constexpr size_t ColIdx = IDX_STRING[S];
                const auto& currStr = std::get<ColIdx>(row.data_);
                const auto& prevStr = std::get<ColIdx>(prev_data_);

                const bool changed = (rows_seen_ == 0) || (currStr != prevStr);

                if (changed) {
                    row_header_[ROW_HEADER_BIT_INDEX[ColIdx]] = true;

                    uint16_t len = static_cast<uint16_t>(
                        std::min(currStr.size(), MAX_STRING_LENGTH));
                    if (bufIdx + 2 + len > buffer.size())
                        buffer.resize(bufIdx + 2 + len);
                    std::memcpy(&buffer[bufIdx], &len, 2);
                    bufIdx += 2;
                    if (len > 0) {
                        std::memcpy(&buffer[bufIdx], currStr.data(), len);
                        bufIdx += len;
                    }
                } else {
                    row_header_[ROW_HEADER_BIT_INDEX[ColIdx]] = false;
                }
            }(), ...);
        }(std::make_index_sequence<STRING_COUNT>{});
    }

    // Write header to buffer
    std::memcpy(buffer.data(), row_header_.data(), headBytes);

    // Bulk-copy current row into prev state (single tuple assignment)
    prev_data_ = row.data_;

    buffer.resize(bufIdx);

    rows_seen_++;
    return std::span<std::byte>(buffer.data(), bufIdx);
}

// ── deserialize ──────────────────────────────────────────────────────────

template<typename... ColumnTypes>
void RowCodecDelta002<LayoutStatic<ColumnTypes...>>::deserialize(
    std::span<const std::byte> buffer, RowType& row)
{
    const size_t headBytes = row_header_.sizeBytes();

    if (buffer.size() < headBytes)
        throw std::runtime_error(
            "RowCodecDelta002::deserialize() failed! Buffer too small for head Bitset.");

    std::memcpy(row_header_.data(), buffer.data(), headBytes);

    // ── First-row state initialisation ───────────────────────────────────
    if (rows_seen_ == 0) {
        prev_data_ = TupleType{};
        grad_data_ = TupleType{};
    }

    // ── Bool values ──────────────────────────────────────────────────────
    if constexpr (BOOL_COUNT > 0) {
        [&]<size_t... S>(std::index_sequence<S...>) {
            ((std::get<IDX_BOOL[S]>(prev_data_) =
                  row_header_[ROW_HEADER_BIT_INDEX[IDX_BOOL[S]]]), ...);
        }(std::make_index_sequence<BOOL_COUNT>{});
    }

    size_t dataOff = headBytes;

    // ── Numeric columns (type-grouped fold expansion) ────────────────────
    forEachScalarType([&]<typename T, const auto& Indices, size_t N>() {
        [[maybe_unused]] constexpr size_t HB = headerBits<T>();

        [&]<size_t... S>(std::index_sequence<S...>) {
            ([&] {
                constexpr size_t ColIdx = Indices[S];
                T& prev = std::get<ColIdx>(prev_data_);
                T& grad = std::get<ColIdx>(grad_data_);

                const uint8_t code = row_header_.decode(ROW_HEADER_BIT_INDEX[ColIdx], HB);

                // ── code 0: ZoH ──────────────────────────────────────────
                if (code == 0) {
                    grad = T{};
                    return;
                }

                // ── code 1: FoC ──────────────────────────────────────────
                if (code == 1) {
                    if constexpr (std::is_floating_point_v<T>) {
                        prev = prev + grad;
                    } else {
                        using U = std::make_unsigned_t<T>;
                        using Signed = std::make_signed_t<T>;
                        Signed g;
                        std::memcpy(&g, &grad, sizeof(Signed));
                        U g_u;
                        std::memcpy(&g_u, &g, sizeof(U));
                        prev = static_cast<T>(static_cast<U>(prev) + g_u);
                    }
                    return;
                }

                // ── code ≥ 2: delta with (code-1) bytes ─────────────────
                const size_t deltaBytes = code - 1;

                if (dataOff + deltaBytes > buffer.size())
                    throw std::runtime_error(
                        "RowCodecDelta002::deserialize() failed! Buffer too small for delta.");

                uint64_t deltaValue = decodeDelta(&buffer[dataOff], deltaBytes);

                T old_prev = prev;
                if constexpr (std::is_floating_point_v<T>)
                    prev = applyFloatXorDelta(prev, deltaValue);
                else
                    prev = applyIntDelta(prev, deltaValue);

                // Update gradient
                if constexpr (std::is_floating_point_v<T>)
                    grad = computeFloatGradient(prev, old_prev);
                else
                    grad = computeIntGradient(prev, old_prev);

                dataOff += deltaBytes;
            }(), ...);
        }(std::make_index_sequence<N>{});
    });

    // ── String columns ───────────────────────────────────────────────────
    if constexpr (STRING_COUNT > 0) {
        [&]<size_t... S>(std::index_sequence<S...>) {
            ([&] {
                constexpr size_t ColIdx = IDX_STRING[S];

                if (row_header_[ROW_HEADER_BIT_INDEX[ColIdx]]) {
                    if (dataOff + 2 > buffer.size())
                        throw std::runtime_error(
                            "RowCodecDelta002::deserialize() failed! Buffer too small for string length.");
                    uint16_t len;
                    std::memcpy(&len, &buffer[dataOff], 2);
                    dataOff += 2;

                    if (len > 0) {
                        if (dataOff + len > buffer.size())
                            throw std::runtime_error(
                                "RowCodecDelta002::deserialize() failed! Buffer too small for string payload.");
                        std::get<ColIdx>(prev_data_).assign(
                            reinterpret_cast<const char*>(&buffer[dataOff]), len);
                        dataOff += len;
                    } else {
                        std::get<ColIdx>(prev_data_).clear();
                    }
                }
                // unchanged strings: prev_data_ already holds correct value
            }(), ...);
        }(std::make_index_sequence<STRING_COUNT>{});
    }

    // Bulk-copy decoded prev state into output row
    row.data_ = prev_data_;

    rows_seen_++;
}

} // namespace bcsv
