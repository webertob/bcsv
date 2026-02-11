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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <limits>
#include <algorithm>
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
 * - Dynamic-size (runtime): bitset<> or bitset<dynamic_extent> uses inline word or heap fallback
 * 
 * Storage uses platform-native word sizes (uintptr_t) for optimal performance.
 * Inline storage is word-aligned.
 * 
 * @tparam N Number of bits (compile-time) or dynamic_extent for runtime size
 * 
 * Examples:
 * @code
 *   bitset<64> fixed;              // Fixed 64 bits, stack storage
 *   bitset<> dynamic(128);         // Dynamic 128 bits, inline/heap storage
 *   bitset<dynamic_extent> dyn;    // Explicit dynamic extent
 * @endcode
 */
template<size_t N = dynamic_extent>
class bitset {
    private:
        using word_t = std::uintptr_t;
        static constexpr bool IS_FIXED = (N != dynamic_extent);
        static constexpr size_t WORD_SIZE = sizeof(word_t);
        static constexpr size_t WORD_BITS = WORD_SIZE * 8;

        static constexpr size_t bitsToWords(size_t bit_count) noexcept {
            return (bit_count + WORD_BITS - 1) / WORD_BITS;
        }

        // Fixed-size: calculate word count at compile time
        static constexpr size_t WORD_COUNT_FIXED = IS_FIXED ? bitsToWords(N) : 0;

        using StorageT = std::conditional_t<
            IS_FIXED,
            std::array<word_t, WORD_COUNT_FIXED>,
            std::uintptr_t>;

        struct EmptySize {};

        // Dynamic storage: exactly two members (size_ and data_)
        [[no_unique_address]] std::conditional_t<IS_FIXED, EmptySize, size_t> size_{};
        [[no_unique_address]] StorageT data_{};

        // Friend declarations
        template<size_t M> friend class bitset;
        template<size_t M> friend bool operator==(const bitset<M>&, const bitset<M>&) noexcept;

        // =====================================================================
        // Helpers (static)
        // =====================================================================
        static constexpr size_t bitsToBytes(size_t bit_count) noexcept {
            return (bit_count + 7) / 8;
        }

        static constexpr size_t bitToWordIndex(size_t bit_pos) noexcept {
            return bit_pos / WORD_BITS;
        }

        static constexpr size_t bitToBitIndex(size_t bit_pos) noexcept {
            return bit_pos % WORD_BITS;
        }

        static constexpr word_t lastWordMask(size_t bit_count) noexcept {
            const size_t bits_in_last = bit_count % WORD_BITS;
            if (bits_in_last == 0) {
                return ~word_t{0};
            }
            return (word_t{1} << bits_in_last) - 1;
        }

        // =====================================================================
        // Helpers (instance)
        // =====================================================================
        constexpr size_t wordCount() const noexcept;
        constexpr size_t byteCount() const noexcept;
        constexpr void clearUnusedBits() noexcept;
        constexpr void setFromValue(unsigned long long val) noexcept;
        constexpr bool usesInline() const noexcept;
        constexpr word_t* wordData() noexcept;
        constexpr const word_t* wordData() const noexcept;
        void releaseHeap() noexcept;
        void resizeStorage(size_t old_size, size_t new_size, word_t value);

        template<class CharT, class Traits, class Allocator>
        void setFromString(
            const std::basic_string<CharT, Traits, Allocator>& str,
            typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
            typename std::basic_string<CharT, Traits, Allocator>::size_type n,
            CharT zero, CharT one);

    public:
        // =====================================================================
        // Reference and Slice Views
        // =====================================================================
        class reference {
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

        private:
            word_t* word_ptr_;
            size_t bit_index_;
        };

        class const_slice_view {
        public:
            const_slice_view(const bitset* owner, size_t start, size_t length);

            size_t size() const noexcept;
            bool empty() const noexcept;

            bool operator[](size_t pos) const;
            bool test(size_t pos) const;

            bool all() const noexcept;
            bool any() const noexcept;
            bool none() const noexcept;
            size_t count() const noexcept;

            bool all(const bitset& mask) const noexcept;
            bool any(const bitset& mask) const noexcept;

            bitset operator<<(size_t shift_amount) const noexcept;
            bitset operator>>(size_t shift_amount) const noexcept;

            bitset<> to_bitset() const;
            bitset<> shifted_left(size_t shift_amount) const;
            bitset<> shifted_right(size_t shift_amount) const;

        protected:
            const bitset* owner_;
            size_t start_;
            size_t length_;

            struct slice_meta {
                size_t start_word_;
                size_t start_bit_;
                size_t word_count_;
                size_t tail_bits_;
                word_t tail_mask_;
            };

