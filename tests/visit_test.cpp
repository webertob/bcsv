/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <gtest/gtest.h>
#include "bcsv/bcsv.h"
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <cstring>

using namespace bcsv;

// Helper: serialize a Row via codec (replaces removed Row::serializeTo)
template<typename LayoutT, TrackingPolicy P = TrackingPolicy::Disabled>
std::span<std::byte> codecSerialize(const typename LayoutT::template RowType<P>& row,
                                     ByteBuffer& buffer, const LayoutT& layout) {
    RowCodecFlat001<LayoutT, P> codec;
    codec.setup(layout);
    return codec.serialize(row, buffer);
}

// Helper: serialize a Row via ZoH codec (replaces removed Row::serializeToZoH)
template<typename LayoutT>
std::span<std::byte> codecSerializeZoH(const typename LayoutT::template RowType<TrackingPolicy::Enabled>& row,
                                        ByteBuffer& buffer, const LayoutT& layout) {
    RowCodecZoH001<LayoutT, TrackingPolicy::Enabled> codec;
    codec.setup(layout);
    return codec.serialize(row, buffer);
}

// ============================================================================
// Test: Row::visit() - Dynamic Layout
// ============================================================================

TEST(VisitTest, RowBasicIteration) {
    Layout layout({
        {"name", ColumnType::STRING},
        {"age", ColumnType::INT32},
        {"salary", ColumnType::DOUBLE},
        {"active", ColumnType::BOOL}
    });
    
    Row row(layout);
    row.set(0, std::string("Alice"));
    row.set(1, int32_t(30));
    row.set(2, 75000.0);
    row.set(3, true);
    
    // Visit and collect values
    std::vector<std::string> visited;
    row.visitConst([&](size_t index, const auto& value) {
        std::ostringstream oss;
        oss << "col[" << index << "]=";
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) {
            oss << value;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, bool>) {
            oss << (value ? "true" : "false");
        } else {
            oss << value;
        }
        visited.push_back(oss.str());
    });
    
    ASSERT_EQ(visited.size(), 4);
    EXPECT_EQ(visited[0], "col[0]=Alice");
    EXPECT_EQ(visited[1], "col[1]=30");
    EXPECT_EQ(visited[2], "col[2]=75000");
    EXPECT_EQ(visited[3], "col[3]=true");
}

TEST(VisitTest, RowStatisticsAggregation) {
    Layout layout({
        {"value1", ColumnType::DOUBLE},
        {"value2", ColumnType::INT32},
        {"value3", ColumnType::FLOAT},
        {"name", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, 10.5);
    row.set(1, int32_t(20));
    row.set(2, 30.5f);
    row.set(3, std::string("test"));
    
    // Compute sum of numeric values
    double sum = 0.0;
    size_t numericCount = 0;
    
    row.visitConst([&](size_t, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            sum += static_cast<double>(value);
            numericCount++;
        }
    });
    
    EXPECT_EQ(numericCount, 3);
    EXPECT_NEAR(sum, 61.0, 0.01);
}

// ============================================================================
// Test: RowStatic::visit() - Static Layout
// ============================================================================

TEST(VisitTest, RowStaticBasicIteration) {
    using LayoutType = LayoutStatic<std::string, int32_t, double, bool>;
    LayoutType layout({"name", "age", "salary", "active"});
    
    RowStatic<std::string, int32_t, double, bool> row(layout);
    row.set<0>(std::string("Bob"));
    row.set<1>(int32_t(25));
    row.set<2>(65000.0);
    row.set<3>(false);
    
    // Visit and verify
    std::vector<std::string> visited;
    row.visitConst([&](auto index, const auto& value) {
        std::ostringstream oss;
        oss << "col[" << index << "]=";
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) {
            oss << value;
        } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, bool>) {
            oss << (value ? "true" : "false");
        } else {
            oss << value;
        }
        visited.push_back(oss.str());
    });
    
    ASSERT_EQ(visited.size(), 4);
    EXPECT_EQ(visited[0], "col[0]=Bob");
    EXPECT_EQ(visited[1], "col[1]=25");
    EXPECT_EQ(visited[2], "col[2]=65000");
    EXPECT_EQ(visited[3], "col[3]=false");
}

