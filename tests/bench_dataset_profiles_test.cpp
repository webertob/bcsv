/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include "bench_datasets.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

size_t countColumnsOfType(const bcsv::Layout& layout, bcsv::ColumnType type) {
    size_t count = 0;
    for (size_t i = 0; i < layout.columnCount(); ++i) {
        if (layout.columnType(i) == type) {
            ++count;
        }
    }
    return count;
}

void assertStringColumnsArePopulatedByGenerators(const bench::DatasetProfile& profile) {
    bcsv::Row randomRow(profile.layout);
    profile.generate(randomRow, 17);

    bcsv::RowImpl<bcsv::TrackingPolicy::Enabled> zohRow(profile.layout);
    profile.generateZoH(zohRow, 17);

    for (size_t i = 0; i < profile.layout.columnCount(); ++i) {
        if (profile.layout.columnType(i) != bcsv::ColumnType::STRING) {
            continue;
        }

        const std::string randomValue = randomRow.get<std::string>(i);
        const std::string zohValue = zohRow.get<std::string>(i);

        EXPECT_FALSE(randomValue.empty())
            << "Random generator produced empty string for column '" << profile.layout.columnName(i)
            << "' in profile '" << profile.name << "'";
        EXPECT_FALSE(zohValue.empty())
            << "ZoH generator produced empty string for column '" << profile.layout.columnName(i)
            << "' in profile '" << profile.name << "'";
    }
}

template <typename RowType>
bool columnValuesDiffer(const RowType& lhs, const RowType& rhs, const bcsv::Layout& layout, size_t column) {
    switch (layout.columnType(column)) {
        case bcsv::ColumnType::VOID: return false;
        case bcsv::ColumnType::BOOL: return lhs.template get<bool>(column) != rhs.template get<bool>(column);
        case bcsv::ColumnType::INT8: return lhs.template get<int8_t>(column) != rhs.template get<int8_t>(column);
        case bcsv::ColumnType::INT16: return lhs.template get<int16_t>(column) != rhs.template get<int16_t>(column);
        case bcsv::ColumnType::INT32: return lhs.template get<int32_t>(column) != rhs.template get<int32_t>(column);
        case bcsv::ColumnType::INT64: return lhs.template get<int64_t>(column) != rhs.template get<int64_t>(column);
        case bcsv::ColumnType::UINT8: return lhs.template get<uint8_t>(column) != rhs.template get<uint8_t>(column);
        case bcsv::ColumnType::UINT16: return lhs.template get<uint16_t>(column) != rhs.template get<uint16_t>(column);
        case bcsv::ColumnType::UINT32: return lhs.template get<uint32_t>(column) != rhs.template get<uint32_t>(column);
        case bcsv::ColumnType::UINT64: return lhs.template get<uint64_t>(column) != rhs.template get<uint64_t>(column);
        case bcsv::ColumnType::FLOAT: return lhs.template get<float>(column) != rhs.template get<float>(column);
        case bcsv::ColumnType::DOUBLE: return lhs.template get<double>(column) != rhs.template get<double>(column);
        case bcsv::ColumnType::STRING: return lhs.template get<std::string>(column) != rhs.template get<std::string>(column);
    }
    return false;
}

template <typename RowType>
void expectColumnsEqual(const RowType& lhs, const RowType& rhs, const bcsv::Layout& layout, const std::vector<size_t>& columns) {
    for (size_t column : columns) {
        EXPECT_FALSE(columnValuesDiffer(lhs, rhs, layout, column))
            << "Expected deterministic value at column '" << layout.columnName(column)
            << "' (index " << column << ")";
    }
}

