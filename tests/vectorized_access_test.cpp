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
#include <array>
#include <vector>
#include <span>
#include <chrono>

using namespace bcsv;

// =============================================================================
// Row (Dynamic) Vectorized Access Tests
// =============================================================================

TEST(RowVectorizedTest, GetMultipleInt32) {
    Layout layout;
    layout.addColumn({"col1", ColumnType::INT32});
    layout.addColumn({"col2", ColumnType::INT32});
    layout.addColumn({"col3", ColumnType::INT32});
    layout.addColumn({"col4", ColumnType::INT32});

    Row row(layout);
    row.set<int32_t>(0, 10);
    row.set<int32_t>(1, 20);
    row.set<int32_t>(2, 30);
    row.set<int32_t>(3, 40);

    // Test with C array wrapped in std::span
    int32_t buffer[3];
    std::span<int32_t> buffer_span{buffer, 3};
    row.get(1, buffer_span);
    EXPECT_EQ(buffer[0], 20) << "Vectorized get failed: buffer[0] from column 1 expected 20";
    EXPECT_EQ(buffer[1], 30) << "Vectorized get failed: buffer[1] from column 2 expected 30";
    EXPECT_EQ(buffer[2], 40) << "Vectorized get failed: buffer[2] from column 3 expected 40";

    // Test span with vector
    std::vector<int32_t> vec(3);
    std::span<int32_t> vec_span{vec};
    row.get(0, vec_span);
    EXPECT_EQ(vec[0], 10) << "Vectorized get to vector failed: vec[0] from column 0 expected 10";
    EXPECT_EQ(vec[1], 20) << "Vectorized get to vector failed: vec[1] from column 1 expected 20";
    EXPECT_EQ(vec[2], 30) << "Vectorized get to vector failed: vec[2] from column 2 expected 30";
}

TEST(RowVectorizedTest, SetMultipleInt32) {
    Layout layout;
    layout.addColumn({"col1", ColumnType::INT32});
    layout.addColumn({"col2", ColumnType::INT32});
    layout.addColumn({"col3", ColumnType::INT32});

    Row row(layout);

    // Test with C array wrapped in std::span
    int32_t values[] = {100, 200, 300};
    row.set(0, std::span<const int32_t>{values, 3});
    EXPECT_EQ(row.get<int32_t>(0), 100) << "Vectorized set failed: column 0 should be 100";
    EXPECT_EQ(row.get<int32_t>(1), 200) << "Vectorized set failed: column 1 should be 200";
    EXPECT_EQ(row.get<int32_t>(2), 300) << "Vectorized set failed: column 2 should be 300";

    // Test span with std::array
    std::array<int32_t, 2> arr = {999, 888};
    row.set(1, std::span<const int32_t>{arr});
    EXPECT_EQ(row.get<int32_t>(1), 999) << "Array-based vectorized set failed: column 1 should be 999";
    EXPECT_EQ(row.get<int32_t>(2), 888) << "Array-based vectorized set failed: column 2 should be 888";
}

TEST(RowVectorizedTest, GetMultipleDoubles) {
    Layout layout;
    layout.addColumn({"col1", ColumnType::DOUBLE});
    layout.addColumn({"col2", ColumnType::DOUBLE});
    layout.addColumn({"col3", ColumnType::DOUBLE});

    Row row(layout);
    row.set<double>(0, 1.5);
    row.set<double>(1, 2.5);
    row.set<double>(2, 3.5);

    double buffer[3];
    std::span<double> buffer_span{buffer, 3};
    row.get(0, buffer_span);
    EXPECT_DOUBLE_EQ(buffer[0], 1.5) << "Vectorized double get failed: column 0 expected 1.5";
    EXPECT_DOUBLE_EQ(buffer[1], 2.5) << "Vectorized double get failed: column 1 expected 2.5";
    EXPECT_DOUBLE_EQ(buffer[2], 3.5) << "Vectorized double get failed: column 2 expected 3.5";
}

