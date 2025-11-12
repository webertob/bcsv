/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <gtest/gtest.h>
#include <sstream>
#include <bcsv/file_footer.h>

using namespace bcsv;

class FileFooterTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test: PacketIndexEntry size validation
TEST_F(FileFooterTest, PacketIndexEntrySize) {
    EXPECT_EQ(sizeof(PacketIndexEntry), 16);
}

// Test: PacketIndexEntry construction
TEST_F(FileFooterTest, PacketIndexEntryConstruction) {
    PacketIndexEntry entry1;
    EXPECT_EQ(entry1.byteOffset_, 0);
    EXPECT_EQ(entry1.firstRow_, 0);
    
    PacketIndexEntry entry2(1000, 5000);
    EXPECT_EQ(entry2.byteOffset_, 1000);
    EXPECT_EQ(entry2.firstRow_, 5000);
}

// Test: FileFooter default construction
TEST_F(FileFooterTest, DefaultConstruction) {
    FileFooter footer;
    
    EXPECT_EQ(footer.packetIndex().size(), 0);
    EXPECT_EQ(footer.totalRowCount(), 0);
    EXPECT_EQ(footer.lastPacketPayloadChecksum(), 0);
}

// Test: Adding packets to index
TEST_F(FileFooterTest, AddPackets) {
    FileFooter footer;
    
    footer.packetIndex().emplace_back(100, 0);
    footer.packetIndex().emplace_back(5000, 1000);
    footer.packetIndex().emplace_back(10000, 2000);
    
    EXPECT_EQ(footer.packetIndex().size(), 3);
    
    EXPECT_EQ(footer.packetIndex()[0].byteOffset_, 100);
    EXPECT_EQ(footer.packetIndex()[0].firstRow_, 0);
    
    EXPECT_EQ(footer.packetIndex()[1].byteOffset_, 5000);
    EXPECT_EQ(footer.packetIndex()[1].firstRow_, 1000);
    
    EXPECT_EQ(footer.packetIndex()[2].byteOffset_, 10000);
    EXPECT_EQ(footer.packetIndex()[2].firstRow_, 2000);
}

// Test: Setting and getting properties
TEST_F(FileFooterTest, SetGetProperties) {
    FileFooter index;
    
    index.totalRowCount() = 12345;
    EXPECT_EQ(index.totalRowCount(), 12345);
    
    index.lastPacketPayloadChecksum() = 0xABCDEF1234567890ULL;
    EXPECT_EQ(index.lastPacketPayloadChecksum(), 0xABCDEF1234567890ULL);
}

// Test: Calculate size
TEST_F(FileFooterTest, CalculateSize) {
    FileFooter index;
    
    // Empty index: 4 (BIDX) + 4 (EIDX) + 4 (offset) + 8 (checksum) + 8 (rows) + 8 (index checksum) = 36 bytes
    EXPECT_EQ(index.encodedSize(), 36);
    
    // Add 1 packet: 36 + 16 = 52 bytes
    index.packetIndex().emplace_back(100, 0);
    EXPECT_EQ(index.encodedSize(), 52);
    
    // Add 2 more packets: 36 + 48 = 84 bytes
    index.packetIndex().emplace_back(5000, 1000);
    index.packetIndex().emplace_back(10000, 2000);
    EXPECT_EQ(index.encodedSize(), 84);
}

// Test: Clear index
TEST_F(FileFooterTest, Clear) {
    FileFooter footer;
    
    footer.packetIndex().emplace_back(100, 0);
    footer.packetIndex().emplace_back(5000, 1000);
    footer.totalRowCount() = 2000;
    footer.lastPacketPayloadChecksum() = 0x1234567890ABCDEFULL;
    
    EXPECT_EQ(footer.packetIndex().size(), 2);
    EXPECT_EQ(footer.totalRowCount(), 2000);
    
    footer.clear();
    
    EXPECT_EQ(footer.packetIndex().size(), 0);
    EXPECT_EQ(footer.totalRowCount(), 0);
    EXPECT_EQ(footer.lastPacketPayloadChecksum(), 0);
}

