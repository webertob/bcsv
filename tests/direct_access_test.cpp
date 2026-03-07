/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file direct_access_test.cpp
 * @brief Comprehensive tests for ReaderDirectAccess::read(size_t index).
 *
 * Tests cover:
 *   - Point access (single row lookups)
 *   - Head pattern (first N rows)
 *   - Tail pattern (last N rows)
 *   - Forward slice (range within file)
 *   - Backward slice (reverse iteration)
 *   - Cross-packet boundary access
 *   - Single-row file edge case
 *   - Out-of-range index
 *   - Piecewise sequential (consecutive reads)
 *   - Jump pattern (alternating packets)
 *   - Both compressed (LZ4) and uncompressed codecs
 *   - ZoH-enabled files
 *   - Static layout direct access
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>
#include <bcsv/bcsv.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;

// ============================================================================
// Test fixture
// ============================================================================

class DirectAccessTest : public ::testing::Test {
protected:
    std::string test_dir_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (fs::path("bcsv_test_files") / (std::string(info->test_suite_name())
                    + "_" + info->name())).string();
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    std::string testFile(const std::string& name) {
        return (fs::path(test_dir_) / name).string();
    }

    // ── Simple numeric layout ───────────────────────────────────────────
    // 6 columns: time(double), x(float), y(float), id(int32), flag(bool), label(string)
    // ~30 bytes/row → with 64 KB packets, ~2000 rows/packet

    bcsv::Layout createLayout() {
        bcsv::Layout layout;
        layout.addColumn({"time",  bcsv::ColumnType::DOUBLE});
        layout.addColumn({"x",     bcsv::ColumnType::FLOAT});
        layout.addColumn({"y",     bcsv::ColumnType::FLOAT});
        layout.addColumn({"id",    bcsv::ColumnType::INT32});
        layout.addColumn({"flag",  bcsv::ColumnType::BOOL});
        layout.addColumn({"label", bcsv::ColumnType::STRING});
        return layout;
    }

    void populateRow(bcsv::Writer<bcsv::Layout>& writer, size_t i) {
        writer.row().set<double>(0, static_cast<double>(i) * 0.001);   // time
        writer.row().set<float>(1, static_cast<float>(i) * 1.5f);       // x
        writer.row().set<float>(2, static_cast<float>(i) * -0.7f);      // y
        writer.row().set<int32_t>(3, static_cast<int32_t>(i));           // id
        writer.row().set(4, (i % 3 == 0));                               // flag
        writer.row().set(5, std::string("row_") + std::to_string(i));    // label
    }

    void validateRow(const bcsv::Row& row, size_t i) {
        EXPECT_DOUBLE_EQ(row.get<double>(0), static_cast<double>(i) * 0.001) << "row " << i;
        EXPECT_FLOAT_EQ(row.get<float>(1), static_cast<float>(i) * 1.5f) << "row " << i;
        EXPECT_FLOAT_EQ(row.get<float>(2), static_cast<float>(i) * -0.7f) << "row " << i;
        EXPECT_EQ(row.get<int32_t>(3), static_cast<int32_t>(i)) << "row " << i;
        EXPECT_EQ(row.get<bool>(4), (i % 3 == 0)) << "row " << i;
        EXPECT_EQ(row.get<std::string>(5), std::string("row_") + std::to_string(i)) << "row " << i;
    }

    /// Write a test file with N rows.
    /// @param blockSizeKB  Packet size in KB (small = more packets = better test coverage)
    /// @param compression  0 = uncompressed, 1-9 = LZ4
    /// @param flags        Additional file flags (e.g., ZERO_ORDER_HOLD)
    void writeTestFile(const std::string& path, size_t nRows,
                       size_t blockSizeKB = 64, size_t compression = 1,
                       bcsv::FileFlags flags = bcsv::FileFlags::NONE) {
        auto layout = createLayout();
        bcsv::Writer<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, compression, blockSizeKB, flags))
            << "Failed to open writer: " << writer.getErrorMsg();

        for (size_t i = 0; i < nRows; ++i) {
            populateRow(writer, i);
            writer.writeRow();
        }
        writer.close();
    }

    /// Write a test file using a specific row codec writer type.
    /// WriterType must be WriterFlat<Layout>, WriterZoH<Layout>, or WriterDelta<Layout>.
    template<typename WriterType>
    void writeTestFileWithCodec(const std::string& path, size_t nRows,
                                size_t blockSizeKB = 64, size_t compression = 1,
                                bcsv::FileFlags flags = bcsv::FileFlags::NONE) {
        auto layout = createLayout();
        WriterType writer(layout);
        ASSERT_TRUE(writer.open(path, true, compression, blockSizeKB, flags))
            << "Failed to open writer: " << writer.getErrorMsg();

        for (size_t i = 0; i < nRows; ++i) {
            writer.row().template set<double>(0, static_cast<double>(i) * 0.001);
            writer.row().template set<float>(1, static_cast<float>(i) * 1.5f);
            writer.row().template set<float>(2, static_cast<float>(i) * -0.7f);
            writer.row().template set<int32_t>(3, static_cast<int32_t>(i));
            writer.row().set(4, (i % 3 == 0));
            writer.row().set(5, std::string("row_") + std::to_string(i));
            writer.writeRow();
        }
        writer.close();
    }
};

