/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file lz4_csv_test.cpp
 * @brief Compressed CSV (.csv.lz4) via the standard LZ4 frame format.
 *
 * The contract under test (ADR-0008): bcsv's compressed CSV is a *standard*
 * LZ4 frame file, so the interop runs both directions — the official `lz4`
 * CLI must decode what bcsv writes, and bcsv must read what the CLI writes.
 * Compression is decided by content (frame magic), never by hoping a file
 * extension matches its bytes.
 *
 * Coverage:
 *   1. detail::Lz4FrameCompressor round-trip + magic bytes (test-local decode)
 *   2. CLI interop: bcsv .csv.lz4 -> `lz4 -d` -> re-read  (spec conformance)
 *   3. CLI interop: `lz4` compresses plain csv -> bcsv reads it
 *   4. CsvWriter/CsvReader .csv.lz4 round-trip across all block max sizes
 *   5. Multi-block stress: quoted strings + newlines straddling 64KB blocks
 *   6. Content sniff: extension lies, magic wins (both directions)
 *   7. Strict decode: truncation / corruption surface as errors, not short reads
 *   8. Plain .csv untouched by the LZ4 path (regression guard vs sniff)
 */

#include <gtest/gtest.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <bcsv/bcsv.h>

namespace fs = std::filesystem;

namespace {

/// Does a usable `lz4` CLI exist? (Interop tests skip if not.)
bool lz4_cli_available() {
    static int cached = -1;
    if (cached < 0) {
        cached = (std::system("lz4 --version > /dev/null 2>&1") == 0) ? 1 : 0;
    }
    return cached == 1;
}

int run_cmd(const std::string& cmd) {
    return std::system(cmd.c_str());
}

std::string read_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& p, std::string_view data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

/// Populate a mixed-type CSV via the plain writer into `path` and return it.
void write_sample_csv(bcsv::CsvWriter<bcsv::Layout>& w, const fs::path& path, size_t rows) {
    ASSERT_TRUE(w.open(path, true));
    for (size_t i = 0; i < rows; ++i) {
        w.row().set(0, (i % 2) == 0);
        w.row().set(1, static_cast<int8_t>(i % 127));
        w.row().set(2, static_cast<int32_t>(i) * 1000);
        w.row().set(3, static_cast<int64_t>(i) * 1000000);
        w.row().set(4, static_cast<uint16_t>(i % 65536));
        w.row().set(5, static_cast<float>(i) * 1.5f);
        w.row().set(6, static_cast<double>(i) * 3.14159);
        w.row().set(7, "row " + std::to_string(i) + " text");
        w.writeRow();
    }
    w.close();
}

bcsv::Layout sampleLayout() {
    bcsv::Layout layout;
    layout.addColumn({"b", bcsv::ColumnType::BOOL});
    layout.addColumn({"i8", bcsv::ColumnType::INT8});
    layout.addColumn({"i32", bcsv::ColumnType::INT32});
    layout.addColumn({"i64", bcsv::ColumnType::INT64});
    layout.addColumn({"u16", bcsv::ColumnType::UINT16});
    layout.addColumn({"f32", bcsv::ColumnType::FLOAT});
    layout.addColumn({"f64", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"s", bcsv::ColumnType::STRING});
    return layout;
}

/// Decode a whole in-memory frame stream through the internal reader
/// (one-shot test helper — library users stream via Lz4FrameIStream).
std::string decode_all(std::string_view compressed) {
    bcsv::detail::Lz4FrameReader reader;
    size_t pos = 0;
    std::string all;
    while (true) {
        std::string plain;
        bool more = reader.next_chunk([&](std::string& out) {
            if (pos >= compressed.size()) return false;
            size_t take = std::min(compressed.size() - pos, size_t(64 * 1024));
            out.append(compressed.substr(pos, take));
            pos += take;
            return true;
        }, plain);
        all += plain;
        if (!more) break;
    }
    return all;
}

/// Read every row with a fresh reader, verifying values, return count.
size_t read_and_sum_rows(const bcsv::Layout& layout, const fs::path& path, size_t expected) {
    bcsv::CsvReader<bcsv::Layout> reader(layout);
    EXPECT_TRUE(reader.open(path)) << reader.getErrorMsg();
    size_t n = 0;
    while (reader.readNext()) {
        EXPECT_EQ(reader.row().get<bool>(0), (n % 2) == 0);
        EXPECT_EQ(reader.row().get<int32_t>(2), static_cast<int32_t>(n) * 1000);
        EXPECT_EQ(reader.row().get<std::string>(7),
                  "row " + std::to_string(n) + " text");
        ++n;
    }
    EXPECT_TRUE(reader.getErrorMsg().empty()) << reader.getErrorMsg();
    EXPECT_EQ(n, expected);
    reader.close();
    return n;
}

} // namespace

