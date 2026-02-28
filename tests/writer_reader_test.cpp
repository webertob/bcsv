/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file writer_reader_test.cpp
 * @brief Writer/Reader lifecycle and error-path tests
 * 
 * Tests cover:
 * - Double-close (Writer and Reader)
 * - Write-after-close
 * - Flush semantics (before/after close, on unopened writer)
 * - Read-after-close
 * - Close without open
 * - Basic write-read round-trip validation
 * - Multi-packet round-trip
 * - ZoH write-read round-trip
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <bcsv/bcsv.h>

using namespace bcsv;
namespace fs = std::filesystem;

class WriterReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Per-test subdirectory prevents parallel TearDown races.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (fs::temp_directory_path() / "bcsv_writer_reader_test"
                     / (std::string(info->test_suite_name()) + "_" + info->name())).string();
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        fs::remove_all(test_dir_);
    }

    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }

    // Create a simple layout for testing
    Layout makeLayout() {
        Layout layout;
        layout.addColumn({"i", ColumnType::INT32});
        layout.addColumn({"f", ColumnType::FLOAT});
        layout.addColumn({"s", ColumnType::STRING});
        return layout;
    }

    // Write a few rows to a file and close it, returns row count written
    size_t writeTestFile(const std::string& path, size_t rows = 10) {
        Layout layout = makeLayout();
        Writer<Layout> writer(layout);
        EXPECT_TRUE(writer.open(path, true));
        for (size_t i = 0; i < rows; ++i) {
            writer.row().set(0, static_cast<int32_t>(i));
            writer.row().set(1, static_cast<float>(i) * 1.5f);
            writer.row().set(2, std::string("row_") + std::to_string(i));
            writer.writeRow();
        }
        writer.close();
        return rows;
    }

    std::string test_dir_;
};

// ============================================================================
// Writer: double-close
// ============================================================================
TEST_F(WriterReaderTest, Writer_DoubleClose_IsHarmless) {
    Layout layout = makeLayout();
    Writer<Layout> writer(layout);
    auto path = testFile("double_close.bcsv");

    ASSERT_TRUE(writer.open(path, true));
    writer.row().set(0, 42);
    writer.row().set(1, 3.14f);
    writer.row().set(2, std::string("hello"));
    writer.writeRow();

    writer.close();
    EXPECT_FALSE(writer.isOpen());

    // Second close should be harmless (early return on !stream_.is_open())
    EXPECT_NO_THROW(writer.close());
    EXPECT_FALSE(writer.isOpen());
}

// ============================================================================
// Writer: write-after-close
// ============================================================================
TEST_F(WriterReaderTest, Writer_WriteAfterClose_Throws) {
    Layout layout = makeLayout();
    Writer<Layout> writer(layout);
    auto path = testFile("write_after_close.bcsv");

    ASSERT_TRUE(writer.open(path, true));
    writer.row().set(0, 1);
    writer.row().set(1, 1.0f);
    writer.row().set(2, std::string("test"));
    writer.writeRow();
    writer.close();

    // writeRow() on closed writer should throw
    EXPECT_THROW(writer.writeRow(), std::runtime_error);
}

// ============================================================================
// Writer: flush semantics
// ============================================================================
TEST_F(WriterReaderTest, Writer_FlushOnUnopenedWriter_IsHarmless) {
    Layout layout = makeLayout();
    Writer<Layout> writer(layout);

    // Flush on never-opened writer should be harmless
    EXPECT_NO_THROW(writer.flush());
}

TEST_F(WriterReaderTest, Writer_FlushAfterClose_IsHarmless) {
    Layout layout = makeLayout();
    Writer<Layout> writer(layout);
    auto path = testFile("flush_after_close.bcsv");

    ASSERT_TRUE(writer.open(path, true));
    writer.row().set(0, 1);
    writer.row().set(1, 1.0f);
    writer.row().set(2, std::string("x"));
    writer.writeRow();
    writer.close();

    // Flush on closed writer should be harmless
    EXPECT_NO_THROW(writer.flush());
}