TEST(VisitTest, RowStaticCompileTimeIndex) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t>;
    LayoutType layout({"a", "b", "c"});
    
    RowStatic<int32_t, int32_t, int32_t> row(layout);
    row.set<0>(10);
    row.set<1>(20);
    row.set<2>(30);
    
    // Verify compile-time index is available
    std::vector<size_t> indices;
    std::vector<int32_t> values;
    
    row.visitConst([&](auto index, const auto& value) {
        // index is a compile-time constant here
        indices.push_back(index);
        values.push_back(value);
    });
    
    ASSERT_EQ(indices.size(), 3);
    EXPECT_EQ(indices[0], 0);
    EXPECT_EQ(indices[1], 1);
    EXPECT_EQ(indices[2], 2);
    
    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 20);
    EXPECT_EQ(values[2], 30);
}

// ============================================================================
// Test: Performance / Use Case Examples
// ============================================================================

TEST(VisitTest, CSVOutputExample) {
    Layout layout({
        {"name", ColumnType::STRING},
        {"age", ColumnType::INT32},
        {"salary", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, std::string("Charlie"));
    row.set(1, int32_t(35));
    row.set(2, 85000.0);
    
    // Generate CSV output using visit
    std::ostringstream csv;
    bool first = true;
    
    row.visitConst([&](size_t, const auto& value) {
        if (!first) csv << ",";
        first = false;
        
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) {
            csv << "\"" << value << "\"";
        } else {
            csv << value;
        }
    });
    
    EXPECT_EQ(csv.str(), "\"Charlie\",35,85000");
}

TEST(VisitTest, HashComputationExample) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t>;
    LayoutType layout({"a", "b", "c"});
    
    RowStatic<int32_t, int32_t, int32_t> row(layout);
    row.set<0>(10);
    row.set<1>(20);
    row.set<2>(30);
    
    // Compute simple hash
    size_t hash = 0;
    row.visitConst([&](auto index, const auto& value) {
        hash ^= std::hash<int32_t>{}(value) << index;
    });
    
    // Just verify hash is non-zero
    EXPECT_NE(hash, 0);
}

// ============================================================================
// Test: Edge Cases
// ============================================================================

TEST(VisitTest, EmptyLayout) {
    std::vector<ColumnDefinition> columns;
    Layout layout(columns);
    Row row(layout);
    
    size_t visitCount = 0;
    row.visitConst([&](size_t, const auto&) {
        visitCount++;
    });
    
    EXPECT_EQ(visitCount, 0);
}

TEST(VisitTest, SingleColumn) {
    Layout layout({{"value", ColumnType::INT32}});
    Row row(layout);
    row.set(0, int32_t(42));
    
    int32_t value = 0;
    row.visitConst([&](size_t index, const auto& v) {
        EXPECT_EQ(index, 0);
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int32_t>) {
            value = v;
        }
    });
    
    EXPECT_EQ(value, 42);
}

TEST(VisitTest, AllPrimitiveTypes) {
    Layout layout({
        {"bool", ColumnType::BOOL},
        {"int8", ColumnType::INT8},
        {"int16", ColumnType::INT16},
        {"int32", ColumnType::INT32},
        {"int64", ColumnType::INT64},
        {"uint8", ColumnType::UINT8},
        {"uint16", ColumnType::UINT16},
        {"uint32", ColumnType::UINT32},
        {"uint64", ColumnType::UINT64},
        {"float", ColumnType::FLOAT},
        {"double", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, true);
    row.set(1, int8_t(1));
    row.set(2, int16_t(2));
    row.set(3, int32_t(3));
    row.set(4, int64_t(4));
    row.set(5, uint8_t(5));
    row.set(6, uint16_t(6));
    row.set(7, uint32_t(7));
    row.set(8, uint64_t(8));
    row.set(9, 9.0f);
    row.set(10, 10.0);
    
    size_t visitCount = 0;
    row.visitConst([&](size_t, const auto&) {
        visitCount++;
    });
    
    EXPECT_EQ(visitCount, 11);
}

// ============================================================================
// Test: Mutable visit() - Non-const Overload
// ============================================================================

TEST(VisitTest, RowMutableVisit) {
    Layout layout({
        {"value1", ColumnType::INT32},
        {"value2", ColumnType::DOUBLE},
        {"value3", ColumnType::FLOAT}
    });
    
    Row row(layout);
    row.set(0, int32_t(10));
    row.set(1, 20.0);
    row.set(2, 30.0f);
    
    // Mutable visit: multiply all values by 2
    row.visit([&](size_t, auto& value, bool&) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value *= 2;
        }
    });
    
    // Verify values were modified
    EXPECT_EQ(row.get<int32_t>(0), 20);
    EXPECT_NEAR(row.get<double>(1), 40.0, 0.01);
    EXPECT_NEAR(row.get<float>(2), 60.0f, 0.01f);
}

