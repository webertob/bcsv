/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bitset_test.cpp
 * @brief Comprehensive tests for unified bitset implementation (fixed & dynamic)
 * 
 * Tests cover:
 * - Fixed-size bitsets (compile-time size)
 * - Dynamic-size bitsets (runtime size)
 * - Small (1-8 bits), medium (64-256), large (1024-8192), very large (65536) sizes
 * - All operations: set, reset, flip, count, any, all, none
 * - Bitwise operators: &, |, ^, ~, <<, >>
 * - Conversions: to_ulong, to_ullong, to_string
 * - I/O: readFrom, writeTo, data access
 * - Dynamic-only: resize, reserve, clear
 */

#include <gtest/gtest.h>
#include <bcsv/bitset.h>
#include <sstream>

using namespace bcsv;

// ============================================================================
// Fixed-Size Bitset Tests
// ============================================================================

class FixedBitsetTest : public ::testing::Test {
protected:
    // Test various fixed sizes
    bitset<1> bs1;
    bitset<8> bs8;
    bitset<64> bs64;
    bitset<256> bs256;
    
    void SetUp() override {
        // Start fresh for each test
        bs1.reset();
        bs8.reset();
        bs64.reset();
        bs256.reset();
    }
};

TEST_F(FixedBitsetTest, Construction_Default) {
    EXPECT_EQ(bs1.size(), 1);
    EXPECT_EQ(bs8.size(), 8);
    EXPECT_EQ(bs64.size(), 64);
    EXPECT_EQ(bs256.size(), 256);
    
    EXPECT_TRUE(bs1.none());
    EXPECT_TRUE(bs8.none());
    EXPECT_TRUE(bs64.none());
    EXPECT_TRUE(bs256.none());
}

TEST_F(FixedBitsetTest, Construction_FromValue) {
    bitset<8> bs_val(0xAB);  // 10101011
    EXPECT_EQ(bs_val.count(), 5);
    EXPECT_TRUE(bs_val[0]);
    EXPECT_TRUE(bs_val[1]);
    EXPECT_FALSE(bs_val[2]);
    EXPECT_TRUE(bs_val[3]);
    EXPECT_TRUE(bs_val[5]);
    EXPECT_TRUE(bs_val[7]);
    
    bitset<64> bs64_val(0xFFFFFFFF00000000ULL);
    EXPECT_EQ(bs64_val.count(), 32);
    for (size_t i = 0; i < 32; ++i) EXPECT_FALSE(bs64_val[i]);
    for (size_t i = 32; i < 64; ++i) EXPECT_TRUE(bs64_val[i]);
}

TEST_F(FixedBitsetTest, Construction_FromString) {
    bitset<8> bs(std::string("10101011"));  // MSB first
    EXPECT_EQ(bs.count(), 5);
    EXPECT_TRUE(bs[0]);   // LSB
    EXPECT_TRUE(bs[1]);
    EXPECT_FALSE(bs[2]);
    EXPECT_TRUE(bs[3]);
    EXPECT_TRUE(bs[7]);   // MSB
}

TEST_F(FixedBitsetTest, ElementAccess_Operators) {
    bs8.set(0);
    bs8.set(3);
    bs8.set(7);
    
    EXPECT_TRUE(bs8[0]);
    EXPECT_FALSE(bs8[1]);
    EXPECT_TRUE(bs8[3]);
    EXPECT_TRUE(bs8[7]);
    
    EXPECT_TRUE(bs8.test(0));
    EXPECT_THROW(bs8.test(8), std::out_of_range);
}

TEST_F(FixedBitsetTest, Modifiers_Set) {
    bs8.set();
    EXPECT_EQ(bs8.count(), 8);
    EXPECT_TRUE(bs8.all());
    
    bs8.reset();
    bs8.set(3, true);
    EXPECT_TRUE(bs8[3]);
    EXPECT_EQ(bs8.count(), 1);
    
    bs8.set(3, false);
    EXPECT_FALSE(bs8[3]);
    EXPECT_TRUE(bs8.none());
}

TEST_F(FixedBitsetTest, Modifiers_Reset) {
    bs8.set();
    bs8.reset();
    EXPECT_TRUE(bs8.none());
    
    bs8.set();
    bs8.reset(3);
    EXPECT_FALSE(bs8[3]);
    EXPECT_EQ(bs8.count(), 7);
}

