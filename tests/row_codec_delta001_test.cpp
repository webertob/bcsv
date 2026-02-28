/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include <gtest/gtest.h>
#include "bcsv/bcsv.h"
#include "bcsv/row_codec_delta001.h"

#include <cmath>
#include <limits>

using namespace bcsv;

// ────────────────────────────────────────────────────────────────────────────
// Basic round-trip tests
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, FirstRowRoundTrip) {
    Layout layout({
        {"b", ColumnType::BOOL},
        {"i32", ColumnType::INT32},
        {"f64", ColumnType::DOUBLE},
        {"s", ColumnType::STRING}
    });

    Row row(layout);
    row.set<bool>(0, true);
    row.set<int32_t>(1, 42);
    row.set<double>(2, 3.14);
    row.set<std::string_view>(3, "hello");

    RowCodecDelta001<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf;
    auto wire = enc.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    // Deserialize with separate codec instance
    RowCodecDelta001<Layout> dec;
    dec.setup(layout);

    Row out(layout);
    dec.deserialize(wire, out);

    EXPECT_TRUE(out.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), 42);
    EXPECT_DOUBLE_EQ(out.get<double>(2), 3.14);
    EXPECT_EQ(out.get<std::string>(3), "hello");
}

TEST(CodecDelta001Test, UnchangedRowEmitsHeader) {
    // Delta codec always emits at least the header (no empty-span shortcut)
    // to keep gradient state synchronised.
    Layout layout({
        {"i32", ColumnType::INT32},
    });

    Row row(layout);
    row.set<int32_t>(0, 100);

    RowCodecDelta001<Layout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire1 = codec.serialize(row, buf);
    EXPECT_FALSE(wire1.empty());  // First row

    auto wire2 = codec.serialize(row, buf);
    EXPECT_FALSE(wire2.empty());  // Same row — still emits header (delta codec)
}

