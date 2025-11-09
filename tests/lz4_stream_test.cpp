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
    std::vector<uint8_t> createTestData(const std::string& content) {
        return std::vector<uint8_t>(content.begin(), content.end());
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
    LZ4CompressionStream compressor(1);
    
    auto input = createTestData("Hello, World! This is a test string for LZ4 compression.");
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    
    int compressedSize = compressor.compress(input, compressed);
    
    EXPECT_GT(compressedSize, 0);
    // Note: Small data may not compress well, so we don't check if it's smaller
}

// Test: Basic decompression
TEST_F(LZ4StreamTest, BasicDecompression) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    auto input = createTestData("Hello, World! This is a test string for LZ4 compression.");
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    std::vector<uint8_t> decompressed(input.size());
    
    int compressedSize = compressor.compress(input, compressed);
    ASSERT_GT(compressedSize, 0);
    
    int decompressedSize = decompressor.decompress(
        std::span<const uint8_t>(compressed.data(), compressedSize),
        decompressed,
        input.size()
    );
    
    EXPECT_EQ(decompressedSize, static_cast<int>(input.size()));
    EXPECT_EQ(decompressed, input);
}

// Test: Round-trip compression/decompression
TEST_F(LZ4StreamTest, RoundTripCompression) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    std::string testString = "The quick brown fox jumps over the lazy dog. ";
    testString += testString; // Make it longer for better compression
    testString += testString;
    
    auto input = createTestData(testString);
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    std::vector<uint8_t> decompressed(input.size());
    
    // Compress
    int compressedSize = compressor.compress(input, compressed);
    ASSERT_GT(compressedSize, 0);
    
    // Decompress
    int decompressedSize = decompressor.decompress(
        std::span<const uint8_t>(compressed.data(), compressedSize),
        decompressed,
        input.size()
    );
    
    EXPECT_EQ(decompressedSize, static_cast<int>(input.size()));
    EXPECT_EQ(decompressed, input);
}

// Test: Streaming context preservation (multiple compressions)
TEST_F(LZ4StreamTest, StreamingContextPreservation) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    // Create similar data that will benefit from dictionary compression
    auto row1 = createTestData("temperature:25.3,humidity:60.2,pressure:1013.25");
    auto row2 = createTestData("temperature:25.4,humidity:60.1,pressure:1013.26");
    auto row3 = createTestData("temperature:25.5,humidity:60.0,pressure:1013.27");
    
    std::vector<uint8_t> compressed1(LZ4_COMPRESSBOUND(row1.size()));
    std::vector<uint8_t> compressed2(LZ4_COMPRESSBOUND(row2.size()));
    std::vector<uint8_t> compressed3(LZ4_COMPRESSBOUND(row3.size()));
    
    // Compress rows with streaming context
    int size1 = compressor.compress(row1, compressed1);
    int size2 = compressor.compress(row2, compressed2);
    int size3 = compressor.compress(row3, compressed3);
    
    ASSERT_GT(size1, 0);
    ASSERT_GT(size2, 0);
    ASSERT_GT(size3, 0);
    
    // Second and third compressions should be smaller due to dictionary
    // (Though this is not guaranteed in all cases, it's typical for similar data)
    
    // Decompress with streaming context
    std::vector<uint8_t> decompressed1(row1.size());
    std::vector<uint8_t> decompressed2(row2.size());
    std::vector<uint8_t> decompressed3(row3.size());
    
    int dsize1 = decompressor.decompress(
        std::span<const uint8_t>(compressed1.data(), size1), decompressed1, row1.size());
    int dsize2 = decompressor.decompress(
        std::span<const uint8_t>(compressed2.data(), size2), decompressed2, row2.size());
    int dsize3 = decompressor.decompress(
        std::span<const uint8_t>(compressed3.data(), size3), decompressed3, row3.size());
    
    EXPECT_EQ(dsize1, static_cast<int>(row1.size()));
    EXPECT_EQ(dsize2, static_cast<int>(row2.size()));
    EXPECT_EQ(dsize3, static_cast<int>(row3.size()));
    
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
    int size1 = compressor.compress(input, compressed);
    ASSERT_GT(size1, 0);
    
    // Reset compressor
    compressor.reset();
    
    // Second compression after reset
    int size2 = compressor.compress(input, compressed);
    ASSERT_GT(size2, 0);
    
    // Both compressions should work correctly
    int dsize = decompressor.decompress(
        std::span<const uint8_t>(compressed.data(), size2), decompressed, input.size());
    EXPECT_EQ(dsize, static_cast<int>(input.size()));
    EXPECT_EQ(decompressed, input);
    
    // Reset decompressor
    decompressor.reset();
    
    // Decompress again after reset
    dsize = decompressor.decompress(
        std::span<const uint8_t>(compressed.data(), size2), decompressed, input.size());
    EXPECT_EQ(dsize, static_cast<int>(input.size()));
    EXPECT_EQ(decompressed, input);
}