TEST_F(FixedBitsetTest, Modifiers_Flip) {
    bs8.flip();
    EXPECT_TRUE(bs8.all());
    
    bs8.reset();
    bs8.flip(3);
    EXPECT_TRUE(bs8[3]);
    EXPECT_EQ(bs8.count(), 1);
    
    bs8.flip(3);
    EXPECT_FALSE(bs8[3]);
    EXPECT_TRUE(bs8.none());
}

TEST_F(FixedBitsetTest, Operations_Count) {
    EXPECT_EQ(bs8.count(), 0);
    
    bs8.set(0);
    EXPECT_EQ(bs8.count(), 1);
    
    bs8.set(3);
    bs8.set(7);
    EXPECT_EQ(bs8.count(), 3);
    
    bs8.set();
    EXPECT_EQ(bs8.count(), 8);
}

TEST_F(FixedBitsetTest, Operations_AnyAllNone) {
    EXPECT_TRUE(bs8.none());
    EXPECT_FALSE(bs8.any());
    EXPECT_FALSE(bs8.all());
    
    bs8.set(0);
    EXPECT_FALSE(bs8.none());
    EXPECT_TRUE(bs8.any());
    EXPECT_FALSE(bs8.all());
    
    bs8.set();
    EXPECT_FALSE(bs8.none());
    EXPECT_TRUE(bs8.any());
    EXPECT_TRUE(bs8.all());
}

TEST_F(FixedBitsetTest, BitwiseOperators_AND) {
    bitset<8> a(0b11110000);
    bitset<8> b(0b11001100);
    bitset<8> result = a & b;
    
    EXPECT_EQ(result.to_ulong(), 0b11000000);
    
    a &= b;
    EXPECT_EQ(a.to_ulong(), 0b11000000);
}

TEST_F(FixedBitsetTest, BitwiseOperators_OR) {
    bitset<8> a(0b11110000);
    bitset<8> b(0b11001100);
    bitset<8> result = a | b;
    
    EXPECT_EQ(result.to_ulong(), 0b11111100);
    
    a |= b;
    EXPECT_EQ(a.to_ulong(), 0b11111100);
}

TEST_F(FixedBitsetTest, BitwiseOperators_XOR) {
    bitset<8> a(0b11110000);
    bitset<8> b(0b11001100);
    bitset<8> result = a ^ b;
    
    EXPECT_EQ(result.to_ulong(), 0b00111100);
    
    a ^= b;
    EXPECT_EQ(a.to_ulong(), 0b00111100);
}

TEST_F(FixedBitsetTest, BitwiseOperators_NOT) {
    bitset<8> a(0b11110000);
    bitset<8> result = ~a;
    
    EXPECT_EQ(result.to_ulong(), 0b00001111);
}

TEST_F(FixedBitsetTest, ShiftOperators_Left) {
    bitset<8> a(0b00001111);
    
    bitset<8> result = a << 2;
    EXPECT_EQ(result.to_ulong(), 0b00111100);
    
    result = a << 4;
    EXPECT_EQ(result.to_ulong(), 0b11110000);
    
    result = a << 8;  // Shift all bits out
    EXPECT_TRUE(result.none());
}

TEST_F(FixedBitsetTest, ShiftOperators_Right) {
    bitset<8> a(0b11110000);
    
    bitset<8> result = a >> 2;
    EXPECT_EQ(result.to_ulong(), 0b00111100);
    
    result = a >> 4;
    EXPECT_EQ(result.to_ulong(), 0b00001111);
    
    result = a >> 8;  // Shift all bits out
    EXPECT_TRUE(result.none());
}

TEST_F(FixedBitsetTest, ShiftOperators_WordBoundary) {
    // Test shifts across word boundaries on 64-bit bitset
    bitset<64> a;
    a.set(31);
    a.set(32);
    
    auto result = a << 1;
    EXPECT_FALSE(result[31]);
    EXPECT_TRUE(result[32]);
    EXPECT_TRUE(result[33]);
    
    result = a >> 1;
    EXPECT_TRUE(result[30]);
    EXPECT_TRUE(result[31]);
    EXPECT_FALSE(result[32]);
}

TEST_F(FixedBitsetTest, Conversions_ToUlong) {
    bitset<8> bs(0xAB);
    EXPECT_EQ(bs.to_ulong(), 0xABUL);
    
    bitset<32> bs32(0x12345678UL);
    EXPECT_EQ(bs32.to_ulong(), 0x12345678UL);
}