// ────────────────────────────────────────────────────────────────────────────
// Delta encoding: small changes → fewer bytes
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, DeltaEncoding_SmallChange) {
    Layout layout({
        {"val", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Row 0: val=1000
    row.set<int32_t>(0, 1000);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<int32_t>(0), 1000);

    // Row 1: val=1001 (delta=1, should use 1 byte instead of 4)
    row.set<int32_t>(0, 1001);
    auto w1 = enc.serialize(row, buf);
    EXPECT_FALSE(w1.empty());
    // Header + 1 byte delta should be smaller than header + 4 bytes plain
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 1001);
}

TEST(CodecDelta001Test, DeltaEncoding_Negative) {
    Layout layout({
        {"val", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    // Decrease by 1 — zigzag delta = 1 (1 byte)
    row.set<int32_t>(0, 99);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 99);
}

TEST(CodecDelta001Test, DeltaEncoding_Float_XOR) {
    Layout layout({
        {"f", ColumnType::FLOAT},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<float>(0, 1.0f);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f);

    // Very similar float value
    row.set<float>(0, 1.0f + 1e-6f);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f + 1e-6f);
}

TEST(CodecDelta001Test, DeltaEncoding_Double) {
    Layout layout({
        {"d", ColumnType::DOUBLE},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<double>(0, 100.0);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 100.0);

    row.set<double>(0, 100.5);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 100.5);
}

// ────────────────────────────────────────────────────────────────────────────
// ZoH (zero-order hold) — unchanged columns
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, ZoH_UnchangedColumn) {
    Layout layout({
        {"a", ColumnType::INT32},
        {"b", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Row 0
    row.set<int32_t>(0, 100);
    row.set<int32_t>(1, 200);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    // Row 1: only column 'a' changes
    row.set<int32_t>(0, 101);
    // b stays 200
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 101);
    EXPECT_EQ(out.get<int32_t>(1), 200);  // ZoH preserved
}

// ────────────────────────────────────────────────────────────────────────────
// FoC (first-order constant) prediction
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, FoC_LinearIntegerSequence) {
    Layout layout({
        {"val", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Row 0: val=100 (plain)
    row.set<int32_t>(0, 100);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    // Row 1: val=110 (delta=10, gradient established = 10)
    row.set<int32_t>(0, 110);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 110);

    // Row 2: val=120 (predicted=110+10=120, FoC match!)
    row.set<int32_t>(0, 120);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 120);

    // Row 3: val=130 (predicted=120+10=130, FoC match!)
    row.set<int32_t>(0, 130);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 130);
}

TEST(CodecDelta001Test, FoC_LinearDoubleSequence) {
    Layout layout({
        {"val", ColumnType::DOUBLE},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<double>(0, 0.0);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<double>(0, 0.5);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // Row 2: predicted = 0.5 + 0.5 = 1.0 → FoC
    row.set<double>(0, 1.0);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 1.0);

    // Row 3: predicted = 1.0 + 0.5 = 1.5 → FoC
    row.set<double>(0, 1.5);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 1.5);
}

TEST(CodecDelta001Test, FoC_NoMatchFallsBackToDelta) {
    Layout layout({
        {"val", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<int32_t>(0, 110);  // gradient=10
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // Row 2: val=125 (predicted=120, actual=125 → FoC fails, delta=15)
    row.set<int32_t>(0, 125);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 125);
}

// ────────────────────────────────────────────────────────────────────────────
// All types round-trip
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, AllTypes_MultiRow) {
    Layout layout({
        {"b",  ColumnType::BOOL},
        {"u8", ColumnType::UINT8},
        {"u16",ColumnType::UINT16},
        {"u32",ColumnType::UINT32},
        {"u64",ColumnType::UINT64},
        {"i8", ColumnType::INT8},
        {"i16",ColumnType::INT16},
        {"i32",ColumnType::INT32},
        {"i64",ColumnType::INT64},
        {"f",  ColumnType::FLOAT},
        {"d",  ColumnType::DOUBLE},
        {"s",  ColumnType::STRING},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    auto setRow = [&](bool b, uint8_t u8, uint16_t u16, uint32_t u32, uint64_t u64,
                      int8_t i8, int16_t i16, int32_t i32, int64_t i64,
                      float f, double d, std::string_view s) {
        row.set<bool>(0, b);
        row.set<uint8_t>(1, u8);
        row.set<uint16_t>(2, u16);
        row.set<uint32_t>(3, u32);
        row.set<uint64_t>(4, u64);
        row.set<int8_t>(5, i8);
        row.set<int16_t>(6, i16);
        row.set<int32_t>(7, i32);
        row.set<int64_t>(8, i64);
        row.set<float>(9, f);
        row.set<double>(10, d);
        row.set<std::string_view>(11, s);
    };

    auto checkRow = [&](bool b, uint8_t u8, uint16_t u16, uint32_t u32, uint64_t u64,
                        int8_t i8, int16_t i16, int32_t i32, int64_t i64,
                        float f, double d, const std::string& s) {
        EXPECT_EQ(out.get<bool>(0), b);
        EXPECT_EQ(out.get<uint8_t>(1), u8);
        EXPECT_EQ(out.get<uint16_t>(2), u16);
        EXPECT_EQ(out.get<uint32_t>(3), u32);
        EXPECT_EQ(out.get<uint64_t>(4), u64);
        EXPECT_EQ(out.get<int8_t>(5), i8);
        EXPECT_EQ(out.get<int16_t>(6), i16);
        EXPECT_EQ(out.get<int32_t>(7), i32);
        EXPECT_EQ(out.get<int64_t>(8), i64);
        EXPECT_FLOAT_EQ(out.get<float>(9), f);
        EXPECT_DOUBLE_EQ(out.get<double>(10), d);
        EXPECT_EQ(out.get<std::string>(11), s);
    };

    // Row 0
    setRow(true, 10, 1000, 100000, 1000000000ULL,
           -5, -500, -50000, -5000000000LL,
           1.5f, 2.5, "first");
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    checkRow(true, 10, 1000, 100000, 1000000000ULL,
             -5, -500, -50000, -5000000000LL,
             1.5f, 2.5, "first");

    // Row 1: small changes
    setRow(false, 11, 1001, 100001, 1000000001ULL,
           -4, -499, -49999, -4999999999LL,
           1.6f, 2.6, "second");
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    checkRow(false, 11, 1001, 100001, 1000000001ULL,
             -4, -499, -49999, -4999999999LL,
             1.6f, 2.6, "second");

    // Row 2: same delta (linear), triggers FoC for integer columns
    setRow(false, 12, 1002, 100002, 1000000002ULL,
           -3, -498, -49998, -4999999998LL,
           1.7f, 2.7, "second");  // string unchanged
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    checkRow(false, 12, 1002, 100002, 1000000002ULL,
             -3, -498, -49998, -4999999998LL,
             1.7f, 2.7, "second");
}

// ────────────────────────────────────────────────────────────────────────────
// String handling
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, StringChanged) {
    Layout layout({
        {"s", ColumnType::STRING},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<std::string_view>(0, "hello");
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<std::string>(0), "hello");

    row.set<std::string_view>(0, "world");
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<std::string>(0), "world");

    // Unchanged string
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<std::string>(0), "world");
}

// ────────────────────────────────────────────────────────────────────────────
// Dispatch integration
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, DispatchIntegration) {
    Layout layout({
        {"i32", ColumnType::INT32},
        {"f64", ColumnType::DOUBLE},
    });

    RowCodecDispatch<Layout> enc, dec;
    enc.setup(RowCodecId::DELTA001, layout);
    dec.setup(RowCodecId::DELTA001, layout);

    EXPECT_TRUE(enc.isDelta());

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 42);
    row.set<double>(1, 3.14);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<int32_t>(0), 42);
    EXPECT_DOUBLE_EQ(out.get<double>(1), 3.14);

    row.set<int32_t>(0, 43);
    row.set<double>(1, 3.15);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 43);
    EXPECT_DOUBLE_EQ(out.get<double>(1), 3.15);
}