// ============================================================================
// Basic point access
// ============================================================================

TEST_F(DirectAccessTest, PointAccess_FirstRow) {
    const size_t N = 100;
    auto path = testFile("point_first.bcsv");
    writeTestFile(path, N);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    ASSERT_TRUE(reader.read(0)) << reader.getErrorMsg();
    validateRow(reader.row(), 0);
    EXPECT_EQ(reader.rowPos(), 0u);
    reader.close();
}

TEST_F(DirectAccessTest, PointAccess_LastRow) {
    const size_t N = 100;
    auto path = testFile("point_last.bcsv");
    writeTestFile(path, N);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    ASSERT_TRUE(reader.read(N - 1)) << reader.getErrorMsg();
    validateRow(reader.row(), N - 1);
    reader.close();
}

TEST_F(DirectAccessTest, PointAccess_MiddleRow) {
    const size_t N = 1000;
    auto path = testFile("point_middle.bcsv");
    writeTestFile(path, N);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    ASSERT_TRUE(reader.read(500)) << reader.getErrorMsg();
    validateRow(reader.row(), 500);
    reader.close();
}

TEST_F(DirectAccessTest, PointAccess_OutOfRange) {
    const size_t N = 100;
    auto path = testFile("point_oor.bcsv");
    writeTestFile(path, N);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    EXPECT_FALSE(reader.read(N));       // Exactly at count → out of range
    EXPECT_FALSE(reader.read(N + 100)); // Way out of range
    reader.close();
}

TEST_F(DirectAccessTest, PointAccess_SingleRowFile) {
    auto path = testFile("point_single.bcsv");
    writeTestFile(path, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), 1u);

    ASSERT_TRUE(reader.read(0)) << reader.getErrorMsg();
    validateRow(reader.row(), 0);

    EXPECT_FALSE(reader.read(1));
    reader.close();
}

// ============================================================================
// Head pattern — first N rows
// ============================================================================

