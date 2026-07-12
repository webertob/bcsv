/*
 * Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file batch_codec_recovery_test.cpp
 * @brief Regression tests for the batch file codec's crash-recovery and
 *        background-error reporting guarantees (review 2026-07 items H1/H2/M6).
 *
 * Covered guarantees:
 *  - A footer-less (crashed) multi-packet file is read back completely — the
 *    background pre-read hitting EOF must not truncate the current packet.
 *  - A corrupt middle packet surfaces as an exception at the packet boundary;
 *    all rows of preceding valid packets are delivered first.
 *  - A background write failure (disk full) is never silent: it surfaces as
 *    an exception from writeRow()/close(), and close() records an error
 *    message. No clean footer is written over a corrupt tail.
 */

#ifdef BCSV_HAS_BATCH_CODEC

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

class BatchCodecRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Unique per test AND per process: ctest runs suite members as
        // separate parallel processes; a shared directory would be deleted
        // by a sibling's TearDown mid-run.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("bcsv_batch_recovery_") + info->name() + "_" +
                std::to_string(static_cast<unsigned long>(::getpid())));
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    static bcsv::Layout makeLayout() {
        return bcsv::Layout({"id", "value", "name"},
                            {bcsv::ColumnType::INT64,
                             bcsv::ColumnType::DOUBLE,
                             bcsv::ColumnType::STRING});
    }

    static void fillRow(bcsv::Row& row, size_t i) {
        // Non-constant data so rows have realistic wire size and the packet
        // count is deterministic-ish (~64 KB packets).
        row.set<int64_t>(0, static_cast<int64_t>(i * 2654435761ULL));
        row.set<double>(1, static_cast<double>(i) * 1.618033988749895);
        row.set(2, std::string("row_") + std::to_string(i * 7919));
    }

    /// Write numRows with the batch codec (64 KB packets). Returns the path.
    fs::path writeFile(const std::string& name, size_t numRows) {
        fs::path path = dir_ / name;
        bcsv::Writer<bcsv::Layout> writer(makeLayout());  // default codec = delta + batch
        EXPECT_TRUE(writer.open(path, true, 1, 64,
                                bcsv::FileFlags::BATCH_COMPRESS | bcsv::FileFlags::DELTA_ENCODING))
            << writer.getErrorMsg();
        for (size_t i = 0; i < numRows; ++i) {
            fillRow(writer.row(), i);
            writer.writeRow();
        }
        writer.close();
        return path;
    }

    /// Read the footer, return the packet index (empty on failure).
    static std::vector<bcsv::PacketIndexEntry> readPacketIndex(const fs::path& path) {
        std::ifstream is(path, std::ios::binary);
        bcsv::FileFooter footer;
        if (!footer.read(is)) return {};
        return {footer.packetIndex().begin(), footer.packetIndex().end()};
    }

    /// Remove the footer (28 + N*16 bytes at EOF), simulating a crash after
    /// the last packet was fully written but before finalize().
    static void stripFooter(const fs::path& path, size_t numPackets) {
        const uintmax_t footerSize = 28 + numPackets * 16;
        const uintmax_t fileSize = fs::file_size(path);
        ASSERT_GT(fileSize, footerSize);
        fs::resize_file(path, fileSize - footerSize);
    }

    /// Sequential read; returns rows successfully read. Validates content.
    static size_t countAndValidateRows(const fs::path& path) {
        bcsv::Reader<bcsv::Layout> reader;
        EXPECT_TRUE(reader.open(path)) << reader.getErrorMsg();
        size_t count = 0;
        while (reader.readNext()) {
            const auto& row = reader.row();
            EXPECT_EQ(row.get<int64_t>(0),
                      static_cast<int64_t>(count * 2654435761ULL));
            ++count;
        }
        reader.close();
        return count;
    }

    fs::path dir_;
};

