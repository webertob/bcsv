/*
 * Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file version_gate_test.cpp
 * @brief Regression tests for the file-format version rules (VERSIONING.md
 *        section "File Format Versioning").
 *
 * Rule A — major must match exactly.
 * Rule B — minor is backward compatible only: a reader opens a file whose
 *          minor is <= its own and refuses anything newer.
 * Rule C — patch is compatible in both directions.
 *
 * Until now these rules lived only in `Reader::readFileHeader()` and in prose,
 * which is thin cover for the guarantee they actually carry: a MINOR may add a
 * new header section (item E12's in-format metadata, say) *because* every
 * reader built before the bump refuses the file outright, instead of parsing
 * the familiar prefix and then reading packets from an offset the new section
 * has moved.  The gate is the reason that is safe, so it is tested here.
 *
 * The version fields sit at fixed offsets in the 24-byte header
 * (12 = major, 13 = minor, 14 = patch), so each case writes a real file and
 * patches one byte — no checksum covers the header, and the footer's offsets
 * are unaffected because the header's size does not change.
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

constexpr std::streamoff kMajorOffset = 12;
constexpr std::streamoff kMinorOffset = 13;
constexpr std::streamoff kPatchOffset = 14;

class VersionGateTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("bcsv_version_gate_") + info->name() + "_" +
                std::to_string(static_cast<unsigned long>(::getpid())));
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    /// Write a two-row file at the library's own version stamp.
    fs::path writeFile(const std::string& name,
                       bcsv::FileFlags flags = bcsv::FileFlags::BATCH_COMPRESS |
                                               bcsv::FileFlags::DELTA_ENCODING) {
        bcsv::Layout layout({"id", "value"},
                            {bcsv::ColumnType::INT64, bcsv::ColumnType::DOUBLE});
        fs::path path = dir_ / name;
        bcsv::Writer<bcsv::Layout> writer(layout);
        EXPECT_TRUE(writer.open(path, true, 1, 64, flags)) << writer.getErrorMsg();
        for (int64_t i = 0; i < 2; ++i) {
            writer.row().set<int64_t>(0, i);
            writer.row().set<double>(1, 1.5 * static_cast<double>(i));
            writer.writeRow();
        }
        writer.close();
        return path;
    }

    static void patchByte(const fs::path& path, std::streamoff offset, uint8_t value) {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(offset);
        f.write(reinterpret_cast<const char*>(&value), 1);
        ASSERT_TRUE(f.good());
    }

    /// Open and read both rows back, asserting the payload survived.
    static void expectReadsBack(const fs::path& path) {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
        for (int64_t i = 0; i < 2; ++i) {
            ASSERT_TRUE(reader.readNext());
            EXPECT_EQ(reader.row().get<int64_t>(0), i);
            EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 1.5 * static_cast<double>(i));
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }

    static void expectRefusedAsIncompatible(const fs::path& path) {
        bcsv::Reader<bcsv::Layout> reader;
        EXPECT_FALSE(reader.open(path));
        EXPECT_NE(reader.getErrorMsg().find("Incompatible file version"),
                  std::string::npos)
            << "unexpected error: " << reader.getErrorMsg();
    }

    fs::path dir_;
};

// ── Rule B: a newer minor is refused, and refused before any packet is read ──

TEST_F(VersionGateTest, NewerMinorIsRefused) {
    ASSERT_LT(bcsv::version::MINOR, 255) << "minor cannot be incremented in a uint8";
    fs::path path = writeFile("newer_minor.bcsv");
    patchByte(path, kMinorOffset, static_cast<uint8_t>(bcsv::version::MINOR + 1));
    expectRefusedAsIncompatible(path);
}

// The E12 scenario in full: a future minor that carries an as-yet-unknown
// feature bit *and* (by implication) a header section this reader does not
// know how to skip.  The version gate must fire on the version alone, before
// the unknown flag can steer codec selection or the moved packet stream can be
// misparsed.
TEST_F(VersionGateTest, NewerMinorWithUnknownFlagBitIsRefused) {
    fs::path path = writeFile("newer_minor_flag.bcsv");
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        uint16_t flags = 0;
        f.seekg(16);
        f.read(reinterpret_cast<char*>(&flags), sizeof(flags));
        flags |= 0x0020;  // bit 5: reserved today, hypothetical metadata bit
        f.seekp(16);
        f.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
        ASSERT_TRUE(f.good());
    }
    patchByte(path, kMinorOffset, static_cast<uint8_t>(bcsv::version::MINOR + 1));
    expectRefusedAsIncompatible(path);
}

// The boundary of the guarantee, pinned deliberately: an unknown FileFlags bit
// on its own is NOT a gate.  `readFromBinary` validates magic, packet size,
// column count, column types and name lengths — not the flags — so a file that
// sets a reserved bit without bumping the minor is accepted and read as if the
// bit were absent.  Anyone adding a feature bit must bump version::MINOR with
// it; this test fails loudly if flag validation is ever added instead, which
// would make the same file refused rather than silently accepted.
TEST_F(VersionGateTest, UnknownFlagBitAloneIsNotAGate) {
    fs::path path = writeFile("unknown_flag.bcsv");
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        uint16_t flags = 0;
        f.seekg(16);
        f.read(reinterpret_cast<char*>(&flags), sizeof(flags));
        flags |= 0x0020;
        f.seekp(16);
        f.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
        ASSERT_TRUE(f.good());
    }
    expectReadsBack(path);
}

TEST_F(VersionGateTest, OlderMinorIsAccepted) {
    if (bcsv::version::MINOR == 0) {
        GTEST_SKIP() << "no older minor to stamp within this major";
    }
    fs::path path = writeFile("older_minor.bcsv");
    patchByte(path, kMinorOffset, static_cast<uint8_t>(bcsv::version::MINOR - 1));
    expectReadsBack(path);
}

// ── Rule A: major must match exactly, in both directions ────────────────────

TEST_F(VersionGateTest, NewerMajorIsRefused) {
    ASSERT_LT(bcsv::version::MAJOR, 255) << "major cannot be incremented in a uint8";
    fs::path path = writeFile("newer_major.bcsv");
    patchByte(path, kMajorOffset, static_cast<uint8_t>(bcsv::version::MAJOR + 1));
    expectRefusedAsIncompatible(path);
}

TEST_F(VersionGateTest, OlderMajorIsRefused) {
    if (bcsv::version::MAJOR == 0) {
        GTEST_SKIP() << "no older major to stamp";
    }
    fs::path path = writeFile("older_major.bcsv");
    patchByte(path, kMajorOffset, static_cast<uint8_t>(bcsv::version::MAJOR - 1));
    expectRefusedAsIncompatible(path);
}

// ── Rule C: patch is compatible in both directions ──────────────────────────

TEST_F(VersionGateTest, NewerPatchIsAccepted) {
    ASSERT_LT(bcsv::version::PATCH, 255) << "patch cannot be incremented in a uint8";
    fs::path path = writeFile("newer_patch.bcsv");
    patchByte(path, kPatchOffset, static_cast<uint8_t>(bcsv::version::PATCH + 1));
    expectReadsBack(path);
}

TEST_F(VersionGateTest, OlderPatchIsAccepted) {
    fs::path path = writeFile("older_patch.bcsv");
    patchByte(path, kPatchOffset, 0);
    expectReadsBack(path);
}

}  // namespace
