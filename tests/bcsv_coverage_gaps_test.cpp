/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bcsv_coverage_gaps_test.cpp
 * @brief Cycle 5 — fill verified test coverage gaps
 *
 * Topics:
 *   - Unicode/UTF-8 column names and string values
 *   - Delta002 special floats through full Writer→Reader file I/O
 *   - Golden-file byte-exact wire format test for Flat001
 *   - Bitset 64→65 column I/O boundary (SOO→heap transition)
 *   - Crash truncation — mid-packet file truncation recovery
 *   - Version compatibility through full Writer→Reader file cycle
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstring>
#include <limits>

#include <bcsv/bcsv.h>
#include <bcsv/codec_row/row_codec_flat001.h>
#include <bcsv/codec_row/row_codec_flat001.hpp>
#include <bcsv/codec_row/row_codec_delta002.h>
#include <bcsv/codec_row/row_codec_delta002.hpp>

using namespace bcsv;
namespace fs = std::filesystem;

// ============================================================================
// Fixture
// ============================================================================

class CoverageGapsTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (fs::temp_directory_path() / "bcsv_coverage_gaps_test"
                     / (std::string(info->test_suite_name()) + "_" + info->name())).string();
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }

    std::string test_dir_;
};

// ============================================================================
// 5.3 — Unicode / UTF-8 round-trip tests
// ============================================================================

TEST_F(CoverageGapsTest, UnicodeRoundTrip_ColumnNames) {
    // Column names with CJK, Nordic, emoji, mixed multi-byte UTF-8
    Layout layout;
    layout.addColumn({"温度", ColumnType::FLOAT});       // Chinese: "temperature"
    layout.addColumn({"Ångström", ColumnType::DOUBLE});  // Nordic: Å ring
    layout.addColumn({"📊data", ColumnType::INT32});     // Emoji prefix
    layout.addColumn({"naïve", ColumnType::STRING});     // Diacritics

    auto path = testFile("unicode_colnames.bcsv");

    // Write
    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.row().set(0, 36.6f);
        writer.row().set(1, 1e-10);
        writer.row().set(2, 42);
        writer.row().set(3, std::string("café"));
        writer.writeRow();
        writer.close();
    }

    // Read and verify column names byte-exact
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path));
        const auto& rl = reader.row().layout();
        ASSERT_EQ(rl.columnCount(), 4u);
        EXPECT_EQ(rl.columnName(0), "温度");
        EXPECT_EQ(rl.columnName(1), "Ångström");
        EXPECT_EQ(rl.columnName(2), "📊data");
        EXPECT_EQ(rl.columnName(3), "naïve");

        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), 36.6f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 1e-10);
        EXPECT_EQ(reader.row().get<int32_t>(2), 42);
        EXPECT_EQ(reader.row().get<std::string>(3), "café");
        reader.close();
    }
}