// Test: Empty input
TEST_F(LZ4StreamTest, EmptyInput) {
    LZ4CompressionStream compressor(1);
    
    std::vector<uint8_t> empty;
    std::vector<uint8_t> compressed(100);
    
    int compressedSize = compressor.compress(empty, compressed);
    EXPECT_EQ(compressedSize, 0);
}

// Test: Insufficient output buffer
TEST_F(LZ4StreamTest, InsufficientOutputBuffer) {
    LZ4CompressionStream compressor(1);
    
    auto input = createTestData("This is a test");
    std::vector<uint8_t> tooSmall(1); // Way too small
    
    int compressedSize = compressor.compress(input, tooSmall);
    EXPECT_EQ(compressedSize, 0); // Should fail
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
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    
    int compressedSize = compressor.compress(input, compressed);
    EXPECT_GT(compressedSize, 0);
}

// Test: Move construction for compression stream
TEST_F(LZ4StreamTest, MoveConstructionCompression) {
    LZ4CompressionStream stream1(5);
    
    // Move construct
    LZ4CompressionStream stream2(std::move(stream1));
    EXPECT_EQ(stream2.getAcceleration(), 5);
    
    // stream2 should be functional
    auto input = createTestData("Test");
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    int size = stream2.compress(input, compressed);
    EXPECT_GT(size, 0);
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
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    int size = stream2.compress(input, compressed);
    EXPECT_GT(size, 0);
}

// Test: Move construction for decompression stream
TEST_F(LZ4StreamTest, MoveConstructionDecompression) {
    LZ4DecompressionStream stream1;
    
    // Move construct
    LZ4DecompressionStream stream2(std::move(stream1));
    
    // stream2 should be functional - compress and decompress test data
    LZ4CompressionStream compressor(1);
    auto input = createTestData("Test data");
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    std::vector<uint8_t> decompressed(input.size());
    
    int csize = compressor.compress(input, compressed);
    ASSERT_GT(csize, 0);
    
    int dsize = stream2.decompress(
        std::span<const uint8_t>(compressed.data(), csize), decompressed, input.size());
    EXPECT_EQ(dsize, static_cast<int>(input.size()));
}

// Test: Decompression with wrong expected size
TEST_F(LZ4StreamTest, DecompressionWrongExpectedSize) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    auto input = createTestData("Test data for size mismatch");
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    std::vector<uint8_t> decompressed(input.size() + 100);
    
    int compressedSize = compressor.compress(input, compressed);
    ASSERT_GT(compressedSize, 0);
    
    // Try to decompress with smaller expected size (should fail)
    int decompressedSize = decompressor.decompress(
        std::span<const uint8_t>(compressed.data(), compressedSize),
        decompressed,
        input.size() - 5 // Too small expected size
    );
    
    EXPECT_LT(decompressedSize, 0); // Should fail
}

// Test: Large data compression
TEST_F(LZ4StreamTest, LargeDataCompression) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream decompressor;
    
    // Create 100KB of data
    std::vector<uint8_t> input(100 * 1024);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>(i % 256);
    }
    
    std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(input.size()));
    std::vector<uint8_t> decompressed(input.size());
    
    int compressedSize = compressor.compress(input, compressed);
    ASSERT_GT(compressedSize, 0);
    
    int decompressedSize = decompressor.decompress(
        std::span<const uint8_t>(compressed.data(), compressedSize),
        decompressed,
        input.size()
    );
    
    EXPECT_EQ(decompressedSize, static_cast<int>(input.size()));
    EXPECT_EQ(decompressed, input);
}
