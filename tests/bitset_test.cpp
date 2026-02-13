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
 * @brief Comprehensive tests for unified Bitset implementation (fixed & dynamic)
 * 
 * Tests cover:
 * - Fixed-size bitsets (compile-time size)
 * - Dynamic-size bitsets (runtime size)
 * - Small (1-8 bits), medium (64-256), large (1024-8192), very large (65536) sizes
 * - All operations: set, reset, flip, count, any, all, none
 * - Bitwise operators: &, |, ^, ~, <<, >>
 * - Conversions: toUlong, toUllong, toString
 * - I/O: readFrom, writeTo, data access
 * - Dynamic-only: resize, reserve, clear
 */

#include <gtest/gtest.h>
#include <bcsv/bitset.h>
#include <bitset>
#include <algorithm>
#include <sstream>
#include <vector>
#include <utility>
#include <cstddef>

using namespace bcsv;

namespace {

enum class PatternKind {
    Zeros,
    Ones,
    Alternating,
    EveryThird,
    SingleMid
};

std::vector<bool> MakePattern(size_t size, PatternKind kind) {
    std::vector<bool> pattern(size, false);
    switch (kind) {
        case PatternKind::Zeros:
            break;
        case PatternKind::Ones:
            std::fill(pattern.begin(), pattern.end(), true);
            break;
        case PatternKind::Alternating:
            for (size_t i = 0; i < size; ++i) {
                pattern[i] = (i % 2 == 0);
            }
            break;
        case PatternKind::EveryThird:
            for (size_t i = 0; i < size; ++i) {
                pattern[i] = (i % 3 == 0);
            }
            break;
        case PatternKind::SingleMid:
            if (size > 0) {
                pattern[size / 2] = true;
            }
            break;
    }
    return pattern;
}

std::string ModelToString(const std::vector<bool>& model) {
    std::string result;
    result.reserve(model.size());
    for (size_t i = model.size(); i > 0; --i) {
        result += model[i - 1] ? '1' : '0';
    }
    return result;
}

std::vector<bool> ModelAnd(const std::vector<bool>& lhs, const std::vector<bool>& rhs) {
    const size_t size = lhs.size();
    std::vector<bool> result(size, false);
    for (size_t i = 0; i < size; ++i) {
        const bool a = i < lhs.size() ? lhs[i] : false;
        const bool b = i < rhs.size() ? rhs[i] : false;
        result[i] = a & b;
    }
    return result;
}

std::vector<bool> ModelOr(const std::vector<bool>& lhs, const std::vector<bool>& rhs) {
    const size_t size = lhs.size();
    std::vector<bool> result(size, false);
    for (size_t i = 0; i < size; ++i) {
        const bool a = i < lhs.size() ? lhs[i] : false;
        const bool b = i < rhs.size() ? rhs[i] : false;
        result[i] = a | b;
    }
    return result;
}

std::vector<bool> ModelXor(const std::vector<bool>& lhs, const std::vector<bool>& rhs) {
    const size_t size = lhs.size();
    std::vector<bool> result(size, false);
    for (size_t i = 0; i < size; ++i) {
        const bool a = i < lhs.size() ? lhs[i] : false;
        const bool b = i < rhs.size() ? rhs[i] : false;
        result[i] = a ^ b;
    }
    return result;
}

template<size_t N>
Bitset<N> MakeFixedBitset(const std::vector<bool>& pattern) {
    Bitset<N> bs;
    for (size_t i = 0; i < N; ++i) {
        if (pattern[i]) {
            bs.set(i);
        }
    }
    return bs;
}

Bitset<> MakeDynamicBitset(size_t size, const std::vector<bool>& pattern) {
    Bitset<> bs(size);
    for (size_t i = 0; i < size; ++i) {
        if (pattern[i]) {
            bs.set(i);
        }
    }
    return bs;
}

template<size_t N>
std::bitset<N> MakeStdBitset(const std::vector<bool>& pattern) {
    std::bitset<N> bs;
    for (size_t i = 0; i < N; ++i) {
        if (pattern[i]) {
            bs.set(i);
        }
    }
    return bs;
}

template<size_t N>
void ExpectParityFixed(const Bitset<N>& bcsv_bs, const std::bitset<N>& std_bs) {
    EXPECT_EQ(bcsv_bs.count(), std_bs.count());
    EXPECT_EQ(bcsv_bs.any(), std_bs.any());
    EXPECT_EQ(bcsv_bs.all(), std_bs.all());
    EXPECT_EQ(bcsv_bs.none(), std_bs.none());
    EXPECT_EQ(bcsv_bs.toString(), std_bs.to_string());
    for (size_t i = 0; i < N; ++i) {
        EXPECT_EQ(bcsv_bs[i], std_bs.test(i));
    }
}

template<size_t N>
void ExpectParityDynamic(const Bitset<>& bcsv_bs, const std::bitset<N>& std_bs) {
    EXPECT_EQ(bcsv_bs.size(), N);
    EXPECT_EQ(bcsv_bs.count(), std_bs.count());
    EXPECT_EQ(bcsv_bs.any(), std_bs.any());
    EXPECT_EQ(bcsv_bs.all(), std_bs.all());
    EXPECT_EQ(bcsv_bs.none(), std_bs.none());
    EXPECT_EQ(bcsv_bs.toString(), std_bs.to_string());
    for (size_t i = 0; i < N; ++i) {
        EXPECT_EQ(bcsv_bs[i], std_bs.test(i));
    }
}

void ExpectMatchesModel(const Bitset<>& bcsv_bs, const std::vector<bool>& model) {
    EXPECT_EQ(bcsv_bs.size(), model.size());
    size_t expected_count = 0;
    for (size_t i = 0; i < model.size(); ++i) {
        if (model[i]) {
            ++expected_count;
        }
        EXPECT_EQ(bcsv_bs[i], model[i]);
    }
    EXPECT_EQ(bcsv_bs.count(), expected_count);
    EXPECT_EQ(bcsv_bs.any(), expected_count > 0);
    EXPECT_EQ(bcsv_bs.none(), expected_count == 0);
    const bool expected_all = (model.size() == 0) || (expected_count == model.size());
    EXPECT_EQ(bcsv_bs.all(), expected_all);
    EXPECT_EQ(bcsv_bs.toString(), ModelToString(model));
}

template<size_t N>
void RunParityForSize() {
    const auto pattern_zero = MakePattern(N, PatternKind::Zeros);
    const auto pattern_one = MakePattern(N, PatternKind::Ones);
    const auto pattern_a = MakePattern(N, PatternKind::Alternating);
    const auto pattern_b = MakePattern(N, PatternKind::EveryThird);
    const auto pattern_mid = MakePattern(N, PatternKind::SingleMid);

    const auto fixed_zero = MakeFixedBitset<N>(pattern_zero);
    const auto std_zero = MakeStdBitset<N>(pattern_zero);
    ExpectParityFixed(fixed_zero, std_zero);

    const auto fixed_one = MakeFixedBitset<N>(pattern_one);
    const auto std_one = MakeStdBitset<N>(pattern_one);
    ExpectParityFixed(fixed_one, std_one);

    const auto fixed_a = MakeFixedBitset<N>(pattern_a);
    const auto fixed_b = MakeFixedBitset<N>(pattern_b);
    const auto fixed_mid = MakeFixedBitset<N>(pattern_mid);
    const auto std_a = MakeStdBitset<N>(pattern_a);
    const auto std_b = MakeStdBitset<N>(pattern_b);
    const auto std_mid = MakeStdBitset<N>(pattern_mid);

    ExpectParityFixed(fixed_a, std_a);
    ExpectParityFixed(fixed_mid, std_mid);

    ExpectParityFixed(fixed_a & fixed_b, std_a & std_b);
    ExpectParityFixed(fixed_a | fixed_b, std_a | std_b);
    ExpectParityFixed(fixed_a ^ fixed_b, std_a ^ std_b);
    ExpectParityFixed(~fixed_a, ~std_a);

    std::vector<size_t> shifts = {0, 1, 2, 3, 7, 8, 15, 31, 63, 64, 65};
    if (N > 0) {
        shifts.push_back(N - 1);
    }
    shifts.push_back(N);
    shifts.push_back(N + 1);
    std::sort(shifts.begin(), shifts.end());
    shifts.erase(std::unique(shifts.begin(), shifts.end()), shifts.end());

    for (size_t shift : shifts) {
        ExpectParityFixed(fixed_a << shift, std_a << shift);
        ExpectParityFixed(fixed_a >> shift, std_a >> shift);

        auto fixed_left = fixed_a;
        auto std_left = std_a;
        fixed_left <<= shift;
        std_left <<= shift;
        ExpectParityFixed(fixed_left, std_left);

        auto fixed_right = fixed_a;
        auto std_right = std_a;
        fixed_right >>= shift;
        std_right >>= shift;
        ExpectParityFixed(fixed_right, std_right);
    }

    const auto dynamic_a = MakeDynamicBitset(N, pattern_a);
    const auto dynamic_b = MakeDynamicBitset(N, pattern_b);
    const auto dynamic_mid = MakeDynamicBitset(N, pattern_mid);

    ExpectParityDynamic(dynamic_a, std_a);
    ExpectParityDynamic(dynamic_mid, std_mid);
    ExpectParityDynamic(dynamic_a & dynamic_b, std_a & std_b);
    ExpectParityDynamic(dynamic_a | dynamic_b, std_a | std_b);
    ExpectParityDynamic(dynamic_a ^ dynamic_b, std_a ^ std_b);
    ExpectParityDynamic(~dynamic_a, ~std_a);

    for (size_t shift : shifts) {
        ExpectParityDynamic(dynamic_a << shift, std_a << shift);
        ExpectParityDynamic(dynamic_a >> shift, std_a >> shift);

        auto dyn_left = dynamic_a;
        auto std_left = std_a;
        dyn_left <<= shift;
        std_left <<= shift;
        ExpectParityDynamic(dyn_left, std_left);

        auto dyn_right = dynamic_a;
        auto std_right = std_a;
        dyn_right >>= shift;
        std_right >>= shift;
        ExpectParityDynamic(dyn_right, std_right);
    }
}

template<size_t... Ns>
void RunParitySweep(std::index_sequence<Ns...>) {
    (RunParityForSize<Ns>(), ...);
}

}  // namespace

