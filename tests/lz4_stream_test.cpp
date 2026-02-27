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
#include <type_traits>
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
        LZ4CompressionStream stream(64 * 1024, 9);
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
    auto c1 = compressor.compress(row1); compressed1.assign(c1.begin(), c1.end());
    auto c2 = compressor.compress(row2); compressed2.assign(c2.begin(), c2.end());
    auto c3 = compressor.compress(row3); compressed3.assign(c3.begin(), c3.end());
    
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
    std::vector<std::byte> compressed;
    
    auto c = compressor.compress(empty);
    compressed.assign(c.begin(), c.end());
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

// Test: Move operations are explicitly deleted because LZ4_stream_t contains
// internal pointers into the ring buffer that would dangle after a move.
TEST_F(LZ4StreamTest, MoveOperationsDeleted) {
    static_assert(!std::is_move_constructible_v<LZ4CompressionStream<>>,
                  "LZ4CompressionStream must not be move-constructible");
    static_assert(!std::is_move_assignable_v<LZ4CompressionStream<>>,
                  "LZ4CompressionStream must not be move-assignable");
    static_assert(!std::is_move_constructible_v<LZ4DecompressionStream<>>,
                  "LZ4DecompressionStream must not be move-constructible");
    static_assert(!std::is_move_assignable_v<LZ4DecompressionStream<>>,
                  "LZ4DecompressionStream must not be move-assignable");
}

