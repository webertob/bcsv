/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file row_codec_zoh001_test.cpp
 * @brief Tests for RowCodecZoH001 — verifies correct serialization and
 *        deserialization of ZoH-encoded rows for both dynamic and static layouts.
 *
 * Tests cover:
 *   - Dynamic Layout: serialize, deserialize, roundtrip
 *   - Static Layout: serialize, deserialize, roundtrip
 *   - Edge cases: all-bool, all-string, all-numeric, single column
 *   - ZoH-specific: no-changes (empty span), bool-only changes,
 *     partial column changes, reset lifecycle
 */

#include <gtest/gtest.h>
#include "bcsv/bcsv.h"
#include "bcsv/row_codec_zoh001.h"
#include "bcsv/row_codec_zoh001.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace bcsv;
using TrackedRow = RowImpl<TrackingPolicy::Enabled>;

// ════════════════════════════════════════════════════════════════════════════
// Dynamic Layout
// ════════════════════════════════════════════════════════════════════════════

class CodecZoH001DynamicTest : public ::testing::Test {
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

TEST_F(CodecZoH001DynamicTest, Serialize_AllChanged) {
    TrackedRow row(layout_);
    row.changesSet();  // mark all as changed (like first row in packet)
    row.set<bool>(0, true);
    row.set<int32_t>(1, 42);
    row.set<double>(2, 3.14);
    row.set<std::string_view>(3, "hello");
    row.set<uint16_t>(4, 1000);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "world!");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());

    // Roundtrip verification
    TrackedRow rowRT(layout_);
    codec.deserialize(wire, rowRT);
    EXPECT_EQ(rowRT.get<bool>(0), true);
    EXPECT_EQ(rowRT.get<int32_t>(1), 42);
    EXPECT_DOUBLE_EQ(rowRT.get<double>(2), 3.14);
    EXPECT_EQ(rowRT.get<std::string>(3), "hello");
    EXPECT_EQ(rowRT.get<uint16_t>(4), 1000);
    EXPECT_EQ(rowRT.get<bool>(5), false);
    EXPECT_EQ(rowRT.get<std::string>(6), "world!");
}

TEST_F(CodecZoH001DynamicTest, Serialize_PartialChanges) {
    TrackedRow row(layout_);
    row.changesSet();
    row.set<bool>(0, true);
    row.set<int32_t>(1, 99);
    row.set<double>(2, 2.0);
    row.set<std::string_view>(3, "init");
    row.set<uint16_t>(4, 500);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "orig");

    // Now reset changes and only change some columns
    row.changesReset();
    row.set<int32_t>(1, 200);        // changed
    row.set<bool>(0, false);         // bool — always in header
    row.set<std::string_view>(6, "updated");  // changed

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
}

TEST_F(CodecZoH001DynamicTest, Serialize_NoChanges) {
    TrackedRow row(layout_);
    // Don't set any values — all bits should be zero.
    row.changesReset();

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    EXPECT_TRUE(wire.empty()) << "codec should return empty span when no changes";
}

TEST_F(CodecZoH001DynamicTest, Serialize_BoolOnlyChanges) {
    TrackedRow row(layout_);
    row.changesReset();
    // Only set a bool — since trackingEnabled, bits_[0] = true for the bool value,
    // but bits_[1..6] won't be set (no non-bool changes)
    row.set<bool>(0, true);

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    // Should be header-only (no data payload) — but not empty since bool changed
    EXPECT_FALSE(wire.empty());
}

