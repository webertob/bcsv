#pragma once

#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <iostream>
#include <bit>
#include <cstring>
#include <limits>
#include "bitset.hpp"

namespace bcsv {

/**
 * @brief Custom allocator that ensures byte-aligned allocation for compatibility
 * with bcsv::bitset storage layout
 */
template<typename T>
class byte_aligned_allocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    template<typename U>
    struct rebind {
        using other = byte_aligned_allocator<U>;
    };
    
    byte_aligned_allocator() noexcept = default;
    
    template<typename U>
    byte_aligned_allocator(const byte_aligned_allocator<U>&) noexcept {}
    
    pointer allocate(size_type n) {
        if (n > max_size()) {
            throw std::bad_alloc();
        }
        
        // Ensure allocation is aligned to byte boundaries
        size_type bytes_needed = n * sizeof(T);
        void* ptr = std::aligned_alloc(alignof(std::byte), bytes_needed);
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        return static_cast<pointer>(ptr);
    }
    
    void deallocate(pointer p, size_type) noexcept {
        std::free(p);
    }
    
    size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }
    
    template<typename U>
    bool operator==(const byte_aligned_allocator<U>&) const noexcept {
        return true;
    }
    
    template<typename U>
    bool operator!=(const byte_aligned_allocator<U>&) const noexcept {
        return false;
    }
};

/**
 * @brief Runtime-flexible bitset that maintains binary compatibility with bcsv::bitset
 * 
 * This class provides a dynamic-size bitset that uses the same memory layout as
 * bcsv::bitset<N>, ensuring binary compatibility for serialization and data exchange.
 */
class bitset_dynamic {
private:
    std::vector<std::byte, byte_aligned_allocator<std::byte>> storage_;
    size_t bit_count_;
    
public:
    /**
     * @brief Proxy class for individual bit access (same as bcsv::bitset)
     */
    class reference {
    private:
        std::byte* byte_ptr;
        size_t bit_index;
        
    public:
        reference(std::byte* ptr, size_t bit_idx) 
            : byte_ptr(ptr), bit_index(bit_idx) {}
        
        reference& operator=(bool value) {
            if (value) {
                *byte_ptr |= std::byte{1} << bit_index;
            } else {
                *byte_ptr &= ~(std::byte{1} << bit_index);
            }
            return *this;
        }
        
        reference& operator=(const reference& other) {
            return *this = bool(other);
        }
        
        operator bool() const {
            return (*byte_ptr & (std::byte{1} << bit_index)) != std::byte{0};
        }
        
        bool operator~() const {
            return !bool(*this);
        }
        
        reference& flip() {
            *byte_ptr ^= std::byte{1} << bit_index;
            return *this;
        }
    };
    
    // Constructors
    explicit bitset_dynamic(size_t bit_count = 0) 
        : storage_((bit_count + 7) / 8, std::byte{0}), bit_count_(bit_count) {}
    
    bitset_dynamic(size_t bit_count, bool value) 
        : storage_((bit_count + 7) / 8, value ? std::byte{0xFF} : std::byte{0}), 
          bit_count_(bit_count) {
        if (value) {
            clear_unused_bits();
        }
    }
    
    bitset_dynamic(size_t bit_count, unsigned long long val) 
        : storage_((bit_count + 7) / 8, std::byte{0}), bit_count_(bit_count) {
        set_from_value(val);
    }
    
    template<class CharT, class Traits, class Allocator>
    explicit bitset_dynamic(const std::basic_string<CharT, Traits, Allocator>& str,
                           size_t bit_count,
                           typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
                           typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
                               std::basic_string<CharT, Traits, Allocator>::npos,
                           CharT zero = CharT('0'),
                           CharT one = CharT('1')) 
        : storage_((bit_count + 7) / 8, std::byte{0}), bit_count_(bit_count) {
        set_from_string(str, pos, n, zero, one);
    }
    
