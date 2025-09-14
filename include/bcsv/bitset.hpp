#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <iostream>
#include <bit>
#include <vector>
#include <cstring>

// SIMD support detection
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #if defined(__AVX2__)
        #define BCSV_BITSET_HAS_AVX2 1
    #endif
    #if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || _M_IX86_FP >= 2))
        #define BCSV_BITSET_HAS_SSE2 1
    #endif
#endif

namespace bcsv {

/**
 * @brief Custom bitset implementation that inherits from std::array<std::byte, N>
 * Provides std::bitset-compatible interface while giving direct access to underlying bytes
 * 
 * @tparam N Number of bits in the bitset
 */
template<size_t N>
class bitset : public std::array<std::byte, (N + 7) / 8> {
public:
    static constexpr size_t length = N;
    static constexpr size_t byte_count = (N + 7) / 8;
    
    using base_type = std::array<std::byte, byte_count>;
    using base_type::size;   // Inherit size() from std::array (returns byte count)
    using base_type::data;   // Inherit data() from std::array (returns std::byte*)
    
    /**
     * @brief Proxy class for individual bit access
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
    constexpr bitset() noexcept : base_type{} {}
    
    constexpr bitset(unsigned long long val) noexcept : base_type{} {
        set_from_value(val);
    }
    
    template<class CharT, class Traits, class Allocator>
    explicit bitset(const std::basic_string<CharT, Traits, Allocator>& str,
                   typename std::basic_string<CharT, Traits, Allocator>::size_type pos = 0,
                   typename std::basic_string<CharT, Traits, Allocator>::size_type n = 
                       std::basic_string<CharT, Traits, Allocator>::npos,
                   CharT zero = CharT('0'),
                   CharT one = CharT('1')) : base_type{} {
        set_from_string(str, pos, n, zero, one);
    }
    
    // std::bitset compatibility - bit access
    constexpr bool operator[](size_t pos) const {
        if (pos >= N) return false;
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        return (base_type::operator[](byte_idx) & (std::byte{1} << bit_idx)) != std::byte{0};
    }
    
    reference operator[](size_t pos) {
        if (pos >= N) {
            throw std::out_of_range("bitset::operator[]: index out of range");
        }
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        return reference(&base_type::operator[](byte_idx), bit_idx);
    }
    
    constexpr bool test(size_t pos) const {
        if (pos >= N) {
            throw std::out_of_range("bitset::test: index out of range");
        }
        return (*this)[pos];
    }
    
    // std::bitset compatibility - bit operations
    constexpr bitset& set() noexcept {
        if (std::is_constant_evaluated()) {
            // Compile-time version
            for (size_t i = 0; i < byte_count; ++i) {
                base_type::operator[](i) = std::byte{0xFF};
            }
            // Clear unused bits in the last byte
            clear_unused_bits();
            return *this;
        }
        
        // Runtime optimized version
        set_all_optimized();
        return *this;
    }

private:
    void set_all_optimized() noexcept {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(base_type::data());
        
        if constexpr (byte_count >= 8) {
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
            
        } else if constexpr (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            if constexpr (byte_count == 4) {
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
            
        } else if constexpr (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            if constexpr (byte_count == 2) {
                uint16_t chunk = 0xFFFFU;
                std::memcpy(bytes, &chunk, 2);
            } else {
                // 3 bytes: set 16-bit chunk + last byte
                uint16_t chunk = 0xFFFFU;
                std::memcpy(bytes, &chunk, 2);
                bytes[2] = 0xFF;
            }
            
        } else {
            // Single byte
            bytes[0] = 0xFF;
        }
        
        // Clear unused bits in the last byte
        clear_unused_bits();
    }

public:
    
    constexpr bitset& set(size_t pos, bool val = true) {
        if (pos >= N) {
            if (std::is_constant_evaluated()) {
                throw std::out_of_range("bitset::set: index out of range");
            } else {
                throw std::out_of_range("bitset::set: index out of range");
            }
        }
        
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        
        if (val) {
            base_type::operator[](byte_idx) |= std::byte{1} << bit_idx;
        } else {
            base_type::operator[](byte_idx) &= ~(std::byte{1} << bit_idx);
        }
        return *this;
    }
    
    constexpr bitset& reset() noexcept {
        if (std::is_constant_evaluated()) {
            // Compile-time version
            for (size_t i = 0; i < byte_count; ++i) {
                base_type::operator[](i) = std::byte{0};
            }
            return *this;
        }
        
        // Runtime optimized version
        reset_all_optimized();
        return *this;
    }

private:
    void reset_all_optimized() noexcept {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(base_type::data());
        
        if constexpr (byte_count >= 8) {
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
            
        } else if constexpr (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            if constexpr (byte_count == 4) {
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
            
        } else if constexpr (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            if constexpr (byte_count == 2) {
                uint16_t chunk = 0;
                std::memcpy(bytes, &chunk, 2);
            } else {
                // 3 bytes: reset 16-bit chunk + last byte
                uint16_t chunk = 0;
                std::memcpy(bytes, &chunk, 2);
                bytes[2] = 0;
            }
            
        } else {
            // Single byte
            bytes[0] = 0;
        }
    }

public:
    
    constexpr bitset& reset(size_t pos) {
        return set(pos, false);
    }
    
    constexpr bitset& flip() noexcept {
        for (size_t i = 0; i < byte_count; ++i) {
            base_type::operator[](i) = ~base_type::operator[](i);
        }
        clear_unused_bits();
        return *this;
    }
    
    constexpr bitset& flip(size_t pos) {
        if (pos >= N) {
            if (std::is_constant_evaluated()) {
                throw std::out_of_range("bitset::flip: index out of range");
            } else {
                throw std::out_of_range("bitset::flip: index out of range");
            }
        }
        
        const size_t byte_idx = pos / 8;
        const size_t bit_idx = pos % 8;
        base_type::operator[](byte_idx) ^= std::byte{1} << bit_idx;
        return *this;
    }
    
    // std::bitset compatibility - queries
    constexpr size_t count() const noexcept {
        if (std::is_constant_evaluated()) {
            // Compile-time version
            size_t result = 0;
            for (size_t i = 0; i < byte_count; ++i) {
                result += std::popcount(static_cast<uint8_t>(base_type::operator[](i)));
            }
            return result;
        }
        
        // Runtime optimized version
        return count_optimized();
    }

private:
    size_t count_optimized() const noexcept {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(base_type::data());
        
#if defined(BCSV_BITSET_HAS_AVX2)
        if constexpr (byte_count >= 32) {
            return count_avx2(bytes);
        }
#endif
        
#if defined(BCSV_BITSET_HAS_SSE2)
        if constexpr (byte_count >= 16) {
            return count_sse2(bytes);
        }
#endif
        
        // Fallback: process 8 bytes at a time when possible
        if constexpr (byte_count >= 8) {
            return count_64bit(bytes);
        }
        
        // Simple byte-by-byte for small bitsets
        size_t result = 0;
        for (size_t i = 0; i < byte_count; ++i) {
            result += std::popcount(static_cast<uint8_t>(base_type::operator[](i)));
        }
        return result;
    }

#if defined(BCSV_BITSET_HAS_AVX2)
    size_t count_avx2(const uint8_t* bytes) const noexcept {
        size_t total = 0;
        size_t i = 0;
        
        // Process 32 bytes at a time with AVX2
        for (; i + 32 <= byte_count; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bytes + i));
            
            // Use lookup table for popcount (faster than individual popcounts for many bytes)
            const __m256i lookup = _mm256_setr_epi8(
                0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
                0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
            );
            
            __m256i low_mask = _mm256_set1_epi8(0x0F);
            __m256i lo = _mm256_and_si256(chunk, low_mask);
            __m256i hi = _mm256_and_si256(_mm256_srli_epi16(chunk, 4), low_mask);
            
            __m256i popcnt_lo = _mm256_shuffle_epi8(lookup, lo);
            __m256i popcnt_hi = _mm256_shuffle_epi8(lookup, hi);
            __m256i popcnt = _mm256_add_epi8(popcnt_lo, popcnt_hi);
            
            // Horizontal sum of all bytes
            popcnt = _mm256_sad_epu8(popcnt, _mm256_setzero_si256());
            total += _mm256_extract_epi64(popcnt, 0) + _mm256_extract_epi64(popcnt, 1) +
                    _mm256_extract_epi64(popcnt, 2) + _mm256_extract_epi64(popcnt, 3);
        }
        
        // Handle remaining bytes
        for (; i < byte_count; ++i) {
            total += std::popcount(bytes[i]);
        }
        
        return total;
    }
#endif

#if defined(BCSV_BITSET_HAS_SSE2)
    size_t count_sse2(const uint8_t* bytes) const noexcept {
        size_t total = 0;
        size_t i = 0;
        
        // Process 16 bytes at a time with SSE2
        for (; i + 16 <= byte_count; i += 16) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes + i));
            
            // Use lookup table for popcount
            const __m128i lookup = _mm_setr_epi8(
                0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
            );
            
            __m128i low_mask = _mm_set1_epi8(0x0F);
            __m128i lo = _mm_and_si128(chunk, low_mask);
            __m128i hi = _mm_and_si128(_mm_srli_epi16(chunk, 4), low_mask);
            
            __m128i popcnt_lo = _mm_shuffle_epi8(lookup, lo);
            __m128i popcnt_hi = _mm_shuffle_epi8(lookup, hi);
            __m128i popcnt = _mm_add_epi8(popcnt_lo, popcnt_hi);
            
            // Horizontal sum of all bytes
            popcnt = _mm_sad_epu8(popcnt, _mm_setzero_si128());
            total += _mm_extract_epi16(popcnt, 0) + _mm_extract_epi16(popcnt, 4);
        }
        
        // Handle remaining bytes
        for (; i < byte_count; ++i) {
            total += std::popcount(bytes[i]);
        }
        
        return total;
    }
#endif

    size_t count_64bit(const uint8_t* bytes) const noexcept {
        size_t total = 0;
        size_t i = 0;
        
        // Process 8 bytes at a time
        for (; i + 8 <= byte_count; i += 8) {
            uint64_t chunk;
            std::memcpy(&chunk, bytes + i, 8);
            total += std::popcount(chunk);
        }
        
        // Handle remaining bytes
        for (; i < byte_count; ++i) {
            total += std::popcount(bytes[i]);
        }
        
        return total;
    }

public:
    
    constexpr size_t size() const noexcept {
        return N;
    }
    
    constexpr size_t sizeBytes() const noexcept {
        return byte_count;
    }
    
    constexpr bool all() const noexcept {
        if (std::is_constant_evaluated()) {
            // Compile-time fallback
            if constexpr (byte_count == 1) {
                // Special case for single byte
                if constexpr (N % 8 != 0) {
                    const std::byte mask = std::byte{(1 << (N % 8)) - 1};
                    return (base_type::operator[](0) & mask) == mask;
                } else {
                    return base_type::operator[](0) == std::byte{0xFF};
                }
            } else {
                // Check full bytes
                for (size_t i = 0; i < byte_count - 1; ++i) {
                    if (base_type::operator[](i) != std::byte{0xFF}) {
                        return false;
                    }
                }
                // Check last byte (might be partial)
                if constexpr (N % 8 != 0) {
                    const std::byte mask = std::byte{(1 << (N % 8)) - 1};
                    return (base_type::operator[](byte_count - 1) & mask) == mask;
                } else {
                    return base_type::operator[](byte_count - 1) == std::byte{0xFF};
                }
            }
        }
        
        // Runtime optimized version
        return all_optimized();
    }

private:
    bool all_optimized() const noexcept {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(base_type::data());
        
        if constexpr (byte_count >= 8) {
            // Use 64-bit chunks for larger bitsets
            size_t i = 0;
            
            // Process full 8-byte chunks
            for (; i + 8 <= byte_count; i += 8) {
                uint64_t chunk;
                std::memcpy(&chunk, bytes + i, 8);
                if (chunk != 0xFFFFFFFFFFFFFFFFULL) return false;
            }
            
            // Handle remaining bytes (1-7 bytes)
            if (i < byte_count) {
                // Process remaining full bytes
                for (; i < byte_count - 1; ++i) {
                    if (bytes[i] != 0xFF) return false;
                }
                
                // Handle last byte (might be partial)
                if constexpr (N % 8 != 0) {
                    const uint8_t mask = (1 << (N % 8)) - 1;
                    return (bytes[byte_count - 1] & mask) == mask;
                } else {
                    return bytes[byte_count - 1] == 0xFF;
                }
            }
            
            // All full chunks were 0xFF, but we need to check if last byte is partial
            if constexpr (N % 8 != 0) {
                const uint8_t mask = (1 << (N % 8)) - 1;
                return (bytes[byte_count - 1] & mask) == mask;
            }
            
            return true;
            
        } else if constexpr (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            if constexpr (byte_count == 4) {
                uint32_t chunk;
                std::memcpy(&chunk, bytes, 4);
                if constexpr (N % 8 == 0) {
                    return chunk == 0xFFFFFFFFU;
                } else {
                    // Handle partial last byte
                    const uint32_t mask = (1U << N) - 1;
                    return (chunk & mask) == mask;
                }
            } else {
                // 5-7 bytes: check 32-bit chunk + remaining bytes
                uint32_t chunk;
                std::memcpy(&chunk, bytes, 4);
                if (chunk != 0xFFFFFFFFU) return false;
                
                // Check remaining full bytes
                for (size_t i = 4; i < byte_count - 1; ++i) {
                    if (bytes[i] != 0xFF) return false;
                }
                
                // Handle last byte (might be partial)
                if constexpr (N % 8 != 0) {
                    const uint8_t mask = (1 << (N % 8)) - 1;
                    return (bytes[byte_count - 1] & mask) == mask;
                } else {
                    return bytes[byte_count - 1] == 0xFF;
                }
            }
            
        } else if constexpr (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            if constexpr (byte_count == 2) {
                uint16_t chunk;
                std::memcpy(&chunk, bytes, 2);
                if constexpr (N % 8 == 0) {
                    return chunk == 0xFFFFU;
                } else {
                    // Handle partial last byte
                    const uint16_t mask = (1U << N) - 1;
                    return (chunk & mask) == mask;
                }
            } else {
                // 3 bytes: check 16-bit chunk + last byte
                uint16_t chunk;
                std::memcpy(&chunk, bytes, 2);
                if (chunk != 0xFFFFU) return false;
                
                // Handle last byte (might be partial)
                if constexpr (N % 8 != 0) {
                    const uint8_t mask = (1 << (N % 8)) - 1;
                    return (bytes[2] & mask) == mask;
                } else {
                    return bytes[2] == 0xFF;
                }
            }
            
        } else {
            // Single byte
            if constexpr (N % 8 == 0) {
                return bytes[0] == 0xFF;
            } else {
                const uint8_t mask = (1 << N) - 1;
                return (bytes[0] & mask) == mask;
            }
        }
    }

public:
    
    constexpr bool any() const noexcept {
        if (std::is_constant_evaluated()) {
            // Compile-time fallback
            for (size_t i = 0; i < byte_count; ++i) {
                if (base_type::operator[](i) != std::byte{0}) {
                    return true;
                }
            }
            return false;
        }
        
        // Runtime optimized version
        return any_optimized();
    }

private:
    bool any_optimized() const noexcept {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(base_type::data());
        
        if constexpr (byte_count >= 8) {
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
            
        } else if constexpr (byte_count >= 4) {
            // Use 32-bit chunks for medium bitsets
            uint32_t chunk;
            std::memcpy(&chunk, bytes, 4);
            
            if constexpr (byte_count == 4) {
                return chunk != 0;
            } else {
                // 5-7 bytes: check 32-bit chunk + remaining bytes
                if (chunk != 0) return true;
                
                for (size_t i = 4; i < byte_count; ++i) {
                    if (bytes[i] != 0) return true;
                }
                return false;
            }
            
        } else if constexpr (byte_count >= 2) {
            // Use 16-bit chunks for small bitsets
            uint16_t chunk;
            std::memcpy(&chunk, bytes, 2);
            
            if constexpr (byte_count == 2) {
                return chunk != 0;
            } else {
                // 3 bytes: check 16-bit chunk + last byte
                return chunk != 0 || bytes[2] != 0;
            }
            
        } else {
            // Single byte
            return bytes[0] != 0;
        }
    }

public:
    
    constexpr bool none() const noexcept {
        return !any();
    }
    
    // std::bitset compatibility - bitwise operations
    constexpr bitset operator~() const noexcept {
        bitset result;
        for (size_t i = 0; i < byte_count; ++i) {
            result.base_type::operator[](i) = ~base_type::operator[](i);
        }
        result.clear_unused_bits();
        return result;
    }
    
    constexpr bitset& operator&=(const bitset& other) noexcept {
        for (size_t i = 0; i < byte_count; ++i) {
            base_type::operator[](i) &= other.base_type::operator[](i);
        }
        return *this;
    }
    
    constexpr bitset& operator|=(const bitset& other) noexcept {
        for (size_t i = 0; i < byte_count; ++i) {
            base_type::operator[](i) |= other.base_type::operator[](i);
        }
        return *this;
    }
    
    constexpr bitset& operator^=(const bitset& other) noexcept {
        for (size_t i = 0; i < byte_count; ++i) {
            base_type::operator[](i) ^= other.base_type::operator[](i);
        }
        return *this;
    }
    
    bitset operator<<(size_t pos) const noexcept {
        bitset result;
        if (pos >= N) return result; // All bits shifted out
        
        if (pos == 0) return *this;
        
        // Optimize for byte-aligned shifts
        const size_t byte_shift = pos / 8;
        const size_t bit_shift = pos % 8;
        
        if (bit_shift == 0) {
            // Pure byte shift - very fast
            shift_bytes_left(result, byte_shift);
        } else {
            // Mixed byte and bit shift
            shift_mixed_left(result, byte_shift, bit_shift);
        }
        
        result.clear_unused_bits();
        return result;
    }
    
    bitset operator>>(size_t pos) const noexcept {
        bitset result;
        if (pos >= N) return result; // All bits shifted out
        
        if (pos == 0) return *this;
        
        // Optimize for byte-aligned shifts
        const size_t byte_shift = pos / 8;
        const size_t bit_shift = pos % 8;
        
        if (bit_shift == 0) {
            // Pure byte shift - very fast
            shift_bytes_right(result, byte_shift);
        } else {
            // Mixed byte and bit shift
            shift_mixed_right(result, byte_shift, bit_shift);
        }
        
        return result;
    }

private:
    void shift_bytes_left(bitset& result, size_t byte_shift) const noexcept {
        if (byte_shift >= byte_count) return; // All bytes shifted out
        
        // Copy bytes to new positions
        for (size_t i = byte_shift; i < byte_count; ++i) {
            result.base_type::operator[](i) = base_type::operator[](i - byte_shift);
        }
    }
    
    void shift_bytes_right(bitset& result, size_t byte_shift) const noexcept {
        if (byte_shift >= byte_count) return; // All bytes shifted out
        
        // Copy bytes to new positions
        for (size_t i = 0; i < byte_count - byte_shift; ++i) {
            result.base_type::operator[](i) = base_type::operator[](i + byte_shift);
        }
    }
    
    void shift_mixed_left(bitset& result, size_t byte_shift, size_t bit_shift) const noexcept {
        if (byte_shift >= byte_count) return;
        
        const size_t inv_bit_shift = 8 - bit_shift;
        
        // Handle the bulk of bytes with carry
        for (size_t i = byte_shift; i < byte_count; ++i) {
            const size_t src_idx = i - byte_shift;
            
            // Current byte shifted
            result.base_type::operator[](i) = base_type::operator[](src_idx) << bit_shift;
            
            // Add carry from previous byte
            if (src_idx > 0) {
                result.base_type::operator[](i) |= base_type::operator[](src_idx - 1) >> inv_bit_shift;
            }
        }
        
        // Handle the last carry
        if (byte_shift + byte_count - byte_shift < byte_count) {
            const size_t last_src = byte_count - byte_shift - 1;
            const size_t last_dst = byte_count - 1;
            if (last_dst < byte_count && last_src < byte_count) {
                result.base_type::operator[](last_dst) |= 
                    base_type::operator[](last_src) >> inv_bit_shift;
            }
        }
    }
    
    void shift_mixed_right(bitset& result, size_t byte_shift, size_t bit_shift) const noexcept {
        if (byte_shift >= byte_count) return;
        
        const size_t inv_bit_shift = 8 - bit_shift;
        
        // Handle the bulk of bytes with carry
        for (size_t i = 0; i < byte_count - byte_shift; ++i) {
            const size_t src_idx = i + byte_shift;
            
            // Current byte shifted
            result.base_type::operator[](i) = base_type::operator[](src_idx) >> bit_shift;
            
            // Add carry from next byte
            if (src_idx + 1 < byte_count) {
                result.base_type::operator[](i) |= base_type::operator[](src_idx + 1) << inv_bit_shift;
            }
        }
    }

public:
    
    bitset& operator<<=(size_t pos) noexcept {
        *this = *this << pos;
        return *this;
    }
    
    bitset& operator>>=(size_t pos) noexcept {
        *this = *this >> pos;
        return *this;
    }
    
    // std::bitset compatibility - conversion
    unsigned long to_ulong() const {
        // Check if any bits beyond position 31 are set
        if constexpr (N > 32) {
            for (size_t i = 32; i < N; ++i) {
                if ((*this)[i]) {
                    throw std::overflow_error("bitset::to_ulong: value contains set bits beyond position 31");
                }
            }
        }
        
        unsigned long result = 0;
        const size_t bytes_to_copy = std::min(sizeof(unsigned long), byte_count);
        
        for (size_t i = 0; i < bytes_to_copy; ++i) {
            result |= static_cast<unsigned long>(base_type::operator[](i)) << (i * 8);
        }
        
        return result;
    }
    
    unsigned long long to_ullong() const {
        // Check if any bits beyond position 63 are set
        if constexpr (N > 64) {
            for (size_t i = 64; i < N; ++i) {
                if ((*this)[i]) {
                    throw std::overflow_error("bitset::to_ullong: value contains set bits beyond position 63");
                }
            }
        }
        
        unsigned long long result = 0;
        const size_t bytes_to_copy = std::min(sizeof(unsigned long long), byte_count);
        
        for (size_t i = 0; i < bytes_to_copy; ++i) {
            result |= static_cast<unsigned long long>(base_type::operator[](i)) << (i * 8);
        }
        
        return result;
    }
    
    std::string to_string(char zero = '0', char one = '1') const {
        std::string result;
        result.reserve(N);
        
        for (size_t i = N; i > 0; --i) {
            result += (*this)[i - 1] ? one : zero;
        }
        
        return result;
    }
    
    // Additional bcsv-specific methods
    
    /**
     * @brief Get direct access to underlying byte array
     * @return Reference to the underlying std::array<std::byte, byte_count>
     */
    constexpr base_type& asArray() noexcept {
        return static_cast<base_type&>(*this);
    }
    