TEST_F(CodecZoH001DynamicTest, Deserialize_AllChanged) {
    TrackedRow row(layout_);
    row.changesSet();
    row.set<bool>(0, true);
    row.set<int32_t>(1, 12345);
    row.set<double>(2, -1.5);
    row.set<std::string_view>(3, "test");
    row.set<uint16_t>(4, 500);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "xyz");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    TrackedRow rowNew(layout_);
    codec.deserialize(wire, rowNew);

    EXPECT_EQ(rowNew.get<bool>(0), true);
    EXPECT_EQ(rowNew.get<int32_t>(1), 12345);
    EXPECT_DOUBLE_EQ(rowNew.get<double>(2), -1.5);
    EXPECT_EQ(rowNew.get<std::string>(3), "test");
    EXPECT_EQ(rowNew.get<uint16_t>(4), 500);
    EXPECT_EQ(rowNew.get<bool>(5), false);
    EXPECT_EQ(rowNew.get<std::string>(6), "xyz");
}

TEST_F(CodecZoH001DynamicTest, Deserialize_PartialChanges) {
    TrackedRow row(layout_);
    row.changesSet();
    row.set<bool>(0, true);
    row.set<int32_t>(1, 99);
    row.set<double>(2, 2.0);
    row.set<std::string_view>(3, "init");
    row.set<uint16_t>(4, 500);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "orig");

    // Reset and change only some columns
    row.changesReset();
    row.set<int32_t>(1, 200);
    row.set<bool>(0, false);
    row.set<std::string_view>(6, "updated");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    // Initialize "previous" state in destination row
    TrackedRow rowNew(layout_);
    rowNew.set<bool>(0, true);
    rowNew.set<int32_t>(1, 99);
    rowNew.set<double>(2, 2.0);
    rowNew.set<std::string_view>(3, "init");
    rowNew.set<uint16_t>(4, 500);
    rowNew.set<bool>(5, true);
    rowNew.set<std::string_view>(6, "orig");

    codec.deserialize(wire, rowNew);

    // Changed columns should be updated
    EXPECT_EQ(rowNew.get<bool>(0), false);
    EXPECT_EQ(rowNew.get<int32_t>(1), 200);
    EXPECT_EQ(rowNew.get<std::string>(6), "updated");

    // Unchanged columns should retain previous values
    EXPECT_DOUBLE_EQ(rowNew.get<double>(2), 2.0);
    EXPECT_EQ(rowNew.get<std::string>(3), "init");
    EXPECT_EQ(rowNew.get<uint16_t>(4), 500);
    EXPECT_EQ(rowNew.get<bool>(5), true);
}

TEST_F(CodecZoH001DynamicTest, Roundtrip) {
    TrackedRow row(layout_);
    row.changesSet();
    row.set<bool>(0, true);
    row.set<int32_t>(1, -2147483648);
    row.set<double>(2, 1e308);
    row.set<std::string_view>(3, "round trip");
    row.set<uint16_t>(4, 32768);
    row.set<bool>(5, true);
    row.set<std::string_view>(6, "back");

    // Serialize via codec
    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    // Deserialize via codec
    TrackedRow rowBack(layout_);
    codec.deserialize(wire, rowBack);

    EXPECT_EQ(row.get<bool>(0), rowBack.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), rowBack.get<int32_t>(1));
    EXPECT_EQ(row.get<double>(2), rowBack.get<double>(2));
    EXPECT_EQ(row.get<std::string>(3), rowBack.get<std::string>(3));
    EXPECT_EQ(row.get<uint16_t>(4), rowBack.get<uint16_t>(4));
    EXPECT_EQ(row.get<bool>(5), rowBack.get<bool>(5));
    EXPECT_EQ(row.get<std::string>(6), rowBack.get<std::string>(6));
}

// ════════════════════════════════════════════════════════════════════════════
// Dynamic Layout — Edge Cases
// ════════════════════════════════════════════════════════════════════════════
TEST(CodecZoH001EdgeTest, AllBoolLayout) {
    Layout layout({
        {"b1", ColumnType::BOOL}, {"b2", ColumnType::BOOL},
        {"b3", ColumnType::BOOL}, {"b4", ColumnType::BOOL},
        {"b5", ColumnType::BOOL}, {"b6", ColumnType::BOOL},
        {"b7", ColumnType::BOOL}, {"b8", ColumnType::BOOL},
        {"b9", ColumnType::BOOL}
    });

    TrackedRow row(layout);
    row.changesSet();
    for (size_t i = 0; i < 9; ++i)
        row.set<bool>(i, (i % 2) == 0);

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
}