template <typename RowType, typename Generator>
void assertGeneratorDeterminism(
    const bench::DatasetProfile& profile,
    Generator generator,
    const std::vector<size_t>& representativeColumns
) {
    RowType a(profile.layout);
    RowType b(profile.layout);
    RowType c(profile.layout);

    generator(a, 17);
    generator(b, 17);
    generator(c, 18);

    expectColumnsEqual(a, b, profile.layout, representativeColumns);

    bool differsForNextRow = false;
    for (size_t column : representativeColumns) {
        if (columnValuesDiffer(a, c, profile.layout, column)) {
            differsForNextRow = true;
            break;
        }
    }
    EXPECT_TRUE(differsForNextRow)
        << "Expected at least one representative column to change between adjacent rows";
}

} // namespace

TEST(BenchmarkProfiles, RegistersNewStringHeavyWorkloads) {
    const std::vector<std::string> names = bench::getProfileNames();

    EXPECT_NE(std::find(names.begin(), names.end(), "event_log"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "iot_fleet"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "financial_orders"), names.end());
}

TEST(BenchmarkProfiles, EventLogSchemaMatchesProposalShape) {
    const auto profile = bench::getProfile("event_log");
    EXPECT_EQ(profile.layout.columnCount(), 27u);
    EXPECT_EQ(countColumnsOfType(profile.layout, bcsv::ColumnType::STRING), 8u);
    EXPECT_EQ(profile.default_rows, 500000u);
    assertStringColumnsArePopulatedByGenerators(profile);

    assertGeneratorDeterminism<bcsv::Row>(
        profile,
        [&profile](bcsv::Row& row, size_t index) { profile.generate(row, index); },
        {0u, 1u, 2u, 10u, 13u, 23u}
    );
    assertGeneratorDeterminism<bcsv::RowImpl<bcsv::TrackingPolicy::Enabled>>(
        profile,
        [&profile](bcsv::RowImpl<bcsv::TrackingPolicy::Enabled>& row, size_t index) { profile.generateZoH(row, index); },
        {0u, 1u, 2u, 10u, 13u, 23u}
    );
}

TEST(BenchmarkProfiles, IotFleetSchemaMatchesProposalShape) {
    const auto profile = bench::getProfile("iot_fleet");
    EXPECT_EQ(profile.layout.columnCount(), 25u);
    EXPECT_EQ(countColumnsOfType(profile.layout, bcsv::ColumnType::STRING), 6u);
    EXPECT_EQ(profile.default_rows, 500000u);
    assertStringColumnsArePopulatedByGenerators(profile);

    assertGeneratorDeterminism<bcsv::Row>(
        profile,
        [&profile](bcsv::Row& row, size_t index) { profile.generate(row, index); },
        {0u, 1u, 2u, 4u, 8u, 11u, 15u}
    );
    assertGeneratorDeterminism<bcsv::RowImpl<bcsv::TrackingPolicy::Enabled>>(
        profile,
        [&profile](bcsv::RowImpl<bcsv::TrackingPolicy::Enabled>& row, size_t index) { profile.generateZoH(row, index); },
        {0u, 1u, 2u, 4u, 8u, 11u, 15u}
    );
}

TEST(BenchmarkProfiles, FinancialOrdersSchemaMatchesProposalShape) {
    const auto profile = bench::getProfile("financial_orders");
    EXPECT_EQ(profile.layout.columnCount(), 22u);
    EXPECT_EQ(countColumnsOfType(profile.layout, bcsv::ColumnType::STRING), 8u);
    EXPECT_EQ(profile.default_rows, 500000u);
    assertStringColumnsArePopulatedByGenerators(profile);

    assertGeneratorDeterminism<bcsv::Row>(
        profile,
        [&profile](bcsv::Row& row, size_t index) { profile.generate(row, index); },
        {0u, 1u, 2u, 5u, 10u, 11u, 18u}
    );
    assertGeneratorDeterminism<bcsv::RowImpl<bcsv::TrackingPolicy::Enabled>>(
        profile,
        [&profile](bcsv::RowImpl<bcsv::TrackingPolicy::Enabled>& row, size_t index) { profile.generateZoH(row, index); },
        {0u, 1u, 2u, 5u, 10u, 11u, 18u}
    );
}