    // Construct from fixed-size bitset (binary compatible)
    template<size_t N>
    explicit bitset_dynamic(const bcsv::bitset<N>& fixed_bitset) 
        : storage_((N + 7) / 8, std::byte{0}), bit_count_(N) {
        const auto& fixed_bytes = fixed_bitset.asArray();
        std::memcpy(storage_.data(), fixed_bytes.data(), storage_.size());
    }
    
    // Convert to fixed-size bitset (binary compatible)
    template<size_t N>
    bcsv::bitset<N> to_fixed_bitset() const {
        if (bit_count_ != N) {
            throw std::invalid_argument("Bit count mismatch: bitset_dynamic has " + 
                                      std::to_string(bit_count_) + " bits, target has " + 
                                      std::to_string(N) + " bits");
        }
        
        bcsv::bitset<N> result;
        auto& result_bytes = result.asArray();
        std::memcpy(result_bytes.data(), storage_.data(), std::min(storage_.size(), result_bytes.size()));
        return result;
    }
    
    // Size operations
    constexpr size_t size() const noexcept { return bit_count_; }
    constexpr size_t sizeBytes() const noexcept { return storage_.size(); }
    bool empty() const noexcept { return bit_count_ == 0; }
    
    void resize(size_t new_bit_count, bool value = false) {
        size_t old_bit_count = bit_count_;
        size_t old_byte_count = storage_.size();
        size_t new_byte_count = (new_bit_count + 7) / 8;
        
        // Update bit count first
        bit_count_ = new_bit_count;
        
        if (new_byte_count > old_byte_count) {
            // Growing: add new bytes
            storage_.resize(new_byte_count, value ? std::byte{0xFF} : std::byte{0});
        } else if (new_byte_count < old_byte_count) {
            // Shrinking: remove bytes
            storage_.resize(new_byte_count);
        }
        
        // If adding bits and value is true, set the new bits
        if (value && new_bit_count > old_bit_count) {
            for (size_t i = old_bit_count; i < new_bit_count; ++i) {
                set(i, true);
            }
        }
        
        clear_unused_bits();
    }
    
    void reserve(size_t bit_capacity) {
        storage_.reserve((bit_capacity + 7) / 8);
    }
    
    void shrink_to_fit() {
        storage_.shrink_to_fit();
    }
    
    void clear() noexcept {
        bit_count_ = 0;
        storage_.clear();
    }
    
    // Bit access (same interface as bcsv::bitset)
    bool operator[](size_t pos) const {
        if (pos >= bit_count_) return false;
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        return (storage_[byte_idx] & (std::byte{1} << bit_idx)) != std::byte{0};
    }
    
    reference operator[](size_t pos) {
        if (pos >= bit_count_) {
            throw std::out_of_range("bitset_dynamic::operator[]: index out of range");
        }
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        return reference(&storage_[byte_idx], bit_idx);
    }
    
    bool test(size_t pos) const {
        if (pos >= bit_count_) {
            throw std::out_of_range("bitset_dynamic::test: index out of range");
        }
        return (*this)[pos];
    }
    
    // Bit operations (same interface as bcsv::bitset)
    bitset_dynamic& set() noexcept {
        if (storage_.empty()) return *this;
        
        set_all_optimized();
        return *this;
    }

private:
    void set_all_optimized() noexcept {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(storage_.data());
        const size_t byte_count = storage_.size();
        
        if (byte_count >= 8) {
            // Use 64-bit chunks for larger bitsets
            size_t i = 0;
            
            // Process 8 bytes at a time
            for (; i + 8 <= byte_count; i += 8) {
                uint64_t chunk = 0xFFFFFFFFFFFFFFFFULL;
                std::memcpy(bytes + i, &chunk, 8);
            }
            
            // Handle remaining bytes (1-7 bytes)
            for (; i < byte_count; ++i) {
                bytes[i] = 0xFF;
            }
            
        } else if (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            if (byte_count == 4) {
                uint32_t chunk = 0xFFFFFFFFU;
                std::memcpy(bytes, &chunk, 4);
            } else {
                // 5-7 bytes: set 32-bit chunk + remaining bytes
                uint32_t chunk = 0xFFFFFFFFU;
                std::memcpy(bytes, &chunk, 4);
                
                for (size_t i = 4; i < byte_count; ++i) {
                    bytes[i] = 0xFF;
                }
            }
            
        } else if (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            if (byte_count == 2) {
                uint16_t chunk = 0xFFFFU;
                std::memcpy(bytes, &chunk, 2);
            } else {
                // 3 bytes: set 16-bit chunk + last byte
                uint16_t chunk = 0xFFFFU;
                std::memcpy(bytes, &chunk, 2);
                bytes[2] = 0xFF;
            }
            
        } else if (byte_count == 1) {
            // Single byte
            bytes[0] = 0xFF;
        }
        
        // Clear unused bits in the last byte
        clear_unused_bits();
    }

public:
    