TEST(CodecZoH001EdgeTest, AllStringLayout) {
    Layout layout({
        {"s1", ColumnType::STRING}, {"s2", ColumnType::STRING},
        {"s3", ColumnType::STRING}
    });

    TrackedRow row(layout);
    row.changesSet();
    row.set<std::string_view>(0, "first");
    row.set<std::string_view>(1, "");
    row.set<std::string_view>(2, "third string is long");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());

    // Roundtrip
    TrackedRow rowRT(layout);
    codec.deserialize(wire, rowRT);
    EXPECT_EQ(rowRT.get<std::string>(0), "first");
    EXPECT_EQ(rowRT.get<std::string>(1), "");
    EXPECT_EQ(rowRT.get<std::string>(2), "third string is long");
}

TEST(CodecZoH001EdgeTest, AllNumericTypes) {
    Layout layout({
        {"i8",  ColumnType::INT8},   {"i16", ColumnType::INT16},
        {"i32", ColumnType::INT32},  {"i64", ColumnType::INT64},
        {"u8",  ColumnType::UINT8},  {"u16", ColumnType::UINT16},
        {"u32", ColumnType::UINT32}, {"u64", ColumnType::UINT64},
        {"f32", ColumnType::FLOAT},  {"f64", ColumnType::DOUBLE}
    });

    TrackedRow row(layout);
    row.changesSet();
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

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());

    // Roundtrip verification
    TrackedRow rowRT(layout);
    codec.deserialize(wire, rowRT);
    EXPECT_EQ(rowRT.get<int8_t>(0), -128);
    EXPECT_EQ(rowRT.get<int16_t>(1), -32768);
    EXPECT_EQ(rowRT.get<int32_t>(2), -2147483647);
    EXPECT_EQ(rowRT.get<int64_t>(3), -9223372036854775807LL);
    EXPECT_EQ(rowRT.get<uint8_t>(4), 255);
    EXPECT_EQ(rowRT.get<uint16_t>(5), 65535);
    EXPECT_EQ(rowRT.get<uint32_t>(6), 4294967295u);
    EXPECT_EQ(rowRT.get<uint64_t>(7), 18446744073709551615ull);
    EXPECT_FLOAT_EQ(rowRT.get<float>(8), 3.14f);
    EXPECT_DOUBLE_EQ(rowRT.get<double>(9), 2.718281828);
}

TEST(CodecZoH001EdgeTest, SingleColumn_Int32) {
    Layout layout({{"x", ColumnType::INT32}});
    TrackedRow row(layout);
    row.changesSet();
    row.set<int32_t>(0, 42);

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
}

TEST(CodecZoH001EdgeTest, SingleColumn_Bool) {
    Layout layout({{"flag", ColumnType::BOOL}});
    TrackedRow row(layout);
    row.changesSet();
    row.set<bool>(0, true);

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
}

TEST(CodecZoH001EdgeTest, SingleColumn_String) {
    Layout layout({{"name", ColumnType::STRING}});
    TrackedRow row(layout);
    row.changesSet();
    row.set<std::string_view>(0, "solo");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());

    // Roundtrip
    TrackedRow rowRT(layout);
    codec.deserialize(wire, rowRT);
    EXPECT_EQ(rowRT.get<std::string>(0), "solo");
}

TEST(CodecZoH001EdgeTest, MultipleRowsSequential) {
    Layout layout({
        {"b", ColumnType::BOOL}, {"i32", ColumnType::INT32},
        {"str", ColumnType::STRING}
    });

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);

    for (int v = 0; v < 10; ++v) {
        TrackedRow row(layout);
        row.changesSet();
        row.set<bool>(0, v % 2 == 0);
        row.set<int32_t>(1, v * 100);
        row.set<std::string_view>(2, std::string("row_") + std::to_string(v));

        ByteBuffer buf;
        auto wire = codec.serialize(row, buf);
        EXPECT_FALSE(wire.empty()) << "row " << v;
    }
}

