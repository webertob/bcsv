// Copyright (c) 2025 Tobias Weber
// SPDX-License-Identifier: MIT

#ifndef BCSV_VLE_HPP
#define BCSV_VLE_HPP

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <concepts>
#include <vector>
#include "checksum.hpp"

namespace bcsv {

/**
 * @brief Variable Length Encoding (VLE) utilities using LEB128-style encoding.
 * 
 * This is a generic VLE implementation supporting uint8_t to uint128_t and int8_t to int128_t.
 * 
 * Encoding format:
 * - Each byte stores 7 bits of data
 * - MSB (bit 7) = continuation bit: 1 = more bytes follow, 0 = last byte
 * - LSB (bits 0-6) = data bits (little-endian)
 * 
 * Signed integers use zigzag encoding: (n << 1) ^ (n >> (bits-1))
 * - Maps signed values to unsigned: 0, -1, 1, -2, 2, -3, 3, ...
 * - Becomes: 0, 1, 2, 3, 4, 5, 6, ...
 * 
 * Optimizations:
 * - uint8_t/int8_t: Direct read/write (no VLE overhead)
 * - Shift limit calculated from sizeof(T) to prevent overflow
 * - Type-specific max bytes: uint16_t=3, uint32_t=5, uint64_t=10, uint128_t=19
 */

// Concept for supported integer types
template<typename T>
concept VLEInteger = std::is_integral_v<T> && 
                     (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || 
                      sizeof(T) == 8 || sizeof(T) == 16);

// Helper: Calculate maximum bytes needed for type
template<typename T>
consteval size_t vle_max_bytes() {
    constexpr size_t bits = sizeof(T) * 8;
    return (bits + 6) / 7;  // Ceiling division
}

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

/**
 * @brief Decode a VLE-encoded value from an input stream.
 * 
 * Specialization for 1-byte types (uint8_t/int8_t): trivial read, no VLE overhead.
 * 
 * @tparam T Integer type to decode (uint8_t through uint128_t, int8_t through int128_t)
 * @param input Input stream to read bytes from
 * @return Decoded value
 * @throws std::runtime_error if invalid encoding or stream error
 */
template<VLEInteger T>
inline T vle_decode(std::istream& input, Checksum::Streaming *hasher = 0) {
    if (!input) {
        throw std::runtime_error("VLE decode: input stream not valid");
    }

    using U = std::make_unsigned_t<T>;
    
    // Optimization: 1-byte types - trivial read
    if constexpr (sizeof(T) == 1) {
        uint8_t byte;
        input.read(reinterpret_cast<char*>(&byte), 1);
        if(hasher) hasher->update(&byte, 1);
        if (!input) {
            throw std::runtime_error("VLE decode: unexpected end of stream");
        }
        if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(static_cast<int8_t>(byte));
        } else {
            return static_cast<T>(byte);
        }
    } else {
        // General VLE decoding for multi-byte types
        constexpr size_t max_bits = sizeof(T) * 8;
        
        U value = 0;
        size_t shift = 0;
        uint8_t byte;
        
        do {
            if (shift >= max_bits) {
                throw std::runtime_error("VLE decode: value exceeds type range");
            }
            
            input.read(reinterpret_cast<char*>(&byte), 1);
            if(hasher) hasher->update(&byte, 1);
            if (!input) {
                throw std::runtime_error("VLE decode: unexpected end of stream");
            }
            
            // Extract data bits (lower 7 bits)
            U data_bits = byte & 0x7F;
            value |= (data_bits << shift);
            shift += 7;
            
        } while (byte & 0x80);
        
        // For signed types, apply zigzag decoding
        if constexpr (std::is_signed_v<T>) {
            return zigzag_decode(value);
        } else {
            return static_cast<T>(value);
        }
    }
}

/**
 * @brief Decode a VLE-encoded value from a span buffer.
 * 
 * Specialization for 1-byte types (uint8_t/int8_t): trivial read, no VLE overhead.
 * 
 * @tparam T Integer type to decode (uint8_t through uint128_t, int8_t through int128_t)
 * @param input Input span containing VLE-encoded data
 * @param value Output parameter to store decoded value
 * @return Number of bytes consumed from the input span
 * @throws std::runtime_error if invalid encoding, buffer too small, or value exceeds type range
 */
template<VLEInteger T>
inline size_t vle_decode(std::span<const uint8_t> input, T& value) {
    if (input.empty()) {
        throw std::runtime_error("VLE decode: input buffer is empty");
    }

    using U = std::make_unsigned_t<T>;
    
    // Optimization: 1-byte types - trivial read
    if constexpr (sizeof(T) == 1) {
        if constexpr (std::is_signed_v<T>) {
            value = static_cast<T>(static_cast<int8_t>(input[0]));
        } else {
            value = static_cast<T>(input[0]);
        }
        return 1;
    } else {
        // General VLE decoding for multi-byte types
        constexpr size_t max_bits = sizeof(T) * 8;
        constexpr size_t max_bytes = vle_max_bytes<T>();
        
        U decoded = 0;
        size_t shift = 0;
        size_t bytes_read = 0;
        
        for (size_t i = 0; i < input.size() && i < max_bytes; ++i) {
            if (shift >= max_bits) {
                throw std::runtime_error("VLE decode: value exceeds type range");
            }
            
            uint8_t byte = input[i];
            bytes_read++;
            
            // Extract data bits (lower 7 bits)
            U data_bits = byte & 0x7F;
            decoded |= (data_bits << shift);
            shift += 7;
            
            // Check if this is the last byte (continuation bit clear)
            if ((byte & 0x80) == 0) {
                // For signed types, apply zigzag decoding
                if constexpr (std::is_signed_v<T>) {
                    value = zigzag_decode(decoded);
                } else {
                    value = static_cast<T>(decoded);
                }
                return bytes_read;
            }
        }
        
        // If we reach here, either buffer too small or invalid encoding
        if (input.size() < max_bytes) {
            throw std::runtime_error("VLE decode: incomplete encoding in buffer");
        }
        
        throw std::runtime_error("VLE decode: invalid encoding (exceeds maximum bytes)");
    }
}

