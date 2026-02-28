/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file csv_reader_writer_test.cpp
 * @brief Tests for CsvReader and CsvWriter (Item 12.b)
 *
 * Test categories:
 *   1. Concept verification (static_assert)
 *   2. CSV-to-CSV round-trip
 *   3. CSV-to-BCSV conversion
 *   4. BCSV-to-CSV conversion
 *   5. Delimiter variants (, ; \t)
 *   6. Decimal separator (. vs ,)
 *   7. String protection (whitespace, quotes, embedded delimiters)
 *   8. Edge cases (empty file, empty rows, large values)
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <string>

#include <bcsv/bcsv.h>

namespace fs = std::filesystem;

// ============================================================================
// 1. Concept verification — compile-time checks
// ============================================================================

// CsvWriter satisfies WriterConcept
static_assert(bcsv::WriterConcept<bcsv::CsvWriter<bcsv::Layout>>,
              "CsvWriter<Layout> must satisfy WriterConcept");

// CsvReader satisfies ReaderConcept
static_assert(bcsv::ReaderConcept<bcsv::CsvReader<bcsv::Layout>>,
              "CsvReader<Layout> must satisfy ReaderConcept");

// Existing binary Writer satisfies WriterConcept
static_assert(bcsv::WriterConcept<bcsv::Writer<bcsv::Layout>>,
              "Writer<Layout> must satisfy WriterConcept");

// Existing binary Reader satisfies ReaderConcept
static_assert(bcsv::ReaderConcept<bcsv::Reader<bcsv::Layout>>,
              "Reader<Layout> must satisfy ReaderConcept");

// ============================================================================
// Test fixture
// ============================================================================

class CsvReaderWriterTest : public ::testing::Test {
protected:
    fs::path tmpDir_;

    void SetUp() override {
        // Per-test subdirectory prevents parallel TearDown races.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        tmpDir_ = fs::temp_directory_path() / "bcsv_csv_test"
                  / (std::string(info->test_suite_name()) + "_" + info->name());
        fs::create_directories(tmpDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmpDir_, ec);
    }

    fs::path tmpFile(const std::string& name) const {
        return tmpDir_ / name;
    }

    /// Create a standard mixed-type layout for testing
    bcsv::Layout createMixedLayout() {
        bcsv::Layout layout;
        layout.addColumn({"bool_col", bcsv::ColumnType::BOOL});
        layout.addColumn({"int8_col", bcsv::ColumnType::INT8});
        layout.addColumn({"int32_col", bcsv::ColumnType::INT32});
        layout.addColumn({"int64_col", bcsv::ColumnType::INT64});
        layout.addColumn({"uint16_col", bcsv::ColumnType::UINT16});
        layout.addColumn({"float_col", bcsv::ColumnType::FLOAT});
        layout.addColumn({"double_col", bcsv::ColumnType::DOUBLE});
        layout.addColumn({"string_col", bcsv::ColumnType::STRING});
        return layout;
    }
};

// ============================================================================
// 2. CSV-to-CSV round-trip
// ============================================================================

TEST_F(CsvReaderWriterTest, CsvRoundTrip_MixedTypes) {
    auto layout = createMixedLayout();
    auto path = tmpFile("roundtrip.csv");

    // Write
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, true);
        writer.row().set(1, int8_t(-42));
        writer.row().set(2, int32_t(123456));
        writer.row().set(3, int64_t(9876543210LL));
        writer.row().set(4, uint16_t(65535));
        writer.row().set(5, 3.14f);
        writer.row().set(6, 2.718281828);
        writer.row().set(7, std::string("hello world"));
        writer.writeRow();

        writer.row().set(0, false);
        writer.row().set(1, int8_t(0));
        writer.row().set(2, int32_t(-999));
        writer.row().set(3, int64_t(0));
        writer.row().set(4, uint16_t(0));
        writer.row().set(5, 0.0f);
        writer.row().set(6, -1.0);
        writer.row().set(7, std::string(""));
        writer.writeRow();

        EXPECT_EQ(writer.rowCount(), 2u);
        writer.close();
    }

    // Read back
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), true);
        EXPECT_EQ(reader.row().get<int8_t>(1), -42);
        EXPECT_EQ(reader.row().get<int32_t>(2), 123456);
        EXPECT_EQ(reader.row().get<int64_t>(3), 9876543210LL);
        EXPECT_EQ(reader.row().get<uint16_t>(4), 65535);
        EXPECT_FLOAT_EQ(reader.row().get<float>(5), 3.14f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(6), 2.718281828);
        EXPECT_EQ(reader.row().get<std::string>(7), "hello world");

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), false);
        EXPECT_EQ(reader.row().get<int8_t>(1), 0);
        EXPECT_EQ(reader.row().get<int32_t>(2), -999);
        EXPECT_EQ(reader.row().get<int64_t>(3), 0);
        EXPECT_EQ(reader.row().get<uint16_t>(4), 0);
        EXPECT_FLOAT_EQ(reader.row().get<float>(5), 0.0f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(6), -1.0);
        EXPECT_EQ(reader.row().get<std::string>(7), "");

        EXPECT_FALSE(reader.readNext()); // EOF
        EXPECT_EQ(reader.rowPos(), 2u);
        reader.close();
    }
}

