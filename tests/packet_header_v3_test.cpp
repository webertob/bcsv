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
#include <bcsv/packet_header_v3.h>

using namespace bcsv;

class PacketHeaderV3Test : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test: Basic construction and magic number
TEST_F(PacketHeaderV3Test, DefaultConstruction) {
    PacketHeaderV3 header;
    
    EXPECT_TRUE(header.isValidMagic());
    EXPECT_EQ(header.firstRowIndex, 0);
    EXPECT_EQ(header.prevPayloadChecksum, 0);
}

// Test: Parametrized construction
TEST_F(PacketHeaderV3Test, ParametrizedConstruction) {
    PacketHeaderV3 header(1000, 0x123456789ABCDEF0ULL);
    
    EXPECT_TRUE(header.isValidMagic());
    EXPECT_EQ(header.firstRowIndex, 1000);
    EXPECT_EQ(header.prevPayloadChecksum, 0x123456789ABCDEF0ULL);
    EXPECT_TRUE(header.validateHeaderChecksum());
}

// Test: Size validation
TEST_F(PacketHeaderV3Test, SizeValidation) {
    EXPECT_EQ(sizeof(PacketHeaderV3), 24);
}

// Test: Header checksum calculation
TEST_F(PacketHeaderV3Test, HeaderChecksumCalculation) {
    PacketHeaderV3 header(42, 0);
    
    // Store original checksum
    uint32_t original = header.headerChecksum;
    EXPECT_NE(original, 0);
    
    // Corrupt checksum
    header.headerChecksum = 0xDEADBEEF;
    EXPECT_FALSE(header.validateHeaderChecksum());
    
    // Recalculate
    header.updateHeaderChecksum();
    EXPECT_EQ(header.headerChecksum, original);
    EXPECT_TRUE(header.validateHeaderChecksum());
}

// Test: Magic number validation
TEST_F(PacketHeaderV3Test, MagicNumberValidation) {
    PacketHeaderV3 header;
    header.updateHeaderChecksum(); // Ensure checksum is valid first
    
    EXPECT_TRUE(header.isValidMagic());
    EXPECT_TRUE(header.validate());
    
    // Corrupt magic number - this invalidates both magic and checksum
    header.magic[0] = 'X';
    EXPECT_FALSE(header.isValidMagic());
    EXPECT_FALSE(header.validate());
}

// Test: Full validation (magic + checksum)
TEST_F(PacketHeaderV3Test, FullValidation) {
    PacketHeaderV3 header(999, 0x1234567890ABCDEFULL);
    
    EXPECT_TRUE(header.validate());
    
    // Test with corrupted checksum
    header.headerChecksum = 0;
    EXPECT_FALSE(header.validate());
    
    // Fix checksum
    header.updateHeaderChecksum();
    EXPECT_TRUE(header.validate());
    
    // Test with corrupted magic
    header.magic[3] = 'X';
    EXPECT_FALSE(header.validate());
}

// Test: Binary I/O round-trip
TEST_F(PacketHeaderV3Test, BinaryIOroundTrip) {
    PacketHeaderV3 original(12345, 0xFEDCBA9876543210ULL);
    
    // Write to stream
    std::stringstream stream;
    EXPECT_TRUE(original.write(stream));
    
    // Read back
    PacketHeaderV3 copy;
    stream.seekg(0);
    EXPECT_TRUE(copy.read(stream));
    
    // Verify fields match
    EXPECT_EQ(copy.firstRowIndex, original.firstRowIndex);
    EXPECT_EQ(copy.prevPayloadChecksum, original.prevPayloadChecksum);
    EXPECT_EQ(copy.headerChecksum, original.headerChecksum);
    EXPECT_TRUE(copy.validate());
}