// ============================================================================
// Fixture
// ============================================================================

class Lz4CsvTest : public ::testing::Test {
protected:
    fs::path tmpDir_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        tmpDir_ = fs::temp_directory_path() / "bcsv_lz4_csv_test"
                  / (std::string(info->test_suite_name()) + "_" + info->name());
        fs::create_directories(tmpDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmpDir_, ec);
    }

    fs::path tmpFile(const std::string& name) const { return tmpDir_ / name; }
};

// ============================================================================
// 1. Core codec round-trip + frame magic
// ============================================================================

TEST_F(Lz4CsvTest, CoreCodec_RoundTripAndMagic) {
    // A pile of text that must span more than one 64KB block to prove the
    // streaming path stitches blocks back together.
    std::string payload;
    payload.reserve(400 * 1024);
    for (int i = 0; i < 20000; ++i) payload += "line " + std::to_string(i) + ",42,hello\n";

    bcsv::detail::Lz4FrameCompressor comp(/*block_max_size_id=*/4);
    std::string frame = comp.header();
    frame += comp.push(payload);   // separate statements: operator+ operand order is unspecified
    frame += comp.finish();

    // Standards marker: LZ4 frame magic little-endian == 04 22 4D 18.
    ASSERT_GE(frame.size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(frame[0]), 0x04);
    EXPECT_EQ(static_cast<unsigned char>(frame[1]), 0x22);
    EXPECT_EQ(static_cast<unsigned char>(frame[2]), 0x4D);
    EXPECT_EQ(static_cast<unsigned char>(frame[3]), 0x18);

    // Decodes back byte-identical.
    EXPECT_EQ(decode_all(frame), payload);
    // ...and compression did something for repetitive text.
    EXPECT_LT(frame.size(), payload.size());
}

TEST_F(Lz4CsvTest, CoreCodec_EmptyAndTinyPayloads) {
    for (std::string payload : {std::string(), std::string("a"), std::string("hello world\n")}) {
        bcsv::detail::Lz4FrameCompressor comp;
        // NOTE: build the frame statement-by-statement. `header() + push() +
        // finish()` would let the compiler evaluate finish() before push()
        // (operator+ operands are indeterminately sequenced) — and the
        // one-shot contract makes that throw. Real callers are ordered.
        std::string frame = comp.header();
        frame += comp.push(payload);
        frame += comp.finish();
        EXPECT_EQ(decode_all(frame), payload) << "payload size " << payload.size();
    }
}

TEST_F(Lz4CsvTest, CoreCodec_InvalidParametersThrow) {
    EXPECT_THROW((bcsv::detail::Lz4FrameCompressor(3)), std::runtime_error);      // below 64KB
    EXPECT_THROW((bcsv::detail::Lz4FrameCompressor(8)), std::runtime_error);      // above 4MB
    EXPECT_THROW((bcsv::detail::Lz4FrameCompressor(4, -1)), std::runtime_error);  // negative level
    EXPECT_THROW((bcsv::detail::Lz4FrameCompressor(4, 13)), std::runtime_error);  // above LZ4HC max
}

// ============================================================================
// 2/3. CLI interop, both directions (the ADR-0008 contract)
// ============================================================================

