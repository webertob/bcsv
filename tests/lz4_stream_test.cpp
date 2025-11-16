/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <bcsv/lz4_stream.hpp>

using namespace bcsv;

class LZ4StreamTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    
    // Helper to create test data
    std::vector<std::byte> createTestData(const std::string& content) {
        std::vector<std::byte> result;
        result.reserve(content.size());
        for (char c : content) {
            result.push_back(static_cast<std::byte>(c));
        }
        return result;
    }
};

// Test: Compression stream creation
TEST_F(LZ4StreamTest, CompressionStreamCreation) {
    EXPECT_NO_THROW({
        LZ4CompressionStream stream(1);
        EXPECT_EQ(stream.getAcceleration(), 1);
    });
    
    EXPECT_NO_THROW({
        LZ4CompressionStream stream(9);
        EXPECT_EQ(stream.getAcceleration(), 9);
    });
}

// Test: Decompression stream creation
TEST_F(LZ4StreamTest, DecompressionStreamCreation) {
    EXPECT_NO_THROW({
        LZ4DecompressionStream stream;
    });
}

// Test: Basic compression
TEST_F(LZ4StreamTest, BasicCompression) {
    LZ4CompressionStream compressor(1 );
    
    auto input = createTestData("Hello, World! This is a test string for LZ4 compression.");
        
    const auto compressedData = compressor.compress(input);
    
    EXPECT_GT(compressedData.size(), 0);
    // Note: Small data may not compress well, so we don't check if it's smaller
}

// Test: Basic decompression
TEST_F(LZ4StreamTest, BasicDecompression) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    auto input = createTestData("Hello, World! This is a test string for LZ4 compression.");
    
    const auto& zip = compressor.compress(input);
    ASSERT_GT(zip.size(), 0);
    
    const auto unzip = decompressor.decompress(zip);
    std::vector<std::byte> output(unzip.begin(), unzip.end());
    EXPECT_EQ(output.size(), input.size());
    EXPECT_EQ(output, input);
}

// Test: Round-trip compression/decompression
TEST_F(LZ4StreamTest, RoundTripCompression) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    std::string testString = "The quick brown fox jumps over the lazy dog. ";
    testString += testString; // Make it longer for better compression
    testString += testString;
    
    auto input = createTestData(testString);
    
    // Compress
    const auto& compressedData = compressor.compress(input);
    ASSERT_GT(compressedData.size(), 0);
    
    // Decompress
    const auto& decompressedData = decompressor.decompress(compressedData);
    std::vector<std::byte> output(decompressedData.begin(), decompressedData.end());
    EXPECT_EQ(output.size(), input.size());
    EXPECT_EQ(output, input);
}

// Test: Streaming context preservation (multiple compressions)
TEST_F(LZ4StreamTest, StreamingContextPreservation) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    // Create similar data that will benefit from dictionary compression
    auto row1 = createTestData("temperature:25.3,humidity:60.2,pressure:1013.25");
    auto row2 = createTestData("temperature:25.4,humidity:60.1,pressure:1013.26");
    auto row3 = createTestData("temperature:25.5,humidity:60.0,pressure:1013.27");
    
    std::vector<std::byte> compressed1;
    std::vector<std::byte> compressed2;
    std::vector<std::byte> compressed3;
    
    // Compress rows with streaming context
    compressed1 = compressor.compress(row1);
    compressed2 = compressor.compress(row2);
    compressed3 = compressor.compress(row3);
    
    ASSERT_GT(compressed1.size(), 0);
    ASSERT_GT(compressed2.size(), 0);
    ASSERT_GT(compressed3.size(), 0);
    
    // Second and third compressions should be smaller due to dictionary
    // (Though this is not guaranteed in all cases, it's typical for similar data)
    
    // Decompress with streaming context
    std::vector<std::byte> decompressed1;
    std::vector<std::byte> decompressed2;
    std::vector<std::byte> decompressed3;
    
    const auto& unzip1 = decompressor.decompress(compressed1);
    decompressed1.assign(unzip1.begin(), unzip1.end());
    const auto& unzip2 = decompressor.decompress(compressed2);
    decompressed2.assign(unzip2.begin(), unzip2.end()); 
    const auto& unzip3 = decompressor.decompress(compressed3);
    decompressed3.assign(unzip3.begin(), unzip3.end());

    EXPECT_EQ(decompressed1.size(), row1.size());
    EXPECT_EQ(decompressed2.size(), row2.size());
    EXPECT_EQ(decompressed3.size(), row3.size());
    
    EXPECT_EQ(decompressed1, row1);
    EXPECT_EQ(decompressed2, row2);
    EXPECT_EQ(decompressed3, row3);
}