TEST(VisitTest, RowMutableVisitStrings) {
    Layout layout({
        {"name", ColumnType::STRING},
        {"count", ColumnType::INT32}
    });
    
    Row row(layout);
    row.set(0, std::string("Alice"));
    row.set(1, int32_t(10));
    
    // Mutable visit: can modify strings
    row.visit([&](size_t, auto& value, bool&) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) {
            value += " Smith";
        } else if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value *= 2;
        }
    });
    
    EXPECT_EQ(row.get<std::string>(0), "Alice Smith");
    EXPECT_EQ(row.get<int32_t>(1), 20);
}

TEST(VisitTest, RowStaticMutableVisit) {
    using LayoutType = LayoutStatic<int32_t, double, float>;
    LayoutType layout({"a", "b", "c"});
    
    RowStatic<int32_t, double, float> row(layout);
    row.set<0>(5);
    row.set<1>(10.0);
    row.set<2>(15.0f);
    
    // Mutable visit with compile-time indices
    row.visit([&](auto, auto& value, bool&) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value *= 3;
        }
    });
    
    EXPECT_EQ(row.get<0>(), 15);
    EXPECT_NEAR(row.get<1>(), 30.0, 0.01);
    EXPECT_NEAR(row.get<2>(), 45.0f, 0.01f);
}

TEST(VisitTest, MutableVisitNormalization) {
    Layout layout({
        {"x", ColumnType::DOUBLE},
        {"y", ColumnType::DOUBLE},
        {"z", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, 3.0);
    row.set(1, 4.0);
    row.set(2, 0.0);
    
    // Calculate magnitude (sqrt(x^2 + y^2 + z^2))
    double magnitude = 0.0;
    row.visitConst([&](size_t, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            magnitude += value * value;
        }
    });
    magnitude = std::sqrt(magnitude);
    
    // Normalize vector using mutable visit
    row.visit([&](size_t, auto& value, bool& change) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value /= magnitude;
        } else {
            change = false;
        }
    });
    
    // Verify normalized (magnitude should be 1.0)
    double normalized_mag = 0.0;
    row.visitConst([&](size_t, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            normalized_mag += value * value;
        }
    });
    normalized_mag = std::sqrt(normalized_mag);
    
    EXPECT_NEAR(normalized_mag, 1.0, 0.0001);
}

// ============================================================================
// Test: visit<T>() — Type-Safe Typed Visit (Compile-Time Dispatch)
// ============================================================================