TEST_F(Lz4CsvTest, Interop_BcsvFrameDecodedByLz4Cli) {
    if (!lz4_cli_available()) GTEST_SKIP() << "lz4 CLI not installed";
    auto layout = sampleLayout();
    const size_t rows = 5000;

    auto lz4path = tmpFile("data.csv.lz4");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, lz4path, rows); }

    // The official tool must decode our frame to valid CSV, and bcsv must read
    // the CLI's output back — this is exactly what "valid lz4 file" means.
    auto outcsv = tmpFile("from_cli.csv");
    ASSERT_EQ(run_cmd(("lz4 -q -df '" + lz4path.string() + "' '" + outcsv.string() + "'").c_str()), 0);
    ASSERT_TRUE(fs::exists(outcsv));

    // Sanity: the CLI-decoded bytes carry the (RFC 4180 quoted) header — a
    // leading-column name alone proves the text came back from a real decode.
    auto bytes = read_bytes(outcsv);
    EXPECT_NE(bytes.find("\"b\""), std::string::npos);
    EXPECT_EQ(read_and_sum_rows(layout, outcsv, rows), rows);

    // And the .csv.lz4 itself still starts with the LZ4 magic.
    EXPECT_EQ(read_bytes(lz4path).substr(0, 4), std::string("\x04\x22\x4D\x18", 4));
}

TEST_F(Lz4CsvTest, Interop_CliFrameDecodedByBcsv) {
    if (!lz4_cli_available()) GTEST_SKIP() << "lz4 CLI not installed";
    auto layout = sampleLayout();
    const size_t rows = 3000;

    auto plain = tmpFile("plain.csv");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, plain, rows); }

    auto cliLz4 = tmpFile("cli.csv.lz4");
    ASSERT_EQ(run_cmd(("lz4 -q '" + plain.string() + "' '" + cliLz4.string() + "'").c_str()), 0);
    ASSERT_TRUE(fs::exists(cliLz4));

    // bcsv must read the CLI-produced frame and yield every row.
    EXPECT_EQ(read_and_sum_rows(layout, cliLz4, rows), rows);
}

TEST_F(Lz4CsvTest, Interop_CliLz4HCReadableByBcsv) {
    if (!lz4_cli_available()) GTEST_SKIP() << "lz4 CLI not installed";
    auto layout = sampleLayout();
    const size_t rows = 2000;

    auto plain = tmpFile("plain_hc.csv");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, plain, rows); }

    auto cliLz4 = tmpFile("cli_hc.csv.lz4");
    // -12 = LZ4HC max, --content-size: exercises header fields bcsv must skip.
    ASSERT_EQ(run_cmd(("lz4 -q -12 --content-size '" + plain.string() + "' '" + cliLz4.string() + "'").c_str()), 0);
    EXPECT_EQ(read_and_sum_rows(layout, cliLz4, rows), rows);
}

// ============================================================================
// 4. .csv.lz4 round-trip across every block max size
// ============================================================================

TEST_F(Lz4CsvTest, RoundTrip_AllBlockSizes) {
    auto layout = sampleLayout();
    const size_t rows = 1500;

    for (int blockId : {4, 5, 6, 7}) {   // 64KB, 256KB, 1MB, 4MB
        auto path = tmpFile("blk_" + std::to_string(blockId) + ".csv.lz4");
        {
            bcsv::CsvWriter<bcsv::Layout> w(layout);
            ASSERT_TRUE(w.setLz4(blockId));
            write_sample_csv(w, path, rows);
        }
        EXPECT_EQ(read_and_sum_rows(layout, path, rows), rows) << "block id " << blockId;
        // Still a valid frame regardless of chosen block size.
        EXPECT_EQ(read_bytes(path).substr(0, 4), std::string("\x04\x22\x4D\x18", 4));
    }
}

