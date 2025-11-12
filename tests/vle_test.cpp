// Copyright (c) 2025 Tobias Weber
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "bcsv/vle.hpp"
#include <vector>
#include <limits>

using namespace bcsv;

// Test basic encoding of small values (1 byte)
TEST(VLETest, EncodeSmallValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 0
    size_t size = vle_encode(0, buffer);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer[0], 0x00);
    
    // Test 1
    size = vle_encode(1, buffer);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer[0], 0x01);
    
    // Test 127 (max 1-byte value)
    size = vle_encode(127, buffer);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer[0], 0x7F);
}

// Test encoding of 2-byte values
TEST(VLETest, EncodeTwoByteValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 128 (min 2-byte value)
    size_t size = vle_encode(128, buffer);
    EXPECT_EQ(size, 2);
    EXPECT_EQ(buffer[0], 0x80);  // 0b10000000 (continuation bit set)
    EXPECT_EQ(buffer[1], 0x01);  // 0b00000001
    
    // Test 300
    size = vle_encode(300, buffer);
    EXPECT_EQ(size, 2);
    EXPECT_EQ(buffer[0], 0xAC);  // 0b10101100 (44 + 128)
    EXPECT_EQ(buffer[1], 0x02);  // 0b00000010
    
    // Test 16383 (max 2-byte value)
    size = vle_encode(16383, buffer);
    EXPECT_EQ(size, 2);
    EXPECT_EQ(buffer[0], 0xFF);  // 0b11111111
    EXPECT_EQ(buffer[1], 0x7F);  // 0b01111111
}

// Test encoding of 3-byte values
TEST(VLETest, EncodeThreeByteValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 16384 (min 3-byte value)
    size_t size = vle_encode(16384, buffer);
    EXPECT_EQ(size, 3);
    EXPECT_EQ(buffer[0], 0x80);
    EXPECT_EQ(buffer[1], 0x80);
    EXPECT_EQ(buffer[2], 0x01);
    
    // Test 2097151 (max 3-byte value)
    size = vle_encode(2097151, buffer);
    EXPECT_EQ(size, 3);
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xFF);
    EXPECT_EQ(buffer[2], 0x7F);
}

// Test encoding of 4-byte values
TEST(VLETest, EncodeFourByteValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 2097152 (min 4-byte value)
    size_t size = vle_encode(2097152, buffer);
    EXPECT_EQ(size, 4);
    EXPECT_EQ(buffer[0], 0x80);
    EXPECT_EQ(buffer[1], 0x80);
    EXPECT_EQ(buffer[2], 0x80);
    EXPECT_EQ(buffer[3], 0x01);
    
    // Test 268435455 (max 4-byte value)
    size = vle_encode(268435455, buffer);
    EXPECT_EQ(size, 4);
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xFF);
    EXPECT_EQ(buffer[2], 0xFF);
    EXPECT_EQ(buffer[3], 0x7F);
}

// Test encoding of 5-byte values (full uint32_t range)
TEST(VLETest, EncodeFiveByteValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 268435456 (min 5-byte value)
    size_t size = vle_encode(268435456, buffer);
    EXPECT_EQ(size, 5);
    EXPECT_EQ(buffer[0], 0x80);
    EXPECT_EQ(buffer[1], 0x80);
    EXPECT_EQ(buffer[2], 0x80);
    EXPECT_EQ(buffer[3], 0x80);
    EXPECT_EQ(buffer[4], 0x01);
    
    // Test UINT32_MAX (4,294,967,295)
    size = vle_encode(UINT32_MAX, buffer);
    EXPECT_EQ(size, 5);
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xFF);
    EXPECT_EQ(buffer[2], 0xFF);
    EXPECT_EQ(buffer[3], 0xFF);
    EXPECT_EQ(buffer[4], 0x0F);  // Only 4 bits used in last byte
}

// Test encoding of larger values (6-8 bytes, typical for 64-bit row sizes)
TEST(VLETest, EncodeLargeValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 8MB (typical max row size in BCSV)
    size_t row_8mb = 8ULL * 1024 * 1024;
    size_t size = vle_encode(row_8mb, buffer);
    EXPECT_EQ(size, 4);  // 8MB = 8,388,608 fits in 4 bytes
    
    size_t decoded;
    vle_decode(std::span(buffer.data(), size), decoded);
    EXPECT_EQ(decoded, row_8mb);
    
    // Test 1GB (large but realistic memory size)
    size_t size_1gb = 1ULL * 1024 * 1024 * 1024;
    size = vle_encode(size_1gb, buffer);
    EXPECT_EQ(size, 5);
    
    vle_decode(std::span(buffer.data(), size), decoded);
    EXPECT_EQ(decoded, size_1gb);
    
    // Test 1TB (extreme but possible on 64-bit)
    size_t size_1tb = 1ULL * 1024 * 1024 * 1024 * 1024;
    size = vle_encode(size_1tb, buffer);
    EXPECT_EQ(size, 6);  // 1TB = 40 bits -> 6 bytes
    
    vle_decode(std::span(buffer.data(), size), decoded);
    EXPECT_EQ(decoded, size_1tb);
}

