// Copyright (c) 2025 Tobias Weber
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "bcsv/vle.hpp"
#include <sstream>
#include <limits>

using namespace bcsv;

// Test uint8_t trivial encoding (no VLE overhead)
TEST(VLETemplateTest, Uint8Trivial) {
    std::stringstream ss;
    
    EXPECT_EQ(vle_encode<uint8_t>(0, ss), 1);
    EXPECT_EQ(vle_encode<uint8_t>(127, ss), 1);
    EXPECT_EQ(vle_encode<uint8_t>(255, ss), 1);
    
    ss.seekg(0);
    EXPECT_EQ(vle_decode<uint8_t>(ss), 0);
    EXPECT_EQ(vle_decode<uint8_t>(ss), 127);
    EXPECT_EQ(vle_decode<uint8_t>(ss), 255);
}

// Test int8_t trivial encoding with zigzag
TEST(VLETemplateTest, Int8Trivial) {
    std::stringstream ss;
    
    EXPECT_EQ(vle_encode<int8_t>(0, ss), 1);
    EXPECT_EQ(vle_encode<int8_t>(-1, ss), 1);
    EXPECT_EQ(vle_encode<int8_t>(127, ss), 1);
    EXPECT_EQ(vle_encode<int8_t>(-128, ss), 1);
    
    ss.seekg(0);
    EXPECT_EQ(vle_decode<int8_t>(ss), 0);
    EXPECT_EQ(vle_decode<int8_t>(ss), -1);
    EXPECT_EQ(vle_decode<int8_t>(ss), 127);
    EXPECT_EQ(vle_decode<int8_t>(ss), -128);
}

// Test uint16_t encoding
TEST(VLETemplateTest, Uint16) {
    std::stringstream ss;
    
    // 1 byte: 0-127
    vle_encode<uint16_t>(0, ss);
    vle_encode<uint16_t>(127, ss);
    
    // 2 bytes: 128-16383
    vle_encode<uint16_t>(128, ss);
    vle_encode<uint16_t>(16383, ss);
    
    // 3 bytes: 16384-65535
    vle_encode<uint16_t>(16384, ss);
    vle_encode<uint16_t>(65535, ss);
    
    ss.seekg(0);
    EXPECT_EQ(vle_decode<uint16_t>(ss), 0);
    EXPECT_EQ(vle_decode<uint16_t>(ss), 127);
    EXPECT_EQ(vle_decode<uint16_t>(ss), 128);
    EXPECT_EQ(vle_decode<uint16_t>(ss), 16383);
    EXPECT_EQ(vle_decode<uint16_t>(ss), 16384);
    EXPECT_EQ(vle_decode<uint16_t>(ss), 65535);
}

// Test int16_t with negative values
TEST(VLETemplateTest, Int16Negative) {
    std::stringstream ss;
    
    vle_encode<int16_t>(0, ss);
    vle_encode<int16_t>(-1, ss);
    vle_encode<int16_t>(1, ss);
    vle_encode<int16_t>(-100, ss);
    vle_encode<int16_t>(100, ss);
    vle_encode<int16_t>(-32768, ss);
    vle_encode<int16_t>(32767, ss);
    
    ss.seekg(0);
    EXPECT_EQ(vle_decode<int16_t>(ss), 0);
    EXPECT_EQ(vle_decode<int16_t>(ss), -1);
    EXPECT_EQ(vle_decode<int16_t>(ss), 1);
    EXPECT_EQ(vle_decode<int16_t>(ss), -100);
    EXPECT_EQ(vle_decode<int16_t>(ss), 100);
    EXPECT_EQ(vle_decode<int16_t>(ss), -32768);
    EXPECT_EQ(vle_decode<int16_t>(ss), 32767);
}

// Test uint32_t
TEST(VLETemplateTest, Uint32) {
    std::stringstream ss;
    
    std::vector<uint32_t> values = {
        0, 127, 128, 16383, 16384,
        2097151, 2097152, 268435455, 268435456,
        UINT32_MAX
    };
    
    for (auto val : values) {
        vle_encode<uint32_t>(val, ss);
    }
    
    ss.seekg(0);
    for (auto expected : values) {
        EXPECT_EQ(vle_decode<uint32_t>(ss), expected);
    }
}