// Test: Write and read empty index
TEST_F(FileFooterTest, WriteReadEmptyIndex) {
    FileFooter original;
    original.totalRowCount() = 0;
    original.lastPacketPayloadChecksum() = 0;
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Position stream at footer for reading (clear state first)
    stream.clear();  // Clear any error flags
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    
    // Read back
    FileFooter copy;
    EXPECT_TRUE(copy.read(stream));
    
    EXPECT_EQ(copy.packetIndex().size(), 0);
    EXPECT_EQ(copy.totalRowCount(), 0);
    EXPECT_EQ(copy.lastPacketPayloadChecksum(), 0);
}

// Test: Write and read index with packets
TEST_F(FileFooterTest, WriteReadWithPackets) {
    FileFooter original;
    
    original.packetIndex().emplace_back(100, 0);
    original.packetIndex().emplace_back(5000, 1000);
    original.packetIndex().emplace_back(10000, 2000);
    original.packetIndex().emplace_back(15000, 3000);
    original.totalRowCount() = 4000;
    original.lastPacketPayloadChecksum() = 0xFEDCBA9876543210ULL;
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Position stream at footer for reading (clear state first)
    stream.clear();
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    
    // Read back
    FileFooter copy;
    EXPECT_TRUE(copy.read(stream));
    
    // Verify packet count and properties
    EXPECT_EQ(copy.packetIndex().size(), 4);
    EXPECT_EQ(copy.totalRowCount(), 4000);
    EXPECT_EQ(copy.lastPacketPayloadChecksum(), 0xFEDCBA9876543210ULL);
    
    // Verify packet entries
    EXPECT_EQ(copy.packetIndex()[0].byteOffset_, 100);
    EXPECT_EQ(copy.packetIndex()[0].firstRow_, 0);
    
    EXPECT_EQ(copy.packetIndex()[1].byteOffset_, 5000);
    EXPECT_EQ(copy.packetIndex()[1].firstRow_, 1000);
    
    EXPECT_EQ(copy.packetIndex()[2].byteOffset_, 10000);
    EXPECT_EQ(copy.packetIndex()[2].firstRow_, 2000);
    
    EXPECT_EQ(copy.packetIndex()[3].byteOffset_, 15000);
    EXPECT_EQ(copy.packetIndex()[3].firstRow_, 3000);
}

// Test: hasValidIndex with valid index
TEST_F(FileFooterTest, HasValidIndexTrue) {
    FileFooter index;
    index.packetIndex().emplace_back(100, 0);
    index.totalRowCount() = 1000;
    index.lastPacketPayloadChecksum() = 0x1234567890ABCDEFULL;
    
    std::stringstream stream;
    EXPECT_TRUE(index.write(stream));
    
    // Reset stream to beginning and clear state
    stream.clear();
    stream.seekg(0);
    
    // Read back
    FileFooter copy;
    EXPECT_TRUE(copy.read(stream));

    EXPECT_TRUE(copy.hasValidIndex());
}

// Test: hasValidIndex with no index
TEST_F(FileFooterTest, HasValidIndexFalse) {
    std::stringstream stream;
    stream << "Some random data without an index";
    
    stream.seekg(0);
    FileFooter footer;
    EXPECT_FALSE(footer.read(stream));
    EXPECT_FALSE(footer.hasValidIndex());
}

// Test: Read with corrupted start magic
TEST_F(FileFooterTest, ReadCorruptedStartMagic) {
    FileFooter original;
    original.packetIndex().emplace_back(100, 0);
    original.totalRowCount() = 1000;
    original.lastPacketPayloadChecksum() = 0x1234567890ABCDEFULL;
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Corrupt the start magic
    stream.clear();  // Clear state
    stream.seekp(0);
    stream.write("XXXX", 4);
    
    // Try to read
    stream.clear();  // Clear state again
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    FileFooter copy;
    EXPECT_FALSE(copy.read(stream));
}

// Test: Read with corrupted end magic
TEST_F(FileFooterTest, ReadCorruptedEndMagic) {
    FileFooter original;
    original.packetIndex().emplace_back(100, 0);
    original.totalRowCount() = 1000;
    original.lastPacketPayloadChecksum() = 0x1234567890ABCDEFULL;
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Find and corrupt the end magic
    size_t indexSize = original.encodedSize();
    stream.clear();  // Clear state
    stream.seekp(indexSize - sizeof(FileFooter));
    stream.write("XXXX", 4);
    
    // Try to read
    stream.clear();  // Clear state again
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    FileFooter copy;
    EXPECT_FALSE(copy.read(stream));
}