// ============================================================================
// Fixed-Size Bitset Tests
// ============================================================================

class FixedBitsetTest : public ::testing::Test {
protected:
    // Test various fixed sizes
    Bitset<1> bs1;
    Bitset<8> bs8;
    Bitset<64> bs64;
    Bitset<256> bs256;
    
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
    Bitset<8> bs_val(0xAB);  // 10101011
    EXPECT_EQ(bs_val.count(), 5);
    EXPECT_TRUE(bs_val[0]);
    EXPECT_TRUE(bs_val[1]);
    EXPECT_FALSE(bs_val[2]);
    EXPECT_TRUE(bs_val[3]);
    EXPECT_TRUE(bs_val[5]);
    EXPECT_TRUE(bs_val[7]);
    
    Bitset<64> bs64_val(0xFFFFFFFF00000000ULL);
    EXPECT_EQ(bs64_val.count(), 32);
    for (size_t i = 0; i < 32; ++i) EXPECT_FALSE(bs64_val[i]);
    for (size_t i = 32; i < 64; ++i) EXPECT_TRUE(bs64_val[i]);
}

TEST_F(FixedBitsetTest, Construction_FromString) {
    Bitset<8> bs(std::string("10101011"));  // MSB first
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

TEST_F(FixedBitsetTest, Reference_CompoundAssignment) {
    // Test operator|= (bitwise OR)
    bs8.reset();
    bs8.set(0);
    EXPECT_TRUE(bs8[0]);
    
    bs8[0] |= false;  // true |= false => true
    EXPECT_TRUE(bs8[0]);
    
    bs8[1] |= true;   // false |= true => true
    EXPECT_TRUE(bs8[1]);
    
    bs8[1] |= false;  // true |= false => true
    EXPECT_TRUE(bs8[1]);
    
    // Test operator&= (bitwise AND)
    bs8.set();
    EXPECT_TRUE(bs8[0]);
    
    bs8[0] &= true;   // true &= true => true
    EXPECT_TRUE(bs8[0]);
    
    bs8[0] &= false;  // true &= false => false
    EXPECT_FALSE(bs8[0]);
    
    bs8[1] &= false;  // true &= false => false
    EXPECT_FALSE(bs8[1]);
    
    // Test operator^= (bitwise XOR)
    bs8.reset();
    bs8.set(0);
    
    bs8[0] ^= false;  // true ^= false => true
    EXPECT_TRUE(bs8[0]);
    
    bs8[0] ^= true;   // true ^= true => false
    EXPECT_FALSE(bs8[0]);
    
    bs8[1] ^= true;   // false ^= true => true
    EXPECT_TRUE(bs8[1]);
    
    bs8[1] ^= false;  // true ^= false => true
    EXPECT_TRUE(bs8[1]);
    
    // Test that it works with variables (like in Row::visit)
    bs8.reset();
    bool changed = true;
    bs8[0] |= changed;
    EXPECT_TRUE(bs8[0]);
    
    changed = false;
    bs8[1] |= changed;
    EXPECT_FALSE(bs8[1]);
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
    Bitset<8> a(0b11110000);
    Bitset<8> b(0b11001100);
    Bitset<8> result = a & b;
    
    EXPECT_EQ(result.toUlong(), 0b11000000);
    
    a &= b;
    EXPECT_EQ(a.toUlong(), 0b11000000);
}

TEST_F(FixedBitsetTest, BitwiseOperators_OR) {
    Bitset<8> a(0b11110000);
    Bitset<8> b(0b11001100);
    Bitset<8> result = a | b;
    
    EXPECT_EQ(result.toUlong(), 0b11111100);
    
    a |= b;
    EXPECT_EQ(a.toUlong(), 0b11111100);
}

TEST_F(FixedBitsetTest, BitwiseOperators_XOR) {
    Bitset<8> a(0b11110000);
    Bitset<8> b(0b11001100);
    Bitset<8> result = a ^ b;
    
    EXPECT_EQ(result.toUlong(), 0b00111100);
    
    a ^= b;
    EXPECT_EQ(a.toUlong(), 0b00111100);
}

TEST_F(FixedBitsetTest, BitwiseOperators_NOT) {
    Bitset<8> a(0b11110000);
    Bitset<8> result = ~a;
    
    EXPECT_EQ(result.toUlong(), 0b00001111);
}

TEST_F(FixedBitsetTest, ShiftOperators_Left) {
    Bitset<8> a(0b00001111);
    
    Bitset<8> result = a << 2;
    EXPECT_EQ(result.toUlong(), 0b00111100);
    
    result = a << 4;
    EXPECT_EQ(result.toUlong(), 0b11110000);
    
    result = a << 8;  // Shift all bits out
    EXPECT_TRUE(result.none());
}

TEST_F(FixedBitsetTest, ShiftOperators_Right) {
    Bitset<8> a(0b11110000);
    
    Bitset<8> result = a >> 2;
    EXPECT_EQ(result.toUlong(), 0b00111100);
    
    result = a >> 4;
    EXPECT_EQ(result.toUlong(), 0b00001111);
    
    result = a >> 8;  // Shift all bits out
    EXPECT_TRUE(result.none());
}

TEST_F(FixedBitsetTest, ShiftOperators_WordBoundary) {
    // Test shifts across word boundaries on 64-bit Bitset
    Bitset<64> a;
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
    Bitset<8> bs(0xAB);
    EXPECT_EQ(bs.toUlong(), 0xABUL);
    
    Bitset<32> bs32(0x12345678UL);
    EXPECT_EQ(bs32.toUlong(), 0x12345678UL);
}

TEST_F(FixedBitsetTest, Conversions_ToUllong) {
    Bitset<64> bs(0xABCDEF0123456789ULL);
    EXPECT_EQ(bs.toUllong(), 0xABCDEF0123456789ULL);
}

TEST_F(FixedBitsetTest, Conversions_ToString) {
    Bitset<8> bs(0b10101011);
    std::string str = bs.toString();
    EXPECT_EQ(str, "10101011");  // MSB first
    
    // Custom chars
    str = bs.toString('.', 'X');
    EXPECT_EQ(str, "X.X.X.XX");
}

TEST_F(FixedBitsetTest, Conversions_Overflow) {
    Bitset<64> bs;
    bs.set();  // All bits = 1
    
    // toUlong should throw if bits beyond position 31 are set
    EXPECT_THROW(bs.toUlong(), std::overflow_error);
    
    // Should succeed if we clear upper bits
    for (size_t i = 32; i < 64; ++i) bs.reset(i);
    EXPECT_NO_THROW(bs.toUlong());
}

TEST_F(FixedBitsetTest, IO_DataAccess) {
    Bitset<64> bs(0x123456789ABCDEF0ULL);
    
    const std::byte* data = bs.data();
    EXPECT_NE(data, nullptr);
    
    // Check byte-level data access
    EXPECT_EQ(static_cast<uint8_t>(data[0]), 0xF0);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), 0xDE);
}

