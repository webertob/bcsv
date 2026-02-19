/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include <gtest/gtest.h>
#include "bcsv/bcsv.h"
#include "bcsv/row_codec_zoh001.h"

using namespace bcsv;

TEST(CodecZoH001DynamicTest, FirstRowAndRoundTrip) {
    Layout layout({
        {"b", ColumnType::BOOL},
        {"i32", ColumnType::INT32},
        {"f64", ColumnType::DOUBLE},
        {"s", ColumnType::STRING}
    });

    RowTracked<TrackingPolicy::Enabled> row(layout);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 42);
    row.set<double>(2, 3.14);
    row.set<std::string_view>(3, "hello");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    codec.reset();

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    RowTracked<TrackingPolicy::Enabled> out(layout);
    codec.deserialize(wire, out);

    EXPECT_TRUE(out.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), 42);
    EXPECT_DOUBLE_EQ(out.get<double>(2), 3.14);
    EXPECT_EQ(out.get<std::string>(3), "hello");
}

TEST(CodecZoH001DynamicTest, NoChangesAfterFirstRowReturnsEmpty) {
    Layout layout({
        {"b", ColumnType::BOOL},
        {"i32", ColumnType::INT32},
        {"s", ColumnType::STRING}
    });

    RowTracked<TrackingPolicy::Enabled> row(layout);
    row.set<bool>(0, false);
    row.set<int32_t>(1, 1);
    row.set<std::string_view>(2, "x");

    RowCodecZoH001<Layout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    codec.reset();

    ByteBuffer buf1;
    auto first = codec.serialize(row, buf1);
    ASSERT_FALSE(first.empty());

    ByteBuffer buf2;
    auto second = codec.serialize(row, buf2);
    EXPECT_TRUE(second.empty());
}

TEST(CodecZoH001DynamicTest, DisabledPolicyStillRoundTrips) {
    Layout layout({
        {"b", ColumnType::BOOL},
        {"i32", ColumnType::INT32},
        {"s", ColumnType::STRING}
    });

    Row row(layout);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 77);
    row.set<std::string_view>(2, "abc");

    RowCodecZoH001<Layout, TrackingPolicy::Disabled> codec;
    codec.setup(layout);
    codec.reset();

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    Row out(layout);
    codec.deserialize(wire, out);
    EXPECT_TRUE(out.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), 77);
    EXPECT_EQ(out.get<std::string>(2), "abc");
}

TEST(CodecZoH001StaticTest, EnabledRoundTrip) {
    using SLayout = LayoutStatic<bool, int32_t, std::string>;
    using SRow = RowStaticTracked<TrackingPolicy::Enabled, bool, int32_t, std::string>;

    SLayout layout;
    SRow row(layout);
    row.set<0>(true);
    row.set<1>(123);
    row.set<2>(std::string("static"));

    RowCodecZoH001<SLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    codec.reset();

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    SRow out(layout);
    codec.deserialize(wire, out);
    EXPECT_TRUE(out.get<0>());
    EXPECT_EQ(out.get<1>(), 123);
    EXPECT_EQ(out.get<2>(), "static");
}

TEST(CodecZoH001StaticTest, NoChangesAfterFirstRowReturnsEmpty) {
    using SLayout = LayoutStatic<bool, int32_t, std::string>;
    using SRow = RowStaticTracked<TrackingPolicy::Enabled, bool, int32_t, std::string>;

    SLayout layout;
    SRow row(layout);
    row.set<0>(false);
    row.set<1>(10);
    row.set<2>(std::string("x"));

    RowCodecZoH001<SLayout, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    codec.reset();

    ByteBuffer buf1;
    auto first = codec.serialize(row, buf1);
    ASSERT_FALSE(first.empty());

    ByteBuffer buf2;
    auto second = codec.serialize(row, buf2);
    EXPECT_TRUE(second.empty());
}
