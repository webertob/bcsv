/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file file_codec_test.cpp
 * @brief Tests for FileCodec integration with Writer/Reader (Item 12).
 *
 * Tests cover:
 * - Round-trip write/read for all 5 implemented file codecs
 *     (Stream001, StreamLZ4001, Packet001, PacketLZ4001, PacketLZ4Batch001)
 * - Both Flat and ZoH row codecs with each file codec
 * - Multi-packet round-trip (packet codecs with small block size)
 * - Empty file round-trip
 * - ZoH repeat handling (zero-length rows)
 * - Sentinel identity checks (ZOH_REPEAT_SENTINEL, EOF_SENTINEL)
 * - resolveFileCodecId mapping (including BATCH_COMPRESS flag)
 * - FileCodecDispatch lifecycle
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <bcsv/bcsv.h>

using namespace bcsv;
namespace fs = std::filesystem;

// ============================================================================
// Helpers
// ============================================================================

class FileCodecTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = (fs::temp_directory_path() / "bcsv_file_codec_test").string();
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        fs::remove_all(test_dir_);
    }

    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }

    Layout makeLayout() {
        Layout layout;
        layout.addColumn({"i", ColumnType::INT32});
        layout.addColumn({"f", ColumnType::FLOAT});
        layout.addColumn({"s", ColumnType::STRING});
        return layout;
    }

    std::string test_dir_;
};

// ============================================================================
// resolveFileCodecId mapping
// ============================================================================

TEST_F(FileCodecTest, ResolveFileCodecId_PacketLZ4_Default) {
    // Default: compression > 0, no STREAM_MODE → PACKET_LZ4_001
    EXPECT_EQ(resolveFileCodecId(1, FileFlags::NONE), FileCodecId::PACKET_LZ4_001);
    EXPECT_EQ(resolveFileCodecId(9, FileFlags::NONE), FileCodecId::PACKET_LZ4_001);
    EXPECT_EQ(resolveFileCodecId(5, FileFlags::ZERO_ORDER_HOLD), FileCodecId::PACKET_LZ4_001);
}

TEST_F(FileCodecTest, ResolveFileCodecId_PacketRaw) {
    // compression = 0, no STREAM_MODE → PACKET_001
    EXPECT_EQ(resolveFileCodecId(0, FileFlags::NONE), FileCodecId::PACKET_001);
    EXPECT_EQ(resolveFileCodecId(0, FileFlags::ZERO_ORDER_HOLD), FileCodecId::PACKET_001);
}

TEST_F(FileCodecTest, ResolveFileCodecId_StreamRaw) {
    // compression = 0, STREAM_MODE → STREAM_001
    EXPECT_EQ(resolveFileCodecId(0, FileFlags::STREAM_MODE), FileCodecId::STREAM_001);
}

TEST_F(FileCodecTest, ResolveFileCodecId_StreamLZ4) {
    // compression > 0, STREAM_MODE → STREAM_LZ4_001
    EXPECT_EQ(resolveFileCodecId(1, FileFlags::STREAM_MODE), FileCodecId::STREAM_LZ4_001);
    EXPECT_EQ(resolveFileCodecId(9, FileFlags::STREAM_MODE), FileCodecId::STREAM_LZ4_001);
}

// ============================================================================
// Sentinel identity
// ============================================================================

TEST_F(FileCodecTest, Sentinels_AreDistinct) {
    // ZOH_REPEAT_SENTINEL and EOF_SENTINEL must have distinct .data() pointers
    EXPECT_NE(ZOH_REPEAT_SENTINEL.data(), EOF_SENTINEL.data());
    EXPECT_NE(ZOH_REPEAT_SENTINEL.data(), nullptr);
    EXPECT_NE(EOF_SENTINEL.data(), nullptr);
}

// ============================================================================
// FileCodecDispatch lifecycle
// ============================================================================