    /**
     * @brief Get direct access to underlying byte array (const version)
     * @return Const reference to the underlying std::array<std::byte, byte_count>
     */
    constexpr const base_type& asArray() const noexcept {
        return static_cast<const base_type&>(*this);
    }
    
    /**
     * @brief Extract a range of bits as a byte array
     * @tparam StartBit First bit to extract (inclusive)
     * @tparam EndBit Last bit to extract (exclusive)
     * @return Array of bytes containing the extracted bits
     */
    template<size_t StartBit, size_t EndBit>
    constexpr std::array<std::byte, (EndBit - StartBit + 7) / 8> extractBits() const {
        static_assert(StartBit < EndBit, "StartBit must be less than EndBit");
        static_assert(EndBit <= N, "EndBit must not exceed bitset size");
        
        constexpr size_t numBits = EndBit - StartBit;
        constexpr size_t numBytes = (numBits + 7) / 8;
        
        std::array<std::byte, numBytes> result{};
        
        for (size_t bit = StartBit; bit < EndBit; ++bit) {
            if ((*this)[bit]) {
                const size_t relBit = bit - StartBit;
                const size_t byteIdx = relBit / 8;
                const size_t bitInByte = relBit % 8;
                result[byteIdx] |= std::byte{1} << bitInByte;
            }
        }
        
        return result;
    }
    
