/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 */

#include <gtest/gtest.h>

#include <array>
#include <string>

#include "bcsv/bcsv.h"

namespace {

using bcsv::ByteBuffer;
using bcsv::ColumnType;
using bcsv::Layout;
using bcsv::LayoutStatic;
using bcsv::Row;
using bcsv::RowCodecZoH001;
using bcsv::RowStatic;

size_t headerSizeFor(const Layout& layout) {
    return (layout.columnCount() + 7) / 8;
}

class CodecZoH001DynamicTest : public ::testing::Test {
protected:
    CodecZoH001DynamicTest() : layout_({
        {"b1", ColumnType::BOOL},
        {"i32", ColumnType::INT32},
        {"d", ColumnType::DOUBLE},
        {"f", ColumnType::FLOAT},
        {"i16", ColumnType::INT16},
        {"b2", ColumnType::BOOL},
        {"s", ColumnType::STRING}
    }) {}

    Layout layout_;
};

TEST_F(CodecZoH001DynamicTest, Serialize_AllChanged) {
    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 42);
    row.set<double>(2, 3.14);
    row.set<float>(3, 1.25f);
    row.set<int16_t>(4, -7);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "hello");

    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
    EXPECT_GT(wire.size(), headerSizeFor(layout_));
}

TEST_F(CodecZoH001DynamicTest, Serialize_PartialChanges) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row first(layout_);
    first.set<bool>(0, true);
    first.set<int32_t>(1, 99);
    first.set<double>(2, 2.0);
    first.set<float>(3, 3.0f);
    first.set<int16_t>(4, 4);
    first.set<bool>(5, false);
    first.set<std::string_view>(6, "orig");

    ByteBuffer buf;
    auto wire1 = codec.serialize(first, buf);
    ASSERT_FALSE(wire1.empty());

    Row second(layout_);
    second.set<bool>(0, false);
    second.set<int32_t>(1, 200);
    second.set<double>(2, 2.0);
    second.set<float>(3, 3.0f);
    second.set<int16_t>(4, 4);
    second.set<bool>(5, false);
    second.set<std::string_view>(6, "updated");

    auto wire2 = codec.serialize(second, buf);
    EXPECT_FALSE(wire2.empty());
    EXPECT_GT(wire2.size(), headerSizeFor(layout_));
    EXPECT_LT(wire2.size(), wire1.size());
}

TEST_F(CodecZoH001DynamicTest, Serialize_NoChanges_HeaderOnly) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row first(layout_);
    first.set<bool>(0, false);
    first.set<int32_t>(1, 10);
    first.set<double>(2, 2.0);
    first.set<float>(3, 3.0f);
    first.set<int16_t>(4, 4);
    first.set<bool>(5, false);
    first.set<std::string_view>(6, "same");

    ByteBuffer buf;
    auto wire1 = codec.serialize(first, buf);
    ASSERT_FALSE(wire1.empty());

    Row second(layout_);
    second.set<bool>(0, false);
    second.set<int32_t>(1, 10);
    second.set<double>(2, 2.0);
    second.set<float>(3, 3.0f);
    second.set<int16_t>(4, 4);
    second.set<bool>(5, false);
    second.set<std::string_view>(6, "same");

    auto wire2 = codec.serialize(second, buf);
    EXPECT_EQ(wire2.size(), headerSizeFor(layout_));
}

TEST_F(CodecZoH001DynamicTest, Serialize_BoolOnlyChanges) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row first(layout_);
    first.set<bool>(0, false);
    first.set<int32_t>(1, 10);
    first.set<double>(2, 20.0);
    first.set<float>(3, 30.0f);
    first.set<int16_t>(4, 40);
    first.set<bool>(5, true);
    first.set<std::string_view>(6, "same");

    ByteBuffer buf;
    auto wire1 = codec.serialize(first, buf);
    ASSERT_FALSE(wire1.empty());

    Row second(layout_);
    second.set<bool>(0, true);
    second.set<int32_t>(1, 10);
    second.set<double>(2, 20.0);
    second.set<float>(3, 30.0f);
    second.set<int16_t>(4, 40);
    second.set<bool>(5, false);
    second.set<std::string_view>(6, "same");

    auto wire2 = codec.serialize(second, buf);
    EXPECT_EQ(wire2.size(), headerSizeFor(layout_));
}

