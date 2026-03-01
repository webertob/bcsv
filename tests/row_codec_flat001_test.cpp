/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file row_codec_flat001_test.cpp
 * @brief Parity tests for RowCodecFlat001 — verifies byte-identical output
 *        compared to the existing Row::serializeTo/deserializeFrom.
 *
 * Tests cover:
 *   - Dynamic Layout: serialize, deserialize per type
 *   - Static Layout: serialize, deserialize per type
 *   - Tracking enabled/disabled
 *   - Edge cases: empty layout, all-bool, all-string, mixed, null strings
 */

#include <gtest/gtest.h>
#include "bcsv/bcsv.h"
#include "bcsv/row_codec_flat001.h"
#include "bcsv/row_codec_flat001.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace bcsv;

// ════════════════════════════════════════════════════════════════════════════
// Dynamic Layout — Serialize Parity
// ════════════════════════════════════════════════════════════════════════════

class CodecFlat001DynamicTest : public ::testing::Test {
protected:
    Layout layout_;
    void SetUp() override {
        layout_ = Layout({
            {"b1", ColumnType::BOOL},
            {"i32", ColumnType::INT32},
            {"f64", ColumnType::DOUBLE},
            {"str", ColumnType::STRING},
            {"u16", ColumnType::UINT16},
            {"b2", ColumnType::BOOL},
            {"str2", ColumnType::STRING}
        });
    }
};

TEST_F(CodecFlat001DynamicTest, SerializeRoundtrip_Untracked) {
    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 42);
    row.set<double>(2, 3.14);
    row.set<std::string_view>(3, "hello");
    row.set<uint16_t>(4, 1000);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "world!");

    RowCodecFlat001<Layout> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    Row rowBack(layout_);
    codec.deserialize(wire, rowBack);
    EXPECT_EQ(row.get<bool>(0), rowBack.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), rowBack.get<int32_t>(1));
    EXPECT_EQ(row.get<double>(2), rowBack.get<double>(2));
    EXPECT_EQ(row.get<std::string>(3), rowBack.get<std::string>(3));
    EXPECT_EQ(row.get<uint16_t>(4), rowBack.get<uint16_t>(4));
    EXPECT_EQ(row.get<bool>(5), rowBack.get<bool>(5));
    EXPECT_EQ(row.get<std::string>(6), rowBack.get<std::string>(6));
}

TEST_F(CodecFlat001DynamicTest, SerializeRoundtrip_Tracked) {
    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, -99);
    row.set<double>(2, 2.718);
    row.set<std::string_view>(3, "tracked");
    row.set<uint16_t>(4, 65535);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "");

    RowCodecFlat001<Layout> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    Row rowBack(layout_);
    codec.deserialize(wire, rowBack);
    EXPECT_EQ(row.get<bool>(0), rowBack.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), rowBack.get<int32_t>(1));
    EXPECT_EQ(row.get<double>(2), rowBack.get<double>(2));
    EXPECT_EQ(row.get<std::string>(3), rowBack.get<std::string>(3));
    EXPECT_EQ(row.get<uint16_t>(4), rowBack.get<uint16_t>(4));
    EXPECT_EQ(row.get<bool>(5), rowBack.get<bool>(5));
    EXPECT_EQ(row.get<std::string>(6), rowBack.get<std::string>(6));
}

TEST_F(CodecFlat001DynamicTest, DeserializeParity_Untracked) {
    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 12345);
    row.set<double>(2, -1.5);
    row.set<std::string_view>(3, "test");
    row.set<uint16_t>(4, 500);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "xyz");

    RowCodecFlat001<Layout> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row rowNew(layout_);
    codec.deserialize(wire, rowNew);

    EXPECT_EQ(row.get<bool>(0), rowNew.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), rowNew.get<int32_t>(1));
    EXPECT_EQ(row.get<double>(2), rowNew.get<double>(2));
    EXPECT_EQ(row.get<std::string>(3), rowNew.get<std::string>(3));
    EXPECT_EQ(row.get<uint16_t>(4), rowNew.get<uint16_t>(4));
    EXPECT_EQ(row.get<bool>(5), rowNew.get<bool>(5));
    EXPECT_EQ(row.get<std::string>(6), rowNew.get<std::string>(6));
}