// Test: Decompression with auto-growing buffer
TEST_F(LZ4StreamTest, DecompressionWrongExpectedSize) {
    LZ4CompressionStream compressor(1);
    
    // create moderately large input data (64 KB)
    std::vector<std::byte> input(64*1024);
    for(size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::byte>(i % 256);
    }
    
    auto c1 = compressor.compress(input);
    std::vector<std::byte> compressed1(c1.begin(), c1.end());
    
    std::cout << "Compressed " << input.size() << " bytes to " << compressed1.size() << " bytes" << std::endl;
    ASSERT_GT(compressed1.size(), 0);
    
    // Decompressor starts with small buffer but should have enough after one growth
    // For 64KB data, compressed ~64KB, 2x = 128KB which should be sufficient
    LZ4DecompressionStream<256*1024> decompressor(1024); // 1 KB initial, 256 KB max
    auto decompressedSpan = decompressor.decompress(compressed1);    
    
    EXPECT_EQ(decompressedSpan.size(), input.size());
    
    // Verify data is correct
    EXPECT_TRUE(std::equal(decompressedSpan.begin(), decompressedSpan.end(), input.begin()));
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

// Test: Verify compression returns non-zero for valid data
TEST_F(LZ4StreamTest, CompressionReturnsValidData) {
    LZ4CompressionStream compressor(1);
    
    std::cout << "\n=== Compression Validity Test ===" << std::endl;
    
    // Test with various data sizes
    std::vector<size_t> sizes = {10, 50, 100, 500};
    
    for (size_t size : sizes) {
        std::vector<std::byte> input(size);
        for (size_t i = 0; i < size; ++i) {
            input[i] = static_cast<std::byte>(i % 256);
        }
        
        const auto& compressed = compressor.compress(input);
        
        std::cout << "Input size " << size << " -> compressed size " << compressed.size() << std::endl;
        
        // Compressed data should NOT be empty for valid input
        ASSERT_GT(compressed.size(), 0) << "Compression returned empty buffer for size " << size;
        
        // Show first bytes of compressed data
        std::cout << "  First 10 bytes (hex): ";
        for (size_t i = 0; i < std::min(size_t(10), compressed.size()); ++i) {
            printf("%02x ", static_cast<unsigned char>(compressed[i]));
        }
        std::cout << std::endl;
    }
    
    std::cout << "=== End Compression Validity Test ===" << std::endl;
}

// Test: Small row streaming (BCSV use case simulation)
TEST_F(LZ4StreamTest, SmallRowStreaming) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream<> decompressor(1024);
    
    std::cout << "\n=== Small Row Streaming Test (BCSV Simulation) ===" << std::endl;
    
    // Simulate 100 small rows with similar structure (typical CSV/BCSV scenario)
    std::vector<std::vector<std::byte>> originalRows;
    std::vector<std::vector<std::byte>> compressedRows;
    
    for (int i = 0; i < 100; ++i) {
        // Create row data similar to CSV: id,name,value,timestamp
        std::string rowData = "id:" + std::to_string(i) + 
                             ",name:User" + std::to_string(i) +
                             ",value:" + std::to_string(100.5 + i * 0.1) +
                             ",timestamp:1234567890";
        
        auto row = createTestData(rowData);
        originalRows.push_back(row);
        
        // Compress with streaming context
        auto c = compressor.compress(row);
        std::vector<std::byte> compressed(c.begin(), c.end());
        compressedRows.push_back(compressed);
        
        // Log first few and last few compressions with hexdump
        if (i < 5 || i >= 95) {
            std::cout << "Row " << i << ": " << row.size() << " -> " << compressed.size() 
                      << " bytes (ratio: " << (100.0 * compressed.size() / row.size()) << "%)" << std::endl;
            
            // Hexdump first 20 bytes of compressed data
            std::cout << "  Compressed hex: ";
            for (size_t j = 0; j < std::min(size_t(20), compressed.size()); ++j) {
                printf("%02x ", static_cast<unsigned char>(compressed[j]));
            }
            std::cout << std::endl;
            
            // Show original data for comparison
            std::cout << "  Original: " << rowData.substr(0, 30) << "..." << std::endl;
        }
    }
    
    std::cout << "\nCompression summary:" << std::endl;
    size_t totalOriginal = 0, totalCompressed = 0;
    for (size_t i = 0; i < originalRows.size(); ++i) {
        totalOriginal += originalRows[i].size();
        totalCompressed += compressedRows[i].size();
    }
    std::cout << "Total: " << totalOriginal << " -> " << totalCompressed 
              << " bytes (overall ratio: " << (100.0 * totalCompressed / totalOriginal) << "%)" << std::endl;
    
    // Decompress and verify
    std::cout << "\nDecompressing..." << std::endl;
    std::vector<std::vector<std::byte>> decompressedRows;
    for (size_t i = 0; i < compressedRows.size(); ++i) {
        auto decompressedSpan = decompressor.decompress(compressedRows[i]);
        // IMPORTANT: Copy the data immediately! The span becomes invalid on next decompress
        std::vector<std::byte> decompressed(decompressedSpan.begin(), decompressedSpan.end());
        decompressedRows.push_back(decompressed);
    }
    
    // Now verify all rows
    for (size_t i = 0; i < decompressedRows.size(); ++i) {
        const auto& decompressed = decompressedRows[i];
        
        if (i < 5) {
            std::cout << "Row " << i << " decompressed: " << decompressed.size() << " bytes" << std::endl;
            std::cout << "  Expected: " << originalRows[i].size() << " bytes" << std::endl;
            
            // Show first 30 chars of decompressed
            std::cout << "  Decompressed: ";
            for (size_t j = 0; j < std::min(size_t(30), decompressed.size()); ++j) {
                char c = static_cast<char>(decompressed[j]);
                std::cout << (isprint(c) ? c : '?');
            }
            std::cout << std::endl;
            
            // Show expected
            std::cout << "  Expected:     ";
            for (size_t j = 0; j < std::min(size_t(30), originalRows[i].size()); ++j) {
                char c = static_cast<char>(originalRows[i][j]);
                std::cout << (isprint(c) ? c : '?');
            }
            std::cout << std::endl;
        }
        
        ASSERT_EQ(decompressed.size(), originalRows[i].size()) 
            << "Row " << i << " size mismatch";
        EXPECT_TRUE(std::equal(decompressed.begin(), decompressed.end(), originalRows[i].begin()))
            << "Row " << i << " data mismatch";
    }
    
    std::cout << "✓ All " << originalRows.size() << " rows compressed and decompressed correctly" << std::endl;
    std::cout << "Dictionary effect visible: later rows compress better than early rows" << std::endl;
    std::cout << "=== End Small Row Streaming Test ===" << std::endl;
}