TEST_F(CodecZoH001DynamicTest, Serialize_BoolTrueToFalse_HeaderOnlyNonEmpty) {
    Layout layout({{"flag", ColumnType::BOOL}, {"value", ColumnType::INT32}, {"name", ColumnType::STRING}});
    RowCodecZoH001<Layout> codec;
    codec.setup(layout);

    Row first(layout);
    first.set<bool>(0, true);
    first.set<int32_t>(1, 7);
    first.set<std::string_view>(2, "same");

    ByteBuffer buf;
    auto wire1 = codec.serialize(first, buf);
    ASSERT_FALSE(wire1.empty());

    Row second(layout);
    second.set<bool>(0, false);
    second.set<int32_t>(1, 7);
    second.set<std::string_view>(2, "same");

    auto wire2 = codec.serialize(second, buf);
    EXPECT_FALSE(wire2.empty());
    EXPECT_EQ(wire2.size(), headerSizeFor(layout));

    Row out(layout);
    out.set<bool>(0, true);
    out.set<int32_t>(1, 7);
    out.set<std::string_view>(2, "same");
    codec.deserialize(wire2, out);
    EXPECT_FALSE(out.get<bool>(0));
}

TEST_F(CodecZoH001DynamicTest, Deserialize_AllChanged) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 12345);
    row.set<double>(2, -1.5);
    row.set<float>(3, 9.25f);
    row.set<int16_t>(4, -44);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "deserialize");

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row out(layout_);
    codec.reset();
    codec.deserialize(wire, out);

    EXPECT_EQ(out.get<bool>(0), true);
    EXPECT_EQ(out.get<int32_t>(1), 12345);
    EXPECT_DOUBLE_EQ(out.get<double>(2), -1.5);
    EXPECT_FLOAT_EQ(out.get<float>(3), 9.25f);
    EXPECT_EQ(out.get<int16_t>(4), -44);
    EXPECT_EQ(out.get<bool>(5), true);
    EXPECT_EQ(out.get<std::string>(6), "deserialize");
}

TEST_F(CodecZoH001DynamicTest, Deserialize_PartialChanges) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row row1(layout_);
    row1.set<bool>(0, true);
    row1.set<int32_t>(1, 99);
    row1.set<double>(2, 2.0);
    row1.set<float>(3, 3.0f);
    row1.set<int16_t>(4, 4);
    row1.set<bool>(5, false);
    row1.set<std::string_view>(6, "orig");

    ByteBuffer buf;
    auto wire1 = codec.serialize(row1, buf);
    ASSERT_FALSE(wire1.empty());

    ByteBuffer wire1Copy(wire1.begin(), wire1.end());

    Row row2(layout_);
    row2.set<bool>(0, false);
    row2.set<int32_t>(1, 200);
    row2.set<double>(2, 2.0);
    row2.set<float>(3, 3.0f);
    row2.set<int16_t>(4, 4);
    row2.set<bool>(5, false);
    row2.set<std::string_view>(6, "updated");

    auto wire2 = codec.serialize(row2, buf);
    ASSERT_FALSE(wire2.empty());

    ByteBuffer wire2Copy(wire2.begin(), wire2.end());

    Row out(layout_);
    codec.reset();
    codec.deserialize(std::span<const std::byte>(wire1Copy.data(), wire1Copy.size()), out);
    codec.deserialize(std::span<const std::byte>(wire2Copy.data(), wire2Copy.size()), out);

    EXPECT_FALSE(out.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), 200);
    EXPECT_DOUBLE_EQ(out.get<double>(2), 2.0);
    EXPECT_FLOAT_EQ(out.get<float>(3), 3.0f);
    EXPECT_EQ(out.get<int16_t>(4), 4);
    EXPECT_FALSE(out.get<bool>(5));
    EXPECT_EQ(out.get<std::string>(6), "updated");
}

TEST_F(CodecZoH001DynamicTest, Roundtrip) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, -2147483648);
    row.set<double>(2, 1e308);
    row.set<float>(3, -1e10f);
    row.set<int16_t>(4, 32767);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "roundtrip");

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    Row out(layout_);
    codec.reset();
    codec.deserialize(wire, out);

    EXPECT_EQ(out.get<bool>(0), row.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), row.get<int32_t>(1));
    EXPECT_DOUBLE_EQ(out.get<double>(2), row.get<double>(2));
    EXPECT_FLOAT_EQ(out.get<float>(3), row.get<float>(3));
    EXPECT_EQ(out.get<int16_t>(4), row.get<int16_t>(4));
    EXPECT_EQ(out.get<bool>(5), row.get<bool>(5));
    EXPECT_EQ(out.get<std::string>(6), row.get<std::string>(6));
}