// Test uint64_t (large values)
TEST(VLETemplateTest, Uint64Large) {
    std::stringstream ss;
    
    std::vector<uint64_t> values = {
        0,
        127,
        UINT32_MAX,
        8ULL * 1024 * 1024,  // 8MB
        1ULL * 1024 * 1024 * 1024,  // 1GB
        1ULL * 1024 * 1024 * 1024 * 1024,  // 1TB
        UINT64_MAX
    };
    
    for (auto val : values) {
        vle_encode<uint64_t>(val, ss);
    }
    
    ss.seekg(0);
    for (auto expected : values) {
        EXPECT_EQ(vle_decode<uint64_t>(ss), expected);
    }
}

// Test int64_t with negative values
TEST(VLETemplateTest, Int64Negative) {
    std::stringstream ss;
    
    std::vector<int64_t> values = {
        0, -1, 1, -100, 100,
        -1000000, 1000000,
        INT64_MIN, INT64_MAX
    };
    
    for (auto val : values) {
        vle_encode<int64_t>(val, ss);
    }
    
    ss.seekg(0);
    for (auto expected : values) {
        EXPECT_EQ(vle_decode<int64_t>(ss), expected);
    }
}

// Test zigzag encoding correctness
TEST(VLETemplateTest, ZigzagEncoding) {
    // Zigzag should map: 0, -1, 1, -2, 2, -3, 3, ...
    //               to:   0,  1, 2,  3, 4,  5, 6, ...
    
    EXPECT_EQ(zigzag_encode<int32_t>(0), 0);
    EXPECT_EQ(zigzag_encode<int32_t>(-1), 1);
    EXPECT_EQ(zigzag_encode<int32_t>(1), 2);
    EXPECT_EQ(zigzag_encode<int32_t>(-2), 3);
    EXPECT_EQ(zigzag_encode<int32_t>(2), 4);
    
    // Round-trip
    for (int32_t i = -1000; i < 1000; ++i) {
        auto encoded = zigzag_encode(i);
        auto decoded = zigzag_decode(encoded);
        EXPECT_EQ(decoded, i) << "Failed for value: " << i;
    }
}

// Test convenience aliases
TEST(VLETemplateTest, ConvenienceAliases) {
    std::stringstream ss;
    
    vle_encode_u64(12345, ss);
    vle_encode_i64(-6789, ss);
    vle_encode_size(99999, ss);
    
    ss.seekg(0);
    EXPECT_EQ(vle_decode_u64(ss), 12345);
    EXPECT_EQ(vle_decode_i64(ss), -6789);
    EXPECT_EQ(vle_decode_size(ss), 99999);
}

// Test error handling
TEST(VLETemplateTest, ErrorHandling) {
    std::stringstream ss;
    
    // Empty stream
    EXPECT_THROW(vle_decode<uint32_t>(ss), std::runtime_error);
    
    // Invalid stream
    ss.setstate(std::ios::badbit);
    EXPECT_THROW(vle_encode<uint32_t>(123, ss), std::runtime_error);
}

// Test max bytes calculation
TEST(VLETemplateTest, MaxBytesCalculation) {
    EXPECT_EQ(vle_max_bytes<uint8_t>(), 2);   // 8 bits → 2 bytes
    EXPECT_EQ(vle_max_bytes<uint16_t>(), 3);  // 16 bits → 3 bytes
    EXPECT_EQ(vle_max_bytes<uint32_t>(), 5);  // 32 bits → 5 bytes
    EXPECT_EQ(vle_max_bytes<uint64_t>(), 10); // 64 bits → 10 bytes
}

