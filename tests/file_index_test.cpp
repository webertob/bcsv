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
#include <bcsv/file_index.h>

using namespace bcsv;

class FileIndexTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test: PacketIndexEntry size validation
TEST_F(FileIndexTest, PacketIndexEntrySize) {
    EXPECT_EQ(sizeof(PacketIndexEntry), 16);
}

// Test: PacketIndexEntry construction
TEST_F(FileIndexTest, PacketIndexEntryConstruction) {
    PacketIndexEntry entry1;
    EXPECT_EQ(entry1.headerOffset, 0);
    EXPECT_EQ(entry1.firstRowIndex, 0);
    
    PacketIndexEntry entry2(1000, 5000);
    EXPECT_EQ(entry2.headerOffset, 1000);
    EXPECT_EQ(entry2.firstRowIndex, 5000);
}

// Test: FileIndex default construction
TEST_F(FileIndexTest, DefaultConstruction) {
    FileIndex index;
    
    EXPECT_EQ(index.packetCount(), 0);
    EXPECT_EQ(index.getTotalRowCount(), 0);
    EXPECT_EQ(index.getLastPacketPayloadChecksum(), 0);
}

// Test: Adding packets to index
TEST_F(FileIndexTest, AddPackets) {
    FileIndex index;
    
    index.addPacket(100, 0);
    index.addPacket(5000, 1000);
    index.addPacket(10000, 2000);
    
    EXPECT_EQ(index.packetCount(), 3);
    
    EXPECT_EQ(index.getPacket(0).headerOffset, 100);
    EXPECT_EQ(index.getPacket(0).firstRowIndex, 0);
    
    EXPECT_EQ(index.getPacket(1).headerOffset, 5000);
    EXPECT_EQ(index.getPacket(1).firstRowIndex, 1000);
    
    EXPECT_EQ(index.getPacket(2).headerOffset, 10000);
    EXPECT_EQ(index.getPacket(2).firstRowIndex, 2000);
}

// Test: Setting and getting properties
TEST_F(FileIndexTest, SetGetProperties) {
    FileIndex index;
    
    index.setTotalRowCount(12345);
    EXPECT_EQ(index.getTotalRowCount(), 12345);
    
    index.setLastPacketPayloadChecksum(0xABCDEF1234567890ULL);
    EXPECT_EQ(index.getLastPacketPayloadChecksum(), 0xABCDEF1234567890ULL);
}

// Test: Calculate size
TEST_F(FileIndexTest, CalculateSize) {
    FileIndex index;
    
    // Empty index: 4 (BIDX) + 4 (EIDX) + 4 (offset) + 8 (checksum) + 8 (rows) + 8 (index checksum) = 36 bytes
    EXPECT_EQ(index.calculateSize(), 36);
    
    // Add 1 packet: 36 + 16 = 52 bytes
    index.addPacket(100, 0);
    EXPECT_EQ(index.calculateSize(), 52);
    
    // Add 2 more packets: 36 + 48 = 84 bytes
    index.addPacket(5000, 1000);
    index.addPacket(10000, 2000);
    EXPECT_EQ(index.calculateSize(), 84);
}

// Test: Clear index
TEST_F(FileIndexTest, Clear) {
    FileIndex index;
    
    index.addPacket(100, 0);
    index.addPacket(5000, 1000);
    index.setTotalRowCount(2000);
    index.setLastPacketPayloadChecksum(0x1234567890ABCDEFULL);
    
    EXPECT_EQ(index.packetCount(), 2);
    EXPECT_EQ(index.getTotalRowCount(), 2000);
    
    index.clear();
    
    EXPECT_EQ(index.packetCount(), 0);
    EXPECT_EQ(index.getTotalRowCount(), 0);
    EXPECT_EQ(index.getLastPacketPayloadChecksum(), 0);
}