TEST_F(FixedBitsetTest, IO_ReadWrite) {
    Bitset<64> bs1(0xABCDEF0123456789ULL);
    std::vector<std::byte> buffer(bs1.sizeBytes());
    
    bs1.writeTo(buffer.data(), buffer.size());
    
    Bitset<64> bs2;
    bs2.readFrom(buffer.data(), buffer.size());
    
    EXPECT_EQ(bs1, bs2);
    EXPECT_EQ(bs2.toUllong(), 0xABCDEF0123456789ULL);
}

TEST_F(FixedBitsetTest, Comparison_Equality) {
    Bitset<8> a(0b10101010);
    Bitset<8> b(0b10101010);
    Bitset<8> c(0b10101011);
    
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST_F(FixedBitsetTest, Stream_Output) {
    Bitset<8> bs(0b10101011);
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
    Bitset<> bs_small{8};
    Bitset<> bs_medium{256};
    Bitset<> bs_large{1024};
    
    void SetUp() override {
        bs_small.reset();
        bs_medium.reset();
        bs_large.reset();
    }
};

TEST_F(DynamicBitsetTest, Construction_Default) {
    Bitset<> bs(128);
    EXPECT_EQ(bs.size(), 128);
    EXPECT_TRUE(bs.none());
    EXPECT_FALSE(bs.isFixedSize());
}

TEST_F(DynamicBitsetTest, Construction_FromValue) {
    Bitset<> bs(64, 0xFFFFFFFF00000000ULL);
    EXPECT_EQ(bs.size(), 64);
    EXPECT_EQ(bs.count(), 32);
    
    for (size_t i = 0; i < 32; ++i) EXPECT_FALSE(bs[i]);
    for (size_t i = 32; i < 64; ++i) EXPECT_TRUE(bs[i]);
}

TEST_F(DynamicBitsetTest, Construction_FromBool) {
    Bitset<> bs_false(64, false);
    EXPECT_TRUE(bs_false.none());
    
    Bitset<> bs_true(64, true);
    EXPECT_TRUE(bs_true.all());
    EXPECT_EQ(bs_true.count(), 64);
}

TEST_F(DynamicBitsetTest, Construction_FromString) {
    std::string bits = "10101010";
    Bitset<> bs(8, bits);
    EXPECT_EQ(bs.size(), 8);
    EXPECT_EQ(bs.count(), 4);
    EXPECT_EQ(bs.toString(), bits);
}

TEST_F(DynamicBitsetTest, Construction_FromFixedBitset) {
    Bitset<64> fixed(0xABCDEF0123456789ULL);
    Bitset<> dynamic(fixed);
    
    EXPECT_EQ(dynamic.size(), 64);
    EXPECT_EQ(dynamic.toUllong(), 0xABCDEF0123456789ULL);
}

TEST_F(DynamicBitsetTest, Reference_CompoundAssignment) {
    // Test operator|= (bitwise OR)
    bs_small.reset();
    bs_small.set(0);
    EXPECT_TRUE(bs_small[0]);
    
    bs_small[0] |= false;
    EXPECT_TRUE(bs_small[0]);
    
    bs_small[1] |= true;
    EXPECT_TRUE(bs_small[1]);
    
    // Test operator&= (bitwise AND)
    bs_small.set();
    bs_small[0] &= true;
    EXPECT_TRUE(bs_small[0]);
    
    bs_small[0] &= false;
    EXPECT_FALSE(bs_small[0]);
    
    // Test operator^= (bitwise XOR)
    bs_small.reset();
    bs_small[0] ^= true;
    EXPECT_TRUE(bs_small[0]);
    
    bs_small[0] ^= true;
    EXPECT_FALSE(bs_small[0]);
    
    // Test with variable (like in Row::visit)
    bool changed = true;
    bs_small[2] |= changed;
    EXPECT_TRUE(bs_small[2]);
    
    // Test across word boundaries (bit 64+)
    bs_medium[64] = false;
    bs_medium[64] |= true;
    EXPECT_TRUE(bs_medium[64]);
}

TEST_F(DynamicBitsetTest, Modifiers_Clear) {
    bs_small.set();
    EXPECT_FALSE(bs_small.empty());
    
    bs_small.clear();
    EXPECT_TRUE(bs_small.empty());
    EXPECT_EQ(bs_small.size(), 0);
}

TEST_F(DynamicBitsetTest, Modifiers_Reserve) {
    Bitset<> bs(64);
    bs.reserve(1024);  // Pre-allocate space
    
    // Size should remain 64
    EXPECT_EQ(bs.size(), 64);
    
    // Should be able to resize without reallocation
    bs.resize(512);
    EXPECT_EQ(bs.size(), 512);
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_Grow) {
    Bitset<> bs(32);
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
    Bitset<> bs(32);
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
    
    Bitset<> bs(50);
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
    Bitset<> bs(32);
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
    Bitset<> bs(128);
    bs.set();
    
    bs.resize(64);
    EXPECT_EQ(bs.size(), 64);
    EXPECT_EQ(bs.count(), 64);
    EXPECT_TRUE(bs.all());
}

TEST_F(DynamicBitsetTest, Modifiers_Resize_ShrinkAndGrow) {
    Bitset<> bs(128);
    for (size_t i = 0; i < 64; ++i) bs.set(i);
    
    bs.resize(64);
    EXPECT_EQ(bs.count(), 64);
    
    bs.resize(128, true);
    EXPECT_EQ(bs.count(), 128);  // New bits filled with 1
    EXPECT_TRUE(bs.all());
}

TEST_F(DynamicBitsetTest, Modifiers_ShrinkToFit) {
    Bitset<> bs(64);
    bs.reserve(1024);
    
    bs.shrinkToFit();
    EXPECT_EQ(bs.size(), 64);  // Size unchanged
}

TEST_F(DynamicBitsetTest, Modifiers_Insert_EmptyToGrown) {
    Bitset<> bs(0);
    std::vector<bool> model;

    auto insert_and_check = [&](size_t pos, bool value) {
        bs.insert(pos, value);
        model.insert(model.begin() + static_cast<std::ptrdiff_t>(pos), value);
        ExpectMatchesModel(bs, model);
    };

    insert_and_check(0, true);
    insert_and_check(1, false);
    insert_and_check(1, true);
    insert_and_check(0, false);
    insert_and_check(bs.size(), true);
}

TEST_F(DynamicBitsetTest, Modifiers_Insert_BoundariesAndMiddle) {
    Bitset<> bs(63);
    std::vector<bool> model(63, false);

    bs.set(0);
    bs.set(31);
    bs.set(62);
    model[0] = true;
    model[31] = true;
    model[62] = true;

    ExpectMatchesModel(bs, model);

    auto insert_and_check = [&](size_t pos, bool value) {
        bs.insert(pos, value);
        model.insert(model.begin() + static_cast<std::ptrdiff_t>(pos), value);
        ExpectMatchesModel(bs, model);
    };

    insert_and_check(0, true);
    insert_and_check(32, false);
    insert_and_check(64, true);
    insert_and_check(bs.size() / 2, true);
}

// ============================================================================
// Erase Tests
// ============================================================================

TEST_F(DynamicBitsetTest, Modifiers_Erase_SingleElement) {
    // Erase from a 1-element bitset
    Bitset<> bs(1);
    bs.set(0);
    EXPECT_EQ(bs.size(), 1u);
    EXPECT_TRUE(bs[0]);

    bs.erase(0);
    EXPECT_EQ(bs.size(), 0u);
}

TEST_F(DynamicBitsetTest, Modifiers_Erase_Front) {
    // Erase first element, remaining bits shift down
    Bitset<> bs(8);
    std::vector<bool> model(8, false);
    // Set pattern: 1 0 1 1 0 0 1 0
    for (size_t i : {0, 2, 3, 6}) { bs.set(i); model[i] = true; }
    ExpectMatchesModel(bs, model);

    bs.erase(0);
    model.erase(model.begin());
    ExpectMatchesModel(bs, model);
}

TEST_F(DynamicBitsetTest, Modifiers_Erase_Back) {
    // Erase last element
    Bitset<> bs(8);
    std::vector<bool> model(8, false);
    for (size_t i : {0, 2, 3, 6}) { bs.set(i); model[i] = true; }

    bs.erase(7);
    model.erase(model.begin() + 7);
    ExpectMatchesModel(bs, model);
}

TEST_F(DynamicBitsetTest, Modifiers_Erase_Middle) {
    // Erase from middle — tests carry chain across same word
    Bitset<> bs(8);
    std::vector<bool> model(8, false);
    for (size_t i : {0, 2, 3, 6}) { bs.set(i); model[i] = true; }

    bs.erase(4);
    model.erase(model.begin() + 4);
    ExpectMatchesModel(bs, model);
}

TEST_F(DynamicBitsetTest, Modifiers_Erase_WordBoundary) {
    // Erase at position 63 in a 128-bit bitset — tests carry from word 1 to word 0
    Bitset<> bs(128);
    std::vector<bool> model(128, false);
    // Set bits near the boundary
    for (size_t i : {62, 63, 64, 65}) { bs.set(i); model[i] = true; }
    ExpectMatchesModel(bs, model);

    bs.erase(63);
    model.erase(model.begin() + 63);
    ExpectMatchesModel(bs, model);
    // After erase: old bit 64 (true) should now be at position 63
    EXPECT_TRUE(bs[63]);
}

TEST_F(DynamicBitsetTest, Modifiers_Erase_MultipleSequential) {
    // Repeated erase to drain a bitset, comparing against model at each step
    Bitset<> bs(65);
    std::vector<bool> model(65, false);
    // Set alternating bits
    for (size_t i = 0; i < 65; i += 2) { bs.set(i); model[i] = true; }
    ExpectMatchesModel(bs, model);

    // Erase from various positions
    auto erase_and_check = [&](size_t pos) {
        bs.erase(pos);
        model.erase(model.begin() + static_cast<std::ptrdiff_t>(pos));
        ExpectMatchesModel(bs, model);
    };

    erase_and_check(0);            // front
    erase_and_check(bs.size() - 1); // back
    erase_and_check(32);           // near word boundary
    erase_and_check(bs.size() / 2); // middle
}

TEST_F(DynamicBitsetTest, Modifiers_Erase_AllOnes) {
    // Erase from a bitset with all bits set — carry chain must propagate 1s correctly
    Bitset<> bs(130);
    std::vector<bool> model(130, true);
    for (size_t i = 0; i < 130; ++i) bs.set(i);
    ExpectMatchesModel(bs, model);

    bs.erase(64);  // word boundary
    model.erase(model.begin() + 64);
    ExpectMatchesModel(bs, model);
    EXPECT_EQ(bs.count(), 129u);
}

// ============================================================================
// PushBack Tests
// ============================================================================

TEST_F(DynamicBitsetTest, Modifiers_PushBack_GrowFromEmpty) {
    Bitset<> bs(0);
    std::vector<bool> model;

    for (size_t i = 0; i < 130; ++i) {
        bool val = (i % 3 == 0);
        bs.pushBack(val);
        model.push_back(val);
        ExpectMatchesModel(bs, model);
    }
}

TEST_F(DynamicBitsetTest, Modifiers_PushBack_AppendToExisting) {
    Bitset<> bs(64, 0xDEADBEEF12345678ULL);
    std::vector<bool> model(64, false);
    for (size_t i = 0; i < 64; ++i) {
        model[i] = (0xDEADBEEF12345678ULL >> i) & 1;
    }
    ExpectMatchesModel(bs, model);

    // Push some values across the word boundary
    bs.pushBack(true);   model.push_back(true);
    bs.pushBack(false);  model.push_back(false);
    bs.pushBack(true);   model.push_back(true);
    ExpectMatchesModel(bs, model);
    EXPECT_EQ(bs.size(), 67u);
}

TEST_F(DynamicBitsetTest, Modifiers_EraseAndPushBack_RoundTrip) {
    // Insert then erase, pushBack then erase — verify model consistency
    Bitset<> bs(0);
    std::vector<bool> model;

    // Build up with pushBack
    for (int i = 0; i < 20; ++i) {
        bs.pushBack(i & 1);
        model.push_back(i & 1);
    }
    ExpectMatchesModel(bs, model);

    // Erase from middle several times
    for (int i = 0; i < 5; ++i) {
        size_t pos = bs.size() / 2;
        bs.erase(pos);
        model.erase(model.begin() + static_cast<std::ptrdiff_t>(pos));
    }
    ExpectMatchesModel(bs, model);
    EXPECT_EQ(bs.size(), 15u);

    // Push back more
    for (int i = 0; i < 10; ++i) {
        bs.pushBack(!(i & 1));
        model.push_back(!(i & 1));
    }
    ExpectMatchesModel(bs, model);
    EXPECT_EQ(bs.size(), 25u);
}

TEST_F(DynamicBitsetTest, Operations_AllowSameAsFixed) {
    // Verify dynamic bitsets support all operations that fixed bitsets do
    Bitset<> bs(64, 0xABCDEF0123456789ULL);
    
    EXPECT_EQ(bs.count(), std::popcount(0xABCDEF0123456789ULL));
    EXPECT_TRUE(bs.any());
    EXPECT_FALSE(bs.all());
    
    bs.flip();
    EXPECT_EQ(bs.count(), 64 - std::popcount(0xABCDEF0123456789ULL));
    
    // Bitwise operations
    Bitset<> other(64, 0xFFFFFFFF00000000ULL);
    bs &= other;
    bs |= other;
    bs ^= other;
    
    // Shifts
    bs <<= 10;
    bs >>= 5;
}

TEST_F(DynamicBitsetTest, Conversions_ToFixed) {
    Bitset<> dynamic(64, 0xABCDEF0123456789ULL);
    Bitset<64> fixed = dynamic.toFixed<64>();
    
    EXPECT_EQ(fixed.toUllong(), 0xABCDEF0123456789ULL);
    
    // Wrong size should throw
    Bitset<> wrong_size(128);
    EXPECT_THROW(wrong_size.toFixed<64>(), std::invalid_argument);
}

TEST_F(DynamicBitsetTest, Comparison_Equality) {
    Bitset<> a(64, 0xABCDULL);
    Bitset<> b(64, 0xABCDULL);
    Bitset<> c(64, 0xABCEULL);
    Bitset<> d(128, 0xABCDULL);  // Different size
    
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);  // Different sizes
}

