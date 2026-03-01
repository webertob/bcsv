/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file stream_operator_test.cpp
 * @brief Tests for operator<< on Layout, LayoutStatic, Row, and RowStatic.
 */

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include "bcsv/bcsv.h"
#include "bcsv/bcsv.hpp"

using namespace bcsv;

// ════════════════════════════════════════════════════════════════════════════
// Layout operator<< tests
// ════════════════════════════════════════════════════════════════════════════

TEST(LayoutStreamTest, EmptyLayout) {
    Layout layout;
    std::ostringstream os;
    os << layout;
    EXPECT_EQ(os.str(), "Empty layout (no columns)");
}

TEST(LayoutStreamTest, SingleColumn) {
    Layout layout({{"x", ColumnType::INT32}});
    std::ostringstream os;
    os << layout;
    const std::string result = os.str();
    EXPECT_NE(result.find("Col"), std::string::npos);
    EXPECT_NE(result.find("Name"), std::string::npos);
    EXPECT_NE(result.find("Type"), std::string::npos);
    EXPECT_NE(result.find("x"), std::string::npos);
    EXPECT_NE(result.find("int32"), std::string::npos);
    EXPECT_NE(result.find("0"), std::string::npos);
}

TEST(LayoutStreamTest, MultiColumn_MixedTypes) {
    Layout layout({
        {"time",   ColumnType::DOUBLE},
        {"sensor", ColumnType::INT32},
        {"active", ColumnType::BOOL},
        {"label",  ColumnType::STRING}
    });
    std::ostringstream os;
    os << layout;
    const std::string result = os.str();

    EXPECT_NE(result.find("time"), std::string::npos);
    EXPECT_NE(result.find("double"), std::string::npos);
    EXPECT_NE(result.find("sensor"), std::string::npos);
    EXPECT_NE(result.find("int32"), std::string::npos);
    EXPECT_NE(result.find("active"), std::string::npos);
    EXPECT_NE(result.find("bool"), std::string::npos);
    EXPECT_NE(result.find("label"), std::string::npos);
    EXPECT_NE(result.find("string"), std::string::npos);

    // Column indices present
    EXPECT_NE(result.find("0"), std::string::npos);
    EXPECT_NE(result.find("3"), std::string::npos);
}

TEST(LayoutStreamTest, LongColumnNames) {
    Layout layout({
        {"a_very_long_column_name_for_testing", ColumnType::FLOAT},
        {"x", ColumnType::UINT8}
    });
    std::ostringstream os;
    os << layout;
    const std::string result = os.str();
    EXPECT_NE(result.find("a_very_long_column_name_for_testing"), std::string::npos);
    EXPECT_NE(result.find("float"), std::string::npos);
}

TEST(LayoutStreamTest, LayoutStatic_MatchesDynamic) {
    using SLayout = LayoutStatic<int32_t, double, std::string>;
    SLayout slayout;
    slayout.setColumnName(0, "a");
    slayout.setColumnName(1, "b");
    slayout.setColumnName(2, "c");

    Layout dlayout({
        {"a", ColumnType::INT32},
        {"b", ColumnType::DOUBLE},
        {"c", ColumnType::STRING}
    });

    std::ostringstream oss, osd;
    oss << slayout;
    osd << dlayout;
    EXPECT_EQ(oss.str(), osd.str());
}


// ════════════════════════════════════════════════════════════════════════════
// Row operator<< tests
// ════════════════════════════════════════════════════════════════════════════

TEST(RowStreamTest, EmptyRow) {
    Layout layout;
    Row row(layout);
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "");
}

TEST(RowStreamTest, SingleBool_True) {
    Layout layout({{"flag", ColumnType::BOOL}});
    Row row(layout);
    row.set<bool>(0, true);
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "true");
}

TEST(RowStreamTest, SingleBool_False) {
    Layout layout({{"flag", ColumnType::BOOL}});
    Row row(layout);
    row.set<bool>(0, false);
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "false");
}

TEST(RowStreamTest, SingleString) {
    Layout layout({{"name", ColumnType::STRING}});
    Row row(layout);
    row.set<std::string>(0, "hello");
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "\"hello\"");
}

TEST(RowStreamTest, MixedTypes) {
    Layout layout({
        {"i",  ColumnType::INT32},
        {"d",  ColumnType::DOUBLE},
        {"b",  ColumnType::BOOL},
        {"s",  ColumnType::STRING}
    });
    Row row(layout);
    row.set<int32_t>(0, 42);
    row.set<double>(1, 3.14);
    row.set<bool>(2, true);
    row.set<std::string>(3, "hello world");

    std::ostringstream os;
    os << row;
    const std::string result = os.str();
    EXPECT_EQ(result, "42, 3.14, true, \"hello world\"");
}

