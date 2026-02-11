/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once
#include "bitset.h"

namespace bcsv {

// ===== Internal Helper Implementations =====

template<size_t N>
constexpr size_t bitset<N>::wordCount() const noexcept {
    if constexpr (IS_FIXED) {
        return WORD_COUNT_FIXED;
    }
    return bitsToWords(size());
}

template<size_t N>
constexpr size_t bitset<N>::byteCount() const noexcept {
    return bitsToBytes(size());
}

template<size_t N>
constexpr bool bitset<N>::usesInline() const noexcept {
    if constexpr (IS_FIXED) {
        return false;
    } else {
        return wordCount() <= 1;
    }
}

template<size_t N>
constexpr typename bitset<N>::word_t* bitset<N>::wordData() noexcept {
    if constexpr (IS_FIXED) {
        return data_.data();
    } else {
        if (usesInline()) {
            return reinterpret_cast<word_t*>(&data_);
        }
        return reinterpret_cast<word_t*>(data_);
    }
}

template<size_t N>
constexpr const typename bitset<N>::word_t* bitset<N>::wordData() const noexcept {
    if constexpr (IS_FIXED) {
        return data_.data();
    } else {
        if (usesInline()) {
            return reinterpret_cast<const word_t*>(&data_);
        }
        return reinterpret_cast<const word_t*>(data_);
    }
}

template<size_t N>
void bitset<N>::releaseHeap() noexcept {
    if constexpr (!IS_FIXED) {
        if (!usesInline() && data_ != 0) {
            delete[] reinterpret_cast<word_t*>(data_);
            data_ = 0;
        }
    }
}

template<size_t N>
void bitset<N>::resizeStorage(size_t old_size, size_t new_size, word_t value) {
    if constexpr (IS_FIXED) {
        (void)old_size;
        (void)new_size;
        (void)value;
        return;
    } else {
        const size_t old_word_count = bitsToWords(old_size);
        const size_t new_word_count = bitsToWords(new_size);

        if (new_word_count <= 1) {
            word_t preserved = 0;
            if (old_word_count > 0) {
                if (old_size <= WORD_BITS) {
                    preserved = static_cast<word_t>(data_);
                } else {
                    preserved = reinterpret_cast<word_t*>(data_)[0];
                }
            }
            if (old_size > WORD_BITS && data_ != 0) {
                delete[] reinterpret_cast<word_t*>(data_);
            }
            if (new_word_count == 1) {
                if (old_word_count == 0) {
                    preserved = value;
                }
                data_ = static_cast<std::uintptr_t>(preserved);
            } else {
                data_ = 0;
            }
            return;
        }

        word_t* new_data = new word_t[new_word_count];
        const size_t copy_words = std::min(old_word_count, new_word_count);

        if (copy_words > 0) {
            if (old_size <= WORD_BITS) {
                new_data[0] = static_cast<word_t>(data_);
            } else if (data_ != 0) {
                std::memcpy(new_data, reinterpret_cast<word_t*>(data_), copy_words * sizeof(word_t));
            }
        }

        if (new_word_count > copy_words) {
            std::fill(new_data + copy_words, new_data + new_word_count, value);
        }

        if (old_size > WORD_BITS && data_ != 0) {
            delete[] reinterpret_cast<word_t*>(data_);
        }
        data_ = reinterpret_cast<std::uintptr_t>(new_data);
    }
}

template<size_t N>
constexpr void bitset<N>::clearUnusedBits() noexcept {
    if (wordCount() == 0) return;
    
    const size_t bits_in_last = size() % WORD_BITS;
    if (bits_in_last == 0) return;
    
    const word_t mask = lastWordMask(size());
    wordData()[wordCount() - 1] &= mask;
}

template<size_t N>
constexpr void bitset<N>::setFromValue(unsigned long long val) noexcept {
    const size_t words_to_set = std::min(
        sizeof(val) / WORD_SIZE,
        wordCount()
    );
    
    for (size_t i = 0; i < words_to_set; ++i) {
        wordData()[i] = static_cast<word_t>(
            val >> (i * WORD_BITS)
        );
    }
    
    clearUnusedBits();
}

template<size_t N>
template<class CharT, class Traits, class Allocator>
void bitset<N>::setFromString(
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

// ===== Reference Proxy Implementation =====

template<size_t N>
bitset<N>::reference::reference(word_t* ptr, size_t bit_idx) 
    : word_ptr_(ptr), bit_index_(bit_idx) {}

template<size_t N>
typename bitset<N>::reference& bitset<N>::reference::operator=(bool value) {
    if (value) {
        *word_ptr_ |= (word_t{1} << bit_index_);
    } else {
        *word_ptr_ &= ~(word_t{1} << bit_index_);
    }
    return *this;
}

template<size_t N>
typename bitset<N>::reference& bitset<N>::reference::operator=(const reference& other) {
    return *this = bool(other);
}

template<size_t N>
bitset<N>::reference::operator bool() const {
    return (*word_ptr_ & (word_t{1} << bit_index_)) != 0;
}

template<size_t N>
bool bitset<N>::reference::operator~() const {
    return !bool(*this);
}

template<size_t N>
typename bitset<N>::reference& bitset<N>::reference::flip() {
    *word_ptr_ ^= (word_t{1} << bit_index_);
    return *this;
}

template<size_t N>
typename bitset<N>::reference& bitset<N>::reference::operator|=(bool value) {
    if (value) {
        *word_ptr_ |= (word_t{1} << bit_index_);
    }
    return *this;
}

template<size_t N>
typename bitset<N>::reference& bitset<N>::reference::operator&=(bool value) {
    if (!value) {
        *word_ptr_ &= ~(word_t{1} << bit_index_);
    }
    return *this;
}

template<size_t N>
typename bitset<N>::reference& bitset<N>::reference::operator^=(bool value) {
    if (value) {
        *word_ptr_ ^= (word_t{1} << bit_index_);
    }
    return *this;
}

// ===== Constructor Implementations =====

// Fixed-size constructors
template<size_t N>
constexpr bitset<N>::bitset() noexcept requires(IS_FIXED) 
    : data_{} {}

template<size_t N>
constexpr bitset<N>::bitset(unsigned long long val) noexcept requires(IS_FIXED)
    : data_{} { 
    setFromValue(val); 
}

template<size_t N>
template<class CharT, class Traits, class Allocator>
bitset<N>::bitset(
    const std::basic_string<CharT, Traits, Allocator>& str,
    typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
    typename std::basic_string<CharT, Traits, Allocator>::size_type n,
    CharT zero,
    CharT one) requires(IS_FIXED)
    : data_{} 
{
    setFromString(str, pos, n, zero, one);
}

// Dynamic-size constructors
template<size_t N>
bitset<N>::bitset(size_t num_bits) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{
    resizeStorage(0, size_, 0);
}

template<size_t N>
bitset<N>::bitset(size_t num_bits, unsigned long long val) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{ 
    resizeStorage(0, size_, 0);
    setFromValue(val); 
}

template<size_t N>
bitset<N>::bitset(size_t num_bits, bool value) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{
    resizeStorage(0, size_, value ? ~word_t{0} : 0);
    if (value) clearUnusedBits();
}

template<size_t N>
template<class CharT, class Traits, class Allocator>
bitset<N>::bitset(
    size_t num_bits,
    const std::basic_string<CharT, Traits, Allocator>& str,
    typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
    typename std::basic_string<CharT, Traits, Allocator>::size_type n,
    CharT zero,
    CharT one) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{
    resizeStorage(0, size_, 0);
    setFromString(str, pos, n, zero, one);
}

// Conversion: Fixed → Dynamic
template<size_t N>
template<size_t M>
bitset<N>::bitset(const bitset<M>& other) requires(!IS_FIXED && M != dynamic_extent)
    : size_(M)
    , data_(0)
{
    resizeStorage(0, size_, 0);
    std::memcpy(data(), other.data(), other.sizeBytes());
}

// ===== Element Access Implementations =====

template<size_t N>
bitset<N>::bitset(const bitset& other) {
    if constexpr (IS_FIXED) {
        data_ = other.data_;
    } else {
        size_ = other.size_;
        data_ = 0;
        resizeStorage(0, size_, 0);
        if (size_ > 0) {
            std::memcpy(data(), other.data(), byteCount());
        }
    }
}

template<size_t N>
bitset<N>::bitset(bitset&& other) noexcept {
    if constexpr (IS_FIXED) {
        data_ = std::move(other.data_);
    } else {
        size_ = other.size_;
        data_ = other.data_;
        if (other.usesInline()) {
            data_ = other.data_;
        } else {
            other.data_ = 0;
        }
        other.size_ = 0;
    }
}

template<size_t N>
bitset<N>& bitset<N>::operator=(const bitset& other) {
    if (this == &other) {
        return *this;
    }
    if constexpr (IS_FIXED) {
        data_ = other.data_;
    } else {
        releaseHeap();
        size_ = other.size_;
        data_ = 0;
        resizeStorage(0, size_, 0);
        if (size_ > 0) {
            std::memcpy(data(), other.data(), byteCount());
        }
    }
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::operator=(bitset&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if constexpr (IS_FIXED) {
        data_ = std::move(other.data_);
    } else {
        releaseHeap();
        size_ = other.size_;
        data_ = other.data_;
        if (other.usesInline()) {
            data_ = other.data_;
        } else {
            other.data_ = 0;
        }
        other.size_ = 0;
    }
    return *this;
}

template<size_t N>
bitset<N>::~bitset() {
    if constexpr (!IS_FIXED) {
        releaseHeap();
    }
}

template<size_t N>
constexpr bool bitset<N>::operator[](size_t pos) const {
    // No bounds checking for performance (like std::vector::operator[])
    // Use test() for checked access
    assert(pos < size() && "bitset::operator[]: index out of range");
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    return (wordData()[word_idx] & (word_t{1} << bit_idx)) != 0;
}

template<size_t N>
typename bitset<N>::reference bitset<N>::operator[](size_t pos) {
    // No bounds checking for performance (like std::vector::operator[])
    // Use test() for checked access  
    assert(pos < size() && "bitset::operator[]: index out of range");
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    return reference(&wordData()[word_idx], bit_idx);
}

template<size_t N>
constexpr bool bitset<N>::test(size_t pos) const {
    if (pos >= size()) {
        throw std::out_of_range("bitset::test: index out of range");
    }
    return (*this)[pos];
}

// ===== Capacity Implementations =====

template<size_t N>
constexpr size_t bitset<N>::size() const noexcept {
    if constexpr (IS_FIXED) {
        return N;
    } else {
        return size_;
    }
}

template<size_t N>
constexpr size_t bitset<N>::sizeBytes() const noexcept {
    return byteCount();
}

template<size_t N>
constexpr bool bitset<N>::empty() const noexcept {
    return size() == 0;
}

template<size_t N>
constexpr size_t bitset<N>::capacity() const noexcept {
    return wordCount() * WORD_BITS;
}

// ===== Slice View Implementations =====

template<size_t N>
bitset<N>::const_slice_view::const_slice_view(
    const bitset* owner,
    size_t start,
    size_t length)
    : owner_(owner), start_(start), length_(length) {}

template<size_t N>
size_t bitset<N>::const_slice_view::size() const noexcept {
    return length_;
}

template<size_t N>
bool bitset<N>::const_slice_view::empty() const noexcept {
    return length_ == 0;
}

template<size_t N>
typename bitset<N>::const_slice_view::slice_meta
bitset<N>::const_slice_view::meta() const noexcept {
    slice_meta info{};
    info.start_word_ = start_ / WORD_BITS;
    info.start_bit_ = start_ % WORD_BITS;
    info.word_count_ = (length_ + WORD_BITS - 1) / WORD_BITS;
    info.tail_bits_ = length_ % WORD_BITS;
    info.tail_mask_ = info.tail_bits_ == 0 ? ~word_t{0} : (word_t{1} << info.tail_bits_) - 1;
    return info;
}

template<size_t N>
typename bitset<N>::word_t
bitset<N>::const_slice_view::load_word(size_t index, const slice_meta& meta) const noexcept {
    const size_t base = meta.start_word_ + index;
    word_t low = owner_->wordData()[base] >> meta.start_bit_;
    word_t high = 0;
    if (meta.start_bit_ != 0 && base + 1 < owner_->wordCount()) {
        high = owner_->wordData()[base + 1] << (WORD_BITS - meta.start_bit_);
    }
    word_t value = low | high;
    if (index + 1 == meta.word_count_ && meta.tail_bits_ != 0) {
        value &= meta.tail_mask_;
    }
    return value;
}

template<size_t N>
bool bitset<N>::const_slice_view::operator[](size_t pos) const {
    assert(pos < length_ && "bitset::slice_view::operator[]: index out of range");
    return (*owner_)[start_ + pos];
}

template<size_t N>
bool bitset<N>::const_slice_view::test(size_t pos) const {
    if (pos >= length_) {
        throw std::out_of_range("bitset::slice_view::test: index out of range");
    }
    return (*owner_)[start_ + pos];
}

template<size_t N>
bool bitset<N>::const_slice_view::all() const noexcept {
    if (length_ == 0) {
        return true;
    }

    const size_t start_word = start_ / WORD_BITS;
    const size_t start_bit = start_ % WORD_BITS;
    const size_t end = start_ + length_;
    const size_t end_word = (end - 1) / WORD_BITS;
    const size_t end_bit = end % WORD_BITS;

    auto mask_from = [](size_t bits) {
        return bits == 0 ? word_t{0} : (bits >= WORD_BITS ? ~word_t{0} : ((word_t{1} << bits) - 1));
    };

    if (start_word == end_word) {
        word_t mask = ~word_t{0} << start_bit;
        if (end_bit != 0) {
            mask &= mask_from(end_bit);
        }
        return (owner_->wordData()[start_word] & mask) == mask;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    if ((owner_->wordData()[start_word] & first_mask) != first_mask) {
        return false;
    }

    for (size_t w = start_word + 1; w < end_word; ++w) {
        if (owner_->wordData()[w] != ~word_t{0}) {
            return false;
        }
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    return (owner_->wordData()[end_word] & last_mask) == last_mask;
}

template<size_t N>
bool bitset<N>::const_slice_view::any() const noexcept {
    if (length_ == 0) {
        return false;
    }

    const size_t start_word = start_ / WORD_BITS;
    const size_t start_bit = start_ % WORD_BITS;
    const size_t end = start_ + length_;
    const size_t end_word = (end - 1) / WORD_BITS;
    const size_t end_bit = end % WORD_BITS;

    auto mask_from = [](size_t bits) {
        return bits == 0 ? word_t{0} : (bits >= WORD_BITS ? ~word_t{0} : ((word_t{1} << bits) - 1));
    };

    if (start_word == end_word) {
        word_t mask = ~word_t{0} << start_bit;
        if (end_bit != 0) {
            mask &= mask_from(end_bit);
        }
        return (owner_->wordData()[start_word] & mask) != 0;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    if (owner_->wordData()[start_word] & first_mask) {
        return true;
    }

    for (size_t w = start_word + 1; w < end_word; ++w) {
        if (owner_->wordData()[w]) {
            return true;
        }
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    return (owner_->wordData()[end_word] & last_mask) != 0;
}

template<size_t N>
bool bitset<N>::const_slice_view::none() const noexcept {
    return !any();
}

template<size_t N>
size_t bitset<N>::const_slice_view::count() const noexcept {
    if (length_ == 0) {
        return 0;
    }

    const size_t start_word = start_ / WORD_BITS;
    const size_t start_bit = start_ % WORD_BITS;
    const size_t end = start_ + length_;
    const size_t end_word = (end - 1) / WORD_BITS;
    const size_t end_bit = end % WORD_BITS;

    auto mask_from = [](size_t bits) {
        return bits == 0 ? word_t{0} : (bits >= WORD_BITS ? ~word_t{0} : ((word_t{1} << bits) - 1));
    };

    size_t total = 0;

    if (start_word == end_word) {
        word_t mask = ~word_t{0} << start_bit;
        if (end_bit != 0) {
            mask &= mask_from(end_bit);
        }
        return std::popcount(owner_->wordData()[start_word] & mask);
    }

    word_t first_mask = ~word_t{0} << start_bit;
    total += std::popcount(owner_->wordData()[start_word] & first_mask);

    for (size_t w = start_word + 1; w < end_word; ++w) {
        total += std::popcount(owner_->wordData()[w]);
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    total += std::popcount(owner_->wordData()[end_word] & last_mask);
    return total;
}

template<size_t N>
bool bitset<N>::const_slice_view::all(const bitset& mask) const noexcept {
    const size_t limit = std::min(length_, mask.size());
    for (size_t i = 0; i < limit; ++i) {
        if (mask.test(i) && !(*owner_)[start_ + i]) {
            return false;
        }
    }
    return true;
}

template<size_t N>
bool bitset<N>::const_slice_view::any(const bitset& mask) const noexcept {
    const size_t limit = std::min(length_, mask.size());
    for (size_t i = 0; i < limit; ++i) {
        if (mask.test(i) && (*owner_)[start_ + i]) {
            return true;
        }
    }
    return false;
}

template<size_t N>
bitset<N> bitset<N>::const_slice_view::operator<<(size_t shift_amount) const noexcept {
    bitset result = *owner_;
    auto view = result.slice(start_, length_);
    view <<= shift_amount;
    return result;
}

template<size_t N>
bitset<N> bitset<N>::const_slice_view::operator>>(size_t shift_amount) const noexcept {
    bitset result = *owner_;
    auto view = result.slice(start_, length_);
    view >>= shift_amount;
    return result;
}

template<size_t N>
bitset<> bitset<N>::const_slice_view::to_bitset() const {
    bitset<> result(length_);
    if (length_ == 0) {
        return result;
    }

    const auto info = meta();
    for (size_t i = 0; i < info.word_count_; ++i) {
        result.wordData()[i] = load_word(i, info);
    }
    result.clearUnusedBits();
    return result;
}

template<size_t N>
bitset<> bitset<N>::const_slice_view::shifted_left(size_t shift_amount) const {
    bitset<> result = to_bitset();
    result <<= shift_amount;
    return result;
}

template<size_t N>
bitset<> bitset<N>::const_slice_view::shifted_right(size_t shift_amount) const {
    bitset<> result = to_bitset();
    result >>= shift_amount;
    return result;
}

template<size_t N>
bitset<N>::slice_view::slice_view(bitset* owner, size_t start, size_t length)
    : const_slice_view(owner, start, length) {}

template<size_t N>
void bitset<N>::slice_view::store_word(
    size_t index,
    word_t value,
    word_t slice_mask,
    const typename bitset<N>::const_slice_view::slice_meta& meta) noexcept
{
    const size_t base = meta.start_word_ + index;
    word_t masked_value = value & slice_mask;
    word_t low_mask = slice_mask << meta.start_bit_;
    word_t low_bits = masked_value << meta.start_bit_;
    word_t* owner_data = (*const_cast<bitset*>(this->owner_)).wordData();
    owner_data[base] = (owner_data[base] & ~low_mask) | low_bits;

    if (meta.start_bit_ != 0 && base + 1 < this->owner_->wordCount()) {
        word_t high_mask = slice_mask >> (WORD_BITS - meta.start_bit_);
        if (high_mask != 0) {
            word_t high_bits = masked_value >> (WORD_BITS - meta.start_bit_);
            owner_data[base + 1] = (owner_data[base + 1] & ~high_mask) | high_bits;
        }
    }
}

template<size_t N>
typename bitset<N>::reference bitset<N>::slice_view::operator[](size_t pos) {
    assert(pos < this->length_ && "bitset::slice_view::operator[]: index out of range");
    return (*const_cast<bitset*>(this->owner_))[this->start_ + pos];
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::set() noexcept {
    if (this->length_ == 0) {
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t start_bit = this->start_ % WORD_BITS;
    const size_t end = this->start_ + this->length_;
    const size_t end_word = (end - 1) / WORD_BITS;
    const size_t end_bit = end % WORD_BITS;

    auto mask_from = [](size_t bits) {
        return bits == 0 ? word_t{0} : (bits >= WORD_BITS ? ~word_t{0} : ((word_t{1} << bits) - 1));
    };

    if (start_word == end_word) {
        word_t mask = ~word_t{0} << start_bit;
        if (end_bit != 0) {
            mask &= mask_from(end_bit);
        }
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word] |= mask;
        return *this;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    (*const_cast<bitset*>(this->owner_)).wordData()[start_word] |= first_mask;

    for (size_t w = start_word + 1; w < end_word; ++w) {
        (*const_cast<bitset*>(this->owner_)).wordData()[w] = ~word_t{0};
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    (*const_cast<bitset*>(this->owner_)).wordData()[end_word] |= last_mask;
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::set(size_t pos, bool val) {
    if (pos >= this->length_) {
        throw std::out_of_range("bitset::slice_view::set: index out of range");
    }
    (*const_cast<bitset*>(this->owner_)).set(this->start_ + pos, val);
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::reset() noexcept {
    if (this->length_ == 0) {
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t start_bit = this->start_ % WORD_BITS;
    const size_t end = this->start_ + this->length_;
    const size_t end_word = (end - 1) / WORD_BITS;
    const size_t end_bit = end % WORD_BITS;

    auto mask_from = [](size_t bits) {
        return bits == 0 ? word_t{0} : (bits >= WORD_BITS ? ~word_t{0} : ((word_t{1} << bits) - 1));
    };

    if (start_word == end_word) {
        word_t mask = ~word_t{0} << start_bit;
        if (end_bit != 0) {
            mask &= mask_from(end_bit);
        }
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word] &= ~mask;
        return *this;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    (*const_cast<bitset*>(this->owner_)).wordData()[start_word] &= ~first_mask;

    for (size_t w = start_word + 1; w < end_word; ++w) {
        (*const_cast<bitset*>(this->owner_)).wordData()[w] = 0;
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    (*const_cast<bitset*>(this->owner_)).wordData()[end_word] &= ~last_mask;
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::reset(size_t pos) {
    return set(pos, false);
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::flip() noexcept {
    if (this->length_ == 0) {
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t start_bit = this->start_ % WORD_BITS;
    const size_t end = this->start_ + this->length_;
    const size_t end_word = (end - 1) / WORD_BITS;
    const size_t end_bit = end % WORD_BITS;

    auto mask_from = [](size_t bits) {
        return bits == 0 ? word_t{0} : (bits >= WORD_BITS ? ~word_t{0} : ((word_t{1} << bits) - 1));
    };

    if (start_word == end_word) {
        word_t mask = ~word_t{0} << start_bit;
        if (end_bit != 0) {
            mask &= mask_from(end_bit);
        }
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word] ^= mask;
        return *this;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    (*const_cast<bitset*>(this->owner_)).wordData()[start_word] ^= first_mask;

    for (size_t w = start_word + 1; w < end_word; ++w) {
        (*const_cast<bitset*>(this->owner_)).wordData()[w] = ~(*const_cast<bitset*>(this->owner_)).wordData()[w];
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    (*const_cast<bitset*>(this->owner_)).wordData()[end_word] ^= last_mask;
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::flip(size_t pos) {
    if (pos >= this->length_) {
        throw std::out_of_range("bitset::slice_view::flip: index out of range");
    }
    (*const_cast<bitset*>(this->owner_)).flip(this->start_ + pos);
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator&=(const bitset& other) noexcept {
    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const auto info = this->meta();
        for (size_t i = 0; i < info.word_count_; ++i) {
            const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};
            const word_t slice_mask = (i + 1 == info.word_count_ && info.tail_bits_ != 0) ? info.tail_mask_ : ~word_t{0};
            const word_t updated = this->load_word(i, info) & other_word;
            store_word(i, updated, slice_mask, info);
        }
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
    const size_t tail_bits = this->length_ % WORD_BITS;
    const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

    for (size_t i = 0; i < word_count; ++i) {
        const size_t owner_word = start_word + i;
        const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};

        if (i + 1 == word_count && tail_bits != 0) {
            word_t* owner_data = (*const_cast<bitset*>(this->owner_)).wordData();
            word_t current = owner_data[owner_word];
            word_t updated = (current & other_word) & tail_mask;
            owner_data[owner_word] = (current & ~tail_mask) | updated;
        } else {
            (*const_cast<bitset*>(this->owner_)).wordData()[owner_word] &= other_word;
        }
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator|=(const bitset& other) noexcept {
    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const auto info = this->meta();
        for (size_t i = 0; i < info.word_count_; ++i) {
            const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};
            const word_t slice_mask = (i + 1 == info.word_count_ && info.tail_bits_ != 0) ? info.tail_mask_ : ~word_t{0};
            const word_t updated = this->load_word(i, info) | other_word;
            store_word(i, updated, slice_mask, info);
        }
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
    const size_t tail_bits = this->length_ % WORD_BITS;
    const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

    for (size_t i = 0; i < word_count; ++i) {
        const size_t owner_word = start_word + i;
        const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};

        if (i + 1 == word_count && tail_bits != 0) {
            word_t* owner_data = (*const_cast<bitset*>(this->owner_)).wordData();
            word_t current = owner_data[owner_word];
            word_t updated = (current | other_word) & tail_mask;
            owner_data[owner_word] = (current & ~tail_mask) | updated;
        } else {
            (*const_cast<bitset*>(this->owner_)).wordData()[owner_word] |= other_word;
        }
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator^=(const bitset& other) noexcept {
    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const auto info = this->meta();
        for (size_t i = 0; i < info.word_count_; ++i) {
            const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};
            const word_t slice_mask = (i + 1 == info.word_count_ && info.tail_bits_ != 0) ? info.tail_mask_ : ~word_t{0};
            const word_t updated = this->load_word(i, info) ^ other_word;
            store_word(i, updated, slice_mask, info);
        }
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
    const size_t tail_bits = this->length_ % WORD_BITS;
    const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

    for (size_t i = 0; i < word_count; ++i) {
        const size_t owner_word = start_word + i;
        const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};

        if (i + 1 == word_count && tail_bits != 0) {
            word_t* owner_data = (*const_cast<bitset*>(this->owner_)).wordData();
            word_t current = owner_data[owner_word];
            word_t updated = (current ^ other_word) & tail_mask;
            owner_data[owner_word] = (current & ~tail_mask) | updated;
        } else {
            (*const_cast<bitset*>(this->owner_)).wordData()[owner_word] ^= other_word;
        }
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator&=(const const_slice_view& other) noexcept {
    const size_t limit = std::min(this->length_, other.size());
    for (size_t i = 0; i < limit; ++i) {
        if (!other.test(i)) {
            (*const_cast<bitset*>(this->owner_)).set(this->start_ + i, false);
        }
    }
    for (size_t i = limit; i < this->length_; ++i) {
        (*const_cast<bitset*>(this->owner_)).set(this->start_ + i, false);
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator|=(const const_slice_view& other) noexcept {
    const size_t limit = std::min(this->length_, other.size());
    for (size_t i = 0; i < limit; ++i) {
        if (other.test(i)) {
            (*const_cast<bitset*>(this->owner_)).set(this->start_ + i, true);
        }
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator^=(const const_slice_view& other) noexcept {
    const size_t limit = std::min(this->length_, other.size());
    for (size_t i = 0; i < limit; ++i) {
        if (other.test(i)) {
            (*const_cast<bitset*>(this->owner_)).flip(this->start_ + i);
        }
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator<<=(size_t shift_amount) noexcept {
    if (shift_amount == 0 || this->length_ == 0) {
        return *this;
    }
    if (shift_amount >= this->length_) {
        reset();
        return *this;
    }

    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const auto info = this->meta();
        const size_t word_shift = shift_amount / WORD_BITS;
        const size_t bit_shift = shift_amount % WORD_BITS;

        for (size_t w = info.word_count_; w-- > 0;) {
            word_t value = 0;
            if (w >= word_shift) {
                const size_t src = w - word_shift;
                word_t src_word = this->load_word(src, info);
                value = src_word << bit_shift;
                if (bit_shift != 0 && src > 0) {
                    value |= this->load_word(src - 1, info) >> (WORD_BITS - bit_shift);
                }
            }
            const word_t slice_mask = (w + 1 == info.word_count_ && info.tail_bits_ != 0) ? info.tail_mask_ : ~word_t{0};
            store_word(w, value, slice_mask, info);
        }
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
    const size_t tail_bits = this->length_ % WORD_BITS;
    const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

    const size_t word_shift = shift_amount / WORD_BITS;
    const size_t bit_shift = shift_amount % WORD_BITS;

    for (size_t w = word_count; w-- > 0;) {
        word_t value = 0;
        if (w >= word_shift) {
            const size_t src = w - word_shift;
            word_t src_word = (*const_cast<bitset*>(this->owner_)).wordData()[start_word + src];
            if (src + 1 == word_count && tail_bits != 0) {
                src_word &= tail_mask;
            }
            value = src_word << bit_shift;
            if (bit_shift != 0 && src > 0) {
                word_t low_word = (*const_cast<bitset*>(this->owner_)).wordData()[start_word + src - 1];
                value |= low_word >> (WORD_BITS - bit_shift);
            }
        }
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word + w] = value;
    }

    if (tail_bits != 0) {
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word + word_count - 1] &= tail_mask;
    }
    return *this;
}

template<size_t N>
typename bitset<N>::slice_view& bitset<N>::slice_view::operator>>=(size_t shift_amount) noexcept {
    if (shift_amount == 0 || this->length_ == 0) {
        return *this;
    }
    if (shift_amount >= this->length_) {
        reset();
        return *this;
    }

    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const auto info = this->meta();
        const size_t word_shift = shift_amount / WORD_BITS;
        const size_t bit_shift = shift_amount % WORD_BITS;

        for (size_t w = 0; w < info.word_count_; ++w) {
            word_t value = 0;
            const size_t src = w + word_shift;
            if (src < info.word_count_) {
                word_t src_word = this->load_word(src, info);
                value = src_word >> bit_shift;
                if (bit_shift != 0 && src + 1 < info.word_count_) {
                    value |= this->load_word(src + 1, info) << (WORD_BITS - bit_shift);
                }
            }
            const word_t slice_mask = (w + 1 == info.word_count_ && info.tail_bits_ != 0) ? info.tail_mask_ : ~word_t{0};
            store_word(w, value, slice_mask, info);
        }
        return *this;
    }

    const size_t start_word = this->start_ / WORD_BITS;
    const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
    const size_t tail_bits = this->length_ % WORD_BITS;
    const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

    const size_t word_shift = shift_amount / WORD_BITS;
    const size_t bit_shift = shift_amount % WORD_BITS;

    for (size_t w = 0; w < word_count; ++w) {
        word_t value = 0;
        const size_t src = w + word_shift;
        if (src < word_count) {
            word_t src_word = (*const_cast<bitset*>(this->owner_)).wordData()[start_word + src];
            if (src + 1 == word_count && tail_bits != 0) {
                src_word &= tail_mask;
            }
            value = src_word >> bit_shift;
            if (bit_shift != 0 && src + 1 < word_count) {
                word_t high_word = (*const_cast<bitset*>(this->owner_)).wordData()[start_word + src + 1];
                if (src + 2 == word_count && tail_bits != 0) {
                    high_word &= tail_mask;
                }
                value |= high_word << (WORD_BITS - bit_shift);
            }
        }
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word + w] = value;
    }

    if (tail_bits != 0) {
        (*const_cast<bitset*>(this->owner_)).wordData()[start_word + word_count - 1] &= tail_mask;
    }
    return *this;
}

template<size_t N>
constexpr bool bitset<N>::is_fixed_size() noexcept {
    return IS_FIXED;
}

template<size_t N>
typename bitset<N>::slice_view bitset<N>::slice(size_t start, size_t length) {
    if (start > size() || length > size() - start) {
        throw std::out_of_range("bitset::slice: range out of bounds");
    }
    return slice_view(this, start, length);
}

template<size_t N>
typename bitset<N>::const_slice_view bitset<N>::slice(size_t start, size_t length) const {
    if (start > size() || length > size() - start) {
        throw std::out_of_range("bitset::slice: range out of bounds");
    }
    return const_slice_view(this, start, length);
}

// ===== Modifier Implementations =====

template<size_t N>
bitset<N>& bitset<N>::set() noexcept {
    for (size_t i = 0; i < wordCount(); ++i) {
        wordData()[i] = ~word_t{0};
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::set(size_t pos, bool val) {
    if (pos >= size()) {
        throw std::out_of_range("bitset::set: index out of range");
    }
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    if (val) {
        wordData()[word_idx] |= (word_t{1} << bit_idx);
    } else {
        wordData()[word_idx] &= ~(word_t{1} << bit_idx);
    }
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::reset() noexcept {
    for (size_t i = 0; i < wordCount(); ++i) {
        wordData()[i] = 0;
    }
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::reset(size_t pos) {
    return set(pos, false);
}

template<size_t N>
bitset<N>& bitset<N>::flip() noexcept {
    for (size_t i = 0; i < wordCount(); ++i) {
        wordData()[i] = ~wordData()[i];
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::flip(size_t pos) {
    if (pos >= size()) {
        throw std::out_of_range("bitset::flip: index out of range");
    }
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    wordData()[word_idx] ^= (word_t{1} << bit_idx);
    return *this;
}

// Dynamic-only modifiers
template<size_t N>
void bitset<N>::clear() noexcept requires(!IS_FIXED) {
    releaseHeap();
    size_ = 0;
    data_ = 0;
}

template<size_t N>
void bitset<N>::reserve(size_t bit_capacity) requires(!IS_FIXED) {
    (void)bit_capacity;
}

template<size_t N>
void bitset<N>::resize(size_t new_size, bool value) requires(!IS_FIXED) {
    const size_t old_size = size_;
    const size_t new_word_count = bitsToWords(new_size);
    
    resizeStorage(old_size, new_size, value ? ~word_t{0} : 0);
    size_ = new_size;
    
    // If growing with value=true, set bits in partially-filled last old word
    if (value && new_size > old_size && new_word_count > 0) {
        const size_t old_word_count = bitsToWords(old_size);
        if (old_word_count > 0 && old_size % WORD_BITS != 0) {
            // Set remaining bits in the last old word
            const size_t old_last_word_idx = old_word_count - 1;
            const size_t start_bit = old_size % WORD_BITS;
            const word_t mask = ~((word_t{1} << start_bit) - 1);
            wordData()[old_last_word_idx] |= mask;
        }
    }
    
    clearUnusedBits();
}

template<size_t N>
void bitset<N>::insert(size_t pos, bool value) requires(!IS_FIXED) {
    if (pos > size()) {
        throw std::out_of_range("bitset::insert: position out of range");
    }
    
    const size_t old_size = size();
    
    // Early exit if inserting at the end
    if (pos == old_size) {
        resize(old_size + 1, value);
        return;
    }
    
    // Resize to make room for one more bit
    resize(old_size + 1, false);

    // Shift all bits from [pos, old_size) one position to the right using
    // word-level operations and carry from lower words.
    const size_t pos_word = bitToWordIndex(pos);
    const size_t pos_bit = bitToBitIndex(pos);
    const size_t new_last_word = bitToWordIndex(old_size);

    word_t* data_ptr = wordData();
    for (size_t w = new_last_word; w > pos_word; --w) {
        const word_t upper = data_ptr[w];
        const word_t lower = data_ptr[w - 1];
        data_ptr[w] = (upper << 1) | (lower >> (WORD_BITS - 1));
    }

    if (pos_bit == 0) {
        data_ptr[pos_word] <<= 1;
    } else {
        const word_t lower_mask = (word_t{1} << pos_bit) - 1;
        const word_t lower_bits = data_ptr[pos_word] & lower_mask;
        const word_t upper_bits = data_ptr[pos_word] & ~lower_mask;
        data_ptr[pos_word] = lower_bits | (upper_bits << 1);
    }

    // Set the inserted bit at position pos.
    set(pos, value);
}

template<size_t N>
void bitset<N>::shrink_to_fit() requires(!IS_FIXED) {
    if (size_ <= WORD_BITS) {
        resizeStorage(size_, size_, 0);
        return;
    }
    const size_t word_count = bitsToWords(size_);
    if (word_count == 0) {
        releaseHeap();
        data_ = 0;
        return;
    }
    word_t* new_data = new word_t[word_count];
    std::memcpy(new_data, wordData(), word_count * sizeof(word_t));
    delete[] reinterpret_cast<word_t*>(data_);
    data_ = reinterpret_cast<std::uintptr_t>(new_data);
}

// ===== Operation Implementations =====

template<size_t N>
bool bitset<N>::all() const noexcept {
    const size_t wc = wordCount();
    if (wc == 0) return true;
    
    // Check full words - early exit on first zero bit found
    for (size_t i = 0; i < wc - 1; ++i) {
        if (~wordData()[i]) {  // Has zero bit
            return false;
        }
    }
    
    // Check last word with mask for unused bits
    const word_t mask = lastWordMask(size());
    return (wordData()[wc - 1] & mask) == mask;
}

template<size_t N>
bool bitset<N>::any() const noexcept {
    // Simple early exit - fastest for sparse bitsets
    for (size_t i = 0; i < wordCount(); ++i) {
        if (wordData()[i]) {
            return true;
        }
    }
    return false;
}

template<size_t N>
bool bitset<N>::all(const bitset& mask) const noexcept {
    // Mask is truncated to this bitset's size; extra mask bits are ignored.
    const size_t wc = wordCount();
    const size_t mwc = std::min(wc, mask.wordCount());
    const word_t last_mask = lastWordMask(size());

    for (size_t i = 0; i < mwc; ++i) {
        word_t mask_word = mask.wordData()[i];
        if (i + 1 == wc) {
            mask_word &= last_mask;
        }
        if ((wordData()[i] & mask_word) != mask_word) {
            return false;
        }
    }
    return true;
}

template<size_t N>
bool bitset<N>::any(const bitset& mask) const noexcept {
    // Mask is truncated to this bitset's size; extra mask bits are ignored.
    const size_t wc = wordCount();
    const size_t mwc = std::min(wc, mask.wordCount());
    const word_t last_mask = lastWordMask(size());

    for (size_t i = 0; i < mwc; ++i) {
        word_t mask_word = mask.wordData()[i];
        if (i + 1 == wc) {
            mask_word &= last_mask;
        }
        if (wordData()[i] & mask_word) {
            return true;
        }
    }
    return false;
}

template<size_t N>
size_t bitset<N>::count() const noexcept {
    size_t total = 0;
    for (size_t i = 0; i < wordCount(); ++i) {
        total += std::popcount(wordData()[i]);
    }
    return total;
}

template<size_t N>
bool bitset<N>::none() const noexcept {
    return !any();
}

// ===== Conversion Implementations =====

template<size_t N>
unsigned long bitset<N>::to_ulong() const {
    if (size() > 32) {
        for (size_t i = 32; i < size(); ++i) {
            if ((*this)[i]) {
                throw std::overflow_error("bitset::to_ulong: value too large");
            }
        }
    }
    
    unsigned long result = 0;
    const size_t words_to_copy = std::min(
        sizeof(unsigned long) / WORD_SIZE,
        wordCount()
    );
    
    for (size_t i = 0; i < words_to_copy; ++i) {
        result |= static_cast<unsigned long>(wordData()[i]) << (i * WORD_BITS);
    }
    
    return result;
}

template<size_t N>
unsigned long long bitset<N>::to_ullong() const {
    if (size() > 64) {
        for (size_t i = 64; i < size(); ++i) {
            if ((*this)[i]) {
                throw std::overflow_error("bitset::to_ullong: value too large");
            }
        }
    }
    
    unsigned long long result = 0;
    const size_t words_to_copy = std::min(
        sizeof(unsigned long long) / WORD_SIZE,
        wordCount()
    );
    
    for (size_t i = 0; i < words_to_copy; ++i) {
        result |= static_cast<unsigned long long>(wordData()[i]) << (i * WORD_BITS);
    }
    
    return result;
}

template<size_t N>
std::string bitset<N>::to_string(char zero, char one) const {
    std::string result;
    result.reserve(size());
    
    for (size_t i = size(); i > 0; --i) {
        result += (*this)[i - 1] ? one : zero;
    }
    
    return result;
}

template<size_t N>
template<size_t M>
bitset<M> bitset<N>::to_fixed() const requires(!IS_FIXED) {
    if (size() != M) {
        throw std::invalid_argument(
            "bitset::to_fixed: size mismatch (expected " + 
            std::to_string(M) + ", got " + std::to_string(size()) + ")"
        );
    }
    
    bitset<M> result;
    std::memcpy(result.data(), data(), byteCount());
    return result;
}

// ===== I/O Implementations =====

template<size_t N>
std::byte* bitset<N>::data() noexcept {
    return reinterpret_cast<std::byte*>(wordData());
}

template<size_t N>
const std::byte* bitset<N>::data() const noexcept {
    return reinterpret_cast<const std::byte*>(wordData());
}

template<size_t N>
void bitset<N>::readFrom(const void* src, size_t available) {
    if (available < byteCount()) {
        throw std::out_of_range("bitset::readFrom: insufficient data");
    }
    std::memcpy(data(), src, byteCount());
    clearUnusedBits();
}

template<size_t N>
void bitset<N>::writeTo(void* dst, size_t capacity) const {
    if (capacity < byteCount()) {
        throw std::out_of_range("bitset::writeTo: insufficient capacity");
    }
    std::memcpy(dst, data(), byteCount());
}

// ===== Bitwise Operator Implementations =====

template<size_t N>
bitset<N> bitset<N>::operator~() const noexcept {
    bitset result = *this;
    result.flip();
    return result;
}

template<size_t N>
bitset<N>& bitset<N>::operator&=(const bitset& other) noexcept {
    // Truncate to this bitset's size; do not resize for mismatched operands.
    const size_t other_wc = other.wordCount();
    const size_t wc = std::min(wordCount(), other_wc);
    for (size_t i = 0; i < wc; ++i) {
        wordData()[i] &= other.wordData()[i];
    }
    // Zero out remaining words (implicit zero-extension of the other operand).
    for (size_t i = wc; i < wordCount(); ++i) {
        wordData()[i] = 0;
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::operator|=(const bitset& other) noexcept {
    // Truncate to this bitset's size; do not resize for mismatched operands.
    const size_t other_wc = other.wordCount();
    const size_t wc = std::min(wordCount(), other_wc);
    for (size_t i = 0; i < wc; ++i) {
        wordData()[i] |= other.wordData()[i];
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::operator^=(const bitset& other) noexcept {
    // Truncate to this bitset's size; do not resize for mismatched operands.
    const size_t other_wc = other.wordCount();
    const size_t wc = std::min(wordCount(), other_wc);
    for (size_t i = 0; i < wc; ++i) {
        wordData()[i] ^= other.wordData()[i];
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
bitset<N> bitset<N>::operator<<(size_t shift_amount) const noexcept {
    // Early exits
    if (shift_amount == 0) {
        return *this;
    }
    
    if (shift_amount >= size()) {
        if constexpr (IS_FIXED) {
            return bitset();
        } else {
            return bitset(size());
        }
    }
    
    // Create zero-initialized result, then copy shifted data into it
    bitset result = [this]() {
        if constexpr (IS_FIXED) {
            return bitset();
        } else {
            return bitset(size());
        }
    }();
    
    const size_t word_shift = shift_amount / WORD_BITS;
    const size_t bit_shift = shift_amount % WORD_BITS;
    const size_t wc = wordCount();
    
    if (bit_shift == 0) {
        // Pure word shift - use memcpy for efficiency (non-overlapping)
        if (word_shift < wc) {
            std::memcpy(&result.wordData()[word_shift], 
                       &wordData()[0],
                       (wc - word_shift) * sizeof(word_t));
        }
    } else {
        // Mixed word + bit shift
        const size_t inv_shift = WORD_BITS - bit_shift;
        const size_t limit = wc - word_shift;
        
        // Process from low to high
        size_t i = 0;
        
        // First word: only left shift, no combine
        result.wordData()[word_shift] = wordData()[0] << bit_shift;
        
        // Main loop: combine two words
        for (i = 1; i < limit; ++i) {
            result.wordData()[i + word_shift] = (wordData()[i] << bit_shift) |
                                                 (wordData()[i - 1] >> inv_shift);
        }
    }
    
    result.clearUnusedBits();
    return result;
}

template<size_t N>
bitset<N> bitset<N>::operator>>(size_t shift_amount) const noexcept {
    // Early exits
    if (shift_amount >= size()) {
        if constexpr (IS_FIXED) {
            return bitset();
        } else {
            return bitset(size());
        }
    }
    
    if (shift_amount == 0) {
        return *this;
    }
    
    // Create result and copy data
    bitset result = *this;
    
    const size_t word_shift = shift_amount / WORD_BITS;
    const size_t bit_shift = shift_amount % WORD_BITS;
    const size_t wc = wordCount();
    
    if (bit_shift == 0) {
        // Pure word shift - use memmove for efficiency
        if (word_shift < wc) {
            std::memmove(&result.wordData()[0],
                        &result.wordData()[word_shift],
                        (wc - word_shift) * sizeof(word_t));
        }
        // Zero out upper words
        std::memset(&result.wordData()[wc - word_shift], 0, word_shift * sizeof(word_t));
    } else {
        // Mixed word + bit shift
        const size_t inv_shift = WORD_BITS - bit_shift;
        const size_t limit = wc - word_shift;
        
        // Process from low to high, avoiding branch in inner loop
        size_t i = 0;
        
        // Main loop: can always combine two words
        for (; i + 1 < limit; ++i) {
            result.wordData()[i] = (wordData()[i + word_shift] >> bit_shift) |
                                    (wordData()[i + word_shift + 1] << inv_shift);
        }
        
        // Last word: only shift, no combine
        if (i < limit) {
            result.wordData()[i] = wordData()[i + word_shift] >> bit_shift;
        }
        
        // Zero out upper words
        for (size_t j = limit; j < wc; ++j) {
            result.wordData()[j] = 0;
        }
    }
    
    return result;
}

template<size_t N>
bitset<N>& bitset<N>::operator<<=(size_t shift_amount) noexcept {
    *this = *this << shift_amount;
    return *this;
}

template<size_t N>
bitset<N>& bitset<N>::operator>>=(size_t shift_amount) noexcept {
    *this = *this >> shift_amount;
    return *this;
}

// ===== Non-member Operator Implementations =====

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
    
    const size_t wc = lhs.wordCount();
    for (size_t i = 0; i < wc; ++i) {
        if (lhs.wordData()[i] != rhs.wordData()[i]) return false;
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

// ===== Hash Support =====

namespace std {
    template<size_t N>
    struct hash<bcsv::bitset<N>> {
        size_t operator()(const bcsv::bitset<N>& bs) const noexcept {
            // FNV-1a hash algorithm
            constexpr size_t fnv_prime = sizeof(size_t) == 8 ? 1099511628211ULL : 16777619UL;
            constexpr size_t fnv_offset = sizeof(size_t) == 8 ? 14695981039346656037ULL : 2166136261UL;
            
            size_t result = fnv_offset;
            
            // Hash the bit count for dynamic bitsets
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
} // namespace std
