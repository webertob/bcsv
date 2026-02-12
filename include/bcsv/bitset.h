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

// Sentinel value for dynamic extent (like std::DYNAMIC_EXTENT for std::span)
inline constexpr size_t DYNAMIC_EXTENT = std::numeric_limits<size_t>::max();

/**
 * @brief Unified Bitset implementation supporting both compile-time and runtime sizes
 * 
 * This class provides a Bitset that can be either:
 * - Fixed-size (compile-time): Bitset<64> uses std::array, stack storage
 * - Dynamic-size (runtime): Bitset<> or Bitset<DYNAMIC_EXTENT> uses inline word or heap fallback
 * 
 * Storage uses platform-native word sizes (uintptr_t) for optimal performance.
 * Inline storage is word-aligned.
 * 
 * @tparam N Number of bits (compile-time) or DYNAMIC_EXTENT for runtime size
 * 
 * Examples:
 * @code
 *   Bitset<64> fixed;              // Fixed 64 bits, stack storage
 *   Bitset<> dynamic(128);         // Dynamic 128 bits, inline/heap storage
 *   Bitset<DYNAMIC_EXTENT> dyn;    // Explicit dynamic extent
 * @endcode
 */
template<size_t N = DYNAMIC_EXTENT>
class Bitset {
    private:
        using word_t = std::uintptr_t;
        static constexpr bool IS_FIXED = (N != DYNAMIC_EXTENT);
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
        template<size_t M> friend class Bitset;
        template<size_t M> friend bool operator==(const Bitset<M>&, const Bitset<M>&) noexcept;

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
        class Reference {
        public:
            Reference(word_t* ptr, size_t bit_idx);

            Reference& operator=(bool value);
            Reference& operator=(const Reference& other);
            Reference& operator|=(bool value);
            Reference& operator&=(bool value);
            Reference& operator^=(bool value);
            operator bool() const;
            bool operator~() const;
            Reference& flip();

        private:
            word_t* word_ptr_;
            size_t bit_index_;
        };

        class ConstSliceView {
        public:
            ConstSliceView(const Bitset* owner, size_t start, size_t length);

            size_t size() const noexcept;
            bool empty() const noexcept;

            bool operator[](size_t pos) const;
            bool test(size_t pos) const;

            bool all() const noexcept;
            bool any() const noexcept;
            bool none() const noexcept;
            size_t count() const noexcept;

            bool all(const Bitset& mask) const noexcept;
            bool any(const Bitset& mask) const noexcept;

            Bitset operator<<(size_t shift_amount) const noexcept;
            Bitset operator>>(size_t shift_amount) const noexcept;

            Bitset<> toBitset() const;
            Bitset<> shiftedLeft(size_t shift_amount) const;
            Bitset<> shiftedRight(size_t shift_amount) const;

        protected:
            const Bitset* owner_;
            size_t start_;
            size_t length_;

            word_t loadWord(size_t index) const noexcept;
        };

        class SliceView : public ConstSliceView {
        public:
            SliceView(Bitset* owner, size_t start, size_t length);

            Reference operator[](size_t pos);

            SliceView& set() noexcept;
            SliceView& set(size_t pos, bool val = true);

            SliceView& reset() noexcept;
            SliceView& reset(size_t pos);

            SliceView& flip() noexcept;
            SliceView& flip(size_t pos);

            SliceView& operator&=(const Bitset& other) noexcept;
            SliceView& operator|=(const Bitset& other) noexcept;
            SliceView& operator^=(const Bitset& other) noexcept;

            SliceView& operator&=(const ConstSliceView& other) noexcept;
            SliceView& operator|=(const ConstSliceView& other) noexcept;
            SliceView& operator^=(const ConstSliceView& other) noexcept;

            SliceView& operator<<=(size_t shift_amount) noexcept;
            SliceView& operator>>=(size_t shift_amount) noexcept;

        private:
            void storeWord(size_t index, word_t value, word_t slice_mask) noexcept;
        };
    
    // ===== Constructors and Assignment =====
    
    // Fixed-size constructors
    constexpr Bitset() noexcept requires(IS_FIXED);
    constexpr Bitset(unsigned long long val) noexcept requires(IS_FIXED);
    