TEST_F(CodecZoH001DynamicTest, MultiRowLifecycle) {
    RowCodecZoH001<Layout> codec;
    codec.setup(layout_);

    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 100);
    row.set<double>(2, 1.0);
    row.set<float>(3, 1.0f);
    row.set<int16_t>(4, 1);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "first");

    ByteBuffer b1;
    auto wire1 = codec.serialize(row, b1);
    EXPECT_FALSE(wire1.empty());

    row.set<bool>(0, false);
    row.set<int32_t>(1, 200);
    ByteBuffer b2;
    auto wire2 = codec.serialize(row, b2);
    EXPECT_FALSE(wire2.empty());

    ByteBuffer b3;
    auto wire3 = codec.serialize(row, b3);
    EXPECT_EQ(wire3.size(), headerSizeFor(layout_));

    codec.reset();
    row.set<bool>(0, true);
    row.set<int32_t>(1, 300);
    ByteBuffer b4;
    auto wire4 = codec.serialize(row, b4);
    EXPECT_GT(wire4.size(), headerSizeFor(layout_));
}

TEST(CodecZoH001EdgeTest, AllBoolLayout) {
    Layout layout({
        {"b0", ColumnType::BOOL}, {"b1", ColumnType::BOOL}, {"b2", ColumnType::BOOL},
        {"b3", ColumnType::BOOL}, {"b4", ColumnType::BOOL}, {"b5", ColumnType::BOOL},
        {"b6", ColumnType::BOOL}, {"b7", ColumnType::BOOL}, {"b8", ColumnType::BOOL}
    });

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);

    Row row(layout);
    for (size_t i = 0; i < 9; ++i) {
        row.set<bool>(i, (i % 2) == 0);
    }

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_EQ(wire.size(), 2u);

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    for (size_t i = 0; i < 9; ++i) {
        EXPECT_EQ(out.get<bool>(i), (i % 2) == 0);
    }
}

TEST(CodecZoH001EdgeTest, AllStringLayout) {
    Layout layout({
        {"s0", ColumnType::STRING},
        {"s1", ColumnType::STRING},
        {"s2", ColumnType::STRING}
    });

    Row row(layout);
    row.set<std::string_view>(0, "first");
    row.set<std::string_view>(1, "");
    row.set<std::string_view>(2, "third string is long");

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_GT(wire.size(), headerSizeFor(layout));

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    EXPECT_EQ(out.get<std::string>(0), "first");
    EXPECT_EQ(out.get<std::string>(1), "");
    EXPECT_EQ(out.get<std::string>(2), "third string is long");
}

TEST(CodecZoH001EdgeTest, AllNumericTypes) {
    Layout layout({
        {"i8", ColumnType::INT8},
        {"i16", ColumnType::INT16},
        {"i32", ColumnType::INT32},
        {"u64", ColumnType::UINT64},
        {"f", ColumnType::FLOAT},
        {"d", ColumnType::DOUBLE}
    });

    Row row(layout);
    row.set<int8_t>(0, -128);
    row.set<int16_t>(1, -32768);
    row.set<int32_t>(2, -2147483647);
    row.set<uint64_t>(3, 1234567890123456789ull);
    row.set<float>(4, 123.25f);
    row.set<double>(5, -1.0e200);

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    EXPECT_EQ(out.get<int8_t>(0), -128);
    EXPECT_EQ(out.get<int16_t>(1), -32768);
    EXPECT_EQ(out.get<int32_t>(2), -2147483647);
    EXPECT_EQ(out.get<uint64_t>(3), 1234567890123456789ull);
    EXPECT_FLOAT_EQ(out.get<float>(4), 123.25f);
    EXPECT_DOUBLE_EQ(out.get<double>(5), -1.0e200);
}

TEST(CodecZoH001EdgeTest, SingleColumn_Int32) {
    Layout layout({{"x", ColumnType::INT32}});
    Row row(layout);
    row.set<int32_t>(0, 42);

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    EXPECT_EQ(out.get<int32_t>(0), 42);
}