TEST(RowVectorizedTest, ChangeTrackingMultiple) {
    Layout layout;
    layout.addColumn({"col1", ColumnType::INT32});
    layout.addColumn({"col2", ColumnType::INT32});
    layout.addColumn({"col3", ColumnType::INT32});

    Row row(layout);
    row.trackChanges(true);
    row.resetChanges();

    EXPECT_FALSE(row.hasAnyChanges()) 
        << "After resetChanges(), hasAnyChanges() should return false";

    // Set multiple values
    int32_t values[] = {10, 20, 30};
    row.set(0, std::span<const int32_t>{values, 3});

    EXPECT_TRUE(row.hasAnyChanges()) 
        << "After vectorized set, hasAnyChanges() should return true";
}

TEST(RowVectorizedTest, BoundaryCheck) {
    Layout layout;
    layout.addColumn({"col1", ColumnType::INT32});
    layout.addColumn({"col2", ColumnType::INT32});

    Row row(layout);

    int32_t buffer[5];
    std::span<int32_t> buffer_span{buffer, 3};
    // This should throw because we're trying to read 3 columns starting at index 1
    // but the layout only has 2 columns total
    EXPECT_THROW(row.get(1, buffer_span), std::out_of_range) 
        << "Vectorized get should throw std::out_of_range when accessing beyond column count";
}

// =============================================================================
// RowStatic Compile-Time Vectorized Access Tests
// =============================================================================

TEST(RowStaticVectorizedTest, CompileTimeGetMultipleInt32) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t, double, bool>;
    LayoutType layout({"col1", "col2", "col3", "col4", "col5"});
    RowStatic<int32_t, int32_t, int32_t, double, bool> row(layout);

    row.set<0>(10);
    row.set<1>(20);
    row.set<2>(30);

    // Test with std::array
    std::array<int32_t, 3> arr;
    std::span<int32_t, 3> arr_span{arr};
    row.get<0>(arr_span);
    EXPECT_EQ(arr[0], 10);
    EXPECT_EQ(arr[1], 20);
    EXPECT_EQ(arr[2], 30);

    // Test with C array
    int32_t buffer[3];
    std::span<int32_t, 3> buffer_span{buffer, 3};
    row.get<0>(buffer_span);
    EXPECT_EQ(buffer[0], 10);
    EXPECT_EQ(buffer[1], 20);
    EXPECT_EQ(buffer[2], 30);

    // Test with span
    std::span<int32_t, 3> span_arr(buffer, 3);
    row.get<0>(span_arr);
    EXPECT_EQ(span_arr[0], 10);
    EXPECT_EQ(span_arr[1], 20);
    EXPECT_EQ(span_arr[2], 30);
}

TEST(RowStaticVectorizedTest, CompileTimeSetMultipleInt32) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t>;
    LayoutType layout({"col1", "col2", "col3"});
    RowStatic<int32_t, int32_t, int32_t> row(layout);

    // Test with std::array
    std::array<int32_t, 3> arr = {100, 200, 300};
    row.set<0>(std::span<const int32_t, 3>{arr});
    EXPECT_EQ(row.get<0>(), 100);
    EXPECT_EQ(row.get<1>(), 200);
    EXPECT_EQ(row.get<2>(), 300);

    // Test with C array
    int32_t buffer[] = {10, 20, 30};
    row.set<0>(std::span<const int32_t, 3>{buffer, 3});
    EXPECT_EQ(row.get<0>(), 10);
    EXPECT_EQ(row.get<1>(), 20);
    EXPECT_EQ(row.get<2>(), 30);
}