TEST(CodecZoH001EdgeTest, DeserializeBufferTooShort) {
    Layout layout({{"i32", ColumnType::INT32}});
    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);

    // Buffer too small even for the change bitset (1 byte for 1 column)
    TrackedRow row(layout);
    EXPECT_THROW(codec.deserialize(std::span<const std::byte>{}, row), std::runtime_error);
}

TEST(CodecZoH001EdgeTest, LargeString) {
    Layout layout({{"big", ColumnType::STRING}});
    TrackedRow row(layout);
    row.changesSet();
    std::string largeStr(10000, 'A');
    row.set<std::string_view>(0, largeStr);

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());

    // Roundtrip
    TrackedRow rowRT(layout);
    codec.deserialize(wire, rowRT);
    EXPECT_EQ(rowRT.get<std::string>(0), largeStr);
}

// ════════════════════════════════════════════════════════════════════════════
// Static Layout
// ════════════════════════════════════════════════════════════════════════════

using TestStaticLayout = LayoutStatic<bool, int32_t, double, std::string, uint16_t, bool, std::string>;
using TrackedStaticRow = RowStaticImpl<TrackingPolicy::Enabled, bool, int32_t, double, std::string, uint16_t, bool, std::string>;

TEST(CodecZoH001StaticTest, Serialize_AllChanged) {
    TestStaticLayout layout;
    TrackedStaticRow row(layout);
    row.changesSet();
    row.set<0>(true);
    row.set<1>(42);
    row.set<2>(3.14);
    row.set<3>(std::string("hello"));
    row.set<4>(uint16_t{1000});
    row.set<5>(false);
    row.set<6>(std::string("world"));

    RowCodecZoH001<TestStaticLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());

    // Roundtrip
    TrackedStaticRow rowRT(layout);
    codec.deserialize(wire, rowRT);
    EXPECT_EQ(rowRT.get<0>(), true);
    EXPECT_EQ(rowRT.get<1>(), 42);
    EXPECT_DOUBLE_EQ(rowRT.get<2>(), 3.14);
    EXPECT_EQ(rowRT.get<3>(), "hello");
    EXPECT_EQ(rowRT.get<4>(), 1000);
    EXPECT_EQ(rowRT.get<5>(), false);
    EXPECT_EQ(rowRT.get<6>(), "world");
}

TEST(CodecZoH001StaticTest, Serialize_PartialChanges) {
    TestStaticLayout layout;
    TrackedStaticRow row(layout);
    row.changesSet();
    row.set<0>(true);
    row.set<1>(99);
    row.set<2>(2.0);
    row.set<3>(std::string("init"));
    row.set<4>(uint16_t{500});
    row.set<5>(true);
    row.set<6>(std::string("orig"));

    // Reset and change only some columns
    row.changesReset();
    row.set<0>(false);          // bool — always in header
    row.set<1>(200);            // changed
    row.set<6>(std::string("updated"));  // changed

    RowCodecZoH001<TestStaticLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    EXPECT_FALSE(wire.empty());
}

TEST(CodecZoH001StaticTest, Serialize_NoChanges) {
    TestStaticLayout layout;
    TrackedStaticRow row(layout);
    row.changesReset();

    RowCodecZoH001<TestStaticLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    EXPECT_TRUE(wire.empty());
}