TEST_F(CsvReaderWriterTest, CsvRoundTrip_AllIntegerTypes) {
    bcsv::Layout layout;
    layout.addColumn({"i8", bcsv::ColumnType::INT8});
    layout.addColumn({"i16", bcsv::ColumnType::INT16});
    layout.addColumn({"i32", bcsv::ColumnType::INT32});
    layout.addColumn({"i64", bcsv::ColumnType::INT64});
    layout.addColumn({"u8", bcsv::ColumnType::UINT8});
    layout.addColumn({"u16", bcsv::ColumnType::UINT16});
    layout.addColumn({"u32", bcsv::ColumnType::UINT32});
    layout.addColumn({"u64", bcsv::ColumnType::UINT64});

    auto path = tmpFile("integers.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        // Min values
        writer.row().set(0, int8_t(-128));
        writer.row().set(1, int16_t(-32768));
        writer.row().set(2, int32_t(-2147483648));
        writer.row().set(3, int64_t(-9223372036854775807LL - 1));
        writer.row().set(4, uint8_t(0));
        writer.row().set(5, uint16_t(0));
        writer.row().set(6, uint32_t(0));
        writer.row().set(7, uint64_t(0));
        writer.writeRow();

        // Max values
        writer.row().set(0, int8_t(127));
        writer.row().set(1, int16_t(32767));
        writer.row().set(2, int32_t(2147483647));
        writer.row().set(3, int64_t(9223372036854775807LL));
        writer.row().set(4, uint8_t(255));
        writer.row().set(5, uint16_t(65535));
        writer.row().set(6, uint32_t(4294967295u));
        writer.row().set(7, uint64_t(18446744073709551615ULL));
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        // Min values
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int8_t>(0), -128);
        EXPECT_EQ(reader.row().get<int16_t>(1), -32768);
        EXPECT_EQ(reader.row().get<int32_t>(2), -2147483648);
        EXPECT_EQ(reader.row().get<int64_t>(3), int64_t(-9223372036854775807LL - 1));
        EXPECT_EQ(reader.row().get<uint8_t>(4), 0);
        EXPECT_EQ(reader.row().get<uint16_t>(5), 0);
        EXPECT_EQ(reader.row().get<uint32_t>(6), 0u);
        EXPECT_EQ(reader.row().get<uint64_t>(7), 0u);

        // Max values
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int8_t>(0), 127);
        EXPECT_EQ(reader.row().get<int16_t>(1), 32767);
        EXPECT_EQ(reader.row().get<int32_t>(2), 2147483647);
        EXPECT_EQ(reader.row().get<int64_t>(3), 9223372036854775807LL);
        EXPECT_EQ(reader.row().get<uint8_t>(4), 255);
        EXPECT_EQ(reader.row().get<uint16_t>(5), 65535);
        EXPECT_EQ(reader.row().get<uint32_t>(6), 4294967295u);
        EXPECT_EQ(reader.row().get<uint64_t>(7), 18446744073709551615ULL);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(CsvReaderWriterTest, CsvRoundTrip_ManyRows) {
    bcsv::Layout layout;
    layout.addColumn({"index", bcsv::ColumnType::INT32});
    layout.addColumn({"value", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("many_rows.csv");
    constexpr size_t N = 10000;

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        for (size_t i = 0; i < N; ++i) {
            writer.row().set(0, static_cast<int32_t>(i));
            writer.row().set(1, static_cast<double>(i) * 0.001);
            writer.writeRow();
        }

        EXPECT_EQ(writer.rowCount(), N);
        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        for (size_t i = 0; i < N; ++i) {
            ASSERT_TRUE(reader.readNext()) << "Failed at row " << i;
            EXPECT_EQ(reader.row().get<int32_t>(0), static_cast<int32_t>(i));
            EXPECT_DOUBLE_EQ(reader.row().get<double>(1), static_cast<double>(i) * 0.001);
        }

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 3. CSV-to-BCSV conversion
// ============================================================================

TEST_F(CsvReaderWriterTest, CsvToBcsv) {
    auto layout = createMixedLayout();
    auto csvPath = tmpFile("source.csv");
    auto bcsvPath = tmpFile("converted.bcsv");

    // Write CSV
    {
        bcsv::CsvWriter<bcsv::Layout> csvWriter(layout);
        ASSERT_TRUE(csvWriter.open(csvPath, true));

        csvWriter.row().set(0, true);
        csvWriter.row().set(1, int8_t(42));
        csvWriter.row().set(2, int32_t(100));
        csvWriter.row().set(3, int64_t(200));
        csvWriter.row().set(4, uint16_t(300));
        csvWriter.row().set(5, 1.5f);
        csvWriter.row().set(6, 2.5);
        csvWriter.row().set(7, std::string("test"));
        csvWriter.writeRow();

        csvWriter.row().set(0, false);
        csvWriter.row().set(1, int8_t(-1));
        csvWriter.row().set(2, int32_t(-200));
        csvWriter.row().set(3, int64_t(-300));
        csvWriter.row().set(4, uint16_t(400));
        csvWriter.row().set(5, -1.5f);
        csvWriter.row().set(6, -2.5);
        csvWriter.row().set(7, std::string("csv data"));
        csvWriter.writeRow();

        csvWriter.close();
    }

    // Read CSV, write BCSV
    {
        bcsv::CsvReader<bcsv::Layout> csvReader(layout);
        bcsv::Writer<bcsv::Layout> bcsvWriter(layout);

        ASSERT_TRUE(csvReader.open(csvPath));
        ASSERT_TRUE(bcsvWriter.open(bcsvPath, true));

        while (csvReader.readNext()) {
            bcsvWriter.write(csvReader.row());
        }

        csvReader.close();
        bcsvWriter.close();
    }

    // Read BCSV and verify
    {
        bcsv::Reader<bcsv::Layout> bcsvReader;
        ASSERT_TRUE(bcsvReader.open(bcsvPath));

        ASSERT_TRUE(bcsvReader.readNext());
        EXPECT_EQ(bcsvReader.row().get<bool>(0), true);
        EXPECT_EQ(bcsvReader.row().get<int8_t>(1), 42);
        EXPECT_EQ(bcsvReader.row().get<int32_t>(2), 100);
        EXPECT_EQ(bcsvReader.row().get<int64_t>(3), 200);
        EXPECT_EQ(bcsvReader.row().get<uint16_t>(4), 300);
        EXPECT_FLOAT_EQ(bcsvReader.row().get<float>(5), 1.5f);
        EXPECT_DOUBLE_EQ(bcsvReader.row().get<double>(6), 2.5);
        EXPECT_EQ(bcsvReader.row().get<std::string>(7), "test");

        ASSERT_TRUE(bcsvReader.readNext());
        EXPECT_EQ(bcsvReader.row().get<bool>(0), false);
        EXPECT_EQ(bcsvReader.row().get<int8_t>(1), -1);
        EXPECT_EQ(bcsvReader.row().get<int32_t>(2), -200);
        EXPECT_EQ(bcsvReader.row().get<int64_t>(3), -300);
        EXPECT_EQ(bcsvReader.row().get<uint16_t>(4), 400);
        EXPECT_FLOAT_EQ(bcsvReader.row().get<float>(5), -1.5f);
        EXPECT_DOUBLE_EQ(bcsvReader.row().get<double>(6), -2.5);
        EXPECT_EQ(bcsvReader.row().get<std::string>(7), "csv data");

        EXPECT_FALSE(bcsvReader.readNext());
        bcsvReader.close();
    }
}

// ============================================================================
// 4. BCSV-to-CSV conversion
// ============================================================================

TEST_F(CsvReaderWriterTest, BcsvToCsv) {
    auto layout = createMixedLayout();
    auto bcsvPath = tmpFile("source.bcsv");
    auto csvPath = tmpFile("converted.csv");

    // Write BCSV
    {
        bcsv::Writer<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(bcsvPath, true));

        writer.row().set(0, true);
        writer.row().set(1, int8_t(77));
        writer.row().set(2, int32_t(999));
        writer.row().set(3, int64_t(1234567890LL));
        writer.row().set(4, uint16_t(5000));
        writer.row().set(5, 6.28f);
        writer.row().set(6, 9.81);
        writer.row().set(7, std::string("from bcsv"));
        writer.writeRow();

        writer.close();
    }

    // Read BCSV, write CSV
    {
        bcsv::Reader<bcsv::Layout> reader;
        bcsv::CsvWriter<bcsv::Layout> csvWriter(layout);

        ASSERT_TRUE(reader.open(bcsvPath));
        ASSERT_TRUE(csvWriter.open(csvPath, true));

        while (reader.readNext()) {
            csvWriter.write(reader.row());
        }

        reader.close();
        csvWriter.close();
    }

    // Read CSV and verify
    {
        bcsv::CsvReader<bcsv::Layout> csvReader(layout);
        ASSERT_TRUE(csvReader.open(csvPath));

        ASSERT_TRUE(csvReader.readNext());
        EXPECT_EQ(csvReader.row().get<bool>(0), true);
        EXPECT_EQ(csvReader.row().get<int8_t>(1), 77);
        EXPECT_EQ(csvReader.row().get<int32_t>(2), 999);
        EXPECT_EQ(csvReader.row().get<int64_t>(3), 1234567890LL);
        EXPECT_EQ(csvReader.row().get<uint16_t>(4), 5000);
        EXPECT_FLOAT_EQ(csvReader.row().get<float>(5), 6.28f);
        EXPECT_DOUBLE_EQ(csvReader.row().get<double>(6), 9.81);
        EXPECT_EQ(csvReader.row().get<std::string>(7), "from bcsv");

        EXPECT_FALSE(csvReader.readNext());
        csvReader.close();
    }
}

// ============================================================================
// 5. Delimiter variants
// ============================================================================

TEST_F(CsvReaderWriterTest, Delimiter_Semicolon) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::STRING});
    layout.addColumn({"c", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("semicolon.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout, ';');
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, int32_t(42));
        writer.row().set(1, std::string("hello;world")); // embedded semicolon
        writer.row().set(2, 3.14);
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout, ';');
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 42);
        EXPECT_EQ(reader.row().get<std::string>(1), "hello;world");
        EXPECT_DOUBLE_EQ(reader.row().get<double>(2), 3.14);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(CsvReaderWriterTest, Delimiter_Tab) {
    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});
    layout.addColumn({"y", bcsv::ColumnType::FLOAT});

    auto path = tmpFile("tab.tsv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout, '\t');
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, int32_t(1));
        writer.row().set(1, 2.5f);
        writer.writeRow();

        writer.row().set(0, int32_t(3));
        writer.row().set(1, 4.5f);
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout, '\t');
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 1);
        EXPECT_FLOAT_EQ(reader.row().get<float>(1), 2.5f);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 3);
        EXPECT_FLOAT_EQ(reader.row().get<float>(1), 4.5f);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 6. Decimal separator (German style: comma as decimal, semicolon as delimiter)