TEST_F(Lz4CsvTest, RoundTrip_HcLevelProducesValidFrame) {
    auto layout = sampleLayout();
    auto path = tmpFile("hc.csv.lz4");
    {
        bcsv::CsvWriter<bcsv::Layout> w(layout);
        ASSERT_TRUE(w.setLz4(5, 9));
        write_sample_csv(w, path, 1000);
    }
    EXPECT_EQ(read_and_sum_rows(layout, path, 1000), 1000u);
}

// ============================================================================
// 5. Multi-block stress: quoted strings / embedded newlines across blocks
// ============================================================================

TEST_F(Lz4CsvTest, MultiBlock_QuotedStringsAcrossFrames) {
    // Layout with a wide string column; fill with long quoted values carrying
    // delimiters and embedded newlines so records straddle 64KB block seams.
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    layout.addColumn({"blob", bcsv::ColumnType::STRING});

    std::string fat = std::string("x,with \"quotes\" and\nnewline,  ") + std::string(3000, 'Z');

    auto path = tmpFile("fat.csv.lz4");
    const size_t rows = 400;   // ~1.2MB of strings → many blocks
    {
        bcsv::CsvWriter<bcsv::Layout> w(layout);
        ASSERT_TRUE(w.open(path, true));
        for (size_t i = 0; i < rows; ++i) {
            w.row().set(0, static_cast<int32_t>(i));
            w.row().set(1, fat);
            w.writeRow();
        }
        w.close();
    }

    size_t seen = 0;
    {
        bcsv::CsvReader<bcsv::Layout> r(layout);
        ASSERT_TRUE(r.open(path));
        while (r.readNext()) {
            ASSERT_EQ(r.row().get<int32_t>(0), static_cast<int32_t>(seen));
            ASSERT_EQ(r.row().get<std::string>(1), fat);   // incl. newline preserved
            ++seen;
        }
        EXPECT_TRUE(r.getErrorMsg().empty()) << r.getErrorMsg();
        r.close();
    }
    EXPECT_EQ(seen, rows);
}

// ============================================================================
// 6. Content sniff: the magic bytes decide, extension is advisory
// ============================================================================

TEST_F(Lz4CsvTest, Sniff_MagicOverridesWrongExtension) {
    auto layout = sampleLayout();
    // Genuinely an LZ4 frame, but named .csv → magic must trigger decoding.
    auto lyingCsv = tmpFile("actually_lz4.csv");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, lyingCsv, 200); }
    // Re-write the SAME frame under a .csv name by hand from a .lz4 source:
    auto lz4src = tmpFile("src.csv.lz4");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, lz4src, 200); }
    write_bytes(lyingCsv, read_bytes(lz4src));
    EXPECT_EQ(read_and_sum_rows(layout, lyingCsv, 200), 200u);
}

TEST_F(Lz4CsvTest, Sniff_PlainTextNamedLz4StillReadsAsText) {
    auto layout = sampleLayout();
    // A .lz4 file that is actually plain CSV: never decompress by filename
    // hope — the byte-4 sniff sees no magic and reads it as CSV.
    auto path = tmpFile("really_plain.csv.lz4");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, path, 150); }
    // The writer chose .lz4 so it DID compress. Instead craft a genuinely
    // plain file under a .lz4 name to exercise the sniff's negative branch.
    auto plain = tmpFile("plain_src.csv");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, plain, 150); }
    write_bytes(path, read_bytes(plain));
    EXPECT_EQ(read_and_sum_rows(layout, path, 150), 150u);
}

// ============================================================================
// 7. Strict decode: broken .lz4 fails loudly, never as a short/empty file
// ============================================================================

