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
#include <cstring>
#include <stdexcept>
#include <span>
#include <iostream>
#include <type_traits>
#include <concepts>
#include <bit> // For std::bit_width
#include <limits> // For std::numeric_limits
#include "byte_buffer.h"
#include "checksum.hpp" // Required for Checksum::Streaming

namespace bcsv {

    // Concept for VLE-supported integer types
    template<typename T>
    concept VLEInteger = std::is_integral_v<T>;

    // Helper: Zigzag encoding for signed integers
    template<typename T>
        requires std::signed_integral<T>
    constexpr auto zigzag_encode(T value) noexcept {
        using U = std::make_unsigned_t<T>;
        constexpr size_t bits = sizeof(T) * 8;
        return static_cast<U>((value << 1) ^ (value >> (bits - 1)));
    }

    // Helper: Zigzag decoding for signed integers
    template<typename T>
        requires std::unsigned_integral<T>
    constexpr auto zigzag_decode(T value) noexcept {
        using S = std::make_signed_t<T>;
        return static_cast<S>((value >> 1) ^ -(value & 1));
    }

    // Helper to calculate VLE limits
    template<typename T, size_t L_BITS>
    struct VLELimits {
        static constexpr size_t LengthBits = L_BITS;
        static constexpr size_t TypeBits = sizeof(T) * 8;
        // If LengthBits == 0 (1 byte type), Capacity is 8.
        // Else Capacity = (2^LengthBits * 8) - LengthBits.
        static constexpr size_t MaxBytes = (1ULL << LengthBits);
        static constexpr size_t CapBits = (MaxBytes * 8) - LengthBits;
        static constexpr bool FullRange = (CapBits >= TypeBits);

        static constexpr T max_value() {
            if constexpr (FullRange) {
                return std::numeric_limits<T>::max();
            } else {
                if constexpr (std::is_signed_v<T>) {
                    // Zigzag max positive comes from (MAX_U - 1)
                    // Max U = 2^CapBits - 1
                    // Max Pos = (Max U - 1) / 2
                    // Note: (1ULL << CapBits) might overflow if CapBits >= 64, but FullRange check prevents this path
                    return static_cast<T>(((1ULL << CapBits) - 2) / 2);
                } else {
                    return static_cast<T>((1ULL << CapBits) - 1);
                }
            }
        }

        static constexpr T min_value() {
            if constexpr (FullRange) {
                return std::numeric_limits<T>::min();
            } else {
                if constexpr (std::is_signed_v<T>) {
                    // Zigzag min negative comes from MAX_U
                    // Min Neg = zigzag_decode(MAX_U) = (MAX_U >> 1) ^ -1
                    uint64_t max_u_shifted = ((1ULL << CapBits) - 1) >> 1;
                    return static_cast<T>(static_cast<int64_t>(max_u_shifted) ^ -1);
                } else {
                    return 0;
                }
            }
        }
    };

    // Traits to determine Length Bits based on Type and Truncated mode
    template<typename T, bool Truncated>
    struct VLETraits {
        static constexpr size_t LengthBits = 
            (sizeof(T) <= 2) ? (Truncated ? 1 : 2) :
            (sizeof(T) <= 4) ? (Truncated ? 2 : 3) :
            (Truncated ? 3 : 4);
            // Default 3/4 bits for 64-bit types
        
        static constexpr size_t MaxEncodedBytes = (sizeof(T) * 8 + LengthBits + 7) / 8;
        static constexpr T VLE_MAX_VALUE = VLELimits<T, LengthBits>::max_value();
        static constexpr T VLE_MIN_VALUE = VLELimits<T, LengthBits>::min_value();

        // Architectural optimization: Determine optimal register size (32 or 64 bit)
        // This ensures better performance on 32-bit MCUs (RISC-V 32, ARM Cortex-M)
        static constexpr size_t BitsRequired = VLELimits<T, LengthBits>::CapBits + LengthBits;
        using PacketType = std::conditional_t<(BitsRequired <= 32), uint32_t, uint64_t>;
        static constexpr bool FitsInRegister = (BitsRequired <= sizeof(PacketType) * 8);
    };
    
