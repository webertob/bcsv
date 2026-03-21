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
#include "bcsv/codec_row/row_codec_delta002.h"

#include <cmath>
#include <filesystem>
#include <limits>

using namespace bcsv;

// ════════════════════════════════════════════════════════════════════════════
// Basic round-trip tests
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, FirstRowRoundTrip) {
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

    RowCodecDelta002<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf;
    auto wire = enc.serialize(row, buf);
    ASSERT_FALSE(wire.empty());

    RowCodecDelta002<Layout> dec;
    dec.setup(layout);

    Row out(layout);
    dec.deserialize(wire, out);

    EXPECT_TRUE(out.get<bool>(0));
    EXPECT_EQ(out.get<int32_t>(1), 42);
    EXPECT_DOUBLE_EQ(out.get<double>(2), 3.14);
    EXPECT_EQ(out.get<std::string>(3), "hello");
}

TEST(CodecDelta002Test, UnchangedRowEmitsHeader) {
    Layout layout({{"i32", ColumnType::INT32}});

    Row row(layout);
    row.set<int32_t>(0, 100);

    RowCodecDelta002<Layout> codec;
    codec.setup(layout);

    ByteBuffer buf;
    auto wire1 = codec.serialize(row, buf);
    EXPECT_FALSE(wire1.empty());

    auto wire2 = codec.serialize(row, buf);
    EXPECT_FALSE(wire2.empty());  // still emits header for gradient sync
}

// ════════════════════════════════════════════════════════════════════════════
// Delta encoding: small changes → fewer bytes
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, DeltaEncoding_SmallChange) {
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 1000);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<int32_t>(0), 1000);

    row.set<int32_t>(0, 1001);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 1001);
}

TEST(CodecDelta002Test, DeltaEncoding_Negative) {
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    row.set<int32_t>(0, 99);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 99);
}

TEST(CodecDelta002Test, DeltaEncoding_Float_XOR) {
    Layout layout({{"f", ColumnType::FLOAT}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<float>(0, 1.0f);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f);

    row.set<float>(0, 1.0f + 1e-6f);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f + 1e-6f);
}

TEST(CodecDelta002Test, DeltaEncoding_Double) {
    Layout layout({{"d", ColumnType::DOUBLE}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<double>(0, 100.0);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 100.0);

    row.set<double>(0, 100.5);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 100.5);
}

// ════════════════════════════════════════════════════════════════════════════
// ZoH (zero-order hold) — unchanged columns
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, ZoH_UnchangedColumn) {
    Layout layout({
        {"a", ColumnType::INT32},
        {"b", ColumnType::INT32},
    });

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    row.set<int32_t>(1, 200);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    // Only column 'a' changes
    row.set<int32_t>(0, 101);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 101);
    EXPECT_EQ(out.get<int32_t>(1), 200);  // ZoH preserved
}

// ════════════════════════════════════════════════════════════════════════════
// FoC (first-order constant) prediction
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, FoC_LinearIntegerSequence) {
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<int32_t>(0, 110);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 110);

    // Row 2: predicted=110+10=120 → FoC
    row.set<int32_t>(0, 120);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 120);

    // Row 3: predicted=120+10=130 → FoC
    row.set<int32_t>(0, 130);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 130);
}

TEST(CodecDelta002Test, FoC_LinearDoubleSequence) {
    Layout layout({{"val", ColumnType::DOUBLE}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<double>(0, 0.0);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<double>(0, 0.5);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    row.set<double>(0, 1.0);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 1.0);

    row.set<double>(0, 1.5);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 1.5);
}

TEST(CodecDelta002Test, FoC_NoMatchFallsBackToDelta) {
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<int32_t>(0, 110);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // predicted=120, actual=125 → delta
    row.set<int32_t>(0, 125);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 125);
}

// ════════════════════════════════════════════════════════════════════════════
// All types round-trip
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, AllTypes_MultiRow) {
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

    RowCodecDelta002<Layout> enc, dec;
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

    // Row 2: same delta (FoC for integers)
    setRow(false, 12, 1002, 100002, 1000000002ULL,
           -3, -498, -49998, -4999999998LL,
           1.7f, 2.7, "second");
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    checkRow(false, 12, 1002, 100002, 1000000002ULL,
             -3, -498, -49998, -4999999998LL,
             1.7f, 2.7, "second");
}