TEST(CodecZoH001EdgeTest, SingleColumn_Bool) {
    Layout layout({{"flag", ColumnType::BOOL}});
    Row row(layout);
    row.set<bool>(0, true);

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    EXPECT_TRUE(out.get<bool>(0));
}

TEST(CodecZoH001EdgeTest, SingleColumn_String) {
    Layout layout({{"name", ColumnType::STRING}});
    Row row(layout);
    row.set<std::string_view>(0, "solo");

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    EXPECT_EQ(out.get<std::string>(0), "solo");
}

TEST(CodecZoH001EdgeTest, MultipleRowsSequential) {
    Layout layout({
        {"b", ColumnType::BOOL},
        {"x", ColumnType::INT32},
        {"s", ColumnType::STRING}
    });

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);

    std::vector<ByteBuffer> wires;
    wires.reserve(10);
    for (int v = 0; v < 10; ++v) {
        Row row(layout);
        row.set<bool>(0, v % 2 == 0);
        row.set<int32_t>(1, v * 100);
        row.set<std::string_view>(2, std::string("row_") + std::to_string(v));
        wires.emplace_back();
        auto wire = codec.serialize(row, wires.back());
        EXPECT_FALSE(wire.empty());
    }

    codec.reset();
    Row out(layout);
    for (int v = 0; v < 10; ++v) {
        auto span = std::span<const std::byte>(wires[v].data(), wires[v].size());
        codec.deserialize(span, out);
        EXPECT_EQ(out.get<bool>(0), v % 2 == 0);
        EXPECT_EQ(out.get<int32_t>(1), v * 100);
        EXPECT_EQ(out.get<std::string>(2), std::string("row_") + std::to_string(v));
    }
}

TEST(CodecZoH001EdgeTest, DeserializeBufferTooShort) {
    Layout layout({{"x", ColumnType::INT32}});
    RowCodecZoH001<Layout> codec;
    codec.setup(layout);

    Row out(layout);
    std::array<std::byte, 0> empty{};
    EXPECT_THROW(codec.deserialize(std::span<const std::byte>(empty.data(), empty.size()), out), std::runtime_error);
}

TEST(CodecZoH001EdgeTest, LargeString) {
    Layout layout({{"big", ColumnType::STRING}});
    Row row(layout);
    std::string largeStr(bcsv::MAX_STRING_LENGTH + 64, 'A');
    row.set<std::string_view>(0, largeStr);

    RowCodecZoH001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row out(layout);
    codec.reset();
    codec.deserialize(wire, out);
    EXPECT_EQ(out.get<std::string>(0).size(), bcsv::MAX_STRING_LENGTH);
}

using TestStaticLayout = LayoutStatic<bool, int32_t, double, float, int16_t, bool, std::string>;

TEST(CodecZoH001StaticTest, Serialize_AllChanged) {
    TestStaticLayout layout({"b1", "i32", "d", "f", "i16", "b2", "s"});
    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row(layout);
    row.set<0>(true);
    row.set<1>(42);
    row.set<2>(3.14);
    row.set<3>(1.25f);
    row.set<4>(-7);
    row.set<5>(true);
    row.set<6>(std::string("hello"));

    RowCodecZoH001<TestStaticLayout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
}

TEST(CodecZoH001StaticTest, Serialize_PartialChanges) {
    TestStaticLayout layout({"b1", "i32", "d", "f", "i16", "b2", "s"});
    RowCodecZoH001<TestStaticLayout> codec;
    codec.setup(layout);

    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row1(layout);
    row1.set<0>(true);
    row1.set<1>(99);
    row1.set<2>(2.0);
    row1.set<3>(3.0f);
    row1.set<4>(4);
    row1.set<5>(false);
    row1.set<6>(std::string("orig"));

    ByteBuffer buf;
    auto wire1 = codec.serialize(row1, buf);
    ASSERT_FALSE(wire1.empty());

    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row2(layout);
    row2.set<0>(false);
    row2.set<1>(200);
    row2.set<2>(2.0);
    row2.set<3>(3.0f);
    row2.set<4>(4);
    row2.set<5>(false);
    row2.set<6>(std::string("updated"));

    auto wire2 = codec.serialize(row2, buf);
    EXPECT_LT(wire2.size(), wire1.size());
}