TEST_F(FileCodecTest, Dispatch_IsSetup_AfterSelect) {
    FileCodecDispatch d;
    EXPECT_FALSE(d.isSetup());

    d.select(1, FileFlags::NONE);
    EXPECT_TRUE(d.isSetup());
    EXPECT_EQ(d.codecId(), FileCodecId::PACKET_LZ4_001);

    d.destroy();
    EXPECT_FALSE(d.isSetup());
}

TEST_F(FileCodecTest, Dispatch_BeginWrite_Finalize) {
    // Verify beginWrite + finalize work through dispatch for all codecs
    FileCodecDispatch d;

    // Stream codec: beginWrite always returns false (no packet boundaries)
    d.setup(FileCodecId::STREAM_001);
    {
        std::ostringstream os;
        FileHeader header(makeLayout().columnCount(), 0);
        header.setFlags(FileFlags::STREAM_MODE);
        d.setupWrite(os, header);
        EXPECT_FALSE(d.beginWrite(os, 0));
        EXPECT_FALSE(d.beginWrite(os, 1));
        d.finalize(os, 0);
    }
    d.destroy();

    // Packet codec: beginWrite returns false for first row, may return true later
    d.setup(FileCodecId::PACKET_001);
    {
        std::ostringstream os;
        FileHeader header(makeLayout().columnCount(), 0);
        d.setupWrite(os, header);
        EXPECT_FALSE(d.beginWrite(os, 0));  // First packet open, not a boundary crossing
        d.finalize(os, 0);
    }
    d.destroy();
}

TEST_F(FileCodecTest, Dispatch_BatchConstructs) {
    FileCodecDispatch d;
#ifdef BCSV_HAS_BATCH_CODEC
    EXPECT_NO_THROW(d.setup(FileCodecId::PACKET_LZ4_BATCH_001));
    EXPECT_TRUE(d.isSetup());
    EXPECT_EQ(d.codecId(), FileCodecId::PACKET_LZ4_BATCH_001);
    d.destroy();
#else
    EXPECT_THROW(d.setup(FileCodecId::PACKET_LZ4_BATCH_001), std::logic_error);
#endif
}

// ============================================================================
// Round-trip helpers
// ============================================================================