    bitset_dynamic& set(size_t pos, bool val = true) {
        if (pos >= bit_count_) {
            throw std::out_of_range("bitset_dynamic::set: index out of range");
        }
        
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        
        if (val) {
            storage_[byte_idx] |= std::byte{1} << bit_idx;
        } else {
            storage_[byte_idx] &= ~(std::byte{1} << bit_idx);
        }
        return *this;
    }
    
    bitset_dynamic& reset() noexcept {
        if (storage_.empty()) return *this;
        
        reset_all_optimized();
        return *this;
    }

private:
    void reset_all_optimized() noexcept {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(storage_.data());
        const size_t byte_count = storage_.size();
        
        if (byte_count >= 8) {
            // Use 64-bit chunks for larger bitsets
            size_t i = 0;
            
            // Process 8 bytes at a time
            for (; i + 8 <= byte_count; i += 8) {
                uint64_t chunk = 0;
                std::memcpy(bytes + i, &chunk, 8);
            }
            
            // Handle remaining bytes (1-7 bytes)
            for (; i < byte_count; ++i) {
                bytes[i] = 0;
            }
            
        } else if (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            if (byte_count == 4) {
                uint32_t chunk = 0;
                std::memcpy(bytes, &chunk, 4);
            } else {
                // 5-7 bytes: reset 32-bit chunk + remaining bytes
                uint32_t chunk = 0;
                std::memcpy(bytes, &chunk, 4);
                
                for (size_t i = 4; i < byte_count; ++i) {
                    bytes[i] = 0;
                }
            }
            
        } else if (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            if (byte_count == 2) {
                uint16_t chunk = 0;
                std::memcpy(bytes, &chunk, 2);
            } else {
                // 3 bytes: reset 16-bit chunk + last byte
                uint16_t chunk = 0;
                std::memcpy(bytes, &chunk, 2);
                bytes[2] = 0;
            }
            
        } else if (byte_count == 1) {
            // Single byte
            bytes[0] = 0;
        }
    }

public:
    
    bitset_dynamic& reset(size_t pos) {
        return set(pos, false);
    }
    
    bitset_dynamic& flip() noexcept {
        for (auto& byte : storage_) {
            byte = ~byte;
        }
        clear_unused_bits();
        return *this;
    }
    
    bitset_dynamic& flip(size_t pos) {
        if (pos >= bit_count_) {
            throw std::out_of_range("bitset_dynamic::flip: index out of range");
        }
        
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        storage_[byte_idx] ^= std::byte{1} << bit_idx;
        return *this;
    }
    
    // Queries (same interface as bcsv::bitset)
    size_t count() const noexcept {
        size_t result = 0;
        for (const auto& byte : storage_) {
            result += std::popcount(static_cast<uint8_t>(byte));
        }
        return result;
    }
    