TEST(BitsetParityTest, FixedAndDynamicMatchStd_ZeroTo130) {
    RunParitySweep(std::make_index_sequence<131>{});
}

TEST(BitsetCombinedOpsTest, FixedAndDynamicCombinedExpressions) {
    constexpr size_t kFixedSize = 64;
    const auto pattern_a = MakePattern(kFixedSize, PatternKind::Alternating);
    const auto pattern_b = MakePattern(kFixedSize, PatternKind::EveryThird);
    const auto pattern_c = MakePattern(kFixedSize, PatternKind::SingleMid);
    const auto pattern_d = MakePattern(kFixedSize, PatternKind::Ones);

    auto a = MakeFixedBitset<kFixedSize>(pattern_a);
    auto b = MakeFixedBitset<kFixedSize>(pattern_b);
    auto c = MakeFixedBitset<kFixedSize>(pattern_c);
    auto d = MakeFixedBitset<kFixedSize>(pattern_d);

    auto std_a = MakeStdBitset<kFixedSize>(pattern_a);
    auto std_b = MakeStdBitset<kFixedSize>(pattern_b);
    auto std_c = MakeStdBitset<kFixedSize>(pattern_c);
    auto std_d = MakeStdBitset<kFixedSize>(pattern_d);

    c |= (a & ~b) | d;
    std_c |= (std_a & ~std_b) | std_d;
    ExpectParityFixed(c, std_c);

    constexpr size_t kDynamicSize = 100;
    const auto dyn_a = MakeDynamicBitset(kDynamicSize, MakePattern(kDynamicSize, PatternKind::Alternating));
    const auto dyn_b = MakeDynamicBitset(kDynamicSize, MakePattern(kDynamicSize, PatternKind::EveryThird));
    auto dyn_c = MakeDynamicBitset(kDynamicSize, MakePattern(kDynamicSize, PatternKind::SingleMid));
    const auto dyn_d = MakeDynamicBitset(kDynamicSize, MakePattern(kDynamicSize, PatternKind::Ones));

    auto std_dyn_a = MakeStdBitset<kDynamicSize>(MakePattern(kDynamicSize, PatternKind::Alternating));
    auto std_dyn_b = MakeStdBitset<kDynamicSize>(MakePattern(kDynamicSize, PatternKind::EveryThird));
    auto std_dyn_c = MakeStdBitset<kDynamicSize>(MakePattern(kDynamicSize, PatternKind::SingleMid));
    auto std_dyn_d = MakeStdBitset<kDynamicSize>(MakePattern(kDynamicSize, PatternKind::Ones));

    dyn_c |= (dyn_a & ~dyn_b) | dyn_d;
    std_dyn_c |= (std_dyn_a & ~std_dyn_b) | std_dyn_d;
    ExpectParityDynamic(dyn_c, std_dyn_c);
}

