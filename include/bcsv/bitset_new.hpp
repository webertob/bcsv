/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "bitset_platform.h"
#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <iostream>
#include <bit>
#include <cstring>
#include <limits>
#include <type_traits>

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
 * Storage is word-aligned (uint64_t on 64-bit, uint32_t on 32-bit) for optimal performance.
 * All operations work identically for both fixed and dynamic sizes.
 * 
 * @tparam N Number of bits (compile-time) or dynamic_extent for runtime size
 * 
 * Examples:
 *   bitset<64> fixed;              // Fixed 64 bits, stack storage
 *   bitset<> dynamic(64);          // Dynamic 64 bits, heap storage
 *   bitset<dynamic_extent> dyn;    // Explicit dynamic extent (same as bitset<>)
 */
template<size_t N = dynamic_extent>
class alignas(detail::WORD_ALIGN) bitset {
private:
    // Compile-time detection of fixed vs dynamic
    static constexpr bool is_fixed = (N != dynamic_extent);
    
    // For fixed-size: calculate word count at compile time
    static constexpr size_t word_count_fixed = 
        is_fixed ? detail::bits_to_words(N) : 0;
    
    // Storage type selection: array for fixed, vector for dynamic
    // Note: std::vector<storage_word_t> with default allocator is sufficient!
    // std::allocator already ensures alignof(storage_word_t) alignment.
    using storage_type = std::conditional_t<
        is_fixed,
        std::array<detail::storage_word_t, word_count_fixed>,
        std::vector<detail::storage_word_t>
    >;
    
    storage_type storage_;
    
    // Size storage: only for dynamic case, zero overhead for fixed case
    struct empty_size {};
    [[no_unique_address]] std::conditional_t<is_fixed, empty_size, size_t> bit_count_;
    
    // Friend declarations for operators that need access to internals
    template<size_t M> friend class bitset;
    template<size_t M> friend bool operator==(const bitset<M>&, const bitset<M>&) noexcept;
    
    // ===== Internal Helpers =====
    
    constexpr size_t word_count() const noexcept {
        if constexpr (is_fixed) {
            return word_count_fixed;
        } else {
            return storage_.size();
        }
    }
    
    constexpr size_t byte_count() const noexcept {
        return detail::bits_to_bytes(size());
    }
    
    constexpr void clear_unused_bits() noexcept {
        if (word_count() == 0) return;
        
        const size_t bits_in_last = size() % detail::WORD_BITS;
        if (bits_in_last == 0) return;
        
        const detail::storage_word_t mask = detail::last_word_mask(size());
        storage_[word_count() - 1] &= mask;
    }
    
    constexpr void set_from_value(unsigned long long val) noexcept {
        // Set bits from unsigned long long value
        const size_t words_to_set = std::min(
            sizeof(val) / detail::WORD_SIZE,
            word_count()
        );
        
        for (size_t i = 0; i < words_to_set; ++i) {
            storage_[i] = static_cast<detail::storage_word_t>(
                val >> (i * detail::WORD_BITS)
            );
        }
        
        clear_unused_bits();
    }
    
    template<class CharT, class Traits, class Allocator>
    void set_from_string(
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n,
        CharT zero, CharT one)
    {
        const auto len = std::min(n, str.length() - pos);
        
        for (size_t i = 0; i < std::min(len, static_cast<decltype(len)>(size())); ++i) {
            const auto ch = str[pos + len - 1 - i];
            if (ch == one) {
                set(i);
            } else if (ch != zero) {
                throw std::invalid_argument("bitset: invalid character in string");
            }
        }
    }
    
public:
    /**
     * @brief Proxy class for individual bit access
     */
    class reference {
    private:
        detail::storage_word_t* word_ptr;
        size_t bit_index;  // 0 to WORD_BITS-1
        
    public:
        reference(detail::storage_word_t* ptr, size_t bit_idx) 
            : word_ptr(ptr), bit_index(bit_idx) {}
        
        reference& operator=(bool value) {
            if (value) {
                *word_ptr |= (detail::storage_word_t{1} << bit_index);
            } else {
                *word_ptr &= ~(detail::storage_word_t{1} << bit_index);
            }
            return *this;
        }
        
        reference& operator=(const reference& other) {
            return *this = bool(other);
        }
        