/**
 * @brief Encode a value into VLE format and write to output stream.
 * 
 * Specialization for 1-byte types (uint8_t/int8_t): trivial write, no VLE overhead.
 * 
 * @tparam T Integer type to encode
 * @param value The value to encode
 * @param output Output stream to write to
 * @return Number of bytes written (1 to vle_max_bytes<T>())
 * @throws std::runtime_error if output stream error
 */
template<VLEInteger T>
inline size_t vle_encode(T value, std::ostream& output) {
    if (!output) {
        throw std::runtime_error("VLE encode: output stream not valid");
    }

    using U = std::make_unsigned_t<T>;
    
    // Optimization: 1-byte types - trivial write
    if constexpr (sizeof(T) == 1) {
        uint8_t byte = static_cast<uint8_t>(value);
        output.put(static_cast<char>(byte));
        if (!output) {
            throw std::runtime_error("VLE encode: stream write failed");
        }
        return 1;
    } else {
        // For signed types, apply zigzag encoding first
        U encoded_value;
        if constexpr (std::is_signed_v<T>) {
            encoded_value = zigzag_encode(value);
        } else {
            encoded_value = static_cast<U>(value);
        }
        
        // VLE encoding
        size_t bytes_written = 0;
        do {
            uint8_t byte = encoded_value & 0x7F;  // Take lower 7 bits
            encoded_value >>= 7;                   // Shift to next 7 bits
            
            if (encoded_value != 0) {
                byte |= 0x80;  // Set continuation bit
            }
            
            output.put(static_cast<char>(byte));
            if (!output) {
                throw std::runtime_error("VLE encode: stream write failed");
            }
            bytes_written++;
            
        } while (encoded_value != 0);
        
        return bytes_written;
    }
}

/**
 * @brief Encode a value into VLE format and write to span buffer.
 * 
 * Specialization for 1-byte types (uint8_t/int8_t): trivial write, no VLE overhead.
 * 
 * @tparam T Integer type to encode
 * @param value The value to encode
 * @param output Output span buffer to write encoded bytes
 * @return Number of bytes written (1 to vle_max_bytes<T>())
 * @throws std::runtime_error if output buffer is too small
 */
template<VLEInteger T>
inline size_t vle_encode(T value, std::span<uint8_t> output) {
    constexpr size_t max_bytes = vle_max_bytes<T>();
    
    if (output.size() < max_bytes) {
        throw std::runtime_error("VLE encode: output buffer too small");
    }

    using U = std::make_unsigned_t<T>;
    
    // Optimization: 1-byte types - trivial write
    if constexpr (sizeof(T) == 1) {
        output[0] = static_cast<uint8_t>(value);
        return 1;
    } else {
        // For signed types, apply zigzag encoding first
        U encoded_value;
        if constexpr (std::is_signed_v<T>) {
            encoded_value = zigzag_encode(value);
        } else {
            encoded_value = static_cast<U>(value);
        }
        
        // VLE encoding
        size_t bytes_written = 0;
        do {
            uint8_t byte = encoded_value & 0x7F;  // Take lower 7 bits
            encoded_value >>= 7;                   // Shift to next 7 bits
            
            if (encoded_value != 0) {
                byte |= 0x80;  // Set continuation bit
            }
            
            output[bytes_written++] = byte;
            
        } while (encoded_value != 0);
        
        return bytes_written;
    }
}

template<VLEInteger T>
std::vector<uint8_t> vle_encode(T value)
{
    if constexpr (sizeof(T) == 1) {
        std::vector<uint8_t> vleBytes = {static_cast<uint8_t>(value)};
        return vleBytes;
    }

    constexpr size_t max_bytes = vle_max_bytes<T>();
    std::vector<uint8_t> vleBytes;
    vleBytes.reserve(max_bytes);

    using U = std::make_unsigned_t<T>;
    // For signed types, apply zigzag encoding first
    U encoded_value;
    if constexpr (std::is_signed_v<T>) {
        encoded_value = zigzag_encode(value);
    } else {
        encoded_value = static_cast<U>(value);
    }

    do {
        uint8_t byte = encoded_value & 0x7F;
        encoded_value >>= 7;
        if (encoded_value != 0) {
            byte |= 0x80; // More bytes to come
        }
        vleBytes.push_back(byte);
    } while (encoded_value != 0);
    return vleBytes;
}

// Convenience aliases for common types
inline uint64_t vle_decode_u64(std::istream& input) { return vle_decode<uint64_t>(input); }
inline int64_t vle_decode_i64(std::istream& input) { return vle_decode<int64_t>(input); }
inline size_t vle_decode_size(std::istream& input) { return vle_decode<size_t>(input); }

inline size_t vle_encode_u64(uint64_t value, std::ostream& output) { return vle_encode(value, output); }
inline size_t vle_encode_i64(int64_t value, std::ostream& output) { return vle_encode(value, output); }
inline size_t vle_encode_size(size_t value, std::ostream& output) { return vle_encode(value, output); }

} // namespace bcsv

#endif // BCSV_VLE_HPP
