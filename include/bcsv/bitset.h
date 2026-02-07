/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <limits>
#include <ostream>
#include <istream>
#include <cassert>

namespace bcsv {

// Sentinel value for dynamic extent (like std::dynamic_extent for std::span)
inline constexpr size_t dynamic_extent = std::numeric_limits<size_t>::max();

/**
 * @brief Unified bitset implementation supporting both compile-time and runtime sizes
 * 
 * This class provides a bitset that can be either:
 * - Fixed-size (compile-time): bitset<64> uses std::array, stack storage
 * - Dynamic-size (runtime): bitset<> or bitset<dynamic_extent> uses std::vector, heap storage
 * 
 * Storage uses platform-native word sizes (uint64_t on 64-bit, uint32_t on 32-bit) 
 * for optimal performance. STL containers provide proper alignment automatically.
 * 
 * @tparam N Number of bits (compile-time) or dynamic_extent for runtime size
 * 
 * Examples:
 * @code
 *   bitset<64> fixed;              // Fixed 64 bits, stack storage
 *   bitset<> dynamic(128);         // Dynamic 128 bits, heap storage
 *   bitset<dynamic_extent> dyn;    // Explicit dynamic extent
 * @endcode
 */
template<size_t N = dynamic_extent>
class bitset {
private:    
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
    using word_t = uint64_t;
#elif defined(__arm__) || defined(_M_ARM) || defined(__i386__) || defined(_M_IX86)
    using word_t = uint32_t;
#else
    // Conservative fallback for unknown platforms
    using word_t = uint32_t;
#endif

    static constexpr size_t WORD_SIZE = sizeof(word_t);
    static constexpr size_t WORD_BITS = WORD_SIZE * 8;

    // Helper constants for fixed-size bitsets
    static constexpr size_t bits_to_words(size_t bit_count) noexcept {
        return (bit_count + WORD_BITS - 1) / WORD_BITS;
    }

    static constexpr size_t bits_to_bytes(size_t bit_count) noexcept {
        return (bit_count + 7) / 8;
    }

    static constexpr size_t bit_to_word_index(size_t bit_pos) noexcept {
        return bit_pos / WORD_BITS;
    }

    static constexpr size_t bit_to_bit_index(size_t bit_pos) noexcept {
        return bit_pos % WORD_BITS;
    }

    static constexpr word_t last_word_mask(size_t bit_count) noexcept {
        const size_t bits_in_last = bit_count % WORD_BITS;
        if (bits_in_last == 0) {
            return ~word_t{0};
        }
        return (word_t{1} << bits_in_last) - 1;
    }

    // Compile-time detection of fixed vs dynamic
    static constexpr bool is_fixed = (N != dynamic_extent);

    // For fixed-size: calculate word count at compile time
    static constexpr size_t word_count_fixed = is_fixed ? bits_to_words(N) : 0;
    
    // Storage type selection: array for fixed, vector for dynamic
    // Note: STL containers already provide proper alignment for storage_word_t
    using storage_type = std::conditional_t<
        is_fixed,
        std::array<word_t, word_count_fixed>,
        std::vector<word_t>
    >;
    
    storage_type storage_;
    
    // Size storage: only for dynamic case, zero overhead for fixed case
    struct empty_size {};
    [[no_unique_address]] std::conditional_t<is_fixed, empty_size, size_t> bit_count_;
    
    // Friend declarations
    template<size_t M> friend class bitset;
    template<size_t M> friend bool operator==(const bitset<M>&, const bitset<M>&) noexcept;
    
    // Internal helpers
    constexpr size_t word_count() const noexcept;
    constexpr size_t byte_count() const noexcept;
    constexpr void clear_unused_bits() noexcept;
    constexpr void set_from_value(unsigned long long val) noexcept;
    
    template<class CharT, class Traits, class Allocator>
    void set_from_string(
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n,
        CharT zero, CharT one);
    
public:
    /**
     * @brief Proxy class for individual bit access
     */
    class reference {
    private:
        word_t* word_ptr;
        size_t bit_index;
        
    public:
        reference(word_t* ptr, size_t bit_idx);
        
        reference& operator=(bool value);
        reference& operator=(const reference& other);
        reference& operator|=(bool value);
        reference& operator&=(bool value);
        reference& operator^=(bool value);
        operator bool() const;
        bool operator~() const;
        reference& flip();
    };
    