        operator bool() const {
            return (*word_ptr & (detail::storage_word_t{1} << bit_index)) != 0;
        }
        
        bool operator~() const {
            return !bool(*this);
        }
        
        reference& flip() {
            *word_ptr ^= (detail::storage_word_t{1} << bit_index);
            return *this;
        }
    };
    
    // ===== Constructors =====
    
    // Fixed-size: default constructor
    constexpr bitset() noexcept requires(is_fixed) 
        : storage_{} {}
    
    // Fixed-size: from unsigned value
    constexpr bitset(unsigned long long val) noexcept requires(is_fixed)
        : storage_{} { 
        set_from_value(val); 
    }
    
    // Fixed-size: from string
    template<class CharT, class Traits, class Allocator>
    explicit bitset(
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(is_fixed)
        : storage_{} 
    {
        set_from_string(str, pos, n, zero, one);
    }
    
    // Dynamic-size: explicit size required
    explicit bitset(size_t num_bits) requires(!is_fixed)
        : storage_(detail::bits_to_words(num_bits), 0)
        , bit_count_(num_bits) {}
    
    // Dynamic-size: size + value
    bitset(size_t num_bits, unsigned long long val) requires(!is_fixed)
        : storage_(detail::bits_to_words(num_bits), 0)
        , bit_count_(num_bits) 
    { 
        set_from_value(val); 
    }
    
    // Dynamic-size: size + bool fill
    bitset(size_t num_bits, bool value) requires(!is_fixed)
        : storage_(detail::bits_to_words(num_bits), 
                   value ? ~detail::storage_word_t{0} : 0)
        , bit_count_(num_bits) 
    {
        if (value) clear_unused_bits();
    }
    
    // Dynamic-size: from string
    template<class CharT, class Traits, class Allocator>
    explicit bitset(
        size_t num_bits,
        const std::basic_string<CharT, Traits, Allocator>& str,
        typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
        typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
            std::basic_string<CharT, Traits, Allocator>::npos,
        CharT zero = CharT('0'),
        CharT one = CharT('1')) requires(!is_fixed)
        : storage_(detail::bits_to_words(num_bits), 0)
        , bit_count_(num_bits) 
    {
        set_from_string(str, pos, n, zero, one);
    }
    
    // Copy/move constructors (compiler-generated work for both fixed and dynamic)
    bitset(const bitset&) = default;
    bitset(bitset&&) noexcept = default;
    bitset& operator=(const bitset&) = default;
    bitset& operator=(bitset&&) noexcept = default;
    
    // Conversion constructor: Fixed → Dynamic
    template<size_t M>
    explicit bitset(const bitset<M>& other) requires(!is_fixed && M != dynamic_extent)
        : storage_(detail::bits_to_words(M), 0)
        , bit_count_(M)
    {
        std::memcpy(data(), other.data(), other.sizeBytes());
    }
    
    // ===== Size Queries =====
    
    constexpr size_t size() const noexcept {
        if constexpr (is_fixed) {
            return N;
        } else {
            return bit_count_;
        }
    }
    
    constexpr size_t sizeBytes() const noexcept {
        return byte_count();
    }
    
    constexpr bool empty() const noexcept {
        return size() == 0;
    }
    
    static constexpr bool is_fixed_size() noexcept {
        return is_fixed;
    }
    
    // ===== Bit Access =====
    
    constexpr bool operator[](size_t pos) const {
        if (pos >= size()) return false;
        
        const size_t word_idx = detail::bit_to_word_index(pos);
        const size_t bit_idx = detail::bit_to_bit_index(pos);
        
        return (storage_[word_idx] & (detail::storage_word_t{1} << bit_idx)) != 0;
    }
    
    reference operator[](size_t pos) {
        if (pos >= size()) {
            throw std::out_of_range("bitset::operator[]: index out of range");
        }
        
        const size_t word_idx = detail::bit_to_word_index(pos);
        const size_t bit_idx = detail::bit_to_bit_index(pos);
        
        return reference(&storage_[word_idx], bit_idx);
    }
    
    constexpr bool test(size_t pos) const {
        if (pos >= size()) {
            throw std::out_of_range("bitset::test: index out of range");
        }
        return (*this)[pos];
    }
    
    // ===== Bit Operations =====
    
    bitset& set() noexcept {
        for (size_t i = 0; i < word_count(); ++i) {
            storage_[i] = ~detail::storage_word_t{0};
        }
        clear_unused_bits();
        return *this;
    }
    
    bitset& set(size_t pos, bool val = true) {
        if (pos >= size()) {
            throw std::out_of_range("bitset::set: index out of range");
        }
        
        const size_t word_idx = detail::bit_to_word_index(pos);
        const size_t bit_idx = detail::bit_to_bit_index(pos);
        
        if (val) {
            storage_[word_idx] |= (detail::storage_word_t{1} << bit_idx);
        } else {
            storage_[word_idx] &= ~(detail::storage_word_t{1} << bit_idx);
        }
        return *this;
    }
    
    bitset& reset() noexcept {
        for (size_t i = 0; i < word_count(); ++i) {
            storage_[i] = 0;
        }
        return *this;
    }
    
    bitset& reset(size_t pos) {
        return set(pos, false);
    }
    
    bitset& flip() noexcept {
        for (size_t i = 0; i < word_count(); ++i) {
            storage_[i] = ~storage_[i];
        }
        clear_unused_bits();
        return *this;
    }
    
    bitset& flip(size_t pos) {
        if (pos >= size()) {
            throw std::out_of_range("bitset::flip: index out of range");
        }
        
        const size_t word_idx = detail::bit_to_word_index(pos);
        const size_t bit_idx = detail::bit_to_bit_index(pos);
        
        storage_[word_idx] ^= (detail::storage_word_t{1} << bit_idx);
        return *this;
    }
    
    // ===== Queries =====
    
    size_t count() const noexcept {
        size_t total = 0;
        for (size_t i = 0; i < word_count(); ++i) {
            total += std::popcount(storage_[i]);
        }
        return total;
    }
    
    bool any() const noexcept {
        for (size_t i = 0; i < word_count(); ++i) {
            if (storage_[i] != 0) return true;
        }
        return false;
    }
    
    bool all() const noexcept {
        const size_t wc = word_count();
        if (wc == 0) return true;
        
        // Check full words
        for (size_t i = 0; i < wc - 1; ++i) {
            if (storage_[i] != ~detail::storage_word_t{0}) {
                return false;
            }
        }
        
        // Check last word (may be partial)
        const detail::storage_word_t mask = detail::last_word_mask(size());
        return (storage_[wc - 1] & mask) == mask;
    }
    
    bool none() const noexcept {
        return !any();
    }
    
    // ===== Bitwise Operations =====
    
    bitset operator~() const noexcept {
        bitset result = *this;
        result.flip();
        return result;
    }
    
    bitset& operator&=(const bitset& other) noexcept {
        const size_t wc = std::min(word_count(), other.word_count());
        for (size_t i = 0; i < wc; ++i) {
            storage_[i] &= other.storage_[i];
        }
        // Zero out any remaining words if this is larger
        for (size_t i = wc; i < word_count(); ++i) {
            storage_[i] = 0;
        }
        return *this;
    }
    
    bitset& operator|=(const bitset& other) noexcept {
        const size_t wc = std::min(word_count(), other.word_count());
        for (size_t i = 0; i < wc; ++i) {
            storage_[i] |= other.storage_[i];
        }
        return *this;
    }
    
    bitset& operator^=(const bitset& other) noexcept {
        const size_t wc = std::min(word_count(), other.word_count());
        for (size_t i = 0; i < wc; ++i) {
            storage_[i] ^= other.storage_[i];
        }
        return *this;
    }
    
    // ===== Shift Operations =====
    
    bitset operator<<(size_t shift_amount) const noexcept {
        bitset result;
        
        if constexpr (!is_fixed) {
            result = bitset(size());
        }
        
        if (shift_amount >= size()) {
            return result;  // All bits shifted out
        }
        
        if (shift_amount == 0) {
            return *this;
        }
        
        const size_t word_shift = shift_amount / detail::WORD_BITS;
        const size_t bit_shift = shift_amount % detail::WORD_BITS;
        
        if (bit_shift == 0) {
            // Pure word shift
            for (size_t i = word_shift; i < word_count(); ++i) {
                result.storage_[i] = storage_[i - word_shift];
            }
        } else {
            // Mixed word + bit shift
            const size_t inv_shift = detail::WORD_BITS - bit_shift;
            
            for (size_t i = word_shift; i < word_count(); ++i) {
                result.storage_[i] = storage_[i - word_shift] << bit_shift;
                
                if (i > word_shift) {
                    result.storage_[i] |= storage_[i - word_shift - 1] >> inv_shift;
                }
            }
        }
        
        result.clear_unused_bits();
        return result;
    }
    
    bitset operator>>(size_t shift_amount) const noexcept {
        bitset result;
        
        if constexpr (!is_fixed) {
            result = bitset(size());
        }
        
        if (shift_amount >= size()) {
            return result;  // All bits shifted out
        }
        
        if (shift_amount == 0) {
            return *this;
        }
        
        const size_t word_shift = shift_amount / detail::WORD_BITS;
        const size_t bit_shift = shift_amount % detail::WORD_BITS;
        
        if (bit_shift == 0) {
            // Pure word shift
            for (size_t i = 0; i < word_count() - word_shift; ++i) {
                result.storage_[i] = storage_[i + word_shift];
            }
        } else {
            // Mixed word + bit shift
            const size_t inv_shift = detail::WORD_BITS - bit_shift;
            
            for (size_t i = 0; i < word_count() - word_shift; ++i) {
                result.storage_[i] = storage_[i + word_shift] >> bit_shift;
                
                if (i + word_shift + 1 < word_count()) {
                    result.storage_[i] |= storage_[i + word_shift + 1] << inv_shift;
                }
            }
        }
        
        return result;
    }
    
    bitset& operator<<=(size_t shift_amount) noexcept {
        *this = *this << shift_amount;
        return *this;
    }
    
    bitset& operator>>=(size_t shift_amount) noexcept {
        *this = *this >> shift_amount;
        return *this;
    }
    
    // ===== Dynamic Operations (only for dynamic_extent) =====
    
    void resize(size_t new_size, bool value = false) requires(!is_fixed) {
        const size_t old_bit_count = bit_count_;
        const size_t old_word_count = storage_.size();
        
        bit_count_ = new_size;
        const size_t new_word_count = detail::bits_to_words(new_size);
        storage_.resize(new_word_count, value ? ~detail::storage_word_t{0} : 0);
        
        // If expanding with value=true, we need to set bits from old_bit_count to new_size
        // std::vector::resize only fills NEW words, not existing partial words
        if (value && new_size > old_bit_count) {
            // Fill any remaining bits in the last old word
            const size_t old_last_word = old_word_count > 0 ? old_word_count - 1 : 0;
            if (old_word_count > 0) {
                const size_t start_bit_in_word = old_bit_count % detail::WORD_BITS;
                for (size_t bit = start_bit_in_word; bit < detail::WORD_BITS; ++bit) {
                    storage_[old_last_word] |= (detail::storage_word_t{1} << bit);
                }
            }
        }
        
        clear_unused_bits();
    }
    
    void reserve(size_t bit_capacity) requires(!is_fixed) {
        storage_.reserve(detail::bits_to_words(bit_capacity));
    }
    
    void shrink_to_fit() requires(!is_fixed) {
        storage_.shrink_to_fit();
    }
    
    void clear() noexcept requires(!is_fixed) {
        bit_count_ = 0;
        storage_.clear();
    }
    
    // ===== Conversion Methods =====
    
    unsigned long to_ulong() const {
        // Check if any bits beyond position 31 are set
        if (size() > 32) {
            for (size_t i = 32; i < size(); ++i) {
                if ((*this)[i]) {
                    throw std::overflow_error("bitset::to_ulong: value too large");
                }
            }
        }
        
        unsigned long result = 0;
        const size_t words_to_copy = std::min(
            sizeof(unsigned long) / detail::WORD_SIZE,
            word_count()
        );
        
        for (size_t i = 0; i < words_to_copy; ++i) {
            result |= static_cast<unsigned long>(storage_[i]) << (i * detail::WORD_BITS);
        }
        
        return result;
    }
    
    unsigned long long to_ullong() const {
        // Check if any bits beyond position 63 are set
        if (size() > 64) {
            for (size_t i = 64; i < size(); ++i) {
                if ((*this)[i]) {
                    throw std::overflow_error("bitset::to_ullong: value too large");
                }
            }
        }
        
        unsigned long long result = 0;
        const size_t words_to_copy = std::min(
            sizeof(unsigned long long) / detail::WORD_SIZE,
            word_count()
        );
        
        for (size_t i = 0; i < words_to_copy; ++i) {
            result |= static_cast<unsigned long long>(storage_[i]) << (i * detail::WORD_BITS);
        }
        
        return result;
    }
    
    std::string to_string(char zero = '0', char one = '1') const {
        std::string result;
        result.reserve(size());
        
        for (size_t i = size(); i > 0; --i) {
            result += (*this)[i - 1] ? one : zero;
        }
        
        return result;
    }
    
    // ===== I/O and Binary Compatibility =====
    
    std::byte* data() noexcept {
        return reinterpret_cast<std::byte*>(storage_.data());
    }
    
    const std::byte* data() const noexcept {
        return reinterpret_cast<const std::byte*>(storage_.data());
    }
    
    void writeTo(void* dst, size_t capacity) const {
        if (capacity < byte_count()) {
            throw std::out_of_range("bitset::writeTo: insufficient capacity");
        }
        std::memcpy(dst, data(), byte_count());
    }
    
    void readFrom(const void* src, size_t available) {
        if (available < byte_count()) {
            throw std::out_of_range("bitset::readFrom: insufficient data");
        }
        std::memcpy(data(), src, byte_count());
        clear_unused_bits();
    }
    
    // Dynamic → Fixed conversion (explicit, with validation)
    template<size_t M>
    bitset<M> to_fixed() const requires(!is_fixed) {
        if (size() != M) {
            throw std::invalid_argument(
                "bitset::to_fixed: size mismatch (expected " + 
                std::to_string(M) + ", got " + std::to_string(size()) + ")"
            );
        }
        
        bitset<M> result;
        std::memcpy(result.data(), data(), byte_count());
        return result;
    }
};

// ===== Non-member Operators =====

template<size_t N>
bitset<N> operator&(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    bitset<N> result = lhs;
    result &= rhs;
    return result;
}

template<size_t N>
bitset<N> operator|(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    bitset<N> result = lhs;
    result |= rhs;
    return result;
}

template<size_t N>
bitset<N> operator^(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    bitset<N> result = lhs;
    result ^= rhs;
    return result;
}

template<size_t N>
bool operator==(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    if constexpr (N == dynamic_extent) {
        if (lhs.size() != rhs.size()) return false;
    }
    
    const size_t wc = lhs.word_count();
    for (size_t i = 0; i < wc; ++i) {
        if (lhs.storage_[i] != rhs.storage_[i]) return false;
    }
    return true;
}

template<size_t N>
bool operator!=(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    return !(lhs == rhs);
}

template<class CharT, class Traits, size_t N>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, 
    const bitset<N>& x) 
{
    return os << x.to_string(CharT('0'), CharT('1'));
}