TEST(RowStaticVectorizedTest, CompileTimeGetPartialRange) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t, int32_t, int32_t>;
    LayoutType layout({"col1", "col2", "col3", "col4", "col5"});
    RowStatic<int32_t, int32_t, int32_t, int32_t, int32_t> row(layout);

    row.set<0>(1);
    row.set<1>(2);
    row.set<2>(3);
    row.set<3>(4);
    row.set<4>(5);

    // Get middle 3 columns
    std::array<int32_t, 3> arr;
    std::span<int32_t, 3> arr_span{arr};
    row.get<1>(arr_span);
    EXPECT_EQ(arr[0], 2);
    EXPECT_EQ(arr[1], 3);
    EXPECT_EQ(arr[2], 4);

    // Get last 2 columns
    std::array<int32_t, 2> arr2;
    std::span<int32_t, 2> arr2_span{arr2};
    row.get<3>(arr2_span);
    EXPECT_EQ(arr2[0], 4);
    EXPECT_EQ(arr2[1], 5);
}

TEST(RowStaticVectorizedTest, CompileTimeChangeTracking) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t>;
    LayoutType layout({"col1", "col2", "col3"});
    RowStatic<int32_t, int32_t, int32_t> row(layout);

    row.trackChanges(true);
    row.resetChanges();
    EXPECT_FALSE(row.hasAnyChanges());

    // Set multiple values
    std::array<int32_t, 3> arr = {10, 20, 30};
    row.set<0, int32_t, 3>(arr);

    EXPECT_TRUE(row.hasAnyChanges());
}

// =============================================================================
// RowStatic Runtime Vectorized Access Tests
// =============================================================================

TEST(RowStaticVectorizedTest, RuntimeGetMultipleInt32) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t, double>;
    LayoutType layout({"col1", "col2", "col3", "col4"});
    RowStatic<int32_t, int32_t, int32_t, double> row(layout);

    row.set<0>(10);
    row.set<1>(20);
    row.set<2>(30);

    // Test with C array wrapped in std::span
    int32_t buffer[3];
    std::span<int32_t> buffer_span{buffer, 3};
    row.get(0, buffer_span);
    EXPECT_EQ(buffer[0], 10);
    EXPECT_EQ(buffer[1], 20);
    EXPECT_EQ(buffer[2], 30);

    // Test span with vector
    std::vector<int32_t> vec(2);
    std::span<int32_t> vec_span{vec};
    row.get(1, vec_span);
    EXPECT_EQ(vec[0], 20);
    EXPECT_EQ(vec[1], 30);
}

TEST(RowStaticVectorizedTest, RuntimeSetMultipleInt32) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t>;
    LayoutType layout({"col1", "col2", "col3"});
    RowStatic<int32_t, int32_t, int32_t> row(layout);

    // Test with C array wrapped in std::span
    int32_t values[] = {100, 200, 300};
    std::span<const int32_t> values_span{values, 3};
    row.set(0, values_span);
    EXPECT_EQ(row.get<0>(), 100);
    EXPECT_EQ(row.get<1>(), 200);
    EXPECT_EQ(row.get<2>(), 300);

    // Test span with std::array
    std::array<int32_t, 2> arr = {999, 888};
    std::span<const int32_t> arr_span{arr};
    row.set(1, arr_span);
    EXPECT_EQ(row.get<1>(), 999);
    EXPECT_EQ(row.get<2>(), 888);
}

TEST(RowStaticVectorizedTest, RuntimeBoundaryCheck) {
    using LayoutType = LayoutStatic<int32_t, int32_t>;
    LayoutType layout({"col1", "col2"});
    RowStatic<int32_t, int32_t> row(layout);

    int32_t buffer[5];
    std::span<int32_t> buffer_span{buffer, 3};
    // This should throw - trying to read 3 columns starting at index 1, but only 2 columns total
    EXPECT_THROW(row.get(1, buffer_span), std::out_of_range);
}

TEST(RowStaticVectorizedTest, RuntimeChangeTracking) {
    using LayoutType = LayoutStatic<int32_t, int32_t, int32_t>;
    LayoutType layout({"col1", "col2", "col3"});
    RowStatic<int32_t, int32_t, int32_t> row(layout);

    row.trackChanges(true);
    row.resetChanges();
    EXPECT_FALSE(row.hasAnyChanges());

    // Set multiple values via runtime interface
    int32_t values[] = {10, 20, 30};
    std::span<const int32_t> values_span{values, 3};
    row.set(0, values_span);

    EXPECT_TRUE(row.hasAnyChanges());
}