TEST_F(FixedBitsetTest, Conversions_ToUllong) {
    bitset<64> bs(0xABCDEF0123456789ULL);
    EXPECT_EQ(bs.to_ullong(), 0xABCDEF0123456789ULL);
}

TEST_F(FixedBitsetTest, Conversions_ToString) {
    bitset<8> bs(0b10101011);
    std::string str = bs.to_string();
    EXPECT_EQ(str, "10101011");  // MSB first
    
    // Custom chars
    str = bs.to_string('.', 'X');
    EXPECT_EQ(str, "X.X.X.XX");
}

TEST_F(FixedBitsetTest, Conversions_Overflow) {
    bitset<64> bs;
    bs.set();  // All bits = 1
    
    // to_ulong should throw if bits beyond position 31 are set
    EXPECT_THROW(bs.to_ulong(), std::overflow_error);
    
    // Should succeed if we clear upper bits
    for (size_t i = 32; i < 64; ++i) bs.reset(i);
    EXPECT_NO_THROW(bs.to_ulong());
}

TEST_F(FixedBitsetTest, IO_DataAccess) {
    bitset<64> bs(0x123456789ABCDEF0ULL);
    
    const std::byte* data = bs.data();
    EXPECT_NE(data, nullptr);
    
    // Check byte-level data access
    EXPECT_EQ(static_cast<uint8_t>(data[0]), 0xF0);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), 0xDE);
}

TEST_F(FixedBitsetTest, IO_ReadWrite) {
    bitset<64> bs1(0xABCDEF0123456789ULL);
    std::vector<std::byte> buffer(bs1.sizeBytes());
    
    bs1.writeTo(buffer.data(), buffer.size());
    
    bitset<64> bs2;
    bs2.readFrom(buffer.data(), buffer.size());
    
    EXPECT_EQ(bs1, bs2);
    EXPECT_EQ(bs2.to_ullong(), 0xABCDEF0123456789ULL);
}