template<class CharT, class Traits, size_t N>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is, 
    bitset<N>& x) 
{
    std::basic_string<CharT, Traits> str;
    str.reserve(x.size());
    
    CharT ch;
    for (size_t i = 0; i < x.size() && is >> ch; ++i) {
        if (ch != CharT('0') && ch != CharT('1')) {
            is.putback(ch);
            break;
        }
        str += ch;
    }
    
    if (!str.empty()) {
        if constexpr (N == dynamic_extent) {
            x = bitset<>(str.length(), str);
        } else {
            x = bitset<N>(str);
        }
    }
    
    return is;
}

} // namespace bcsv

// Hash support for bcsv::bitset
namespace std {
    template<size_t N>
    struct hash<bcsv::bitset<N>> {
        size_t operator()(const bcsv::bitset<N>& bs) const noexcept {
            size_t result = 0;
            
            // FNV-1a hash algorithm
            constexpr size_t fnv_prime = sizeof(size_t) == 8 ? 1099511628211ULL : 16777619UL;
            constexpr size_t fnv_offset = sizeof(size_t) == 8 ? 14695981039346656037ULL : 2166136261UL;
            
            result = fnv_offset;
            
            // Hash the bit count first (for dynamic bitsets)
            if constexpr (N == bcsv::dynamic_extent) {
                result ^= bs.size();
                result *= fnv_prime;
            }
            
            // Hash the byte data
            const auto* data = bs.data();
            for (size_t i = 0; i < bs.sizeBytes(); ++i) {
                result ^= static_cast<size_t>(data[i]);
                result *= fnv_prime;
            }
            
            return result;
        }
    };
}