TEST_F(WriterReaderTest, Writer_FlushWhileOpen_Succeeds) {
    Layout layout = makeLayout();
    Writer<Layout> writer(layout);
    auto path = testFile("flush_while_open.bcsv");

    ASSERT_TRUE(writer.open(path, true));
    writer.row().set(0, 99);
    writer.row().set(1, 2.5f);
    writer.row().set(2, std::string("flush"));
    writer.writeRow();

    EXPECT_NO_THROW(writer.flush());
    EXPECT_TRUE(writer.isOpen());

    // File should exist and have non-zero size after flush
    EXPECT_TRUE(fs::exists(path));
    EXPECT_GT(fs::file_size(path), 0u);

    writer.close();
}

// ============================================================================
// Writer: close without open
// ============================================================================
TEST_F(WriterReaderTest, Writer_CloseWithoutOpen_IsHarmless) {
    Layout layout = makeLayout();
    Writer<Layout> writer(layout);

    EXPECT_FALSE(writer.isOpen());
    EXPECT_NO_THROW(writer.close());
    EXPECT_FALSE(writer.isOpen());
}

// ============================================================================
// Reader: double-close
// ============================================================================
TEST_F(WriterReaderTest, Reader_DoubleClose_IsHarmless) {
    auto path = testFile("reader_double_close.bcsv");
    writeTestFile(path);

    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));

    reader.close();
    EXPECT_FALSE(reader.isOpen());

    // Second close should be harmless
    EXPECT_NO_THROW(reader.close());
    EXPECT_FALSE(reader.isOpen());
}

// ============================================================================
// Reader: read-after-close
// ============================================================================
TEST_F(WriterReaderTest, Reader_ReadAfterClose_ReturnsFalse) {
    auto path = testFile("reader_read_after_close.bcsv");
    writeTestFile(path, 5);

    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));

    // Read one row to ensure it works
    EXPECT_TRUE(reader.readNext());

    reader.close();

    // readNext() on closed reader should return false (not crash)
    EXPECT_FALSE(reader.readNext());
}

// ============================================================================
// Reader: close without open
// ============================================================================
TEST_F(WriterReaderTest, Reader_CloseWithoutOpen_IsHarmless) {
    Reader<Layout> reader;

    EXPECT_FALSE(reader.isOpen());
    EXPECT_NO_THROW(reader.close());
    EXPECT_FALSE(reader.isOpen());
}

// ============================================================================
// Reader: readNext on never-opened reader
// ============================================================================
TEST_F(WriterReaderTest, Reader_ReadNextWithoutOpen_ReturnsFalse) {
    Reader<Layout> reader;

    EXPECT_FALSE(reader.readNext());
}

// ============================================================================
// Round-trip: write N rows, read N rows, verify data integrity
// ============================================================================
TEST_F(WriterReaderTest, RoundTrip_BasicIntegrity) {
    auto path = testFile("round_trip.bcsv");
    constexpr size_t N = 50;

    // Write
    Layout wLayout = makeLayout();
    Writer<Layout> writer(wLayout);
    ASSERT_TRUE(writer.open(path, true));
    for (size_t i = 0; i < N; ++i) {
        writer.row().set(0, static_cast<int32_t>(i * 3));
        writer.row().set(1, static_cast<float>(i) * 0.25f);
        writer.row().set(2, std::string("val_") + std::to_string(i));
        writer.writeRow();
    }
    writer.close();

    // Read and verify
    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));

    size_t count = 0;
    while (reader.readNext()) {
        EXPECT_EQ(reader.row().template get<int32_t>(0), static_cast<int32_t>(count * 3));
        EXPECT_FLOAT_EQ(reader.row().template get<float>(1), static_cast<float>(count) * 0.25f);
        EXPECT_EQ(reader.row().template get<std::string>(2), std::string("val_") + std::to_string(count));
        ++count;
    }
    EXPECT_EQ(count, N);
    reader.close();
}