// Test: Read with corrupted checksum
TEST_F(FileFooterTest, ReadCorruptedChecksum) {
    FileFooter original;
    original.packetIndex().emplace_back(100, 0);
    original.totalRowCount() = 1000;
    original.lastPacketPayloadChecksum() = 0x1234567890ABCDEFULL;
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Corrupt the checksum (last 8 bytes)
    stream.clear();  // Clear state
    stream.seekp(-8, std::ios::end);
    uint64_t badChecksum = 0xDEADBEEFDEADBEEFULL;
    stream.write(reinterpret_cast<const char*>(&badChecksum), 8);
    
    // Try to read
    stream.clear();  // Clear state again
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    FileFooter copy;
    EXPECT_FALSE(copy.read(stream));
}

// Test: Large index (many packets)
TEST_F(FileFooterTest, LargeIndex) {
    FileFooter original;
    
    // Add 1000 packets
    for (uint64_t i = 0; i < 1000; ++i) {
        original.packetIndex().emplace_back(i * 10000, i * 100);
    }
    original.totalRowCount() = 100000;
    original.lastPacketPayloadChecksum() = 0xABCDEF1234567890ULL;
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Expected size: 36 + 1000*16 = 16036 bytes
    EXPECT_EQ(original.encodedSize(), 16036);
    
    // Read back
    stream.clear();  // Clear state
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    FileFooter copy;
    EXPECT_TRUE(copy.read(stream));
    
    // Verify
    EXPECT_EQ(copy.packetIndex().size(), 1000);
    EXPECT_EQ(copy.totalRowCount(), 100000);
    EXPECT_EQ(copy.lastPacketPayloadChecksum(), 0xABCDEF1234567890ULL);
    
    // Spot check some entries
    EXPECT_EQ(copy.packetIndex()[0].byteOffset_, 0);
    EXPECT_EQ(copy.packetIndex()[0].firstRow_, 0);
    
    EXPECT_EQ(copy.packetIndex()[500].byteOffset_, 5000000);
    EXPECT_EQ(copy.packetIndex()[500].firstRow_, 50000);
    
    EXPECT_EQ(copy.packetIndex()[999].byteOffset_, 9990000);
    EXPECT_EQ(copy.packetIndex()[999].firstRow_, 99900);
}

// Test: Get packets vector
TEST_F(FileFooterTest, GetPacketsVector) {
    FileFooter footer;
    
    footer.packetIndex().emplace_back(100, 0);
    footer.packetIndex().emplace_back(5000, 1000);
    footer.packetIndex().emplace_back(10000, 2000);
    
    const auto& packets = footer.packetIndex();
    EXPECT_EQ(packets.size(), 3);
    
    EXPECT_EQ(packets[0].byteOffset_, 100);
    EXPECT_EQ(packets[0].firstRow_, 0);
    
    EXPECT_EQ(packets[1].byteOffset_, 5000);
    EXPECT_EQ(packets[1].firstRow_, 1000);
    
    EXPECT_EQ(packets[2].byteOffset_, 10000);
    EXPECT_EQ(packets[2].firstRow_, 2000);
}

// Test: Footer size constant
TEST_F(FileFooterTest, FooterSizeConstant) {
    EXPECT_EQ(sizeof(FileFooter), 32);
}

// Test: Edge case - maximum values
TEST_F(FileFooterTest, MaximumValues) {
    FileFooter original;
    
    original.packetIndex().emplace_back(UINT64_MAX, UINT64_MAX);
    original.totalRowCount() = UINT64_MAX;
    original.lastPacketPayloadChecksum() = UINT64_MAX;
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    stream.clear();  // Clear state
    stream.seekg(-static_cast<int64_t>(sizeof(FileFooter)), std::ios::end);
    FileFooter copy;
    EXPECT_TRUE(copy.read(stream));
    
    EXPECT_EQ(copy.packetIndex().size(), 1);
    EXPECT_EQ(copy.packetIndex()[0].byteOffset_, UINT64_MAX);
    EXPECT_EQ(copy.packetIndex()[0].firstRow_, UINT64_MAX);
    EXPECT_EQ(copy.totalRowCount(), UINT64_MAX);
    EXPECT_EQ(copy.lastPacketPayloadChecksum(), UINT64_MAX);
}