// ════════════════════════════════════════════════════════════════════════════
// String handling
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, StringChanged) {
    Layout layout({{"s", ColumnType::STRING}});

    RowCodecDelta002<Layout> enc, dec;
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

    // Unchanged
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<std::string>(0), "world");
}

// ════════════════════════════════════════════════════════════════════════════
// Dispatch integration
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, DispatchIntegration) {
    Layout layout({
        {"i32", ColumnType::INT32},
        {"f64", ColumnType::DOUBLE},
    });

    RowCodecDispatch<Layout> enc, dec;
    enc.setup(RowCodecId::DELTA002, layout);
    dec.setup(RowCodecId::DELTA002, layout);

    EXPECT_TRUE(enc.isDelta());
    EXPECT_EQ(enc.codecId(), RowCodecId::DELTA002);

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

TEST(CodecDelta002Test, SelectCodecWithDeltaFlag) {
    Layout layout({{"i32", ColumnType::INT32}});

    RowCodecDispatch<Layout> dispatch;
    dispatch.selectCodec(version::MINOR, FileFlags::DELTA_ENCODING, layout);
    EXPECT_TRUE(dispatch.isDelta());
    EXPECT_EQ(dispatch.codecId(), RowCodecId::DELTA002);
}

TEST(CodecDelta002Test, SelectCodecPriority_DeltaOverZoH) {
    Layout layout({{"i32", ColumnType::INT32}});

    RowCodecDispatch<Layout> dispatch;
    // DELTA_ENCODING takes priority over ZERO_ORDER_HOLD
    dispatch.selectCodec(version::MINOR, FileFlags::DELTA_ENCODING | FileFlags::ZERO_ORDER_HOLD, layout);
    EXPECT_TRUE(dispatch.isDelta());
    EXPECT_EQ(dispatch.codecId(), RowCodecId::DELTA002);
}

// ════════════════════════════════════════════════════════════════════════════
// Codec reset
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, Reset_RestartsEncoding) {
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    row.set<int32_t>(0, 110);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    enc.reset();
    dec.reset();

    row.set<int32_t>(0, 200);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 200);

    row.set<int32_t>(0, 201);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 201);
}

// ════════════════════════════════════════════════════════════════════════════
// Multi-row stress
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, StressTest_1000Rows) {
    Layout layout({
        {"ts",  ColumnType::UINT64},
        {"val", ColumnType::DOUBLE},
        {"flag",ColumnType::BOOL},
    });

    RowCodecDelta002<Layout> enc, dec;
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

// ════════════════════════════════════════════════════════════════════════════
// Edge cases
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, BoolOnlyLayout) {
    Layout layout({
        {"a", ColumnType::BOOL},
        {"b", ColumnType::BOOL},
    });

    RowCodecDelta002<Layout> enc, dec;
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

TEST(CodecDelta002Test, StringOnlyLayout) {
    Layout layout({
        {"s1", ColumnType::STRING},
        {"s2", ColumnType::STRING},
    });

    RowCodecDelta002<Layout> enc, dec;
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

    row.set<std::string_view>(1, "ccc");
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<std::string>(0), "aaa");
    EXPECT_EQ(out.get<std::string>(1), "ccc");
}

TEST(CodecDelta002Test, UnsignedTypes) {
    Layout layout({
        {"u8",  ColumnType::UINT8},
        {"u16", ColumnType::UINT16},
        {"u32", ColumnType::UINT32},
        {"u64", ColumnType::UINT64},
    });

    RowCodecDelta002<Layout> enc, dec;
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

TEST(CodecDelta002Test, GradientSyncAfterZoH) {
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    row.set<int32_t>(0, 200);  // gradient=100
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    row.set<int32_t>(0, 300);  // FoC: 200+100=300
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    // ZoH → gradient zeroed
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    auto w4 = enc.serialize(row, buf);
    dec.deserialize(w4, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    auto w5 = enc.serialize(row, buf);
    dec.deserialize(w5, out);
    EXPECT_EQ(out.get<int32_t>(0), 300);

    // delta from 300 (gradient was 0 from ZoH)
    row.set<int32_t>(0, 305);
    auto w6 = enc.serialize(row, buf);
    dec.deserialize(w6, out);
    EXPECT_EQ(out.get<int32_t>(0), 305);
}

TEST(CodecDelta002Test, EmptyLayout) {
    std::vector<ColumnDefinition> noCols;
    Layout layout(noCols);

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    auto w0 = enc.serialize(row, buf);
    EXPECT_TRUE(w0.empty());
    dec.deserialize(w0, out);

    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
}

// ════════════════════════════════════════════════════════════════════════════
// Signed overflow, NaN/Inf, wire-size assertions
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, SignedOverflow_INT8_Wrap) {
    Layout layout({{"val", ColumnType::INT8}});
    RowCodecDelta002<Layout> enc, dec;
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

    row.set<int8_t>(0, -128);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int8_t>(0), -128);
}

