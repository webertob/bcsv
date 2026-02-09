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

using namespace bcsv;

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
// Test: RowView::visit() - Buffer View
// ============================================================================

TEST(VisitTest, RowViewBasicIteration) {
    Layout layout({
        {"id", ColumnType::INT32},
        {"value", ColumnType::DOUBLE},
        {"flag", ColumnType::BOOL}
    });
    
    Row row(layout);
    row.set(0, int32_t(42));
    row.set(1, 3.14);
    row.set(2, true);
    
    // Serialize to buffer
    ByteBuffer buffer;
    auto serialized = row.serializeTo(buffer);
    
    // Create RowView
    RowView rowView(layout, std::span<std::byte>(serialized));
    
    // Visit and verify
    std::vector<std::string> visited;
    rowView.visitConst([&](size_t index, const auto& value) {
        std::ostringstream oss;
        oss << index << ":";
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, bool>) {
            oss << (value ? "true" : "false");
        } else {
            oss << value;
        }
        visited.push_back(oss.str());
    });
    
    ASSERT_EQ(visited.size(), 3);
    EXPECT_EQ(visited[0], "0:42");
    EXPECT_EQ(visited[1], "1:3.14");
    EXPECT_EQ(visited[2], "2:true");
}

TEST(VisitTest, RowViewStringHandling) {
    Layout layout({
        {"name", ColumnType::STRING},
        {"count", ColumnType::INT32},
        {"description", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, std::string("Alice"));
    row.set(1, int32_t(100));
    row.set(2, std::string("Description text"));
    
    ByteBuffer buffer;
    auto serialized = row.serializeTo(buffer);
    
    RowView rowView(layout, std::span<std::byte>(serialized));
    
    // Verify strings are accessed as string_view (zero-copy)
    std::vector<std::string> strings;
    rowView.visitConst([&](size_t, const auto& value) {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string_view>) {
            strings.push_back(std::string(value));
        }
    });
    
    ASSERT_EQ(strings.size(), 2);
    EXPECT_EQ(strings[0], "Alice");
    EXPECT_EQ(strings[1], "Description text");
}

// ============================================================================
// Test: RowViewStatic::visit() - Static Buffer View
// ============================================================================

TEST(VisitTest, RowViewStaticBasicIteration) {
    using LayoutType = LayoutStatic<int32_t, double, bool>;
    LayoutType layout({"id", "value", "flag"});
    
    RowStatic<int32_t, double, bool> row(layout);
    row.set<0>(100);
    row.set<1>(2.718);
    row.set<2>(false);
    
    ByteBuffer buffer;
    auto serialized = row.serializeTo(buffer);
    
    RowViewStatic<int32_t, double, bool> rowView(layout, std::span<std::byte>(serialized));
    
    // Visit and verify using runtime comparison
    std::vector<std::string> visited;
    rowView.visitConst([&](auto index, const auto& val) {
        std::ostringstream oss;
        oss << "col[" << index << "]=";
        if constexpr (std::is_same_v<std::decay_t<decltype(val)>, bool>) {
            oss << (val ? "true" : "false");
        } else {
            oss << val;
        }
        visited.push_back(oss.str());
    });
    
    ASSERT_EQ(visited.size(), 3);
    EXPECT_EQ(visited[0], "col[0]=100");
    EXPECT_EQ(visited[1], "col[1]=2.718");
    EXPECT_EQ(visited[2], "col[2]=false");
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

TEST(VisitTest, RowMutableVisitWithChangeTracking) {
    Layout layout({
        {"a", ColumnType::INT32},
        {"b", ColumnType::INT32},
        {"c", ColumnType::INT32}
    });
    
    Row row(layout, true);  // Enable change tracking
    row.set(0, 10);
    row.set(1, 20);
    row.set(2, 30);
    row.resetChanges();  // Clear changes
    
    EXPECT_FALSE(row.hasAnyChanges());
    
    // Mutable visit should mark all columns as changed
    row.visit([&](size_t, auto& value, bool&) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value += 5;
        }
    });
    
    EXPECT_TRUE(row.hasAnyChanges());
    EXPECT_EQ(row.get<int32_t>(0), 15);
    EXPECT_EQ(row.get<int32_t>(1), 25);
    EXPECT_EQ(row.get<int32_t>(2), 35);
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

TEST(VisitTest, RowStaticMutableVisitWithTracking) {
    using LayoutType = LayoutStatic<int32_t, int32_t>;
    LayoutType layout({"x", "y"});
    
    RowStatic<int32_t, int32_t> row(layout);
    row.trackChanges(true);
    row.set<0>(100);
    row.set<1>(200);
    row.resetChanges();
    
    EXPECT_FALSE(row.hasAnyChanges());
    
    // Mutable visit should mark all as changed
    row.visit([&](auto, auto& value, bool&) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value += 50;
        }
    });
    
    EXPECT_TRUE(row.hasAnyChanges());
    EXPECT_EQ(row.get<0>(), 150);
    EXPECT_EQ(row.get<1>(), 250);
}

TEST(VisitTest, RowViewMutableVisitPrimitives) {
    Layout layout({
        {"id", ColumnType::INT32},
        {"value", ColumnType::DOUBLE},
        {"flag", ColumnType::BOOL}
    });
    
    Row row(layout);
    row.set(0, int32_t(42));
    row.set(1, 3.14);
    row.set(2, true);
    
    ByteBuffer buffer;
    auto serialized = row.serializeTo(buffer);
    
    RowView rowView(layout, std::span<std::byte>(serialized));
    
    // Mutable visit: modify primitives in buffer
    rowView.visit([&](size_t, auto& value, bool& change) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int32_t>) {
            value = 100;
        } else if constexpr (std::is_same_v<T, double>) {
            value = 2.718;
        } else if constexpr (std::is_same_v<T, bool>) {
            value = false;
        } else {
            change = false;
        }
    });
    
    // Verify changes persisted in buffer
    EXPECT_EQ(rowView.get<int32_t>(0), 100);
    EXPECT_NEAR(rowView.get<double>(1), 2.718, 0.001);
    EXPECT_FALSE(rowView.get<bool>(2));
}