// Test encoding of maximum 64-bit value
TEST(VLETest, EncodeMaxUInt64) {
    std::vector<uint8_t> buffer(10);
    
    // Test UINT64_MAX
    size_t size = vle_encode(UINT64_MAX, buffer);
    EXPECT_EQ(size, 10);
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xFF);
    EXPECT_EQ(buffer[2], 0xFF);
    EXPECT_EQ(buffer[3], 0xFF);
    EXPECT_EQ(buffer[4], 0xFF);
    EXPECT_EQ(buffer[5], 0xFF);
    EXPECT_EQ(buffer[6], 0xFF);
    EXPECT_EQ(buffer[7], 0xFF);
    EXPECT_EQ(buffer[8], 0xFF);
    EXPECT_EQ(buffer[9], 0x01);  // Only 1 bit used in last byte
    
    size_t decoded;
    vle_decode(std::span(buffer.data(), size), decoded);
    EXPECT_EQ(decoded, UINT64_MAX);
}

// Test decoding matches encoding
TEST(VLETest, EncodeDecodeRoundTrip) {
    std::vector<uint8_t> buffer(10);
    std::vector<size_t> test_values = {
        0, 1, 127, 128, 300, 16383, 16384, 65535,
        2097151, 2097152, 268435455, 268435456, 
        UINT32_MAX,
        8ULL * 1024 * 1024,  // 8MB
        1ULL * 1024 * 1024 * 1024,  // 1GB
        1ULL * 1024 * 1024 * 1024 * 1024,  // 1TB
        UINT64_MAX
    };
    
    for (size_t original : test_values) {
        size_t encoded_size = vle_encode(original, buffer);
        
        size_t decoded;
        size_t decoded_size = vle_decode(std::span(buffer.data(), encoded_size), decoded);
        
        EXPECT_EQ(decoded, original) << "Failed for value: " << original;
        EXPECT_EQ(decoded_size, encoded_size) << "Size mismatch for value: " << original;
    }
}

// Test vle_encoded_size calculation
TEST(VLETest, EncodedSizeCalculation) {
    EXPECT_EQ(vle_encoded_size(0), 1);
    EXPECT_EQ(vle_encoded_size(127), 1);
    EXPECT_EQ(vle_encoded_size(128), 2);
    EXPECT_EQ(vle_encoded_size(16383), 2);
    EXPECT_EQ(vle_encoded_size(16384), 3);
    EXPECT_EQ(vle_encoded_size(2097151), 3);
    EXPECT_EQ(vle_encoded_size(2097152), 4);
    EXPECT_EQ(vle_encoded_size(268435455), 4);
    EXPECT_EQ(vle_encoded_size(268435456), 5);
    EXPECT_EQ(vle_encoded_size(UINT32_MAX), 5);
    EXPECT_EQ(vle_encoded_size(8ULL * 1024 * 1024), 4);  // 8MB
    EXPECT_EQ(vle_encoded_size(1ULL * 1024 * 1024 * 1024), 5);  // 1GB
    EXPECT_EQ(vle_encoded_size(1ULL * 1024 * 1024 * 1024 * 1024), 6);  // 1TB
    EXPECT_EQ(vle_encoded_size(UINT64_MAX), 10);
}

// Test vle_peek_size
TEST(VLETest, PeekSize) {
    std::vector<uint8_t> buffer(10);
    
    // Test various sizes
    vle_encode(100, buffer);  // 1 byte
    EXPECT_EQ(vle_peek_size(buffer), 1);
    
    vle_encode(1000, buffer);  // 2 bytes
    EXPECT_EQ(vle_peek_size(buffer), 2);
    
    vle_encode(100000, buffer);  // 3 bytes
    EXPECT_EQ(vle_peek_size(buffer), 3);
    
    vle_encode(10000000, buffer);  // 4 bytes
    EXPECT_EQ(vle_peek_size(buffer), 4);
    
    vle_encode(UINT32_MAX, buffer);  // 5 bytes
    EXPECT_EQ(vle_peek_size(buffer), 5);
    
    vle_encode(1ULL * 1024 * 1024 * 1024 * 1024, buffer);  // 1TB = 6 bytes
    EXPECT_EQ(vle_peek_size(buffer), 6);
    
    vle_encode(UINT64_MAX, buffer);  // 10 bytes
    EXPECT_EQ(vle_peek_size(buffer), 10);
}

