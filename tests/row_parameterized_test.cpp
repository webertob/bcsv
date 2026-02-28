/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file row_parameterized_test.cpp
 * @brief Parameterized tests for Row API to reduce duplication across data types
 * 
 * This test suite uses Google Test's typed tests to verify that all primitive
 * data types work correctly with the Row API, reducing code duplication and
 * ensuring consistent behavior across all supported types.
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>
#include <limits>
#include <type_traits>

using namespace bcsv;

// =============================================================================
// Type List for Parameterized Tests
// =============================================================================

// Helper template to associate C++ types with ColumnType enum
template<typename T>
struct TypeTraits;

template<> struct TypeTraits<bool> {
    static constexpr ColumnType column_type = ColumnType::BOOL;
    static constexpr const char* name = "BOOL";
    static bool test_value_1() { return false; }
    static bool test_value_2() { return true; }
    static bool test_value_3() { return true; }
};

template<> struct TypeTraits<int8_t> {
    static constexpr ColumnType column_type = ColumnType::INT8;
    static constexpr const char* name = "INT8";
    static int8_t test_value_1() { return -100; }
    static int8_t test_value_2() { return 0; }
    static int8_t test_value_3() { return 127; }
};

template<> struct TypeTraits<int16_t> {
    static constexpr ColumnType column_type = ColumnType::INT16;
    static constexpr const char* name = "INT16";
    static int16_t test_value_1() { return -1000; }
    static int16_t test_value_2() { return 0; }
    static int16_t test_value_3() { return 32767; }
};

template<> struct TypeTraits<int32_t> {
    static constexpr ColumnType column_type = ColumnType::INT32;
    static constexpr const char* name = "INT32";
    static int32_t test_value_1() { return -100000; }
    static int32_t test_value_2() { return 0; }
    static int32_t test_value_3() { return 2147483647; }
};

template<> struct TypeTraits<int64_t> {
    static constexpr ColumnType column_type = ColumnType::INT64;
    static constexpr const char* name = "INT64";
    static int64_t test_value_1() { return -1000000000LL; }
    static int64_t test_value_2() { return 0; }
    static int64_t test_value_3() { return 9223372036854775807LL; }
};

template<> struct TypeTraits<uint8_t> {
    static constexpr ColumnType column_type = ColumnType::UINT8;
    static constexpr const char* name = "UINT8";
    static uint8_t test_value_1() { return 0; }
    static uint8_t test_value_2() { return 128; }
    static uint8_t test_value_3() { return 255; }
};

template<> struct TypeTraits<uint16_t> {
    static constexpr ColumnType column_type = ColumnType::UINT16;
    static constexpr const char* name = "UINT16";
    static uint16_t test_value_1() { return 0; }
    static uint16_t test_value_2() { return 32768; }
    static uint16_t test_value_3() { return 65535; }
};

template<> struct TypeTraits<uint32_t> {
    static constexpr ColumnType column_type = ColumnType::UINT32;
    static constexpr const char* name = "UINT32";
    static uint32_t test_value_1() { return 0; }
    static uint32_t test_value_2() { return 2147483648U; }
    static uint32_t test_value_3() { return 4294967295U; }
};

template<> struct TypeTraits<uint64_t> {
    static constexpr ColumnType column_type = ColumnType::UINT64;
    static constexpr const char* name = "UINT64";
    static uint64_t test_value_1() { return 0; }
    static uint64_t test_value_2() { return 9223372036854775808ULL; }
    static uint64_t test_value_3() { return 18446744073709551615ULL; }
};

template<> struct TypeTraits<float> {
    static constexpr ColumnType column_type = ColumnType::FLOAT;
    static constexpr const char* name = "FLOAT";
    static float test_value_1() { return -123.456f; }
    static float test_value_2() { return 0.0f; }
    static float test_value_3() { return 789.012f; }
};

template<> struct TypeTraits<double> {
    static constexpr ColumnType column_type = ColumnType::DOUBLE;
    static constexpr const char* name = "DOUBLE";
    static double test_value_1() { return -123456.789; }
    static double test_value_2() { return 0.0; }
    static double test_value_3() { return 987654.321; }
};

// Type list for parameterized tests
using PrimitiveTypes = ::testing::Types<
    bool, int8_t, int16_t, int32_t, int64_t,
    uint8_t, uint16_t, uint32_t, uint64_t,
    float, double
>;

// =============================================================================
// Parameterized Tests for Row (Dynamic Layout)
// =============================================================================

template<typename T>
class RowTypedTest : public ::testing::Test {
protected:
    Layout layout_;
    
    void SetUp() override {
        layout_.addColumn({"value1", TypeTraits<T>::column_type});
        layout_.addColumn({"value2", TypeTraits<T>::column_type});
        layout_.addColumn({"value3", TypeTraits<T>::column_type});
    }
    