TEST(CodecDelta002Test, SignedOverflow_INT32_MinMax) {
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, std::numeric_limits<int32_t>::min());
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);
    EXPECT_EQ(out.get<int32_t>(0), std::numeric_limits<int32_t>::min());

    row.set<int32_t>(0, std::numeric_limits<int32_t>::max());
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), std::numeric_limits<int32_t>::max());

    row.set<int32_t>(0, std::numeric_limits<int32_t>::min());
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), std::numeric_limits<int32_t>::min());
}

TEST(CodecDelta002Test, SignedOverflow_INT64_MinMax) {
    Layout layout({{"val", ColumnType::INT64}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int64_t>(0, std::numeric_limits<int64_t>::min());
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);
    EXPECT_EQ(out.get<int64_t>(0), std::numeric_limits<int64_t>::min());

    row.set<int64_t>(0, std::numeric_limits<int64_t>::max());
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int64_t>(0), std::numeric_limits<int64_t>::max());
}

TEST(CodecDelta002Test, Float_NaN_Inf_RoundTrip) {
    Layout layout({
        {"f", ColumnType::FLOAT},
        {"d", ColumnType::DOUBLE},
    });
    RowCodecDelta002<Layout> enc, dec;
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

    // NaN → normal
    row.set<float>(0, 1.0f);
    row.set<double>(1, 2.0);
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f);
    EXPECT_DOUBLE_EQ(out.get<double>(1), 2.0);
}

TEST(CodecDelta002Test, Float_Subnormal_RoundTrip) {
    Layout layout({
        {"f", ColumnType::FLOAT},
        {"d", ColumnType::DOUBLE},
    });
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    // Subnormal (denormalized) values — smallest non-zero magnitudes
    float  sub_f = std::numeric_limits<float>::denorm_min();
    double sub_d = std::numeric_limits<double>::denorm_min();

    row.set<float>(0, sub_f);
    row.set<double>(1, sub_d);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<float>(0), sub_f);
    EXPECT_EQ(out.get<double>(1), sub_d);

    // Subnormal → normal transition
    row.set<float>(0, 1.0f);
    row.set<double>(1, 1.0);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_FLOAT_EQ(out.get<float>(0), 1.0f);
    EXPECT_DOUBLE_EQ(out.get<double>(1), 1.0);

    // Normal → subnormal transition
    row.set<float>(0, sub_f);
    row.set<double>(1, sub_d);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<float>(0), sub_f);
    EXPECT_EQ(out.get<double>(1), sub_d);
}

TEST(CodecDelta002Test, WireSize_DeltaSmallerThanFirstRow) {
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta002<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf;
    Row row(layout);

    // Row 0: delta-from-zero for value 1000000
    row.set<int32_t>(0, 1000000);
    auto w0 = enc.serialize(row, buf);
    size_t firstRowSize = w0.size();

    // Row 1: delta=1 → 1 byte
    row.set<int32_t>(0, 1000001);
    auto w1 = enc.serialize(row, buf);
    size_t deltaSize = w1.size();

    EXPECT_LT(deltaSize, firstRowSize) << "Delta row should be smaller than first row";
}

TEST(CodecDelta002Test, WireSize_ZoH_HeaderOnly) {
    Layout layout({{"val", ColumnType::INT64}});
    RowCodecDelta002<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf;
    Row row(layout);

    row.set<int64_t>(0, 42);
    static_cast<void>(enc.serialize(row, buf));

    // Same value → ZoH mode (code=0)
    auto w1 = enc.serialize(row, buf);
    // INT64 column: 4 header bits → 1 byte header
    size_t headBytes = 1;
    EXPECT_EQ(w1.size(), headBytes) << "ZoH row should be header-only";
}