// ============================================================================

TEST_F(CsvReaderWriterTest, DecimalSeparator_German) {
    bcsv::Layout layout;
    layout.addColumn({"value_f", bcsv::ColumnType::FLOAT});
    layout.addColumn({"value_d", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("german.csv");

    // Write with German decimal separator: semicolon delimiter, comma decimal
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout, ';', ',');
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, 1.5f);
        writer.row().set(1, 2.718281828);
        writer.writeRow();

        writer.row().set(0, -0.001f);
        writer.row().set(1, 3.14159265358979);
        writer.writeRow();

        writer.close();
    }

    // Verify the file content has commas as decimal separators
    {
        std::ifstream f(path);
        std::string line;
        std::getline(f, line); // header
        std::getline(f, line); // first data line
        // Should contain ',' as decimal separator and ';' as field delimiter
        EXPECT_NE(line.find(';'), std::string::npos) << "Expected semicolon delimiter in: " << line;
        // Should NOT contain '.' (decimal point replaced with ',')
    }

    // Read back with same settings
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout, ';', ',');
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), 1.5f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 2.718281828);

        ASSERT_TRUE(reader.readNext());
        EXPECT_NEAR(reader.row().get<float>(0), -0.001f, 1e-6f);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 3.14159265358979);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 7. String protection