// Performance test
TEST(VLETemplateTest, Performance) {
    constexpr size_t ITERATIONS = 100000;
    std::stringstream ss;
    
    // Encode performance
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        vle_encode<uint64_t>(i % 1000000, ss);
    }
    auto end = std::chrono::steady_clock::now();
    auto encode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    // Decode performance
    ss.seekg(0);
    start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        [[maybe_unused]] auto val = vle_decode<uint64_t>(ss);
    }
    end = std::chrono::steady_clock::now();
    auto decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << "VLE Template Performance (" << ITERATIONS << " iterations):\n";
    std::cout << "  Encode: " << (encode_ns / ITERATIONS) << " ns/op\n";
    std::cout << "  Decode: " << (decode_ns / ITERATIONS) << " ns/op\n";
    
    // Reasonable performance expected
    EXPECT_LT(encode_ns / ITERATIONS, 500);
    EXPECT_LT(decode_ns / ITERATIONS, 500);
}

// Test BCSV use case with offset (size_t for row lengths)
TEST(VLETemplateTest, BCSVRowLengthWithOffset) {
    std::stringstream ss;
    
    // Writer encodes: VLE(rowLength + 1)
    std::vector<size_t> row_lengths = {0, 150, 8192, 8ULL * 1024 * 1024};
    
    for (auto len : row_lengths) {
        vle_encode<size_t>(len + 1, ss);  // +1 offset
    }
    
    // Reader decodes and subtracts 1
    ss.seekg(0);
    for (auto expected : row_lengths) {
        size_t decoded = vle_decode<size_t>(ss);
        EXPECT_EQ(decoded - 1, expected);  // -1 offset
    }
}

// Test span-based encode
TEST(VLETemplateTest, SpanEncodeBasic) {
    std::vector<uint8_t> buffer(10);
    
    // uint8_t - trivial
    size_t size = vle_encode<uint8_t>(42, buffer);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer[0], 42);
    
    // uint16_t - 1 byte
    size = vle_encode<uint16_t>(100, buffer);
    EXPECT_EQ(size, 1);
    EXPECT_EQ(buffer[0], 100);
    
    // uint16_t - 2 bytes
    size = vle_encode<uint16_t>(300, buffer);
    EXPECT_EQ(size, 2);
    EXPECT_EQ(buffer[0], 0xAC);  // 44 + 128 (continuation bit)
    EXPECT_EQ(buffer[1], 0x02);
    
    // uint64_t - large value
    size = vle_encode<uint64_t>(UINT64_MAX, buffer);
    EXPECT_EQ(size, 10);
}

// Test span-based decode
TEST(VLETemplateTest, SpanDecodeBasic) {
    std::vector<uint8_t> buffer(10);
    
    // Encode then decode uint8_t
    vle_encode<uint8_t>(42, buffer);
    uint8_t val8;
    size_t bytes_read = vle_decode(std::span(buffer.data(), 10), val8);
    EXPECT_EQ(bytes_read, 1);
    EXPECT_EQ(val8, 42);
    
    // Encode then decode uint16_t (2 bytes)
    vle_encode<uint16_t>(300, buffer);
    uint16_t val16;
    bytes_read = vle_decode(std::span(buffer.data(), 10), val16);
    EXPECT_EQ(bytes_read, 2);
    EXPECT_EQ(val16, 300);
    
    // Encode then decode uint64_t (max value)
    vle_encode<uint64_t>(UINT64_MAX, buffer);
    uint64_t val64;
    bytes_read = vle_decode(std::span(buffer.data(), 10), val64);
    EXPECT_EQ(bytes_read, 10);
    EXPECT_EQ(val64, UINT64_MAX);
}

// Test span-based signed integers
TEST(VLETemplateTest, SpanSignedIntegers) {
    std::vector<uint8_t> buffer(10);
    
    // int8_t - trivial
    vle_encode<int8_t>(-42, buffer);
    int8_t val8;
    size_t bytes_read = vle_decode(std::span(buffer.data(), 10), val8);
    EXPECT_EQ(bytes_read, 1);
    EXPECT_EQ(val8, -42);
    
    // int32_t with zigzag
    std::vector<int32_t> test_values = {0, -1, 1, -100, 100, INT32_MIN, INT32_MAX};
    
    for (auto original : test_values) {
        size_t size = vle_encode<int32_t>(original, buffer);
        int32_t decoded;
        bytes_read = vle_decode(std::span(buffer.data(), size), decoded);
        EXPECT_EQ(bytes_read, size);
        EXPECT_EQ(decoded, original) << "Failed for value: " << original;
    }
}