TEST(CodecDelta002Test, FoC_Float_NoAccumulatedError) {
    Layout layout({{"val", ColumnType::DOUBLE}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<double>(0, 0.0);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<double>(0, 0.125);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    for (int i = 2; i < 52; ++i) {
        row.set<double>(0, i * 0.125);
        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);
        EXPECT_DOUBLE_EQ(out.get<double>(0), i * 0.125)
            << "FoC failed at row " << i;
    }
}

TEST(CodecDelta002Test, ManyColumns_WideLayout) {
    std::vector<ColumnDefinition> cols;
    for (int i = 0; i < 10; ++i) cols.push_back({"u32_" + std::to_string(i), ColumnType::UINT32});
    for (int i = 0; i < 10; ++i) cols.push_back({"i64_" + std::to_string(i), ColumnType::INT64});
    for (int i = 0; i < 10; ++i) cols.push_back({"f64_" + std::to_string(i), ColumnType::DOUBLE});
    for (int i = 0; i < 10; ++i) cols.push_back({"f32_" + std::to_string(i), ColumnType::FLOAT});
    for (int i = 0; i < 5; ++i)  cols.push_back({"b_" + std::to_string(i), ColumnType::BOOL});
    for (int i = 0; i < 5; ++i)  cols.push_back({"s_" + std::to_string(i), ColumnType::STRING});

    Layout layout(cols);
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    for (int i = 0; i < 10; ++i) row.set<uint32_t>(i, static_cast<uint32_t>(i * 100));
    for (int i = 0; i < 10; ++i) row.set<int64_t>(10 + i, static_cast<int64_t>(i * -1000));
    for (int i = 0; i < 10; ++i) row.set<double>(20 + i, i * 1.5);
    for (int i = 0; i < 10; ++i) row.set<float>(30 + i, static_cast<float>(i * 0.5));
    for (int i = 0; i < 5; ++i)  row.set<bool>(40 + i, (i % 2) == 0);
    for (int i = 0; i < 5; ++i)  row.set<std::string_view>(45 + i, "init");

    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<uint32_t>(i), static_cast<uint32_t>(i * 100));
    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<int64_t>(10 + i), static_cast<int64_t>(i * -1000));

    // Row 1: small changes to u32 and i64; leave floats/bools/strings unchanged
    for (int i = 0; i < 10; ++i) row.set<uint32_t>(i, static_cast<uint32_t>(i * 100 + 1));
    for (int i = 0; i < 10; ++i) row.set<int64_t>(10 + i, static_cast<int64_t>(i * -1000 - 1));
    for (int i = 0; i < 10; ++i) row.set<double>(20 + i, i * 1.5 + 0.001);

    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<uint32_t>(i), static_cast<uint32_t>(i * 100 + 1));
    for (int i = 0; i < 10; ++i) EXPECT_EQ(out.get<int64_t>(10 + i), static_cast<int64_t>(i * -1000 - 1));
    for (int i = 0; i < 10; ++i) EXPECT_FLOAT_EQ(out.get<float>(30 + i), static_cast<float>(i * 0.5));
    for (int i = 0; i < 5; ++i)  EXPECT_EQ(out.get<std::string>(45 + i), "init");
}

TEST(CodecDelta002Test, DeltaEncoding_UINT64_LargeDelta) {
    Layout layout({{"val", ColumnType::UINT64}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<uint64_t>(0, 0);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<uint64_t>(0, 0xFFFFFFFFFFFFFFFFULL);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<uint64_t>(0), 0xFFFFFFFFFFFFFFFFULL);
}

TEST(CodecDelta002Test, FoC_SignedInteger_NegativeGradient) {
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    (void)enc.serialize(row, buf);
    dec.deserialize({buf.data(), buf.size()}, out);

    row.set<int32_t>(0, 90);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 90);

    row.set<int32_t>(0, 80);  // FoC: 90+(-10)=80
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<int32_t>(0), 80);

    row.set<int32_t>(0, 70);  // FoC: 80+(-10)=70
    auto w3 = enc.serialize(row, buf);
    dec.deserialize(w3, out);
    EXPECT_EQ(out.get<int32_t>(0), 70);
}