    // Explicit specialization for 1-byte types (trivial encoding handled in function, but for consistency)
    template<typename T> struct VLETraits<T, true> {
        static constexpr size_t LengthBits = (sizeof(T) == 1) ? 0 : 
            (sizeof(T) <= 2) ? 1 :
            (sizeof(T) <= 4) ? 2 : 3;
        
        static constexpr size_t MaxEncodedBytes = (sizeof(T) * 8 + LengthBits + 7) / 8;
        static constexpr T VLE_MAX_VALUE = VLELimits<T, LengthBits>::max_value();
        static constexpr T VLE_MIN_VALUE = VLELimits<T, LengthBits>::min_value();

        static constexpr size_t BitsRequired = VLELimits<T, LengthBits>::CapBits + LengthBits;
        using PacketType = std::conditional_t<(BitsRequired <= 32), uint32_t, uint64_t>;
        static constexpr bool FitsInRegister = (BitsRequired <= sizeof(PacketType) * 8);
    };
    
    template<typename T> struct VLETraits<T, false> {
        static constexpr size_t LengthBits = (sizeof(T) == 1) ? 0 : 
            (sizeof(T) <= 2) ? 2 :
            (sizeof(T) <= 4) ? 3 : 4;

        static constexpr size_t MaxEncodedBytes = (sizeof(T) * 8 + LengthBits + 7) / 8;
        static constexpr T VLE_MAX_VALUE = VLELimits<T, LengthBits>::max_value();
        static constexpr T VLE_MIN_VALUE = VLELimits<T, LengthBits>::min_value();

        static constexpr size_t BitsRequired = VLELimits<T, LengthBits>::CapBits + LengthBits;
        using PacketType = std::conditional_t<(BitsRequired <= 32), uint32_t, uint64_t>;
        static constexpr bool FitsInRegister = (BitsRequired <= sizeof(PacketType) * 8);
    };


    /**
    * @brief Encode an integer using block-length encoding (BLE)
    * @tparam Truncated If true, optimizes length bits assuming val fits in sizeof(T) bytes.
    *                   If false, supports full range of T but uses more bits for length.
    * @tparam CheckBounds If true, throws std::overflow_error/length_error on bounds violation.
    */
    template<typename T, bool Truncated = false, bool CheckBounds = true>
    inline size_t vle_encode(const T &value, void* dst, size_t dst_capacity) {
        if constexpr (CheckBounds) {
            if (dst_capacity < 1) [[unlikely]]
                throw std::length_error("Destination buffer too small for VLE encoding (1 byte)");
        }

        if constexpr (sizeof(T) == 1) {
            static_cast<uint8_t*>(dst)[0] = static_cast<uint8_t>(value);
            return 1;
        }

        constexpr size_t LEN_BITS = VLETraits<T, Truncated>::LengthBits;
        constexpr bool FitsInRegister = VLETraits<T, Truncated>::FitsInRegister;
        using PacketType = typename VLETraits<T, Truncated>::PacketType;
        
        using U = std::make_unsigned_t<T>;
        U uval;
         
        if constexpr (std::is_signed_v<T>) {
            uval = zigzag_encode(value);
        } else {
            uval = static_cast<U>(value);
        }

        // Determine number of bytes needed
        // std::bit_width returns the number of bits needed to represent the value
        size_t data_bits = std::bit_width(uval);
        size_t total_bits = data_bits + LEN_BITS;
        
        // Calculate bytes: (total_bits + 7) / 8
        // If uval is 0, data_bits=0, total_bits=LEN_BITS. If LEN_BITS>0, at least 1 byte.
        // Cases:
        // LEN=3. 0->3 bits -> 1 byte. 
        // 31 (11111) -> 5+3=8 bits -> 1 byte.
        // 32 (100000) -> 6+3=9 bits -> 2 bytes.
        size_t numBytes = (total_bits <= 8) ? 1 : ((total_bits + 7) >> 3);

        if constexpr (CheckBounds) {
             constexpr size_t max_bytes = (1ULL << LEN_BITS);
             if (numBytes > max_bytes) [[unlikely]] {
                 throw std::overflow_error("Value too large for VLE encoding configuration");
             }
             if (dst_capacity < numBytes) [[unlikely]] {
                throw std::length_error("Destination buffer too small for VLE encoding");
            }
        }

        if constexpr (FitsInRegister) {
            PacketType packet = (static_cast<PacketType>(uval) << LEN_BITS) | (numBytes - 1);
            
            // Fast path: if capacity allows, write fully into buffer (overwriting potentially tail bytes, which is safe if sequential write)
            if (dst_capacity >= sizeof(PacketType)) {
                 std::memcpy(dst, &packet, sizeof(PacketType));
                 return numBytes;
            }

            // Standard path: Construct packet in register and copy exact bytes
            // Note: Shifts of uval are safe as uval fits in register, and we only read 'numBytes'.
            std::memcpy(dst, &packet, numBytes);
        } else {
            // uint64 full mode (up to 9 bytes)
            // Low part (8 bytes)
            uint64_t packet_low = (static_cast<uint64_t>(uval) << LEN_BITS) | (numBytes - 1);
            
            if (numBytes <= 8) {
                std::memcpy(dst, &packet_low, numBytes);
            } else {
                // Write 8 bytes then the 9th
                std::memcpy(dst, &packet_low, 8);
                // Extract remaining high bits. 
                // We shifted left by LEN_BITS (4). So we lost top 4 bits of uval in packet_low.
                // Recover them from uval.
                // uval >> (64 - LEN_BITS)
                uint8_t high = static_cast<uint8_t>(uval >> (64 - LEN_BITS));
                static_cast<uint8_t*>(dst)[8] = high;
            }
        }

        return numBytes;
    }

