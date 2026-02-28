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
 * @file row_codec_delta001.hpp
 * @brief RowCodecDelta001 template implementations.
 */

#include "row_codec_delta001.h"
#include "row.hpp"
#include "vle.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <type_traits>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// Static helpers
// ────────────────────────────────────────────────────────────────────────────

template<typename LayoutType>
size_t RowCodecDelta001<LayoutType>::vleByteCount(uint64_t absValue) {
    if (absValue == 0) return 1;
    size_t bits = std::bit_width(absValue);
    return (bits + 7) / 8;
}

template<typename LayoutType>
size_t RowCodecDelta001<LayoutType>::encodeDelta(std::byte* dst, uint64_t zigzagValue, size_t byteCount) {
    for (size_t i = 0; i < byteCount; ++i) {
        dst[i] = static_cast<std::byte>((zigzagValue >> (i * 8)) & 0xFF);
    }
    return byteCount;
}

template<typename LayoutType>
uint64_t RowCodecDelta001<LayoutType>::decodeDelta(const std::byte* src, size_t byteCount) {
    uint64_t result = 0;
    for (size_t i = 0; i < byteCount; ++i) {
        result |= static_cast<uint64_t>(static_cast<uint8_t>(src[i])) << (i * 8);
    }
    return result;
}

// ── Integer delta helpers ─────────────────────────────────────────────────

template<typename LayoutType>
template<typename T>
uint64_t RowCodecDelta001<LayoutType>::computeIntDelta(const std::byte* curr, const std::byte* prev) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    // Perform subtraction in unsigned domain to avoid signed overflow UB,
    // then reinterpret as signed for zigzag encoding.
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    U delta_u = static_cast<U>(c) - static_cast<U>(p);
    S delta;
    std::memcpy(&delta, &delta_u, sizeof(S));
    return static_cast<uint64_t>(zigzagEncode(delta));
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta001<LayoutType>::applyIntDelta(std::byte* dst, const std::byte* prev, uint64_t zigzagDelta) {
    T p;
    std::memcpy(&p, prev, sizeof(T));
    using U = std::make_unsigned_t<T>;
    // Decode zigzag, then add in unsigned domain to avoid signed overflow UB.
    U delta_u;
    { auto decoded = zigzagDecode(static_cast<U>(zigzagDelta)); std::memcpy(&delta_u, &decoded, sizeof(U)); }
    T result = static_cast<T>(static_cast<U>(p) + delta_u);
    std::memcpy(dst, &result, sizeof(T));
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta001<LayoutType>::computeIntGradient(std::byte* grad, const std::byte* curr, const std::byte* prev) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    // Subtract in unsigned domain to avoid signed overflow UB.
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    U g_u = static_cast<U>(c) - static_cast<U>(p);
    S g;
    std::memcpy(&g, &g_u, sizeof(S));
    std::memcpy(grad, &g, sizeof(T));
}

template<typename LayoutType>
template<typename T>
bool RowCodecDelta001<LayoutType>::checkIntFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    // Add gradient in unsigned domain to avoid signed overflow UB.
    using U = std::make_unsigned_t<T>;
    using S = std::make_signed_t<T>;
    S g;
    std::memcpy(&g, grad, sizeof(T));
    U g_u;
    std::memcpy(&g_u, &g, sizeof(U));
    T predicted = static_cast<T>(static_cast<U>(p) + g_u);
    return (c == predicted);
}

// ── Float/double XOR delta helpers ─────────────────────────────────────────

