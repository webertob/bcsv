/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include <gtest/gtest.h>
#include <sstream>
#include <cstring>

#include <bcsv/file_header.h>
#include <bcsv/file_header.hpp>
#include <bcsv/byte_buffer.h>
#include <bcsv/layout.h>
#include <bcsv/layout.hpp>

using namespace bcsv;

// ==========================================================================
// FileHeader tests
// ==========================================================================

class FileHeaderTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// --- Construction & defaults ---

TEST_F(FileHeaderTest, DefaultConstruction) {
    FileHeader hdr;
    EXPECT_TRUE(hdr.isValidMagic());
    EXPECT_EQ(hdr.versionMajor(), BCSV_FORMAT_VERSION_MAJOR);
    EXPECT_EQ(hdr.versionMinor(), BCSV_FORMAT_VERSION_MINOR);
    EXPECT_EQ(hdr.versionPatch(), BCSV_FORMAT_VERSION_PATCH);
}

TEST_F(FileHeaderTest, VersionString) {
    FileHeader hdr;
    auto vs = hdr.versionString();
    EXPECT_FALSE(vs.empty());
    // Must contain two dots: "M.m.p"
    EXPECT_EQ(std::count(vs.begin(), vs.end(), '.'), 2);
}

TEST_F(FileHeaderTest, SetVersion) {
    FileHeader hdr;
    hdr.setVersion(2, 3, 4);
    EXPECT_EQ(hdr.versionMajor(), 2);
    EXPECT_EQ(hdr.versionMinor(), 3);
    EXPECT_EQ(hdr.versionPatch(), 4);
    EXPECT_EQ(hdr.versionString(), "2.3.4");
}

// --- Compression ---

TEST_F(FileHeaderTest, CompressionLevel_Clamped) {
    FileHeader hdr;
    hdr.setCompressionLevel(0);
    EXPECT_EQ(hdr.getCompressionLevel(), 0);
    hdr.setCompressionLevel(5);
    EXPECT_EQ(hdr.getCompressionLevel(), 5);
    hdr.setCompressionLevel(9);
    EXPECT_EQ(hdr.getCompressionLevel(), 9);
    hdr.setCompressionLevel(100);  // should clamp to 9
    EXPECT_EQ(hdr.getCompressionLevel(), 9);
}

// --- Packet size ---

TEST_F(FileHeaderTest, PacketSize) {
    FileHeader hdr;
    hdr.setPacketSize(65536);
    EXPECT_EQ(hdr.getPacketSize(), 65536u);
    hdr.setPacketSize(0);
    EXPECT_EQ(hdr.getPacketSize(), 0u);
}

// --- Flags ---

TEST_F(FileHeaderTest, FlagOperations) {
    FileHeader hdr;
    EXPECT_FALSE(hdr.hasFlag(FileFlags::ZERO_ORDER_HOLD));

    hdr.setFlag(FileFlags::ZERO_ORDER_HOLD, true);
    EXPECT_TRUE(hdr.hasFlag(FileFlags::ZERO_ORDER_HOLD));

    hdr.clearFlag(FileFlags::ZERO_ORDER_HOLD);
    EXPECT_FALSE(hdr.hasFlag(FileFlags::ZERO_ORDER_HOLD));
}

TEST_F(FileHeaderTest, SetAndGetFlags) {
    FileHeader hdr;
    hdr.setFlags(FileFlags::ZERO_ORDER_HOLD);
    EXPECT_EQ(hdr.getFlags(), FileFlags::ZERO_ORDER_HOLD);
    hdr.setFlags(FileFlags::NONE);
    EXPECT_EQ(hdr.getFlags(), FileFlags::NONE);
}

// --- Magic number ---

TEST_F(FileHeaderTest, MagicNumber) {
    FileHeader hdr;
    EXPECT_TRUE(hdr.isValidMagic());
    EXPECT_EQ(hdr.getMagic(), BCSV_MAGIC);
}

// --- Binary round-trip ---