/// Write N rows with Flat codec + specified file codec flags, verify round-trip.
template<typename WriterType>
void roundTripFlat(const std::string& path, Layout layout,
                   size_t numRows, size_t compressionLevel,
                   FileFlags flags, size_t blockSizeKB = 64) {
    // Write
    {
        WriterType writer(layout);
        ASSERT_TRUE(writer.open(path, true, compressionLevel, blockSizeKB, flags))
            << writer.getErrorMsg();
        for (size_t i = 0; i < numRows; ++i) {
            writer.row().set(0, static_cast<int32_t>(i));
            writer.row().set(1, static_cast<float>(i) * 0.5f);
            writer.row().set(2, std::string("r") + std::to_string(i));
            writer.writeRow();
        }
        writer.close();
    }

    // Read
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

        for (size_t i = 0; i < numRows; ++i) {
            ASSERT_TRUE(reader.readNext()) << "Failed at row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(0), static_cast<int32_t>(i));
            EXPECT_FLOAT_EQ(reader.row().get<float>(1), static_cast<float>(i) * 0.5f);
            EXPECT_EQ(reader.row().get<std::string>(2), std::string("r") + std::to_string(i));
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

/// Write N rows with ZoH codec + specified file codec flags, verify round-trip.
template<typename WriterType>
void roundTripZoH(const std::string& path, Layout layout,
                  size_t numRows, size_t compressionLevel,
                  FileFlags flags, size_t blockSizeKB = 64) {
    // Write — deliberately repeat some rows to exercise ZoH
    {
        WriterType writer(layout);
        FileFlags zohFlags = flags | FileFlags::ZERO_ORDER_HOLD;
        ASSERT_TRUE(writer.open(path, true, compressionLevel, blockSizeKB, zohFlags))
            << writer.getErrorMsg();
        for (size_t i = 0; i < numRows; ++i) {
            // Change only every 3rd row → ZoH repeats the other 2
            if (i % 3 == 0) {
                writer.row().set(0, static_cast<int32_t>(i));
                writer.row().set(1, static_cast<float>(i) * 0.5f);
                writer.row().set(2, std::string("z") + std::to_string(i));
            }
            writer.writeRow();
        }
        writer.close();
    }

    // Read
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

        int32_t expectedI = 0;
        float expectedF = 0.0f;
        std::string expectedS = "z0";
        for (size_t i = 0; i < numRows; ++i) {
            if (i % 3 == 0) {
                expectedI = static_cast<int32_t>(i);
                expectedF = static_cast<float>(i) * 0.5f;
                expectedS = std::string("z") + std::to_string(i);
            }
            ASSERT_TRUE(reader.readNext()) << "Failed at row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(0), expectedI) << "Row " << i;
            EXPECT_FLOAT_EQ(reader.row().get<float>(1), expectedF) << "Row " << i;
            EXPECT_EQ(reader.row().get<std::string>(2), expectedS) << "Row " << i;
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// PacketLZ4001 (default, v1.3.0 compatible)
// ============================================================================

TEST_F(FileCodecTest, RoundTrip_PacketLZ4_Flat) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("pkt_lz4_flat.bcsv"), makeLayout(), 100, 1, FileFlags::NONE);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4_ZoH) {
    roundTripZoH<WriterZoH<Layout>>(
        testFile("pkt_lz4_zoh.bcsv"), makeLayout(), 100, 1, FileFlags::NONE);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4_MultiPacket) {
    // Force many packets with tiny block size (1 KB)
    roundTripFlat<WriterFlat<Layout>>(
        testFile("pkt_lz4_multi.bcsv"), makeLayout(), 500, 1, FileFlags::NONE, 1);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4_Empty) {
    auto path = testFile("pkt_lz4_empty.bcsv");
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 1, 64, FileFlags::NONE));
        writer.close();
    }
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// Packet001 (raw, no compression)
// ============================================================================

TEST_F(FileCodecTest, RoundTrip_PacketRaw_Flat) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("pkt_raw_flat.bcsv"), makeLayout(), 100, 0, FileFlags::NONE);
}

TEST_F(FileCodecTest, RoundTrip_PacketRaw_ZoH) {
    roundTripZoH<WriterZoH<Layout>>(
        testFile("pkt_raw_zoh.bcsv"), makeLayout(), 100, 0, FileFlags::NONE);
}

TEST_F(FileCodecTest, RoundTrip_PacketRaw_MultiPacket) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("pkt_raw_multi.bcsv"), makeLayout(), 500, 0, FileFlags::NONE, 1);
}

TEST_F(FileCodecTest, RoundTrip_PacketRaw_Empty) {
    auto path = testFile("pkt_raw_empty.bcsv");
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 0, 64, FileFlags::NONE));
        writer.close();
    }
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// Stream001 (raw, no packets)
// ============================================================================

TEST_F(FileCodecTest, RoundTrip_StreamRaw_Flat) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("str_raw_flat.bcsv"), makeLayout(), 100, 0, FileFlags::STREAM_MODE);
}

TEST_F(FileCodecTest, RoundTrip_StreamRaw_ZoH) {
    roundTripZoH<WriterZoH<Layout>>(
        testFile("str_raw_zoh.bcsv"), makeLayout(), 100, 0, FileFlags::STREAM_MODE);
}

TEST_F(FileCodecTest, RoundTrip_StreamRaw_Empty) {
    auto path = testFile("str_raw_empty.bcsv");
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 0, 64, FileFlags::STREAM_MODE));
        writer.close();
    }
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// StreamLZ4001 (no packets, LZ4 compression)
// ============================================================================