template<typename LayoutType>
template<typename T>
uint64_t RowCodecDelta001<LayoutType>::computeFloatXorDelta(const std::byte* curr, const std::byte* prev) {
    static_assert(std::is_floating_point_v<T>);
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    U c_bits, p_bits;
    std::memcpy(&c_bits, curr, sizeof(T));
    std::memcpy(&p_bits, prev, sizeof(T));
    return static_cast<uint64_t>(c_bits ^ p_bits);
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta001<LayoutType>::applyFloatXorDelta(std::byte* dst, const std::byte* prev, uint64_t xorDelta) {
    static_assert(std::is_floating_point_v<T>);
    using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
    U p_bits;
    std::memcpy(&p_bits, prev, sizeof(T));
    U result_bits = p_bits ^ static_cast<U>(xorDelta);
    std::memcpy(dst, &result_bits, sizeof(T));
}

template<typename LayoutType>
template<typename T>
void RowCodecDelta001<LayoutType>::computeFloatGradient(std::byte* grad, const std::byte* curr, const std::byte* prev) {
    T c, p;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    T g = c - p;
    std::memcpy(grad, &g, sizeof(T));
}

template<typename LayoutType>
template<typename T>
bool RowCodecDelta001<LayoutType>::checkFloatFoC(const std::byte* curr, const std::byte* prev, const std::byte* grad) {
    T c, p, g;
    std::memcpy(&c, curr, sizeof(T));
    std::memcpy(&p, prev, sizeof(T));
    std::memcpy(&g, grad, sizeof(T));
    T predicted = p + g;
    return std::memcmp(&c, &predicted, sizeof(T)) == 0;
}

// ────────────────────────────────────────────────────────────────────────────
// setup / reset
// ────────────────────────────────────────────────────────────────────────────

template<typename LayoutType>
void RowCodecDelta001<LayoutType>::setup(const LayoutType& layout) {
    guard_ = LayoutGuard(layout.data());
    layout_ = &layout;

    const size_t colCount = layout.columnCount();
    const auto& types = layout.columnTypes();
    const auto& offsets = layout.columnOffsets();

    bool_count_ = layout.columnCount(ColumnType::BOOL);

    // Build per-column metadata in ColumnType enum order (same as ZoH)
    col_head_offsets_.clear();
    col_head_bits_.clear();
    col_data_offsets_.clear();
    col_type_sizes_.clear();
    col_is_signed_.clear();
    col_is_float_.clear();
    str_offsets_.clear();

    static constexpr ColumnType TYPE_ORDER[] = {
        ColumnType::UINT8, ColumnType::UINT16, ColumnType::UINT32, ColumnType::UINT64,
        ColumnType::INT8, ColumnType::INT16, ColumnType::INT32, ColumnType::INT64,
        ColumnType::FLOAT, ColumnType::DOUBLE
    };

    size_t head_bit_pos = bool_count_;  // bools occupy the first bool_count_ bits

    for (auto type : TYPE_ORDER) {
        size_t typeSize = sizeOf(type);
        bool isSigned = (type == ColumnType::INT8 || type == ColumnType::INT16 ||
                         type == ColumnType::INT32 || type == ColumnType::INT64);
        bool isFloat = (type == ColumnType::FLOAT || type == ColumnType::DOUBLE);
        size_t lbits = lenBitsForSize(typeSize);
        size_t totalBits = 2 + lbits;

        for (size_t i = 0; i < colCount; ++i) {
            if (types[i] != type) continue;

            col_head_offsets_.push_back(head_bit_pos);
            col_head_bits_.push_back(totalBits);
            col_data_offsets_.push_back(offsets[i]);
            col_type_sizes_.push_back(static_cast<uint8_t>(typeSize));
            col_is_signed_.push_back(isSigned);
            col_is_float_.push_back(isFloat);

            head_bit_pos += totalBits;
        }
    }

    // String columns: 1 bit each (use layout offsets for strg_ index)
    for (size_t i = 0; i < colCount; ++i) {
        if (types[i] == ColumnType::STRING) {
            str_offsets_.push_back(offsets[i]);
            head_bit_pos++;
        }
    }

    head_bits_ = head_bit_pos;
    head_.resize(head_bits_);
    head_.reset();

    prev_data_.clear();
    prev_strg_.clear();
    grad_data_.clear();

    rows_seen_ = 0;
}

template<typename LayoutType>
void RowCodecDelta001<LayoutType>::reset() noexcept {
    // Only rows_seen_ needs resetting. The first-row path in serialize/
    // deserialize re-initialises prev_data_, prev_strg_, and grad_data_
    // from scratch, so clearing them here would be redundant work.
    rows_seen_ = 0;
}

// ────────────────────────────────────────────────────────────────────────────
// Type-dispatch helper (reduces repetitive switch/case blocks)
// ────────────────────────────────────────────────────────────────────────────
namespace detail_delta {

/// Call fn<T>(args...) dispatched by (isFloat, isSigned, typeSize).
template<typename Fn, typename... Args>
inline auto dispatchType(bool isFloat, bool isSigned, size_t sz,
                         Fn&& fn, Args&&... args)
{
    if (isFloat) {
        if (sz == 4) return fn.template operator()<float>(std::forward<Args>(args)...);
        else         return fn.template operator()<double>(std::forward<Args>(args)...);
    } else if (isSigned) {
        switch (sz) {
            case 1: return fn.template operator()<int8_t>(std::forward<Args>(args)...);
            case 2: return fn.template operator()<int16_t>(std::forward<Args>(args)...);
            case 4: return fn.template operator()<int32_t>(std::forward<Args>(args)...);
            default: return fn.template operator()<int64_t>(std::forward<Args>(args)...);
        }
    } else {
        switch (sz) {
            case 1: return fn.template operator()<uint8_t>(std::forward<Args>(args)...);
            case 2: return fn.template operator()<uint16_t>(std::forward<Args>(args)...);
            case 4: return fn.template operator()<uint32_t>(std::forward<Args>(args)...);
            default: return fn.template operator()<uint64_t>(std::forward<Args>(args)...);
        }
    }
}

} // namespace detail_delta

// ────────────────────────────────────────────────────────────────────────────
// serialize
// ────────────────────────────────────────────────────────────────────────────

template<typename LayoutType>
std::span<std::byte> RowCodecDelta001<LayoutType>::serialize(
    const RowType& row, ByteBuffer& buffer) {

    const size_t headBytes = (head_bits_ + 7) / 8;
    const size_t numCols = col_data_offsets_.size();
    const size_t numStrCols = str_offsets_.size();

    // Reset header to all zeros
    head_.reset();

    // ── Bool values ──────────────────────────────────────────────────────
    if (bool_count_ > 0) {
        assignRange(head_, 0, row.bits_, 0, bool_count_);
    }

    // ── First row in packet: emit everything as plain ────────────────────
    if (rows_seen_ == 0) {
        size_t maxDataSize = 0;
        for (size_t c = 0; c < numCols; ++c) maxDataSize += col_type_sizes_[c];
        for (size_t s = 0; s < numStrCols; ++s) maxDataSize += 2 + row.strg_[str_offsets_[s]].size();

        buffer.resize(headBytes + maxDataSize);
        size_t bufIdx = headBytes;

        for (size_t c = 0; c < numCols; ++c) {
            head_.encode(col_head_offsets_[c], 2, 0x01);  // mode=01 (plain)
            std::memcpy(&buffer[bufIdx], &row.data_[col_data_offsets_[c]], col_type_sizes_[c]);
            bufIdx += col_type_sizes_[c];
        }

        // All strings: change flag = 1
        size_t strHeadBit = head_bits_ - numStrCols;
        for (size_t s = 0; s < numStrCols; ++s) {
            head_[strHeadBit + s] = true;
            const auto& str = row.strg_[str_offsets_[s]];
            uint16_t len = static_cast<uint16_t>(str.size());
            std::memcpy(&buffer[bufIdx], &len, 2);
            bufIdx += 2;
            if (len > 0) { std::memcpy(&buffer[bufIdx], str.data(), len); bufIdx += len; }
        }

        head_.writeTo(buffer.data(), headBytes);

        // Prime prev state
        prev_data_.assign(row.data_.begin(), row.data_.end());
        prev_strg_.assign(row.strg_.begin(), row.strg_.end());
        grad_data_.assign(prev_data_.size(), std::byte{0});

        buffer.resize(bufIdx);
        rows_seen_ = 1;
        return std::span<std::byte>(buffer.data(), bufIdx);
    }

    // ── Subsequent rows: delta/ZoH/FoC encoding ─────────────────────────

    // Pessimistic buffer size
    size_t maxSize = headBytes;
    for (size_t c = 0; c < numCols; ++c) maxSize += col_type_sizes_[c];
    for (size_t s = 0; s < numStrCols; ++s) maxSize += 2 + row.strg_[str_offsets_[s]].size();
    buffer.resize(maxSize);
    size_t bufIdx = headBytes;

    // ── Numeric columns ──────────────────────────────────────────────────
    for (size_t c = 0; c < numCols; ++c) {
        const size_t headOff = col_head_offsets_[c];
        const size_t off = col_data_offsets_[c];
        const size_t sz = col_type_sizes_[c];

        bool unchanged = (std::memcmp(&row.data_[off], &prev_data_[off], sz) == 0);

        if (unchanged) {
            head_.encode(headOff, 2, 0x00);  // ZoH
            std::memset(&grad_data_[off], 0, sz);
            continue;
        }

        // Check FoC prediction (only valid from row >= 2, i.e. gradient established)
        if (rows_seen_ >= 2) {
            auto checkFoC = [&]<typename T>() -> bool {
                if constexpr (std::is_floating_point_v<T>)
                    return checkFloatFoC<T>(&row.data_[off], &prev_data_[off], &grad_data_[off]);
                else
                    return checkIntFoC<T>(&row.data_[off], &prev_data_[off], &grad_data_[off]);
            };

            bool foc_match = detail_delta::dispatchType(
                col_is_float_[c], col_is_signed_[c], sz, checkFoC);

            if (foc_match) {
                head_.encode(headOff, 2, 0x02);  // FoC
                std::memcpy(&prev_data_[off], &row.data_[off], sz);
                // Gradient stays the same
                continue;
            }
        }

        // Compute delta
        auto computeDelta = [&]<typename T>() -> uint64_t {
            if constexpr (std::is_floating_point_v<T>)
                return computeFloatXorDelta<T>(&row.data_[off], &prev_data_[off]);
            else
                return computeIntDelta<T>(&row.data_[off], &prev_data_[off]);
        };

        uint64_t delta = detail_delta::dispatchType(
            col_is_float_[c], col_is_signed_[c], sz, computeDelta);

        size_t deltaBytes = vleByteCount(delta);

        if (deltaBytes < sz) {
            head_.encode(headOff, 2, 0x03);  // delta
            size_t lenBits = lenBitsForSize(sz);
            if (lenBits > 0) head_.encode(headOff + 2, lenBits, static_cast<uint8_t>(deltaBytes - 1));
            encodeDelta(&buffer[bufIdx], delta, deltaBytes);
            bufIdx += deltaBytes;
        } else {
            head_.encode(headOff, 2, 0x01);  // plain
            std::memcpy(&buffer[bufIdx], &row.data_[off], sz);
            bufIdx += sz;
        }

        // Update gradient
        auto updateGrad = [&]<typename T>() {
            if constexpr (std::is_floating_point_v<T>)
                computeFloatGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);
            else
                computeIntGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);
        };
        detail_delta::dispatchType(col_is_float_[c], col_is_signed_[c], sz, updateGrad);

        std::memcpy(&prev_data_[off], &row.data_[off], sz);
    }

    // ── String columns ──────────────────────────────────────────────────
    size_t strHeadBit = head_bits_ - numStrCols;
    for (size_t s = 0; s < numStrCols; ++s) {
        const uint32_t strIdx = str_offsets_[s];
        if (row.strg_[strIdx] != prev_strg_[strIdx]) {
            head_[strHeadBit + s] = true;
            prev_strg_[strIdx] = row.strg_[strIdx];

            const auto& str = row.strg_[strIdx];
            uint16_t len = static_cast<uint16_t>(str.size());
            if (bufIdx + 2 + len > buffer.size()) buffer.resize(bufIdx + 2 + len);
            std::memcpy(&buffer[bufIdx], &len, 2);
            bufIdx += 2;
            if (len > 0) { std::memcpy(&buffer[bufIdx], str.data(), len); bufIdx += len; }
        } else {
            head_[strHeadBit + s] = false;
        }
    }

    // Note: we do NOT use the all-ZoH shortcut (empty span). Delta codec
    // always emits the header to keep gradient state synchronised between
    // writer and reader (reader must call deserialize to zero gradients).
    head_.writeTo(buffer.data(), headBytes);
    buffer.resize(bufIdx);
    rows_seen_++;
    return std::span<std::byte>(buffer.data(), bufIdx);
}