    // ===== Constructors and Assignment =====
    
    // Fixed-size constructors
    constexpr bitset() noexcept requires(is_fixed);
    constexpr bitset(unsigned long long val) noexcept requires(is_fixed);
    
    template<class CharT, class Traits, class Allocator>
    explicit bitset(
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(is_fixed);
    
    // Dynamic-size constructors
    explicit bitset(size_t num_bits = 0) requires(!is_fixed);
    bitset(size_t num_bits, unsigned long long val) requires(!is_fixed);
    bitset(size_t num_bits, bool value) requires(!is_fixed);
    
    template<class CharT, class Traits, class Allocator>
    explicit bitset(
        size_t num_bits,
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(!is_fixed);
    
    // Copy/move (compiler-generated)
    bitset(const bitset&) = default;
    bitset(bitset&&) noexcept = default;
    bitset& operator=(const bitset&) = default;
    bitset& operator=(bitset&&) noexcept = default;
    
    // Conversion: Fixed → Dynamic
    template<size_t M>
    explicit bitset(const bitset<M>& other) requires(!is_fixed && M != dynamic_extent);
    
    // ===== Element Access =====
    // operator[] - Unchecked access (UB if out of bounds, debug assertion only)
    // test()     - Checked access (throws std::out_of_range if out of bounds)
    
    constexpr bool operator[](size_t pos) const;
    reference operator[](size_t pos);
    constexpr bool test(size_t pos) const;
    
    // ===== Capacity =====
    
    constexpr size_t size() const noexcept;
    constexpr size_t sizeBytes() const noexcept;
    constexpr bool empty() const noexcept;
    static constexpr bool is_fixed_size() noexcept;
    
    // ===== Modifiers =====
    // All single-bit modifiers (set/reset/flip with pos) throw std::out_of_range if out of bounds
    
    bitset& set() noexcept;
    bitset& set(size_t pos, bool val = true);  // Throws if pos >= size()
    
    bitset& reset() noexcept;
    bitset& reset(size_t pos);  // Throws if pos >= size()
    
    bitset& flip() noexcept;
    bitset& flip(size_t pos);  // Throws if pos >= size()
    
    // Dynamic-only modifiers
    void clear() noexcept requires(!is_fixed);
    void reserve(size_t bit_capacity) requires(!is_fixed);
    void resize(size_t new_size, bool value = false) requires(!is_fixed);
    void shrink_to_fit() requires(!is_fixed);
    
    // ===== Operations =====
    
    bool all() const noexcept;
    bool any() const noexcept;
    size_t count() const noexcept;
    bool none() const noexcept;
    
    // ===== Conversions =====
    
    unsigned long to_ulong() const;
    unsigned long long to_ullong() const;
    std::string to_string(char zero = '0', char one = '1') const;
    
    // Dynamic → Fixed conversion (with validation)
    template<size_t M>
    bitset<M> to_fixed() const requires(!is_fixed);
    
    // ===== I/O and Binary Compatibility =====
    
    std::byte* data() noexcept;
    const std::byte* data() const noexcept;
    
    void readFrom(const void* src, size_t available);
    void writeTo(void* dst, size_t capacity) const;
    
    // ===== Bitwise Operators =====
    
    bitset operator~() const noexcept;
    
    bitset& operator&=(const bitset& other) noexcept;
    bitset& operator|=(const bitset& other) noexcept;
    bitset& operator^=(const bitset& other) noexcept;
    
    bitset operator<<(size_t shift_amount) const noexcept;
    bitset operator>>(size_t shift_amount) const noexcept;
    
    bitset& operator<<=(size_t shift_amount) noexcept;
    bitset& operator>>=(size_t shift_amount) noexcept;
};

// ===== Non-member Operators =====

template<size_t N>
bitset<N> operator&(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;

template<size_t N>
bitset<N> operator|(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;

template<size_t N>
bitset<N> operator^(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;

template<size_t N>
bool operator==(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;

template<size_t N>
bool operator!=(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;

template<class CharT, class Traits, size_t N>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, 
    const bitset<N>& x);

template<class CharT, class Traits, size_t N>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is, 
    bitset<N>& x);

} // namespace bcsv

// Include implementation
#include "bitset.hpp"