TEST_F(FileCodecTest, RoundTrip_StreamLZ4_Flat) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("str_lz4_flat.bcsv"), makeLayout(), 100, 1, FileFlags::STREAM_MODE);
}

TEST_F(FileCodecTest, RoundTrip_StreamLZ4_ZoH) {
    roundTripZoH<WriterZoH<Layout>>(
        testFile("str_lz4_zoh.bcsv"), makeLayout(), 100, 1, FileFlags::STREAM_MODE);
}

TEST_F(FileCodecTest, RoundTrip_StreamLZ4_Empty) {
    auto path = testFile("str_lz4_empty.bcsv");
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 1, 64, FileFlags::STREAM_MODE));
        writer.close();
    }
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// Delta codec: file round-trips
// ============================================================================

/// Write N rows with Delta codec + specified file codec flags, verify round-trip.
template<typename WriterType>
void roundTripDelta(const std::string& path, Layout layout,
                    size_t numRows, size_t compressionLevel,
                    FileFlags flags, size_t blockSizeKB = 64) {
    // Write — vary values to exercise delta/ZoH/FoC modes
    {
        WriterType writer(layout);
        FileFlags deltaFlags = flags | FileFlags::DELTA_ENCODING;
        ASSERT_TRUE(writer.open(path, true, compressionLevel, blockSizeKB, deltaFlags))
            << writer.getErrorMsg();
        for (size_t i = 0; i < numRows; ++i) {
            writer.row().set(0, static_cast<int32_t>(i));
            writer.row().set(1, static_cast<float>(i) * 0.1f);
            if (i % 5 == 0) {
                writer.row().set(2, std::string("d") + std::to_string(i));
            }
            writer.writeRow();
        }
        writer.close();
    }

    // Read
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

        std::string expectedS = "d0";
        for (size_t i = 0; i < numRows; ++i) {
            if (i % 5 == 0) {
                expectedS = std::string("d") + std::to_string(i);
            }
            ASSERT_TRUE(reader.readNext()) << "Failed at row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(0), static_cast<int32_t>(i)) << "Row " << i;
            EXPECT_FLOAT_EQ(reader.row().get<float>(1), static_cast<float>(i) * 0.1f) << "Row " << i;
            EXPECT_EQ(reader.row().get<std::string>(2), expectedS) << "Row " << i;
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4_Delta) {
    roundTripDelta<WriterDelta<Layout>>(
        testFile("pkt_lz4_delta.bcsv"), makeLayout(), 100, 1, FileFlags::NONE);
}

TEST_F(FileCodecTest, RoundTrip_PacketRaw_Delta) {
    roundTripDelta<WriterDelta<Layout>>(
        testFile("pkt_raw_delta.bcsv"), makeLayout(), 100, 0, FileFlags::NONE);
}

TEST_F(FileCodecTest, RoundTrip_StreamRaw_Delta) {
    roundTripDelta<WriterDelta<Layout>>(
        testFile("str_raw_delta.bcsv"), makeLayout(), 100, 0, FileFlags::STREAM_MODE);
}

TEST_F(FileCodecTest, RoundTrip_StreamLZ4_Delta) {
    roundTripDelta<WriterDelta<Layout>>(
        testFile("str_lz4_delta.bcsv"), makeLayout(), 100, 1, FileFlags::STREAM_MODE);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4_Delta_MultiPacket) {
    // Tiny block size to force multiple packets
    roundTripDelta<WriterDelta<Layout>>(
        testFile("pkt_lz4_delta_multi.bcsv"), makeLayout(), 500, 1, FileFlags::NONE, 1);
}

// ============================================================================
// PacketLZ4Batch001 (async double-buffered batch LZ4)
// ============================================================================

TEST_F(FileCodecTest, ResolveFileCodecId_BatchCompress) {
    // BATCH_COMPRESS + compression > 0 → PACKET_LZ4_BATCH_001
    EXPECT_EQ(resolveFileCodecId(1, FileFlags::BATCH_COMPRESS), FileCodecId::PACKET_LZ4_BATCH_001);
    EXPECT_EQ(resolveFileCodecId(9, FileFlags::BATCH_COMPRESS), FileCodecId::PACKET_LZ4_BATCH_001);
    // With ZoH too
    EXPECT_EQ(resolveFileCodecId(5, FileFlags::BATCH_COMPRESS | FileFlags::ZERO_ORDER_HOLD),
              FileCodecId::PACKET_LZ4_BATCH_001);
    // BATCH_COMPRESS without compression → PACKET_001 (batch requires compression)
    EXPECT_EQ(resolveFileCodecId(0, FileFlags::BATCH_COMPRESS), FileCodecId::PACKET_001);
}

#ifdef BCSV_HAS_BATCH_CODEC

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_Flat) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("batch_flat.bcsv"), makeLayout(), 100, 1, FileFlags::BATCH_COMPRESS);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_ZoH) {
    roundTripZoH<WriterZoH<Layout>>(
        testFile("batch_zoh.bcsv"), makeLayout(), 100, 1, FileFlags::BATCH_COMPRESS);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_Delta) {
    roundTripDelta<WriterDelta<Layout>>(
        testFile("batch_delta.bcsv"), makeLayout(), 100, 1, FileFlags::BATCH_COMPRESS);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_MultiPacket) {
    // Force many packets with tiny block size (1 KB)
    roundTripFlat<WriterFlat<Layout>>(
        testFile("batch_multi.bcsv"), makeLayout(), 500, 1, FileFlags::BATCH_COMPRESS, 1);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_Empty) {
    auto path = testFile("batch_empty.bcsv");
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 1, 64, FileFlags::BATCH_COMPRESS));
        writer.close();
    }
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_SingleRow) {
    auto path = testFile("batch_single.bcsv");
    auto layout = makeLayout();
    {
        WriterFlat<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64, FileFlags::BATCH_COMPRESS));
        writer.row().set(0, int32_t{42});
        writer.row().set(1, 3.14f);
        writer.row().set(2, std::string("only"));
        writer.writeRow();
        writer.close();
    }
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 42);
        EXPECT_FLOAT_EQ(reader.row().get<float>(1), 3.14f);
        EXPECT_EQ(reader.row().get<std::string>(2), "only");
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_LargerDataset) {
    roundTripFlat<WriterFlat<Layout>>(
        testFile("batch_large.bcsv"), makeLayout(), 1000, 1, FileFlags::BATCH_COMPRESS, 1);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_HC_Compression) {
    // Test with HC mode (level 6-9)
    roundTripFlat<WriterFlat<Layout>>(
        testFile("batch_hc.bcsv"), makeLayout(), 200, 7, FileFlags::BATCH_COMPRESS);
}

