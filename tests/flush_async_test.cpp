/*
 * Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file flush_async_test.cpp
 * @brief Writer::flushAsync() (1.5.23): the non-blocking flush.
 *
 *  - It writes the same file as flush(), byte for byte (the 8-byte creation
 *    timestamp at header offset 4 aside), for every row and file codec.
 *  - A flush() right after it waits for the packet in flight (TSan).
 *  - Rows are readable once the next synchronization point returns.
 *  - A failed background write is reported, not lost.
 */

#ifdef BCSV_HAS_BATCH_CODEC

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
using bcsv::ColumnType;
using bcsv::FileFlags;

class FlushAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("bcsv_flush_async_") + info->name() + "_" +
                std::to_string(static_cast<unsigned long>(::getpid())));
        fs::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    fs::path dir_;
};

bcsv::Layout makeLayout() {
    return bcsv::Layout({"t", "f", "d", "i16", "b", "s"},
                        {ColumnType::UINT64, ColumnType::FLOAT, ColumnType::DOUBLE,
                         ColumnType::INT16, ColumnType::BOOL, ColumnType::STRING});
}

void fillRow(bcsv::Row& r, uint64_t i) {
    r.set<uint64_t>(0, 1000 * i);
    r.set<float>(1, static_cast<float>(std::sin(static_cast<double>(i) * 0.01)));
    r.set<double>(2, static_cast<double>(i / 50) * 0.5);   // steps: ZoH/delta hits
    r.set<int16_t>(3, static_cast<int16_t>(static_cast<int>(i % 200) - 100));
    r.set<bool>(4, ((i / 37) % 2) != 0);
    r.set(5, "mode-" + std::to_string(i / 2500));
}

