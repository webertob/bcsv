/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file codec_dispatch_test.cpp
 * @brief Cross-combination tests for CodecDispatch (Item 11, Phase 7).
 *
 * Tests all four combinations of {Flat, ZoH} × {Disabled, Enabled}:
 *   - Flat  + Disabled  (natural fit — existing tests cover this extensively)
 *   - ZoH   + Enabled   (natural fit — existing tests cover this extensively)
 *   - Flat  + Enabled   (cross: Writer writes flat, Reader tracks changes)
 *   - ZoH   + Disabled  (cross: Writer writes ZoH, Reader skips tracking)
 *
 * Both dynamic (Layout) and static (LayoutStatic) paths are tested.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

#include "bcsv/writer.h"
#include "bcsv/reader.h"
#include "bcsv/row_codec_dispatch.h"
#include <bcsv/bcsv.h>

namespace {

// ── Test data ────────────────────────────────────────────────────────────────
struct RowData {
    bool        b1;
    int32_t     i32;
    double      d;
    std::string s;
};

const std::vector<RowData> TEST_DATA = {
    { true,   42,    3.14,   "hello" },
    { false, -100,   2.718,  "world" },
    { true,   0,     0.0,    "" },
    { false,  999,  -1.5,    "bcsv" },
    { true,   42,    3.14,   "hello" },   // repeat of row 0 (tests ZoH repeat)
    { true,   42,    3.14,   "hello" },   // another repeat
    { false, -999,   100.0,  "changed" }, // change after repeats
};

// ── Helpers ──────────────────────────────────────────────────────────────────

bcsv::Layout createFlexLayout() {
    bcsv::Layout layout;
    layout.addColumn({"b1",  bcsv::ColumnType::BOOL});
    layout.addColumn({"i32", bcsv::ColumnType::INT32});
    layout.addColumn({"d",   bcsv::ColumnType::DOUBLE});
    layout.addColumn({"s",   bcsv::ColumnType::STRING});
    return layout;
}

using StaticLayout = bcsv::LayoutStatic<bool, int32_t, double, std::string>;

StaticLayout createStaticLayout() {
    return StaticLayout({"b1", "i32", "d", "s"});
}

const std::string TEST_DIR = "bcsv_test_files/codec_dispatch";

// ── Write helpers ────────────────────────────────────────────────────────────

void writeFlatFlexible(const std::string& path) {
    auto layout = createFlexLayout();
    bcsv::Writer<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path, true)) << writer.getErrorMsg();
    for (const auto& d : TEST_DATA) {
        writer.row().set(0, d.b1);
        writer.row().set(1, d.i32);
        writer.row().set(2, d.d);
        writer.row().set(3, d.s);
        writer.writeRow();
    }
    writer.close();
}

void writeZoHFlexible(const std::string& path) {
    auto layout = createFlexLayout();
    bcsv::WriterZoH<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD))
        << writer.getErrorMsg();
    for (const auto& d : TEST_DATA) {
        writer.row().set(0, d.b1);
        writer.row().set(1, d.i32);
        writer.row().set(2, d.d);
        writer.row().set(3, d.s);
        writer.writeRow();
    }
    writer.close();
}

void writeFlatStatic(const std::string& path) {
    auto layout = createStaticLayout();
    bcsv::Writer<StaticLayout> writer(layout);
    ASSERT_TRUE(writer.open(path, true)) << writer.getErrorMsg();
    for (const auto& d : TEST_DATA) {
        writer.row().set<0>(d.b1);
        writer.row().set<1>(d.i32);
        writer.row().set<2>(d.d);
        writer.row().set<3>(d.s);
        writer.writeRow();
    }
    writer.close();
}

void writeZoHStatic(const std::string& path) {
    auto layout = createStaticLayout();
    bcsv::WriterZoH<StaticLayout> writer(layout);
    ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD))
        << writer.getErrorMsg();
    for (const auto& d : TEST_DATA) {
        writer.row().set<0>(d.b1);
        writer.row().set<1>(d.i32);
        writer.row().set<2>(d.d);
        writer.row().set<3>(d.s);
        writer.writeRow();
    }
    writer.close();
}

// ── Verification ─────────────────────────────────────────────────────────────

template<typename ReaderType>
void verifyData(ReaderType& reader) {
    size_t count = 0;
    while (reader.readNext()) {
        ASSERT_LT(count, TEST_DATA.size()) << "Too many rows read";
        const auto& expected = TEST_DATA[count];
        const auto& row = reader.row();

        EXPECT_EQ(row.template get<bool>(0),        expected.b1)   << "row " << count << " b1";
        EXPECT_EQ(row.template get<int32_t>(1),      expected.i32)  << "row " << count << " i32";
        EXPECT_DOUBLE_EQ(row.template get<double>(2), expected.d)   << "row " << count << " d";
        EXPECT_EQ(row.template get<std::string>(3),  expected.s)    << "row " << count << " s";
        ++count;
    }
    EXPECT_EQ(count, TEST_DATA.size()) << "Row count mismatch";
}

template<typename ReaderType>
void verifyDataStatic(ReaderType& reader) {
    size_t count = 0;
    while (reader.readNext()) {
        ASSERT_LT(count, TEST_DATA.size()) << "Too many rows read";
        const auto& expected = TEST_DATA[count];
        const auto& row = reader.row();

        // Use runtime-indexed get<Type>(index) to avoid auto-deduction issues
        // in template context with GTest macros.
        EXPECT_EQ(row.template get<bool>(0),        expected.b1)   << "row " << count << " b1";
        EXPECT_EQ(row.template get<int32_t>(1),      expected.i32)  << "row " << count << " i32";
        EXPECT_DOUBLE_EQ(row.template get<double>(2), expected.d)   << "row " << count << " d";
        EXPECT_EQ(row.template get<std::string>(3),  expected.s)    << "row " << count << " s";
        ++count;
    }
    EXPECT_EQ(count, TEST_DATA.size()) << "Row count mismatch";
}

} // anonymous namespace


