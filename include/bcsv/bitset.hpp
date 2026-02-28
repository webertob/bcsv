/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
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
constexpr size_t Bitset<N>::wordCount() const noexcept {
    if constexpr (IS_FIXED) {
        return WORD_COUNT_FIXED;
    }
    return bitsToWords(size());
}

template<size_t N>
constexpr size_t Bitset<N>::byteCount() const noexcept {
    return bitsToBytes(size());
}

template<size_t N>
constexpr bool Bitset<N>::usesInline() const noexcept {
    if constexpr (IS_FIXED) {
        return false;
    } else {
        return wordCount() <= 1;
    }
}

template<size_t N>
constexpr typename Bitset<N>::word_t* Bitset<N>::wordData() noexcept {
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
constexpr const typename Bitset<N>::word_t* Bitset<N>::wordData() const noexcept {
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
void Bitset<N>::releaseHeap() noexcept {
    if constexpr (!IS_FIXED) {
        if (!usesInline() && data_ != 0) {
            delete[] reinterpret_cast<word_t*>(data_);
            data_ = 0;
        }
    }
}

template<size_t N>
void Bitset<N>::resizeStorage(size_t old_size, size_t new_size, word_t value) {
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
constexpr void Bitset<N>::clearUnusedBits() noexcept {
    if (wordCount() == 0) return;
    
    const size_t bits_in_last = size() % WORD_BITS;
    if (bits_in_last == 0) return;
    
    const word_t mask = lastWordMask(size());
    wordData()[wordCount() - 1] &= mask;
}

template<size_t N>
constexpr void Bitset<N>::setFromValue(unsigned long long val) noexcept {
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
void Bitset<N>::setFromString(
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
            throw std::invalid_argument("Bitset: invalid character in string");
        }
    }
}

// ===== Reference Proxy Implementation =====

template<size_t N>
Bitset<N>::Reference::Reference(word_t* ptr, size_t bit_idx) 
    : word_ptr_(ptr), bit_index_(bit_idx) {}

template<size_t N>
typename Bitset<N>::Reference& Bitset<N>::Reference::operator=(bool value) {
    if (value) {
        *word_ptr_ |= (word_t{1} << bit_index_);
    } else {
        *word_ptr_ &= ~(word_t{1} << bit_index_);
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::Reference& Bitset<N>::Reference::operator=(const Reference& other) {
    return *this = bool(other);
}

template<size_t N>
Bitset<N>::Reference::operator bool() const {
    return (*word_ptr_ & (word_t{1} << bit_index_)) != 0;
}

template<size_t N>
bool Bitset<N>::Reference::operator~() const {
    return !bool(*this);
}

template<size_t N>
typename Bitset<N>::Reference& Bitset<N>::Reference::flip() {
    *word_ptr_ ^= (word_t{1} << bit_index_);
    return *this;
}

template<size_t N>
typename Bitset<N>::Reference& Bitset<N>::Reference::operator|=(bool value) {
    if (value) {
        *word_ptr_ |= (word_t{1} << bit_index_);
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::Reference& Bitset<N>::Reference::operator&=(bool value) {
    if (!value) {
        *word_ptr_ &= ~(word_t{1} << bit_index_);
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::Reference& Bitset<N>::Reference::operator^=(bool value) {
    if (value) {
        *word_ptr_ ^= (word_t{1} << bit_index_);
    }
    return *this;
}

// ===== Constructor Implementations =====

// Fixed-size constructors
template<size_t N>
constexpr Bitset<N>::Bitset() noexcept requires(IS_FIXED) 
    : data_{} {}

template<size_t N>
constexpr Bitset<N>::Bitset(unsigned long long val) noexcept requires(IS_FIXED)
    : data_{} { 
    setFromValue(val); 
}

template<size_t N>
template<class CharT, class Traits, class Allocator>
Bitset<N>::Bitset(
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
Bitset<N>::Bitset(size_t num_bits) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{
    resizeStorage(0, size_, 0);
}

template<size_t N>
Bitset<N>::Bitset(size_t num_bits, unsigned long long val) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{ 
    resizeStorage(0, size_, 0);
    setFromValue(val); 
}

template<size_t N>
Bitset<N>::Bitset(size_t num_bits, bool value) requires(!IS_FIXED)
    : size_(num_bits)
    , data_(0)
{
    resizeStorage(0, size_, value ? ~word_t{0} : 0);
    if (value) clearUnusedBits();
}

template<size_t N>
template<class CharT, class Traits, class Allocator>
Bitset<N>::Bitset(
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
Bitset<N>::Bitset(const Bitset<M>& other) requires(!IS_FIXED && M != DYNAMIC_EXTENT)
    : size_(M)
    , data_(0)
{
    resizeStorage(0, size_, 0);
    std::memcpy(data(), other.data(), other.sizeBytes());
}

// ===== Element Access Implementations =====

template<size_t N>
Bitset<N>::Bitset(const Bitset& other) {
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
Bitset<N>::Bitset(Bitset&& other) noexcept {
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
Bitset<N>& Bitset<N>::operator=(const Bitset& other) {
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
Bitset<N>& Bitset<N>::operator=(Bitset&& other) noexcept {
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
Bitset<N>::~Bitset() {
    if constexpr (!IS_FIXED) {
        releaseHeap();
    }
}

template<size_t N>
constexpr bool Bitset<N>::operator[](size_t pos) const {
    // No bounds checking for performance (like std::vector::operator[])
    // Use test() for checked access
    assert(pos < size() && "Bitset::operator[]: index out of range");
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    return (wordData()[word_idx] & (word_t{1} << bit_idx)) != 0;
}

template<size_t N>
typename Bitset<N>::Reference Bitset<N>::operator[](size_t pos) {
    // No bounds checking for performance (like std::vector::operator[])
    // Use test() for checked access  
    assert(pos < size() && "Bitset::operator[]: index out of range");
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    return Reference(&wordData()[word_idx], bit_idx);
}

template<size_t N>
constexpr bool Bitset<N>::test(size_t pos) const {
    if (pos >= size()) {
        throw std::out_of_range("Bitset::test: index out of range");
    }
    return (*this)[pos];
}

// ===== Capacity Implementations =====

template<size_t N>
constexpr size_t Bitset<N>::size() const noexcept {
    if constexpr (IS_FIXED) {
        return N;
    } else {
        return size_;
    }
}

template<size_t N>
constexpr size_t Bitset<N>::sizeBytes() const noexcept {
    return byteCount();
}

template<size_t N>
constexpr bool Bitset<N>::empty() const noexcept {
    return size() == 0;
}

template<size_t N>
constexpr size_t Bitset<N>::capacity() const noexcept {
    return wordCount() * WORD_BITS;
}

// ===== Slice View Implementations =====

template<size_t N>
Bitset<N>::ConstSliceView::ConstSliceView(
    const Bitset* owner,
    size_t start,
    size_t length)
    : owner_(owner), start_(start), length_(length) {}

template<size_t N>
size_t Bitset<N>::ConstSliceView::size() const noexcept {
    return length_;
}

template<size_t N>
bool Bitset<N>::ConstSliceView::empty() const noexcept {
    return length_ == 0;
}

template<size_t N>
typename Bitset<N>::word_t
Bitset<N>::ConstSliceView::loadWord(size_t index) const noexcept {
    const size_t start_word = start_ / WORD_BITS;
    const size_t start_bit = start_ % WORD_BITS;
    const size_t word_count = (length_ + WORD_BITS - 1) / WORD_BITS;
    const size_t tail_bits = length_ % WORD_BITS;
    const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

    const size_t base = start_word + index;
    word_t low = owner_->wordData()[base] >> start_bit;
    word_t high = 0;
    if (start_bit != 0 && base + 1 < owner_->wordCount()) {
        high = owner_->wordData()[base + 1] << (WORD_BITS - start_bit);
    }
    word_t value = low | high;
    if (index + 1 == word_count && tail_bits != 0) {
        value &= tail_mask;
    }
    return value;
}

template<size_t N>
bool Bitset<N>::ConstSliceView::operator[](size_t pos) const {
    assert(pos < length_ && "Bitset::SliceView::operator[]: index out of range");
    return (*owner_)[start_ + pos];
}

template<size_t N>
bool Bitset<N>::ConstSliceView::test(size_t pos) const {
    if (pos >= length_) {
        throw std::out_of_range("Bitset::SliceView::test: index out of range");
    }
    return (*owner_)[start_ + pos];
}

template<size_t N>
bool Bitset<N>::ConstSliceView::all() const noexcept {
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
bool Bitset<N>::ConstSliceView::any() const noexcept {
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
bool Bitset<N>::ConstSliceView::none() const noexcept {
    return !any();
}

template<size_t N>
size_t Bitset<N>::ConstSliceView::count() const noexcept {
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
bool Bitset<N>::ConstSliceView::all(const Bitset& mask) const noexcept {
    const size_t limit = std::min(length_, mask.size());
    for (size_t i = 0; i < limit; ++i) {
        if (mask.test(i) && !(*owner_)[start_ + i]) {
            return false;
        }
    }
    return true;
}

template<size_t N>
bool Bitset<N>::ConstSliceView::any(const Bitset& mask) const noexcept {
    const size_t limit = std::min(length_, mask.size());
    for (size_t i = 0; i < limit; ++i) {
        if (mask.test(i) && (*owner_)[start_ + i]) {
            return true;
        }
    }
    return false;
}

template<size_t N>
Bitset<N> Bitset<N>::ConstSliceView::operator<<(size_t shift_amount) const noexcept {
    Bitset result = *owner_;
    auto view = result.slice(start_, length_);
    view <<= shift_amount;
    return result;
}

template<size_t N>
Bitset<N> Bitset<N>::ConstSliceView::operator>>(size_t shift_amount) const noexcept {
    Bitset result = *owner_;
    auto view = result.slice(start_, length_);
    view >>= shift_amount;
    return result;
}

template<size_t N>
Bitset<> Bitset<N>::ConstSliceView::toBitset() const {
    Bitset<> result(length_);
    if (length_ == 0) {
        return result;
    }

    const size_t word_count = (length_ + WORD_BITS - 1) / WORD_BITS;
    for (size_t i = 0; i < word_count; ++i) {
        result.wordData()[i] = loadWord(i);
    }
    result.clearUnusedBits();
    return result;
}

template<size_t N>
Bitset<> Bitset<N>::ConstSliceView::shiftedLeft(size_t shift_amount) const {
    Bitset<> result = toBitset();
    result <<= shift_amount;
    return result;
}

template<size_t N>
Bitset<> Bitset<N>::ConstSliceView::shiftedRight(size_t shift_amount) const {
    Bitset<> result = toBitset();
    result >>= shift_amount;
    return result;
}

template<size_t N>
Bitset<N>::SliceView::SliceView(Bitset* owner, size_t start, size_t length)
    : ConstSliceView(owner, start, length) {}

template<size_t N>
void Bitset<N>::SliceView::storeWord(
    size_t index,
    word_t value,
    word_t slice_mask) noexcept
{
    const size_t start_word = this->start_ / WORD_BITS;
    const size_t start_bit = this->start_ % WORD_BITS;
    const size_t base = start_word + index;
    word_t masked_value = value & slice_mask;
    word_t low_mask = slice_mask << start_bit;
    word_t low_bits = masked_value << start_bit;
    word_t* owner_data = (*const_cast<Bitset*>(this->owner_)).wordData();
    owner_data[base] = (owner_data[base] & ~low_mask) | low_bits;

    if (start_bit != 0 && base + 1 < this->owner_->wordCount()) {
        word_t high_mask = slice_mask >> (WORD_BITS - start_bit);
        if (high_mask != 0) {
            word_t high_bits = masked_value >> (WORD_BITS - start_bit);
            owner_data[base + 1] = (owner_data[base + 1] & ~high_mask) | high_bits;
        }
    }
}

template<size_t N>
typename Bitset<N>::Reference Bitset<N>::SliceView::operator[](size_t pos) {
    assert(pos < this->length_ && "Bitset::SliceView::operator[]: index out of range");
    return (*const_cast<Bitset*>(this->owner_))[this->start_ + pos];
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::set() noexcept {
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
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word] |= mask;
        return *this;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    (*const_cast<Bitset*>(this->owner_)).wordData()[start_word] |= first_mask;

    for (size_t w = start_word + 1; w < end_word; ++w) {
        (*const_cast<Bitset*>(this->owner_)).wordData()[w] = ~word_t{0};
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    (*const_cast<Bitset*>(this->owner_)).wordData()[end_word] |= last_mask;
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::set(size_t pos, bool val) {
    if (pos >= this->length_) {
        throw std::out_of_range("Bitset::SliceView::set: index out of range");
    }
    (*const_cast<Bitset*>(this->owner_)).set(this->start_ + pos, val);
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::reset() noexcept {
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
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word] &= ~mask;
        return *this;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    (*const_cast<Bitset*>(this->owner_)).wordData()[start_word] &= ~first_mask;

    for (size_t w = start_word + 1; w < end_word; ++w) {
        (*const_cast<Bitset*>(this->owner_)).wordData()[w] = 0;
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    (*const_cast<Bitset*>(this->owner_)).wordData()[end_word] &= ~last_mask;
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::reset(size_t pos) {
    return set(pos, false);
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::flip() noexcept {
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
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word] ^= mask;
        return *this;
    }

    word_t first_mask = ~word_t{0} << start_bit;
    (*const_cast<Bitset*>(this->owner_)).wordData()[start_word] ^= first_mask;

    for (size_t w = start_word + 1; w < end_word; ++w) {
        (*const_cast<Bitset*>(this->owner_)).wordData()[w] = ~(*const_cast<Bitset*>(this->owner_)).wordData()[w];
    }

    word_t last_mask = end_bit == 0 ? ~word_t{0} : mask_from(end_bit);
    (*const_cast<Bitset*>(this->owner_)).wordData()[end_word] ^= last_mask;
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::flip(size_t pos) {
    if (pos >= this->length_) {
        throw std::out_of_range("Bitset::SliceView::flip: index out of range");
    }
    (*const_cast<Bitset*>(this->owner_)).flip(this->start_ + pos);
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator&=(const Bitset& other) noexcept {
    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
        const size_t tail_bits = this->length_ % WORD_BITS;
        const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

        for (size_t i = 0; i < word_count; ++i) {
            const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};
            const word_t slice_mask = (i + 1 == word_count && tail_bits != 0) ? tail_mask : ~word_t{0};
            const word_t updated = this->loadWord(i) & other_word;
            storeWord(i, updated, slice_mask);
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
            word_t* owner_data = (*const_cast<Bitset*>(this->owner_)).wordData();
            word_t current = owner_data[owner_word];
            word_t updated = (current & other_word) & tail_mask;
            owner_data[owner_word] = (current & ~tail_mask) | updated;
        } else {
            (*const_cast<Bitset*>(this->owner_)).wordData()[owner_word] &= other_word;
        }
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator|=(const Bitset& other) noexcept {
    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
        const size_t tail_bits = this->length_ % WORD_BITS;
        const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

        for (size_t i = 0; i < word_count; ++i) {
            const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};
            const word_t slice_mask = (i + 1 == word_count && tail_bits != 0) ? tail_mask : ~word_t{0};
            const word_t updated = this->loadWord(i) | other_word;
            storeWord(i, updated, slice_mask);
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
            word_t* owner_data = (*const_cast<Bitset*>(this->owner_)).wordData();
            word_t current = owner_data[owner_word];
            word_t updated = (current | other_word) & tail_mask;
            owner_data[owner_word] = (current & ~tail_mask) | updated;
        } else {
            (*const_cast<Bitset*>(this->owner_)).wordData()[owner_word] |= other_word;
        }
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator^=(const Bitset& other) noexcept {
    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
        const size_t tail_bits = this->length_ % WORD_BITS;
        const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;

        for (size_t i = 0; i < word_count; ++i) {
            const word_t other_word = (i < other.wordCount()) ? other.wordData()[i] : word_t{0};
            const word_t slice_mask = (i + 1 == word_count && tail_bits != 0) ? tail_mask : ~word_t{0};
            const word_t updated = this->loadWord(i) ^ other_word;
            storeWord(i, updated, slice_mask);
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
            word_t* owner_data = (*const_cast<Bitset*>(this->owner_)).wordData();
            word_t current = owner_data[owner_word];
            word_t updated = (current ^ other_word) & tail_mask;
            owner_data[owner_word] = (current & ~tail_mask) | updated;
        } else {
            (*const_cast<Bitset*>(this->owner_)).wordData()[owner_word] ^= other_word;
        }
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator&=(const ConstSliceView& other) noexcept {
    const size_t limit = std::min(this->length_, other.size());
    for (size_t i = 0; i < limit; ++i) {
        if (!other.test(i)) {
            (*const_cast<Bitset*>(this->owner_)).set(this->start_ + i, false);
        }
    }
    for (size_t i = limit; i < this->length_; ++i) {
        (*const_cast<Bitset*>(this->owner_)).set(this->start_ + i, false);
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator|=(const ConstSliceView& other) noexcept {
    const size_t limit = std::min(this->length_, other.size());
    for (size_t i = 0; i < limit; ++i) {
        if (other.test(i)) {
            (*const_cast<Bitset*>(this->owner_)).set(this->start_ + i, true);
        }
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator^=(const ConstSliceView& other) noexcept {
    const size_t limit = std::min(this->length_, other.size());
    for (size_t i = 0; i < limit; ++i) {
        if (other.test(i)) {
            (*const_cast<Bitset*>(this->owner_)).flip(this->start_ + i);
        }
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator<<=(size_t shift_amount) noexcept {
    if (shift_amount == 0 || this->length_ == 0) {
        return *this;
    }
    if (shift_amount >= this->length_) {
        reset();
        return *this;
    }

    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
        const size_t tail_bits = this->length_ % WORD_BITS;
        const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;
        const size_t word_shift = shift_amount / WORD_BITS;
        const size_t bit_shift = shift_amount % WORD_BITS;

        for (size_t w = word_count; w-- > 0;) {
            word_t value = 0;
            if (w >= word_shift) {
                const size_t src = w - word_shift;
                word_t src_word = this->loadWord(src);
                value = src_word << bit_shift;
                if (bit_shift != 0 && src > 0) {
                    value |= this->loadWord(src - 1) >> (WORD_BITS - bit_shift);
                }
            }
            const word_t slice_mask = (w + 1 == word_count && tail_bits != 0) ? tail_mask : ~word_t{0};
            storeWord(w, value, slice_mask);
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
            word_t src_word = (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + src];
            if (src + 1 == word_count && tail_bits != 0) {
                src_word &= tail_mask;
            }
            value = src_word << bit_shift;
            if (bit_shift != 0 && src > 0) {
                word_t low_word = (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + src - 1];
                value |= low_word >> (WORD_BITS - bit_shift);
            }
        }
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + w] = value;
    }

    if (tail_bits != 0) {
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + word_count - 1] &= tail_mask;
    }
    return *this;
}

template<size_t N>
typename Bitset<N>::SliceView& Bitset<N>::SliceView::operator>>=(size_t shift_amount) noexcept {
    if (shift_amount == 0 || this->length_ == 0) {
        return *this;
    }
    if (shift_amount >= this->length_) {
        reset();
        return *this;
    }

    const size_t start_bit = this->start_ % WORD_BITS;
    if (start_bit != 0) {
        const size_t word_count = (this->length_ + WORD_BITS - 1) / WORD_BITS;
        const size_t tail_bits = this->length_ % WORD_BITS;
        const word_t tail_mask = tail_bits == 0 ? ~word_t{0} : (word_t{1} << tail_bits) - 1;
        const size_t word_shift = shift_amount / WORD_BITS;
        const size_t bit_shift = shift_amount % WORD_BITS;

        for (size_t w = 0; w < word_count; ++w) {
            word_t value = 0;
            const size_t src = w + word_shift;
            if (src < word_count) {
                word_t src_word = this->loadWord(src);
                value = src_word >> bit_shift;
                if (bit_shift != 0 && src + 1 < word_count) {
                    value |= this->loadWord(src + 1) << (WORD_BITS - bit_shift);
                }
            }
            const word_t slice_mask = (w + 1 == word_count && tail_bits != 0) ? tail_mask : ~word_t{0};
            storeWord(w, value, slice_mask);
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
            word_t src_word = (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + src];
            if (src + 1 == word_count && tail_bits != 0) {
                src_word &= tail_mask;
            }
            value = src_word >> bit_shift;
            if (bit_shift != 0 && src + 1 < word_count) {
                word_t high_word = (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + src + 1];
                if (src + 2 == word_count && tail_bits != 0) {
                    high_word &= tail_mask;
                }
                value |= high_word << (WORD_BITS - bit_shift);
            }
        }
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + w] = value;
    }

    if (tail_bits != 0) {
        (*const_cast<Bitset*>(this->owner_)).wordData()[start_word + word_count - 1] &= tail_mask;
    }
    return *this;
}

template<size_t N>
constexpr bool Bitset<N>::isFixedSize() noexcept {
    return IS_FIXED;
}

template<size_t N>
typename Bitset<N>::SliceView Bitset<N>::slice(size_t start, size_t length) {
    if (start > size() || length > size() - start) {
        throw std::out_of_range("Bitset::slice: range out of bounds");
    }
    return SliceView(this, start, length);
}

template<size_t N>
typename Bitset<N>::ConstSliceView Bitset<N>::slice(size_t start, size_t length) const {
    if (start > size() || length > size() - start) {
        throw std::out_of_range("Bitset::slice: range out of bounds");
    }
    return ConstSliceView(this, start, length);
}

// ===== Modifier Implementations =====

template<size_t N>
Bitset<N>& Bitset<N>::set() noexcept {
    for (size_t i = 0; i < wordCount(); ++i) {
        wordData()[i] = ~word_t{0};
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
Bitset<N>& Bitset<N>::set(size_t pos, bool val) {
    if (pos >= size()) {
        throw std::out_of_range("Bitset::set: index out of range");
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
Bitset<N>& Bitset<N>::reset() noexcept {
    for (size_t i = 0; i < wordCount(); ++i) {
        wordData()[i] = 0;
    }
    return *this;
}

template<size_t N>
Bitset<N>& Bitset<N>::reset(size_t pos) {
    return set(pos, false);
}

template<size_t N>
Bitset<N>& Bitset<N>::flip() noexcept {
    for (size_t i = 0; i < wordCount(); ++i) {
        wordData()[i] = ~wordData()[i];
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
Bitset<N>& Bitset<N>::flip(size_t pos) {
    if (pos >= size()) {
        throw std::out_of_range("Bitset::flip: index out of range");
    }
    
    const size_t word_idx = bitToWordIndex(pos);
    const size_t bit_idx = bitToBitIndex(pos);
    
    wordData()[word_idx] ^= (word_t{1} << bit_idx);
    return *this;
}

// Dynamic-only modifiers
template<size_t N>
void Bitset<N>::clear() noexcept requires(!IS_FIXED) {
    releaseHeap();
    size_ = 0;
    data_ = 0;
}

template<size_t N>
void Bitset<N>::reserve(size_t bit_capacity) requires(!IS_FIXED) {
    (void)bit_capacity;
}

template<size_t N>
void Bitset<N>::resize(size_t new_size, bool value) requires(!IS_FIXED) {
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
void Bitset<N>::insert(size_t pos, bool value) requires(!IS_FIXED) {
    if (pos > size()) {
        throw std::out_of_range("Bitset::insert: position out of range");
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
void Bitset<N>::erase(size_t pos) requires(!IS_FIXED) {
    if (pos >= size()) {
        throw std::out_of_range("Bitset::erase: position out of range");
    }

    const size_t old_size = size();

    // Trivial: removing the last (only remaining) bit
    if (old_size == 1) {
        resize(0);
        return;
    }

    // Shift all bits from (pos, old_size) one position to the left.
    const size_t pos_word = bitToWordIndex(pos);
    const size_t pos_bit  = bitToBitIndex(pos);
    const size_t last_word = bitToWordIndex(old_size - 1);

    word_t* data_ptr = wordData();

    // Handle the word containing pos: keep lower bits, shift upper bits down
    if (pos_bit == WORD_BITS - 1) {
        // Erasing the MSB of this word — just clear it, carry from next word below
        data_ptr[pos_word] &= (word_t{1} << pos_bit) - 1;
    } else {
        const word_t lower_mask = (word_t{1} << pos_bit) - 1;
        const word_t lower_bits = data_ptr[pos_word] & lower_mask;
        const word_t upper_bits = data_ptr[pos_word] >> (pos_bit + 1);
        data_ptr[pos_word] = lower_bits | (upper_bits << pos_bit);
    }

    // Carry LSB from each subsequent word into the MSB of the previous word
    for (size_t w = pos_word; w < last_word; ++w) {
        const word_t next = data_ptr[w + 1];
        // Set MSB of current word to LSB of next word
        if (next & 1) {
            data_ptr[w] |= word_t{1} << (WORD_BITS - 1);
        } else {
            data_ptr[w] &= ~(word_t{1} << (WORD_BITS - 1));
        }
        data_ptr[w + 1] = next >> 1;
    }

    resize(old_size - 1);
}

template<size_t N>
void Bitset<N>::pushBack(bool value) requires(!IS_FIXED) {
    const size_t old_size = size();
    resize(old_size + 1);
    // operator[] is unchecked — safe here since we just resized
    (*this)[old_size] = value;
}

template<size_t N>
void Bitset<N>::shrinkToFit() requires(!IS_FIXED) {
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

// ===== Multi-bit Field Packing =====

template<size_t N>
void Bitset<N>::encode(size_t pos, size_t bitCount, uint8_t value) {
    assert(bitCount >= 1 && bitCount <= 8);
    assert(pos + bitCount <= size());
    // Write bits LSB-first
    for (size_t i = 0; i < bitCount; ++i) {
        (*this)[pos + i] = (value >> i) & 1;
    }
}

template<size_t N>
uint8_t Bitset<N>::decode(size_t pos, size_t bitCount) const {
    assert(bitCount >= 1 && bitCount <= 8);
    assert(pos + bitCount <= size());
    uint8_t result = 0;
    for (size_t i = 0; i < bitCount; ++i) {
        if ((*this)[pos + i]) {
            result |= (1u << i);
        }
    }
    return result;
}

// ===== Operation Implementations =====

template<size_t N>
bool Bitset<N>::all() const noexcept {
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
bool Bitset<N>::any() const noexcept {
    // Simple early exit - fastest for sparse bitsets
    for (size_t i = 0; i < wordCount(); ++i) {
        if (wordData()[i]) {
            return true;
        }
    }
    return false;
}

template<size_t N>
bool Bitset<N>::all(const Bitset& mask) const noexcept {
    // Mask is truncated to this Bitset's size; extra mask bits are ignored.
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
bool Bitset<N>::any(const Bitset& mask) const noexcept {
    // Mask is truncated to this Bitset's size; extra mask bits are ignored.
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
size_t Bitset<N>::count() const noexcept {
    size_t total = 0;
    for (size_t i = 0; i < wordCount(); ++i) {
        total += std::popcount(wordData()[i]);
    }
    return total;
}

template<size_t N>
bool Bitset<N>::none() const noexcept {
    return !any();
}

// ===== Conversion Implementations =====

template<size_t N>
unsigned long Bitset<N>::toUlong() const {
    if (size() > 32) {
        for (size_t i = 32; i < size(); ++i) {
            if ((*this)[i]) {
                throw std::overflow_error("Bitset::toUlong: value too large");
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
unsigned long long Bitset<N>::toUllong() const {
    if (size() > 64) {
        for (size_t i = 64; i < size(); ++i) {
            if ((*this)[i]) {
                throw std::overflow_error("Bitset::toUllong: value too large");
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
std::string Bitset<N>::toString(char zero, char one) const {
    std::string result;
    result.reserve(size());
    
    for (size_t i = size(); i > 0; --i) {
        result += (*this)[i - 1] ? one : zero;
    }
    
    return result;
}

template<size_t N>
template<size_t M>
Bitset<M> Bitset<N>::toFixed() const requires(!IS_FIXED) {
    if (size() != M) {
        throw std::invalid_argument(
            "Bitset::toFixed: size mismatch (expected " + 
            std::to_string(M) + ", got " + std::to_string(size()) + ")"
        );
    }
    
    Bitset<M> result;
    std::memcpy(result.data(), data(), byteCount());
    return result;
}

// ===== I/O Implementations =====

template<size_t N>
std::byte* Bitset<N>::data() noexcept {
    return reinterpret_cast<std::byte*>(wordData());
}

template<size_t N>
const std::byte* Bitset<N>::data() const noexcept {
    return reinterpret_cast<const std::byte*>(wordData());
}

template<size_t N>
void Bitset<N>::readFrom(const void* src, size_t available) {
    if (available < byteCount()) {
        throw std::out_of_range("Bitset::readFrom: insufficient data");
    }
    std::memcpy(data(), src, byteCount());
    clearUnusedBits();
}

template<size_t N>
void Bitset<N>::writeTo(void* dst, size_t capacity) const {
    if (capacity < byteCount()) {
        throw std::out_of_range("Bitset::writeTo: insufficient capacity");
    }
    // Ensure padding bits in the last word are zero before export
    const_cast<Bitset*>(this)->clearUnusedBits();
    std::memcpy(dst, data(), byteCount());
}

// ===== Bitwise Operator Implementations =====

template<size_t N>
Bitset<N> Bitset<N>::operator~() const noexcept {
    Bitset result = *this;
    result.flip();
    return result;
}

template<size_t N>
Bitset<N>& Bitset<N>::operator&=(const Bitset& other) noexcept {
    // Truncate to this Bitset's size; do not resize for mismatched operands.
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
Bitset<N>& Bitset<N>::operator|=(const Bitset& other) noexcept {
    // Truncate to this Bitset's size; do not resize for mismatched operands.
    const size_t other_wc = other.wordCount();
    const size_t wc = std::min(wordCount(), other_wc);
    for (size_t i = 0; i < wc; ++i) {
        wordData()[i] |= other.wordData()[i];
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
Bitset<N>& Bitset<N>::operator^=(const Bitset& other) noexcept {
    // Truncate to this Bitset's size; do not resize for mismatched operands.
    const size_t other_wc = other.wordCount();
    const size_t wc = std::min(wordCount(), other_wc);
    for (size_t i = 0; i < wc; ++i) {
        wordData()[i] ^= other.wordData()[i];
    }
    clearUnusedBits();
    return *this;
}

template<size_t N>
Bitset<N> Bitset<N>::operator<<(size_t shift_amount) const noexcept {
    // Early exits
    if (shift_amount == 0) {
        return *this;
    }
    
    if (shift_amount >= size()) {
        if constexpr (IS_FIXED) {
            return Bitset();
        } else {
            return Bitset(size());
        }
    }
    
    // Create zero-initialized result, then copy shifted data into it
    Bitset result = [this]() {
        if constexpr (IS_FIXED) {
            return Bitset();
        } else {
            return Bitset(size());
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
Bitset<N> Bitset<N>::operator>>(size_t shift_amount) const noexcept {
    // Early exits
    if (shift_amount >= size()) {
        if constexpr (IS_FIXED) {
            return Bitset();
        } else {
            return Bitset(size());
        }
    }
    
    if (shift_amount == 0) {
        return *this;
    }
    
    // Create result and copy data
    Bitset result = *this;
    
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
Bitset<N>& Bitset<N>::operator<<=(size_t shift_amount) noexcept {
    *this = *this << shift_amount;
    return *this;
}

template<size_t N>
Bitset<N>& Bitset<N>::operator>>=(size_t shift_amount) noexcept {
    *this = *this >> shift_amount;
    return *this;
}

// ===== Block Operation Implementations =====

// ---------------------------------------------------------------------------
// Helper: extract one logical word from a raw word_t array at an arbitrary
// bit offset.  Handles cross-word-boundary extraction via shift-and-combine.
// This is a static free helper — no indirection, fully constexpr.
// ---------------------------------------------------------------------------
namespace detail {

template<typename WordT>
constexpr WordT extractWord(const WordT* data, size_t word_count,
                            size_t bit_offset, size_t word_index) noexcept {
    constexpr size_t WBITS = sizeof(WordT) * 8;
    const size_t base_word = bit_offset / WBITS + word_index;
    const size_t shift = bit_offset % WBITS;

    if (shift == 0) {
        return (base_word < word_count) ? data[base_word] : WordT{0};
    }

    WordT lo = (base_word < word_count)     ? data[base_word]     : WordT{0};
    WordT hi = (base_word + 1 < word_count) ? data[base_word + 1] : WordT{0};
    return (lo >> shift) | (hi << (WBITS - shift));
}

} // namespace detail

template<size_t N>
template<size_t M>
constexpr bool Bitset<N>::equalRange(const Bitset<M>& other,
                                     size_t offset,
                                     size_t len) const noexcept {
    // Resolve default length
    if (len == npos) {
        len = other.size();
    }
    if (len == 0) {
        return true;
    }

    // Precondition assertions (UB otherwise, but no crash in release)
    // Use overflow-safe form: len <= size() - offset
    assert(offset <= size() && "equalRange: offset exceeds this->size()");
    assert(len <= size() - offset && "equalRange: offset+len exceeds this->size()");
    assert(len <= other.size() && "equalRange: len exceeds other.size()");

    const word_t* this_data  = wordData();
    const size_t  this_wc    = wordCount();
    const auto*   other_data = other.wordData();
    const size_t  other_wc   = other.wordCount();

    const size_t full_words = len / WORD_BITS;
    const size_t tail_bits  = len % WORD_BITS;
    const word_t tail_mask  = tail_bits == 0 ? ~word_t{0}
                              : (word_t{1} << tail_bits) - 1;

    const size_t this_shift = offset % WORD_BITS;

    // Fast path: this-side is word-aligned — no shift/combine needed
    if (this_shift == 0) {
        const size_t base = offset / WORD_BITS;
        for (size_t i = 0; i < full_words; ++i) {
            word_t a = this_data[base + i];
            word_t b = (i < other_wc) ? other_data[i] : word_t{0};
            if (a != b) return false;
        }
        if (tail_bits != 0) {
            word_t a = (base + full_words < this_wc)
                       ? this_data[base + full_words] : word_t{0};
            word_t b = (full_words < other_wc)
                       ? other_data[full_words] : word_t{0};
            if ((a & tail_mask) != (b & tail_mask)) return false;
        }
        return true;
    }

    // General path: this-side at arbitrary bit offset
    for (size_t i = 0; i < full_words; ++i) {
        word_t a = detail::extractWord(this_data, this_wc, offset, i);
        word_t b = (i < other_wc) ? other_data[i] : word_t{0};
        if (a != b) return false;
    }
    if (tail_bits != 0) {
        word_t a = detail::extractWord(this_data, this_wc, offset, full_words);
        word_t b = (full_words < other_wc) ? other_data[full_words] : word_t{0};
        if ((a & tail_mask) != (b & tail_mask)) return false;
    }
    return true;
}

template<size_t N>
template<size_t M>
constexpr Bitset<N>& Bitset<N>::assignRange(const Bitset<M>& src,
                                            size_t offset,
                                            size_t len) noexcept {
    // Resolve default length
    if (len == npos) {
        len = src.size();
    }
    if (len == 0) {
        return *this;
    }

    assert(offset <= size() && "assignRange: offset exceeds this->size()");
    assert(len <= size() - offset && "assignRange: offset+len exceeds this->size()");
    assert(len <= src.size() && "assignRange: len exceeds src.size()");

    word_t*       dst_data = wordData();
    const size_t  dst_wc   = wordCount();
    const auto*   src_data = src.wordData();
    const size_t  src_wc   = src.wordCount();

    const size_t full_words = len / WORD_BITS;
    const size_t tail_bits  = len % WORD_BITS;
    const word_t tail_mask  = tail_bits == 0 ? ~word_t{0}
                              : (word_t{1} << tail_bits) - 1;

    const size_t dst_shift = offset % WORD_BITS;

    // Fast path: destination is word-aligned
    if (dst_shift == 0) {
        const size_t base = offset / WORD_BITS;
        for (size_t i = 0; i < full_words; ++i) {
            word_t s = (i < src_wc) ? src_data[i] : word_t{0};
            dst_data[base + i] = s;
        }
        if (tail_bits != 0) {
            word_t s = (full_words < src_wc) ? src_data[full_words] : word_t{0};
            word_t& d = dst_data[base + full_words];
            d = (d & ~tail_mask) | (s & tail_mask);
        }
        clearUnusedBits();
        return *this;
    }

    // General path: destination at arbitrary bit offset — scatter src words
    // into dst using shift-split-mask approach.
    //
    // Each logical source word i maps to dst words at:
    //   base_word = offset/WORD_BITS + i
    // Split: low part shifted left by dst_shift, high part shifted right.
    const size_t base_word = offset / WORD_BITS;
    const size_t inv_shift = WORD_BITS - dst_shift;

    auto scatterWord = [&](size_t i, word_t src_val, word_t mask) {
        // mask defines which bits of src_val are meaningful (e.g. tail_mask)
        const word_t val = src_val & mask;
        const size_t lo_idx = base_word + i;

        // Low portion
        const word_t lo_mask = mask << dst_shift;
        const word_t lo_bits = val << dst_shift;
        if (lo_idx < dst_wc) {
            dst_data[lo_idx] = (dst_data[lo_idx] & ~lo_mask) | lo_bits;
        }

        // High portion (spills into next word)
        const word_t hi_mask = mask >> inv_shift;
        if (hi_mask != 0 && lo_idx + 1 < dst_wc) {
            const word_t hi_bits = val >> inv_shift;
            dst_data[lo_idx + 1] = (dst_data[lo_idx + 1] & ~hi_mask) | hi_bits;
        }
    };

    for (size_t i = 0; i < full_words; ++i) {
        word_t s = (i < src_wc) ? src_data[i] : word_t{0};
        scatterWord(i, s, ~word_t{0});
    }
    if (tail_bits != 0) {
        word_t s = (full_words < src_wc) ? src_data[full_words] : word_t{0};
        scatterWord(full_words, s, tail_mask);
    }

    clearUnusedBits();
    return *this;
}

// ===== Non-member Operator Implementations =====

template<size_t N>
Bitset<N> operator&(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept {
    Bitset<N> result = lhs;
    result &= rhs;
    return result;
}

template<size_t N>
Bitset<N> operator|(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept {
    Bitset<N> result = lhs;
    result |= rhs;
    return result;
}

template<size_t N>
Bitset<N> operator^(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept {
    Bitset<N> result = lhs;
    result ^= rhs;
    return result;
}

template<size_t N>
bool operator==(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept {
    if constexpr (N == DYNAMIC_EXTENT) {
        if (lhs.size() != rhs.size()) return false;
    }
    
    const size_t wc = lhs.wordCount();
    for (size_t i = 0; i < wc; ++i) {
        if (lhs.wordData()[i] != rhs.wordData()[i]) return false;
    }
    return true;
}

template<size_t N>
bool operator!=(const Bitset<N>& lhs, const Bitset<N>& rhs) noexcept {
    return !(lhs == rhs);
}

template<class CharT, class Traits, size_t N>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, 
    const Bitset<N>& x) 
{
    return os << x.toString(CharT('0'), CharT('1'));
}

template<class CharT, class Traits, size_t N>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is, 
    Bitset<N>& x) 
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
        if constexpr (N == DYNAMIC_EXTENT) {
            x = Bitset<>(str.length(), str);
        } else {
            x = Bitset<N>(str);
        }
    }
    
    return is;
}

// ===== Free-function Block Operations (dual-offset) =====

template<size_t N, size_t M>
constexpr bool equalRange(const Bitset<N>& a, size_t offset_a,
                          const Bitset<M>& b, size_t offset_b,
                          size_t len) noexcept {
    using word_t = typename Bitset<N>::word_t;
    static_assert(std::is_same_v<word_t, typename Bitset<M>::word_t>,
                  "equalRange requires identical word types");
    constexpr size_t WBITS = sizeof(word_t) * 8;

    if (len == 0) return true;

    assert(offset_a <= a.size() && "equalRange: offset_a exceeds a.size()");
    assert(len <= a.size() - offset_a && "equalRange: offset_a+len exceeds a.size()");
    assert(offset_b <= b.size() && "equalRange: offset_b exceeds b.size()");
    assert(len <= b.size() - offset_b && "equalRange: offset_b+len exceeds b.size()");

    const word_t* a_data = a.wordData();
    const size_t  a_wc   = a.wordCount();
    const word_t* b_data = b.wordData();
    const size_t  b_wc   = b.wordCount();

    const size_t full_words = len / WBITS;
    const size_t tail_bits  = len % WBITS;
    const word_t tail_mask  = tail_bits == 0 ? ~word_t{0}
                              : (word_t{1} << tail_bits) - 1;

    // Fast path: both sides word-aligned — direct word comparison
    if (offset_a % WBITS == 0 && offset_b % WBITS == 0) {
        const size_t base_a = offset_a / WBITS;
        const size_t base_b = offset_b / WBITS;
        for (size_t i = 0; i < full_words; ++i) {
            word_t wa = (base_a + i < a_wc) ? a_data[base_a + i] : word_t{0};
            word_t wb = (base_b + i < b_wc) ? b_data[base_b + i] : word_t{0};
            if (wa != wb) return false;
        }
        if (tail_bits != 0) {
            word_t wa = (base_a + full_words < a_wc)
                        ? a_data[base_a + full_words] : word_t{0};
            word_t wb = (base_b + full_words < b_wc)
                        ? b_data[base_b + full_words] : word_t{0};
            if ((wa & tail_mask) != (wb & tail_mask)) return false;
        }
        return true;
    }

    // General path: arbitrary bit offsets — use extractWord on both sides
    for (size_t i = 0; i < full_words; ++i) {
        word_t wa = detail::extractWord(a_data, a_wc, offset_a, i);
        word_t wb = detail::extractWord(b_data, b_wc, offset_b, i);
        if (wa != wb) return false;
    }
    if (tail_bits != 0) {
        word_t wa = detail::extractWord(a_data, a_wc, offset_a, full_words);
        word_t wb = detail::extractWord(b_data, b_wc, offset_b, full_words);
        if ((wa & tail_mask) != (wb & tail_mask)) return false;
    }
    return true;
}

template<size_t N, size_t M>
constexpr void assignRange(Bitset<N>& dst, size_t offset_dst,
                           const Bitset<M>& src, size_t offset_src,
                           size_t len) noexcept {
    using word_t = typename Bitset<N>::word_t;
    static_assert(std::is_same_v<word_t, typename Bitset<M>::word_t>,
                  "assignRange requires identical word types");
    constexpr size_t WBITS = sizeof(word_t) * 8;

    if (len == 0) return;

    assert(offset_dst <= dst.size() && "assignRange: offset_dst exceeds dst.size()");
    assert(len <= dst.size() - offset_dst && "assignRange: offset_dst+len exceeds dst.size()");
    assert(offset_src <= src.size() && "assignRange: offset_src exceeds src.size()");
    assert(len <= src.size() - offset_src && "assignRange: offset_src+len exceeds src.size()");

    // Direct scatter: extract each source word and scatter into destination.
    // No temporary allocation — works with arbitrary offsets on both sides.

    const word_t* src_data = src.wordData();
    const size_t  src_wc   = src.wordCount();
    word_t*       dst_data = dst.wordData();
    const size_t  dst_wc   = dst.wordCount();

    const size_t full_words = len / WBITS;
    const size_t tail_bits  = len % WBITS;
    const word_t tail_mask  = tail_bits == 0 ? ~word_t{0}
                              : (word_t{1} << tail_bits) - 1;

    const size_t dst_shift = offset_dst % WBITS;
    const size_t dst_base  = offset_dst / WBITS;

    // Fast path: both sides word-aligned — direct word copy with tail mask
    if (dst_shift == 0 && offset_src % WBITS == 0) {
        const size_t src_base = offset_src / WBITS;
        for (size_t i = 0; i < full_words; ++i) {
            word_t s = (src_base + i < src_wc) ? src_data[src_base + i] : word_t{0};
            dst_data[dst_base + i] = s;
        }
        if (tail_bits != 0) {
            word_t s = (src_base + full_words < src_wc)
                       ? src_data[src_base + full_words] : word_t{0};
            word_t& d = dst_data[dst_base + full_words];
            d = (d & ~tail_mask) | (s & tail_mask);
        }
        dst.clearUnusedBits();
        return;
    }

    // General path: extract source words at arbitrary offset, scatter into dst.
    const size_t inv_shift = WBITS - dst_shift;

    auto scatterWord = [&](size_t i, word_t src_val, word_t mask) {
        const word_t val = src_val & mask;
        const size_t lo_idx = dst_base + i;

        if (dst_shift == 0) {
            // Destination word-aligned — direct masked write
            if (lo_idx < dst_wc) {
                if (mask == ~word_t{0}) {
                    dst_data[lo_idx] = val;
                } else {
                    dst_data[lo_idx] = (dst_data[lo_idx] & ~mask) | val;
                }
            }
            return;
        }

        // Low portion
        const word_t lo_mask = mask << dst_shift;
        const word_t lo_bits = val << dst_shift;
        if (lo_idx < dst_wc) {
            dst_data[lo_idx] = (dst_data[lo_idx] & ~lo_mask) | lo_bits;
        }

        // High portion (spills into next word)
        const word_t hi_mask = mask >> inv_shift;
        if (hi_mask != 0 && lo_idx + 1 < dst_wc) {
            const word_t hi_bits = val >> inv_shift;
            dst_data[lo_idx + 1] = (dst_data[lo_idx + 1] & ~hi_mask) | hi_bits;
        }
    };

    for (size_t i = 0; i < full_words; ++i) {
        word_t s = detail::extractWord(src_data, src_wc, offset_src, i);
        scatterWord(i, s, ~word_t{0});
    }
    if (tail_bits != 0) {
        word_t s = detail::extractWord(src_data, src_wc, offset_src, full_words);
        scatterWord(full_words, s, tail_mask);
    }

    dst.clearUnusedBits();
}

} // namespace bcsv

// ===== Hash Support =====

namespace std {
    template<size_t N>
    struct hash<bcsv::Bitset<N>> {
        size_t operator()(const bcsv::Bitset<N>& bs) const noexcept {
            // FNV-1a hash algorithm
            constexpr size_t fnv_prime = sizeof(size_t) == 8 ? 1099511628211ULL : 16777619UL;
            constexpr size_t fnv_offset = sizeof(size_t) == 8 ? 14695981039346656037ULL : 2166136261UL;
            
            size_t result = fnv_offset;
            
            // Hash the bit count for dynamic bitsets
            if constexpr (N == bcsv::DYNAMIC_EXTENT) {
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