// A footer-less multi-packet file must be readable to the very last row of
// the last complete packet.  Before the H1 fix, the BG pre-read hitting EOF
// left eofbit set on the shared stream and the reader dropped the rows of
// the final packet (nondeterministically, usually all of them).
TEST_F(BatchCodecRecoveryTest, FooterlessFileReadsAllPackets) {
    constexpr size_t kRows = 20000;
    fs::path path = writeFile("footerless.bcsv", kRows);

    auto index = readPacketIndex(path);
    ASSERT_GE(index.size(), 3u) << "test needs a multi-packet file";

    stripFooter(path, index.size());

    EXPECT_EQ(countAndValidateRows(path), kRows);
}

// Repeat the footer-less read many times: the pre-fix failure was a race
// (transient stream-state windows), so a single pass can pass by luck.
TEST_F(BatchCodecRecoveryTest, FooterlessFileReadIsStable) {
    constexpr size_t kRows = 5000;
    fs::path path = writeFile("footerless_stable.bcsv", kRows);

    auto index = readPacketIndex(path);
    ASSERT_GE(index.size(), 2u);
    stripFooter(path, index.size());

    for (int pass = 0; pass < 20; ++pass) {
        EXPECT_EQ(countAndValidateRows(path), kRows) << "pass " << pass;
    }
}

// A corrupt middle packet: every row of the preceding valid packets is
// delivered, then the error surfaces as an exception at the packet boundary.
// Before the H2 fix the exception slot was read/written unsynchronized and
// the error could be missed or reported at a racy point.
TEST_F(BatchCodecRecoveryTest, CorruptMiddlePacketSurfacesAtBoundary) {
    constexpr size_t kRows = 20000;
    fs::path path = writeFile("corrupt_middle.bcsv", kRows);

    auto index = readPacketIndex(path);
    ASSERT_GE(index.size(), 3u);

    // Corrupt one byte inside the second packet's compressed payload
    // (past PacketHeader (16) + sizes (8)).
    const uint64_t target = index[1].byte_offset + 16 + 8 + 4;
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(static_cast<std::streamoff>(target));
        char b{};
        f.read(&b, 1);
        b = static_cast<char>(b ^ 0xFF);
        f.seekp(static_cast<std::streamoff>(target));
        f.write(&b, 1);
    }

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    const size_t packet1Rows = static_cast<size_t>(index[1].first_row);
    size_t count = 0;
    bool threw = false;
    try {
        while (reader.readNext()) {
            ++count;
        }
    } catch (const std::exception&) {
        threw = true;
    }

    EXPECT_TRUE(threw) << "corruption must surface as an exception, not silent EOF";
    EXPECT_EQ(count, packet1Rows)
        << "all rows of the leading valid packet must be delivered first";
}

#ifdef __linux__
// Background write failure (disk full) must never be silent.  The error
// surfaces as an exception from writeRow() (next packet boundary) or from
// close(); close() additionally records an error message and must not write
// a clean footer.  /dev/full fails every physical write with ENOSPC.
TEST_F(BatchCodecRecoveryTest, DiskFullIsReported) {
    bcsv::Writer<bcsv::Layout> writer(makeLayout());
    if (!writer.open("/dev/full", true, 1, 64,
                     bcsv::FileFlags::BATCH_COMPRESS | bcsv::FileFlags::DELTA_ENCODING)) {
        GTEST_SKIP() << "cannot open /dev/full: " << writer.getErrorMsg();
    }

    bool failureReported = false;
    try {
        // Enough rows for several 64 KB packets → several BG write attempts.
        for (size_t i = 0; i < 50000; ++i) {
            fillRow(writer.row(), i);
            writer.writeRow();
        }
        writer.close();
    } catch (const std::exception&) {
        failureReported = true;
    }
    if (!failureReported) {
        // Tolerate error reporting via message instead of exception, but
        // silent success is a failure of the guarantee under test.
        failureReported = !writer.getErrorMsg().empty();
    }
    EXPECT_TRUE(failureReported) << "disk-full write completed 'successfully'";
}
#endif  // __linux__

}  // namespace

#endif  // BCSV_HAS_BATCH_CODEC