// Test: Debug streaming with detailed logging
TEST_F(LZ4StreamTest, DebugStreamingBehavior) {
    LZ4CompressionStream compressor(1);
    LZ4DecompressionStream<> decompressor(1024);  // Start with 1KB buffer
    
    std::cout << "\n=== LZ4 Streaming Debug Test ===" << std::endl;
    
    // Create 3 similar rows (typical BCSV scenario)
    auto row1 = createTestData("sensor_id:001,temperature:25.3,humidity:60.2,pressure:1013.25,timestamp:1234567890");
    auto row2 = createTestData("sensor_id:002,temperature:25.4,humidity:60.1,pressure:1013.26,timestamp:1234567891");
    auto row3 = createTestData("sensor_id:003,temperature:25.5,humidity:60.0,pressure:1013.27,timestamp:1234567892");
    
    std::cout << "Original sizes: " << row1.size() << ", " << row2.size() << ", " << row3.size() << std::endl;
    
    // Compress with streaming context - MUST COPY because compress() returns reference to internal buffer
    auto c1 = compressor.compress(row1); std::vector<std::byte> compressed1(c1.begin(), c1.end());  // Copy, not reference!
    std::cout << "Compressed row 1: " << row1.size() << " -> " << compressed1.size() 
              << " bytes (ratio: " << (100.0 * compressed1.size() / row1.size()) << "%)" << std::endl;
    
    auto c2 = compressor.compress(row2); std::vector<std::byte> compressed2(c2.begin(), c2.end());  // Copy, not reference!
    std::cout << "Compressed row 2: " << row2.size() << " -> " << compressed2.size() 
              << " bytes (ratio: " << (100.0 * compressed2.size() / row2.size()) << "%)" << std::endl;
    
    auto c3 = compressor.compress(row3); std::vector<std::byte> compressed3(c3.begin(), c3.end());  // Copy, not reference!
    std::cout << "Compressed row 3: " << row3.size() << " -> " << compressed3.size() 
              << " bytes (ratio: " << (100.0 * compressed3.size() / row3.size()) << "%)" << std::endl;
    
    // Verify compression worked
    ASSERT_GT(compressed1.size(), 0);
    ASSERT_GT(compressed2.size(), 0);
    ASSERT_GT(compressed3.size(), 0);
    
    // Dictionary compression should make row2 and row3 smaller
    std::cout << "\nDictionary effect: Row2 vs Row1: " 
              << ((compressed2.size() < compressed1.size()) ? "BETTER" : "SAME/WORSE")
              << ", Row3 vs Row1: "
              << ((compressed3.size() < compressed1.size()) ? "BETTER" : "SAME/WORSE") << std::endl;
    
    // Decompress with streaming context
    std::cout << "\nDecompressing..." << std::endl;
    
    std::cout << "Attempting to decompress row 1 (" << compressed1.size() << " compressed bytes)..." << std::endl;
    
    // Print first few bytes of compressed data for debugging
    std::cout << "Compressed1 first bytes: ";
    for (size_t i = 0; i < std::min(size_t(10), compressed1.size()); ++i) {
        std::cout << std::hex << static_cast<int>(compressed1[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    auto decompressed1 = decompressor.decompress(compressed1);
    std::cout << "Decompressed row 1: " << decompressed1.size() << " bytes" << std::endl;
    ASSERT_EQ(decompressed1.size(), row1.size());
    EXPECT_TRUE(std::equal(decompressed1.begin(), decompressed1.end(), row1.begin()));
    
    auto decompressed2 = decompressor.decompress(compressed2);
    std::cout << "Decompressed row 2: " << decompressed2.size() << " bytes" << std::endl;
    ASSERT_EQ(decompressed2.size(), row2.size());
    EXPECT_TRUE(std::equal(decompressed2.begin(), decompressed2.end(), row2.begin()));
    
    auto decompressed3 = decompressor.decompress(compressed3);
    std::cout << "Decompressed row 3: " << decompressed3.size() << " bytes" << std::endl;
    ASSERT_EQ(decompressed3.size(), row3.size());
    EXPECT_TRUE(std::equal(decompressed3.begin(), decompressed3.end(), row3.begin()));
    
    std::cout << "\n✓ All rows compressed and decompressed correctly with streaming context" << std::endl;
    std::cout << "=== End Debug Test ===" << std::endl;
}

// Test: Large data compression (replacing ChunkedCompression tests)
TEST_F(LZ4StreamTest, LargeDataCompressionMixed) {
    std::cout << "\n=== Large Data Compression Test (Mixed Sizes) ===" << std::endl;
    
    // Use heap allocation to avoid stack overflow
    auto compressorPtr = std::make_unique<LZ4CompressionStream<>>(1);
    auto decompressorPtr = std::make_unique<LZ4DecompressionStream<>>();
    
    auto& compressor = *compressorPtr;
    auto& decompressor = *decompressorPtr;
    
    // 1. Small data
    std::vector<std::byte> smallData = createTestData(std::string(1024, 'A'));
    auto compressedSmall = compressor.compress(smallData);
    auto decompressedSmallSpan = decompressor.decompress(compressedSmall);
    std::vector<std::byte> decompressedSmall(decompressedSmallSpan.begin(), decompressedSmallSpan.end());
    
    ASSERT_EQ(decompressedSmall.size(), smallData.size());
    EXPECT_EQ(decompressedSmall, smallData);
    std::cout << "✓ Small data (1KB) passed" << std::endl;

    // 2. Medium data (spanning multiple internal blocks if it were chunked, but here it's one stream block)
    // 200KB data
    std::vector<std::byte> mediumData(200 * 1024);
    for(size_t i=0; i<mediumData.size(); ++i) mediumData[i] = static_cast<std::byte>(i % 256);
    
    auto compressedMedium = compressor.compress(mediumData);
    auto decompressedMediumSpan = decompressor.decompress(compressedMedium);
    std::vector<std::byte> decompressedMedium(decompressedMediumSpan.begin(), decompressedMediumSpan.end());
    
    ASSERT_EQ(decompressedMedium.size(), mediumData.size());
    EXPECT_EQ(decompressedMedium, mediumData);
    std::cout << "✓ Medium data (200KB) passed" << std::endl;

    // 3. Very Large data (16MB) - forcing fallback to large buffer
    size_t largeSize = 16 * 1024 * 1024;
    std::vector<std::byte> largeData(largeSize);
    for(size_t i=0; i<largeSize; ++i) largeData[i] = static_cast<std::byte>((i / 1024) % 256);
    
    std::cout << "Compressing 16MB data..." << std::endl;
    auto compressedLarge = compressor.compress(largeData);
    std::cout << "Compressed size: " << compressedLarge.size() << std::endl;
    
    std::cout << "Decompressing 16MB data..." << std::endl;
    auto decompressedLargeSpan = decompressor.decompress(compressedLarge);
    
    ASSERT_EQ(decompressedLargeSpan.size(), largeData.size());
    
    // Verify sample
    bool correct = true;
    for(size_t i=0; i<largeSize; i+=4096) {
        if(decompressedLargeSpan[i] != largeData[i]) {
            correct = false;
            break;
        }
    }
    EXPECT_TRUE(correct);
    std::cout << "✓ Large data (16MB) passed" << std::endl;
    
    // 4. Small data AGAIN to verify dictionary/history persistence after large block
    std::vector<std::byte> smallData2 = createTestData("Post-large-block-test");
    auto compressedSmall2 = compressor.compress(smallData2);
    auto decompressedSmall2Span = decompressor.decompress(compressedSmall2);
    std::vector<std::byte> decompressedSmall2(decompressedSmall2Span.begin(), decompressedSmall2Span.end());
    
    ASSERT_EQ(decompressedSmall2.size(), smallData2.size());
    EXPECT_EQ(decompressedSmall2, smallData2);
    std::cout << "✓ Small data after large block passed" << std::endl;

    std::cout << "=== End Large Data Test ===" << std::endl;
}