TEST(CodecZoH001StaticTest, Serialize_NoChanges) {
    TestStaticLayout layout({"b1", "i32", "d", "f", "i16", "b2", "s"});
    RowCodecZoH001<TestStaticLayout> codec;
    codec.setup(layout);

    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row1(layout);
    row1.set<0>(false);
    row1.set<1>(10);
    row1.set<2>(2.0);
    row1.set<3>(3.0f);
    row1.set<4>(4);
    row1.set<5>(false);
    row1.set<6>(std::string("same"));

    ByteBuffer buf;
    auto wire1 = codec.serialize(row1, buf);
    ASSERT_FALSE(wire1.empty());

    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row2(layout);
    row2.set<0>(false);
    row2.set<1>(10);
    row2.set<2>(2.0);
    row2.set<3>(3.0f);
    row2.set<4>(4);
    row2.set<5>(false);
    row2.set<6>(std::string("same"));

    auto wire2 = codec.serialize(row2, buf);
    EXPECT_EQ(wire2.size(), 1u);
}

TEST(CodecZoH001StaticTest, Deserialize_AllChanged) {
    TestStaticLayout layout({"b1", "i32", "d", "f", "i16", "b2", "s"});
    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row(layout);
    row.set<0>(true);
    row.set<1>(-999);
    row.set<2>(1e-10);
    row.set<3>(-0.5f);
    row.set<4>(-12);
    row.set<5>(true);
    row.set<6>(std::string("static"));

    RowCodecZoH001<TestStaticLayout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> out(layout);
    codec.reset();
    codec.deserialize(wire, out);

    EXPECT_EQ(out.get<0>(), true);
    EXPECT_EQ(out.get<1>(), -999);
    EXPECT_DOUBLE_EQ(out.get<2>(), 1e-10);
    EXPECT_FLOAT_EQ(out.get<3>(), -0.5f);
    EXPECT_EQ(out.get<4>(), -12);
    EXPECT_EQ(out.get<5>(), true);
    EXPECT_EQ(out.get<6>(), "static");
}

TEST(CodecZoH001StaticTest, Roundtrip) {
    TestStaticLayout layout({"b1", "i32", "d", "f", "i16", "b2", "s"});
    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> row(layout);
    row.set<0>(false);
    row.set<1>(2147483647);
    row.set<2>(-0.0);
    row.set<3>(9.0f);
    row.set<4>(32767);
    row.set<5>(true);
    row.set<6>(std::string("roundtrip_static"));

    RowCodecZoH001<TestStaticLayout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    RowStatic<bool, int32_t, double, float, int16_t, bool, std::string> out(layout);
    codec.reset();
    codec.deserialize(wire, out);

    EXPECT_EQ(out.get<0>(), row.get<0>());
    EXPECT_EQ(out.get<1>(), row.get<1>());
    EXPECT_DOUBLE_EQ(out.get<2>(), row.get<2>());
    EXPECT_FLOAT_EQ(out.get<3>(), row.get<3>());
    EXPECT_EQ(out.get<4>(), row.get<4>());
    EXPECT_EQ(out.get<5>(), row.get<5>());
    EXPECT_EQ(out.get<6>(), row.get<6>());
}

TEST(CodecZoH001StaticTest, MultipleRowsSequential) {
    using MiniLayout = LayoutStatic<bool, int32_t, std::string>;
    MiniLayout layout({"b", "x", "s"});

    RowCodecZoH001<MiniLayout> codec;
    codec.setup(layout);

    std::vector<ByteBuffer> wires;
    wires.reserve(10);
    for (int v = 0; v < 10; ++v) {
        RowStatic<bool, int32_t, std::string> row(layout);
        row.set<0>(v % 2 == 0);
        row.set<1>(v * 100);
        row.set<2>(std::string("srow_") + std::to_string(v));
        wires.emplace_back();
        auto wire = codec.serialize(row, wires.back());
        EXPECT_FALSE(wire.empty());
    }

    codec.reset();
    RowStatic<bool, int32_t, std::string> out(layout);
    for (int v = 0; v < 10; ++v) {
        auto span = std::span<const std::byte>(wires[v].data(), wires[v].size());
        codec.deserialize(span, out);
        EXPECT_EQ(out.get<0>(), v % 2 == 0);
        EXPECT_EQ(out.get<1>(), v * 100);
        EXPECT_EQ(out.get<2>(), std::string("srow_") + std::to_string(v));
    }
}

}  // namespace