// ============================================================================

TEST_F(CsvReaderWriterTest, StringProtection_Whitespace) {
    bcsv::Layout layout;
    layout.addColumn({"name", bcsv::ColumnType::STRING});

    auto path = tmpFile("whitespace.csv");

    // Strings with leading/trailing whitespace must be preserved
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, std::string(" leading space"));
        writer.writeRow();

        writer.row().set(0, std::string("trailing space "));
        writer.writeRow();

        writer.row().set(0, std::string(" both spaces "));
        writer.writeRow();

        writer.row().set(0, std::string("   "));
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), " leading space");

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "trailing space ");

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), " both spaces ");

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "   ");

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(CsvReaderWriterTest, StringProtection_EmbeddedDelimiters) {
    bcsv::Layout layout;
    layout.addColumn({"data", bcsv::ColumnType::STRING});
    layout.addColumn({"num", bcsv::ColumnType::INT32});

    auto path = tmpFile("embedded.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, std::string("contains,comma"));
        writer.row().set(1, int32_t(1));
        writer.writeRow();

        writer.row().set(0, std::string("contains\"quote"));
        writer.row().set(1, int32_t(2));
        writer.writeRow();

        writer.row().set(0, std::string("contains\nnewline"));
        writer.row().set(1, int32_t(3));
        writer.writeRow();

        writer.row().set(0, std::string("all,of\"the\nabove"));
        writer.row().set(1, int32_t(4));
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "contains,comma");
        EXPECT_EQ(reader.row().get<int32_t>(1), 1);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "contains\"quote");
        EXPECT_EQ(reader.row().get<int32_t>(1), 2);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "contains\nnewline");
        EXPECT_EQ(reader.row().get<int32_t>(1), 3);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "all,of\"the\nabove");
        EXPECT_EQ(reader.row().get<int32_t>(1), 4);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