TEST(BitsetSizeMismatchTest, DynamicBitwiseTruncatesToLhsSize) {
    const auto model_a = MakePattern(10, PatternKind::Alternating);
    const auto model_b = MakePattern(64, PatternKind::EveryThird);
    const auto model_c = MakePattern(96, PatternKind::SingleMid);

    const auto a = MakeDynamicBitset(model_a.size(), model_a);
    const auto b = MakeDynamicBitset(model_b.size(), model_b);
    const auto c = MakeDynamicBitset(model_c.size(), model_c);

    const auto expected_and = ModelAnd(model_a, model_b);
    const auto expected_or = ModelOr(model_a, model_b);
    const auto expected_xor = ModelXor(model_a, model_b);

    ExpectMatchesModel(a & b, expected_and);
    ExpectMatchesModel(a | b, expected_or);
    ExpectMatchesModel(a ^ b, expected_xor);

    auto lhs_or = a;
    lhs_or |= b;
    ExpectMatchesModel(lhs_or, expected_or);

    auto lhs_xor = a;
    lhs_xor ^= b;
    ExpectMatchesModel(lhs_xor, expected_xor);

    auto lhs_and = b;
    lhs_and &= c;
    ExpectMatchesModel(lhs_and, ModelAnd(model_b, model_c));
}