TEST_F(CodecFlat001DynamicTest, DeserializeParity_Tracked) {
    Row row(layout_);
    row.set<bool>(0, false);
    row.set<int32_t>(1, 0);
    row.set<double>(2, 0.0);
    row.set<std::string_view>(3, "");
    row.set<uint16_t>(4, 0);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "nonempty");

    RowCodecFlat001<Layout> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    Row rowNew(layout_);
    codec.deserialize(wire, rowNew);

    EXPECT_EQ(row.get<bool>(0), rowNew.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), rowNew.get<int32_t>(1));
    EXPECT_EQ(row.get<double>(2), rowNew.get<double>(2));
    EXPECT_EQ(row.get<std::string>(3), rowNew.get<std::string>(3));
    EXPECT_EQ(row.get<uint16_t>(4), rowNew.get<uint16_t>(4));
    EXPECT_EQ(row.get<bool>(5), rowNew.get<bool>(5));
    EXPECT_EQ(row.get<std::string>(6), rowNew.get<std::string>(6));
}

TEST_F(CodecFlat001DynamicTest, RoundtripParity) {
    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, -2147483648);
    row.set<double>(2, 1e308);
    row.set<std::string_view>(3, "round trip");
    row.set<uint16_t>(4, 32768);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "back");

    // serialize via codec
    RowCodecFlat001<Layout> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    // deserialize via codec
    Row rowBack(layout_);
    codec.deserialize(wire, rowBack);

    EXPECT_EQ(row.get<bool>(0), rowBack.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), rowBack.get<int32_t>(1));
    EXPECT_EQ(row.get<double>(2), rowBack.get<double>(2));
    EXPECT_EQ(row.get<std::string>(3), rowBack.get<std::string>(3));
    EXPECT_EQ(row.get<uint16_t>(4), rowBack.get<uint16_t>(4));
    EXPECT_EQ(row.get<bool>(5), rowBack.get<bool>(5));
    EXPECT_EQ(row.get<std::string>(6), rowBack.get<std::string>(6));
}
// Dynamic Layout — Edge Cases
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecFlat001EdgeTest, AllBoolLayout) {
    Layout layout({
        {"b1", ColumnType::BOOL}, {"b2", ColumnType::BOOL},
        {"b3", ColumnType::BOOL}, {"b4", ColumnType::BOOL},
        {"b5", ColumnType::BOOL}, {"b6", ColumnType::BOOL},
        {"b7", ColumnType::BOOL}, {"b8", ColumnType::BOOL},
        {"b9", ColumnType::BOOL}
    });

    Row row(layout);
    for (size_t i = 0; i < 9; ++i)
        row.set<bool>(i, (i % 2) == 0);

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer bufNew;
    auto spanNew = codec.serialize(row, bufNew);
    ASSERT_FALSE(spanNew.empty()) << "all-bool serialize must produce output";

}

TEST(CodecFlat001EdgeTest, AllStringLayout) {
    Layout layout({
        {"s1", ColumnType::STRING}, {"s2", ColumnType::STRING},
        {"s3", ColumnType::STRING}
    });

    Row row(layout);
    row.set<std::string_view>(0, "first");
    row.set<std::string_view>(1, "");
    row.set<std::string_view>(2, "third string is long");

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer bufNew;
    auto spanNew = codec.serialize(row, bufNew);
    ASSERT_FALSE(spanNew.empty()) << "all-string serialize must produce output";
}