    /**
     * @brief Extract a range of bits as a byte array (runtime version)
     * @param startBit First bit to extract (inclusive)
     * @param endBit Last bit to extract (exclusive)
     * @return Vector of bytes containing the extracted bits
     */
    std::vector<std::byte> extractBits(size_t startBit, size_t endBit) const {
        if (startBit >= endBit || endBit > N) {
            throw std::invalid_argument("Invalid bit range");
        }
        
        const size_t numBits = endBit - startBit;
        const size_t numBytes = (numBits + 7) / 8;
        
        std::vector<std::byte> result(numBytes, std::byte{0});
        
        for (size_t bit = startBit; bit < endBit; ++bit) {
            if ((*this)[bit]) {
                const size_t relBit = bit - startBit;
                const size_t byteIdx = relBit / 8;
                const size_t bitInByte = relBit % 8;
                result[byteIdx] |= std::byte{1} << bitInByte;
            }
        }
        
        return result;
    }
    
    /**
     * @brief Check if the bitset is empty (all bits are zero)
     * @return true if no bits are set, false otherwise
     */
    constexpr bool empty() const noexcept {
        return none();
    }

private:
    constexpr void clear_unused_bits() noexcept {
        if constexpr (N % 8 != 0) {
            const std::byte mask = std::byte{(1 << (N % 8)) - 1};
            base_type::operator[](byte_count - 1) &= mask;
        }
    }
    