TEST(BitsetMaskOpsTest, FixedAndDynamicMaskedQueries) {
    Bitset<64> fixed_a;
    Bitset<64> fixed_mask;

    fixed_a.reset();
    fixed_mask.reset();
    fixed_mask.set(1);
    fixed_mask.set(5);
    fixed_mask.set(63);

    fixed_a.set(1);
    fixed_a.set(5);
    fixed_a.set(63);

    EXPECT_TRUE(fixed_a.any(fixed_mask));
    EXPECT_TRUE(fixed_a.all(fixed_mask));

    fixed_a.reset(5);
    EXPECT_TRUE(fixed_a.any(fixed_mask));
    EXPECT_FALSE(fixed_a.all(fixed_mask));

    Bitset<> dyn_a(64);
    dyn_a.reset();
    dyn_a.set(2);
    dyn_a.set(7);

    Bitset<> dyn_mask_same(64);
    dyn_mask_same.reset();
    dyn_mask_same.set(2);
    dyn_mask_same.set(7);

    EXPECT_TRUE(dyn_a.any(dyn_mask_same));
    EXPECT_TRUE(dyn_a.all(dyn_mask_same));

    Bitset<> dyn_mask_small(23);
    dyn_mask_small.reset();
    dyn_mask_small.set(2);
    dyn_mask_small.set(7);

    EXPECT_TRUE(dyn_a.any(dyn_mask_small));
    EXPECT_TRUE(dyn_a.all(dyn_mask_small));

    Bitset<> dyn_mask_large(96);
    dyn_mask_large.reset();
    dyn_mask_large.set(2);
    dyn_mask_large.set(7);
    dyn_mask_large.set(80);

    EXPECT_TRUE(dyn_a.any(dyn_mask_large));
    EXPECT_TRUE(dyn_a.all(dyn_mask_large));

    dyn_a.reset(7);
    EXPECT_TRUE(dyn_a.any(dyn_mask_small));
    EXPECT_FALSE(dyn_a.all(dyn_mask_small));
    EXPECT_TRUE(dyn_a.any(dyn_mask_large));
    EXPECT_FALSE(dyn_a.all(dyn_mask_large));
}