TEST_F(FileCodecTest, RoundTrip_PacketLZ4Batch_MultiPacket_ZoH) {
    // ZoH + multi-packet to test boundary crossing with ZoH repeats
    roundTripZoH<WriterZoH<Layout>>(
        testFile("batch_zoh_multi.bcsv"), makeLayout(), 500, 1, FileFlags::BATCH_COMPRESS, 1);
}

#endif // BCSV_HAS_BATCH_CODEC

// ============================================================================
// Cross-codec: ensure all codecs produce readable files with many rows
// ============================================================================

TEST_F(FileCodecTest, RoundTrip_AllCodecs_LargerDataset) {
    // 1000 rows each — enough to span multiple packets for packet codecs (1KB blocks)
    struct Config {
        std::string name;
        size_t compression;
        FileFlags flags;
        size_t blockKB;
    };

    std::vector<Config> configs = {
        {"pkt_lz4_1k", 1, FileFlags::NONE,        1},
        {"pkt_raw_1k", 0, FileFlags::NONE,         1},
        {"str_lz4_1k", 1, FileFlags::STREAM_MODE,  64},
        {"str_raw_1k", 0, FileFlags::STREAM_MODE,  64},
#ifdef BCSV_HAS_BATCH_CODEC
        {"batch_1k",   1, FileFlags::BATCH_COMPRESS, 1},
#endif
    };

    for (const auto& cfg : configs) {
        SCOPED_TRACE(cfg.name);
        roundTripFlat<WriterFlat<Layout>>(
            testFile(cfg.name + ".bcsv"), makeLayout(), 1000,
            cfg.compression, cfg.flags, cfg.blockKB);
    }
}