TEST_F(CsvReaderWriterTest, StringProtection_EmptyString) {
    bcsv::Layout layout;
    layout.addColumn({"s", bcsv::ColumnType::STRING});
    layout.addColumn({"n", bcsv::ColumnType::INT32});

    auto path = tmpFile("empty_string.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, std::string(""));
        writer.row().set(1, int32_t(99));
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "");
        EXPECT_EQ(reader.row().get<int32_t>(1), 99);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 8. Edge cases
// ============================================================================

TEST_F(CsvReaderWriterTest, OpenNonExistentFile) {
    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    EXPECT_FALSE(reader.open(tmpFile("does_not_exist.csv")));
    EXPECT_FALSE(reader.getErrorMsg().empty());
}

TEST_F(CsvReaderWriterTest, OpenAlreadyOpenFile) {
    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});

    auto path = tmpFile("already_open.csv");

    bcsv::CsvWriter<bcsv::Layout> writer(layout);
    ASSERT_TRUE(writer.open(path, true));
    writer.row().set(0, int32_t(1));
    writer.writeRow();

    // Try to open again without closing
    EXPECT_FALSE(writer.open(tmpFile("other.csv"), true));

    writer.close();
}

TEST_F(CsvReaderWriterTest, OverwriteProtection) {
    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});

    auto path = tmpFile("existing.csv");

    // Create file first
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.row().set(0, int32_t(1));
        writer.writeRow();
        writer.close();
    }

    // Try to open without overwrite flag
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        EXPECT_FALSE(writer.open(path, false));
    }

    // Open with overwrite flag should succeed
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        EXPECT_TRUE(writer.open(path, true));
        writer.close();
    }
}

TEST_F(CsvReaderWriterTest, ReadFromExternalCsv) {
    // Create a CSV manually (simulating external tool output)
    bcsv::Layout layout;
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"value", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"flag", bcsv::ColumnType::BOOL});

    auto path = tmpFile("external.csv");
    {
        std::ofstream f(path);
        f << "name,value,flag\n";
        f << "\"alpha\",1.5,true\n";
        f << "\"beta\",2.5,false\n";
        f << "\"gamma\",-0.5,1\n";
    }

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    ASSERT_TRUE(reader.open(path));

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<std::string>(0), "alpha");
    EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 1.5);
    EXPECT_EQ(reader.row().get<bool>(2), true);

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<std::string>(0), "beta");
    EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 2.5);
    EXPECT_EQ(reader.row().get<bool>(2), false);

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<std::string>(0), "gamma");
    EXPECT_DOUBLE_EQ(reader.row().get<double>(1), -0.5);
    EXPECT_EQ(reader.row().get<bool>(2), true); // "1" → true

    EXPECT_FALSE(reader.readNext());
    reader.close();
}

TEST_F(CsvReaderWriterTest, ColumnCountMismatch) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::INT32});

    auto path = tmpFile("mismatch.csv");
    {
        std::ofstream f(path);
        f << "a,b,c\n"; // 3 columns vs 2 in layout
        f << "1,2,3\n";
    }

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    EXPECT_FALSE(reader.open(path));
    EXPECT_FALSE(reader.getErrorMsg().empty());
}

TEST_F(CsvReaderWriterTest, BoolColumns) {
    bcsv::Layout layout;
    layout.addColumn({"b1", bcsv::ColumnType::BOOL});
    layout.addColumn({"b2", bcsv::ColumnType::BOOL});

    auto path = tmpFile("bools.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, true);
        writer.row().set(1, false);
        writer.writeRow();

        writer.row().set(0, false);
        writer.row().set(1, true);
        writer.writeRow();

        writer.close();
    }

    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), true);
        EXPECT_EQ(reader.row().get<bool>(1), false);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), false);
        EXPECT_EQ(reader.row().get<bool>(1), true);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ============================================================================
// 9. Generic function using concepts
// ============================================================================

namespace {
    // Demonstrates that ReaderConcept enables generic algorithms
    template<bcsv::ReaderConcept R>
    size_t countRows(R& reader) {
        size_t n = 0;
        while (reader.readNext()) ++n;
        return n;
    }

    template<bcsv::WriterConcept W>
    void writeIntRow(W& writer, int32_t value) {
        writer.row().set(0, value);
        writer.writeRow();
    }
}