TEST(BitsetSliceTest, FixedSliceReadWrite) {
    Bitset<16> bs;
    bs.reset();
    bs.set(4);
    bs.set(7);

    auto slice = bs.slice(4, 6);
    EXPECT_EQ(slice.size(), 6u);
    EXPECT_TRUE(slice[0]);
    EXPECT_TRUE(slice[3]);

    slice.reset(0);
    slice.set(1, true);
    EXPECT_FALSE(bs[4]);
    EXPECT_TRUE(bs[5]);
}

TEST(BitsetSliceTest, DynamicSliceOpsAndMasking) {
    Bitset<> bs(16);
    bs.reset();
    bs.set(4);
    bs.set(8);
    bs.set(9);

    auto slice = bs.slice(4, 6);
    EXPECT_TRUE(slice.any());
    EXPECT_FALSE(slice.all());

    Bitset<> mask_same(6);
    mask_same.reset();
    mask_same.set(0);
    mask_same.set(4);
    EXPECT_TRUE(slice.any(mask_same));
    EXPECT_TRUE(slice.all(mask_same));

    Bitset<> mask_small(3);
    mask_small.reset();
    mask_small.set(0);
    EXPECT_TRUE(slice.any(mask_small));
    EXPECT_TRUE(slice.all(mask_small));

    Bitset<> mask_large(12);
    mask_large.reset();
    mask_large.set(0);
    mask_large.set(4);
    mask_large.set(10);  // ignored
    EXPECT_TRUE(slice.any(mask_large));
    EXPECT_TRUE(slice.all(mask_large));

    Bitset<> rhs(6);
    rhs.reset();
    rhs.set(1);
    rhs.set(4);
    slice &= rhs;
    EXPECT_FALSE(bs[4]);
    EXPECT_TRUE(bs[8]);
    EXPECT_FALSE(bs[9]);

    slice.reset();
    slice.set(0, true);
    slice.set(1, true);
    slice <<= 2;
    EXPECT_FALSE(bs[4]);
    EXPECT_FALSE(bs[5]);
    EXPECT_TRUE(bs[6]);
    EXPECT_TRUE(bs[7]);

    auto compact = slice.toBitset();
    EXPECT_EQ(compact.size(), 6u);
    EXPECT_TRUE(compact[2]);
    EXPECT_TRUE(compact[3]);

    auto shifted_left = slice.shiftedLeft(1);
    EXPECT_EQ(shifted_left.size(), 6u);
    EXPECT_TRUE(shifted_left[3]);
    EXPECT_TRUE(shifted_left[4]);
}