// Test error handling: buffer too small for encoding
TEST(VLETest, EncodeBufferTooSmall) {
    std::vector<uint8_t> buffer(9);  // Only 9 bytes
    EXPECT_THROW(vle_encode(UINT64_MAX, buffer), std::invalid_argument);
}

// Test error handling: empty decode buffer
TEST(VLETest, DecodeEmptyBuffer) {
    std::vector<uint8_t> buffer;
    size_t value;
    EXPECT_THROW(vle_decode(buffer, value), std::invalid_argument);
}

// Test error handling: empty peek buffer
TEST(VLETest, PeekEmptyBuffer) {
    std::vector<uint8_t> buffer;
    EXPECT_THROW(vle_peek_size(buffer), std::invalid_argument);
}

// Test error handling: invalid encoding (all continuation bits set)
TEST(VLETest, DecodeInvalidEncoding) {
    std::vector<uint8_t> buffer = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // All continuation bits set
    size_t value;
    EXPECT_THROW(vle_decode(buffer, value), std::runtime_error);
}

// Test error handling: incomplete encoding in peek
TEST(VLETest, PeekIncompleteEncoding) {
    std::vector<uint8_t> buffer = {0x80, 0x80};  // Continuation bits set, but incomplete
    EXPECT_THROW(vle_peek_size(buffer), std::runtime_error);
}

// Test streaming decoder with small values
TEST(VLETest, StreamingDecoderSmallValues) {
    std::vector<uint8_t> buffer(10);
    
    // Test 0
    vle_encode(0, buffer);
    VLEDecoder decoder;
    decoder.add_byte(buffer[0]);
    EXPECT_TRUE(decoder.is_complete());
    EXPECT_EQ(decoder.get_value(), 0);
    EXPECT_EQ(decoder.bytes_read(), 1);
    
    // Test 127
    decoder.reset();
    vle_encode(127, buffer);
    decoder.add_byte(buffer[0]);
    EXPECT_TRUE(decoder.is_complete());
    EXPECT_EQ(decoder.get_value(), 127);
}

// Test streaming decoder with multi-byte values
TEST(VLETest, StreamingDecoderMultiByteValues) {
    std::vector<uint8_t> buffer(10);
    VLEDecoder decoder;
    
    // Test 300 (2 bytes)
    size_t size = vle_encode(300, buffer);
    for (size_t i = 0; i < size; ++i) {
        EXPECT_FALSE(decoder.is_complete()) << "Should not be complete at byte " << i;
        decoder.add_byte(buffer[i]);
    }
    EXPECT_TRUE(decoder.is_complete());
    EXPECT_EQ(decoder.get_value(), 300);
    EXPECT_EQ(decoder.bytes_read(), 2);
    
    // Test 8MB (4 bytes)
    decoder.reset();
    size_t value_8mb = 8ULL * 1024 * 1024;
    size = vle_encode(value_8mb, buffer);
    for (size_t i = 0; i < size; ++i) {
        decoder.add_byte(buffer[i]);
    }
    EXPECT_TRUE(decoder.is_complete());
    EXPECT_EQ(decoder.get_value(), value_8mb);
    EXPECT_EQ(decoder.bytes_read(), 4);
}

// Test streaming decoder with maximum value
TEST(VLETest, StreamingDecoderMaxValue) {
    std::vector<uint8_t> buffer(10);
    VLEDecoder decoder;
    
    size_t size = vle_encode(UINT64_MAX, buffer);
    EXPECT_EQ(size, 10);
    
    for (size_t i = 0; i < size; ++i) {
        EXPECT_FALSE(decoder.is_complete());
        decoder.add_byte(buffer[i]);
    }
    
    EXPECT_TRUE(decoder.is_complete());
    EXPECT_EQ(decoder.get_value(), UINT64_MAX);
    EXPECT_EQ(decoder.bytes_read(), 10);
}

// Test streaming decoder error handling
TEST(VLETest, StreamingDecoderErrors) {
    VLEDecoder decoder;
    
    // Test calling add_byte after completion
    decoder.add_byte(0x42);  // Single byte value
    EXPECT_TRUE(decoder.is_complete());
    EXPECT_THROW(decoder.add_byte(0x00), std::runtime_error);
    
    // Test getting value before completion
    decoder.reset();
    decoder.add_byte(0x80);  // Continuation bit set
    EXPECT_FALSE(decoder.is_complete());
    EXPECT_THROW(decoder.get_value(), std::runtime_error);
    
    // Test exceeding 10 bytes
    decoder.reset();
    for (int i = 0; i < 10; ++i) {
        decoder.add_byte(0xFF);  // All continuation bits
    }
    EXPECT_THROW(decoder.add_byte(0xFF), std::runtime_error);
}