// ============================================================================
// Round-trip: multi-packet (enough rows to trigger packet boundaries)
// ============================================================================
TEST_F(WriterReaderTest, RoundTrip_MultiPacket) {
    auto path = testFile("multi_packet.bcsv");
    // Default packet size is ~64KB. With 3 columns (int32 + float + string),
    // each row is roughly 20-30 bytes, so 5000 rows should span multiple packets.
    constexpr size_t N = 5000;

    Layout wLayout = makeLayout();
    Writer<Layout> writer(wLayout);
    ASSERT_TRUE(writer.open(path, true));
    for (size_t i = 0; i < N; ++i) {
        writer.row().set(0, static_cast<int32_t>(i));
        writer.row().set(1, static_cast<float>(i));
        writer.row().set(2, std::string("r") + std::to_string(i));
        writer.writeRow();
    }
    writer.close();

    // Read all back
    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));

    size_t count = 0;
    while (reader.readNext()) {
        EXPECT_EQ(reader.row().template get<int32_t>(0), static_cast<int32_t>(count));
        ++count;
    }
    EXPECT_EQ(count, N);
    reader.close();
}

// ============================================================================
// Round-trip: ZoH encoding
// ============================================================================
TEST_F(WriterReaderTest, RoundTrip_ZoH) {
    auto path = testFile("round_trip_zoh.bcsv");
    constexpr size_t N = 100;

    // WriterZoH selects RowCodecZoH001 at compile time;
    // FileFlags::ZERO_ORDER_HOLD flag is written to the file header
    // so Reader can select the matching codec at open time.
    using ZoHLayout = LayoutStatic<int32_t, float>;
    using ZoHWriter = WriterZoH<ZoHLayout>;
    using ZoHReader = Reader<ZoHLayout>;

    ZoHLayout wLayout({"counter", "value"});
    ZoHWriter writer(wLayout);
    ASSERT_TRUE(writer.open(path, true, 0, 64, FileFlags::ZERO_ORDER_HOLD));

    for (size_t i = 0; i < N; ++i) {
        writer.row().set<0>(static_cast<int32_t>(i));
        // Only change float every 10 rows (ZoH should compress well)
        writer.row().set<1>(static_cast<float>(i / 10));
        writer.writeRow();
    }
    writer.close();

    // Read back
    ZoHReader reader;
    ASSERT_TRUE(reader.open(path));

    size_t count = 0;
    while (reader.readNext()) {
        auto val0 = reader.row().template get<0>();
        auto val1 = reader.row().template get<1>();
        EXPECT_EQ(val0, static_cast<int32_t>(count));
        EXPECT_FLOAT_EQ(val1, static_cast<float>(count / 10));
        ++count;
    }
    EXPECT_EQ(count, N);
    reader.close();
}

// ============================================================================
// Writer: destructor calls close (data is recoverable)
// ============================================================================
TEST_F(WriterReaderTest, Writer_DestructorClosesFile) {
    auto path = testFile("destructor_close.bcsv");
    constexpr size_t N = 5;

    {
        Layout layout = makeLayout();
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        for (size_t i = 0; i < N; ++i) {
            writer.row().set(0, static_cast<int32_t>(i));
            writer.row().set(1, 0.0f);
            writer.row().set(2, std::string("d"));
            writer.writeRow();
        }
        // No explicit close — destructor handles it
    }

    // File should be readable
    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));

    size_t count = 0;
    while (reader.readNext()) {
        ++count;
    }
    EXPECT_EQ(count, N);
    reader.close();
}

// ============================================================================
// Writer: overwrite flag
// ============================================================================
TEST_F(WriterReaderTest, Writer_OverwriteTrue_ReplacesFile) {
    auto path = testFile("overwrite.bcsv");

    // Write first file
    writeTestFile(path, 5);
    auto size1 = fs::file_size(path);
    EXPECT_GT(size1, 0u);

    // Overwrite with different data
    writeTestFile(path, 20);
    auto size2 = fs::file_size(path);
    EXPECT_GT(size2, size1);

    // Read back and verify it has 20 rows
    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));
    size_t count = 0;
    while (reader.readNext()) ++count;
    EXPECT_EQ(count, 20u);
    reader.close();
}

// ============================================================================
// Writer: empty file (open + close, no rows written)
// ============================================================================
TEST_F(WriterReaderTest, Writer_EmptyFile_IsReadable) {
    auto path = testFile("empty.bcsv");

    Layout wLayout = makeLayout();
    Writer<Layout> writer(wLayout);
    ASSERT_TRUE(writer.open(path, true));
    writer.close();

    // Empty file should be openable and readNext returns false immediately
    Reader<Layout> reader;
    ASSERT_TRUE(reader.open(path));
    EXPECT_FALSE(reader.readNext());
    reader.close();
}