// ============================================================================
// Large Bitset Tests (Clustered for Performance)
// ============================================================================

TEST(LargeBitsetTest, FixedSize_1024bits) {
    Bitset<1024> bs;
    
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
    Bitset<8192> bs;
    bs.set();
    
    EXPECT_EQ(bs.count(), 8192);
    EXPECT_TRUE(bs.all());
    
    // Test shifting large Bitset
    auto shifted = bs >> 100;
    EXPECT_EQ(shifted.count(), 8192 - 100);
}

TEST(LargeBitsetTest, DynamicSize_65536bits_RowScenario) {
    // Test case for 65k rows scenario mentioned in requirements
    const size_t NUM_ROWS = 65536;
    Bitset<> bs(NUM_ROWS);
    
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
    Bitset<> bs(1024);
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
    Bitset<SIZE> a, b;
    
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
    Bitset<1> bs;
    EXPECT_EQ(bs.size(), 1);
    
    bs.set();
    EXPECT_TRUE(bs.all());
    EXPECT_EQ(bs.count(), 1);
    
    bs.reset();
    EXPECT_TRUE(bs.none());
}

TEST(BitsetEdgeCasesTest, Size_NotPowerOfTwo) {
    Bitset<13> bs;
    bs.set();
    EXPECT_EQ(bs.count(), 13);
    EXPECT_TRUE(bs.all());
    
    Bitset<100> bs100;
    bs100.set();
    EXPECT_EQ(bs100.count(), 100);
}

TEST(BitsetEdgeCasesTest, Size_WordBoundary_63_64_65) {
    // Test sizes around word boundaries
    Bitset<63> bs63;
    bs63.set();
    EXPECT_EQ(bs63.count(), 63);
    
    Bitset<64> bs64;
    bs64.set();
    EXPECT_EQ(bs64.count(), 64);
    
    Bitset<65> bs65;
    bs65.set();
    EXPECT_EQ(bs65.count(), 65);
}

TEST(BitsetEdgeCasesTest, Size_WordBoundary_127_128_129) {
    Bitset<127> bs127;
    bs127.set();
    EXPECT_EQ(bs127.count(), 127);
    EXPECT_TRUE(bs127.all());

    Bitset<128> bs128;
    bs128.set();
    EXPECT_EQ(bs128.count(), 128);
    EXPECT_TRUE(bs128.all());

    Bitset<129> bs129;
    bs129.set();
    EXPECT_EQ(bs129.count(), 129);
    EXPECT_TRUE(bs129.all());
}

TEST(BitsetEdgeCasesTest, OutOfRange_Access) {
    Bitset<8> bs;
    EXPECT_THROW(bs.test(8), std::out_of_range);
    EXPECT_THROW(bs.set(8), std::out_of_range);
    EXPECT_THROW(bs.reset(8), std::out_of_range);
    EXPECT_THROW(bs.flip(8), std::out_of_range);
}

TEST(BitsetEdgeCasesTest, IO_InsufficientBuffer) {
    Bitset<64> bs;
    std::vector<std::byte> small_buffer(4);  // Too small
    
    EXPECT_THROW(bs.writeTo(small_buffer.data(), small_buffer.size()), 
                 std::out_of_range);
    EXPECT_THROW(bs.readFrom(small_buffer.data(), small_buffer.size()), 
                 std::out_of_range);
}

TEST(BitsetEdgeCasesTest, Shift_Zero) {
    Bitset<8> bs(0b10101010);
    
    auto result_left = bs << 0;
    EXPECT_EQ(result_left, bs);
    
    auto result_right = bs >> 0;
    EXPECT_EQ(result_right, bs);
}

TEST(BitsetEdgeCasesTest, Shift_AllBitsOut) {
    Bitset<8> bs(0xFF);
    
    auto result_left = bs << 10;
    EXPECT_TRUE(result_left.none());
    
    auto result_right = bs >> 10;
    EXPECT_TRUE(result_right.none());
}

// ============================================================================
// Interoperability Tests
// ============================================================================

TEST(BitsetInteropTest, FixedToDynamic) {
    Bitset<64> fixed(0xABCDEF0123456789ULL);
    Bitset<> dynamic(fixed);
    
    EXPECT_EQ(dynamic.size(), 64);
    EXPECT_EQ(dynamic.toUllong(), fixed.toUllong());
}

TEST(BitsetInteropTest, DynamicToFixed) {
    Bitset<> dynamic(64, 0xABCDEF0123456789ULL);
    Bitset<64> fixed = dynamic.toFixed<64>();
    
    EXPECT_EQ(fixed.toUllong(), 0xABCDEF0123456789ULL);
}

TEST(BitsetInteropTest, BinaryCompatibility) {
    // Ensure fixed and dynamic bitsets produce identical binary data
    Bitset<64> fixed(0xABCDEF0123456789ULL);
    Bitset<> dynamic(64, 0xABCDEF0123456789ULL);
    
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
    std::cout << "✓ Conversions: toUlong, toUllong, toString, toFixed\n";
    std::cout << "✓ I/O operations: data, readFrom, writeTo\n";
    std::cout << "✓ Dynamic operations: resize, reserve, clear, shrinkToFit, insert, erase, pushBack\n";
    std::cout << "✓ Edge cases: word boundaries, partial words, out of range\n";
    std::cout << "✓ Std::Bitset parity: sizes 0-130, shifts, bitwise ops\n";
    std::cout << "✓ Interoperability: fixed ↔ dynamic conversions\n";
    std::cout << "============================\n\n";
    
    SUCCEED();
}