// Test: Checksum chain simulation
TEST_F(PacketHeaderV3Test, ChecksumChainSimulation) {
    // Simulate writing 3 packets
    uint64_t packet1Checksum = 0xAAAAAAAAAAAAAAAAULL;
    uint64_t packet2Checksum = 0xBBBBBBBBBBBBBBBBULL;
    
    // Packet 1: prevPayloadChecksum = 0 (first packet)
    PacketHeaderV3 packet1(0, 0);
    EXPECT_EQ(packet1.prevPayloadChecksum, 0);
    EXPECT_TRUE(packet1.validate());
    
    // Packet 2: prevPayloadChecksum = packet1's payload checksum
    PacketHeaderV3 packet2(1000, packet1Checksum);
    EXPECT_EQ(packet2.prevPayloadChecksum, packet1Checksum);
    EXPECT_TRUE(packet2.validate());
    
    // Packet 3: prevPayloadChecksum = packet2's payload checksum
    PacketHeaderV3 packet3(2000, packet2Checksum);
    EXPECT_EQ(packet3.prevPayloadChecksum, packet2Checksum);
    EXPECT_TRUE(packet3.validate());
    
    // Verify row index progression
    EXPECT_EQ(packet1.firstRowIndex, 0);
    EXPECT_EQ(packet2.firstRowIndex, 1000);
    EXPECT_EQ(packet3.firstRowIndex, 2000);
}

// Test: Read with invalid magic
TEST_F(PacketHeaderV3Test, ReadInvalidMagic) {
    std::stringstream stream;
    
    // Write corrupted header
    PacketHeaderV3 corrupted;
    corrupted.magic[0] = 'X';
    corrupted.updateHeaderChecksum();
    stream.write(reinterpret_cast<const char*>(&corrupted), sizeof(PacketHeaderV3));
    
    // Try to read
    PacketHeaderV3 header;
    stream.seekg(0);
    EXPECT_FALSE(header.read(stream));
}

// Test: Read with invalid checksum
TEST_F(PacketHeaderV3Test, ReadInvalidChecksum) {
    std::stringstream stream;
    
    // Write header with corrupted checksum
    PacketHeaderV3 corrupted(100, 0);
    corrupted.headerChecksum = 0xDEADBEEF;
    stream.write(reinterpret_cast<const char*>(&corrupted), sizeof(PacketHeaderV3));
    
    // Try to read
    PacketHeaderV3 header;
    stream.seekg(0);
    EXPECT_FALSE(header.read(stream));
}

// Test: Edge case - maximum row index
TEST_F(PacketHeaderV3Test, MaximumRowIndex) {
    PacketHeaderV3 header(UINT64_MAX, UINT64_MAX);
    
    EXPECT_EQ(header.firstRowIndex, UINT64_MAX);
    EXPECT_EQ(header.prevPayloadChecksum, UINT64_MAX);
    EXPECT_TRUE(header.validate());
}

// Test: Edge case - zero values
TEST_F(PacketHeaderV3Test, ZeroValues) {
    PacketHeaderV3 header(0, 0);
    
    EXPECT_EQ(header.firstRowIndex, 0);
    EXPECT_EQ(header.prevPayloadChecksum, 0);
    EXPECT_TRUE(header.validate());
}

// Test: Memory layout (no padding)
TEST_F(PacketHeaderV3Test, MemoryLayout) {
    PacketHeaderV3 header;
    
    // Check offsets using pointer arithmetic
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&header);
    const uint8_t* magic_ptr = reinterpret_cast<const uint8_t*>(&header.magic);
    const uint8_t* firstRow_ptr = reinterpret_cast<const uint8_t*>(&header.firstRowIndex);
    const uint8_t* prevChecksum_ptr = reinterpret_cast<const uint8_t*>(&header.prevPayloadChecksum);
    const uint8_t* headerChecksum_ptr = reinterpret_cast<const uint8_t*>(&header.headerChecksum);
    
    EXPECT_EQ(magic_ptr - base, 0);           // Offset 0
    EXPECT_EQ(firstRow_ptr - base, 4);        // Offset 4
    EXPECT_EQ(prevChecksum_ptr - base, 12);   // Offset 12
    EXPECT_EQ(headerChecksum_ptr - base, 20); // Offset 20
}