            slice_meta meta() const noexcept;
            word_t load_word(size_t index, const slice_meta& meta) const noexcept;
        };

        class slice_view : public const_slice_view {
        public:
            slice_view(bitset* owner, size_t start, size_t length);

            reference operator[](size_t pos);

            slice_view& set() noexcept;
            slice_view& set(size_t pos, bool val = true);

            slice_view& reset() noexcept;
            slice_view& reset(size_t pos);

            slice_view& flip() noexcept;
            slice_view& flip(size_t pos);

            slice_view& operator&=(const bitset& other) noexcept;
            slice_view& operator|=(const bitset& other) noexcept;
            slice_view& operator^=(const bitset& other) noexcept;

            slice_view& operator&=(const const_slice_view& other) noexcept;
            slice_view& operator|=(const const_slice_view& other) noexcept;
            slice_view& operator^=(const const_slice_view& other) noexcept;

            slice_view& operator<<=(size_t shift_amount) noexcept;
            slice_view& operator>>=(size_t shift_amount) noexcept;

        private:
            using slice_meta = typename const_slice_view::slice_meta;
            void store_word(size_t index, word_t value, word_t slice_mask, const slice_meta& meta) noexcept;
        };
    
    // ===== Constructors and Assignment =====
    
    // Fixed-size constructors
    constexpr bitset() noexcept requires(IS_FIXED);
    constexpr bitset(unsigned long long val) noexcept requires(IS_FIXED);
    
    template<class CharT, class Traits, class Allocator>
    explicit bitset(
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(IS_FIXED);
    
    // Dynamic-size constructors
    explicit bitset(size_t num_bits = 0) requires(!IS_FIXED);
    bitset(size_t num_bits, unsigned long long val) requires(!IS_FIXED);
    bitset(size_t num_bits, bool value) requires(!IS_FIXED);
    
    template<class CharT, class Traits, class Allocator>
    explicit bitset(
        size_t num_bits,
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(!IS_FIXED);
    
    bitset(const bitset& other);
    bitset(bitset&& other) noexcept;
    bitset& operator=(const bitset& other);
    bitset& operator=(bitset&& other) noexcept;
    ~bitset();
    
    // Conversion: Fixed → Dynamic
    template<size_t M>
    explicit bitset(const bitset<M>& other) requires(!IS_FIXED && M != dynamic_extent);
    
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
    constexpr size_t capacity() const noexcept;
    static constexpr bool is_fixed_size() noexcept;

    // ===== Views =====
    // Throws std::out_of_range if the range exceeds the bitset size.
    slice_view slice(size_t start, size_t length);
    const_slice_view slice(size_t start, size_t length) const;
    
    // ===== Modifiers =====
    // All single-bit modifiers (set/reset/flip with pos) throw std::out_of_range if out of bounds
    
    bitset& set() noexcept;
    bitset& set(size_t pos, bool val = true);  // Throws if pos >= size()
    
    bitset& reset() noexcept;
    bitset& reset(size_t pos);  // Throws if pos >= size()
    
    bitset& flip() noexcept;
    bitset& flip(size_t pos);  // Throws if pos >= size()
    
    // Dynamic-only modifiers
    void clear() noexcept requires(!IS_FIXED);
    void reserve(size_t bit_capacity) requires(!IS_FIXED);
    void resize(size_t new_size, bool value = false) requires(!IS_FIXED);
    void insert(size_t pos, bool value = false) requires(!IS_FIXED);  // Insert bit at pos, shifting subsequent bits right
    void shrink_to_fit() requires(!IS_FIXED);
    
    // ===== Operations =====
    
    bool all() const noexcept;
    bool any() const noexcept;
    size_t count() const noexcept;
    bool none() const noexcept;

    // Masked queries (avoid temporary bitsets)
    // Mask is truncated to this bitset's size; extra mask bits are ignored.
    bool all(const bitset& mask) const noexcept;
    bool any(const bitset& mask) const noexcept;
    
    // ===== Conversions =====
    
    unsigned long to_ulong() const;
    unsigned long long to_ullong() const;
    std::string to_string(char zero = '0', char one = '1') const;
    
    // Dynamic → Fixed conversion (with validation)
    template<size_t M>
    bitset<M> to_fixed() const requires(!IS_FIXED);
    
    // ===== I/O and Binary Compatibility =====
    
    std::byte* data() noexcept;
    const std::byte* data() const noexcept;
    
    void readFrom(const void* src, size_t available);
    void writeTo(void* dst, size_t capacity) const;
    
    // ===== Bitwise Operators =====
    // For dynamic bitsets, compound ops truncate to this bitset's size.
    
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