TEST(CodecDelta001Test, SelectCodecWithDeltaFlag) {
    Layout layout({
        {"i32", ColumnType::INT32},
    });

    RowCodecDispatch<Layout> dispatch;
    dispatch.selectCodec(FileFlags::DELTA_ENCODING, layout);
    EXPECT_TRUE(dispatch.isDelta());
    EXPECT_EQ(dispatch.codecId(), RowCodecId::DELTA001);
}

TEST(CodecDelta001Test, SelectCodecPriority) {
    Layout layout({
        {"i32", ColumnType::INT32},
    });

    RowCodecDispatch<Layout> dispatch;

    // DELTA_ENCODING takes priority over ZERO_ORDER_HOLD
    dispatch.selectCodec(FileFlags::DELTA_ENCODING | FileFlags::ZERO_ORDER_HOLD, layout);
    EXPECT_TRUE(dispatch.isDelta());
}

// ────────────────────────────────────────────────────────────────────────────
// Codec reset
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, Reset_RestartsEncoding) {
    Layout layout({
        {"val", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Encode a few rows
    row.set<int32_t>(0, 100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    row.set<int32_t>(0, 110);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // Reset both sides (simulates new packet)
    enc.reset();
    dec.reset();

    // After reset, first row should be plain again
    row.set<int32_t>(0, 200);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 200);

    // Second row after reset should use delta
    row.set<int32_t>(0, 201);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 201);
}

// ────────────────────────────────────────────────────────────────────────────
// Multi-row stress
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, StressTest_1000Rows) {
    Layout layout({
        {"ts",  ColumnType::UINT64},
        {"val", ColumnType::DOUBLE},
        {"flag",ColumnType::BOOL},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    for (int i = 0; i < 1000; ++i) {
        row.set<uint64_t>(0, static_cast<uint64_t>(1000000 + i));
        row.set<double>(1, 100.0 + i * 0.1);
        row.set<bool>(2, (i % 2) == 0);

        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);

        EXPECT_EQ(out.get<uint64_t>(0), static_cast<uint64_t>(1000000 + i));
        EXPECT_DOUBLE_EQ(out.get<double>(1), 100.0 + i * 0.1);
        EXPECT_EQ(out.get<bool>(2), (i % 2) == 0);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Edge cases
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, BoolOnlyLayout) {
    Layout layout({
        {"a", ColumnType::BOOL},
        {"b", ColumnType::BOOL},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<bool>(0, true);
    row.set<bool>(1, false);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_TRUE(out.get<bool>(0));
    EXPECT_FALSE(out.get<bool>(1));

    row.set<bool>(0, false);
    row.set<bool>(1, true);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_FALSE(out.get<bool>(0));
    EXPECT_TRUE(out.get<bool>(1));
}

TEST(CodecDelta001Test, StringOnlyLayout) {
    Layout layout({
        {"s1", ColumnType::STRING},
        {"s2", ColumnType::STRING},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<std::string_view>(0, "aaa");
    row.set<std::string_view>(1, "bbb");
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<std::string>(0), "aaa");
    EXPECT_EQ(out.get<std::string>(1), "bbb");

    // Only s2 changes
    row.set<std::string_view>(1, "ccc");
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<std::string>(0), "aaa");
    EXPECT_EQ(out.get<std::string>(1), "ccc");
}

TEST(CodecDelta001Test, UnsignedTypes) {
    Layout layout({
        {"u8",  ColumnType::UINT8},
        {"u16", ColumnType::UINT16},
        {"u32", ColumnType::UINT32},
        {"u64", ColumnType::UINT64},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<uint8_t>(0, 255);
    row.set<uint16_t>(1, 65535);
    row.set<uint32_t>(2, 0xFFFFFFFF);
    row.set<uint64_t>(3, 0xFFFFFFFFFFFFFFFFULL);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<uint8_t>(0), 255);
    EXPECT_EQ(out.get<uint16_t>(1), 65535);
    EXPECT_EQ(out.get<uint32_t>(2), 0xFFFFFFFF);
    EXPECT_EQ(out.get<uint64_t>(3), 0xFFFFFFFFFFFFFFFFULL);

    // Wrap to 0
    row.set<uint8_t>(0, 0);
    row.set<uint16_t>(1, 0);
    row.set<uint32_t>(2, 0);
    row.set<uint64_t>(3, 0);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<uint8_t>(0), 0);
    EXPECT_EQ(out.get<uint16_t>(1), 0);
    EXPECT_EQ(out.get<uint32_t>(2), 0u);
    EXPECT_EQ(out.get<uint64_t>(3), 0u);
}

TEST(CodecDelta001Test, GradientSyncAfterZoH) {
    // Regression test: ensure gradient is properly zeroed after ZoH repeat,
    // and FoC prediction doesn't use stale gradient.
    Layout layout({
        {"val", ColumnType::INT32},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Row 0: 100
    row.set<int32_t>(0, 100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    // Row 1: 200 (gradient=100)
    row.set<int32_t>(0, 200);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // Row 2: 300 (FoC: predicted=200+100=300 → match)
    row.set<int32_t>(0, 300);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    // Row 3: 300 (ZoH, gradient → 0)
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    // Row 4: 300 (ZoH again, gradient still 0)
    auto w4 = enc.serialize(row, buf);
    dec.deserialize(w4, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    // Row 5: 300 (ZoH, gradient=0, predicted=300+0=300 → FoC match)
    auto w5 = enc.serialize(row, buf);
    dec.deserialize(w5, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    // Row 6: 305 (delta from 300, NOT FoC because gradient was 0)
    row.set<int32_t>(0, 305);
    auto w6 = enc.serialize(row, buf);
    dec.deserialize(w6, out);
    EXPECT_EQ(out.get<int32_t>(0), 305);
}

TEST(CodecDelta001Test, EmptyLayout) {
    std::vector<ColumnDefinition> noCols;
    Layout layout(noCols);

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Empty layout should produce zero-length wire data
    auto w0 = enc.serialize(row, buf);
    EXPECT_TRUE(w0.empty());
    // Deserialize of empty span should be harmless
    dec.deserialize(w0, out);

    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 3 additions: edge cases, wire-size assertions, signed overflow, NaN/Inf
// ────────────────────────────────────────────────────────────────────────────

TEST(CodecDelta001Test, SignedOverflow_INT8_Wrap) {
    // INT8 wrapping from -128 to 127 (max positive delta)
    Layout layout({{"val", ColumnType::INT8}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int8_t>(0, -128);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<int8_t>(0), -128);

    row.set<int8_t>(0, 127);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int8_t>(0), 127);

    // And back
    row.set<int8_t>(0, -128);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int8_t>(0), -128);
}

TEST(CodecDelta001Test, SignedOverflow_INT32_MinMax) {
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, std::numeric_limits<int32_t>::min());
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);
    EXPECT_EQ(out.get<int32_t>(0), std::numeric_limits<int32_t>::min());

    row.set<int32_t>(0, std::numeric_limits<int32_t>::max());
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), std::numeric_limits<int32_t>::max());

    // Max back to min (full range delta)
    row.set<int32_t>(0, std::numeric_limits<int32_t>::min());
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), std::numeric_limits<int32_t>::min());
}

TEST(CodecDelta001Test, SignedOverflow_INT64_MinMax) {
    Layout layout({{"val", ColumnType::INT64}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int64_t>(0, std::numeric_limits<int64_t>::min());
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);
    EXPECT_EQ(out.get<int64_t>(0), std::numeric_limits<int64_t>::min());

    row.set<int64_t>(0, std::numeric_limits<int64_t>::max());
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int64_t>(0), std::numeric_limits<int64_t>::max());
}

TEST(CodecDelta001Test, Float_NaN_Inf_RoundTrip) {
    Layout layout({
        {"f", ColumnType::FLOAT},
        {"d", ColumnType::DOUBLE},
    });
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // NaN
    row.set<float>(0, std::numeric_limits<float>::quiet_NaN());
    row.set<double>(1, std::numeric_limits<double>::quiet_NaN());
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_TRUE(std::isnan(out.get<float>(0)));
    EXPECT_TRUE(std::isnan(out.get<double>(1)));

    // +Inf
    row.set<float>(0, std::numeric_limits<float>::infinity());
    row.set<double>(1, std::numeric_limits<double>::infinity());
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<float>(0), std::numeric_limits<float>::infinity());
    EXPECT_EQ(out.get<double>(1), std::numeric_limits<double>::infinity());

    // -Inf
    row.set<float>(0, -std::numeric_limits<float>::infinity());
    row.set<double>(1, -std::numeric_limits<double>::infinity());
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<float>(0), -std::numeric_limits<float>::infinity());
    EXPECT_EQ(out.get<double>(1), -std::numeric_limits<double>::infinity());

    // NaN → normal (XOR delta should handle this)
    row.set<float>(0, 1.0f);
    row.set<double>(1, 2.0);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f);
    EXPECT_DOUBLE_EQ(out.get<double>(1), 2.0);
}

TEST(CodecDelta001Test, WireSize_DeltaSmallerThanPlain) {
    // For INT32 column with small delta, wire size should be smaller than plain row
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta001<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf;
    Row row(layout);

    // Row 0: plain (header + 4 bytes data)
    row.set<int32_t>(0, 1000000);
    auto w0 = enc.serialize(row, buf);
    size_t plainSize = w0.size();

    // Row 1: delta=1 (header + 1 byte delta instead of 4 bytes)
    row.set<int32_t>(0, 1000001);
    auto w1 = enc.serialize(row, buf);
    size_t deltaSize = w1.size();

    EXPECT_LT(deltaSize, plainSize) << "Delta row should be smaller than plain row";
}

TEST(CodecDelta001Test, WireSize_ZoH_HeaderOnly) {
    // Unchanged row should only contain the header, no data payload
    Layout layout({{"val", ColumnType::INT64}});
    RowCodecDelta001<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf;
    Row row(layout);

    row.set<int64_t>(0, 42);
    auto w0 = enc.serialize(row, buf);

    // Same value → ZoH mode
    auto w1 = enc.serialize(row, buf);
    size_t headBytes = 1;  // 2 mode bits + 3 length bits = 5 bits → 1 byte header
    EXPECT_EQ(w1.size(), headBytes) << "ZoH row should be header-only";
}

TEST(CodecDelta001Test, FoC_Float_NoAccumulatedError) {
    // Verify that FoC prediction uses exact bitwise comparison,
    // preventing floating-point error accumulation over many rows.
    Layout layout({{"val", ColumnType::DOUBLE}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Start with values that have exact FP representation
    row.set<double>(0, 0.0);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<double>(0, 0.125);  // 1/8, exact in binary
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // 50 rows of FoC: 0.250, 0.375, 0.500, ...
    for (int i = 2; i < 52; ++i) {
        row.set<double>(0, i * 0.125);
        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);
        EXPECT_DOUBLE_EQ(out.get<double>(0), i * 0.125)
            << "FoC failed at row " << i;
    }
}

TEST(CodecDelta001Test, ManyColumns_WideLayout) {
    // Stress test with 50 columns of different types
    std::vector<ColumnDefinition> cols;
    for (int i = 0; i < 10; ++i) cols.push_back({"u32_" + std::to_string(i), ColumnType::UINT32});
    for (int i = 0; i < 10; ++i) cols.push_back({"i64_" + std::to_string(i), ColumnType::INT64});
    for (int i = 0; i < 10; ++i) cols.push_back({"f64_" + std::to_string(i), ColumnType::DOUBLE});
    for (int i = 0; i < 10; ++i) cols.push_back({"f32_" + std::to_string(i), ColumnType::FLOAT});
    for (int i = 0; i < 5; ++i)  cols.push_back({"b_" + std::to_string(i), ColumnType::BOOL});
    for (int i = 0; i < 5; ++i)  cols.push_back({"s_" + std::to_string(i), ColumnType::STRING});

    Layout layout(cols);
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Row 0: set all columns
    for (int i = 0; i < 10; ++i) row.set<uint32_t>(i, static_cast<uint32_t>(i * 100));
    for (int i = 0; i < 10; ++i) row.set<int64_t>(10 + i, static_cast<int64_t>(i * -1000));
    for (int i = 0; i < 10; ++i) row.set<double>(20 + i, i * 1.5);
    for (int i = 0; i < 10; ++i) row.set<float>(30 + i, static_cast<float>(i * 0.5));
    for (int i = 0; i < 5; ++i)  row.set<bool>(40 + i, (i % 2) == 0);
    for (int i = 0; i < 5; ++i)  row.set<std::string_view>(45 + i, "init");

    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    // Verify row 0
    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<uint32_t>(i), static_cast<uint32_t>(i * 100));
    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<int64_t>(10 + i), static_cast<int64_t>(i * -1000));

    // Row 1: small changes to trigger delta encoding
    for (int i = 0; i < 10; ++i) row.set<uint32_t>(i, static_cast<uint32_t>(i * 100 + 1));
    for (int i = 0; i < 10; ++i) row.set<int64_t>(10 + i, static_cast<int64_t>(i * -1000 - 1));
    for (int i = 0; i < 10; ++i) row.set<double>(20 + i, i * 1.5 + 0.001);
    // Leave floats, bools, strings unchanged → ZoH

    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<uint32_t>(i), static_cast<uint32_t>(i * 100 + 1));
    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<int64_t>(10 + i), static_cast<int64_t>(i * -1000 - 1));
    for (int i = 0; i < 10; ++i) EXPECT_FLOAT_EQ(out.get<float>(30 + i), static_cast<float>(i * 0.5));  // unchanged
    for (int i = 0; i < 5; ++i)  EXPECT_EQ(out.get<std::string>(45 + i), "init");  // unchanged
}