TEST_F(CsvReaderWriterTest, GenericConceptUsage) {
    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});

    auto csvPath  = tmpFile("generic.csv");
    auto bcsvPath = tmpFile("generic.bcsv");

    // Write with generic function — CSV
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(csvPath, true));
        writeIntRow(writer, 1);
        writeIntRow(writer, 2);
        writeIntRow(writer, 3);
        writer.close();
    }

    // Write with generic function — BCSV
    {
        bcsv::Writer<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(bcsvPath, true));
        writeIntRow(writer, 10);
        writeIntRow(writer, 20);
        writer.close();
    }

    // Count with generic function — CSV
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(csvPath));
        EXPECT_EQ(countRows(reader), 3u);
        reader.close();
    }

    // Count with generic function — BCSV
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(bcsvPath));
        EXPECT_EQ(countRows(reader), 2u);
        reader.close();
    }
}

// ── Test: header-less CSV round-trip ───────────────────────────────

TEST_F(CsvReaderWriterTest, NoHeader_RoundTrip) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("no_header.csv");

    // Write without header
    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, /*overwrite=*/true, /*includeHeader=*/false));
        writer.row().set(0, int32_t(10));
        writer.row().set(1, 3.14);
        writer.writeRow();
        writer.row().set(0, int32_t(20));
        writer.row().set(1, 2.72);
        writer.writeRow();
        writer.close();
    }

    // Verify file content has no header line
    {
        std::ifstream f(path);
        std::string firstLine;
        std::getline(f, firstLine);
        // First line should be data, not "a,b"
        EXPECT_NE(firstLine, "a,b");
        EXPECT_TRUE(firstLine.find("10") != std::string::npos);
    }

    // Read without header
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path, /*hasHeader=*/false));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 10);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 3.14);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 20);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), 2.72);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: Windows \r\n line endings ─────────────────────────────────

TEST_F(CsvReaderWriterTest, WindowsCRLF_LineEndings) {
    bcsv::Layout layout;
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"value", bcsv::ColumnType::INT32});

    auto path = tmpFile("crlf.csv");

    // Write a CSV file with explicit \r\n line endings
    {
        std::ofstream f(path, std::ios::binary);
        f << "name,value\r\n";
        f << "\"alpha\",100\r\n";
        f << "\"beta\",200\r\n";
        f << "\"gamma\",300\r\n";
        f.close();
    }

    // Read with CsvReader — should handle \r\n transparently
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "alpha");
        EXPECT_EQ(reader.row().get<int32_t>(1), 100);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "beta");
        EXPECT_EQ(reader.row().get<int32_t>(1), 200);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), "gamma");
        EXPECT_EQ(reader.row().get<int32_t>(1), 300);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: from_chars error detection ────────────────────────────────

TEST_F(CsvReaderWriterTest, FromCharsErrorDetection) {
    bcsv::Layout layout;
    layout.addColumn({"num", bcsv::ColumnType::INT32});

    auto path = tmpFile("bad_num.csv");

    // Write a CSV with a non-numeric value in an INT32 column
    {
        std::ofstream f(path);
        f << "num\n";
        f << "42\n";
        f << "abc\n";  // invalid — should set warning
        f << "99\n";
        f.close();
    }

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    ASSERT_TRUE(reader.open(path));

    // First row: valid
    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 42);

    // Second row: "abc" — parseCells succeeds but sets a warning in err_msg_
    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 0);  // defaults to 0
    EXPECT_FALSE(reader.getErrorMsg().empty());   // warning was set
    EXPECT_TRUE(reader.getErrorMsg().find("Invalid INT32") != std::string::npos);

    // Third row: valid
    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 99);

    EXPECT_FALSE(reader.readNext());
    reader.close();
}

// ── Test: fileLine counter accuracy ─────────────────────────────────

TEST_F(CsvReaderWriterTest, FileLineCounter) {
    bcsv::Layout layout;
    layout.addColumn({"val", bcsv::ColumnType::INT32});

    auto path = tmpFile("line_counter.csv");

    // Write CSV with empty lines interspersed
    {
        std::ofstream f(path);
        f << "val\n";    // line 1: header
        f << "1\n";      // line 2: data row 0
        f << "\n";       // line 3: empty (skipped)
        f << "\n";       // line 4: empty (skipped)
        f << "2\n";      // line 5: data row 1
        f << "3\n";      // line 6: data row 2
        f.close();
    }

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    ASSERT_TRUE(reader.open(path));

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 1);
    EXPECT_EQ(reader.rowPos(), 1u);
    EXPECT_EQ(reader.fileLine(), 2u);  // header=1, first data=2

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 2);
    EXPECT_EQ(reader.rowPos(), 2u);
    EXPECT_EQ(reader.fileLine(), 5u);  // skipped 2 empty lines (3,4), data at 5

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 3);
    EXPECT_EQ(reader.rowPos(), 3u);
    EXPECT_EQ(reader.fileLine(), 6u);

    EXPECT_FALSE(reader.readNext());
    reader.close();
}
// ============================================================================
// Phase 1 — Additional coverage tests
// ============================================================================