    bool all() const noexcept {
        if (empty()) return true;
        
        return all_optimized();
    }

private:
    bool all_optimized() const noexcept {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(storage_.data());
        const size_t byte_count = storage_.size();
        
        if (byte_count == 0) return true;
        
        if (byte_count >= 8) {
            // Use 64-bit chunks for larger bitsets
            size_t i = 0;
            
            // Process full 8-byte chunks (except possibly the last one)
            const size_t full_chunks = (byte_count - 1) / 8; // Don't include last chunk if it contains the partial byte
            for (; i < full_chunks * 8; i += 8) {
                uint64_t chunk;
                std::memcpy(&chunk, bytes + i, 8);
                if (chunk != 0xFFFFFFFFFFFFFFFFULL) return false;
            }
            
            // Handle remaining bytes (including the last byte which might be partial)
            for (; i < byte_count - 1; ++i) {
                if (bytes[i] != 0xFF) return false;
            }
            
            // Handle last byte (might be partial)
            if (bit_count_ % 8 != 0) {
                const uint8_t mask = (1 << (bit_count_ % 8)) - 1;
                return (bytes[byte_count - 1] & mask) == mask;
            } else {
                return bytes[byte_count - 1] == 0xFF;
            }
            
        } else if (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            // Handle remaining full bytes except the last one
            for (size_t i = 0; i < byte_count - 1; ++i) {
                if (bytes[i] != 0xFF) return false;
            }
            
            // Handle last byte (might be partial)
            if (bit_count_ % 8 != 0) {
                const uint8_t mask = (1 << (bit_count_ % 8)) - 1;
                return (bytes[byte_count - 1] & mask) == mask;
            } else {
                return bytes[byte_count - 1] == 0xFF;
            }
            
        } else if (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            // Handle remaining full bytes except the last one
            for (size_t i = 0; i < byte_count - 1; ++i) {
                if (bytes[i] != 0xFF) return false;
            }
            
            // Handle last byte (might be partial)
            if (bit_count_ % 8 != 0) {
                const uint8_t mask = (1 << (bit_count_ % 8)) - 1;
                return (bytes[byte_count - 1] & mask) == mask;
            } else {
                return bytes[byte_count - 1] == 0xFF;
            }
            
        } else if (byte_count == 1) {
            // Single byte
            if (bit_count_ % 8 == 0) {
                return bytes[0] == 0xFF;
            } else {
                const uint8_t mask = (1 << bit_count_) - 1;
                return (bytes[0] & mask) == mask;
            }
        }
        
        return true; // Empty bitset
    }

public:
    
    bool any() const noexcept {
        if (storage_.empty()) return false;
        
        return any_optimized();
    }

private:
    bool any_optimized() const noexcept {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(storage_.data());
        const size_t byte_count = storage_.size();
        
        if (byte_count >= 8) {
            // Use 64-bit chunks for larger bitsets
            size_t i = 0;
            
            // Process 8 bytes at a time
            for (; i + 8 <= byte_count; i += 8) {
                uint64_t chunk;
                std::memcpy(&chunk, bytes + i, 8);
                if (chunk != 0) return true;
            }
            
            // Handle remaining bytes (1-7 bytes)
            if (i < byte_count) {
                // Read remaining bytes as smaller chunks
                if (i + 4 <= byte_count) {
                    uint32_t chunk32;
                    std::memcpy(&chunk32, bytes + i, 4);
                    if (chunk32 != 0) return true;
                    i += 4;
                }
                if (i + 2 <= byte_count) {
                    uint16_t chunk16;
                    std::memcpy(&chunk16, bytes + i, 2);
                    if (chunk16 != 0) return true;
                    i += 2;
                }
                if (i < byte_count) {
                    if (bytes[i] != 0) return true;
                }
            }
            
            return false;
            
        } else if (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            uint32_t chunk;
            std::memcpy(&chunk, bytes, 4);
            
            if (byte_count == 4) {
                return chunk != 0;
            } else {
                // 5-7 bytes: check 32-bit chunk + remaining bytes
                if (chunk != 0) return true;
                
                for (size_t i = 4; i < byte_count; ++i) {
                    if (bytes[i] != 0) return true;
                }
                return false;
            }
            
        } else if (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            uint16_t chunk;
            std::memcpy(&chunk, bytes, 2);
            
            if (byte_count == 2) {
                return chunk != 0;
            } else {
                // 3 bytes: check 16-bit chunk + last byte
                return chunk != 0 || bytes[2] != 0;
            }
            
        } else if (byte_count == 1) {
            // Single byte
            return bytes[0] != 0;
        } else {
            // Empty
            return false;
        }
    }

public:
    