TEST_F(FileHeaderTest, WriteReadRoundTrip) {
    Layout layout;
    layout.addColumn(ColumnDefinition("x", ColumnType::INT32));
    layout.addColumn(ColumnDefinition("name", ColumnType::STRING));
    layout.addColumn(ColumnDefinition("value", ColumnType::DOUBLE));

    FileHeader writer_hdr(layout.columnCount(), 7);
    writer_hdr.setFlag(FileFlags::ZERO_ORDER_HOLD, true);
    writer_hdr.setPacketSize(32768);

    // Write to stream
    std::ostringstream oss(std::ios::binary);
    writer_hdr.writeToBinary(oss, layout);
    ASSERT_TRUE(oss.good());

    // Read back
    std::string data = oss.str();
    std::istringstream iss(data, std::ios::binary);

    Layout read_layout;
    FileHeader reader_hdr;
    reader_hdr.readFromBinary(iss, read_layout);

    EXPECT_TRUE(reader_hdr.isValidMagic());
    EXPECT_EQ(reader_hdr.getCompressionLevel(), 7);
    EXPECT_TRUE(reader_hdr.hasFlag(FileFlags::ZERO_ORDER_HOLD));
    EXPECT_EQ(reader_hdr.getPacketSize(), 32768u);

    // Verify layout came back correctly
    ASSERT_EQ(read_layout.columnCount(), 3u);
    EXPECT_EQ(read_layout.columnName(0), "x");
    EXPECT_EQ(read_layout.columnName(1), "name");
    EXPECT_EQ(read_layout.columnName(2), "value");
    EXPECT_EQ(read_layout.columnType(0), ColumnType::INT32);
    EXPECT_EQ(read_layout.columnType(1), ColumnType::STRING);
    EXPECT_EQ(read_layout.columnType(2), ColumnType::DOUBLE);
}

TEST_F(FileHeaderTest, BinarySize_MatchesOutput) {
    Layout layout;
    layout.addColumn(ColumnDefinition("a", ColumnType::BOOL));
    layout.addColumn(ColumnDefinition("longer_name", ColumnType::FLOAT));

    size_t expected = FileHeader::getBinarySize(layout);

    FileHeader hdr(layout.columnCount(), 0);
    std::ostringstream oss(std::ios::binary);
    hdr.writeToBinary(oss, layout);

    EXPECT_EQ(oss.str().size(), expected);
}

TEST_F(FileHeaderTest, ReadFromBinary_InvalidMagic) {
    // Create a stream with garbage data
    std::string garbage(64, '\0');
    std::istringstream iss(garbage, std::ios::binary);

    Layout layout;
    FileHeader hdr;
    EXPECT_THROW(hdr.readFromBinary(iss, layout), std::runtime_error);
}

// ==========================================================================
// ByteBuffer (LazyAllocator) tests
// ==========================================================================

class ByteBufferTest : public ::testing::Test {};

TEST_F(ByteBufferTest, DefaultConstruction) {
    ByteBuffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

TEST_F(ByteBufferTest, ResizeDoesNotThrow) {
    ByteBuffer buf;
    EXPECT_NO_THROW(buf.resize(1024));
    EXPECT_EQ(buf.size(), 1024u);
}

TEST_F(ByteBufferTest, WriteThenRead) {
    ByteBuffer buf;
    buf.resize(8);
    uint64_t val = 0xDEADBEEF12345678ULL;
    std::memcpy(buf.data(), &val, sizeof(val));

    uint64_t readback = 0;
    std::memcpy(&readback, buf.data(), sizeof(readback));
    EXPECT_EQ(readback, val);
}

TEST_F(ByteBufferTest, PushBack) {
    ByteBuffer buf;
    buf.push_back(std::byte{0xAA});
    buf.push_back(std::byte{0xBB});
    EXPECT_EQ(buf.size(), 2u);
    EXPECT_EQ(buf[0], std::byte{0xAA});
    EXPECT_EQ(buf[1], std::byte{0xBB});
}

TEST_F(ByteBufferTest, Clear) {
    ByteBuffer buf;
    buf.resize(256);
    EXPECT_EQ(buf.size(), 256u);
    buf.clear();
    EXPECT_EQ(buf.size(), 0u);
}

TEST_F(ByteBufferTest, Reserve) {
    ByteBuffer buf;
    buf.reserve(4096);
    EXPECT_GE(buf.capacity(), 4096u);
    EXPECT_EQ(buf.size(), 0u);
}

TEST_F(ByteBufferTest, LargeAllocation) {
    ByteBuffer buf;
    // 1 MB should work without issue
    EXPECT_NO_THROW(buf.resize(1024 * 1024));
    EXPECT_EQ(buf.size(), 1024u * 1024u);
}

TEST_F(ByteBufferTest, AllocatorEquality) {
    LazyAllocator<std::byte> a1, a2;
    EXPECT_EQ(a1, a2);
    EXPECT_FALSE(a1 != a2);
}

TEST_F(ByteBufferTest, MaxSize) {
    LazyAllocator<std::byte> alloc;
    EXPECT_GT(alloc.max_size(), 0u);
}