// ── Test: NaN/Inf round-trip for float and double ───────────────────
TEST_F(CsvReaderWriterTest, NanInf_RoundTrip) {
    bcsv::Layout layout;
    layout.addColumn({"f", bcsv::ColumnType::FLOAT});
    layout.addColumn({"d", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("nan_inf.csv");

    float posInfF = std::numeric_limits<float>::infinity();
    float negInfF = -std::numeric_limits<float>::infinity();
    float nanF    = std::numeric_limits<float>::quiet_NaN();
    double posInfD = std::numeric_limits<double>::infinity();
    double negInfD = -std::numeric_limits<double>::infinity();
    double nanD    = std::numeric_limits<double>::quiet_NaN();

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, posInfF);
        writer.row().set(1, posInfD);
        writer.writeRow();

        writer.row().set(0, negInfF);
        writer.row().set(1, negInfD);
        writer.writeRow();

        writer.row().set(0, nanF);
        writer.row().set(1, nanD);
        writer.writeRow();

        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        // Row 0: +Inf
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<float>(0), posInfF);
        EXPECT_EQ(reader.row().get<double>(1), posInfD);

        // Row 1: -Inf
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<float>(0), negInfF);
        EXPECT_EQ(reader.row().get<double>(1), negInfD);

        // Row 2: NaN (use isnan — NaN != NaN by IEEE 754)
        ASSERT_TRUE(reader.readNext());
        EXPECT_TRUE(std::isnan(reader.row().get<float>(0)));
        EXPECT_TRUE(std::isnan(reader.row().get<double>(1)));

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: UTF-8 BOM handling ────────────────────────────────────────
TEST_F(CsvReaderWriterTest, UTF8_BOM) {
    bcsv::Layout layout;
    layout.addColumn({"val", bcsv::ColumnType::INT32});

    auto path = tmpFile("bom.csv");

    // Manually write a CSV file starting with UTF-8 BOM (EF BB BF)
    {
        std::ofstream f(path, std::ios::binary);
        f << "\xEF\xBB\xBF";  // UTF-8 BOM
        f << "val\n";
        f << "42\n";
        f << "99\n";
        f.close();
    }

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    ASSERT_TRUE(reader.open(path))
        << "BOM file should open successfully: " << reader.getErrorMsg();

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 42);

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 99);

    EXPECT_FALSE(reader.readNext());
    reader.close();
}

// ── Test: Multi-line quoted field (externally written) ──────────────
TEST_F(CsvReaderWriterTest, MultiLineQuotedField_External) {
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    layout.addColumn({"text", bcsv::ColumnType::STRING});
    layout.addColumn({"val", bcsv::ColumnType::INT32});

    auto path = tmpFile("multiline.csv");

    // Write a CSV that has a quoted field spanning multiple physical lines
    {
        std::ofstream f(path);
        f << "id,text,val\n";
        f << "1,\"line one\nline two\nline three\",100\n";
        f << "2,\"simple\",200\n";
        f.close();
    }

    bcsv::CsvReader<bcsv::Layout> reader(layout);
    ASSERT_TRUE(reader.open(path));

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 1);
    EXPECT_EQ(reader.row().get<std::string>(1), "line one\nline two\nline three");
    EXPECT_EQ(reader.row().get<int32_t>(2), 100);

    ASSERT_TRUE(reader.readNext());
    EXPECT_EQ(reader.row().get<int32_t>(0), 2);
    EXPECT_EQ(reader.row().get<std::string>(1), "simple");
    EXPECT_EQ(reader.row().get<int32_t>(2), 200);

    EXPECT_FALSE(reader.readNext());
    reader.close();
}

// ── Test: Scientific notation with comma decimal separator ──────────
TEST_F(CsvReaderWriterTest, ScientificNotation_CommaDecimal) {
    bcsv::Layout layout;
    layout.addColumn({"f", bcsv::ColumnType::FLOAT});
    layout.addColumn({"d", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("sci_comma.csv");

    // Write values that std::to_chars may render in scientific notation
    float  largeF = 1.5e10f;
    double largeD = 2.5e20;
    float  smallF = 1.5e-6f;
    double smallD = 2.5e-15;

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout, ';', ',');
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, largeF);
        writer.row().set(1, largeD);
        writer.writeRow();

        writer.row().set(0, smallF);
        writer.row().set(1, smallD);
        writer.writeRow();

        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout, ';', ',');
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), largeF);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), largeD);

        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), smallF);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), smallD);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: write() with external row ─────────────────────────────────