// Test: Stream reset
TEST_F(LZ4StreamTest, StreamReset) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    auto input = createTestData("Test data for compression");
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    std::vector<uint8_t> decompressed(input.size());
    
    // First compression
    auto zip1 = compressor.compress(input);
    ASSERT_GT(zip1.size(), 0);
    
    // Reset compressor
    compressor.reset();
    
    // Second compression after reset
    auto zip2 = compressor.compress(input);
    ASSERT_GT(zip2.size(), 0);
    
    // Both compressions should work correctly
    auto unzip2 = decompressor.decompress(zip2);
    std::vector<std::byte> output(unzip2.begin(), unzip2.end());
    EXPECT_EQ(output.size(), input.size());
    EXPECT_EQ(output, input);
    
    // Reset decompressor
    decompressor.reset();
    
    // Decompress again after reset
    auto unzip2_after_reset = decompressor.decompress(zip2);
    std::vector<std::byte> output_after_reset(unzip2_after_reset.begin(), unzip2_after_reset.end());
    EXPECT_EQ(output_after_reset.size(), input.size());
    EXPECT_EQ(output_after_reset, input);
}

// Test: Empty input
TEST_F(LZ4StreamTest, EmptyInput) {
    LZ4CompressionStream compressor(1);
    
    std::vector<std::byte> empty;
    std::vector<std::byte> compressed(100);
    
    compressed = compressor.compress(empty);
    EXPECT_EQ(compressed.size(), 0);
}

// Test: Acceleration level changes
TEST_F(LZ4StreamTest, AccelerationLevelChanges) {
    LZ4CompressionStream compressor(1);
    
    EXPECT_EQ(compressor.getAcceleration(), 1);
    
    compressor.setAcceleration(5);
    EXPECT_EQ(compressor.getAcceleration(), 5);
    
    compressor.setAcceleration(9);
    EXPECT_EQ(compressor.getAcceleration(), 9);
    
    // Compression should still work after acceleration change
    auto input = createTestData("Test data");
    const auto& compressed = compressor.compress(input);
    EXPECT_GT(compressed.size(), 0);
}

// Test: Move construction for compression stream
TEST_F(LZ4StreamTest, MoveConstructionCompression) {
    LZ4CompressionStream stream1(5);
    
    // Move construct
    LZ4CompressionStream stream2(std::move(stream1));
    EXPECT_EQ(stream2.getAcceleration(), 5);
    
    // stream2 should be functional
    auto input = createTestData("Test");
    const auto& compressed = stream2.compress(input);
    EXPECT_GT(compressed.size(), 0);
}

// Test: Move assignment for compression stream
TEST_F(LZ4StreamTest, MoveAssignmentCompression) {
    LZ4CompressionStream stream1(7);
    LZ4CompressionStream stream2(1);
    
    // Move assign
    stream2 = std::move(stream1);
    EXPECT_EQ(stream2.getAcceleration(), 7);
    
    // stream2 should be functional
    auto input = createTestData("Test");
    const auto& compressed = stream2.compress(input);
    EXPECT_GT(compressed.size(), 0);
}

// Test: Move construction for decompression stream
TEST_F(LZ4StreamTest, MoveConstructionDecompression) {
    LZ4DecompressionStream stream1;
    
    // Move construct
    LZ4DecompressionStream stream2(std::move(stream1));
    
    // stream2 should be functional - compress and decompress test data
    LZ4CompressionStream compressor(1);
    auto input = createTestData("Test data");
    
    const auto& compressed = compressor.compress(input);
    ASSERT_GT(compressed.size(), 0);
    
    auto decompressed = stream2.decompress(compressed);
    EXPECT_EQ(decompressed.size(), input.size());
}

// Test: Decompression with wrong expected size
TEST_F(LZ4StreamTest, DecompressionWrongExpectedSize) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    // create very large input data
    std::vector<uint8_t> input(8*1024*1024, 0xAB);
    const auto& compressed = compressor.compress(input);    
    ASSERT_GT(compressed.size(), 0);
    
    // Try to decompress with smaller expected size (should fail)
    decompressor.resizeBuffer(64*1024); // 64 KB buffer
    auto decompressedSpan = decompressor.decompress(compressed);    
    
    EXPECT_EQ(decompressedSpan.size(), input.size()); // Should not fail
}

// Test: Large data compression
TEST_F(LZ4StreamTest, LargeDataCompression) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    // Create 100KB of data
    std::vector<std::byte> input(100 * 1024);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::byte>(i % 256);
    }
        
    const auto& compressed = compressor.compress(input);
    ASSERT_GT(compressed.size(), 0);
    
    auto decompressed = decompressor.decompress(compressed);
    std::vector<std::byte> output(decompressed.begin(), decompressed.end());
    EXPECT_EQ(output.size(), input.size());
    EXPECT_EQ(output, input);
}