// Test: Write and read empty index
TEST_F(FileIndexTest, WriteReadEmptyIndex) {
    FileIndex original;
    original.setTotalRowCount(0);
    original.setLastPacketPayloadChecksum(0);
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Position stream at footer for reading (clear state first)
    stream.clear();  // Clear any error flags
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    
    // Read back
    FileIndex copy;
    EXPECT_TRUE(copy.read(stream));
    
    EXPECT_EQ(copy.packetCount(), 0);
    EXPECT_EQ(copy.getTotalRowCount(), 0);
    EXPECT_EQ(copy.getLastPacketPayloadChecksum(), 0);
}

// Test: Write and read index with packets
TEST_F(FileIndexTest, WriteReadWithPackets) {
    FileIndex original;
    
    original.addPacket(100, 0);
    original.addPacket(5000, 1000);
    original.addPacket(10000, 2000);
    original.addPacket(15000, 3000);
    original.setTotalRowCount(4000);
    original.setLastPacketPayloadChecksum(0xFEDCBA9876543210ULL);
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Position stream at footer for reading (clear state first)
    stream.clear();
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    
    // Read back
    FileIndex copy;
    EXPECT_TRUE(copy.read(stream));
    
    // Verify packet count and properties
    EXPECT_EQ(copy.packetCount(), 4);
    EXPECT_EQ(copy.getTotalRowCount(), 4000);
    EXPECT_EQ(copy.getLastPacketPayloadChecksum(), 0xFEDCBA9876543210ULL);
    
    // Verify packet entries
    EXPECT_EQ(copy.getPacket(0).headerOffset, 100);
    EXPECT_EQ(copy.getPacket(0).firstRowIndex, 0);
    
    EXPECT_EQ(copy.getPacket(1).headerOffset, 5000);
    EXPECT_EQ(copy.getPacket(1).firstRowIndex, 1000);
    
    EXPECT_EQ(copy.getPacket(2).headerOffset, 10000);
    EXPECT_EQ(copy.getPacket(2).firstRowIndex, 2000);
    
    EXPECT_EQ(copy.getPacket(3).headerOffset, 15000);
    EXPECT_EQ(copy.getPacket(3).firstRowIndex, 3000);
}

// Test: hasValidIndex with valid index
TEST_F(FileIndexTest, HasValidIndexTrue) {
    FileIndex index;
    index.addPacket(100, 0);
    index.setTotalRowCount(1000);
    index.setLastPacketPayloadChecksum(0x1234567890ABCDEFULL);
    
    std::stringstream stream;
    EXPECT_TRUE(index.write(stream));
    
    // Reset stream to beginning and clear state
    stream.clear();
    stream.seekg(0);
    
    EXPECT_TRUE(FileIndex::hasValidIndex(stream));
}

// Test: hasValidIndex with no index
TEST_F(FileIndexTest, HasValidIndexFalse) {
    std::stringstream stream;
    stream << "Some random data without an index";
    
    stream.seekg(0);
    EXPECT_FALSE(FileIndex::hasValidIndex(stream));
}

// Test: Read with corrupted start magic
TEST_F(FileIndexTest, ReadCorruptedStartMagic) {
    FileIndex original;
    original.addPacket(100, 0);
    original.setTotalRowCount(1000);
    original.setLastPacketPayloadChecksum(0x1234567890ABCDEFULL);
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Corrupt the start magic
    stream.clear();  // Clear state
    stream.seekp(0);
    stream.write("XXXX", 4);
    
    // Try to read
    stream.clear();  // Clear state again
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    FileIndex copy;
    EXPECT_FALSE(copy.read(stream));
}

// Test: Read with corrupted end magic
TEST_F(FileIndexTest, ReadCorruptedEndMagic) {
    FileIndex original;
    original.addPacket(100, 0);
    original.setTotalRowCount(1000);
    original.setLastPacketPayloadChecksum(0x1234567890ABCDEFULL);
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Find and corrupt the end magic
    size_t indexSize = original.calculateSize();
    stream.clear();  // Clear state
    stream.seekp(indexSize - FileIndex::FOOTER_SIZE);
    stream.write("XXXX", 4);
    
    // Try to read
    stream.clear();  // Clear state again
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    FileIndex copy;
    EXPECT_FALSE(copy.read(stream));
}

