/*
 * Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file format_hardening_test.cpp
 * @brief Regression tests for write/read validation symmetry and
 *        hostile-input hardening (review 2026-07 items H3, M1, M2, M3, M4, B7).
 *
 * Covered guarantees:
 *  - writeRow() rejects rows above MAX_ROW_LENGTH (the reader always did).
 *  - Delta002 deserialize rejects invalid header length codes (no shift UB).
 *  - Flat001 serialize emits exactly the bytes it accounts for when a string
 *    exceeds MAX_STRING_LENGTH (no uninitialized bytes on the wire).
 *  - FileFooter::read rejects malformed start_offset without huge allocations.
 *  - Batch codec rejects declared packet sizes beyond the header's limit.
 *  - Direct access validates packet checksums like sequential reads do.
 *  - FileHeader enforces a cumulative column-name cap, on write and read.
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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

class FormatHardeningTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("bcsv_hardening_") + info->name() + "_" +
                std::to_string(static_cast<unsigned long>(::getpid())));
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
};

// ── B1 (H3): write-side MAX_ROW_LENGTH enforcement ──────────────────────────

TEST_F(FormatHardeningTest, WriteRowExceedingMaxRowLengthThrows) {
    // 300 string columns × 64 KiB ≈ 19.6 MB serialized — above the 16 MiB
    // row limit that every read path enforces.
    constexpr size_t kCols = 300;
    std::vector<std::string> names;
    std::vector<bcsv::ColumnType> types(kCols, bcsv::ColumnType::STRING);
    for (size_t i = 0; i < kCols; ++i) names.push_back("s" + std::to_string(i));
    bcsv::Layout layout(names, types);

    fs::path path = dir_ / "oversized_row.bcsv";
    bcsv::WriterFlat<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path, true)) << writer.getErrorMsg();

    // Two small rows first — they must survive the failed third row.
    for (int r = 0; r < 2; ++r) {
        for (size_t c = 0; c < kCols; ++c) {
            writer.row().set(c, std::string("small"));
        }
        writer.writeRow();
    }

    const std::string big(bcsv::MAX_STRING_LENGTH, 'x');
    for (size_t c = 0; c < kCols; ++c) {
        writer.row().set(c, big);
    }
    EXPECT_THROW(writer.writeRow(), std::runtime_error);
    writer.close();

    // The file must still be valid and contain exactly the two small rows.
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    size_t count = 0;
    while (reader.readNext()) {
        EXPECT_EQ(reader.row().get<std::string>(0), "small");
        ++count;
    }
    EXPECT_EQ(count, 2u);
}

// ── B2 (M1): Delta002 invalid header length code ─────────────────────────────

TEST_F(FormatHardeningTest, Delta002InvalidLengthCodeThrows) {
    // Single int64 column → 4 header bits, valid codes 0..9.
    bcsv::Layout layout({"v"}, {bcsv::ColumnType::INT64});
    bcsv::RowCodecDelta002<bcsv::Layout> codec;
    codec.setup(layout);
    codec.reset();

    bcsv::Row row(layout);

    // Craft a row buffer: header byte with code 15 (0b1111) → deltaBytes 14,
    // followed by 14 payload bytes so the buffer-size check passes and the
    // (formerly UB) shift path would be reached.
    std::vector<std::byte> wire(1 + 14, std::byte{0});
    wire[0] = std::byte{0x0F};

    try {
        codec.deserialize({wire.data(), wire.size()}, row);
        FAIL() << "expected invalid length code to throw";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("Invalid delta length code"),
                  std::string::npos)
            << "unexpected message: " << e.what();
    }
}

TEST_F(FormatHardeningTest, Delta002InvalidLengthCodeSmallTypeThrows) {
    // Single int8 column → 2 header bits, valid codes 0..2; code 3 → 2 bytes
    // for a 1-byte column.
    bcsv::Layout layout({"v"}, {bcsv::ColumnType::INT8});
    bcsv::RowCodecDelta002<bcsv::Layout> codec;
    codec.setup(layout);
    codec.reset();

    bcsv::Row row(layout);
    std::vector<std::byte> wire(1 + 2, std::byte{0});
    wire[0] = std::byte{0x03};

    EXPECT_THROW(codec.deserialize({wire.data(), wire.size()}, row),
                 std::runtime_error);
}

// ── B3 (M3): Flat001 oversized-string serialization ─────────────────────────

TEST_F(FormatHardeningTest, Flat001OversizedStringSerializesExactBytes) {
    bcsv::Layout layout({"s"}, {bcsv::ColumnType::STRING});
    bcsv::RowCodecFlat001<bcsv::Layout> codec;
    codec.setup(layout);

    bcsv::Row row(layout);
    // ref<>() bypasses set()'s length handling — exactly the path that used
    // to leak uninitialized buffer bytes for strings > MAX_STRING_LENGTH.
    row.ref<std::string>(0).assign(bcsv::MAX_STRING_LENGTH + 4465, 'x');

    bcsv::ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    // Expected wire size: uint16 length prefix + exactly MAX_STRING_LENGTH
    // payload bytes (documented truncation).  Before the fix the span was
    // 4465 bytes longer than the bytes actually written.
    const size_t expected = sizeof(uint16_t) + bcsv::MAX_STRING_LENGTH;
    ASSERT_EQ(wire.size(), expected);

    // Every payload byte must be the written character — nothing uninitialized.
    for (size_t i = sizeof(uint16_t); i < wire.size(); ++i) {
        ASSERT_EQ(wire[i], std::byte{'x'}) << "uninitialized byte at " << i;
    }

    // Round-trip: reader sees the truncated string.
    bcsv::Row out(layout);
    codec.deserialize(wire, out);
    const auto& s = out.get<std::string>(0);
    EXPECT_EQ(s.size(), bcsv::MAX_STRING_LENGTH);
    EXPECT_EQ(s.front(), 'x');
    EXPECT_EQ(s.back(), 'x');
}

// ── B4 (M4): footer start_offset validation ─────────────────────────────────

class FooterHardeningTest : public FormatHardeningTest {
protected:
    fs::path writeSmallFile(const std::string& name) {
        bcsv::Layout layout({"a", "b"},
                            {bcsv::ColumnType::INT32, bcsv::ColumnType::DOUBLE});
        fs::path path = dir_ / name;
        bcsv::Writer<bcsv::Layout> writer(layout);
        EXPECT_TRUE(writer.open(path, true)) << writer.getErrorMsg();
        for (int i = 0; i < 1000; ++i) {
            writer.row().set<int32_t>(0, i);
            writer.row().set<double>(1, i * 0.5);
            writer.writeRow();
        }
        writer.close();
        return path;
    }

    // Patch the footer ConstSection's start_offset (at EOF-20, after the
    // 4-byte EIDX magic at EOF-24).
    static void patchStartOffset(const fs::path& path, uint32_t value) {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(-20, std::ios::end);
        f.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
};

TEST_F(FooterHardeningTest, FooterStartOffsetUnderflowRejected) {
    fs::path path = writeSmallFile("underflow.bcsv");
    patchStartOffset(path, 12);  // < minimum (28) → size_t underflow pre-fix
    {
        // Plant "BIDX" at EOF-12 so the pre-fix code path actually reaches
        // the underflowing indexSize arithmetic (otherwise the magic check
        // rejects first and the regression is not pinned).  Pre-fix this
        // made footer.read() attempt a ~2^60-entry vector resize.
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(-12, std::ios::end);
        f.write("BIDX", 4);
    }

    std::ifstream is(path, std::ios::binary);
    bcsv::FileFooter footer;
    bool ok = true;
    try {
        ok = footer.read(is);
    } catch (const std::exception& e) {
        FAIL() << "footer.read must reject malformed input, not throw/allocate: "
               << e.what();
    }
    EXPECT_FALSE(ok);
}

TEST_F(FooterHardeningTest, FooterStartOffsetBeyondFileRejected) {
    fs::path path = writeSmallFile("beyond.bcsv");
    patchStartOffset(path, 0xF0000000u);  // far past the file start

    std::ifstream is(path, std::ios::binary);
    bcsv::FileFooter footer;
    EXPECT_FALSE(footer.read(is));
}

// ── B4 (M4): batch codec declared-size bomb ─────────────────────────────────

TEST_F(FormatHardeningTest, BatchDeclaredSizeBombRejected) {
#ifndef BCSV_HAS_BATCH_CODEC
    GTEST_SKIP() << "batch codec disabled";
#else
    bcsv::Layout layout({"a"}, {bcsv::ColumnType::INT64});
    fs::path path = dir_ / "bomb.bcsv";
    {
        bcsv::Writer<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64,
                                bcsv::FileFlags::BATCH_COMPRESS |
                                bcsv::FileFlags::DELTA_ENCODING))
            << writer.getErrorMsg();
        writer.row().set<int64_t>(0, 42);
        writer.writeRow();
        writer.close();
    }

    // Locate the first packet via the footer, then patch its declared
    // uncompressed_size (offset +16 past the PacketHeader) to ~900 MiB.
    uint64_t packetOffset = 0;
    {
        std::ifstream is(path, std::ios::binary);
        bcsv::FileFooter footer;
        ASSERT_TRUE(footer.read(is));
        ASSERT_FALSE(footer.packetIndex().empty());
        packetOffset = footer.packetIndex()[0].byte_offset;
    }
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        uint32_t bogus = 900u * 1024 * 1024;
        f.seekp(static_cast<std::streamoff>(packetOffset + 16));
        f.write(reinterpret_cast<const char*>(&bogus), sizeof(bogus));
    }

    // The reader must reject the packet against the header's 64 KiB packet
    // size (plus one-row slack) instead of allocating ~1.8 GiB first.
    bcsv::Reader<bcsv::Layout> reader;
    EXPECT_FALSE(reader.open(path));
    EXPECT_NE(reader.getErrorMsg().find("packet size"), std::string::npos)
        << "unexpected error: " << reader.getErrorMsg();
#endif
}

// ── B5 (M2): direct access validates packet checksums ───────────────────────

TEST_F(FormatHardeningTest, DirectAccessDetectsCorruptPacket) {
    bcsv::Layout layout({"id", "txt"},
                        {bcsv::ColumnType::INT64, bcsv::ColumnType::STRING});
    fs::path path = dir_ / "da_corrupt.bcsv";
    {
        // Non-batch packet-LZ4 codec: checksum sits after the terminator and
        // was previously never validated on the direct-access path.
        bcsv::WriterFlat<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::NONE))
            << writer.getErrorMsg();
        for (int i = 0; i < 20000; ++i) {
            writer.row().set<int64_t>(0, i);
            writer.row().set(1, "row_" + std::to_string(i * 7919));
            writer.writeRow();
        }
        writer.close();
    }

    std::vector<bcsv::PacketIndexEntry> index;
    {
        std::ifstream is(path, std::ios::binary);
        bcsv::FileFooter footer;
        ASSERT_TRUE(footer.read(is));
        index.assign(footer.packetIndex().begin(), footer.packetIndex().end());
    }
    ASSERT_GE(index.size(), 3u) << "test needs a multi-packet file";

    // Corrupt one byte in the middle of packet 2's payload.
    const uint64_t mid = (index[1].byte_offset + index[2].byte_offset) / 2;
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(static_cast<std::streamoff>(mid));
        char b{};
        f.read(&b, 1);
        b = static_cast<char>(b ^ 0xFF);
        f.seekp(static_cast<std::streamoff>(mid));
        f.write(&b, 1);
    }

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // Reading a row inside the corrupt packet must fail loudly (checksum or
    // decode error) — never return quietly.  Pre-fix, loadPacket() skipped
    // checksum validation entirely.
    const size_t target = static_cast<size_t>(index[1].first_row) + 5;
    bool detected = false;
    try {
        if (!reader.read(target)) {
            detected = true;  // reported via error return
        }
    } catch (const std::exception&) {
        detected = true;      // reported via exception
    }
    EXPECT_TRUE(detected) << "corrupt packet served without any error signal";
}

// ── B7: cumulative column-name cap ───────────────────────────────────────────

TEST_F(FormatHardeningTest, HeaderCumulativeNameCapEnforcedOnWrite) {
    // 300 columns × 60 000-char names ≈ 18 MB > 16 MiB cap.
    std::vector<std::string> names;
    std::vector<bcsv::ColumnType> types(300, bcsv::ColumnType::INT8);
    for (size_t i = 0; i < 300; ++i) {
        std::string n(60000, 'n');
        n[0] = static_cast<char>('a' + (i % 26));
        n[1] = static_cast<char>('a' + ((i / 26) % 26));
        n[2] = static_cast<char>('a' + ((i / 676) % 26));
        names.push_back(std::move(n));
    }
    bcsv::Layout layout(names, types);

    bcsv::FileHeader header(layout.columnCount(), 1);
    std::ostringstream os(std::ios::binary);
    EXPECT_THROW(header.writeToBinary(os, layout), std::runtime_error);
}

TEST_F(FormatHardeningTest, HeaderCumulativeNameCapEnforcedOnRead) {
    // Write a valid 300-column header with unique fixed-width (4-char)
    // names, then patch every name-length field to 65535 (cumulative
    // ≈ 19.6 MB > cap).  Names must be unique — the Layout renames
    // duplicates, which would break the uniform length pattern.
    std::vector<std::string> names;
    std::vector<bcsv::ColumnType> types(300, bcsv::ColumnType::INT8);
    for (size_t i = 0; i < 300; ++i) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "c%03zu", i);
        names.emplace_back(buf);
    }
    bcsv::Layout layout(names, types);

    bcsv::FileHeader header(layout.columnCount(), 1);
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    header.writeToBinary(ss, layout);
    std::string bytes = ss.str();

    // The name-length array is the unique run of 300 consecutive uint16 == 4.
    std::string pattern(600, '\0');
    for (size_t i = 0; i < 300; ++i) pattern[2 * i] = '\x04';
    size_t pos = bytes.find(pattern);
    ASSERT_NE(pos, std::string::npos);
    for (size_t i = 0; i < 300; ++i) {
        bytes[pos + 2 * i]     = '\xFF';
        bytes[pos + 2 * i + 1] = '\xFF';
    }

    std::istringstream is(bytes, std::ios::binary);
    bcsv::Layout outLayout;
    bcsv::FileHeader outHeader;
    try {
        outHeader.readFromBinary(is, outLayout);
        FAIL() << "expected cumulative name cap to reject the header";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("Cumulative column-name size"),
                  std::string::npos)
            << "unexpected message: " << e.what();
    }
}

// ── H2 (review): rejected oversized row must poison the writer ──────────────
// The serializer commits the rejected row into the ZoH/Delta reference state
// before the size check can run; continuing to write against that state
// silently corrupts the stream (a retry of the same row becomes a 0-byte ZoH
// repeat).  The writer must refuse further rows until flush() resyncs at a
// packet boundary.
TEST_F(FormatHardeningTest, OversizedRowPoisonsStatefulWriter) {
    constexpr size_t kCols = 300;
    std::vector<std::string> names;
    std::vector<bcsv::ColumnType> types(kCols, bcsv::ColumnType::STRING);
    for (size_t i = 0; i < kCols; ++i) names.push_back("s" + std::to_string(i));
    bcsv::Layout layout(names, types);

    fs::path path = dir_ / "poisoned.bcsv";
    bcsv::WriterZoH<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path, true)) << writer.getErrorMsg();

    for (size_t c = 0; c < kCols; ++c) writer.row().set(c, std::string("A"));
    writer.writeRow();

    const std::string big(bcsv::MAX_STRING_LENGTH, 'x');
    for (size_t c = 0; c < kCols; ++c) writer.row().set(c, big);
    EXPECT_THROW(writer.writeRow(), std::runtime_error);

    // Retrying (even with a small row) must throw — the codec state is
    // desynced.  Pre-fix, retrying the SAME oversized row silently wrote a
    // 0-byte ZoH repeat and the file read back one row too many.
    EXPECT_THROW(writer.writeRow(), std::runtime_error);

    // flush() forces a packet boundary → encoder and decoder resync.
    writer.flush();
    for (size_t c = 0; c < kCols; ++c) writer.row().set(c, std::string("C"));
    writer.writeRow();
    writer.close();

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    std::vector<std::string> got;
    while (reader.readNext()) got.push_back(reader.row().get<std::string>(0));
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], "A");
    EXPECT_EQ(got[1], "C");
}

// ── H1 + M2 (review): direct access with a corrupt NEIGHBOR packet ─────────
// M2: corruption in packet N+1 must not make the fully valid packet N
// unreadable (finishPacketRead stops after the checksum instead of opening
// the next packet).  H1: after a failed read of a corrupt packet, the row
// cache must be invalidated — a subsequent read must not serve rows of the
// corrupt packet under the previous packet's indices.
TEST_F(FormatHardeningTest, DirectAccessCorruptNeighborAndCacheInvalidation) {
    bcsv::Layout layout({"id", "txt"},
                        {bcsv::ColumnType::INT64, bcsv::ColumnType::STRING});
    fs::path path = dir_ / "da_neighbor.bcsv";
    {
        bcsv::WriterFlat<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::NONE))
            << writer.getErrorMsg();
        for (int i = 0; i < 20000; ++i) {
            writer.row().set<int64_t>(0, i);
            writer.row().set(1, "row_" + std::to_string(i * 7919));
            writer.writeRow();
        }
        writer.close();
    }

    std::vector<bcsv::PacketIndexEntry> index;
    {
        std::ifstream is(path, std::ios::binary);
        bcsv::FileFooter footer;
        ASSERT_TRUE(footer.read(is));
        index.assign(footer.packetIndex().begin(), footer.packetIndex().end());
    }
    ASSERT_GE(index.size(), 3u);

    // Corrupt packet 1's HEADER (magic) — packet 0 stays fully valid.
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(static_cast<std::streamoff>(index[1].byte_offset));
        const char zeros[4] = {0, 0, 0, 0};
        f.write(zeros, 4);
    }

    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();

    // M2: a row inside the VALID packet 0 must read fine even though the
    // next packet's header is destroyed (pre-fix: the checksum peek opened
    // packet 1's header and threw).
    ASSERT_TRUE(reader.read(5)) << reader.getErrorMsg();
    EXPECT_EQ(reader.row().get<int64_t>(0), 5);

    // Reading inside the corrupt packet must fail loudly...
    const size_t inCorrupt = static_cast<size_t>(index[1].first_row) + 3;
    bool failed = false;
    try {
        if (!reader.read(inCorrupt)) failed = true;
    } catch (const std::exception&) {
        failed = true;
    }
    EXPECT_TRUE(failed);

    // ...and H1: afterwards the reader must still serve CORRECT data for
    // valid packets (pre-fix: stale cache metadata could label the corrupt
    // packet's rows with packet 0's indices).
    bool ok2 = false;
    int64_t v2 = -1;
    try {
        ok2 = reader.read(7);
        if (ok2) v2 = reader.row().get<int64_t>(0);
    } catch (const std::exception&) {
        ok2 = false;
    }
    if (ok2) {
        EXPECT_EQ(v2, 7) << "cache served wrong packet's data";
    }
}

#ifdef __linux__
// ── M1 (review): disk-full must be reported even for SMALL files ───────────
// Small files live entirely in the ofstream buffer; the physical write (and
// its ENOSPC) happens at flush/close time.  Pre-fix, close() checked the
// stream state BEFORE the flush inside stream_.close() — total silence.
TEST_F(FormatHardeningTest, DiskFullSmallFileIsReported) {
    bcsv::Layout layout({"a"}, {bcsv::ColumnType::INT64});
    bcsv::Writer<bcsv::Layout> writer(layout);
    if (!writer.open("/dev/full", true)) {
        GTEST_SKIP() << "cannot open /dev/full: " << writer.getErrorMsg();
    }
    for (int i = 0; i < 100; ++i) {
        writer.row().set<int64_t>(0, i);
        writer.writeRow();
    }
    bool reported = false;
    try {
        writer.close();
    } catch (const std::exception&) {
        reported = true;
    }
    if (!reported) {
        reported = !writer.getErrorMsg().empty();
    }
    EXPECT_TRUE(reported) << "small-file disk-full close() was silent";
}
#endif  // __linux__

}  // namespace