// ────────────────────────────────────────────────────────────────────────────
// deserialize
// ────────────────────────────────────────────────────────────────────────────

template<typename LayoutType>
void RowCodecDelta001<LayoutType>::deserialize(
    std::span<const std::byte> buffer, RowType& row) {

    const size_t headBytes = (head_bits_ + 7) / 8;
    const size_t numCols = col_data_offsets_.size();
    const size_t numStrCols = str_offsets_.size();

    assert(buffer.size() >= headBytes && "buffer too small for header");

    head_.readFrom(buffer.data(), headBytes);

    // ── Bool values ──────────────────────────────────────────────────────
    if (bool_count_ > 0) {
        assignRange(row.bits_, 0, head_, 0, bool_count_);
    }

    // Prime prev state on first deserialize (row 0)
    if (rows_seen_ == 0) {
        prev_data_.resize(row.data_.size(), std::byte{0});
        prev_strg_.resize(row.strg_.size());
        grad_data_.assign(row.data_.size(), std::byte{0});
    }

    size_t dataOff = headBytes;

    // ── Numeric columns ──────────────────────────────────────────────────
    for (size_t c = 0; c < numCols; ++c) {
        const size_t headOff = col_head_offsets_[c];
        const size_t off = col_data_offsets_[c];
        const size_t sz = col_type_sizes_[c];

        uint8_t mode = head_.decode(headOff, 2);

        switch (mode) {
            case 0x00:  // ZoH — unchanged, zero gradient
                // Row retains previous value. Zero gradient for state sync.
                std::memset(&grad_data_[off], 0, sz);
                break;

            case 0x01: {  // plain — full value
                // Update gradient before overwriting prev
                if (rows_seen_ > 0) {
                    auto updateGrad = [&]<typename T>() {
                        if constexpr (std::is_floating_point_v<T>)
                            computeFloatGradient<T>(&grad_data_[off], &buffer[dataOff], &prev_data_[off]);
                        else
                            computeIntGradient<T>(&grad_data_[off], &buffer[dataOff], &prev_data_[off]);
                    };
                    detail_delta::dispatchType(col_is_float_[c], col_is_signed_[c], sz, updateGrad);
                }

                std::memcpy(&row.data_[off], &buffer[dataOff], sz);
                std::memcpy(&prev_data_[off], &buffer[dataOff], sz);
                dataOff += sz;
                break;
            }

            case 0x02: {  // FoC — first-order prediction
                auto applyFoC = [&]<typename T>() {
                    if constexpr (std::is_floating_point_v<T>) {
                        T p, g;
                        std::memcpy(&p, &prev_data_[off], sizeof(T));
                        std::memcpy(&g, &grad_data_[off], sizeof(T));
                        T result = p + g;
                        std::memcpy(&row.data_[off], &result, sizeof(T));
                        std::memcpy(&prev_data_[off], &result, sizeof(T));
                    } else {
                        // Add gradient in unsigned domain to avoid signed overflow UB.
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
                };
                detail_delta::dispatchType(col_is_float_[c], col_is_signed_[c], sz, applyFoC);
                // Gradient unchanged
                break;
            }

            case 0x03: {  // delta — VLE encoded
                size_t lenBits = lenBitsForSize(sz);
                size_t deltaBytes = 1;
                if (lenBits > 0) deltaBytes = head_.decode(headOff + 2, lenBits) + 1;
                uint64_t zigzagDelta = decodeDelta(&buffer[dataOff], deltaBytes);

                auto applyDelta = [&]<typename T>() {
                    if constexpr (std::is_floating_point_v<T>)
                        applyFloatXorDelta<T>(&row.data_[off], &prev_data_[off], zigzagDelta);
                    else
                        applyIntDelta<T>(&row.data_[off], &prev_data_[off], zigzagDelta);
                };
                detail_delta::dispatchType(col_is_float_[c], col_is_signed_[c], sz, applyDelta);

                // Update gradient
                auto updateGrad = [&]<typename T>() {
                    if constexpr (std::is_floating_point_v<T>)
                        computeFloatGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);
                    else
                        computeIntGradient<T>(&grad_data_[off], &row.data_[off], &prev_data_[off]);
                };
                detail_delta::dispatchType(col_is_float_[c], col_is_signed_[c], sz, updateGrad);

                std::memcpy(&prev_data_[off], &row.data_[off], sz);
                dataOff += deltaBytes;
                break;
            }
        }
    }

    // ── String columns ──────────────────────────────────────────────────
    size_t strHeadBit = head_bits_ - numStrCols;
    for (size_t s = 0; s < numStrCols; ++s) {
        if (head_[strHeadBit + s]) {
            const uint32_t strIdx = str_offsets_[s];
            uint16_t len;
            std::memcpy(&len, &buffer[dataOff], 2);
            dataOff += 2;
            row.strg_[strIdx].assign(reinterpret_cast<const char*>(&buffer[dataOff]), len);
            prev_strg_[strIdx] = row.strg_[strIdx];
            dataOff += len;
        }
    }

    rows_seen_++;
}

} // namespace bcsv