TEST(CodecDelta001Test, DeltaEncoding_UINT64_LargeDelta) {
    // Verify that large deltas fall back to plain encoding
    Layout layout({{"val", ColumnType::UINT64}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<uint64_t>(0, 0);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    // Large delta: value requires ~8 bytes delta, so plain should be used
    row.set<uint64_t>(0, 0xFFFFFFFFFFFFFFFFULL);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<uint64_t>(0), 0xFFFFFFFFFFFFFFFFULL);
}

TEST(CodecDelta001Test, FoC_SignedInteger_NegativeGradient) {
    // FoC with negative gradient: 100, 90, 80, 70, ...
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<int32_t>(0, 90);  // gradient = -10
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 90);

    // Row 2: predicted = 90 + (-10) = 80 → FoC
    row.set<int32_t>(0, 80);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 80);

    // Row 3: predicted = 80 + (-10) = 70 → FoC
    row.set<int32_t>(0, 70);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 70);
}

TEST(CodecDelta001Test, MultiPacketReset_GradientState) {
    // Simulate packet boundary: encode rows, reset, encode more
    // Ensures gradient doesn't leak across packets
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Packet 1: ramp up 100, 110, 120, 130
    for (int v = 100; v <= 130; v += 10) {
        row.set<int32_t>(0, v);
        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);
        EXPECT_EQ(out.get<int32_t>(0), v);
    }

    // Reset (new packet)
    enc.reset();
    dec.reset();

    // Packet 2: completely different sequence 500, 510, 520
    for (int v = 500; v <= 520; v += 10) {
        row.set<int32_t>(0, v);
        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);
        EXPECT_EQ(out.get<int32_t>(0), v);
    }
}