// ════════════════════════════════════════════════════════════════════════════
// Flexible Layout cross-combination tests
// ════════════════════════════════════════════════════════════════════════════

class CodecDispatchFlexTest : public ::testing::Test {
protected:
    std::string test_dir_;
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (std::filesystem::path(TEST_DIR) / (std::string(info->test_suite_name())
                     + "_" + info->name())).string();
        std::filesystem::create_directories(test_dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }
};

// Natural: Flat file, Disabled reader (existing path — sanity check)
TEST_F(CodecDispatchFlexTest, FlatFile_DisabledReader) {
    const auto path = testFile("dispatch_flat_disabled.bcsv");
    writeFlatFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyData(reader);
    reader.close();
}

// Natural: ZoH file, Enabled reader (existing path — sanity check)
TEST_F(CodecDispatchFlexTest, ZoHFile_EnabledReader) {
    const auto path = testFile("dispatch_zoh_enabled.bcsv");
    writeZoHFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyData(reader);
    reader.close();
}

// Cross: Flat file, Enabled reader (should work — all columns marked changed)
TEST_F(CodecDispatchFlexTest, FlatFile_EnabledReader) {
    const auto path = testFile("dispatch_flat_enabled.bcsv");
    writeFlatFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_TRUE(reader.isOpen());
    verifyData(reader);
    reader.close();
}

// Cross: ZoH file, Disabled reader (should work — codec uses internal wire_bits_)
TEST_F(CodecDispatchFlexTest, ZoHFile_DisabledReader) {
    const auto path = testFile("dispatch_zoh_disabled.bcsv");
    writeZoHFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_TRUE(reader.isOpen());
    verifyData(reader);
    reader.close();
}

// ════════════════════════════════════════════════════════════════════════════
// Static Layout cross-combination tests
// ════════════════════════════════════════════════════════════════════════════

class CodecDispatchStaticTest : public ::testing::Test {
protected:
    std::string test_dir_;
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (std::filesystem::path(TEST_DIR) / (std::string(info->test_suite_name())
                     + "_" + info->name())).string();
        std::filesystem::create_directories(test_dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }
};

// Natural: Flat file, Disabled reader
TEST_F(CodecDispatchStaticTest, FlatFile_DisabledReader) {
    const auto path = testFile("dispatch_static_flat_disabled.bcsv");
    writeFlatStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyDataStatic(reader);
    reader.close();
}

// Natural: ZoH file, Enabled reader
TEST_F(CodecDispatchStaticTest, ZoHFile_EnabledReader) {
    const auto path = testFile("dispatch_static_zoh_enabled.bcsv");
    writeZoHStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyDataStatic(reader);
    reader.close();
}

// Cross: Flat file, Enabled reader
TEST_F(CodecDispatchStaticTest, FlatFile_EnabledReader) {
    const auto path = testFile("dispatch_static_flat_enabled.bcsv");
    writeFlatStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyDataStatic(reader);
    reader.close();
}

// Cross: ZoH file, Disabled reader
TEST_F(CodecDispatchStaticTest, ZoHFile_DisabledReader) {
    const auto path = testFile("dispatch_static_zoh_disabled.bcsv");
    writeZoHStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyDataStatic(reader);
    reader.close();
}


// ════════════════════════════════════════════════════════════════════════════
// CodecDispatch unit tests (direct API, without Writer/Reader)
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDispatchUnitTest, DefaultState) {
    bcsv::RowCodecDispatch<bcsv::Layout> dispatch;
    EXPECT_FALSE(dispatch.isSetup());
    EXPECT_FALSE(dispatch.isZoH());
    EXPECT_FALSE(dispatch.isFlat());
}

TEST(CodecDispatchUnitTest, SelectFlat) {
    auto layout = createFlexLayout();
    bcsv::RowCodecDispatch<bcsv::Layout> dispatch;
    dispatch.selectCodec(bcsv::FileFlags::NONE, layout);
    EXPECT_TRUE(dispatch.isSetup());
    EXPECT_TRUE(dispatch.isFlat());
    EXPECT_FALSE(dispatch.isZoH());
}

TEST(CodecDispatchUnitTest, SelectZoH) {
    auto layout = createFlexLayout();
    bcsv::RowCodecDispatch<bcsv::Layout> dispatch;
    dispatch.selectCodec(bcsv::FileFlags::ZERO_ORDER_HOLD, layout);
    EXPECT_TRUE(dispatch.isSetup());
    EXPECT_FALSE(dispatch.isFlat());
    EXPECT_TRUE(dispatch.isZoH());
}

TEST(CodecDispatchUnitTest, ReSelect) {
    auto layout = createFlexLayout();
    bcsv::RowCodecDispatch<bcsv::Layout> dispatch;

    // First select flat
    dispatch.selectCodec(bcsv::FileFlags::NONE, layout);
    EXPECT_TRUE(dispatch.isFlat());

    // Re-select to ZoH (tests destroy + placement new)
    dispatch.selectCodec(bcsv::FileFlags::ZERO_ORDER_HOLD, layout);
    EXPECT_TRUE(dispatch.isZoH());
}