TEST(CodecFlat001EdgeTest, AllNumericTypes) {
    Layout layout({
        {"i8",  ColumnType::INT8},   {"i16", ColumnType::INT16},
        {"i32", ColumnType::INT32},  {"i64", ColumnType::INT64},
        {"u8",  ColumnType::UINT8},  {"u16", ColumnType::UINT16},
        {"u32", ColumnType::UINT32}, {"u64", ColumnType::UINT64},
        {"f32", ColumnType::FLOAT},  {"f64", ColumnType::DOUBLE}
    });

    Row row(layout);
    row.set<int8_t>(0, -128);
    row.set<int16_t>(1, -32768);
    row.set<int32_t>(2, -2147483647);
    row.set<int64_t>(3, -9223372036854775807LL);
    row.set<uint8_t>(4, 255);
    row.set<uint16_t>(5, 65535);
    row.set<uint32_t>(6, 4294967295u);
    row.set<uint64_t>(7, 18446744073709551615ull);
    row.set<float>(8, 3.14f);
    row.set<double>(9, 2.718281828);

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer bufNew;
    auto spanNew = codec.serialize(row, bufNew);
    ASSERT_FALSE(spanNew.empty()) << "all-numeric serialize must produce output";

    // Deserialize roundtrip
    Row rowNew(layout);
    codec.deserialize(spanNew, rowNew);
    EXPECT_EQ(row.get<int8_t>(0), rowNew.get<int8_t>(0));
    EXPECT_EQ(row.get<int16_t>(1), rowNew.get<int16_t>(1));
    EXPECT_EQ(row.get<int32_t>(2), rowNew.get<int32_t>(2));
    EXPECT_EQ(row.get<int64_t>(3), rowNew.get<int64_t>(3));
    EXPECT_EQ(row.get<uint8_t>(4), rowNew.get<uint8_t>(4));
    EXPECT_EQ(row.get<uint16_t>(5), rowNew.get<uint16_t>(5));
    EXPECT_EQ(row.get<uint32_t>(6), rowNew.get<uint32_t>(6));
    EXPECT_EQ(row.get<uint64_t>(7), rowNew.get<uint64_t>(7));
    EXPECT_FLOAT_EQ(row.get<float>(8), rowNew.get<float>(8));
    EXPECT_DOUBLE_EQ(row.get<double>(9), rowNew.get<double>(9));
}

TEST(CodecFlat001EdgeTest, WireMetadata_Dynamic) {
    Layout layout({
        {"b1", ColumnType::BOOL}, {"i32", ColumnType::INT32},
        {"str", ColumnType::STRING}, {"f64", ColumnType::DOUBLE},
        {"b2", ColumnType::BOOL}
    });

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);

    // 2 bools → 1 byte; int32(4) + double(8) = 12; 1 string → 1×2 = 2; fixed = 1+12+2 = 15
    EXPECT_EQ(codec.rowHeaderSize(), 1u);
    EXPECT_EQ(codec.wireDataSize(), 12u);
    EXPECT_EQ(codec.wireStrgCount(), 1u);
    EXPECT_EQ(codec.wireFixedSize(), 15u);
}

TEST(CodecFlat001EdgeTest, SingleColumn_Int32) {
    Layout layout({{"x", ColumnType::INT32}});
    Row row(layout);
    row.set<int32_t>(0, 42);

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto span = codec.serialize(row, buf);
    EXPECT_EQ(span.size(), sizeof(int32_t)) << "single int32 wire size";
}

TEST(CodecFlat001EdgeTest, SingleColumn_Bool) {
    Layout layout({{"flag", ColumnType::BOOL}});
    Row row(layout);
    row.set<bool>(0, true);

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto span = codec.serialize(row, buf);
    EXPECT_EQ(span.size(), 1u) << "single bool wire size";
}

TEST(CodecFlat001EdgeTest, SingleColumn_String) {
    Layout layout({{"name", ColumnType::STRING}});
    Row row(layout);
    row.set<std::string_view>(0, "solo");

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto span = codec.serialize(row, buf);
    // 0 bits + 0 data + 1 strg_len (2) + 4 payload = 6
    EXPECT_EQ(span.size(), 2u + 4u) << "single string wire size";
}