TEST(CodecDelta002Test, MultiPacketReset_GradientState) {
    Layout layout({{"val", ColumnType::INT32}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    for (int v = 100; v <= 130; v += 10) {
        row.set<int32_t>(0, v);
        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);
        EXPECT_EQ(out.get<int32_t>(0), v);
    }

    enc.reset();
    dec.reset();

    for (int v = 500; v <= 520; v += 10) {
        row.set<int32_t>(0, v);
        auto w = enc.serialize(row, buf);
        dec.deserialize(w, out);
        EXPECT_EQ(out.get<int32_t>(0), v);
    }
}

TEST(CodecDelta002Test, EmptyString_RoundTrip) {
    Layout layout({{"s", ColumnType::STRING}});
    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<std::string_view>(0, "");
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<std::string>(0), "");

    row.set<std::string_view>(0, "hello");
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<std::string>(0), "hello");

    row.set<std::string_view>(0, "");
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<std::string>(0), "");
}

TEST(CodecDelta002Test, AllColumnTypes_FoC_Sequence) {
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

    RowCodecDelta002<Layout> enc, dec;
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

// ════════════════════════════════════════════════════════════════════════════
// Delta002-specific tests
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002Test, FirstRow_ZeroValue_ZoH) {
    // When first row value is 0, memcmp against prev(=0) matches → ZoH.
    // Decoder must still produce correct value (0).
    Layout layout({{"val", ColumnType::INT32}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 0);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<int32_t>(0), 0);

    // Then a non-zero value should still work
    row.set<int32_t>(0, 42);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<int32_t>(0), 42);
}

TEST(CodecDelta002Test, FirstRow_ZeroFloat_ZoH) {
    // Float 0.0 on first row should match prev(=0) → ZoH
    Layout layout({{"val", ColumnType::DOUBLE}});

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<double>(0, 0.0);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 0.0);

    row.set<double>(0, 3.14);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_DOUBLE_EQ(out.get<double>(0), 3.14);
}

TEST(CodecDelta002Test, FirstRow_DeltaFromZero) {
    // First row encodes as delta-from-zero (zigzag for ints, XOR for floats).
    // Verify correctness for non-trivial values.
    Layout layout({
        {"i32", ColumnType::INT32},
        {"f32", ColumnType::FLOAT},
    });

    RowCodecDelta002<Layout> enc, dec;
    enc.setup(layout);
    dec.setup(layout);

    ByteBuffer buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, -12345);
    row.set<float>(1, -1.5f);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<int32_t>(0), -12345);
    EXPECT_FLOAT_EQ(out.get<float>(1), -1.5f);
}

TEST(CodecDelta002Test, CopyConstructor) {
    Layout layout({
        {"i32", ColumnType::INT32},
        {"s",   ColumnType::STRING},
    });

    RowCodecDelta002<Layout> enc;
    enc.setup(layout);

    ByteBuffer buf0, buf;
    Row row(layout), out(layout);

    row.set<int32_t>(0, 100);
    row.set<std::string_view>(1, "hello");
    auto w0 = enc.serialize(row, buf0);   // first row in its own buffer

    // Copy encoder after one row — both should produce identical output
    RowCodecDelta002<Layout> enc2(enc);

    row.set<int32_t>(0, 101);
    auto w1_orig = enc.serialize(row, buf);

    ByteBuffer buf2;
    auto w1_copy = enc2.serialize(row, buf2);

    // Wire output must be identical
    ASSERT_EQ(w1_orig.size(), w1_copy.size());
    EXPECT_EQ(std::memcmp(w1_orig.data(), w1_copy.data(), w1_orig.size()), 0);

    // Verify decoding: decoder must see both rows to have correct state
    RowCodecDelta002<Layout> dec;
    dec.setup(layout);
    dec.deserialize(w0, out);      // first row (buf0, not overwritten)
    dec.deserialize(w1_copy, out); // second row (from copied encoder)
    EXPECT_EQ(out.get<int32_t>(0), 101);
    EXPECT_EQ(out.get<std::string>(1), "hello");
}

// ════════════════════════════════════════════════════════════════════════════
// LayoutStatic specialization tests
// ════════════════════════════════════════════════════════════════════════════

