// Copyright (c) 2025-2026 Tobias Weber
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "bcsv/vle.hpp"
#include <sstream>
#include <limits>
#include <vector>

using namespace bcsv;

// Test uint8_t trivial encoding (no VLE overhead)
TEST(VLETemplateTest, Uint8Trivial) {
    std::stringstream ss;
    
    EXPECT_EQ(vleEncode<uint8_t>(0, ss), 1);
    EXPECT_EQ(vleEncode<uint8_t>(127, ss), 1);
    EXPECT_EQ(vleEncode<uint8_t>(255, ss), 1);
    
    ss.seekg(0);
    uint8_t val;
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, 0);
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, 127);
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, 255);
}

// Test int8_t trivial encoding with zigzag/cast
TEST(VLETemplateTest, Int8Trivial) {
    std::stringstream ss;
    
    EXPECT_EQ(vleEncode<int8_t>(0, ss), 1);
    EXPECT_EQ(vleEncode<int8_t>(-1, ss), 1);
    EXPECT_EQ(vleEncode<int8_t>(127, ss), 1);
    EXPECT_EQ(vleEncode<int8_t>(-128, ss), 1);
    
    ss.seekg(0);
    int8_t val;
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, 0);
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, -1);
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, 127);
    EXPECT_EQ(vleDecode(ss, val), 1); EXPECT_EQ(val, -128);
}

// Test uint16_t encoding (Full mode: 2 bits len -> max 3 bytes)
TEST(VLETemplateTest, Uint16Full) {
    std::stringstream ss;
    
    // 0-63: 1 byte (6 bits data)
    EXPECT_EQ((vleEncode<uint16_t, false>(0, ss)), 1);
    EXPECT_EQ((vleEncode<uint16_t, false>(63, ss)), 1);

    // 64-16383: 2 bytes
    EXPECT_EQ((vleEncode<uint16_t, false>(64, ss)), 2);
    EXPECT_EQ((vleEncode<uint16_t, false>(16383, ss)), 2);
    
    // 16384+: 3 bytes
    EXPECT_EQ((vleEncode<uint16_t, false>(16384, ss)), 3);
    EXPECT_EQ((vleEncode<uint16_t, false>(65535, ss)), 3);
    
    ss.seekg(0);
    uint16_t val;
    (vleDecode<uint16_t, false>(ss, val)); EXPECT_EQ(val, 0);
    (vleDecode<uint16_t, false>(ss, val)); EXPECT_EQ(val, 63);
    (vleDecode<uint16_t, false>(ss, val)); EXPECT_EQ(val, 64);
    (vleDecode<uint16_t, false>(ss, val)); EXPECT_EQ(val, 16383);
    (vleDecode<uint16_t, false>(ss, val)); EXPECT_EQ(val, 16384);
    (vleDecode<uint16_t, false>(ss, val)); EXPECT_EQ(val, 65535);
}

// Test uint16_t encoding (Truncated mode: 1 bit len -> max 2 bytes)
TEST(VLETemplateTest, Uint16Truncated) {
    std::stringstream ss;
    
    // 0-127: 1 byte (7 bits data)
    EXPECT_EQ((vleEncode<uint16_t, true>(0, ss)), 1);
    EXPECT_EQ((vleEncode<uint16_t, true>(127, ss)), 1);

    // 128-32767: 2 bytes
    EXPECT_EQ((vleEncode<uint16_t, true>(128, ss)), 2);
    EXPECT_EQ((vleEncode<uint16_t, true>(32767, ss)), 2);
    
    // > 32767: Overflow!
    uint8_t buf[16];
    EXPECT_THROW((vleEncode<uint16_t, true>(32768, buf, 16)), std::overflow_error);
    
    ss.seekg(0);
    uint16_t val;
    (vleDecode<uint16_t, true>(ss, val)); EXPECT_EQ(val, 0);
    (vleDecode<uint16_t, true>(ss, val)); EXPECT_EQ(val, 127);
    (vleDecode<uint16_t, true>(ss, val)); EXPECT_EQ(val, 128);
    (vleDecode<uint16_t, true>(ss, val)); EXPECT_EQ(val, 32767);
}

// Test generic API with buffer (uint16 Full)
TEST(VLETemplateTest, BufferAPI) {
    uint8_t buf[8];
    // Encode 63 as uint16 Full (1 byte: (63 << 2) | 0 = 252 (0xFC))
    size_t sz = vleEncode<uint16_t>(63, buf, 8);
    EXPECT_EQ(sz, 1);
    EXPECT_EQ(buf[0], 0xFC); 
    
    // Encode 127 as uint16 Full (2 bytes)
    // 127 = 01111111
    // Encoded: (127 << 2) | 1 = 509 = 0x1FD
    // Little endian: FD 01
    sz = vleEncode<uint16_t>(127, buf, 8);
    EXPECT_EQ(sz, 2);
    EXPECT_EQ(buf[0], 0xFD);
    EXPECT_EQ(buf[1], 0x01);
    
    // Decode
    uint16_t val;
    size_t consumed = vleDecode<uint16_t>(val, buf, 8);
    EXPECT_EQ(consumed, 2);
    EXPECT_EQ(val, 127);
}

// Test ByteBuffer API
TEST(VLETemplateTest, ByteBufferAPI) {
    ByteBuffer bb;
    vleEncode<uint32_t>(123456, bb);
    // uint32 Full -> 3 bits len.
    // 123456 fits in 5+8+8 = 21 bits? No.
    // Data bits capacity:
    // 1 byte: 5 bits (31)
    // 2 bytes: 13 bits (8191)
    // 3 bytes: 21 bits (2097151)
    // 123456 <= 2097151 -> 3 bytes.
    EXPECT_EQ(bb.size(), 3);
    
    // Decode using span
    std::span<std::byte> sp(bb.data(), bb.size());
    uint32_t val = vleDecode<uint32_t>(sp);
    EXPECT_EQ(val, 123456);
    EXPECT_EQ(sp.size(), 0); 
}

// Test Large Values (uint64 Full -> 4 bits len)
TEST(VLETemplateTest, LargeValues) {
    std::stringstream ss;
    // uint64 Full supports full 64 bits?
    // 9 bytes: 4 bits header -> 4 data bits + 8*8 = 68 bits. Yes.
    
    uint64_t huge = 1ULL << 63; // Max bit
    vleEncode<uint64_t>(huge, ss); // Should take 9 bytes
    
    ss.seekg(0);
    uint64_t val;
    vleDecode<uint64_t>(ss, val);
    EXPECT_EQ(val, huge);
}

// Test Overflow
TEST(VLETemplateTest, Overflow) {
    // uint16 truncated max is 32767
    uint8_t buf[16];
    EXPECT_THROW((vleEncode<uint16_t, true>(32768, buf, 16)), std::overflow_error);
    
    // uint64 truncated max
    // 8 bytes. 3 bits len. 
    // Max bits = 5 + 7*8 = 61.
    uint64_t tooBig = (1ULL << 62);
    EXPECT_THROW((vleEncode<uint64_t, true>(tooBig, buf, 16)), std::overflow_error);
}