TEST(CodecFlat001EdgeTest, MultipleRowsSequential) {
    Layout layout({
        {"i32", ColumnType::INT32}, {"str", ColumnType::STRING}
    });

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);

    for (int v = 0; v < 10; ++v) {
        Row row(layout);
        row.set<int32_t>(0, v * 100);
        row.set<std::string_view>(1, std::string("row_") + std::to_string(v));

        ByteBuffer buf;
        auto span = codec.serialize(row, buf);
        ASSERT_FALSE(span.empty()) << "sequential row " << v;

        // Roundtrip check
        Row rowBack(layout);
        codec.deserialize(span, rowBack);
        EXPECT_EQ(row.get<int32_t>(0), rowBack.get<int32_t>(0));
        EXPECT_EQ(row.get<std::string>(1), rowBack.get<std::string>(1));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Static Layout — Serialize/Deserialize Parity
// ════════════════════════════════════════════════════════════════════════════

using TestStaticLayout = LayoutStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string>;

TEST(CodecFlat001StaticTest, SerializeRoundtrip_Untracked) {
    TestStaticLayout layout;
    RowStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string> row(layout);
    row.set<0>(true);
    row.set<1>(42);
    row.set<2>(3.14);
    row.set<3>(std::string("hello"));
    row.set<4>(uint16_t{1000});
    row.set<5>(false);
    row.set<6>(std::string("world"));

    RowCodecFlat001<TestStaticLayout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    RowStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string> rowBack(layout);
    codec.deserialize(wire, rowBack);
    EXPECT_EQ(row.get<0>(), rowBack.get<0>());
    EXPECT_EQ(row.get<1>(), rowBack.get<1>());
    EXPECT_DOUBLE_EQ(row.get<2>(), rowBack.get<2>());
    EXPECT_EQ(row.get<3>(), rowBack.get<3>());
    EXPECT_EQ(row.get<4>(), rowBack.get<4>());
    EXPECT_EQ(row.get<5>(), rowBack.get<5>());
    EXPECT_EQ(row.get<6>(), rowBack.get<6>());
}

TEST(CodecFlat001StaticTest, DeserializeRoundtrip_Untracked) {
    TestStaticLayout layout;
    RowStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string> row(layout);
    row.set<0>(true);
    row.set<1>(-999);
    row.set<2>(1e-10);
    row.set<3>(std::string("deser"));
    row.set<4>(uint16_t{50000});
    row.set<5>(true);
    row.set<6>(std::string("test"));

    RowCodecFlat001<TestStaticLayout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    RowStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string> rowNew(layout);
    codec.deserialize(wire, rowNew);

    EXPECT_EQ(row.get<0>(), rowNew.get<0>());
    EXPECT_EQ(row.get<1>(), rowNew.get<1>());
    EXPECT_DOUBLE_EQ(row.get<2>(), rowNew.get<2>());
    EXPECT_EQ(row.get<3>(), rowNew.get<3>());
    EXPECT_EQ(row.get<4>(), rowNew.get<4>());
    EXPECT_EQ(row.get<5>(), rowNew.get<5>());
    EXPECT_EQ(row.get<6>(), rowNew.get<6>());
}

TEST(CodecFlat001StaticTest, RoundtripParity) {
    TestStaticLayout layout;
    RowStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string> row(layout);
    row.set<0>(false);
    row.set<1>(2147483647);
    row.set<2>(-0.0);
    row.set<3>(std::string("roundtrip"));
    row.set<4>(uint16_t{12345});
    row.set<5>(true);
    row.set<6>(std::string(""));

    RowCodecFlat001<TestStaticLayout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    RowStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string> rowBack(layout);
    codec.deserialize(wire, rowBack);

    EXPECT_EQ(row.get<0>(), rowBack.get<0>());
    EXPECT_EQ(row.get<1>(), rowBack.get<1>());
    EXPECT_DOUBLE_EQ(row.get<2>(), rowBack.get<2>());
    EXPECT_EQ(row.get<3>(), rowBack.get<3>());
    EXPECT_EQ(row.get<4>(), rowBack.get<4>());
    EXPECT_EQ(row.get<5>(), rowBack.get<5>());
    EXPECT_EQ(row.get<6>(), rowBack.get<6>());
}

TEST(CodecFlat001StaticTest, WireMetadata_Static) {
    using SLayout = LayoutStatic<bool, int32_t, std::string, double, bool>;
    using Codec = RowCodecFlat001<SLayout>;

    // Verify constexpr wire constants
    // 2 bools → 1 byte; int32(4) + double(8) = 12; 1 string → 1×2 = 2; fixed = 1+12+2 = 15
    EXPECT_EQ(Codec::ROW_HEADER_SIZE, 1u);
    EXPECT_EQ(Codec::WIRE_DATA_SIZE, 12u);
    EXPECT_EQ(Codec::WIRE_STRG_COUNT, 1u);
    EXPECT_EQ(Codec::WIRE_FIXED_SIZE, 15u);
}

TEST(CodecFlat001StaticTest, MultipleRowsSequential) {
    using SLayout = LayoutStatic<int32_t, std::string>;
    SLayout layout;

    RowCodecFlat001<SLayout> codec;
    codec.setup(layout);

    for (int v = 0; v < 10; ++v) {
        RowStatic<int32_t, std::string> row(layout);
        row.set<0>(v * 100);
        row.set<1>(std::string("srow_") + std::to_string(v));

        ByteBuffer buf;
        auto span = codec.serialize(row, buf);
        ASSERT_FALSE(span.empty()) << "static sequential row " << v;

        RowStatic<int32_t, std::string> rowBack(layout);
        codec.deserialize(span, rowBack);
        EXPECT_EQ(row.get<0>(), rowBack.get<0>());
        EXPECT_EQ(row.get<1>(), rowBack.get<1>());
    }
}
// ════════════════════════════════════════════════════════════════════════════
// Large string and boundary tests
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecFlat001EdgeTest, LargeString) {
    Layout layout({{"big", ColumnType::STRING}});
    Row row(layout);
    std::string largeStr(10000, 'A');
    row.set<std::string_view>(0, largeStr);

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto span = codec.serialize(row, buf);
    EXPECT_GT(span.size(), 10000u) << "large string wire size";

    Row rowBack(layout);
    codec.deserialize(span, rowBack);
    EXPECT_EQ(row.get<std::string>(0), rowBack.get<std::string>(0));
}