TEST(CodecZoH001StaticTest, Deserialize_AllChanged) {
    TestStaticLayout layout;
    TrackedStaticRow row(layout);
    row.changesSet();
    row.set<0>(true);
    row.set<1>(-999);
    row.set<2>(1e-10);
    row.set<3>(std::string("deser"));
    row.set<4>(uint16_t{50000});
    row.set<5>(true);
    row.set<6>(std::string("test"));

    RowCodecZoH001<TestStaticLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    TrackedStaticRow rowNew(layout);
    codec.deserialize(wire, rowNew);

    EXPECT_EQ(rowNew.get<0>(), true);
    EXPECT_EQ(rowNew.get<1>(), -999);
    EXPECT_DOUBLE_EQ(rowNew.get<2>(), 1e-10);
    EXPECT_EQ(rowNew.get<3>(), "deser");
    EXPECT_EQ(rowNew.get<4>(), 50000);
    EXPECT_EQ(rowNew.get<5>(), true);
    EXPECT_EQ(rowNew.get<6>(), "test");
}

TEST(CodecZoH001StaticTest, Roundtrip) {
    TestStaticLayout layout;
    TrackedStaticRow row(layout);
    row.changesSet();
    row.set<0>(false);
    row.set<1>(2147483647);
    row.set<2>(-0.0);
    row.set<3>(std::string("roundtrip"));
    row.set<4>(uint16_t{12345});
    row.set<5>(true);
    row.set<6>(std::string(""));

    RowCodecZoH001<TestStaticLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    TrackedStaticRow rowBack(layout);
    codec.deserialize(wire, rowBack);

    EXPECT_EQ(row.get<0>(), rowBack.get<0>());
    EXPECT_EQ(row.get<1>(), rowBack.get<1>());
    EXPECT_DOUBLE_EQ(row.get<2>(), rowBack.get<2>());
    EXPECT_EQ(row.get<3>(), rowBack.get<3>());
    EXPECT_EQ(row.get<4>(), rowBack.get<4>());
    EXPECT_EQ(row.get<5>(), rowBack.get<5>());
    EXPECT_EQ(row.get<6>(), rowBack.get<6>());
}

TEST(CodecZoH001StaticTest, MultipleRowsSequential) {
    using SLayout = LayoutStatic<bool, int32_t, std::string>;
    SLayout layout;

    RowCodecZoH001<SLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);

    for (int v = 0; v < 10; ++v) {
        RowStaticImpl<TrackingPolicy::Enabled, bool, int32_t, std::string> row(layout);
        row.changesSet();
        row.set<0>(v % 2 == 0);
        row.set<1>(v * 100);
        row.set<2>(std::string("srow_") + std::to_string(v));

        ByteBuffer buf;
        auto wire = codec.serialize(row, buf);
        EXPECT_FALSE(wire.empty()) << "row " << v;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// ZoH-specific multi-row simulation (serialize → reset → partial changes)
// ════════════════════════════════════════════════════════════════════════════

TEST_F(CodecZoH001DynamicTest, MultiRowLifecycle) {
    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout_);

    TrackedRow row(layout_);

    // ── Row 1: first in packet → all changes ──
    row.changesSet();
    row.set<bool>(0, true);
    row.set<int32_t>(1, 100);
    row.set<double>(2, 1.0);
    row.set<std::string_view>(3, "first");
    row.set<uint16_t>(4, 10);
    row.set<bool>(5, false);
    row.set<std::string_view>(6, "a");

    ByteBuffer buf1;
    auto wire1 = codec.serialize(row, buf1);
    EXPECT_FALSE(wire1.empty());

    // ── Row 2: reset + partial changes ──
    row.changesReset();
    row.set<int32_t>(1, 200);
    row.set<bool>(0, false);

    ByteBuffer buf2;
    auto wire2 = codec.serialize(row, buf2);
    EXPECT_FALSE(wire2.empty());

    // ── Row 3: no changes ──
    row.changesReset();

    ByteBuffer buf3;
    auto wire3 = codec.serialize(row, buf3);
    EXPECT_TRUE(wire3.empty());

    // ── Row 4: new packet → all changes again ──
    codec.reset();
    row.changesSet();
    row.set<bool>(0, true);
    row.set<int32_t>(1, 300);

    ByteBuffer buf4;
    auto wire4 = codec.serialize(row, buf4);
    EXPECT_FALSE(wire4.empty());
}