// Test span round-trip with various types
TEST(VLETemplateTest, SpanRoundTrip) {
    std::vector<uint8_t> buffer(20);
    
    // Test various uint64_t values
    std::vector<uint64_t> values = {
        0, 127, 128, 16383, 16384,
        UINT32_MAX,
        8ULL * 1024 * 1024,  // 8MB
        1ULL * 1024 * 1024 * 1024,  // 1GB
        UINT64_MAX
    };
    
    for (auto original : values) {
        size_t encoded_size = vle_encode<uint64_t>(original, buffer);
        uint64_t decoded;
        size_t decoded_size = vle_decode(std::span(buffer.data(), encoded_size), decoded);
        
        EXPECT_EQ(decoded_size, encoded_size) << "Size mismatch for value: " << original;
        EXPECT_EQ(decoded, original) << "Value mismatch for: " << original;
    }
}

// Test span error handling
TEST(VLETemplateTest, SpanErrorHandling) {
    std::vector<uint8_t> buffer(2);  // Too small for some values
    
    // Buffer too small for encoding
    EXPECT_THROW(vle_encode<uint64_t>(UINT64_MAX, buffer), std::runtime_error);
    
    // Empty buffer for decoding
    uint32_t value;
    std::vector<uint8_t> empty;
    EXPECT_THROW(vle_decode(std::span(empty.data(), 0), value), std::runtime_error);
    
    // Invalid encoding (all continuation bits set)
    std::vector<uint8_t> invalid = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_THROW(vle_decode(std::span(invalid.data(), invalid.size()), value), std::runtime_error);
}

// Test span vs stream consistency
TEST(VLETemplateTest, SpanStreamConsistency) {
    std::vector<uint8_t> span_buffer(10);
    std::stringstream stream;
    
    std::vector<uint64_t> test_values = {0, 127, 300, 65535, UINT32_MAX, UINT64_MAX};
    
    for (auto value : test_values) {
        // Encode with both methods
        size_t span_size = vle_encode<uint64_t>(value, span_buffer);
        size_t stream_size = vle_encode<uint64_t>(value, stream);
        
        EXPECT_EQ(span_size, stream_size) << "Size mismatch for value: " << value;
        
        // Decode with both methods
        uint64_t span_decoded;
        size_t span_bytes_read = vle_decode(std::span(span_buffer.data(), span_size), span_decoded);
        
        stream.seekg(0);
        uint64_t stream_decoded = vle_decode<uint64_t>(stream);
        
        EXPECT_EQ(span_bytes_read, span_size);
        EXPECT_EQ(span_decoded, value);
        EXPECT_EQ(stream_decoded, value);
        EXPECT_EQ(span_decoded, stream_decoded);
        
        // Clear stream for next iteration
        stream.str("");
        stream.clear();
    }
}

// Test span-based BCSV use case
TEST(VLETemplateTest, SpanBCSVUseCase) {
    std::vector<uint8_t> buffer(100);
    size_t write_pos = 0;
    
    // Writer encodes multiple row lengths with offset
    std::vector<size_t> row_lengths = {0, 150, 8192, 8ULL * 1024 * 1024};
    
    for (auto len : row_lengths) {
        size_t bytes_written = vle_encode<size_t>(len + 1, std::span(buffer.data() + write_pos, buffer.size() - write_pos));
        write_pos += bytes_written;
    }
    
    // Reader decodes
    size_t read_pos = 0;
    for (auto expected : row_lengths) {
        size_t decoded;
        size_t bytes_read = vle_decode(std::span(buffer.data() + read_pos, write_pos - read_pos), decoded);
        EXPECT_EQ(decoded - 1, expected);  // Subtract offset
        read_pos += bytes_read;
    }
    
    EXPECT_EQ(read_pos, write_pos);  // Verify we read exactly what was written
}