// Test streaming decoder with BCSV use case (simulating reading from stream)
TEST(VLETest, StreamingDecoderBCSVUseCase) {
    // Simulate a stream with multiple VLE-encoded row lengths
    std::vector<uint8_t> stream;
    std::vector<size_t> row_lengths = {0, 150, 8192, 8ULL * 1024 * 1024};
    std::vector<uint8_t> temp(10);
    
    // Encode all row lengths with offset (+1) into stream
    for (size_t len : row_lengths) {
        size_t encoded_size = vle_encode(len + 1, temp);
        stream.insert(stream.end(), temp.begin(), temp.begin() + encoded_size);
    }
    
    // Decode from stream byte-by-byte
    size_t stream_pos = 0;
    VLEDecoder decoder;
    std::vector<size_t> decoded_lengths;
    
    while (stream_pos < stream.size()) {
        decoder.add_byte(stream[stream_pos++]);
        
        if (decoder.is_complete()) {
            size_t encoded_value = decoder.get_value();
            size_t actual_length = encoded_value - 1;  // Subtract offset
            decoded_lengths.push_back(actual_length);
            decoder.reset();
        }
    }
    
    EXPECT_EQ(decoded_lengths, row_lengths);
}

// Test BCSV-specific use case: encoding with offset
TEST(VLETest, BCSVRowLengthWithOffset) {
    std::vector<uint8_t> buffer(10);
    
    // Writer encodes: VLE(rowLength + 1)
    // - rowLength=0 (ZoH) -> VLE(1)
    // - rowLength=actual -> VLE(actual+1)
    
    // Test ZoH marker (rowLength=0)
    size_t row_length = 0;
    size_t size = vle_encode(row_length + 1, buffer);  // VLE(1)
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer[0], 0x01);
    
    size_t decoded;
    vle_decode(std::span(buffer.data(), size), decoded);
    EXPECT_EQ(decoded, 1);
    size_t actual_length = decoded - 1;  // Reader subtracts 1
    EXPECT_EQ(actual_length, 0);  // ZoH
    
    // Test actual row length (e.g., 150 bytes)
    row_length = 150;
    size = vle_encode(row_length + 1, buffer);  // VLE(151)
    vle_decode(std::span(buffer.data(), size), decoded);
    actual_length = decoded - 1;
    EXPECT_EQ(actual_length, 150);
    
    // Test large row length (8MB)
    row_length = 8ULL * 1024 * 1024;
    size = vle_encode(row_length + 1, buffer);
    vle_decode(std::span(buffer.data(), size), decoded);
    actual_length = decoded - 1;
    EXPECT_EQ(actual_length, 8ULL * 1024 * 1024);
}

// Test edge case: max BCSV row length with offset
TEST(VLETest, BCSVMaxRowLengthWithOffset) {
    std::vector<uint8_t> buffer(10);
    
    // BCSV max row length ~= UINT64_MAX - 1 (accounting for offset)
    // VLE(UINT64_MAX) = rowLength + 1
    // Therefore max rowLength = UINT64_MAX - 1
    
    size_t max_row_length = UINT64_MAX - 1;
    size_t size = vle_encode(max_row_length + 1, buffer);  // VLE(UINT64_MAX)
    EXPECT_EQ(size, 10);
    
    size_t decoded;
    vle_decode(std::span(buffer.data(), size), decoded);
    size_t actual_length = decoded - 1;
    EXPECT_EQ(actual_length, max_row_length);
}

// Performance test: encode/decode speed
TEST(VLETest, Performance) {
    constexpr size_t ITERATIONS = 1000000;
    std::vector<uint8_t> buffer(10);
    
    // Test encoding performance
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        vle_encode(i % 1000000, buffer);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto encode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    // Test decoding performance (span-based)
    vle_encode(123456, buffer);
    start = std::chrono::high_resolution_clock::now();
    size_t value;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        vle_decode(buffer, value);
    }
    end = std::chrono::high_resolution_clock::now();
    auto decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    // Test streaming decoder performance
    vle_encode(123456, buffer);
    size_t encoded_size = vle_encoded_size(123456);
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        VLEDecoder decoder;
        for (size_t j = 0; j < encoded_size; ++j) {
            decoder.add_byte(buffer[j]);
        }
        value = decoder.get_value();
    }
    end = std::chrono::high_resolution_clock::now();
    auto stream_decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << "VLE Performance (1M iterations):\n";
    std::cout << "  Encode: " << (encode_ns / ITERATIONS) << " ns/op\n";
    std::cout << "  Decode (span): " << (decode_ns / ITERATIONS) << " ns/op\n";
    std::cout << "  Decode (stream): " << (stream_decode_ns / ITERATIONS) << " ns/op\n";
    
    // Expect reasonable performance (< 100ns per operation on modern CPU)
    EXPECT_LT(encode_ns / ITERATIONS, 100);
    EXPECT_LT(decode_ns / ITERATIONS, 100);
    EXPECT_LT(stream_decode_ns / ITERATIONS, 200);  // Streaming has overhead
}