// =============================================================================
// Mixed Type Tests
// =============================================================================

TEST(VectorizedMixedTest, MixedColumnsPartialAccess) {
    using LayoutType = LayoutStatic<std::string, int32_t, int32_t, double, bool>;
    LayoutType layout({"name", "age", "score", "rating", "active"});
    RowStatic<std::string, int32_t, int32_t, double, bool> row(layout);

    row.set<0>(std::string("John"));
    row.set<1>(25);
    row.set<2>(100);
    row.set<3>(4.5);
    row.set<4>(true);

    // Get only the two int32 columns
    std::array<int32_t, 2> int_cols;
    std::span<int32_t, 2> int_cols_span{int_cols};
    row.get<1>(int_cols_span);
    EXPECT_EQ(int_cols[0], 25);
    EXPECT_EQ(int_cols[1], 100);

    // Set the two int32 columns
    std::array<int32_t, 2> new_vals = {30, 150};
    std::span<const int32_t, 2> new_vals_span{new_vals};
    row.set<1>(new_vals_span);
    EXPECT_EQ(row.get<1>(), 30);
    EXPECT_EQ(row.get<2>(), 150);
}

// =============================================================================
// Performance Comparison Test (Optional - for manual benchmarking)
// =============================================================================

TEST(VectorizedPerformanceTest, CompareIndividualVsBulk) {
    constexpr size_t NUM_COLUMNS = 100;
    constexpr size_t NUM_ITERATIONS = 50000;

    Layout layout;
    for (size_t i = 0; i < NUM_COLUMNS; ++i) {
        layout.addColumn({"col" + std::to_string(i), ColumnType::INT32});
    }

    Row row(layout);

    // Initialize with some values
    for (size_t i = 0; i < NUM_COLUMNS; ++i) {
        row.set<int32_t>(i, static_cast<int32_t>(i));
    }

    // Individual access
    auto start_individual = std::chrono::steady_clock::now();
    for (size_t iter = 0; iter < NUM_ITERATIONS; ++iter) {
        int32_t buffer[NUM_COLUMNS];
        for (size_t i = 0; i < NUM_COLUMNS; ++i) {
            buffer[i] = row.get<int32_t>(i);
        }
        // Prevent optimization
        volatile int32_t sum = 0;
        for (size_t i = 0; i < NUM_COLUMNS; ++i) {
            sum += buffer[i];
        }
    }
    auto end_individual = std::chrono::steady_clock::now();
    auto duration_individual = std::chrono::duration_cast<std::chrono::microseconds>(end_individual - start_individual);

    // Bulk access
    auto start_bulk = std::chrono::steady_clock::now();
    for (size_t iter = 0; iter < NUM_ITERATIONS; ++iter) {
        int32_t buffer[NUM_COLUMNS];
        std::span<int32_t> buffer_span{buffer, NUM_COLUMNS};
        row.get(0, buffer_span);
        // Prevent optimization
        volatile int32_t sum = 0;
        for (size_t i = 0; i < NUM_COLUMNS; ++i) {
            sum += buffer[i];
        }
    }
    auto end_bulk = std::chrono::steady_clock::now();
    auto duration_bulk = std::chrono::duration_cast<std::chrono::microseconds>(end_bulk - start_bulk);

    std::cout << "Individual access: " << duration_individual.count() << " μs\n";
    std::cout << "Bulk access:       " << duration_bulk.count() << " μs\n";
    std::cout << "Speedup:           " << (double)duration_individual.count() / duration_bulk.count() << "x\n";

    // We expect bulk to be at least as fast as individual
    // In practice, it should be faster due to reduced overhead
    EXPECT_LE(duration_bulk.count(), duration_individual.count() * 1.2); // Allow 20% margin
}
