/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 */

#include <gtest/gtest.h>
#include "bcsv/bcsv.h"

namespace {

using bcsv::ColumnType;
using bcsv::Layout;
using bcsv::Row;

TEST(RowRefMutationRefactorTest, RefMutatesPrimitiveAndBoolAndString) {
    Layout layout({
        {"b", ColumnType::BOOL},
        {"i", ColumnType::INT32},
        {"s", ColumnType::STRING}
    });

    Row row(layout);
    row.set<bool>(0, false);
    row.set<int32_t>(1, 1);
    row.set<std::string_view>(2, "x");

    auto b = row.ref<bool>(0);
    b = true;
    row.ref<int32_t>(1) = 99;
    row.ref<std::string>(2) = "updated";

    EXPECT_TRUE(row.get<bool>(0));
    EXPECT_EQ(row.get<int32_t>(1), 99);
    EXPECT_EQ(row.get<std::string>(2), "updated");
}

}  // namespace