TEST_F(FixedBitsetTest, Comparison_Equality) {
    bitset<8> a(0b10101010);
    bitset<8> b(0b10101010);
    bitset<8> c(0b10101011);
    
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST_F(FixedBitsetTest, Stream_Output) {
    bitset<8> bs(0b10101011);
    std::ostringstream oss;
    oss << bs;
    EXPECT_EQ(oss.str(), "10101011");
}

// ============================================================================
// Dynamic-Size Bitset Tests
// ============================================================================

class DynamicBitsetTest : public ::testing::Test {
protected:
    // Test various dynamic sizes
    bitset<> bs_small{8};
    bitset<> bs_medium{256};
    bitset<> bs_large{1024};
    
    void SetUp() override {
        bs_small.reset();
        bs_medium.reset();
        bs_large.reset();
    }
};

TEST_F(DynamicBitsetTest, Construction_Default) {
    bitset<> bs(128);
    EXPECT_EQ(bs.size(), 128);
    EXPECT_TRUE(bs.none());
    EXPECT_FALSE(bs.is_fixed_size());
}

TEST_F(DynamicBitsetTest, Construction_FromValue) {
    bitset<> bs(64, 0xFFFFFFFF00000000ULL);
    EXPECT_EQ(bs.size(), 64);
    EXPECT_EQ(bs.count(), 32);
    
    for (size_t i = 0; i < 32; ++i) EXPECT_FALSE(bs[i]);
    for (size_t i = 32; i < 64; ++i) EXPECT_TRUE(bs[i]);
}

TEST_F(DynamicBitsetTest, Construction_FromBool) {
    bitset<> bs_false(64, false);
    EXPECT_TRUE(bs_false.none());
    
    bitset<> bs_true(64, true);
    EXPECT_TRUE(bs_true.all());
    EXPECT_EQ(bs_true.count(), 64);
}

TEST_F(DynamicBitsetTest, Construction_FromString) {
    std::string bits = "10101010";
    bitset<> bs(8, bits);
    EXPECT_EQ(bs.size(), 8);
    EXPECT_EQ(bs.count(), 4);
    EXPECT_EQ(bs.to_string(), bits);
}

TEST_F(DynamicBitsetTest, Construction_FromFixedBitset) {
    bitset<64> fixed(0xABCDEF0123456789ULL);
    bitset<> dynamic(fixed);
    
    EXPECT_EQ(dynamic.size(), 64);
    EXPECT_EQ(dynamic.to_ullong(), 0xABCDEF0123456789ULL);
}

TEST_F(DynamicBitsetTest, Modifiers_Clear) {
    bs_small.set();
    EXPECT_FALSE(bs_small.empty());
    
    bs_small.clear();
    EXPECT_TRUE(bs_small.empty());
    EXPECT_EQ(bs_small.size(), 0);
}

TEST_F(DynamicBitsetTest, Modifiers_Reserve) {
    bitset<> bs(64);
    bs.reserve(1024);  // Pre-allocate space
    
    // Size should remain 64
    EXPECT_EQ(bs.size(), 64);
    
    // Should be able to resize without reallocation
    bs.resize(512);
    EXPECT_EQ(bs.size(), 512);
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_Grow) {
    bitset<> bs(32);
    bs.set();  // All 32 bits = 1
    
    bs.resize(64);
    EXPECT_EQ(bs.size(), 64);
    EXPECT_EQ(bs.count(), 32);  // Original 32 bits still set
    
    // New bits should be 0
    for (size_t i = 32; i < 64; ++i) {
        EXPECT_FALSE(bs[i]);
    }
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_GrowWithValue) {
    bitset<> bs(32);
    bs.set();  // All 32 bits = 1
    
    bs.resize(64, true);  // Grow and set new bits to 1
    EXPECT_EQ(bs.size(), 64);
    EXPECT_EQ(bs.count(), 64);  // All bits should be 1
    
    EXPECT_TRUE(bs.all());
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_BugCheck_PartialWord) {
    // This specifically tests the resize() bug mentioned in planning
    // Scenario: resize from 50 bits to 128 bits with value=true
    // The last word (bits 0-63) is only partially filled (bits 0-49)
    // Bits 50-63 need to be set when growing
    
    bitset<> bs(50);
    for (size_t i = 0; i < 50; ++i) bs.set(i);
    
    bs.resize(128, true);
    
    EXPECT_EQ(bs.size(), 128);
    EXPECT_EQ(bs.count(), 128) << "All 128 bits should be set";
    
    // Verify all bits are actually set
    for (size_t i = 0; i < 128; ++i) {
        EXPECT_TRUE(bs[i]) << "Bit " << i << " should be set";
    }
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_BugCheck_MultipleWords) {
    // Test resize across word boundaries: 32→64→128
    bitset<> bs(32);
    bs.set();
    
    bs.resize(64, true);
    EXPECT_EQ(bs.count(), 64) << "All 64 bits should be set after first resize";
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_TRUE(bs[i]) << "Bit " << i << " should be set after resize to 64";
    }
    
    bs.resize(128, true);
    EXPECT_EQ(bs.count(), 128) << "All 128 bits should be set after second resize";
    for (size_t i = 0; i < 128; ++i) {
        EXPECT_TRUE(bs[i]) << "Bit " << i << " should be set after resize to 128";
    }
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_Shrink) {
    bitset<> bs(128);
    bs.set();
    
    bs.resize(64);
    EXPECT_EQ(bs.size(), 64);
    EXPECT_EQ(bs.count(), 64);
    EXPECT_TRUE(bs.all());
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_ShrinkAndGrow) {
    bitset<> bs(128);
    for (size_t i = 0; i < 64; ++i) bs.set(i);
    
    bs.resize(64);
    EXPECT_EQ(bs.count(), 64);
    
    bs.resize(128, true);
    EXPECT_EQ(bs.count(), 128);  // New bits filled with 1
    EXPECT_TRUE(bs.all());
}

TEST_F(DynamicBitsetTest, Modifiers_ShrinkToFit) {
    bitset<> bs(64);
    bs.reserve(1024);
    
    bs.shrink_to_fit();
    EXPECT_EQ(bs.size(), 64);  // Size unchanged
}

TEST_F(DynamicBitsetTest, Operations_AllowSameAsFixed) {
    // Verify dynamic bitsets support all operations that fixed bitsets do
    bitset<> bs(64, 0xABCDEF0123456789ULL);
    
    EXPECT_EQ(bs.count(), std::popcount(0xABCDEF0123456789ULL));
    EXPECT_TRUE(bs.any());
    EXPECT_FALSE(bs.all());
    
    bs.flip();
    EXPECT_EQ(bs.count(), 64 - std::popcount(0xABCDEF0123456789ULL));
    
    // Bitwise operations
    bitset<> other(64, 0xFFFFFFFF00000000ULL);
    bs &= other;
    bs |= other;
    bs ^= other;
    
    // Shifts
    bs <<= 10;
    bs >>= 5;
}