    bool none() const noexcept {
        return !any();
    }
    
    // Bitwise operations
    bitset_dynamic operator~() const {
        bitset_dynamic result(bit_count_);
        for (size_t i = 0; i < storage_.size(); ++i) {
            result.storage_[i] = ~storage_[i];
        }
        result.clear_unused_bits();
        return result;
    }
    
    bitset_dynamic& operator&=(const bitset_dynamic& other) {
        if (bit_count_ != other.bit_count_) {
            throw std::invalid_argument("bitset_dynamic size mismatch in &=");
        }
        
        for (size_t i = 0; i < storage_.size(); ++i) {
            storage_[i] &= other.storage_[i];
        }
        return *this;
    }
    
    bitset_dynamic& operator|=(const bitset_dynamic& other) {
        if (bit_count_ != other.bit_count_) {
            throw std::invalid_argument("bitset_dynamic size mismatch in |=");
        }
        
        for (size_t i = 0; i < storage_.size(); ++i) {
            storage_[i] |= other.storage_[i];
        }
        return *this;
    }
    
    bitset_dynamic& operator^=(const bitset_dynamic& other) {
        if (bit_count_ != other.bit_count_) {
            throw std::invalid_argument("bitset_dynamic size mismatch in ^=");
        }
        
        for (size_t i = 0; i < storage_.size(); ++i) {
            storage_[i] ^= other.storage_[i];
        }
        return *this;
    }
    
    // Shift operations
    bitset_dynamic operator<<(size_t pos) const {
        bitset_dynamic result(bit_count_);
        if (pos == 0) return *this;
        if (pos >= bit_count_) return result; // All bits shifted out
        
        // Byte-level shifting with carry propagation
        const size_t byte_shift = pos / 8;
        const size_t bit_shift = pos % 8;
        
        if (bit_shift == 0) {
            // Pure byte alignment - just copy bytes with offset
            for (size_t i = byte_shift; i < storage_.size(); ++i) {
                result.storage_[i] = storage_[i - byte_shift];
            }
        } else {
            // Need to handle bit-level carry between bytes
            const size_t inv_bit_shift = 8 - bit_shift;
            
            // Start from the end to avoid overwriting data we still need
            for (size_t i = byte_shift; i < storage_.size(); ++i) {
                const size_t src_idx = i - byte_shift;
                
                // Shift current byte
                result.storage_[i] |= storage_[src_idx] << bit_shift;
                
                // Handle carry from previous byte (if exists and we're not at the beginning)
                if (src_idx > 0) {
                    result.storage_[i] |= storage_[src_idx - 1] >> inv_bit_shift;
                }
            }
        }
        
        result.clear_unused_bits();
        return result;
    }
    
    bitset_dynamic operator>>(size_t pos) const {
        bitset_dynamic result(bit_count_);
        if (pos == 0) return *this;
        if (pos >= bit_count_) return result; // All bits shifted out
        
        // Byte-level shifting with carry propagation
        const size_t byte_shift = pos / 8;
        const size_t bit_shift = pos % 8;
        
        if (bit_shift == 0) {
            // Pure byte alignment - just copy bytes with offset
            for (size_t i = 0; i < storage_.size() - byte_shift; ++i) {
                result.storage_[i] = storage_[i + byte_shift];
            }
        } else {
            // Need to handle bit-level carry between bytes
            const size_t inv_bit_shift = 8 - bit_shift;
            
            for (size_t i = 0; i < storage_.size() - byte_shift; ++i) {
                const size_t src_idx = i + byte_shift;
                
                // Shift current byte and add to result
                result.storage_[i] |= storage_[src_idx] >> bit_shift;
                
                // Handle carry from next byte (if exists)
                if (src_idx + 1 < storage_.size()) {
                    result.storage_[i] |= storage_[src_idx + 1] << inv_bit_shift;
                }
            }
        }
        
        result.clear_unused_bits();
        return result;
    }
    
