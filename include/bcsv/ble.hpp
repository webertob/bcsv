#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <span>
#include <cstring>

namespace bcsv {

    /**
    * @brief Encode an unsigned integer using block-length encoding (BLE)
    * @details Uses 1-8 bytes where the first 3 bits encode the byte count (0-7 means 1-8 bytes)
    * @param value The unsigned integer to encode (max 2^61 - 1)
    * @return std::array<uint8_t, 8> containing the encoded bytes and actual size
    */
    inline std::pair<std::array<uint8_t, 8>, size_t> ble_encode(uint64_t value) {
        std::array<uint8_t, 8> buffer;
        size_t numBytes;
        uint64_t encoded;
        
        // Determine bytes needed and encode in one step
        // Max values: 1B=2^5, 2B=2^13, 3B=2^21, 4B=2^29, 5B=2^37, 6B=2^45, 7B=2^53, 8B=2^61
        if (value < (1ULL << 5)) {          // 32
            numBytes = 1;
            encoded = (value << 3) | 0;
        } else if (value < (1ULL << 13)) {  // 8,192
            numBytes = 2;
            encoded = (value << 3) | 1;
        } else if (value < (1ULL << 21)) {  // 2,097,152
            numBytes = 3;
            encoded = (value << 3) | 2;
        } else if (value < (1ULL << 29)) {  // 536,870,912
            numBytes = 4;
            encoded = (value << 3) | 3;
        } else if (value < (1ULL << 37)) {  // 137,438,953,472
            numBytes = 5;
            encoded = (value << 3) | 4;
        } else if (value < (1ULL << 45)) {  // 35,184,372,088,832
            numBytes = 6;
            encoded = (value << 3) | 5;
        } else if (value < (1ULL << 53)) {  // 9,007,199,254,740,992
            numBytes = 7;
            encoded = (value << 3) | 6;
        } else if (value < (1ULL << 61)) {  // 2,305,843,009,213,693,952
            numBytes = 8;
            encoded = (value << 3) | 7;
        } else {
            throw std::overflow_error("Value too large for BLE encoding (max 2^61 - 1)");
        }
        
        // Single memcpy - compiler optimizes this to efficient register operations
        // Most architectures will use a single mov instruction for small sizes
        std::memcpy(buffer.data(), &encoded, numBytes);
        
        return {buffer, numBytes};
    }

    /**
    * @brief Decode a block-length encoded unsigned integer
    * @param data Pointer to the encoded data
    * @param bytesRead Output parameter for number of bytes consumed
    * @return The decoded unsigned integer value
    */
    inline uint64_t ble_decode(const uint8_t* data, size_t& bytesRead) {
        // Read length from first 3 bits
        uint8_t lengthBits = data[0] & 0x07;
        bytesRead = lengthBits + 1;
        
        // Single memcpy into 64-bit register, padding with zeros
        uint64_t encoded = 0;
        std::memcpy(&encoded, data, bytesRead);
        
        // Extract value by shifting right 3 bits
        return encoded >> 3;
    }

    /**
    * @brief Decode a block-length encoded unsigned integer from a span
    * @param data Span of encoded data
    * @return Pair of decoded value and number of bytes consumed
    */
    inline std::pair<uint64_t, size_t> ble_decode(std::span<const uint8_t> data) {
        if (data.empty()) {
            throw std::invalid_argument("Empty data span in ble_decode");
        }
        
        size_t bytesRead = 0;
        uint64_t value = ble_decode(data.data(), bytesRead);
        
        if (bytesRead > data.size()) {
            throw std::runtime_error("Insufficient data for BLE decoding");
        }
        
        return {value, bytesRead};
    }
} // namespace bcsv