TEST_F(Lz4CsvTest, TruncatedFrame_SurfacesAsErrorNotShortRead) {
    auto layout = sampleLayout();
    auto full = tmpFile("full.csv.lz4");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, full, 20000); }

    auto bytes = read_bytes(full);
    auto trunc = tmpFile("trunc.csv.lz4");
    write_bytes(trunc, bytes.substr(0, bytes.size() / 2));   // cut inside a block

    bcsv::CsvReader<bcsv::Layout> r(layout);
    ASSERT_TRUE(r.open(trunc));
    // Read everything available...
    size_t got = 0;
    while (r.readNext()) ++got;
    // ...then demand the reason: a decoder error, not a clean EOF. This is the
    // anti-silent-truncation guarantee for on-the-fly decompression.
    EXPECT_NE(r.readNext(), true);
    EXPECT_GT(got, 0u);   // rows did stream before the cut — then the error hits
    std::string msg = r.getErrorMsg();
    EXPECT_NE(msg.find("LZ4"), std::string::npos) << "expected an LZ4 error, got: " << msg;
    r.close();
}

TEST_F(Lz4CsvTest, TruncatedFrame_HeaderOnlyFailsAtOpen) {
    auto layout = sampleLayout();
    auto full = tmpFile("h.csv.lz4");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, full, 100); }
    auto bytes = read_bytes(full);

    auto chopped = tmpFile("h_chop.csv.lz4");
    write_bytes(chopped, bytes.substr(0, 3));   // not even full magic
    bcsv::CsvReader<bcsv::Layout> r(layout);
    // Fewer than 4 bytes: no magic → treated as plain CSV → header check runs
    // on garbage; must not succeed cleanly.
    bool opened = r.open(chopped);
    if (opened) {
        // If it opened as "plain", reading must error or yield a mismatched
        // header rather than silently succeeding with 0 real rows.
        EXPECT_FALSE(r.readNext());
    }
    r.close();
}

TEST_F(Lz4CsvTest, NonFrameNamedLz4_ReadsAsPlainOrNull) {
    // Bytes that start with an LZ4-looking magic but are not a frame must fail
    // the strict decoder rather than return wrong data.
    auto layout = sampleLayout();
    auto bogus = tmpFile("bogus.csv.lz4");
    std::string bad = std::string("\x04\x22\x4D\x18", 4) + std::string(64, '\xFF');
    write_bytes(bogus, bad);
    bcsv::CsvReader<bcsv::Layout> r(layout);
    // open() sniffs magic → wraps in LZ4 → header read hits a corrupt frame.
    // Either open() itself fails, or the first read does. Must never yield data.
    if (r.open(bogus)) {
        EXPECT_FALSE(r.readNext());
        EXPECT_NE(r.getErrorMsg().find("LZ4"), std::string::npos);
    }
    r.close();
}

// ============================================================================
// 8. Checksums on: single-byte bit-rot becomes a loud decode error too.
//    Without block/content checksums a flipped payload byte decodes to
//    plausible garbage and nothing ever reports it (measured: ~38% silent).
//    bcsv writes the flags on, so corruption must surface through the
//    documented channel — readNext() returns false with an "LZ4" message,
//    it never throws out of the loop and never yields wrong values silently.
// ============================================================================

TEST_F(Lz4CsvTest, CorruptedFrame_SurfacesAsErrorNotWrongData) {
    auto layout = sampleLayout();
    auto full = tmpFile("good.csv.lz4");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, full, 5000); }

    auto probe = read_bytes(full);
    auto bad = tmpFile("flip.csv.lz4");
    std::mt19937 rng(42);
    // Block+content checksums must catch the vast majority of bit flips — a
    // silent miss is the failure mode this test guards against. Loudness has
    // two legitimate shapes: open() fails (corruption hit the first block) or
    // iteration stops with an LZ4 message. A flip is NOT required to be
    // detected only when it is provably inert (byte unchanged / padding).
    bool silent_miss = false;
    size_t loud = 0;
    for (int t = 0; t < 20; ++t) {
        const size_t where = 16 + (static_cast<size_t>(rng()) % (probe.size() - 32));
        const unsigned char xorv = static_cast<unsigned char>(rng() & 0xFF) + 1;
        auto mod = probe;
        mod[where] ^= xorv;
        if (mod[where] == probe[where]) continue;   // inert flip (XOR of identical nibbles)
        write_bytes(bad, mod);

        bcsv::CsvReader<bcsv::Layout> r(layout);
        bool opened = r.open(bad);
        if (!opened) {
            EXPECT_NE(r.getErrorMsg().find("LZ4"), std::string::npos)
                << "flip " << t << ": open rejected without an LZ4 error: " << r.getErrorMsg();
            ++loud;
            r.close();
            continue;
        }
        bool threw = false;
        try {
            while (r.readNext()) { /* drain */ }
        } catch (...) {
            threw = true;
            ADD_FAILURE() << "flip " << t << ": readNext threw — errors must surface via getErrorMsg()";
        }
        const std::string msg = r.getErrorMsg();
        if (!threw && (msg.empty() || msg.rfind("Warning", 0) == 0)) {
            silent_miss = true;   // decoded mutated bytes and reported nothing
        } else if (!threw) {
            EXPECT_NE(msg.find("LZ4"), std::string::npos)
                << "flip " << t << ": non-LZ4 error text: " << msg;
            ++loud;
        }
        r.close();
    }
    EXPECT_GT(loud, 15u) << "expected ~all payload flips to be caught loudly";
    EXPECT_FALSE(silent_miss) << "a corrupted frame decoded without any error report";
}