     // Appends the bytes to the ByteBuffer increasing its size
    template<typename T, bool Truncated = false, bool CheckBounds = true>
    inline void vle_encode(const T &value, ByteBuffer &bufferToAppend) {
        uint8_t tempBuf[16]; // Increased safety buffer
        size_t written = vle_encode<T, Truncated, CheckBounds>(value, tempBuf, 16);
        
        size_t currentSize = bufferToAppend.size();
        bufferToAppend.resize(currentSize + written);
        std::memcpy(bufferToAppend.data() + currentSize, tempBuf, written);
    }

    // Helper for std::ostream
    template<typename T, bool Truncated = false, bool CheckBounds = true>
    inline size_t vle_encode(const T &value, std::ostream& os) {
        uint8_t tempBuf[16];
        size_t written = vle_encode<T, Truncated, CheckBounds>(value, tempBuf, 16);
        os.write(reinterpret_cast<char*>(tempBuf), written);
        return written;
    }

    // Returns the number of bytes consumed
    template<typename T, bool Truncated = false, bool CheckBounds = true>
    inline size_t vle_decode(T &value, const void* src, size_t src_capacity) {
        if constexpr (CheckBounds) {
            if (src_capacity == 0) [[unlikely]] {
                throw std::runtime_error("Empty buffer in vle_decode");
            }
        } else {
             // If manual bounds checking is disabled, ensure we don't segfault on 0 cap check
             // if user really wants raw performance they ensure src is valid. 
             // But we need at least 1 byte read.
        }

        if constexpr (sizeof(T) == 1) {
            const T* bytes = static_cast<const T*>(src);
            value = bytes[0];
            return 1;
        }
        
        constexpr size_t LEN_BITS = VLETraits<T, Truncated>::LengthBits;
        constexpr size_t LEN_MASK = (1 << LEN_BITS) - 1;
        constexpr bool FitsInRegister = VLETraits<T, Truncated>::FitsInRegister;
        using PacketType = typename VLETraits<T, Truncated>::PacketType;

        const uint8_t* bytes = static_cast<const uint8_t*>(src);
        
        // Read length from first bits
        size_t numBytes = (bytes[0] & LEN_MASK) + 1;
        
        if constexpr (CheckBounds) {
            if (src_capacity < numBytes) [[unlikely]] {
                throw std::runtime_error("Insufficient data for VLE decoding");
            }
        }

        using U = std::make_unsigned_t<T>;
        U uval = 0;

        if constexpr (FitsInRegister) {
            PacketType packet = 0;
            
            // Fast path: over-read full register if buffer allows
            if (src_capacity >= sizeof(PacketType)) {
                 std::memcpy(&packet, bytes, sizeof(PacketType));
                 
                 // Clean up potential garbage in high bytes
                 // Shift left then right clears the top 'unused_bits'.
                 size_t unused_bits = (sizeof(PacketType) - numBytes) * 8;
                 packet = (packet << unused_bits) >> unused_bits;
            } else {
                std::memcpy(&packet, bytes, numBytes);
            }
            
            uval = static_cast<U>(packet >> LEN_BITS);
        } else {
            // uint64 full mode
            // Need up to 9 bytes. 
            // Reuse packet logic for first 8 bytes.
            uint64_t packet = 0;
            size_t low_bytes = (numBytes > 8) ? 8 : numBytes;
            std::memcpy(&packet, bytes, low_bytes);
            
            uval = static_cast<U>(packet >> LEN_BITS);
            
            if (numBytes > 8) {
                // 9th byte. Bits contribute to top.
                uint8_t high = bytes[8];
                // Shift amount: 8*8 - LEN_BITS = 64 - 4 = 60.
                uval |= (static_cast<U>(high) << (64 - LEN_BITS));
            }
        }

        // Assign to T
        if constexpr (std::is_signed_v<T>) {
            value = zigzag_decode(static_cast<std::make_unsigned_t<T>>(uval));
        } else {
            value = static_cast<T>(uval);
        }

        return numBytes;
    }