TEST(VisitTest, TypedVisitScalarReadWrite) {
    Layout layout({
        {"a", ColumnType::DOUBLE},
        {"b", ColumnType::DOUBLE},
        {"c", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, 1.0);
    row.set(1, 2.0);
    row.set(2, 3.0);
    
    // Typed mutable visit: multiply all doubles by 10
    row.visit<double>(0, [](size_t, double& val, bool& changed) {
        val *= 10.0;
        changed = true;
    }, 3);
    
    EXPECT_NEAR(row.get<double>(0), 10.0, 0.01);
    EXPECT_NEAR(row.get<double>(1), 20.0, 0.01);
    EXPECT_NEAR(row.get<double>(2), 30.0, 0.01);
}

TEST(VisitTest, TypedVisitConstReadOnly) {
    Layout layout({
        {"x", ColumnType::INT32},
        {"y", ColumnType::INT32},
        {"z", ColumnType::INT32}
    });
    
    Row row(layout);
    row.set(0, int32_t(10));
    row.set(1, int32_t(20));
    row.set(2, int32_t(30));
    
    // Typed const visit: sum all values
    int32_t sum = 0;
    row.visitConst<int32_t>(0, [&](size_t, const int32_t& val) {
        sum += val;
    }, 3);
    
    EXPECT_EQ(sum, 60);
}

TEST(VisitTest, TypedVisitSingleColumn) {
    Layout layout({
        {"id", ColumnType::UINT32},
        {"value", ColumnType::DOUBLE},
        {"name", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, uint32_t(42));
    row.set(1, 3.14);
    row.set(2, std::string("hello"));
    
    // Visit single column (default count=1)
    double result = 0;
    row.visitConst<double>(1, [&](size_t, const double& val) {
        result = val;
    });
    EXPECT_NEAR(result, 3.14, 0.001);
    
    // Visit single string column
    std::string strResult;
    row.visitConst<std::string>(2, [&](size_t, const std::string& val) {
        strResult = val;
    });
    EXPECT_EQ(strResult, "hello");
    
    // Mutable visit: modify the double
    row.visit<double>(1, [](size_t, double& val, bool& changed) {
        val = 2.718;
        changed = true;
    });
    EXPECT_NEAR(row.get<double>(1), 2.718, 0.001);
}

TEST(VisitTest, TypedVisitBool) {
    Layout layout({
        {"flag1", ColumnType::BOOL},
        {"flag2", ColumnType::BOOL},
        {"flag3", ColumnType::BOOL}
    });
    
    Row row(layout);
    row.set(0, true);
    row.set(1, false);
    row.set(2, true);
    
    // Const visit: count true values
    int trueCount = 0;
    row.visitConst<bool>(0, [&](size_t, const bool& val) {
        if (val) trueCount++;
    }, 3);
    EXPECT_EQ(trueCount, 2);
    
    // Mutable visit: flip all bools
    row.visit<bool>(0, [](size_t, bool& val, bool&) {
        val = !val;
    }, 3);
    
    EXPECT_FALSE(row.get<bool>(0));
    EXPECT_TRUE(row.get<bool>(1));
    EXPECT_FALSE(row.get<bool>(2));
}

TEST(VisitTest, TypedVisitString) {
    Layout layout({
        {"s1", ColumnType::STRING},
        {"s2", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, std::string("hello"));
    row.set(1, std::string("world"));
    
    // Mutable visit: append suffix
    row.visit<std::string>(0, [](size_t, std::string& val, bool& changed) {
        val += "!";
        changed = true;
    }, 2);
    
    EXPECT_EQ(row.get<std::string>(0), "hello!");
    EXPECT_EQ(row.get<std::string>(1), "world!");
}

TEST(VisitTest, TypedVisitTypeMismatchThrows) {
    Layout layout({
        {"value", ColumnType::INT32},
        {"name", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, int32_t(42));
    row.set(1, std::string("test"));
    
    // Visiting INT32 column as DOUBLE should throw
    EXPECT_THROW(
        row.visit<double>(0, [](size_t, double&, bool&) {}, 1),
        std::runtime_error
    );
    
    // Visiting 2 columns where types differ should throw
    EXPECT_THROW(
        row.visit<int32_t>(0, [](size_t, int32_t&, bool&) {}, 2),
        std::runtime_error
    );
    
    // visitConst type mismatch
    EXPECT_THROW(
        row.visitConst<double>(0, [](size_t, const double&) {}, 1),
        std::runtime_error
    );
}

TEST(VisitTest, TypedVisitRangeOutOfBoundsThrows) {
    Layout layout({
        {"a", ColumnType::INT32},
        {"b", ColumnType::INT32}
    });
    
    Row row(layout);
    
    // Range exceeds column count
    EXPECT_THROW(
        row.visit<int32_t>(0, [](size_t, int32_t&, bool&) {}, 5),
        std::out_of_range
    );
    
    EXPECT_THROW(
        row.visitConst<int32_t>(3, [](size_t, const int32_t&) {}, 1),
        std::out_of_range
    );
}

TEST(VisitTest, TypedVisitZeroCount) {
    Layout layout({{"a", ColumnType::INT32}});
    Row row(layout);
    
    int callCount = 0;
    // count=0 should be a no-op
    row.visit<int32_t>(0, [&](size_t, int32_t&, bool&) { callCount++; }, 0);
    EXPECT_EQ(callCount, 0);
    
    row.visitConst<int32_t>(0, [&](size_t, const int32_t&) { callCount++; }, 0);
    EXPECT_EQ(callCount, 0);
}

TEST(VisitTest, TypedVisitTwoParamVisitor) {
    Layout layout({
        {"x", ColumnType::DOUBLE},
        {"y", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, 1.0);
    row.set(1, 2.0);
    
    // 2-param visitor (no bool& changed) — marks all as changed when tracking
    row.visit<double>(0, [](size_t, double& val) {
        val *= 3.0;
    }, 2);
    
    EXPECT_NEAR(row.get<double>(0), 3.0, 0.01);
    EXPECT_NEAR(row.get<double>(1), 6.0, 0.01);
}