TEST_F(DirectAccessTest, HeadPattern) {
    const size_t TOTAL = 5000;
    const size_t HEAD = 50;
    auto path = testFile("head.bcsv");
    writeTestFile(path, TOTAL);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (size_t i = 0; i < HEAD; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// Tail pattern — last N rows
// ============================================================================

TEST_F(DirectAccessTest, TailPattern) {
    const size_t TOTAL = 5000;
    const size_t TAIL = 50;
    auto path = testFile("tail.bcsv");
    writeTestFile(path, TOTAL);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (size_t i = TOTAL - TAIL; i < TOTAL; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// Slice/range — a range within the file
// ============================================================================

TEST_F(DirectAccessTest, ForwardSlice) {
    const size_t TOTAL = 5000;
    auto path = testFile("slice_fwd.bcsv");
    writeTestFile(path, TOTAL);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Read rows 2000..2100
    for (size_t i = 2000; i <= 2100; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, BackwardSlice) {
    const size_t TOTAL = 5000;
    auto path = testFile("slice_bwd.bcsv");
    writeTestFile(path, TOTAL);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Read rows 2100..2000 in reverse
    for (size_t i = 2100; i >= 2000 && i <= 2100; --i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// Cross-packet boundary
// ============================================================================

TEST_F(DirectAccessTest, CrossPacketBoundary) {
    // Force small packets: 64 KB → ~2000 rows/packet.
    // Write 10000 rows → ~5 packets.  Read around boundaries.
    const size_t TOTAL = 10000;
    auto path = testFile("cross_pkt.bcsv");
    writeTestFile(path, TOTAL, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), TOTAL);

    // Determine packet boundaries from the footer
    const auto& index = reader.fileFooter().packetIndex();
    ASSERT_GE(index.size(), 2u) << "Need at least 2 packets for boundary test";

    // Read last row of first packet and first row of second packet
    size_t lastRowPkt0 = static_cast<size_t>(index[1].first_row) - 1;
    size_t firstRowPkt1 = static_cast<size_t>(index[1].first_row);

    ASSERT_TRUE(reader.read(lastRowPkt0)) << reader.getErrorMsg();
    validateRow(reader.row(), lastRowPkt0);

    ASSERT_TRUE(reader.read(firstRowPkt1)) << reader.getErrorMsg();
    validateRow(reader.row(), firstRowPkt1);

    reader.close();
}

// ============================================================================
// Jump pattern — alternating between distant packets
// ============================================================================

TEST_F(DirectAccessTest, JumpPattern) {
    const size_t TOTAL = 10000;
    auto path = testFile("jump.bcsv");
    writeTestFile(path, TOTAL, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Alternate between near-start and near-end rows
    for (int iter = 0; iter < 10; ++iter) {
        size_t near_start = static_cast<size_t>(iter * 10);
        size_t near_end = TOTAL - 1 - static_cast<size_t>(iter * 10);

        ASSERT_TRUE(reader.read(near_start)) << "iter=" << iter << " start: " << reader.getErrorMsg();
        validateRow(reader.row(), near_start);

        ASSERT_TRUE(reader.read(near_end)) << "iter=" << iter << " end: " << reader.getErrorMsg();
        validateRow(reader.row(), near_end);
    }
    reader.close();
}

// ============================================================================
// Full sequential validation via read() — compare against readNext()
// ============================================================================

TEST_F(DirectAccessTest, FullSequentialMatchesReadNext) {
    const size_t TOTAL = 500;
    auto path = testFile("seq_match.bcsv");
    writeTestFile(path, TOTAL, 64, 1);

    // Read all rows sequentially with readNext()
    std::vector<double> times_sequential(TOTAL);
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        size_t i = 0;
        while (reader.readNext()) {
            times_sequential[i] = reader.row().get<double>(0);
            i++;
        }
        ASSERT_EQ(i, TOTAL);
        reader.close();
    }

    // Read all rows via read(i) and compare
    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = 0; i < TOTAL; ++i) {
            ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
            EXPECT_DOUBLE_EQ(reader.row().get<double>(0), times_sequential[i])
                << "Mismatch at row " << i;
        }
        reader.close();
    }
}

// ============================================================================
// Uncompressed codec path
// ============================================================================

TEST_F(DirectAccessTest, UncompressedPointAccess) {
    const size_t N = 1000;
    auto path = testFile("uncompressed.bcsv");
    writeTestFile(path, N, 64, 0);  // compressionLevel=0

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    // Point access
    ASSERT_TRUE(reader.read(0)) << reader.getErrorMsg();
    validateRow(reader.row(), 0);

    ASSERT_TRUE(reader.read(N / 2)) << reader.getErrorMsg();
    validateRow(reader.row(), N / 2);

    ASSERT_TRUE(reader.read(N - 1)) << reader.getErrorMsg();
    validateRow(reader.row(), N - 1);

    reader.close();
}

TEST_F(DirectAccessTest, UncompressedForwardSlice) {
    const size_t N = 5000;
    auto path = testFile("uncompr_slice.bcsv");
    writeTestFile(path, N, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (size_t i = 2000; i <= 2100; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, UncompressedBackwardSlice) {
    const size_t N = 5000;
    auto path = testFile("uncompr_bwd.bcsv");
    writeTestFile(path, N, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Backward read forces cursor re-open within same packet
    for (size_t i = 500; i >= 400 && i <= 500; --i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, UncompressedCrossPacket) {
    const size_t TOTAL = 10000;
    auto path = testFile("uncompr_cross.bcsv");
    writeTestFile(path, TOTAL, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    ASSERT_GE(index.size(), 2u);

    size_t lastRowPkt0 = static_cast<size_t>(index[1].first_row) - 1;
    size_t firstRowPkt1 = static_cast<size_t>(index[1].first_row);

    ASSERT_TRUE(reader.read(lastRowPkt0)) << reader.getErrorMsg();
    validateRow(reader.row(), lastRowPkt0);

    ASSERT_TRUE(reader.read(firstRowPkt1)) << reader.getErrorMsg();
    validateRow(reader.row(), firstRowPkt1);

    reader.close();
}

TEST_F(DirectAccessTest, UncompressedFullSequential) {
    const size_t N = 500;
    auto path = testFile("uncompr_full.bcsv");
    writeTestFile(path, N, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path));

    for (size_t i = 0; i < N; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i;
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// Multi-packet stress — many packets, verify every row
// ============================================================================

TEST_F(DirectAccessTest, MultiPacket_EveryRow_Compressed) {
    const size_t TOTAL = 10000;
    auto path = testFile("multi_pkt_compr.bcsv");
    writeTestFile(path, TOTAL, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), TOTAL);
    EXPECT_GE(reader.fileFooter().packetIndex().size(), 3u)
        << "Expected multiple packets for comprehensive boundary testing";

    for (size_t i = 0; i < TOTAL; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, MultiPacket_EveryRow_Uncompressed) {
    const size_t TOTAL = 10000;
    auto path = testFile("multi_pkt_uncompr.bcsv");
    writeTestFile(path, TOTAL, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (size_t i = 0; i < TOTAL; ++i) {
        ASSERT_TRUE(reader.read(i)) << "Failed at row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// readNext() still works after open() on ReaderDirectAccess
// ============================================================================

TEST_F(DirectAccessTest, ReadNextStillWorks) {
    const size_t N = 100;
    auto path = testFile("readnext.bcsv");
    writeTestFile(path, N);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // readNext() should still iterate sequentially
    size_t count = 0;
    while (reader.readNext()) {
        validateRow(reader.row(), count);
        count++;
    }
    EXPECT_EQ(count, N);
    reader.close();
}

// ============================================================================
// Interleaved read() and readNext() — stress cache invalidation
// ============================================================================

TEST_F(DirectAccessTest, MixedReadAndReadNext) {
    const size_t N = 500;
    auto path = testFile("mixed.bcsv");
    writeTestFile(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Read first 10 rows via readNext
    for (size_t i = 0; i < 10; ++i) {
        ASSERT_TRUE(reader.readNext());
        validateRow(reader.row(), i);
    }

    // Jump to row 250 via read()
    ASSERT_TRUE(reader.read(250)) << reader.getErrorMsg();
    validateRow(reader.row(), 250);

    reader.close();
}

// ============================================================================
// Static layout direct access
// ============================================================================

using StaticLayout = bcsv::LayoutStatic<double, float, float, int32_t, bool, std::string>;

TEST_F(DirectAccessTest, StaticLayout_PointAccess) {
    const size_t N = 200;
    auto path = testFile("static.bcsv");

    // Write with static layout
    {
        StaticLayout layout({"time", "x", "y", "id", "flag", "label"});
        bcsv::Writer<StaticLayout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        for (size_t i = 0; i < N; ++i) {
            writer.row().template set<0>(static_cast<double>(i) * 0.001);
            writer.row().template set<1>(static_cast<float>(i) * 1.5f);
            writer.row().template set<2>(static_cast<float>(i) * -0.7f);
            writer.row().template set<3>(static_cast<int32_t>(i));
            writer.row().template set<4>(i % 3 == 0);
            writer.row().template set<5>(std::string("row_") + std::to_string(i));
            writer.writeRow();
        }
        writer.close();
    }

    // Read with direct access
    bcsv::ReaderDirectAccess<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    ASSERT_TRUE(reader.read(0)) << reader.getErrorMsg();
    EXPECT_DOUBLE_EQ(reader.row().template get<0>(), 0.0);

    ASSERT_TRUE(reader.read(N / 2)) << reader.getErrorMsg();
    EXPECT_DOUBLE_EQ(reader.row().template get<0>(), static_cast<double>(N / 2) * 0.001);

    ASSERT_TRUE(reader.read(N - 1)) << reader.getErrorMsg();

    reader.close();
}

// ============================================================================
// Performance comparison: read() head/tail vs full sequential readNext()
// ============================================================================

TEST_F(DirectAccessTest, Perf_HeadVsSequential) {
    const size_t TOTAL = 50000;
    const size_t HEAD = 100;
    auto path = testFile("perf_head.bcsv");
    writeTestFile(path, TOTAL, 64, 1);

    // Time direct-access head
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = 0; i < HEAD; ++i) {
            ASSERT_TRUE(reader.read(i));
        }
        reader.close();
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto da_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Time sequential full read (readNext over all rows)
    t0 = std::chrono::high_resolution_clock::now();
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        while (reader.readNext()) { /* consume all */ }
        reader.close();
    }
    t1 = std::chrono::high_resolution_clock::now();
    auto seq_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    std::cout << "  Head(" << HEAD << "/" << TOTAL << "): direct=" << da_us
              << "μs, sequential=" << seq_us << "μs, speedup="
              << (da_us > 0 ? static_cast<double>(seq_us) / static_cast<double>(da_us) : 0.0)
              << "x" << std::endl;

    // Direct access head should be faster than reading the entire file
    EXPECT_LT(da_us, seq_us) << "Direct access head should be faster than full sequential read";
}

TEST_F(DirectAccessTest, Perf_TailVsSequential) {
    const size_t TOTAL = 50000;
    const size_t TAIL = 100;
    auto path = testFile("perf_tail.bcsv");
    writeTestFile(path, TOTAL, 64, 1);

    // Time direct-access tail
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = TOTAL - TAIL; i < TOTAL; ++i) {
            ASSERT_TRUE(reader.read(i));
        }
        reader.close();
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto da_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Time sequential full read
    t0 = std::chrono::high_resolution_clock::now();
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        while (reader.readNext()) {}
        reader.close();
    }
    t1 = std::chrono::high_resolution_clock::now();
    auto seq_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    std::cout << "  Tail(" << TAIL << "/" << TOTAL << "): direct=" << da_us
              << "μs, sequential=" << seq_us << "μs, speedup="
              << (da_us > 0 ? static_cast<double>(seq_us) / static_cast<double>(da_us) : 0.0)
              << "x" << std::endl;

    EXPECT_LT(da_us, seq_us) << "Direct access tail should be faster than full sequential read";
}

// ============================================================================
// Row Codec Matrix — test all row codecs (flat, zoh, delta) with various file
// codecs (packet, packet_lz4, packet_lz4_batch).
//
// These tests validate the LIB-1 and LIB-2 fixes:
//   LIB-1: stateful codecs (zoh/delta) now decode sequentially from packet
//          start via the watermark mechanism.
//   LIB-2: packet_lz4_batch now implements seekToPacket().
// ============================================================================

// --- Flat row-codec (stateless) + all packet file-codecs ---

TEST_F(DirectAccessTest, CodecMatrix_Flat_Packet) {
    const size_t N = 10000;
    auto path = testFile("flat_packet.bcsv");
    writeTestFileWithCodec<bcsv::WriterFlat<bcsv::Layout>>(path, N, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);
    EXPECT_GE(reader.fileFooter().packetIndex().size(), 3u);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CodecMatrix_Flat_PacketLZ4) {
    const size_t N = 10000;
    auto path = testFile("flat_pktlz4.bcsv");
    writeTestFileWithCodec<bcsv::WriterFlat<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CodecMatrix_Flat_PacketLZ4Batch) {
    const size_t N = 10000;
    auto path = testFile("flat_batchlz4.bcsv");
    writeTestFileWithCodec<bcsv::WriterFlat<bcsv::Layout>>(
        path, N, 64, 1, bcsv::FileFlags::BATCH_COMPRESS);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// --- ZoH row-codec (stateful) + all packet file-codecs ---

TEST_F(DirectAccessTest, CodecMatrix_ZoH_Packet) {
    const size_t N = 10000;
    auto path = testFile("zoh_packet.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CodecMatrix_ZoH_PacketLZ4) {
    const size_t N = 10000;
    auto path = testFile("zoh_pktlz4.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CodecMatrix_ZoH_PacketLZ4Batch) {
    const size_t N = 10000;
    auto path = testFile("zoh_batchlz4.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(
        path, N, 64, 1, bcsv::FileFlags::BATCH_COMPRESS);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// --- Delta row-codec (highly stateful) + all packet file-codecs ---

TEST_F(DirectAccessTest, CodecMatrix_Delta_Packet) {
    const size_t N = 10000;
    auto path = testFile("delta_packet.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 0);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CodecMatrix_Delta_PacketLZ4) {
    const size_t N = 10000;
    auto path = testFile("delta_pktlz4.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CodecMatrix_Delta_PacketLZ4Batch) {
    const size_t N = 10000;
    auto path = testFile("delta_batchlz4.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(
        path, N, 64, 1, bcsv::FileFlags::BATCH_COMPRESS);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; i += 100) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// Default codec combination — delta + packet_lz4_batch (the most common combo)
// Full sequential verification: read(i) must match readNext() for every row.
// ============================================================================

TEST_F(DirectAccessTest, DefaultCodec_FullSequentialParity) {
    const size_t N = 5000;
    auto path = testFile("default_seq_parity.bcsv");
    // WriterDelta + BATCH_COMPRESS = default codec combo
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(
        path, N, 64, 1, bcsv::FileFlags::BATCH_COMPRESS);

    // Read all rows sequentially via readNext()
    std::vector<double> seq_times(N);
    std::vector<int32_t> seq_ids(N);
    std::vector<std::string> seq_labels(N);
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        size_t cnt = 0;
        while (reader.readNext()) {
            seq_times[cnt]  = reader.row().get<double>(0);
            seq_ids[cnt]    = reader.row().get<int32_t>(3);
            seq_labels[cnt] = reader.row().get<std::string>(5);
            cnt++;
        }
        ASSERT_EQ(cnt, N);
        reader.close();
    }

    // Read all rows via read(i) and compare
    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        for (size_t i = 0; i < N; ++i) {
            ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
            EXPECT_DOUBLE_EQ(reader.row().get<double>(0), seq_times[i]) << "time mismatch at row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(3), seq_ids[i]) << "id mismatch at row " << i;
            EXPECT_EQ(reader.row().get<std::string>(5), seq_labels[i]) << "label mismatch at row " << i;
        }
        reader.close();
    }
}

// ============================================================================
// Central 1/3 slice — read rows [N/3 .. 2N/3), skipping first/last thirds.
// Tests the primary use case: skipping artifacts at start/end of measurement.
// ============================================================================

TEST_F(DirectAccessTest, CentralThird_Flat) {
    const size_t N = 10000;
    auto path = testFile("c3_flat.bcsv");
    writeTestFileWithCodec<bcsv::WriterFlat<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    size_t start = N / 3, end = 2 * N / 3;
    for (size_t i = start; i < end; ++i) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CentralThird_ZoH) {
    const size_t N = 10000;
    auto path = testFile("c3_zoh.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    size_t start = N / 3, end = 2 * N / 3;
    for (size_t i = start; i < end; ++i) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

TEST_F(DirectAccessTest, CentralThird_Delta) {
    const size_t N = 10000;
    auto path = testFile("c3_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    size_t start = N / 3, end = 2 * N / 3;
    for (size_t i = start; i < end; ++i) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// ============================================================================
// Random access — 10000 rows (5 packets), 1000 random indices.
// Tests worst-case access pattern for stateful codecs.
// ============================================================================

TEST_F(DirectAccessTest, Random_Flat) {
    const size_t N = 10000;
    auto path = testFile("random_flat.bcsv");
    writeTestFileWithCodec<bcsv::WriterFlat<bcsv::Layout>>(path, N, 64, 1);

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, N - 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (int j = 0; j < 1000; ++j) {
        size_t idx = dist(rng);
        ASSERT_TRUE(reader.read(idx)) << "j=" << j << " idx=" << idx << ": " << reader.getErrorMsg();
        validateRow(reader.row(), idx);
    }
    reader.close();
}

TEST_F(DirectAccessTest, Random_ZoH) {
    const size_t N = 10000;
    auto path = testFile("random_zoh.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 1);

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, N - 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (int j = 0; j < 1000; ++j) {
        size_t idx = dist(rng);
        ASSERT_TRUE(reader.read(idx)) << "j=" << j << " idx=" << idx << ": " << reader.getErrorMsg();
        validateRow(reader.row(), idx);
    }
    reader.close();
}

TEST_F(DirectAccessTest, Random_Delta) {
    const size_t N = 10000;
    auto path = testFile("random_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, N - 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (int j = 0; j < 1000; ++j) {
        size_t idx = dist(rng);
        ASSERT_TRUE(reader.read(idx)) << "j=" << j << " idx=" << idx << ": " << reader.getErrorMsg();
        validateRow(reader.row(), idx);
    }
    reader.close();
}

// ============================================================================
// Intra-packet position tests — read the 5th and 16th row within each packet.
// Validates mid-packet decode for stateful codecs.
// ============================================================================

TEST_F(DirectAccessTest, IntraPacket_Row5_Flat) {
    const size_t N = 10000;
    auto path = testFile("intra5_flat.bcsv");
    writeTestFileWithCodec<bcsv::WriterFlat<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    for (size_t p = 0; p < index.size(); ++p) {
        size_t firstRow = static_cast<size_t>(index[p].first_row);
        size_t pktSize = (p + 1 < index.size())
            ? static_cast<size_t>(index[p + 1].first_row) - firstRow
            : reader.rowCount() - firstRow;

        if (pktSize > 5) {
            size_t target = firstRow + 4;  // 5th row (0-indexed)
            ASSERT_TRUE(reader.read(target)) << "pkt=" << p << " row=" << target << ": " << reader.getErrorMsg();
            validateRow(reader.row(), target);
        }
        if (pktSize > 16) {
            size_t target = firstRow + 15;  // 16th row (0-indexed)
            ASSERT_TRUE(reader.read(target)) << "pkt=" << p << " row=" << target << ": " << reader.getErrorMsg();
            validateRow(reader.row(), target);
        }
    }
    reader.close();
}

TEST_F(DirectAccessTest, IntraPacket_Row5_ZoH) {
    const size_t N = 10000;
    auto path = testFile("intra5_zoh.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    for (size_t p = 0; p < index.size(); ++p) {
        size_t firstRow = static_cast<size_t>(index[p].first_row);
        size_t pktSize = (p + 1 < index.size())
            ? static_cast<size_t>(index[p + 1].first_row) - firstRow
            : reader.rowCount() - firstRow;

        if (pktSize > 5) {
            size_t target = firstRow + 4;
            ASSERT_TRUE(reader.read(target)) << "pkt=" << p << " row=" << target << ": " << reader.getErrorMsg();
            validateRow(reader.row(), target);
        }
        if (pktSize > 16) {
            size_t target = firstRow + 15;
            ASSERT_TRUE(reader.read(target)) << "pkt=" << p << " row=" << target << ": " << reader.getErrorMsg();
            validateRow(reader.row(), target);
        }
    }
    reader.close();
}

TEST_F(DirectAccessTest, IntraPacket_Row5_Delta) {
    const size_t N = 10000;
    auto path = testFile("intra5_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    for (size_t p = 0; p < index.size(); ++p) {
        size_t firstRow = static_cast<size_t>(index[p].first_row);
        size_t pktSize = (p + 1 < index.size())
            ? static_cast<size_t>(index[p + 1].first_row) - firstRow
            : reader.rowCount() - firstRow;

        if (pktSize > 5) {
            size_t target = firstRow + 4;
            ASSERT_TRUE(reader.read(target)) << "pkt=" << p << " row=" << target << ": " << reader.getErrorMsg();
            validateRow(reader.row(), target);
        }
        if (pktSize > 16) {
            size_t target = firstRow + 15;
            ASSERT_TRUE(reader.read(target)) << "pkt=" << p << " row=" << target << ": " << reader.getErrorMsg();
            validateRow(reader.row(), target);
        }
    }
    reader.close();
}

// ============================================================================
// Edge cases
// ============================================================================

// First row of every packet — should always work (codec reset at packet boundary)
TEST_F(DirectAccessTest, Edge_FirstRowOfEveryPacket_Delta) {
    const size_t N = 10000;
    auto path = testFile("edge_firstrow.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    for (size_t p = 0; p < index.size(); ++p) {
        size_t firstRow = static_cast<size_t>(index[p].first_row);
        ASSERT_TRUE(reader.read(firstRow)) << "pkt=" << p << ": " << reader.getErrorMsg();
        validateRow(reader.row(), firstRow);
    }
    reader.close();
}

// Last row of every packet
TEST_F(DirectAccessTest, Edge_LastRowOfEveryPacket_Delta) {
    const size_t N = 10000;
    auto path = testFile("edge_lastrow.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    for (size_t p = 0; p < index.size(); ++p) {
        size_t lastRow;
        if (p + 1 < index.size()) {
            lastRow = static_cast<size_t>(index[p + 1].first_row) - 1;
        } else {
            lastRow = reader.rowCount() - 1;
        }
        ASSERT_TRUE(reader.read(lastRow)) << "pkt=" << p << " lastRow=" << lastRow << ": " << reader.getErrorMsg();
        validateRow(reader.row(), lastRow);
    }
    reader.close();
}

// Backward read within a single packet — forces watermark reset
TEST_F(DirectAccessTest, Edge_BackwardWithinPacket_Delta) {
    const size_t N = 10000;
    auto path = testFile("edge_backward.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Read forward to row 500, then backward to row 20
    ASSERT_TRUE(reader.read(500)) << reader.getErrorMsg();
    validateRow(reader.row(), 500);

    ASSERT_TRUE(reader.read(20)) << reader.getErrorMsg();
    validateRow(reader.row(), 20);

    // Forward again to row 100
    ASSERT_TRUE(reader.read(100)) << reader.getErrorMsg();
    validateRow(reader.row(), 100);

    reader.close();
}

// Re-read the same row — must return identical data
TEST_F(DirectAccessTest, Edge_ReReadSameRow_Delta) {
    const size_t N = 5000;
    auto path = testFile("edge_reread.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Read row 1234
    ASSERT_TRUE(reader.read(1234)) << reader.getErrorMsg();
    double t1 = reader.row().get<double>(0);
    int32_t id1 = reader.row().get<int32_t>(3);
    std::string lbl1 = reader.row().get<std::string>(5);

    // Read same row again
    ASSERT_TRUE(reader.read(1234)) << reader.getErrorMsg();
    EXPECT_DOUBLE_EQ(reader.row().get<double>(0), t1);
    EXPECT_EQ(reader.row().get<int32_t>(3), id1);
    EXPECT_EQ(reader.row().get<std::string>(5), lbl1);

    // Read a different row, then re-read 1234
    ASSERT_TRUE(reader.read(1235)) << reader.getErrorMsg();
    ASSERT_TRUE(reader.read(1234)) << reader.getErrorMsg();
    EXPECT_DOUBLE_EQ(reader.row().get<double>(0), t1);
    EXPECT_EQ(reader.row().get<int32_t>(3), id1);
    EXPECT_EQ(reader.row().get<std::string>(5), lbl1);

    reader.close();
}

// Packet boundary exact: last row of packet K then first row of packet K+1
TEST_F(DirectAccessTest, Edge_PacketBoundaryExact_Delta) {
    const size_t N = 10000;
    auto path = testFile("edge_pktbnd.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const auto& index = reader.fileFooter().packetIndex();
    ASSERT_GE(index.size(), 2u);

    for (size_t p = 0; p + 1 < index.size(); ++p) {
        size_t lastRowPktK = static_cast<size_t>(index[p + 1].first_row) - 1;
        size_t firstRowPktK1 = static_cast<size_t>(index[p + 1].first_row);

        ASSERT_TRUE(reader.read(lastRowPktK)) << "pkt=" << p << ": " << reader.getErrorMsg();
        validateRow(reader.row(), lastRowPktK);

        ASSERT_TRUE(reader.read(firstRowPktK1)) << "pkt=" << p << "→" << p + 1 << ": " << reader.getErrorMsg();
        validateRow(reader.row(), firstRowPktK1);
    }
    reader.close();
}

// Sequential vs Direct access full parity — delta codec (the critical one)
TEST_F(DirectAccessTest, Edge_SequentialParity_Delta) {
    const size_t N = 2000;
    auto path = testFile("edge_parity_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    // Collect all sequential values
    std::vector<double> seq_times(N);
    std::vector<float> seq_x(N);
    std::vector<int32_t> seq_ids(N);
    std::vector<std::string> seq_labels(N);
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        size_t cnt = 0;
        while (reader.readNext()) {
            seq_times[cnt]  = reader.row().get<double>(0);
            seq_x[cnt]      = reader.row().get<float>(1);
            seq_ids[cnt]    = reader.row().get<int32_t>(3);
            seq_labels[cnt] = reader.row().get<std::string>(5);
            cnt++;
        }
        ASSERT_EQ(cnt, N);
        reader.close();
    }

    // Compare every row via direct access
    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = 0; i < N; ++i) {
            ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
            EXPECT_DOUBLE_EQ(reader.row().get<double>(0), seq_times[i]) << "time @ row " << i;
            EXPECT_FLOAT_EQ(reader.row().get<float>(1), seq_x[i]) << "x @ row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(3), seq_ids[i]) << "id @ row " << i;
            EXPECT_EQ(reader.row().get<std::string>(5), seq_labels[i]) << "label @ row " << i;
        }
        reader.close();
    }
}

// Sequential vs Direct access parity — ZoH codec
TEST_F(DirectAccessTest, Edge_SequentialParity_ZoH) {
    const size_t N = 2000;
    auto path = testFile("edge_parity_zoh.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 1);

    std::vector<double> seq_times(N);
    std::vector<int32_t> seq_ids(N);
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        size_t cnt = 0;
        while (reader.readNext()) {
            seq_times[cnt] = reader.row().get<double>(0);
            seq_ids[cnt]   = reader.row().get<int32_t>(3);
            cnt++;
        }
        ASSERT_EQ(cnt, N);
        reader.close();
    }

    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = 0; i < N; ++i) {
            ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
            EXPECT_DOUBLE_EQ(reader.row().get<double>(0), seq_times[i]) << "time @ row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(3), seq_ids[i]) << "id @ row " << i;
        }
        reader.close();
    }
}

// Jump between distant packets with delta codec
TEST_F(DirectAccessTest, Edge_JumpPattern_Delta) {
    const size_t N = 10000;
    auto path = testFile("edge_jump_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    for (int iter = 0; iter < 20; ++iter) {
        size_t near_start = static_cast<size_t>(iter * 5);
        size_t near_end = N - 1 - static_cast<size_t>(iter * 5);

        ASSERT_TRUE(reader.read(near_start)) << "iter=" << iter << " start: " << reader.getErrorMsg();
        validateRow(reader.row(), near_start);

        ASSERT_TRUE(reader.read(near_end)) << "iter=" << iter << " end: " << reader.getErrorMsg();
        validateRow(reader.row(), near_end);
    }
    reader.close();
}

// Delta + batch LZ4 — the default combo that triggered both bugs
TEST_F(DirectAccessTest, Edge_DefaultCombo_EveryRow) {
    const size_t N = 3000;
    auto path = testFile("edge_default_every.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(
        path, N, 64, 1, bcsv::FileFlags::BATCH_COMPRESS);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_EQ(reader.rowCount(), N);

    for (size_t i = 0; i < N; ++i) {
        ASSERT_TRUE(reader.read(i)) << "row " << i << ": " << reader.getErrorMsg();
        validateRow(reader.row(), i);
    }
    reader.close();
}

// Performance: central 30% direct access vs full sequential read
TEST_F(DirectAccessTest, Perf_CentralSlice_Delta) {
    const size_t TOTAL = 50000;
    auto path = testFile("perf_central_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, TOTAL, 64, 1);

    size_t start = TOTAL * 35 / 100;
    size_t end   = TOTAL * 65 / 100;
    size_t sliceLen = end - start;

    // Time direct-access central slice
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = start; i < end; ++i) {
            ASSERT_TRUE(reader.read(i));
        }
        reader.close();
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    auto da_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Time sequential full read (readNext over all rows, skip unwanted)
    t0 = std::chrono::high_resolution_clock::now();
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));
        while (reader.readNext()) { /* consume all */ }
        reader.close();
    }
    t1 = std::chrono::high_resolution_clock::now();
    auto seq_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    std::cout << "  Central30%_Delta(" << sliceLen << "/" << TOTAL << "): direct=" << da_us
              << "μs, sequential=" << seq_us << "μs, speedup="
              << (da_us > 0 ? static_cast<double>(seq_us) / static_cast<double>(da_us) : 0.0)
              << "x" << std::endl;
}

// ============================================================================
// Stream-mode files — ReaderDirectAccess must fail gracefully
// ============================================================================

TEST_F(DirectAccessTest, StreamMode_ReturnsError) {
    // Stream codecs don't support seekToPacket() — DA must report an error.
    const size_t N = 100;
    auto path = testFile("stream_mode.bcsv");
    writeTestFile(path, N, 64, 0, bcsv::FileFlags::STREAM_MODE);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    // open() may fail (no footer for stream files) or succeed with rebuilt footer,
    // but read() must not succeed without seekToPacket support.
    if (reader.open(path, true)) {
        // If open succeeds (via footer rebuild), read must fail
        EXPECT_FALSE(reader.read(0)) << "Stream-mode should not support direct access";
        EXPECT_FALSE(reader.getErrorMsg().empty());
    }
    reader.close();
}

// ============================================================================
// readNext() + read() interleaving — watermark must auto-invalidate
// ============================================================================

TEST_F(DirectAccessTest, Interleave_ReadNextThenRead_Delta) {
    // Verify that read() produces correct results even after readNext()
    // has advanced the base class row_pos_ (stateful delta codec).
    const size_t N = 2000;
    auto path = testFile("interleave_delta.bcsv");
    writeTestFileWithCodec<bcsv::WriterDelta<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Sequential reads via readNext() — advances base row_pos_
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(reader.readNext());
    }
    // row_pos_ is now 5 (rows 0..4 consumed)

    // Direct access to an arbitrary row — must produce correct data
    // even though readNext() modified row_pos_ and row_ state
    ASSERT_TRUE(reader.read(100)) << reader.getErrorMsg();
    validateRow(reader.row(), 100);

    // Backward direct access — must still work after the interleaving
    ASSERT_TRUE(reader.read(50)) << reader.getErrorMsg();
    validateRow(reader.row(), 50);

    // Forward direct access
    ASSERT_TRUE(reader.read(150)) << reader.getErrorMsg();
    validateRow(reader.row(), 150);

    reader.close();
}

TEST_F(DirectAccessTest, Interleave_ReadNextThenRead_ZoH) {
    // Verify that read() produces correct results even after readNext()
    // has advanced the base class row_pos_ (stateful ZoH codec).
    const size_t N = 2000;
    auto path = testFile("interleave_zoh.bcsv");
    writeTestFileWithCodec<bcsv::WriterZoH<bcsv::Layout>>(path, N, 64, 1);

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Sequential reads via readNext()
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(reader.readNext());
    }

    // Direct access after readNext() must still work correctly
    ASSERT_TRUE(reader.read(500)) << reader.getErrorMsg();
    validateRow(reader.row(), 500);

    // Backward access
    ASSERT_TRUE(reader.read(200)) << reader.getErrorMsg();
    validateRow(reader.row(), 200);

    reader.close();
}