    constexpr void set_from_value(unsigned long long val) noexcept {
        const size_t bytes_to_set = std::min(sizeof(val), byte_count);
        
        for (size_t i = 0; i < bytes_to_set; ++i) {
            base_type::operator[](i) = std::byte(val >> (i * 8));
        }
        
        clear_unused_bits();
    }
    
    template<class CharT, class Traits, class Allocator>
    void set_from_string(const std::basic_string<CharT, Traits, Allocator>& str,
                        typename std::basic_string<CharT, Traits, Allocator>::size_type pos,
                        typename std::basic_string<CharT, Traits, Allocator>::size_type n,
                        CharT zero, CharT one) {
        const auto len = std::min(n, str.length() - pos);
        
        for (size_t i = 0; i < std::min(len, static_cast<decltype(len)>(N)); ++i) {
            const auto ch = str[pos + len - 1 - i];
            if (ch == one) {
                set(i);
            } else if (ch != zero) {
                throw std::invalid_argument("bitset::bitset: invalid character in string");
            }
        }
    }
};

// Non-member operators
template<size_t N>
constexpr bitset<N> operator&(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    bitset<N> result = lhs;
    result &= rhs;
    return result;
}

template<size_t N>
constexpr bitset<N> operator|(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    bitset<N> result = lhs;
    result |= rhs;
    return result;
}

template<size_t N>
constexpr bitset<N> operator^(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    bitset<N> result = lhs;
    result ^= rhs;
    return result;
}

template<size_t N>
constexpr bool operator==(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    return lhs.asArray() == rhs.asArray();
}

template<size_t N>
constexpr bool operator!=(const bitset<N>& lhs, const bitset<N>& rhs) noexcept {
    return !(lhs == rhs);
}

template<class CharT, class Traits, size_t N>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, const bitset<N>& x) {
    return os << x.to_string(CharT('0'), CharT('1'));
}

template<class CharT, class Traits, size_t N>
std::basic_istream<CharT, Traits>& operator>>(std::basic_istream<CharT, Traits>& is, bitset<N>& x) {
    std::basic_string<CharT, Traits> str;
    str.reserve(N);
    
    CharT ch;
    for (size_t i = 0; i < N && is >> ch; ++i) {
        if (ch != CharT('0') && ch != CharT('1')) {
            is.putback(ch);
            break;
        }
        str += ch;
    }
    
    if (!str.empty()) {
        x = bitset<N>(str);
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
            const auto& bytes = bs.asArray();
            
            // Use FNV-1a hash algorithm for byte array
            constexpr size_t fnv_prime = sizeof(size_t) == 8 ? 1099511628211ULL : 16777619UL;
            constexpr size_t fnv_offset = sizeof(size_t) == 8 ? 14695981039346656037ULL : 2166136261UL;
            
            result = fnv_offset;
            for (const auto& byte : bytes) {
                result ^= static_cast<size_t>(byte);
                result *= fnv_prime;
            }
            
            return result;
        }
    };
}