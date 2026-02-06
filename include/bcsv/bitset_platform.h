/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <limits>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace bcsv {
namespace detail {

// ===== Platform Detection and Word Type Selection =====

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
    // 64-bit platforms: x86-64, ARM64
    using storage_word_t = uint64_t;
    static constexpr size_t WORD_SIZE = 8;
    static constexpr size_t WORD_BITS = 64;
    static constexpr size_t WORD_ALIGN = alignof(uint64_t);
    static constexpr const char* PLATFORM_NAME = "64-bit";
#elif defined(__arm__) || defined(_M_ARM) || defined(__i386__) || defined(_M_IX86)
    // 32-bit platforms: ARM32, x86
    using storage_word_t = uint32_t;
    static constexpr size_t WORD_SIZE = 4;
    static constexpr size_t WORD_BITS = 32;
    static constexpr size_t WORD_ALIGN = alignof(uint32_t);
    static constexpr const char* PLATFORM_NAME = "32-bit";
#else
    // Conservative fallback for unknown platforms
    using storage_word_t = uint32_t;
    static constexpr size_t WORD_SIZE = 4;
    static constexpr size_t WORD_BITS = 32;
    static constexpr size_t WORD_ALIGN = alignof(uint32_t);
    static constexpr const char* PLATFORM_NAME = "32-bit (fallback)";
#endif

// Compile-time assertions to verify platform assumptions
static_assert(sizeof(storage_word_t) == WORD_SIZE, 
              "Storage word size mismatch");
static_assert(WORD_SIZE * 8 == WORD_BITS, 
              "Word size and bits inconsistent");
static_assert(WORD_ALIGN <= WORD_SIZE, 
              "Alignment cannot exceed word size");
static_assert((WORD_SIZE & (WORD_SIZE - 1)) == 0, 
              "Word size must be power of 2");

// Note: We don't need a custom allocator!
// std::allocator<storage_word_t> already ensures alignof(storage_word_t) alignment,
// which is exactly what we need. The STL is already optimized for the platform.

// ===== Helper Functions =====

/**
 * @brief Calculate number of words needed to store N bits
 */
constexpr size_t bits_to_words(size_t bit_count) noexcept {
    return (bit_count + WORD_BITS - 1) / WORD_BITS;
}

/**
 * @brief Calculate number of bytes needed to store N bits
 */
constexpr size_t bits_to_bytes(size_t bit_count) noexcept {
    return (bit_count + 7) / 8;
}

/**
 * @brief Get word index from bit position
 */
constexpr size_t bit_to_word_index(size_t bit_pos) noexcept {
    return bit_pos / WORD_BITS;
}

/**
 * @brief Get bit index within word from bit position
 */
constexpr size_t bit_to_bit_index(size_t bit_pos) noexcept {
    return bit_pos % WORD_BITS;
}

/**
 * @brief Create mask for bits in last word
 */
constexpr storage_word_t last_word_mask(size_t bit_count) noexcept {
    const size_t bits_in_last = bit_count % WORD_BITS;
    if (bits_in_last == 0) {
        return ~storage_word_t{0};
    }
    return (storage_word_t{1} << bits_in_last) - 1;
}

} // namespace detail
} // namespace bcsv