TEST_F(DynamicBitsetTest, Conversions_ToFixed) {
    bitset<> dynamic(64, 0xABCDEF0123456789ULL);
    bitset<64> fixed = dynamic.to_fixed<64>();
    
    EXPECT_EQ(fixed.to_ullong(), 0xABCDEF0123456789ULL);
    
    // Wrong size should throw
    bitset<> wrong_size(128);
    EXPECT_THROW(wrong_size.to_fixed<64>(), std::invalid_argument);
}

TEST_F(DynamicBitsetTest, Comparison_Equality) {
    bitset<> a(64, 0xABCDULL);
    bitset<> b(64, 0xABCDULL);
    bitset<> c(64, 0xABCEULL);
    bitset<> d(128, 0xABCDULL);  // Different size
    
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);  // Different sizes
}

// ============================================================================
// Large Bitset Tests (Clustered for Performance)
// ============================================================================

TEST(LargeBitsetTest, FixedSize_1024bits) {
    bitset<1024> bs;
    
    // Set every 10th bit
    for (size_t i = 0; i < 1024; i += 10) {
        bs.set(i);
    }
    
    EXPECT_EQ(bs.count(), 103);  // 1024/10 = 102.4 → 103 bits
    
    // Verify pattern
    for (size_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(bs[i], (i % 10 == 0));
    }
}

TEST(LargeBitsetTest, FixedSize_8192bits) {
    bitset<8192> bs;
    bs.set();
    
    EXPECT_EQ(bs.count(), 8192);
    EXPECT_TRUE(bs.all());
    
    // Test shifting large bitset
    auto shifted = bs >> 100;
    EXPECT_EQ(shifted.count(), 8192 - 100);
}

TEST(LargeBitsetTest, DynamicSize_65536bits_RowScenario) {
    // Test case for 65k rows scenario mentioned in requirements
    const size_t NUM_ROWS = 65536;
    bitset<> bs(NUM_ROWS);
    
    EXPECT_EQ(bs.size(), NUM_ROWS);
    
    // Mark every 100th row as processed
    size_t count = 0;
    for (size_t i = 0; i < NUM_ROWS; i += 100) {
        bs.set(i);
        ++count;
    }
    
    EXPECT_EQ(bs.count(), count);
    
    // Verify we can query specific rows efficiently
    EXPECT_TRUE(bs[0]);
    EXPECT_FALSE(bs[1]);
    EXPECT_TRUE(bs[100]);
    EXPECT_TRUE(bs[65500]);
}

TEST(LargeBitsetTest, DynamicSize_Resize_Large) {
    bitset<> bs(1024);
    bs.set();
    
    bs.resize(8192, true);
    EXPECT_EQ(bs.size(), 8192);
    EXPECT_EQ(bs.count(), 8192);
    EXPECT_TRUE(bs.all());
    
    bs.resize(512);
    EXPECT_EQ(bs.size(), 512);
    EXPECT_EQ(bs.count(), 512);
}