    bitset_dynamic& operator<<=(size_t pos) {
        *this = *this << pos;
        return *this;
    }
    
    bitset_dynamic& operator>>=(size_t pos) {
        *this = *this >> pos;
        return *this;
    }
    
    // Conversion methods
    unsigned long to_ulong() const {
        if (bit_count_ > 32) {
            for (size_t i = 32; i < bit_count_; ++i) {
                if ((*this)[i]) {
                    throw std::overflow_error("bitset_dynamic::to_ulong: value contains set bits beyond position 31");
                }
            }
        }
        
        unsigned long result = 0;
        const size_t bytes_to_copy = std::min(sizeof(unsigned long), storage_.size());
        
        for (size_t i = 0; i < bytes_to_copy; ++i) {
            result |= static_cast<unsigned long>(storage_[i]) << (i * 8);
        }
        
        return result;
    }
    
    unsigned long long to_ullong() const {
        if (bit_count_ > 64) {
            for (size_t i = 64; i < bit_count_; ++i) {
                if ((*this)[i]) {
                    throw std::overflow_error("bitset_dynamic::to_ullong: value contains set bits beyond position 63");
                }
            }
        }
        
        unsigned long long result = 0;
        const size_t bytes_to_copy = std::min(sizeof(unsigned long long), storage_.size());
        
        for (size_t i = 0; i < bytes_to_copy; ++i) {
            result |= static_cast<unsigned long long>(storage_[i]) << (i * 8);
        }
        
        return result;
    }
    
    std::string to_string(char zero = '0', char one = '1') const {
        std::string result;
        result.reserve(bit_count_);
        
        for (size_t i = bit_count_; i > 0; --i) {
            result += (*this)[i - 1] ? one : zero;
        }
        
        return result;
    }
    
    // Direct byte access (compatible with bcsv::bitset)
    std::byte* data() noexcept { return storage_.data(); }
    const std::byte* data() const noexcept { return storage_.data(); }
    
    // Binary compatibility methods
    
    /**
     * @brief Check if this bitset_dynamic is binary compatible with a fixed-size bitset
     */
    template<size_t N>
    bool is_compatible_with() const noexcept {
        return bit_count_ == N && storage_.size() == (N + 7) / 8;
    }
    
    /**
     * @brief Get raw byte data for serialization (compatible with bcsv::bitset)
     */
    std::vector<std::byte> get_bytes() const {
        return std::vector<std::byte>(storage_.begin(), storage_.end());
    }
    
    /**
     * @brief Set data from raw bytes (compatible with bcsv::bitset)
     */
    void set_bytes(const std::vector<std::byte>& bytes, size_t new_bit_count) {
        bit_count_ = new_bit_count;
        storage_.assign(bytes.begin(), bytes.end());
        
        // Ensure we have enough bytes
        size_t required_bytes = (bit_count_ + 7) / 8;
        if (storage_.size() < required_bytes) {
            storage_.resize(required_bytes, std::byte{0});
        }
        
        clear_unused_bits();
    }
    
private:
    void clear_unused_bits() noexcept {
        if (bit_count_ % 8 != 0 && !storage_.empty()) {
            const uint8_t mask_val = (1u << (bit_count_ % 8)) - 1u;
            const std::byte mask = std::byte{mask_val};
            storage_.back() &= mask;
        }
    }
    
    void set_from_value(unsigned long long val) noexcept {
        const size_t bytes_to_set = std::min(sizeof(val), storage_.size());
        
        for (size_t i = 0; i < bytes_to_set; ++i) {
            storage_[i] = std::byte(val >> (i * 8));
        }
        
        clear_unused_bits();
    }
    