TEST(RowStreamTest, StringWithEmbeddedComma) {
    Layout layout({{"s", ColumnType::STRING}});
    Row row(layout);
    row.set<std::string>(0, "hello, world");
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "\"hello, world\"");
}

TEST(RowStreamTest, StringWithEmbeddedQuotes) {
    Layout layout({{"s", ColumnType::STRING}});
    Row row(layout);
    row.set<std::string>(0, "say \"hi\"");
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "\"say \"\"hi\"\"\"");
}

TEST(RowStreamTest, DefaultValues) {
    Layout layout({
        {"i", ColumnType::INT32},
        {"d", ColumnType::DOUBLE},
        {"b", ColumnType::BOOL},
        {"s", ColumnType::STRING}
    });
    Row row(layout);
    // Default values: 0, 0, false, ""
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "0, 0, false, \"\"");
}

TEST(RowStreamTest, AllIntegerTypes) {
    Layout layout({
        {"u8",  ColumnType::UINT8},
        {"u16", ColumnType::UINT16},
        {"u32", ColumnType::UINT32},
        {"u64", ColumnType::UINT64},
        {"i8",  ColumnType::INT8},
        {"i16", ColumnType::INT16},
        {"i32", ColumnType::INT32},
        {"i64", ColumnType::INT64}
    });
    Row row(layout);
    row.set<uint8_t>(0, 255);
    row.set<uint16_t>(1, 1000);
    row.set<uint32_t>(2, 100000);
    row.set<uint64_t>(3, 999999999ULL);
    row.set<int8_t>(4, -128);
    row.set<int16_t>(5, -1000);
    row.set<int32_t>(6, -100000);
    row.set<int64_t>(7, -999999999LL);

    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "255, 1000, 100000, 999999999, -128, -1000, -100000, -999999999");
}

TEST(RowStreamTest, NegativeFloat) {
    Layout layout({{"f", ColumnType::FLOAT}});
    Row row(layout);
    row.set<float>(0, -1.5f);
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "-1.5");
}

TEST(RowStreamTest, EmptyString) {
    Layout layout({{"s", ColumnType::STRING}});
    Row row(layout);
    row.set<std::string>(0, "");
    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "\"\"");
}

// ════════════════════════════════════════════════════════════════════════════
// RowStatic operator<< tests
// ════════════════════════════════════════════════════════════════════════════

TEST(RowStaticStreamTest, MixedTypes) {
    using SLayout = LayoutStatic<int32_t, double, bool, std::string>;
    SLayout layout;
    typename SLayout::RowType row(layout);

    row.template set<0>(42);
    row.template set<1>(3.14);
    row.template set<2>(true);
    row.template set<3>(std::string("hello world"));

    std::ostringstream os;
    os << row;
    EXPECT_EQ(os.str(), "42, 3.14, true, \"hello world\"");
}

TEST(RowStaticStreamTest, MatchesDynamicRow) {
    // RowStatic and Row with equivalent layouts must produce same output
    using SLayout = LayoutStatic<int32_t, bool, std::string>;
    SLayout slayout;
    slayout.setColumnName(0, "x");
    slayout.setColumnName(1, "b");
    slayout.setColumnName(2, "s");

    typename SLayout::RowType srow(slayout);
    srow.template set<0>(7);
    srow.template set<1>(false);
    srow.template set<2>(std::string("test"));

    Layout dlayout({
        {"x", ColumnType::INT32},
        {"b", ColumnType::BOOL},
        {"s", ColumnType::STRING}
    });
    Row drow(dlayout);
    drow.set<int32_t>(0, 7);
    drow.set<bool>(1, false);
    drow.set<std::string>(2, "test");

    std::ostringstream oss, osd;
    oss << srow;
    osd << drow;
    EXPECT_EQ(oss.str(), osd.str());
}

TEST(RowStreamTest, MultipleRowsToSameStream) {
    Layout layout({{"x", ColumnType::INT32}});
    Row row1(layout), row2(layout);
    row1.set<int32_t>(0, 1);
    row2.set<int32_t>(0, 2);

    std::ostringstream os;
    os << row1 << '\n' << row2;
    EXPECT_EQ(os.str(), "1\n2");
}

TEST(RowStreamTest, RoundTripConsistency) {
    Layout layout({
        {"a", ColumnType::UINT32},
        {"b", ColumnType::FLOAT},
        {"c", ColumnType::STRING}
    });
    Row row(layout);
    row.set<uint32_t>(0, 12345);
    row.set<float>(1, 2.5f);
    row.set<std::string>(2, "data");

    // Write twice, must produce identical output
    std::ostringstream os1, os2;
    os1 << row;
    os2 << row;
    EXPECT_EQ(os1.str(), os2.str());
    EXPECT_EQ(os1.str(), "12345, 2.5, \"data\"");
}
