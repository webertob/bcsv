/*
 * Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file nan_inf_test.cpp
 * @brief NaN / ±Inf / −0.0 / subnormal bit-exactness matrix
 *        (review 2026-07, Cycle C).
 *
 * Guarantee under test: the BCSV binary format round-trips every IEEE-754
 * bit pattern — including NaN payloads and signed zeros — through all three
 * row codecs (flat001, zoh001, delta002) on both dynamic and static layouts,
 * at codec level and through full file I/O.  The CSV bridge preserves
 * values (nan/inf/-inf/-0) but not NaN payloads.
 *
 * Regression focus (pre-Cycle-C bugs, static layouts only):
 *  - ZoH/Delta change detection used IEEE operator== → a -0.0 following a
 *    +0.0 was "unchanged" and decoded as +0.0 (silent sign loss), and
 *    repeated NaNs never held (pure compression loss).
 *  - Delta FoC prediction could rely on platform-defined NaN payload
 *    propagation through arithmetic (now declined by the encoder).
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
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

// ── special-value inventory ─────────────────────────────────────────────────

inline double nanWithPayloadD() {
    return std::bit_cast<double>(UINT64_C(0x7FF8DEADBEEF1234));
}
inline float nanWithPayloadF() {
    return std::bit_cast<float>(UINT32_C(0x7FC0BEEF));
}

// (float, double) value pairs; consecutive repeats exercise ZoH hold and
// the +0.0 / -0.0 transitions exercise the sign-bit regression.
inline std::vector<std::pair<float, double>> specialSequence() {
    const float  fNaN = std::numeric_limits<float>::quiet_NaN();
    const double dNaN = std::numeric_limits<double>::quiet_NaN();
    const float  fInf = std::numeric_limits<float>::infinity();
    const double dInf = std::numeric_limits<double>::infinity();
    return {
        {1.5f, 2.5},
        {+0.0f, +0.0},
        {-0.0f, -0.0},                                        // sign flip after +0.0
        {+0.0f, -0.0},
        {fNaN, dNaN},
        {fNaN, dNaN},                                         // repeat → ZoH hold
        {nanWithPayloadF(), nanWithPayloadD()},               // payload change
        {nanWithPayloadF(), nanWithPayloadD()},               // payload repeat
        {fInf, dInf},
        {-fInf, -dInf},
        {std::numeric_limits<float>::denorm_min(), std::numeric_limits<double>::denorm_min()},
        {-std::numeric_limits<float>::max(), -std::numeric_limits<double>::max()},
        {3.25f, dNaN},                                        // NaN next to finite
        {fNaN, 7.75},
        {1.5f, 2.5},                                          // back to start value
    };
}

inline uint32_t bitsOf(float v)  { return std::bit_cast<uint32_t>(v); }
inline uint64_t bitsOf(double v) { return std::bit_cast<uint64_t>(v); }

// ── codec-level round trip, dynamic layout ──────────────────────────────────

template<template<typename> class Codec>
void runDynamicCodecRoundTrip() {
    bcsv::Layout layout({"f", "d"},
                        {bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE});
    Codec<bcsv::Layout> enc, dec;
    enc.setup(layout);
    enc.reset();
    dec.setup(layout);
    dec.reset();

    bcsv::Row row(layout), out(layout);
    bcsv::ByteBuffer buf;
    auto seq = specialSequence();

    for (size_t i = 0; i < seq.size(); ++i) {
        row.set<float>(0, seq[i].first);
        row.set<double>(1, seq[i].second);
        buf.clear();
        auto wire = enc.serialize(row, buf);
        if (!wire.empty()) {
            dec.deserialize(wire, out);
        }
        // empty span = ZoH full-row repeat → `out` keeps the previous
        // (identical) values, so the comparison below still applies.
        EXPECT_EQ(bitsOf(out.get<float>(0)), bitsOf(seq[i].first))  << "row " << i;
        EXPECT_EQ(bitsOf(out.get<double>(1)), bitsOf(seq[i].second)) << "row " << i;
    }
}

TEST(NanInfDynamicCodec, Flat001RoundTripBitExact)  { runDynamicCodecRoundTrip<bcsv::RowCodecFlat001>(); }
TEST(NanInfDynamicCodec, ZoH001RoundTripBitExact)   { runDynamicCodecRoundTrip<bcsv::RowCodecZoH001>(); }
TEST(NanInfDynamicCodec, Delta002RoundTripBitExact) { runDynamicCodecRoundTrip<bcsv::RowCodecDelta002>(); }

// ── codec-level round trip, static layout ───────────────────────────────────

template<template<typename> class Codec>
void runStaticCodecRoundTrip() {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    Codec<SLayout> enc, dec;
    enc.setup(layout);
    enc.reset();
    dec.setup(layout);
    dec.reset();

    bcsv::RowStatic<float, double> row(layout), out(layout);
    bcsv::ByteBuffer buf;
    auto seq = specialSequence();

    for (size_t i = 0; i < seq.size(); ++i) {
        row.set<0>(seq[i].first);
        row.set<1>(seq[i].second);
        buf.clear();
        auto wire = enc.serialize(row, buf);
        if (!wire.empty()) {
            dec.deserialize(wire, out);
        }
        EXPECT_EQ(bitsOf(out.get<0>()), bitsOf(seq[i].first))  << "row " << i;
        EXPECT_EQ(bitsOf(out.get<1>()), bitsOf(seq[i].second)) << "row " << i;
    }
}

TEST(NanInfStaticCodec, Flat001RoundTripBitExact)  { runStaticCodecRoundTrip<bcsv::RowCodecFlat001>(); }
TEST(NanInfStaticCodec, ZoH001RoundTripBitExact)   { runStaticCodecRoundTrip<bcsv::RowCodecZoH001>(); }
TEST(NanInfStaticCodec, Delta002RoundTripBitExact) { runStaticCodecRoundTrip<bcsv::RowCodecDelta002>(); }

// ── static-layout regressions (pre-Cycle-C bugs) ───────────────────────────

// Repeated identical NaN rows must produce the ZoH empty-span repeat.
// Pre-fix: NaN != NaN forced a full re-serialization of every NaN row.
TEST(NanInfStaticCodec, ZoHRepeatedNaNHolds) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    bcsv::RowCodecZoH001<SLayout> enc;
    enc.setup(layout);
    enc.reset();

    bcsv::RowStatic<float, double> row(layout);
    row.set<0>(std::numeric_limits<float>::quiet_NaN());
    row.set<1>(nanWithPayloadD());

    bcsv::ByteBuffer buf;
    auto first = enc.serialize(row, buf);
    EXPECT_FALSE(first.empty());

    buf.clear();
    auto repeat = enc.serialize(row, buf);
    EXPECT_TRUE(repeat.empty()) << "identical NaN row must hold (ZoH repeat)";
}

// A -0.0 following +0.0 must be flagged as a change and round-trip its sign.
// Pre-fix: operator== treated them as equal → decoder held +0.0.
TEST(NanInfStaticCodec, ZoHNegativeZeroIsAChange) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    bcsv::RowCodecZoH001<SLayout> enc, dec;
    enc.setup(layout);
    enc.reset();
    dec.setup(layout);
    dec.reset();

    bcsv::RowStatic<float, double> row(layout), out(layout);
    bcsv::ByteBuffer buf;

    row.set<0>(+0.0f);
    row.set<1>(+0.0);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    row.set<0>(-0.0f);
    row.set<1>(-0.0);
    buf.clear();
    auto w2 = enc.serialize(row, buf);
    ASSERT_FALSE(w2.empty()) << "-0.0 after +0.0 must be a change, not a hold";
    dec.deserialize(w2, out);

    EXPECT_TRUE(std::signbit(out.get<0>()));
    EXPECT_TRUE(std::signbit(out.get<1>()));
}

TEST(NanInfStaticCodec, DeltaNegativeZeroRoundTrip) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    bcsv::RowCodecDelta002<SLayout> enc, dec;
    enc.setup(layout);
    enc.reset();
    dec.setup(layout);
    dec.reset();

    bcsv::RowStatic<float, double> row(layout), out(layout);
    bcsv::ByteBuffer buf;

    row.set<0>(+0.0f);
    row.set<1>(+0.0);
    auto w1 = enc.serialize(row, buf);
    dec.deserialize(w1, out);

    row.set<0>(-0.0f);
    row.set<1>(-0.0);
    buf.clear();
    auto w2 = enc.serialize(row, buf);
    dec.deserialize(w2, out);

    EXPECT_TRUE(std::signbit(out.get<0>()));
    EXPECT_TRUE(std::signbit(out.get<1>()));
}

// NaN interleaved with finite values through the delta codec (exercises the
// FoC non-finite guard: gradients through NaN are NaN; the encoder must
// never emit an FoC prediction the decoder reconstructs via NaN arithmetic).
TEST(NanInfStaticCodec, DeltaNaNInterleavedBitExact) {
    using SLayout = bcsv::LayoutStatic<double>;
    SLayout layout({"d"});
    bcsv::RowCodecDelta002<SLayout> enc, dec;
    enc.setup(layout);
    enc.reset();
    dec.setup(layout);
    dec.reset();

    const double seq[] = {1.0, nanWithPayloadD(), 5.0, nanWithPayloadD(), 2.0,
                          3.0, 4.0, 5.0,   // linear run → FoC engages on finites
                          nanWithPayloadD(), nanWithPayloadD()};

    bcsv::RowStatic<double> row(layout), out(layout);
    bcsv::ByteBuffer buf;
    for (double v : seq) {
        row.set<0>(v);
        buf.clear();
        auto wire = enc.serialize(row, buf);
        if (!wire.empty()) {
            dec.deserialize(wire, out);
        }
        EXPECT_EQ(bitsOf(out.get<0>()), bitsOf(v));
    }
}

// ── full file round trip (all codecs × both layout kinds) ──────────────────

class NanInfFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() /
               (std::string("bcsv_naninf_") + info->name() + "_" +
                std::to_string(static_cast<unsigned long>(::getpid())));
        fs::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    fs::path dir_;
};

template<typename WriterT>
void writeSpecialFile(const fs::path& path, const bcsv::Layout& layout) {
    WriterT writer(layout);
    ASSERT_TRUE(writer.open(path, true)) << writer.getErrorMsg();
    for (const auto& [f, d] : specialSequence()) {
        writer.row().template set<float>(0, f);
        writer.row().template set<double>(1, d);
        writer.writeRow();
    }
    writer.close();
}

void verifySpecialFile(const fs::path& path) {
    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    auto seq = specialSequence();
    size_t i = 0;
    while (reader.readNext()) {
        ASSERT_LT(i, seq.size());
        EXPECT_EQ(bitsOf(reader.row().get<float>(0)), bitsOf(seq[i].first))  << "row " << i;
        EXPECT_EQ(bitsOf(reader.row().get<double>(1)), bitsOf(seq[i].second)) << "row " << i;
        ++i;
    }
    EXPECT_EQ(i, seq.size());
    reader.close();
}

TEST_F(NanInfFileTest, DynamicFlatFile) {
    bcsv::Layout layout({"f", "d"}, {bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE});
    auto p = dir_ / "flat.bcsv";
    writeSpecialFile<bcsv::WriterFlat<bcsv::Layout>>(p, layout);
    verifySpecialFile(p);
}

TEST_F(NanInfFileTest, DynamicZoHFile) {
    bcsv::Layout layout({"f", "d"}, {bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE});
    auto p = dir_ / "zoh.bcsv";
    writeSpecialFile<bcsv::WriterZoH<bcsv::Layout>>(p, layout);
    verifySpecialFile(p);
}

TEST_F(NanInfFileTest, DynamicDeltaFile) {
    bcsv::Layout layout({"f", "d"}, {bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE});
    auto p = dir_ / "delta.bcsv";
    writeSpecialFile<bcsv::WriterDelta<bcsv::Layout>>(p, layout);
    verifySpecialFile(p);
}

template<typename WriterT>
void writeSpecialFileStatic(const fs::path& path,
                            const bcsv::LayoutStatic<float, double>& layout) {
    WriterT writer(layout);
    ASSERT_TRUE(writer.open(path, true)) << writer.getErrorMsg();
    for (const auto& [f, d] : specialSequence()) {
        writer.row().template set<0>(f);
        writer.row().template set<1>(d);
        writer.writeRow();
    }
    writer.close();
}

void verifySpecialFileStatic(const fs::path& path) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    bcsv::Reader<SLayout> reader;
    ASSERT_TRUE(reader.open(path)) << reader.getErrorMsg();
    auto seq = specialSequence();
    size_t i = 0;
    while (reader.readNext()) {
        ASSERT_LT(i, seq.size());
        EXPECT_EQ(bitsOf(reader.row().get<0>()), bitsOf(seq[i].first))  << "row " << i;
        EXPECT_EQ(bitsOf(reader.row().get<1>()), bitsOf(seq[i].second)) << "row " << i;
        ++i;
    }
    EXPECT_EQ(i, seq.size());
    reader.close();
}

TEST_F(NanInfFileTest, StaticFlatFile) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    auto p = dir_ / "sflat.bcsv";
    writeSpecialFileStatic<bcsv::WriterFlat<SLayout>>(p, layout);
    verifySpecialFileStatic(p);
}

TEST_F(NanInfFileTest, StaticZoHFile) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    auto p = dir_ / "szoh.bcsv";
    writeSpecialFileStatic<bcsv::WriterZoH<SLayout>>(p, layout);
    verifySpecialFileStatic(p);
}

TEST_F(NanInfFileTest, StaticDeltaFile) {
    using SLayout = bcsv::LayoutStatic<float, double>;
    SLayout layout({"f", "d"});
    auto p = dir_ / "sdelta.bcsv";
    writeSpecialFileStatic<bcsv::WriterDelta<SLayout>>(p, layout);
    verifySpecialFileStatic(p);
}

// ── CSV bridge: values (not payloads) round-trip ────────────────────────────

TEST_F(NanInfFileTest, CsvBridgeSpecialValues) {
    bcsv::Layout layout({"f", "d"}, {bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE});
    auto p = dir_ / "special.csv";
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(p, true));
        for (const auto& [f, d] : specialSequence()) {
            writer.row().set<float>(0, f);
            writer.row().set<double>(1, d);
            writer.writeRow();
        }
        writer.close();
    }
    bcsv::CsvReader<bcsv::Layout> reader(layout);
    ASSERT_TRUE(reader.open(p));
    auto seq = specialSequence();
    size_t i = 0;
    while (reader.readNext()) {
        ASSERT_LT(i, seq.size());
        const float  f = reader.row().get<float>(0);
        const double d = reader.row().get<double>(1);
        // CSV preserves the value class and sign, not NaN payload bits.
        EXPECT_EQ(std::isnan(f), std::isnan(seq[i].first))  << "row " << i;
        EXPECT_EQ(std::isnan(d), std::isnan(seq[i].second)) << "row " << i;
        if (!std::isnan(seq[i].first)) {
            EXPECT_EQ(f, seq[i].first) << "row " << i;
            EXPECT_EQ(std::signbit(f), std::signbit(seq[i].first)) << "row " << i;
        }
        if (!std::isnan(seq[i].second)) {
            EXPECT_EQ(d, seq[i].second) << "row " << i;
            EXPECT_EQ(std::signbit(d), std::signbit(seq[i].second)) << "row " << i;
        }
        ++i;
    }
    EXPECT_EQ(i, seq.size());
}

}  // namespace