    template<class CharT, class Traits, class Allocator>
    explicit Bitset(
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(IS_FIXED);
    
    // Dynamic-size constructors
    explicit Bitset(size_t num_bits = 0) requires(!IS_FIXED);
    Bitset(size_t num_bits, unsigned long long val) requires(!IS_FIXED);
    Bitset(size_t num_bits, bool value) requires(!IS_FIXED);
    
    template<class CharT, class Traits, class Allocator>
    explicit Bitset(
        size_t num_bits,
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(!IS_FIXED);
    
    Bitset(const Bitset& other);
    Bitset(Bitset&& other) noexcept;
    Bitset& operator=(const Bitset& other);
    Bitset& operator=(Bitset&& other) noexcept;
    ~Bitset();
    
    // Conversion: Fixed → Dynamic
    template<size_t M>
    explicit Bitset(const Bitset<M>& other) requires(!IS_FIXED && M != DYNAMIC_EXTENT);
    
    // ===== Element Access =====
    // operator[] - Unchecked access (UB if out of bounds, debug assertion only)
    // test()     - Checked access (throws std::out_of_range if out of bounds)
    
    constexpr bool operator[](size_t pos) const;
    Reference operator[](size_t pos);
    constexpr bool test(size_t pos) const;
    
    // ===== Capacity =====
    
    constexpr size_t size() const noexcept;
    constexpr size_t sizeBytes() const noexcept;
    constexpr bool empty() const noexcept;
    constexpr size_t capacity() const noexcept;
    static constexpr bool isFixedSize() noexcept;

    // ===== Views =====
    // Throws std::out_of_range if the range exceeds the Bitset size.
    SliceView slice(size_t start, size_t length);
    ConstSliceView slice(size_t start, size_t length) const;
    
    // ===== Modifiers =====
    // All single-bit modifiers (set/reset/flip with pos) throw std::out_of_range if out of bounds
    
    Bitset& set() noexcept;
    Bitset& set(size_t pos, bool val = true);  // Throws if pos >= size()
    
    Bitset& reset() noexcept;
    Bitset& reset(size_t pos);  // Throws if pos >= size()
    
    Bitset& flip() noexcept;
    Bitset& flip(size_t pos);  // Throws if pos >= size()
    
    // Dynamic-only modifiers
    void clear() noexcept requires(!IS_FIXED);
    void reserve(size_t bit_capacity) requires(!IS_FIXED);
    void resize(size_t new_size, bool value = false) requires(!IS_FIXED);
    void insert(size_t pos, bool value = false) requires(!IS_FIXED);  // Insert bit at pos, shifting subsequent bits right
    void shrinkToFit() requires(!IS_FIXED);
    
    // ===== Operations =====
    
    bool all() const noexcept;
    bool any() const noexcept;
    size_t count() const noexcept;
    bool none() const noexcept;

    // Masked queries (avoid temporary bitsets)
    // Mask is truncated to this Bitset's size; extra mask bits are ignored.
    bool all(const Bitset& mask) const noexcept;
    bool any(const Bitset& mask) const noexcept;
    
    // ===== Conversions =====
    
    unsigned long toUlong() const;
    unsigned long long toUllong() const;
    std::string toString(char zero = '0', char one = '1') const;
    
    // Dynamic → Fixed conversion (with validation)
    template<size_t M>
    Bitset<M> toFixed() const requires(!IS_FIXED);
    
    // ===== I/O and Binary Compatibility =====
    
    std::byte* data() noexcept;
    const std::byte* data() const noexcept;
    
    void readFrom(const void* src, size_t available);
    void writeTo(void* dst, size_t capacity) const;
    
    // ===== Bitwise Operators =====
    // For dynamic bitsets, compound ops truncate to this Bitset's size.
    
    Bitset operator~() const noexcept;
    
    Bitset& operator&=(const Bitset& other) noexcept;
    Bitset& operator|=(const Bitset& other) noexcept;
    Bitset& operator^=(const Bitset& other) noexcept;
    
    Bitset operator<<(size_t shift_amount) const noexcept;
    Bitset operator>>(size_t shift_amount) const noexcept;
    
    Bitset& operator<<=(size_t shift_amount) noexcept;
    Bitset& operator>>=(size_t shift_amount) noexcept;
};

// ===== Non-member Operators =====

template<size_t N>
Bitset<N> operator&(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept;

template<size_t N>
Bitset<N> operator|(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept;

template<size_t N>
Bitset<N> operator^(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept;

template<size_t N>
bool operator==(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept;

template<size_t N>
bool operator!=(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept;

template<class CharT, class Traits, size_t N>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, 
    const Bitset<N>& x);

template<class CharT, class Traits, size_t N>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is, 
    Bitset<N>& x);

} // namespace bcsv

// Include implementation
#include "bitset.hpp"