TEST_F(CoverageGapsTest, UnicodeRoundTrip_StringValues) {
    // Multi-byte UTF-8 string values — byte-exact verification
    Layout layout;
    layout.addColumn({"text", ColumnType::STRING});

    auto path = testFile("unicode_strings.bcsv");

    const std::vector<std::string> test_strings = {
        "你好世界",                    // Chinese: "Hello World"
        "مرحبا بالعالم",                // Arabic: "Hello World"
        "🎉🔥💯🚀",                  // Emoji sequence
        "Ñoño señor",                  // Spanish with ñ
        "Ελληνικά κείμενο",            // Greek
        "",                            // Empty string
        "ASCII fallback",              // Pure ASCII
        "Mixed: café 日本語 🌍",       // Mixed scripts
    };

    // Write
    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        for (const auto& s : test_strings) {
            writer.row().set(0, s);
            writer.writeRow();
        }
        writer.close();
    }

    // Read and verify byte-exact
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path));
        for (size_t i = 0; i < test_strings.size(); ++i) {
            ASSERT_TRUE(reader.readNext()) << "Failed to read row " << i;
            std::string actual = reader.row().get<std::string>(0);
            EXPECT_EQ(actual, test_strings[i])
                << "Mismatch at row " << i
                << ": expected " << test_strings[i].size() << " bytes"
                << ", got " << actual.size() << " bytes";
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 5.4 — Delta002 special floats through full file I/O
// ============================================================================

TEST_F(CoverageGapsTest, Delta002SpecialFloats_FullFileIO) {
    // Write NaN, Inf, -Inf, subnormal through full Writer→Reader cycle
    // using the Delta002 codec (the default for delta-encoded files)
    Layout layout;
    layout.addColumn({"f", ColumnType::FLOAT});
    layout.addColumn({"d", ColumnType::DOUBLE});

    auto path = testFile("special_floats_delta.bcsv");

    const float  sub_f =  std::numeric_limits<float>::denorm_min();
    const double sub_d =  std::numeric_limits<double>::denorm_min();
    const float  nan_f =  std::numeric_limits<float>::quiet_NaN();
    const double nan_d =  std::numeric_limits<double>::quiet_NaN();
    const float  inf_f =  std::numeric_limits<float>::infinity();
    const double inf_d =  std::numeric_limits<double>::infinity();

    // Write using Delta codec explicitly
    {
        Writer<Layout, RowCodecDelta002<Layout>> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        // Row 0: normal values
        writer.row().set(0, 1.0f);
        writer.row().set(1, 2.0);
        writer.writeRow();

        // Row 1: NaN
        writer.row().set(0, nan_f);
        writer.row().set(1, nan_d);
        writer.writeRow();

        // Row 2: +Inf
        writer.row().set(0, inf_f);
        writer.row().set(1, inf_d);
        writer.writeRow();

        // Row 3: -Inf
        writer.row().set(0, -inf_f);
        writer.row().set(1, -inf_d);
        writer.writeRow();

        // Row 4: subnormal
        writer.row().set(0, sub_f);
        writer.row().set(1, sub_d);
        writer.writeRow();

        // Row 5: back to normal (tests subnormal→normal delta)
        writer.row().set(0, 42.0f);
        writer.row().set(1, 99.0);
        writer.writeRow();

        writer.close();
    }

    // Read and verify
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(path));

        // Row 0: normal
        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), 1.0f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 2.0);

        // Row 1: NaN
        ASSERT_TRUE(reader.readNext());
        EXPECT_TRUE(std::isnan(reader.row().get<float>(0)));
        EXPECT_TRUE(std::isnan(reader.row().get<double>(1)));

        // Row 2: +Inf
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<float>(0), inf_f);
        EXPECT_EQ(reader.row().get<double>(1), inf_d);

        // Row 3: -Inf
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<float>(0), -inf_f);
        EXPECT_EQ(reader.row().get<double>(1), -inf_d);

        // Row 4: subnormal
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<float>(0), sub_f);
        EXPECT_EQ(reader.row().get<double>(1), sub_d);

        // Row 5: normal again
        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), 42.0f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 99.0);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 5.5 — Golden-file byte-exact wire format test for Flat001
// ============================================================================

TEST_F(CoverageGapsTest, WireFormatGolden_Flat001) {
    // Write a row with known layout {INT32, STRING} using Flat001 codec,
    // then compare the serialized bytes against a hardcoded expected sequence.
    //
    // Wire layout for Flat001: [bits_][data_][strg_lengths][strg_data]
    //   - bits_: 0 bytes (no BOOLs in layout)
    //   - data_: 4 bytes (INT32 = 4 bytes, little-endian)
    //   - strg_lengths: 2 bytes per string column (uint16_t length)
    //   - strg_data: variable (actual string bytes)
    //
    // Layout: {"val": INT32, "tag": STRING}
    // Row:    val=0x01020304 (16909060), tag="Hi"
    //
    // Expected wire bytes:
    //   INT32 data (LE):  04 03 02 01
    //   STRING length:    02 00        (uint16_t LE = 2)
    //   STRING payload:   48 69        ('H' 'i')
    //   Total: 8 bytes

    Layout layout;
    layout.addColumn({"val", ColumnType::INT32});
    layout.addColumn({"tag", ColumnType::STRING});

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);

    // Verify structure
    EXPECT_EQ(codec.rowHeaderSize(), 0u);   // no bools
    EXPECT_EQ(codec.wireDataSize(), 4u);    // INT32
    EXPECT_EQ(codec.wireStrgCount(), 1u);   // one string
    EXPECT_EQ(codec.wireFixedSize(), 6u);   // 0 + 4 + 1*2

    Row row(layout);
    row.set<int32_t>(0, 0x01020304);  // 16909060
    row.set(1, std::string("Hi"));

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    // Expected: [04 03 02 01] [02 00] [48 69]
    const uint8_t expected[] = {
        0x04, 0x03, 0x02, 0x01,  // INT32: 0x01020304 in LE
        0x02, 0x00,              // STRING length: 2 (uint16_t LE)
        0x48, 0x69,              // STRING payload: "Hi"
    };

    ASSERT_EQ(wire.size(), sizeof(expected))
        << "Wire size mismatch: got " << wire.size() << " expected " << sizeof(expected);
    EXPECT_EQ(std::memcmp(wire.data(), expected, sizeof(expected)), 0)
        << "Byte-exact mismatch in Flat001 wire format (format v1.4.0)";
}