    // Span gets updated
    template<typename T, bool Truncated = false, bool CheckBounds = true>
    inline T vle_decode(std::span<std::byte> &bufferToRead) {
        T val;
        size_t consumed = vle_decode<T, Truncated, CheckBounds>(val, bufferToRead.data(), bufferToRead.size());
        bufferToRead = bufferToRead.subspan(consumed);
        return val;
    }
    
    // Helper for std::istream
    template<typename T, bool Truncated = false, bool CheckBounds = true>
    inline size_t vle_decode(std::istream& is, T& value, Checksum::Streaming* checksum = nullptr) {
        constexpr size_t MAX_BYTES = VLETraits<T, Truncated>::MaxEncodedBytes;
        // Align buffer for 64-bit register access optimization
        alignas(8) uint8_t buffer[MAX_BYTES];
        
        if (!is.read(reinterpret_cast<char*>(buffer), 1)) {
            throw std::runtime_error("VLE decode: unexpected end of stream");
        }
        
        constexpr size_t LEN_BITS = VLETraits<T, Truncated>::LengthBits;
        constexpr size_t LEN_MASK = (1 << LEN_BITS) - 1;

        size_t numBytes = (buffer[0] & LEN_MASK) + 1;
        
        if constexpr (CheckBounds) {
             if (numBytes > MAX_BYTES) [[unlikely]] {
                 throw std::runtime_error("VLE decode: length invalid (too large)");
             }
        }
        
        if (checksum) {
            checksum->update(reinterpret_cast<char*>(buffer), 1);
        }
        
        if (numBytes > 1) {
            if (!is.read(reinterpret_cast<char*>(buffer) + 1, numBytes - 1)) {
                throw std::runtime_error("VLE decode: unexpected end of stream");
            }
            if(checksum) {
                checksum->update(reinterpret_cast<char*>(buffer) + 1, numBytes - 1); // Checksum only the bytes after the header
            }
        }
        
        // Delegate to pointer-based decode
        T val; 
        vle_decode<T, Truncated>(val, buffer, numBytes);
        value = val;
        
        return numBytes;
    }

} // namespace bcsv