TEST(CodecDelta002StaticTest, BasicRoundTrip) {
    using SLayout = LayoutStatic<int32_t, float, std::string>;
    SLayout layout({"counter", "value", "label"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<int32_t, float, std::string> row(layout);
    RowStatic<int32_t, float, std::string> out(layout);
    ByteBuffer buf;

    row.set<0>(42);
    row.set<1>(3.14f);
    row.set<2>(std::string("hello"));

    auto wire = enc.serialize(row, buf);
    ASSERT_GT(wire.size(), 0u);
    dec.deserialize(wire, out);

    EXPECT_EQ(out.get<0>(), 42);
    EXPECT_FLOAT_EQ(out.get<1>(), 3.14f);
    EXPECT_EQ(out.get<2>(), "hello");
}

TEST(CodecDelta002StaticTest, ZoH_UnchangedRow) {
    using SLayout = LayoutStatic<int32_t, double>;
    SLayout layout({"a", "b"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<int32_t, double> row(layout);
    RowStatic<int32_t, double> out(layout);
    ByteBuffer buf;

    row.set<0>(100);
    row.set<1>(2.5);

    // First row
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);
    EXPECT_EQ(out.get<0>(), 100);
    EXPECT_DOUBLE_EQ(out.get<1>(), 2.5);

    // Second row: identical values → ZoH
    buf.resize(0);
    auto w1 = enc.serialize(row, buf);
    ASSERT_GT(w1.size(), 0u);  // always emits header
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<0>(), 100);
    EXPECT_DOUBLE_EQ(out.get<1>(), 2.5);
}

TEST(CodecDelta002StaticTest, FoC_LinearSequence) {
    using SLayout = LayoutStatic<int32_t>;
    SLayout layout({"counter"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<int32_t> row(layout);
    RowStatic<int32_t> out(layout);
    ByteBuffer buf;

    // Write rows 0, 10, 20, 30, ...   (linear ramp → FoC from row 2)
    for (int i = 0; i < 20; ++i) {
        row.set<0>(i * 10);
        buf.resize(0);
        auto wire = enc.serialize(row, buf);
        dec.deserialize(wire, out);
        EXPECT_EQ(out.get<0>(), i * 10) << "Row " << i;
    }
}

TEST(CodecDelta002StaticTest, DeltaEncoding_SmallChange) {
    using SLayout = LayoutStatic<int64_t, float>;
    SLayout layout({"big", "flt"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<int64_t, float> row(layout);
    RowStatic<int64_t, float> out(layout);
    ByteBuffer buf;

    row.set<0>(int64_t(1000000));
    row.set<1>(1.0f);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    // Small change → 1-byte delta
    row.set<0>(int64_t(1000001));
    row.set<1>(1.5f);
    buf.resize(0);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);
    EXPECT_EQ(out.get<0>(), int64_t(1000001));
    EXPECT_FLOAT_EQ(out.get<1>(), 1.5f);
}

TEST(CodecDelta002StaticTest, AllTypes_MultiRow) {
    using SLayout = LayoutStatic<
        bool, uint8_t, uint16_t, uint32_t, uint64_t,
        int8_t, int16_t, int32_t, int64_t,
        float, double, std::string>;
    SLayout layout({"b", "u8", "u16", "u32", "u64",
                    "i8", "i16", "i32", "i64",
                    "f", "d", "s"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<bool, uint8_t, uint16_t, uint32_t, uint64_t,
              int8_t, int16_t, int32_t, int64_t,
              float, double, std::string> row(layout);
    decltype(row) out(layout);
    ByteBuffer buf;

    for (int i = 0; i < 50; ++i) {
        row.set<0>(i % 2 == 0);
        row.set<1>(static_cast<uint8_t>(i));
        row.set<2>(static_cast<uint16_t>(i * 10));
        row.set<3>(static_cast<uint32_t>(i * 100));
        row.set<4>(static_cast<uint64_t>(i * 1000));
        row.set<5>(static_cast<int8_t>(i - 25));
        row.set<6>(static_cast<int16_t>(i * -5));
        row.set<7>(static_cast<int32_t>(i * 42));
        row.set<8>(static_cast<int64_t>(i * -1000));
        row.set<9>(static_cast<float>(i * 0.1f));
        row.set<10>(static_cast<double>(i * 0.01));
        row.set<11>(std::string("row_") + std::to_string(i));

        buf.resize(0);
        auto wire = enc.serialize(row, buf);
        dec.deserialize(wire, out);

        EXPECT_EQ(out.get<0>(), i % 2 == 0) << "Row " << i;
        EXPECT_EQ(out.get<1>(), static_cast<uint8_t>(i)) << "Row " << i;
        EXPECT_EQ(out.get<2>(), static_cast<uint16_t>(i * 10)) << "Row " << i;
        EXPECT_EQ(out.get<3>(), static_cast<uint32_t>(i * 100)) << "Row " << i;
        EXPECT_EQ(out.get<4>(), static_cast<uint64_t>(i * 1000)) << "Row " << i;
        EXPECT_EQ(out.get<5>(), static_cast<int8_t>(i - 25)) << "Row " << i;
        EXPECT_EQ(out.get<6>(), static_cast<int16_t>(i * -5)) << "Row " << i;
        EXPECT_EQ(out.get<7>(), static_cast<int32_t>(i * 42)) << "Row " << i;
        EXPECT_EQ(out.get<8>(), static_cast<int64_t>(i * -1000)) << "Row " << i;
        EXPECT_FLOAT_EQ(out.get<9>(), static_cast<float>(i * 0.1f)) << "Row " << i;
        EXPECT_DOUBLE_EQ(out.get<10>(), static_cast<double>(i * 0.01)) << "Row " << i;
        EXPECT_EQ(out.get<11>(), std::string("row_") + std::to_string(i)) << "Row " << i;
    }
}

TEST(CodecDelta002StaticTest, FileRoundTrip_WriterDelta) {
    using SLayout = LayoutStatic<int32_t, float, std::string>;
    SLayout layout({"counter", "sensor", "tag"});

    const std::string path = "test_delta_static_roundtrip.bcsv";
    constexpr size_t N = 200;

    // Write
    {
        WriterDelta<SLayout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 0, 64, FileFlags::DELTA_ENCODING));
        for (size_t i = 0; i < N; ++i) {
            writer.row().template set<0>(static_cast<int32_t>(i * 3));
            writer.row().template set<1>(static_cast<float>(i * 0.5f));
            writer.row().template set<2>(i % 10 == 0 ? "changed" : "same");
            writer.writeRow();
        }
        writer.close();
    }

    // Read
    {
        Reader<SLayout> reader;
        ASSERT_TRUE(reader.open(path));
        size_t count = 0;
        while (reader.readNext()) {
            EXPECT_EQ(reader.row().template get<0>(), static_cast<int32_t>(count * 3)) << "Row " << count;
            EXPECT_FLOAT_EQ(reader.row().template get<1>(), static_cast<float>(count * 0.5f)) << "Row " << count;
            EXPECT_EQ(reader.row().template get<2>(), count % 10 == 0 ? "changed" : "same") << "Row " << count;
            ++count;
        }
        EXPECT_EQ(count, N);
        reader.close();
    }

    std::filesystem::remove(path);
}

TEST(CodecDelta002StaticTest, BoolOnlyLayout) {
    using SLayout = LayoutStatic<bool, bool, bool>;
    SLayout layout({"a", "b", "c"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<bool, bool, bool> row(layout);
    RowStatic<bool, bool, bool> out(layout);
    ByteBuffer buf;

    for (int i = 0; i < 10; ++i) {
        row.set<0>(i % 2 == 0);
        row.set<1>(i % 3 == 0);
        row.set<2>(i % 5 == 0);

        buf.resize(0);
        auto wire = enc.serialize(row, buf);
        dec.deserialize(wire, out);

        EXPECT_EQ(out.get<0>(), i % 2 == 0) << "Row " << i;
        EXPECT_EQ(out.get<1>(), i % 3 == 0) << "Row " << i;
        EXPECT_EQ(out.get<2>(), i % 5 == 0) << "Row " << i;
    }
}

TEST(CodecDelta002StaticTest, Reset_RestartsEncoding) {
    using SLayout = LayoutStatic<int32_t>;
    SLayout layout({"x"});

    RowCodecDelta002<SLayout> enc;
    enc.setup(layout);
    RowCodecDelta002<SLayout> dec;
    dec.setup(layout);

    RowStatic<int32_t> row(layout);
    RowStatic<int32_t> out(layout);
    ByteBuffer buf;

    // Write 2 rows to build up state (feed both enc and dec)
    row.set<0>(100);
    auto w0 = enc.serialize(row, buf);
    dec.deserialize(w0, out);

    row.set<0>(110);
    buf.resize(0);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    // Reset both encoder and decoder
    enc.reset();
    dec.reset();

    // After reset, encoding should restart from clean state
    row.set<0>(999);
    buf.resize(0);
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);
    EXPECT_EQ(out.get<0>(), 999);
}