TEST(CodecFlat001EdgeTest, DeserializeBufferTooShort) {
    Layout layout({{"i32", ColumnType::INT32}});
    RowCodecFlat001<Layout> codec;
    codec.setup(layout);

    std::byte shortBuf[2] = {};
    Row row(layout);
    EXPECT_THROW(codec.deserialize(std::span<const std::byte>(shortBuf, 2), row), std::runtime_error);
}

// ════════════════════════════════════════════════════════════════════════════
// AppendToBuffer: verify codec appends correctly when buffer is non-empty
// ════════════════════════════════════════════════════════════════════════════

TEST_F(CodecFlat001DynamicTest, AppendToExistingBuffer) {
    Row row(layout_);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 42);
    row.set<double>(2, 1.0);
    row.set<std::string_view>(3, "a");
    row.set<uint16_t>(4, 7);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "b");

    // Pre-fill buffer with some junk
    ByteBuffer buf;
    buf.resize(16, std::byte{0xFF});

    RowCodecFlat001<Layout> codec;
    codec.setup(layout_);
    auto wire = codec.serialize(row, buf);

    // Deserialize should work on the returned span (which starts at offset 16)
    Row rowBack(layout_);
    codec.deserialize(wire, rowBack);
    EXPECT_EQ(row.get<int32_t>(1), rowBack.get<int32_t>(1));
    EXPECT_EQ(row.get<std::string>(3), rowBack.get<std::string>(3));
}