// ============================================================================
// Stream codecs: no footer written, so ReaderDirectAccess::open should fail
// when trying to read footer on a stream-mode file (graceful error)
// ============================================================================

TEST_F(FileCodecTest, StreamMode_DirectAccessOpen_FailsGracefully) {
    auto path = testFile("stream_no_da.bcsv");
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 0, 64, FileFlags::STREAM_MODE));
        writer.row().set(0, 42);
        writer.row().set(1, 1.0f);
        writer.row().set(2, std::string("hello"));
        writer.writeRow();
        writer.close();
    }
    {
        ReaderDirectAccess<Layout> reader;
        // DirectAccess expects a footer, stream mode files have none that's
        // useful — should fail or warn
        bool ok = reader.open(path);
        // Either it fails or succeeds; we just shouldn't crash
        if (ok) reader.close();
    }
}

// ============================================================================
// Stream codec: per-row XXH32 checksum corruption detection
// ============================================================================

TEST_F(FileCodecTest, StreamRaw_ChecksumCorruption_Throws) {
    auto path = testFile("str_raw_corrupt.bcsv");
    // Write a valid file
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 0, 64, FileFlags::STREAM_MODE));
        writer.row().set(0, 42);
        writer.row().set(1, 3.14f);
        writer.row().set(2, std::string("test"));
        writer.writeRow();
        writer.close();
    }

    // Corrupt a payload byte (flip bit in the row data, after VLE length)
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        // Seek to end to get file size
        f.seekg(0, std::ios::end);
        auto fileSize = f.tellg();
        ASSERT_GT(fileSize, 10);  // Sanity check

        // Corrupt a byte near the end but before the checksum (4 bytes from end)
        // The last 4 bytes are the XXH32 checksum.
        // Corrupt a byte just before the checksum.
        f.seekp(static_cast<std::streamoff>(fileSize) - 5);
        char byte;
        f.read(&byte, 1);
        f.seekp(static_cast<std::streamoff>(fileSize) - 5);
        byte ^= 0xFF;  // Flip all bits
        f.write(&byte, 1);
        f.close();
    }

    // Read should throw on checksum mismatch
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_THROW(reader.readNext(), std::runtime_error);
        reader.close();
    }
}

TEST_F(FileCodecTest, StreamLZ4_ChecksumCorruption_Throws) {
    auto path = testFile("str_lz4_corrupt.bcsv");
    // Write a valid file
    {
        WriterFlat<Layout> writer(makeLayout());
        ASSERT_TRUE(writer.open(path, true, 1, 64, FileFlags::STREAM_MODE));
        writer.row().set(0, 42);
        writer.row().set(1, 3.14f);
        writer.row().set(2, std::string("test"));
        writer.writeRow();
        writer.close();
    }

    // Corrupt a byte in the compressed data
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekg(0, std::ios::end);
        auto fileSize = f.tellg();
        ASSERT_GT(fileSize, 10);

        // Corrupt a byte before the checksum
        f.seekp(static_cast<std::streamoff>(fileSize) - 5);
        char byte;
        f.read(&byte, 1);
        f.seekp(static_cast<std::streamoff>(fileSize) - 5);
        byte ^= 0xFF;
        f.write(&byte, 1);
        f.close();
    }

    // Read should throw on checksum mismatch
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        EXPECT_THROW(reader.readNext(), std::runtime_error);
        reader.close();
    }
}
