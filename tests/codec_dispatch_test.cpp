/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "bcsv/bcsv.h"

namespace {

struct RowData {
    bool b1;
    int32_t i32;
    double d;
    std::string s;
};

const std::vector<RowData> TEST_DATA = {
    {true, 42, 3.14, "hello"},
    {false, -100, 2.718, "world"},
    {true, 0, 0.0, ""},
    {false, 999, -1.5, "bcsv"},
    {true, 42, 3.14, "hello"},
    {true, 42, 3.14, "hello"},
    {false, -999, 100.0, "changed"},
};

bcsv::Layout createFlexLayout() {
    bcsv::Layout layout;
    layout.addColumn({"b1", bcsv::ColumnType::BOOL});
    layout.addColumn({"i32", bcsv::ColumnType::INT32});
    layout.addColumn({"d", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"s", bcsv::ColumnType::STRING});
    return layout;
}

using StaticLayout = bcsv::LayoutStatic<bool, int32_t, double, std::string>;

StaticLayout createStaticLayout() {
    return StaticLayout({"b1", "i32", "d", "s"});
}

const std::string TEST_DIR = "bcsv_test_files";

std::string testPath(const std::string& name) {
    std::filesystem::create_directories(TEST_DIR);
    return TEST_DIR + "/" + name;
}

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
    bcsv::Writer<bcsv::Layout> writer(layout);
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
    bcsv::Writer<StaticLayout> writer(layout);
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

template<typename ReaderType>
void verifyData(ReaderType& reader) {
    size_t count = 0;
    while (reader.readNext()) {
        ASSERT_LT(count, TEST_DATA.size()) << "Too many rows read";
        const auto& expected = TEST_DATA[count];
        const auto& row = reader.row();

        EXPECT_EQ(row.template get<bool>(0), expected.b1) << "row " << count << " b1";
        EXPECT_EQ(row.template get<int32_t>(1), expected.i32) << "row " << count << " i32";
        EXPECT_DOUBLE_EQ(row.template get<double>(2), expected.d) << "row " << count << " d";
        EXPECT_EQ(row.template get<std::string>(3), expected.s) << "row " << count << " s";
        ++count;
    }
    EXPECT_EQ(count, TEST_DATA.size()) << "Row count mismatch";
}

class CodecDispatchFlexTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories(TEST_DIR);
    }
};

TEST_F(CodecDispatchFlexTest, FlatFile_ReadsCorrectly) {
    const auto path = testPath("dispatch_flat_dynamic.bcsv");
    writeFlatFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyData(reader);
    reader.close();
}

TEST_F(CodecDispatchFlexTest, ZoHFile_ReadsCorrectly) {
    const auto path = testPath("dispatch_zoh_dynamic.bcsv");
    writeZoHFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyData(reader);
    reader.close();
}

TEST_F(CodecDispatchFlexTest, FlatFile_OpenStateIsValid) {
    const auto path = testPath("dispatch_flat_dynamic_open.bcsv");
    writeFlatFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_TRUE(reader.isOpen());
    verifyData(reader);
    reader.close();
}

TEST_F(CodecDispatchFlexTest, ZoHFile_OpenStateIsValid) {
    const auto path = testPath("dispatch_zoh_dynamic_open.bcsv");
    writeZoHFlexible(path);

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_TRUE(reader.isOpen());
    verifyData(reader);
    reader.close();
}

class CodecDispatchStaticTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories(TEST_DIR);
    }
};

TEST_F(CodecDispatchStaticTest, FlatFile_ReadsCorrectly) {
    const auto path = testPath("dispatch_flat_static.bcsv");
    writeFlatStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyData(reader);
    reader.close();
}

TEST_F(CodecDispatchStaticTest, ZoHFile_ReadsCorrectly) {
    const auto path = testPath("dispatch_zoh_static.bcsv");
    writeZoHStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    verifyData(reader);
    reader.close();
}

TEST_F(CodecDispatchStaticTest, FlatFile_OpenStateIsValid) {
    const auto path = testPath("dispatch_flat_static_open.bcsv");
    writeFlatStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_TRUE(reader.isOpen());
    verifyData(reader);
    reader.close();
}

TEST_F(CodecDispatchStaticTest, ZoHFile_OpenStateIsValid) {
    const auto path = testPath("dispatch_zoh_static_open.bcsv");
    writeZoHStatic(path);

    bcsv::Reader<StaticLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    EXPECT_TRUE(reader.isOpen());
    verifyData(reader);
    reader.close();
}

TEST(CodecDispatchUnitTest, DefaultState) {
    bcsv::CodecDispatch<bcsv::Layout> dispatch;
    EXPECT_FALSE(dispatch.isSetup());
    EXPECT_FALSE(dispatch.isZoH());
    EXPECT_FALSE(dispatch.isFlat());
}

TEST(CodecDispatchUnitTest, SelectFlat) {
    auto layout = createFlexLayout();
    bcsv::CodecDispatch<bcsv::Layout> dispatch;
    dispatch.selectCodec(bcsv::FileFlags::NONE, layout);

    EXPECT_TRUE(dispatch.isSetup());
    EXPECT_TRUE(dispatch.isFlat());
    EXPECT_FALSE(dispatch.isZoH());
}

TEST(CodecDispatchUnitTest, SelectZoH) {
    auto layout = createFlexLayout();
    bcsv::CodecDispatch<bcsv::Layout> dispatch;
    dispatch.selectCodec(bcsv::FileFlags::ZERO_ORDER_HOLD, layout);

    EXPECT_TRUE(dispatch.isSetup());
    EXPECT_TRUE(dispatch.isZoH());
    EXPECT_FALSE(dispatch.isFlat());
}

TEST(CodecDispatchUnitTest, ReSelect) {
    auto layout = createFlexLayout();
    bcsv::CodecDispatch<bcsv::Layout> dispatch;

    dispatch.selectCodec(bcsv::FileFlags::NONE, layout);
    EXPECT_TRUE(dispatch.isFlat());

    dispatch.selectCodec(bcsv::FileFlags::ZERO_ORDER_HOLD, layout);
    EXPECT_TRUE(dispatch.isZoH());

    dispatch.selectCodec(bcsv::FileFlags::NONE, layout);
    EXPECT_TRUE(dispatch.isFlat());
}

TEST(CodecDispatchUnitTest, SerializeDeserializeViaDispatch) {
    auto layout = createFlexLayout();
    bcsv::CodecDispatch<bcsv::Layout> dispatch;
    dispatch.selectCodec(bcsv::FileFlags::ZERO_ORDER_HOLD, layout);

    bcsv::Row in(layout);
    in.set<bool>(0, true);
    in.set<int32_t>(1, 11);
    in.set<double>(2, 4.5);
    in.set<std::string_view>(3, "abc");

    bcsv::ByteBuffer buf;
    auto wire = dispatch.serialize(in, buf);
    ASSERT_FALSE(wire.empty());

    bcsv::Row out(layout);
    dispatch.reset();
    dispatch.deserialize(wire, out);

    EXPECT_TRUE(out.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), 11);
    EXPECT_DOUBLE_EQ(out.get<double>(2), 4.5);
    EXPECT_EQ(out.get<std::string>(3), "abc");
}

}  // namespace