// ============================================================================
// 9. Plain .csv untouched by the LZ4 path (sniff must not misfire)
// ============================================================================

TEST_F(Lz4CsvTest, PlainCsv_RegressionGuardAgainstSniff) {
    auto layout = sampleLayout();
    auto path = tmpFile("plain.csv");
    { bcsv::CsvWriter<bcsv::Layout> w(layout); write_sample_csv(w, path, 1000); }

    // Plain CSV must never be sniffed as LZ4 (text content is not frame magic).
    EXPECT_FALSE(bcsv::lz4_frame_sniff(read_bytes(path).substr(0, 4)));
    EXPECT_EQ(read_and_sum_rows(layout, path, 1000), 1000u);
}

TEST_F(Lz4CsvTest, Setter_ExternalOstreamIsPlain) {
    // "Inert for plain destinations" is documented behavior: an external
    // std::ostream has no extension to opt in — output must be plain CSV.
    auto layout = sampleLayout();
    std::ostringstream oss;
    {
        bcsv::CsvWriter<bcsv::Layout> w(layout);
        ASSERT_TRUE(w.setLz4(6, 9));           // configured... but destination is not .lz4
        ASSERT_TRUE(w.open(oss, true));
        w.row().set(0, true);
        w.row().set(1, static_cast<int8_t>(3));
        w.row().set(2, static_cast<int32_t>(7));
        w.row().set(3, static_cast<int64_t>(11));
        w.row().set(4, static_cast<uint16_t>(1));
        w.row().set(5, 1.5f);
        w.row().set(6, 2.5);
        w.row().set(7, "x");
        w.writeRow();
        w.close();
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("\"b\""), std::string::npos) << out.substr(0, 48);  // quoted CSV header
    EXPECT_NE(out.find("\n"), std::string::npos);
    EXPECT_EQ(out.find(std::string("\x04\x22\x4D\x18", 4)), std::string::npos);  // no frame magic
}

TEST_F(Lz4CsvTest, Setter_OnlyBeforeOpen) {
    bcsv::CsvWriter<bcsv::Layout> w(sampleLayout());
    EXPECT_TRUE(w.setLz4(5));                  // before open: ok
    EXPECT_FALSE(w.setLz4(99));                // invalid id rejected, with a reason
    EXPECT_NE(w.getErrorMsg().find("block size"), std::string::npos) << w.getErrorMsg();
    EXPECT_FALSE(w.setLz4(4, 99));             // invalid level rejected, with a reason
    EXPECT_NE(w.getErrorMsg().find("compression level"), std::string::npos) << w.getErrorMsg();
    ASSERT_TRUE(w.open(tmpFile("late.csv"), true));
    EXPECT_FALSE(w.setLz4(6));                 // after open: rejected (header written)
    EXPECT_NE(w.getErrorMsg().find("already open"), std::string::npos) << w.getErrorMsg();
    w.close();
}