TEST_F(CoverageGapsTest, WireFormatGolden_Flat001_WithBool) {
    // Layout: {"flag": BOOL, "val": INT32}
    // Row:    flag=true, val=7
    //
    // Wire layout:
    //   bits_: 1 byte (1 bool, packed into 1 byte, bit 0 set → 0x01)
    //   data_: 4 bytes (INT32 LE: 07 00 00 00)
    //   Total: 5 bytes (no strings)

    Layout layout;
    layout.addColumn({"flag", ColumnType::BOOL});
    layout.addColumn({"val", ColumnType::INT32});

    RowCodecFlat001<Layout> codec;
    codec.setup(layout);

    EXPECT_EQ(codec.rowHeaderSize(), 1u);  // ceil(1 bool / 8)
    EXPECT_EQ(codec.wireDataSize(), 4u);
    EXPECT_EQ(codec.wireStrgCount(), 0u);
    EXPECT_EQ(codec.wireFixedSize(), 5u);  // 1 + 4 + 0

    Row row(layout);
    row.set(0, true);
    row.set<int32_t>(1, 7);

    ByteBuffer buf;
    auto wire = codec.serialize(row, buf);

    const uint8_t expected[] = {
        0x01,                    // BOOL bits: bit 0 = true
        0x07, 0x00, 0x00, 0x00, // INT32: 7 in LE
    };

    ASSERT_EQ(wire.size(), sizeof(expected));
    EXPECT_EQ(std::memcmp(wire.data(), expected, sizeof(expected)), 0)
        << "Byte-exact mismatch in Flat001 wire format with bool (format v1.4.0)";
}

// ============================================================================
// 5.2c — Bitset 64→65 column I/O boundary
// ============================================================================

TEST_F(CoverageGapsTest, BitsetIOBoundary_64To65BoolColumns) {
    // Write and read files with 64 bool columns (SOO inline) and
    // 65 bool columns (heap-allocated), verifying all values survive
    // the full Writer→Reader file cycle.

    for (size_t num_cols : {64u, 65u}) {
        Layout layout;
        for (size_t i = 0; i < num_cols; ++i) {
            layout.addColumn({"b" + std::to_string(i), ColumnType::BOOL});
        }

        auto path = testFile("bitset_" + std::to_string(num_cols) + ".bcsv");

        // Pattern: set bit i if (i % 3 == 0) — covers first, last, and interior
        {
            Writer<Layout> writer(layout);
            ASSERT_TRUE(writer.open(path, true)) << "Failed to open for " << num_cols << " cols";
            for (size_t i = 0; i < num_cols; ++i) {
                writer.row().set(i, (i % 3 == 0));
            }
            writer.writeRow();

            // Second row: all true
            for (size_t i = 0; i < num_cols; ++i) {
                writer.row().set(i, true);
            }
            writer.writeRow();

            // Third row: all false
            for (size_t i = 0; i < num_cols; ++i) {
                writer.row().set(i, false);
            }
            writer.writeRow();
            writer.close();
        }

        // Read and verify
        {
            Reader<Layout> reader;
            ASSERT_TRUE(reader.open(path)) << "Failed to open for reading " << num_cols << " cols";
            ASSERT_EQ(reader.row().layout().columnCount(), num_cols);

            // Row 0: pattern
            ASSERT_TRUE(reader.readNext());
            for (size_t i = 0; i < num_cols; ++i) {
                EXPECT_EQ(reader.row().get<bool>(i), (i % 3 == 0))
                    << "Pattern mismatch at col " << i << " with " << num_cols << " cols";
            }

            // Row 1: all true
            ASSERT_TRUE(reader.readNext());
            for (size_t i = 0; i < num_cols; ++i) {
                EXPECT_TRUE(reader.row().get<bool>(i))
                    << "All-true mismatch at col " << i << " with " << num_cols << " cols";
            }

            // Row 2: all false
            ASSERT_TRUE(reader.readNext());
            for (size_t i = 0; i < num_cols; ++i) {
                EXPECT_FALSE(reader.row().get<bool>(i))
                    << "All-false mismatch at col " << i << " with " << num_cols << " cols";
            }

            EXPECT_FALSE(reader.readNext());
            reader.close();
        }
    }
}

// ============================================================================
// 5.7 — Crash truncation recovery
// ============================================================================