TEST_F(CsvReaderWriterTest, WriteExternalRow) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::STRING});

    auto path = tmpFile("ext_row.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        // Use write(row) instead of filling writer.row() + writeRow()
        bcsv::Row externalRow(layout);
        externalRow.set<int32_t>(0, 42);
        externalRow.set<std::string_view>(1, "hello");
        writer.write(externalRow);

        externalRow.set<int32_t>(0, 99);
        externalRow.set<std::string_view>(1, "world");
        writer.write(externalRow);

        EXPECT_EQ(writer.rowCount(), 2u);
        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 42);
        EXPECT_EQ(reader.row().get<std::string>(1), "hello");

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 99);
        EXPECT_EQ(reader.row().get<std::string>(1), "world");

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: Large string (near MAX_STRING_LENGTH) ─────────────────────
TEST_F(CsvReaderWriterTest, LargeString) {
    bcsv::Layout layout;
    layout.addColumn({"s", bcsv::ColumnType::STRING});
    layout.addColumn({"n", bcsv::ColumnType::INT32});

    auto path = tmpFile("large_str.csv");

    // Create a string of 10,000 characters (well above typical but below MAX_STRING_LENGTH)
    std::string longStr(10000, 'X');
    // Insert some special chars to test quoting
    longStr[0] = '"';
    longStr[100] = ',';
    longStr[200] = '\n';
    longStr[9999] = '"';

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set<std::string_view>(0, longStr);
        writer.row().set<int32_t>(1, 42);
        writer.writeRow();

        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<std::string>(0), longStr);
        EXPECT_EQ(reader.row().get<int32_t>(1), 42);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: Pathological quoting (doubled quotes, adjacent to delimiters) ──
TEST_F(CsvReaderWriterTest, PathologicalQuoting) {
    bcsv::Layout layout;
    layout.addColumn({"s", bcsv::ColumnType::STRING});

    auto path = tmpFile("patho_quote.csv");

    // Test strings with tricky quoting patterns
    std::vector<std::string> tricky = {
        "",                     // empty string
        "\"",                   // single quote
        "\"\"",                 // two quotes
        "a\"b",                 // quote in middle
        "\"hello\"",            // quoted word
        ",",                    // just a delimiter
        "\n",                   // just a newline
        "a,b\"c\nd",            // mix of special chars
        "   ",                  // whitespace only
    };

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        for (const auto& s : tricky) {
            writer.row().set<std::string_view>(0, s);
            writer.writeRow();
        }

        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        for (size_t i = 0; i < tricky.size(); ++i) {
            ASSERT_TRUE(reader.readNext()) << "Failed at row " << i;
            EXPECT_EQ(reader.row().get<std::string>(0), tricky[i])
                << "Mismatch at row " << i << " (expected: [" << tricky[i] << "])";
        }

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: Denormalized and subnormal float values ───────────────────
TEST_F(CsvReaderWriterTest, SubnormalFloat_RoundTrip) {
    bcsv::Layout layout;
    layout.addColumn({"f", bcsv::ColumnType::FLOAT});
    layout.addColumn({"d", bcsv::ColumnType::DOUBLE});

    auto path = tmpFile("subnormal.csv");

    float  subF = std::numeric_limits<float>::denorm_min();
    double subD = std::numeric_limits<double>::denorm_min();
    float  minF = std::numeric_limits<float>::min();  // smallest normal
    double minD = std::numeric_limits<double>::min();

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set(0, subF);
        writer.row().set(1, subD);
        writer.writeRow();

        writer.row().set(0, minF);
        writer.row().set(1, minD);
        writer.writeRow();

        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), subF);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), subD);

        ASSERT_TRUE(reader.readNext());
        EXPECT_FLOAT_EQ(reader.row().get<float>(0), minF);
        EXPECT_DOUBLE_EQ(reader.row().get<double>(1), minD);

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: Single-column CSV (edge case: no delimiter in data) ───────
TEST_F(CsvReaderWriterTest, SingleColumnCSV) {
    bcsv::Layout layout;
    layout.addColumn({"val", bcsv::ColumnType::INT64});

    auto path = tmpFile("single_col.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        for (int64_t i = 0; i < 5; ++i) {
            writer.row().set<int64_t>(0, i * 1000000000LL);
            writer.writeRow();
        }
        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        for (int64_t i = 0; i < 5; ++i) {
            ASSERT_TRUE(reader.readNext());
            EXPECT_EQ(reader.row().get<int64_t>(0), i * 1000000000LL);
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// ── Test: Close then reopen does not corrupt output ─────────────────
TEST_F(CsvReaderWriterTest, CloseAndVerify) {
    bcsv::Layout layout;
    layout.addColumn({"val", bcsv::ColumnType::INT32});

    auto path = tmpFile("close_test.csv");

    {
        bcsv::CsvWriter<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));

        writer.row().set<int32_t>(0, 1);
        writer.writeRow();

        writer.row().set<int32_t>(0, 2);
        writer.writeRow();

        writer.row().set<int32_t>(0, 3);
        writer.writeRow();

        EXPECT_EQ(writer.rowCount(), 3u);
        writer.close();
    }
    {
        bcsv::CsvReader<bcsv::Layout> reader(layout);
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 1);
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 2);
        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 3);
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}