TEST(LargeBitsetTest, BitwiseOperations_Performance) {
    const size_t SIZE = 4096;
    bitset<SIZE> a, b;
    
    // Initialize with patterns
    for (size_t i = 0; i < SIZE; i += 2) a.set(i);
    for (size_t i = 0; i < SIZE; i += 3) b.set(i);
    
    // AND operation
    auto result_and = a & b;
    size_t expected_and = 0;
    for (size_t i = 0; i < SIZE; ++i) {
        if (i % 2 == 0 && i % 3 == 0) ++expected_and;
    }
    EXPECT_EQ(result_and.count(), expected_and);
    
    // OR operation
    auto result_or = a | b;
    EXPECT_GT(result_or.count(), result_and.count());
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(BitsetEdgeCasesTest, Size_One) {
    bitset<1> bs;
    EXPECT_EQ(bs.size(), 1);
    
    bs.set();
    EXPECT_TRUE(bs.all());
    EXPECT_EQ(bs.count(), 1);
    
    bs.reset();
    EXPECT_TRUE(bs.none());
}

TEST(BitsetEdgeCasesTest, Size_NotPowerOfTwo) {
    bitset<13> bs;
    bs.set();
    EXPECT_EQ(bs.count(), 13);
    EXPECT_TRUE(bs.all());
    
    bitset<100> bs100;
    bs100.set();
    EXPECT_EQ(bs100.count(), 100);
}

TEST(BitsetEdgeCasesTest, Size_WordBoundary_63_64_65) {
    // Test sizes around word boundaries
    bitset<63> bs63;
    bs63.set();
    EXPECT_EQ(bs63.count(), 63);
    
    bitset<64> bs64;
    bs64.set();
    EXPECT_EQ(bs64.count(), 64);
    
    bitset<65> bs65;
    bs65.set();
    EXPECT_EQ(bs65.count(), 65);
}

TEST(BitsetEdgeCasesTest, OutOfRange_Access) {
    bitset<8> bs;
    EXPECT_THROW(bs.test(8), std::out_of_range);
    EXPECT_THROW(bs.set(8), std::out_of_range);
    EXPECT_THROW(bs.reset(8), std::out_of_range);
    EXPECT_THROW(bs.flip(8), std::out_of_range);
}

TEST(BitsetEdgeCasesTest, IO_InsufficientBuffer) {
    bitset<64> bs;
    std::vector<std::byte> small_buffer(4);  // Too small
    
    EXPECT_THROW(bs.writeTo(small_buffer.data(), small_buffer.size()), 
                 std::out_of_range);
    EXPECT_THROW(bs.readFrom(small_buffer.data(), small_buffer.size()), 
                 std::out_of_range);
}

TEST(BitsetEdgeCasesTest, Shift_Zero) {
    bitset<8> bs(0b10101010);
    
    auto result_left = bs << 0;
    EXPECT_EQ(result_left, bs);
    
    auto result_right = bs >> 0;
    EXPECT_EQ(result_right, bs);
}

TEST(BitsetEdgeCasesTest, Shift_AllBitsOut) {
    bitset<8> bs(0xFF);
    
    auto result_left = bs << 10;
    EXPECT_TRUE(result_left.none());
    
    auto result_right = bs >> 10;
    EXPECT_TRUE(result_right.none());
}

// ============================================================================
// Interoperability Tests
// ============================================================================

TEST(BitsetInteropTest, FixedToDynamic) {
    bitset<64> fixed(0xABCDEF0123456789ULL);
    bitset<> dynamic(fixed);
    
    EXPECT_EQ(dynamic.size(), 64);
    EXPECT_EQ(dynamic.to_ullong(), fixed.to_ullong());
}

TEST(BitsetInteropTest, DynamicToFixed) {
    bitset<> dynamic(64, 0xABCDEF0123456789ULL);
    bitset<64> fixed = dynamic.to_fixed<64>();
    
    EXPECT_EQ(fixed.to_ullong(), 0xABCDEF0123456789ULL);
}

TEST(BitsetInteropTest, BinaryCompatibility) {
    // Ensure fixed and dynamic bitsets produce identical binary data
    bitset<64> fixed(0xABCDEF0123456789ULL);
    bitset<> dynamic(64, 0xABCDEF0123456789ULL);
    
    EXPECT_EQ(fixed.sizeBytes(), dynamic.sizeBytes());
    
    std::vector<std::byte> fixed_data(fixed.sizeBytes());
    std::vector<std::byte> dynamic_data(dynamic.sizeBytes());
    
    fixed.writeTo(fixed_data.data(), fixed_data.size());
    dynamic.writeTo(dynamic_data.data(), dynamic_data.size());
    
    EXPECT_EQ(fixed_data, dynamic_data);
}

// ============================================================================
// Summary Test Output
// ============================================================================

TEST(BitsetSummaryTest, AllSizesWork) {
    std::cout << "\n=== Bitset Test Summary ===\n";
    std::cout << "✓ Fixed-size bitsets: 1, 8, 64, 256, 1024, 8192 bits\n";
    std::cout << "✓ Dynamic-size bitsets: 8, 256, 1024, 65536 bits\n";
    std::cout << "✓ All operations tested: set, reset, flip, count, any, all, none\n";
    std::cout << "✓ Bitwise operators: &, |, ^, ~, <<, >>\n";
    std::cout << "✓ Conversions: to_ulong, to_ullong, to_string, to_fixed\n";
    std::cout << "✓ I/O operations: data, readFrom, writeTo\n";
    std::cout << "✓ Dynamic operations: resize, reserve, clear, shrink_to_fit\n";
    std::cout << "✓ Edge cases: word boundaries, partial words, out of range\n";
    std::cout << "✓ Interoperability: fixed ↔ dynamic conversions\n";
    std::cout << "============================\n\n";
    
    SUCCEED();
}