std::vector<char> readMasked(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<char> b{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    if (b.size() >= 12) std::memset(b.data() + 4, 0, 8);    // creation time
    return b;
}

uint64_t countRows(const fs::path& p) {
    bcsv::Reader<bcsv::Layout> r;
    EXPECT_TRUE(r.open(p)) << r.getErrorMsg();
    uint64_t n = 0;
    while (r.readNext()) ++n;
    return n;
}

/// 20000 rows in 64 KiB packets (so automatic boundaries happen too), a flush
/// before the first row, one every 997 rows, and two back to back (an empty
/// packet). Only the flush call differs between the two files.
template<typename Codec>
void expectSameBytes(const fs::path& dir, size_t level, FileFlags flags) {
    const auto layout = makeLayout();
    auto write = [&](const fs::path& p, bool async) {
        bcsv::Writer<bcsv::Layout, Codec> w(layout);
        ASSERT_TRUE(w.open(p, true, level, 64, flags)) << w.getErrorMsg();
        auto flush = [&] { async ? w.flushAsync() : w.flush(); };
        flush();
        for (uint64_t i = 0; i < 20000; ++i) {
            fillRow(w.row(), i);
            w.writeRow();
            if ((i + 1) % 997 == 0) flush();
            if (i == 5000) { flush(); flush(); }
        }
        w.close();
    };
    write(dir / "sync.bcsv", false);
    write(dir / "async.bcsv", true);
    const auto a = readMasked(dir / "sync.bcsv");
    const auto b = readMasked(dir / "async.bcsv");
    EXPECT_EQ(a.size(), b.size());
    EXPECT_TRUE(a == b) << "flushAsync() wrote a different file than flush()";
    EXPECT_EQ(countRows(dir / "async.bcsv"), 20000u);
}

TEST_F(FlushAsyncTest, SameBytesDeltaBatchLevel1) {
    expectSameBytes<bcsv::RowCodecDelta002<bcsv::Layout>>(dir_, 1, FileFlags::BATCH_COMPRESS);
}
TEST_F(FlushAsyncTest, SameBytesDeltaBatchLevel6) {
    expectSameBytes<bcsv::RowCodecDelta002<bcsv::Layout>>(dir_, 6, FileFlags::BATCH_COMPRESS);
}
TEST_F(FlushAsyncTest, SameBytesZoHBatch) {
    expectSameBytes<bcsv::RowCodecZoH001<bcsv::Layout>>(dir_, 1, FileFlags::BATCH_COMPRESS);
}
TEST_F(FlushAsyncTest, SameBytesFlatBatch) {
    expectSameBytes<bcsv::RowCodecFlat001<bcsv::Layout>>(dir_, 1, FileFlags::BATCH_COMPRESS);
}
TEST_F(FlushAsyncTest, SameBytesPacketCodecsWithoutThread) {
    // No background thread to hand off to: flushAsync() is a plain flush().
    expectSameBytes<bcsv::RowCodecDelta002<bcsv::Layout>>(dir_, 1, FileFlags::NONE);
    expectSameBytes<bcsv::RowCodecDelta002<bcsv::Layout>>(dir_, 0, FileFlags::NONE);
}
TEST_F(FlushAsyncTest, SameBytesStreamMode) {
    expectSameBytes<bcsv::RowCodecDelta002<bcsv::Layout>>(dir_, 1, FileFlags::STREAM_MODE);
}

TEST_F(FlushAsyncTest, FlushAfterFlushAsyncWaitsForThePacketInFlight) {
    // flushAsync() leaves a packet in flight with an empty active buffer; the
    // flush() that follows must not touch the stream before it is written.
    // A data race here is what the clang-tsan preset reports.
    const fs::path p = dir_ / "ff.bcsv";
    bcsv::Writer<bcsv::Layout> w(makeLayout());
    ASSERT_TRUE(w.open(p, true, 6, 64));
    for (uint64_t k = 0; k < 20; ++k) {
        for (uint64_t i = 0; i < 500; ++i) { fillRow(w.row(), k * 500 + i); w.writeRow(); }
        w.flushAsync();
        w.flush();
        EXPECT_EQ(countRows(p), (k + 1) * 500) << "flush() returned before the rows were written";
        w.flushAsync();
    }
    w.close();
    EXPECT_EQ(countRows(p), 10000u);
}

TEST_F(FlushAsyncTest, ClearsPoisonLikeFlush) {
    // A rejected oversized row poisons the writer; flushAsync() resynchronises
    // at the packet boundary exactly as flush() does.
    std::vector<std::string> names{"i"};
    std::vector<ColumnType> types{ColumnType::UINT32};
    for (int k = 0; k < 260; ++k) { names.push_back("s" + std::to_string(k)); types.push_back(ColumnType::STRING); }
    const fs::path p = dir_ / "poison.bcsv";
    bcsv::Writer<bcsv::Layout> w(bcsv::Layout(names, types));
    ASSERT_TRUE(w.open(p, true, 1, 64));
    w.row().set<uint32_t>(0, 0);
    w.writeRow();
    const std::string big(65000, 'x');          // 260 x 65000 B > MAX_ROW_LENGTH
    for (int k = 1; k <= 260; ++k) w.row().set(k, big);
    EXPECT_THROW(w.writeRow(), std::runtime_error);
    EXPECT_TRUE(w.isPoisoned());
    w.flushAsync();
    EXPECT_FALSE(w.isPoisoned());
    for (int k = 1; k <= 260; ++k) w.row().set(k, std::string());
    w.row().set<uint32_t>(0, 1);
    w.writeRow();
    w.close();
    EXPECT_EQ(countRows(p), 2u);
}

#ifdef __linux__
TEST_F(FlushAsyncTest, BackgroundWriteFailureIsReported) {
    // /dev/full fails every physical write with ENOSPC: the packet handed off
    // by flushAsync() fails on the background thread, and the next call that
    // waits for it (here flush(); an empty flushAsync() does not wait) throws.
    bcsv::Writer<bcsv::Layout> w(makeLayout());
    if (!w.open("/dev/full", true, 1, 64)) {
        GTEST_SKIP() << "cannot open /dev/full: " << w.getErrorMsg();
    }
    bool threw = false;
    try {
        for (uint64_t i = 0; i < 2000; ++i) { fillRow(w.row(), i); w.writeRow(); }
        w.flushAsync();
        w.flush();
    } catch (const std::exception& e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("FileCodecPacketLZ4Batch001"), std::string::npos) << e.what();
    }
    EXPECT_TRUE(threw) << "a failed background write went unreported";
    try { w.close(); } catch (const std::exception&) {}
    EXPECT_FALSE(w.isOpen());
}
#endif

}  // namespace

#endif  // BCSV_HAS_BATCH_CODEC