// Test: Read with corrupted checksum
TEST_F(FileIndexTest, ReadCorruptedChecksum) {
    FileIndex original;
    original.addPacket(100, 0);
    original.setTotalRowCount(1000);
    original.setLastPacketPayloadChecksum(0x1234567890ABCDEFULL);
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Corrupt the checksum (last 8 bytes)
    stream.clear();  // Clear state
    stream.seekp(-8, std::ios::end);
    uint64_t badChecksum = 0xDEADBEEFDEADBEEFULL;
    stream.write(reinterpret_cast<const char*>(&badChecksum), 8);
    
    // Try to read
    stream.clear();  // Clear state again
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    FileIndex copy;
    EXPECT_FALSE(copy.read(stream));
}

// Test: Large index (many packets)
TEST_F(FileIndexTest, LargeIndex) {
    FileIndex original;
    
    // Add 1000 packets
    for (uint64_t i = 0; i < 1000; ++i) {
        original.addPacket(i * 10000, i * 100);
    }
    original.setTotalRowCount(100000);
    original.setLastPacketPayloadChecksum(0xABCDEF1234567890ULL);
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Expected size: 36 + 1000*16 = 16036 bytes
    EXPECT_EQ(original.calculateSize(), 16036);
    
    // Read back
    stream.clear();  // Clear state
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    FileIndex copy;
    EXPECT_TRUE(copy.read(stream));
    
    // Verify
    EXPECT_EQ(copy.packetCount(), 1000);
    EXPECT_EQ(copy.getTotalRowCount(), 100000);
    EXPECT_EQ(copy.getLastPacketPayloadChecksum(), 0xABCDEF1234567890ULL);
    
    // Spot check some entries
    EXPECT_EQ(copy.getPacket(0).headerOffset, 0);
    EXPECT_EQ(copy.getPacket(0).firstRowIndex, 0);
    
    EXPECT_EQ(copy.getPacket(500).headerOffset, 5000000);
    EXPECT_EQ(copy.getPacket(500).firstRowIndex, 50000);
    
    EXPECT_EQ(copy.getPacket(999).headerOffset, 9990000);
    EXPECT_EQ(copy.getPacket(999).firstRowIndex, 99900);
}

// Test: Get packets vector
TEST_F(FileIndexTest, GetPacketsVector) {
    FileIndex index;
    
    index.addPacket(100, 0);
    index.addPacket(5000, 1000);
    index.addPacket(10000, 2000);
    
    const auto& packets = index.getPackets();
    EXPECT_EQ(packets.size(), 3);
    
    EXPECT_EQ(packets[0].headerOffset, 100);
    EXPECT_EQ(packets[0].firstRowIndex, 0);
    
    EXPECT_EQ(packets[1].headerOffset, 5000);
    EXPECT_EQ(packets[1].firstRowIndex, 1000);
    
    EXPECT_EQ(packets[2].headerOffset, 10000);
    EXPECT_EQ(packets[2].firstRowIndex, 2000);
}

// Test: Footer size constant
TEST_F(FileIndexTest, FooterSizeConstant) {
    EXPECT_EQ(FileIndex::FOOTER_SIZE, 32);
}

// Test: Edge case - maximum values
TEST_F(FileIndexTest, MaximumValues) {
    FileIndex original;
    
    original.addPacket(UINT64_MAX, UINT64_MAX);
    original.setTotalRowCount(UINT64_MAX);
    original.setLastPacketPayloadChecksum(UINT64_MAX);
    
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    stream.clear();  // Clear state
    stream.seekg(-static_cast<int64_t>(FileIndex::FOOTER_SIZE), std::ios::end);
    FileIndex copy;
    EXPECT_TRUE(copy.read(stream));
    
    EXPECT_EQ(copy.packetCount(), 1);
    EXPECT_EQ(copy.getPacket(0).headerOffset, UINT64_MAX);
    EXPECT_EQ(copy.getPacket(0).firstRowIndex, UINT64_MAX);
    EXPECT_EQ(copy.getTotalRowCount(), UINT64_MAX);
    EXPECT_EQ(copy.getLastPacketPayloadChecksum(), UINT64_MAX);
}