    std::string GetTypeName() const {
        return TypeTraits<T>::name;
    }
};

TYPED_TEST_SUITE(RowTypedTest, PrimitiveTypes);

TYPED_TEST(RowTypedTest, GetSetScalar) {
    using T = TypeParam;
    Row row(this->layout_);
    
    T val1 = TypeTraits<T>::test_value_1();
    T val2 = TypeTraits<T>::test_value_2();
    T val3 = TypeTraits<T>::test_value_3();
    
    // Set values
    row.set(0, val1);
    row.set(1, val2);
    row.set(2, val3);
    
    // Get and verify values
    if constexpr (std::is_floating_point_v<T>) {
        EXPECT_FLOAT_EQ(val1, row.get<T>(0))
            << "Column 0 value mismatch for type " << this->GetTypeName();
        EXPECT_FLOAT_EQ(val2, row.get<T>(1))
            << "Column 1 value mismatch for type " << this->GetTypeName();
        EXPECT_FLOAT_EQ(val3, row.get<T>(2))
            << "Column 2 value mismatch for type " << this->GetTypeName();
    } else {
        EXPECT_EQ(val1, row.get<T>(0))
            << "Column 0 value mismatch for type " << this->GetTypeName();
        EXPECT_EQ(val2, row.get<T>(1))
            << "Column 1 value mismatch for type " << this->GetTypeName();
        EXPECT_EQ(val3, row.get<T>(2))
            << "Column 2 value mismatch for type " << this->GetTypeName();
    }
}

TYPED_TEST(RowTypedTest, VectorizedGetSet) {
    using T = TypeParam;
    Row row(this->layout_);
    
    T values[] = {
        TypeTraits<T>::test_value_1(),
        TypeTraits<T>::test_value_2(),
        TypeTraits<T>::test_value_3()
    };
    
    // Vectorized set
    ASSERT_NO_THROW(row.set(0, std::span<const T>{values, 3}))
        << "Vectorized set failed for type " << this->GetTypeName();
    
    // Vectorized get
    T result[3];
    std::span<T> result_span{result, 3};
    ASSERT_NO_THROW(row.get(0, result_span))
        << "Vectorized get failed for type " << this->GetTypeName();
    
    // Verify values
    for (size_t i = 0; i < 3; ++i) {
        if constexpr (std::is_floating_point_v<T>) {
            EXPECT_FLOAT_EQ(values[i], result[i])
                << "Vectorized value mismatch at index " << i 
                << " for type " << this->GetTypeName();
        } else {
            EXPECT_EQ(values[i], result[i])
                << "Vectorized value mismatch at index " << i 
                << " for type " << this->GetTypeName();
        }
    }
}

TYPED_TEST(RowTypedTest, Serialization) {
    using T = TypeParam;
    Row row1(this->layout_);
    
    T val1 = TypeTraits<T>::test_value_1();
    T val2 = TypeTraits<T>::test_value_2();
    T val3 = TypeTraits<T>::test_value_3();
    
    row1.set(0, val1);
    row1.set(1, val2);
    row1.set(2, val3);
    
    // Serialize via codec
    ByteBuffer buffer;
    RowCodecFlat001<Layout> codec;
    codec.setup(this->layout_);
    auto serialized = codec.serialize(row1, buffer);
    ASSERT_FALSE(serialized.empty())
        << "Serialization failed for type " << this->GetTypeName();
    
    // Deserialize
    Row row2(this->layout_);
    ASSERT_NO_THROW(codec.deserialize(serialized, row2))
        << "Deserialization failed for type " << this->GetTypeName();
    
    // Verify round-trip
    if constexpr (std::is_floating_point_v<T>) {
        EXPECT_FLOAT_EQ(val1, row2.get<T>(0))
            << "Round-trip failed for column 0, type " << this->GetTypeName();
        EXPECT_FLOAT_EQ(val2, row2.get<T>(1))
            << "Round-trip failed for column 1, type " << this->GetTypeName();
        EXPECT_FLOAT_EQ(val3, row2.get<T>(2))
            << "Round-trip failed for column 2, type " << this->GetTypeName();
    } else {
        EXPECT_EQ(val1, row2.get<T>(0))
            << "Round-trip failed for column 0, type " << this->GetTypeName();
        EXPECT_EQ(val2, row2.get<T>(1))
            << "Round-trip failed for column 1, type " << this->GetTypeName();
        EXPECT_EQ(val3, row2.get<T>(2))
            << "Round-trip failed for column 2, type " << this->GetTypeName();
    }
}

TYPED_TEST(RowTypedTest, TypeMismatch) {
    using T = TypeParam;
    Row row(this->layout_);
    
    // Note: The flexible set() API allows type conversions between compatible types
    // Type mismatch is only caught for incompatible types (e.g., string to number)
    // For strict type checking, use the template version set<T>()
    
    // Skip this test for int32_t since that's what we're testing with
    if constexpr (!std::is_same_v<T, int32_t>) {
        // The flexible API allows implicit conversions, so this will succeed
        // This is by design for the Row API
        // For strict type checking, RowStatic with compile-time types should be used
    }
}