TEST(CodecDelta001Test, EmptyString_RoundTrip) {
    Layout layout({{"s", ColumnType::STRING}});
    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Empty string
    row.set<std::string_view>(0, "");
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<std::string>(0), "");

    // Non-empty
    row.set<std::string_view>(0, "hello");
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<std::string>(0), "hello");

    // Back to empty
    row.set<std::string_view>(0, "");
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<std::string>(0), "");
}

TEST(CodecDelta001Test, AllColumnTypes_FoC_Sequence) {
    // Verify FoC prediction triggers for all numeric types with linear sequences
    Layout layout({
        {"u8",  ColumnType::UINT8},
        {"u16", ColumnType::UINT16},
        {"u32", ColumnType::UINT32},
        {"u64", ColumnType::UINT64},
        {"i8",  ColumnType::INT8},
        {"i16", ColumnType::INT16},
        {"i32", ColumnType::INT32},
        {"i64", ColumnType::INT64},
    });

    RowCodecDelta001<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    for (int r = 0; r < 10; ++r) {
        row.set<uint8_t>(0, static_cast<uint8_t>(10 + r));
        row.set<uint16_t>(1, static_cast<uint16_t>(1000 + r * 5));
        row.set<uint32_t>(2, 100000 + static_cast<uint32_t>(r * 100));
        row.set<uint64_t>(3, 1000000000ULL + r * 1000);
        row.set<int8_t>(4, static_cast<int8_t>(-50 + r));
        row.set<int16_t>(5, static_cast<int16_t>(-5000 + r * 10));
        row.set<int32_t>(6, -100000 + r * 200);
        row.set<int64_t>(7, -1000000000LL + r * 3000);

        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);

        EXPECT_EQ(out.get<uint8_t>(0), static_cast<uint8_t>(10 + r)) << "Row " << r;
        EXPECT_EQ(out.get<uint16_t>(1), static_cast<uint16_t>(1000 + r * 5)) << "Row " << r;
        EXPECT_EQ(out.get<uint32_t>(2), 100000 + static_cast<uint32_t>(r * 100)) << "Row " << r;
        EXPECT_EQ(out.get<uint64_t>(3), 1000000000ULL + r * 1000) << "Row " << r;
        EXPECT_EQ(out.get<int8_t>(4), static_cast<int8_t>(-50 + r)) << "Row " << r;
        EXPECT_EQ(out.get<int16_t>(5), static_cast<int16_t>(-5000 + r * 10)) << "Row " << r;
        EXPECT_EQ(out.get<int32_t>(6), -100000 + r * 200) << "Row " << r;
        EXPECT_EQ(out.get<int64_t>(7), -1000000000LL + r * 3000) << "Row " << r;
    }
}