    template<class CharT, class Traits, class Allocator>
    void set_from_string(const std::basic_string<CharT, Traits, Allocator>& str,
                        typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
                        typename std::basic_string<CharT, Traits, Allocator>::size_type n,
                        CharT zero, CharT one) {
        const auto len = std::min(n, str.length() - pos);
        
        for (size_t i = 0; i < std::min(len, static_cast<decltype(len)>(bit_count_)); ++i) {
            const auto ch = str[pos + len - 1 - i];
            if (ch == one) {
                set(i);
            } else if (ch != zero) {
                throw std::invalid_argument("bitset_dynamic::bitset_dynamic: invalid character in string");
            }
        }
    }
};

// Non-member operators for bitset_dynamic
inline bitset_dynamic operator&(const bitset_dynamic& lhs, const bitset_dynamic& rhs) {
    bitset_dynamic result = lhs;
    result &= rhs;
    return result;
}

inline bitset_dynamic operator|(const bitset_dynamic& lhs, const bitset_dynamic& rhs) {
    bitset_dynamic result = lhs;
    result |= rhs;
    return result;
}

inline bitset_dynamic operator^(const bitset_dynamic& lhs, const bitset_dynamic& rhs) {
    bitset_dynamic result = lhs;
    result ^= rhs;
    return result;
}

inline bool operator==(const bitset_dynamic& lhs, const bitset_dynamic& rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    
    // Compare underlying byte storage
    return std::equal(lhs.data(), lhs.data() + lhs.sizeBytes(),
                     rhs.data(), rhs.data() + rhs.sizeBytes());
}

inline bool operator!=(const bitset_dynamic& lhs, const bitset_dynamic& rhs) noexcept {
    return !(lhs == rhs);
}

template<class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const bitset_dynamic& x) {
    return os << x.to_string(CharT('0'), CharT('1'));
}

template<class CharT, class Traits>
std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, bitset_dynamic& x) {
    std::basic_string<CharT, Traits> str;
    
    CharT ch;
    while (is >> ch) {
        if (ch != CharT('0') && ch != CharT('1')) {
            is.putback(ch);
            break;
        }
        str += ch;
    }
    
    if (!str.empty()) {
        x = bitset_dynamic(str, str.length());
    }
    
    return is;
}

// Cross-compatibility functions

/**
 * @brief Convert fixed-size bitset to dynamic bitset
 */
template<size_t N>
bitset_dynamic to_dynamic(const bcsv::bitset<N>& fixed_bitset) {
    return bitset_dynamic(fixed_bitset);
}

/**
 * @brief Convert dynamic bitset to fixed-size bitset
 */
template<size_t N>
bcsv::bitset<N> to_fixed(const bitset_dynamic& bitset_dynamic) {
    return bitset_dynamic.template to_fixed_bitset<N>();
}

/**
 * @brief Check binary compatibility between fixed and dynamic bitsets
 */
template<size_t N>
bool are_binary_compatible(const bcsv::bitset<N>& fixed_bitset, const bitset_dynamic& bitset_dynamic) {
    return bitset_dynamic.template is_compatible_with<N>() &&
           std::memcmp(fixed_bitset.data(), bitset_dynamic.data(), (N + 7) / 8) == 0;
}

} // namespace bcsv

// Hash support for bcsv::bitset_dynamic
namespace std {
    template<>
    struct hash<bcsv::bitset_dynamic> {
        size_t operator()(const bcsv::bitset_dynamic& bs) const noexcept {
            size_t result = 0;
            
            // Use FNV-1a hash algorithm for byte array
            constexpr size_t fnv_prime = sizeof(size_t) == 8 ? 1099511628211ULL : 16777619UL;
            constexpr size_t fnv_offset = sizeof(size_t) == 8 ? 14695981039346656037ULL : 2166136261UL;
            
            result = fnv_offset;
            
            // Hash the bit count first
            result ^= bs.size();
            result *= fnv_prime;
            
            // Hash the byte data
            for (size_t i = 0; i < bs.sizeBytes(); ++i) {
                result ^= static_cast<size_t>(bs.data()[i]);
                result *= fnv_prime;
            }
            
            return result;
        }
    };
}