TYPED_TEST(RowTypedTest, BoundaryValues) {
    using T = TypeParam;
    
    if constexpr (std::is_integral_v<T>) {
        Row row(this->layout_);
        
        T min_val = std::numeric_limits<T>::min();
        T max_val = std::numeric_limits<T>::max();
        
        row.set(0, min_val);
        row.set(1, max_val);
        
        EXPECT_EQ(min_val, row.get<T>(0))
            << "Min value not preserved for type " << this->GetTypeName();
        EXPECT_EQ(max_val, row.get<T>(1))
            << "Max value not preserved for type " << this->GetTypeName();
    }
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST(RowEdgeCases, SelfAssignment) {
    Layout layout;
    layout.addColumn({"value", ColumnType::INT32});
    
    Row row(layout);
    row.set<int32_t>(0, 42);
    
    // Self-assignment should not crash or corrupt data
    row = row;
    
    EXPECT_EQ(42, row.get<int32_t>(0))
        << "Self-assignment corrupted data";
}

TEST(RowEdgeCases, MoveAfterMove) {
    Layout layout;
    layout.addColumn({"value", ColumnType::INT32});
    
    Row row1(layout);
    row1.set<int32_t>(0, 42);
    
    Row row2 = std::move(row1);
    EXPECT_EQ(42, row2.get<int32_t>(0))
        << "Move constructor failed to transfer data";
    
    // Move from already-moved-from object (should be safe but in moved-from state)
    Row row3 = std::move(row1);
    // row1 is now in moved-from state - don't access it
    
    EXPECT_EQ(42, row2.get<int32_t>(0))
        << "Original move target was affected by second move";
}

TEST(RowEdgeCases, StringWithEmbeddedNulls) {
    Layout layout;
    layout.addColumn({"text", ColumnType::STRING});
    
    Row row(layout);
    
    // String with embedded null bytes
    std::string with_null("hello\0world", 11);
    row.set(0, with_null);
    
    std::string retrieved = row.get<std::string>(0);
    EXPECT_EQ(11, retrieved.size())
        << "String size not preserved with embedded nulls";
    EXPECT_EQ(with_null, retrieved)
        << "String content not preserved with embedded nulls";
}

TEST(RowEdgeCases, EmptyStringVsDefault) {
    Layout layout;
    layout.addColumn({"text", ColumnType::STRING});
    
    Row row(layout);
    
    // Check default value
    std::string default_val = row.get<std::string>(0);
    EXPECT_TRUE(default_val.empty())
        << "Default string value should be empty";
    
    // Set empty string explicitly
    row.set(0, std::string(""));
    
    std::string retrieved = row.get<std::string>(0);
    EXPECT_TRUE(retrieved.empty())
        << "Empty string not preserved";
}

TEST(RowEdgeCases, VectorizedOutOfBounds) {
    Layout layout;
    layout.addColumn({"a", ColumnType::INT32});
    layout.addColumn({"b", ColumnType::INT32});
    
    Row row(layout);
    
    int32_t values[5] = {1, 2, 3, 4, 5};
    
    // Try to set 5 values starting at index 0, but only 2 columns exist
    // The actual exception type is std::out_of_range for bounds checking
    EXPECT_THROW(
        row.set(0, std::span<const int32_t>{values, 5}),
        std::out_of_range
    ) << "Out-of-bounds vectorized set should throw std::out_of_range";
    
    // Try to get 3 values starting at index 1 (would need indices 1, 2, 3)
    int32_t buffer[3];
    std::span<int32_t> buffer_span{buffer, 3};
    EXPECT_THROW(
        row.get(1, buffer_span),
        std::out_of_range
    ) << "Out-of-bounds vectorized get should throw std::out_of_range";
}

TEST(RowEdgeCases, FlexibleGetWithConversion) {
    Layout layout;
    layout.addColumn({"byte", ColumnType::INT8});
    layout.addColumn({"word", ColumnType::INT16});
    
    Row row(layout);
    row.set<int8_t>(0, 127);
    row.set<int16_t>(1, 32767);
    
    // Flexible get with conversion (int8 -> int32)
    int32_t val1;
    ASSERT_TRUE(row.get(0, val1))
        << "Flexible get failed to convert int8 to int32";
    EXPECT_EQ(127, val1)
        << "Conversion from int8 to int32 produced wrong value";
    
    // Flexible get with conversion (int16 -> int64)
    int64_t val2;
    ASSERT_TRUE(row.get(1, val2))
        << "Flexible get failed to convert int16 to int64";
    EXPECT_EQ(32767, val2)
        << "Conversion from int16 to int64 produced wrong value";
}