TEST_F(CoverageGapsTest, CrashTruncation_MidPacketTruncation) {
    // Write a file with multiple packets by using a small packet size,
    // then truncate the file mid-way through the last packet.
    // Reader should recover rows from all complete prior packets.

    Layout layout;
    layout.addColumn({"i", ColumnType::INT32});
    layout.addColumn({"s", ColumnType::STRING});

    auto path = testFile("crash_truncate.bcsv");

    // Write enough rows so the uncompressed file is large enough to truncate
    constexpr int TOTAL_ROWS = 500;
    const std::string filler(200, 'X');  // 200-byte string per row

    {
        Writer<Layout> writer(layout);
        // Use uncompressed stream so size is predictable
        ASSERT_TRUE(writer.open(path, true, 0));
        for (int i = 0; i < TOTAL_ROWS; ++i) {
            writer.row().set(0, i);
            writer.row().set(1, filler + std::to_string(i));
            writer.writeRow();
        }
        writer.close();
    }

    // Get the full file size
    const auto full_size = fs::file_size(path);
    ASSERT_GT(full_size, size_t(1024))
        << "File should be at least 1KB";

    // Truncate the file to remove the last ~30% (cuts into the last packet)
    const auto truncated_size = full_size * 7 / 10;
    {
        // Read file, write back truncated
        std::ifstream in(path, std::ios::binary);
        std::string data(truncated_size, '\0');
        in.read(data.data(), static_cast<std::streamsize>(truncated_size));
        in.close();

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(truncated_size));
        out.close();
    }

    EXPECT_EQ(fs::file_size(path), truncated_size);

    // Read the truncated file — should recover some rows without crashing
    {
        Reader<Layout> reader;
        // Reader may or may not open a truncated file depending on header integrity
        if (reader.open(path)) {
            int recovered = 0;
            try {
                while (reader.readNext()) {
                    EXPECT_EQ(reader.row().get<int32_t>(0), recovered)
                        << "Row order mismatch at recovered row " << recovered;
                    ++recovered;
                }
            } catch (const std::exception&) {
                // Truncated data may cause an exception — that's acceptable
            }
            // Should recover some rows but fewer than the total
            EXPECT_GT(recovered, 0) << "Should recover at least some rows";
            EXPECT_LT(recovered, TOTAL_ROWS) << "Should not recover all rows from truncated file";
        }
        // Whether open succeeds or fails, the key assertion is no crash
    }
}

// ============================================================================
// 5.1 — Version compatibility through full Writer→Reader file cycle
// ============================================================================

TEST_F(CoverageGapsTest, VersionCompatibility_FullFileCycle_RejectsNewerMinor) {
    // Write a valid file, binary-patch the minor version byte to current+1,
    // then verify Reader::open() rejects it.

    Layout layout;
    layout.addColumn({"x", ColumnType::INT32});

    auto path = testFile("version_patch.bcsv");

    // Write a valid file
    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.row().set(0, 123);
        writer.writeRow();
        writer.close();
    }

    // Binary-patch the minor version byte (offset 13 in file header)
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(13);
        char patched_minor = static_cast<char>(version::MINOR + 1);
        f.write(&patched_minor, 1);
        f.close();
    }

    // Reader should reject the file
    {
        Reader<Layout> reader;
        bool opened = reader.open(path);
        EXPECT_FALSE(opened) << "Reader should reject file with minor version "
                             << (version::MINOR + 1);
    }
}

TEST_F(CoverageGapsTest, VersionCompatibility_FullFileCycle_RejectsMajor2) {
    // Write a valid file, patch major version to 2, Reader should reject.

    Layout layout;
    layout.addColumn({"x", ColumnType::INT32});

    auto path = testFile("version_major2.bcsv");

    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.row().set(0, 456);
        writer.writeRow();
        writer.close();
    }

    // Patch major version (offset 12) to 2
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(12);
        char patched_major = 2;
        f.write(&patched_major, 1);
        f.close();
    }

    {
        Reader<Layout> reader;
        bool opened = reader.open(path);
        EXPECT_FALSE(opened) << "Reader should reject file with major version 2";
    }
}

TEST_F(CoverageGapsTest, VersionCompatibility_FullFileCycle_AcceptsOlderMinor) {
    // Write a valid file, patch minor version to current-1, Reader should accept.

    if (version::MINOR == 0) {
        GTEST_SKIP() << "Cannot test older minor when current minor is 0";
    }

    Layout layout;
    layout.addColumn({"x", ColumnType::INT32});

    auto path = testFile("version_older_minor.bcsv");

    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.row().set(0, 789);
        writer.writeRow();
        writer.close();
    }

    // Patch minor version (offset 13) to current - 1
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(13);
        char patched_minor = static_cast<char>(version::MINOR - 1);
        f.write(&patched_minor, 1);
        f.close();
    }

    {
        Reader<Layout> reader;
        bool opened = reader.open(path);
        EXPECT_TRUE(opened)
            << "Reader should accept file with older minor version: " << reader.getErrorMsg();
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 789);
        reader.close();
    }
}