TEST(VisitTest, RowViewMutableVisitStringsReadOnly) {
    Layout layout({
        {"name", ColumnType::STRING},
        {"age", ColumnType::INT32}
    });
    
    Row row(layout);
    row.set(0, std::string("Bob"));
    row.set(1, int32_t(25));
    
    ByteBuffer buffer;
    auto serialized = row.serializeTo(buffer);
    
    RowView rowView(layout, std::span<std::byte>(serialized));
    
    // Mutable visit: strings are read-only, primitives are mutable
    std::string capturedName;
    rowView.visit([&](size_t, auto& value, bool& change) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
            // String passed as const (read-only)
            capturedName = std::string(value);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            value = 30;  // Can modify primitive
        } else {
            change = false;
        }
    });
    
    EXPECT_EQ(capturedName, "Bob");
    EXPECT_EQ(rowView.get<int32_t>(1), 30);
}

TEST(VisitTest, RowViewStaticMutableVisit) {
    using LayoutType = LayoutStatic<int32_t, double, bool>;
    LayoutType layout({"id", "value", "flag"});
    
    RowStatic<int32_t, double, bool> row(layout);
    row.set<0>(10);
    row.set<1>(1.5);
    row.set<2>(true);
    
    ByteBuffer buffer;
    auto serialized = row.serializeTo(buffer);
    
    RowViewStatic<int32_t, double, bool> rowView(layout, std::span<std::byte>(serialized));
    
    // Mutable visit: modify primitives in buffer
    rowView.visit([&](auto, auto& value, bool& change) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value *= 2;
        } else if constexpr (std::is_same_v<T, bool>) {
            value = false;
        } else {
            change = false;
        }
    });
    
    // Verify changes
    EXPECT_EQ(rowView.get<0>(), 20);
    EXPECT_NEAR(rowView.get<1>(), 3.0, 0.01);
    EXPECT_FALSE(rowView.get<2>());
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

TEST(VisitTest, FineGrainedChangeTracking) {
    Layout layout({
        {"a", ColumnType::INT32},
        {"b", ColumnType::INT32},
        {"c", ColumnType::INT32}
    });
    
    Row row(layout, true);  // Enable change tracking
    row.set(0, 10);
    row.set(1, 20);
    row.set(2, 30);
    row.resetChanges();
    
    EXPECT_FALSE(row.hasAnyChanges());
    
    // Fine-grained visitor: only modifies columns where value < 25
    // Uses optional 3rd parameter to control change tracking
    row.visit([&](size_t, auto& value, bool& changed) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            if (value < 25) {
                value += 100;  // Modify
                changed = true;  // Mark as changed
            } else {
                changed = false;  // Explicitly mark as NOT changed
            }
        }
    });
    
    // Only columns 0 and 1 should be marked as changed (values were < 25)
    EXPECT_EQ(row.get<int32_t>(0), 110);  // Modified
    EXPECT_EQ(row.get<int32_t>(1), 120);  // Modified
    EXPECT_EQ(row.get<int32_t>(2), 30);   // NOT modified
    
    // Can verify via serialization - ZoH would only include changed columns
    ByteBuffer buffer;
    auto serialized = row.serializeToZoH(buffer);
    
    // With fine-grained tracking, only 2 columns changed
    // Expected size: bitset (1 byte) + 2 * sizeof(int32_t) = 1 + 8 = 9 bytes
    EXPECT_EQ(serialized.size(), 9);
}

TEST(VisitTest, FineGrainedChangeTrackingRowStatic) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t, int32_t>;
    LayoutType layout({"a", "b", "c", "d"});
    
    RowStatic<int32_t, int32_t, int32_t, int32_t> row(layout);
    row.trackChanges(true);
    row.set<0>(5);
    row.set<1>(15);
    row.set<2>(25);
    row.set<3>(35);
    row.resetChanges();
    
    // Conditional modification with fine-grained tracking
    row.visit([&](auto, auto& value, bool& changed) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            if (value % 10 == 5) {  // Only modify values ending in 5
                value *= 2;
                changed = true;
            } else {
                changed = false;
            }
        } else {
            changed = false;
        }
    });
    
    EXPECT_EQ(row.get<0>(), 10);   // Modified (was 5)
    EXPECT_EQ(row.get<1>(), 30);   // Modified (was 15)
    EXPECT_EQ(row.get<2>(), 50);   // Modified (was 25)
    EXPECT_EQ(row.get<3>(), 70);   // Modified (was 35)
    
    // All were actually modified in this case
    EXPECT_TRUE(row.hasAnyChanges());
}

TEST(VisitTest, IgnoringChangeFlagParameter) {
    Layout layout({
        {"x", ColumnType::INT32},
        {"y", ColumnType::INT32}
    });
    
    Row row(layout, true);
    row.set(0, 10);
    row.set(1, 20);
    row.resetChanges();
    
    // Visitor with unnamed 3rd parameter - still marks all as changed
    row.visit([&](size_t, auto& value, bool&) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            value *= 2;
        }
    });
    
    EXPECT_EQ(row.get<int32_t>(0), 20);
    EXPECT_EQ(row.get<int32_t>(1), 40);
    EXPECT_TRUE(row.hasAnyChanges());
}


