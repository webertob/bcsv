/**
 * @file review_fixes_test.cpp
 * @brief Tests verifying fixes from the critical review (Phases 1-5)
 *
 * Covers:
 * - ZoH bool-only transition round-trip (#1 non_bool_mask_)
 * - Writer::write(const RowType&) convenience method (#23)
 * - Layout operator== compares names (#18)
 * - Reader re-open guard (#22)
 * - Vectorized set bounds checking (#7)
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kTmpBase = std::filesystem::temp_directory_path() / "bcsv_review_tests";

class ReviewFixesTest : public ::testing::Test {
protected:
    std::filesystem::path kTmpDir;
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        kTmpDir = kTmpBase / (std::string(info->test_suite_name())
                  + "_" + info->name());
        std::filesystem::create_directories(kTmpDir);
    }
    void TearDown() override {
        std::filesystem::remove_all(kTmpDir);
    }
};

// -------------------------------------------------------------------
// #1  ZoH bool-only transition: rows that differ only in bool columns
//     must still round-trip correctly (non_bool_mask_ fix)
// -------------------------------------------------------------------
TEST_F(ReviewFixesTest, ZoH_BoolOnlyTransition_RoundTrip) {
    auto path = kTmpDir / "zoh_bool_only.bcsv";

    bcsv::Layout layout;
    layout.addColumn({"flag1", bcsv::ColumnType::BOOL});
    layout.addColumn({"flag2", bcsv::ColumnType::BOOL});
    layout.addColumn({"value", bcsv::ColumnType::INT32});

    // Write: rows 0-2 share the same INT32 but toggle bools
    {
        bcsv::WriterZoH<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD));

        // Row 0: false, true, 42
        writer.row().set(0, false);
        writer.row().set(1, true);
        writer.row().set(2, int32_t{42});
        writer.writeRow();

        // Row 1: true, false, 42  (only bools changed)
        writer.row().set(0, true);
        writer.row().set(1, false);
        writer.row().set(2, int32_t{42});
        writer.writeRow();

        // Row 2: true, true, 42   (one bool changed)
        writer.row().set(0, true);
        writer.row().set(1, true);
        writer.row().set(2, int32_t{42});
        writer.writeRow();

        // Row 3: true, true, 99   (scalar changed, bools same)
        writer.row().set(0, true);
        writer.row().set(1, true);
        writer.row().set(2, int32_t{99});
        writer.writeRow();

        writer.close();
    }

    // Read back and verify
    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), false);
        EXPECT_EQ(reader.row().get<bool>(1), true);
        EXPECT_EQ(reader.row().get<int32_t>(2), 42);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), true);
        EXPECT_EQ(reader.row().get<bool>(1), false);
        EXPECT_EQ(reader.row().get<int32_t>(2), 42);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), true);
        EXPECT_EQ(reader.row().get<bool>(1), true);
        EXPECT_EQ(reader.row().get<int32_t>(2), 42);

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<bool>(0), true);
        EXPECT_EQ(reader.row().get<bool>(1), true);
        EXPECT_EQ(reader.row().get<int32_t>(2), 99);

        EXPECT_FALSE(reader.readNext()); // EOF
        reader.close();
    }
}

// -------------------------------------------------------------------
// #1  ZoH all-bool layout: every column is bool, transitions must
//     not be treated as ZoH repeats
// -------------------------------------------------------------------
TEST_F(ReviewFixesTest, ZoH_AllBoolLayout_RoundTrip) {
    auto path = kTmpDir / "zoh_all_bool.bcsv";

    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::BOOL});
    layout.addColumn({"b", bcsv::ColumnType::BOOL});
    layout.addColumn({"c", bcsv::ColumnType::BOOL});

    constexpr size_t N = 8; // all 2^3 combinations
    {
        bcsv::WriterZoH<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD));

        for (size_t i = 0; i < N; ++i) {
            writer.row().set(0, static_cast<bool>(i & 1));
            writer.row().set(1, static_cast<bool>(i & 2));
            writer.row().set(2, static_cast<bool>(i & 4));
            writer.writeRow();
        }
        writer.close();
    }

    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));

        for (size_t i = 0; i < N; ++i) {
            ASSERT_TRUE(reader.readNext()) << "Expected row " << i;
            EXPECT_EQ(reader.row().get<bool>(0), static_cast<bool>(i & 1)) << "row " << i;
            EXPECT_EQ(reader.row().get<bool>(1), static_cast<bool>(i & 2)) << "row " << i;
            EXPECT_EQ(reader.row().get<bool>(2), static_cast<bool>(i & 4)) << "row " << i;
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// -------------------------------------------------------------------
// #23  Writer::write(const RowType&) convenience method
// -------------------------------------------------------------------
TEST_F(ReviewFixesTest, WriterWrite_CopiesAndWrites) {
    auto path = kTmpDir / "writer_write.bcsv";

    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});
    layout.addColumn({"s", bcsv::ColumnType::STRING});

    // Build an external row and use write()
    bcsv::Row external(layout);
    external.set(0, int32_t{77});
    external.set(1, std::string("hello"));

    {
        bcsv::Writer<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.write(external);

        // Modify external and write again
        external.set(0, int32_t{88});
        external.set(1, std::string("world"));
        writer.write(external);

        writer.close();
    }

    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 77);
        EXPECT_EQ(reader.row().get<std::string>(1), "hello");

        ASSERT_TRUE(reader.readNext());
        EXPECT_EQ(reader.row().get<int32_t>(0), 88);
        EXPECT_EQ(reader.row().get<std::string>(1), "world");

        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// -------------------------------------------------------------------
// #23  Writer::write() with ZoH writer
// -------------------------------------------------------------------
TEST_F(ReviewFixesTest, WriterWrite_ZoH) {
    auto path = kTmpDir / "writer_write_zoh.bcsv";

    bcsv::Layout layout;
    layout.addColumn({"val", bcsv::ColumnType::DOUBLE});

    bcsv::Row external(layout);

    {
        bcsv::WriterZoH<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD));

        for (int i = 0; i < 5; ++i) {
            external.set(0, static_cast<double>(i));
            writer.write(external);
        }
        writer.close();
    }

    {
        bcsv::Reader<bcsv::Layout> reader;
        ASSERT_TRUE(reader.open(path));

        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(reader.readNext());
            EXPECT_DOUBLE_EQ(reader.row().get<double>(0), static_cast<double>(i));
        }
        EXPECT_FALSE(reader.readNext());
        reader.close();
    }
}

// -------------------------------------------------------------------
// #18  Layout operator== now compares column names, not just types
// -------------------------------------------------------------------
TEST(LayoutEqualityTest, DifferentNamesAreNotEqual) {
    bcsv::Layout a;
    a.addColumn({"x", bcsv::ColumnType::INT32});
    a.addColumn({"y", bcsv::ColumnType::DOUBLE});

    bcsv::Layout b;
    b.addColumn({"a", bcsv::ColumnType::INT32});
    b.addColumn({"b", bcsv::ColumnType::DOUBLE});

    // Same types, different names → not equal
    EXPECT_TRUE(a.isCompatible(b));   // isCompatible checks types only
    EXPECT_NE(a, b);                   // operator== checks types AND names
}

TEST(LayoutEqualityTest, SameNamesAndTypesAreEqual) {
    bcsv::Layout a;
    a.addColumn({"x", bcsv::ColumnType::INT32});
    a.addColumn({"y", bcsv::ColumnType::DOUBLE});

    bcsv::Layout b;
    b.addColumn({"x", bcsv::ColumnType::INT32});
    b.addColumn({"y", bcsv::ColumnType::DOUBLE});

    EXPECT_EQ(a, b);
}

// -------------------------------------------------------------------
// #22  Reader::open() rejects re-open without close
// -------------------------------------------------------------------
TEST_F(ReviewFixesTest, ReaderRejectsReOpenWithoutClose) {
    auto path = kTmpDir / "reopen_test.bcsv";

    bcsv::Layout layout;
    layout.addColumn({"v", bcsv::ColumnType::INT32});

    // Create a valid file first
    {
        bcsv::Writer<bcsv::Layout> writer(layout);
        ASSERT_TRUE(writer.open(path, true));
        writer.row().set(0, int32_t{1});
        writer.writeRow();
        writer.close();
    }

    bcsv::Reader<bcsv::Layout> reader;
    ASSERT_TRUE(reader.open(path));

    // Second open without close should fail
    EXPECT_FALSE(reader.open(path));

    reader.close();

    // After close, re-open should succeed
    EXPECT_TRUE(reader.open(path));
    reader.close();
}

// -------------------------------------------------------------------
// #7  Vectorized set bounds checking
// -------------------------------------------------------------------
TEST(VectorizedBoundsTest, SetThrowsOnOverflow) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::INT32});
    bcsv::Row row(layout);

    // 3 values starting at column 0, but only 2 columns exist
    std::vector<int32_t> vals = {1, 2, 3};
    EXPECT_THROW(row.set<int32_t>(0, std::span<const int32_t>(vals)), std::out_of_range);
}

} // anonymous namespace
