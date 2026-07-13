/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 *
 * Direct unit tests for the shared CLI tool headers:
 *   src/tools/type_probe.h  — ColumnProbeState, cellLoses, coerce
 *   src/tools/spec_parse.h  — parseTypeSpec, parseNameSpec, resolveColumnKey,
 *                             parseColumnSelection
 * (Previously this logic lived in bcsvCast.cpp and was only reachable
 * through the binary.)
 */

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "type_probe.h"
#include "spec_parse.h"

using bcsv::ColumnType;
using bcsv_cli::ColumnProbeState;
using bcsv_cli::CsvColumnProbe;
using bcsv_cli::cellLoses;
using bcsv_cli::coerce;
using bcsv_cli::doubleToIntForced;
using bcsv_cli::parseColumnSelection;
using bcsv_cli::parseNameSpec;
using bcsv_cli::parseTypeSpec;
using bcsv_cli::resolveColumnKey;
using bcsv_cli::satToInt;

// ════════════════════════════════════════════════════════════════════
// ColumnProbeState
// ════════════════════════════════════════════════════════════════════

TEST(ColumnProbe, SignedLadderNarrowsByRange) {
    ColumnProbeState p;
    p.init(ColumnType::INT64, 0, false, 0.0);
    p.visitIntegerSigned(-5);
    p.visitIntegerSigned(100);
    EXPECT_EQ(p.optimal_type, ColumnType::INT8);
    p.visitIntegerSigned(-200);
    EXPECT_EQ(p.optimal_type, ColumnType::INT16);
    p.visitIntegerSigned(-40000);
    EXPECT_EQ(p.optimal_type, ColumnType::INT32);
    p.visitIntegerSigned(std::numeric_limits<int64_t>::min());
    EXPECT_EQ(p.optimal_type, ColumnType::INT64);
}

TEST(ColumnProbe, SignedAllPositiveNarrowsToUnsigned) {
    ColumnProbeState p;
    p.init(ColumnType::INT64, 0, false, 0.0);
    p.visitIntegerSigned(0);
    p.visitIntegerSigned(200);
    EXPECT_EQ(p.optimal_type, ColumnType::UINT8);
    p.visitIntegerSigned(70000);
    EXPECT_EQ(p.optimal_type, ColumnType::UINT32);
}

TEST(ColumnProbe, NoSameWidthSignFlip) {
    // INT32 column with positive values that fit uint32 but not uint16:
    // flipping INT32→UINT32 saves nothing, keep INT32.
    ColumnProbeState p;
    p.init(ColumnType::INT32, 0, false, 0.0);
    p.visitIntegerSigned(3000000000LL % INT32_MAX);  // ~ 852516352, fits uint32 & int32
    EXPECT_EQ(p.optimal_type, ColumnType::INT32);
}

TEST(ColumnProbe, BoolFromZeroOneIntegers) {
    ColumnProbeState p;
    p.init(ColumnType::INT32, 0, false, 0.0);
    p.visitIntegerSigned(0);
    p.visitIntegerSigned(1);
    EXPECT_EQ(p.optimal_type, ColumnType::BOOL);
}

TEST(ColumnProbe, UnsignedLadder) {
    ColumnProbeState p;
    p.init(ColumnType::UINT64, 0, false, 0.0);
    p.visitIntegerUnsigned(3);
    EXPECT_EQ(p.optimal_type, ColumnType::UINT8);
    p.visitIntegerUnsigned(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(p.optimal_type, ColumnType::UINT64);
}

TEST(ColumnProbe, FloatWholeValuesNarrowToInt) {
    ColumnProbeState p;
    p.init(ColumnType::DOUBLE, 0, false, 0.0);
    p.visitFloat(3.0);
    p.visitFloat(250.0);
    EXPECT_EQ(p.optimal_type, ColumnType::UINT8);
    p.visitFloat(-2.0);
    EXPECT_EQ(p.optimal_type, ColumnType::INT16);
}

TEST(ColumnProbe, FloatFractionalRoundTripsToFloat) {
    ColumnProbeState p;
    p.init(ColumnType::DOUBLE, 0, false, 0.0);
    p.visitFloat(1.5);       // exact in float
    EXPECT_EQ(p.optimal_type, ColumnType::FLOAT);
    p.visitFloat(0.1);       // 0.1 does not survive double→float→double
    EXPECT_EQ(p.optimal_type, ColumnType::DOUBLE);
}

TEST(ColumnProbe, FloatToleranceEnablesLossyNarrowing) {
    ColumnProbeState p;
    p.init(ColumnType::DOUBLE, 0, false, 1e-6);
    p.visitFloat(0.1);       // within 1e-6 of its float round-trip
    EXPECT_EQ(p.optimal_type, ColumnType::FLOAT);
}

TEST(ColumnProbe, NonFiniteKillsIntAndBoolLadders) {
    ColumnProbeState p;
    p.init(ColumnType::DOUBLE, 0, false, 0.0);
    p.visitFloat(std::numeric_limits<double>::quiet_NaN());
    p.visitFloat(1.0);
    EXPECT_EQ(p.optimal_type, ColumnType::FLOAT);  // NaN round-trips; int/bool dead
}

TEST(ColumnProbe, LadderDiesAtTwoPow63Boundary) {
    ColumnProbeState p;
    p.init(ColumnType::DOUBLE, 0, false, 0.0);
    p.visitFloat(9223372036854775808.0);  // exactly 2^63 — must not become int64
    EXPECT_NE(p.optimal_type, ColumnType::INT64);
    EXPECT_NE(p.optimal_type, ColumnType::UINT64);
}

TEST(ColumnProbe, StringProbeNumericAndBailout) {
    ColumnProbeState p;
    p.init(ColumnType::STRING, 0, /*probeStrings=*/true, 0.0);
    p.visitString("42");
    EXPECT_EQ(p.optimal_type, ColumnType::UINT8);
    p.visitString("not a number");
    EXPECT_EQ(p.optimal_type, ColumnType::STRING);
    EXPECT_TRUE(p.str_done);
    p.checkStabilization();
    EXPECT_FALSE(p.alive);
}

TEST(ColumnProbe, StringProbeWhitespacePreserved) {
    // Leading/trailing whitespace means the string is not a clean numeric.
    ColumnProbeState p;
    p.init(ColumnType::STRING, 0, true, 0.0);
    p.visitString(" 42");
    EXPECT_EQ(p.optimal_type, ColumnType::STRING);
}

// ════════════════════════════════════════════════════════════════════
// cellLoses / coerce edges
// ════════════════════════════════════════════════════════════════════

TEST(CellLoses, IntegerRangeChecks) {
    uint64_t oor = 0, unp = 0;
    EXPECT_FALSE(cellLoses<int64_t>(ColumnType::INT8, int64_t(127), 0.0, oor, unp));
    EXPECT_TRUE(cellLoses<int64_t>(ColumnType::INT8, int64_t(128), 0.0, oor, unp));
    EXPECT_EQ(oor, 1u);
    EXPECT_TRUE(cellLoses<int64_t>(ColumnType::UINT8, int64_t(-1), 0.0, oor, unp));
    EXPECT_EQ(oor, 2u);
    EXPECT_EQ(unp, 0u);
}

TEST(CellLoses, IntToFloatMantissa) {
    uint64_t oor = 0, unp = 0;
    EXPECT_FALSE(cellLoses<int64_t>(ColumnType::FLOAT, int64_t(16777216), 0.0, oor, unp));   // 2^24
    EXPECT_TRUE(cellLoses<int64_t>(ColumnType::FLOAT, int64_t(16777217), 0.0, oor, unp));    // 2^24+1
    EXPECT_FALSE(cellLoses<int64_t>(ColumnType::DOUBLE, int64_t(1) << 53, 0.0, oor, unp));
    EXPECT_TRUE(cellLoses<int64_t>(ColumnType::DOUBLE, (int64_t(1) << 53) + 1, 0.0, oor, unp));
}

TEST(CellLoses, DoubleAtExactTwoPow63IsOutOfRange) {
    uint64_t oor = 0, unp = 0;
    EXPECT_TRUE(cellLoses<double>(ColumnType::INT64, 9223372036854775808.0, 0.0, oor, unp));
    EXPECT_EQ(oor, 1u);
    EXPECT_TRUE(cellLoses<double>(ColumnType::UINT64, 18446744073709551616.0, 0.0, oor, unp));
    EXPECT_EQ(oor, 2u);
    // One below the boundary is fine.
    EXPECT_FALSE(cellLoses<double>(ColumnType::INT64, 9223372036854774784.0, 0.0, oor, unp));
}

TEST(CellLoses, StringCells) {
    uint64_t oor = 0, unp = 0;
    EXPECT_FALSE(cellLoses<std::string>(ColumnType::INT32, std::string("123"), 0.0, oor, unp));
    EXPECT_TRUE(cellLoses<std::string>(ColumnType::INT32, std::string("abc"), 0.0, oor, unp));
    EXPECT_EQ(unp, 1u);
    EXPECT_TRUE(cellLoses<std::string>(ColumnType::INT32, std::string(""), 0.0, oor, unp));
    EXPECT_EQ(unp, 2u);
}

TEST(Coerce, SaturatesAndCounts) {
    uint64_t clamped = 0;
    auto v = coerce<int64_t>(ColumnType::INT8, int64_t(300), 0.0, clamped);
    EXPECT_EQ(std::get<int8_t>(v), 127);
    EXPECT_EQ(clamped, 1u);
    v = coerce<int64_t>(ColumnType::UINT8, int64_t(-5), 0.0, clamped);
    EXPECT_EQ(std::get<uint8_t>(v), 0);   // negative signed → unsigned min
    EXPECT_EQ(clamped, 2u);
}

TEST(Coerce, DoubleToIntNaNAndInf) {
    uint64_t clamped = 0;
    auto v = doubleToIntForced(ColumnType::INT32, std::numeric_limits<double>::quiet_NaN(), 0.0, clamped);
    EXPECT_EQ(std::get<int32_t>(v), 0);
    EXPECT_EQ(clamped, 1u);
    v = doubleToIntForced(ColumnType::INT32, std::numeric_limits<double>::infinity(), 0.0, clamped);
    EXPECT_EQ(std::get<int32_t>(v), std::numeric_limits<int32_t>::max());
    EXPECT_EQ(clamped, 2u);
}

TEST(Coerce, WithinToleranceRoundingNotCounted) {
    uint64_t clamped = 0;
    auto v = doubleToIntForced(ColumnType::INT32, 5.0000001, 1e-3, clamped);
    EXPECT_EQ(std::get<int32_t>(v), 5);
    EXPECT_EQ(clamped, 0u);
}

// ════════════════════════════════════════════════════════════════════
// spec_parse.h — parseTypeSpec / parseNameSpec / selections
// ════════════════════════════════════════════════════════════════════

static const std::vector<std::string> NAMES = {"time", "temp", "count", "note"};

TEST(SpecParse, MapFormIndicesAndRanges) {
    auto r = parseTypeSpec("0=int32, 2:3=string", 4);
    ASSERT_TRUE(r[0] && r[2] && r[3]);
    EXPECT_FALSE(r[1].has_value());
    EXPECT_EQ(*r[0], ColumnType::INT32);
    EXPECT_EQ(*r[2], ColumnType::STRING);
    EXPECT_EQ(*r[3], ColumnType::STRING);
}

TEST(SpecParse, MapFormNegativeIndex) {
    auto r = parseTypeSpec("-1=bool", 4);
    ASSERT_TRUE(r[3].has_value());
    EXPECT_EQ(*r[3], ColumnType::BOOL);
}

TEST(SpecParse, MapFormNameKeys) {
    auto r = parseTypeSpec("temp=float,count=uint32", 4, NAMES);
    EXPECT_EQ(*r[1], ColumnType::FLOAT);
    EXPECT_EQ(*r[2], ColumnType::UINT32);
    EXPECT_FALSE(r[0].has_value());
}

TEST(SpecParse, UnknownNameThrows) {
    EXPECT_THROW(parseTypeSpec("pressure=float", 4, NAMES), std::invalid_argument);
}

TEST(SpecParse, NameKeyWithoutNamesThrows) {
    EXPECT_THROW(parseTypeSpec("temp=float", 4), std::invalid_argument);
}

TEST(SpecParse, AmbiguousNameThrows) {
    std::vector<std::string> dup = {"x", "x"};
    EXPECT_THROW(parseTypeSpec("x=int32", 2, dup), std::invalid_argument);
}

TEST(SpecParse, NumericLookingKeyIsAlwaysIndex) {
    // A column literally named "2" must be addressed by its index, not its name.
    std::vector<std::string> names = {"a", "2", "c"};
    auto r = parseTypeSpec("2=int32", 3, names);
    EXPECT_TRUE(r[2].has_value());   // index 2, NOT the column named "2" (index 1)
    EXPECT_FALSE(r[1].has_value());
}

TEST(SpecParse, DuplicateAssignmentThrows) {
    EXPECT_THROW(parseTypeSpec("0=int32,0=float", 4), std::invalid_argument);
    EXPECT_THROW(parseTypeSpec("0:2=int32,1=float", 4), std::invalid_argument);
    EXPECT_THROW(parseTypeSpec("temp=float,1=int32", 4, NAMES), std::invalid_argument);
}

TEST(SpecParse, ListFormExactCount) {
    auto r = parseTypeSpec("int32,float,uint8,string", 4);
    EXPECT_EQ(*r[0], ColumnType::INT32);
    EXPECT_EQ(*r[3], ColumnType::STRING);
    EXPECT_THROW(parseTypeSpec("int32,float", 4), std::invalid_argument);
    EXPECT_THROW(parseTypeSpec("int32,float,uint8,string,bool", 4), std::invalid_argument);
}

TEST(SpecParse, ListFormAuto) {
    auto r = parseTypeSpec("int32,auto,auto,string", 4, {}, false, /*allow_auto=*/true);
    EXPECT_TRUE(r[0].has_value());
    EXPECT_FALSE(r[1].has_value());
    EXPECT_FALSE(r[2].has_value());
    // 'auto' rejected when not allowed
    EXPECT_THROW(parseTypeSpec("int32,auto,auto,string", 4), std::invalid_argument);
}

TEST(SpecParse, MapFormAuto) {
    auto r = parseTypeSpec("0=auto,1=int32", 4, {}, false, /*allow_auto=*/true);
    EXPECT_FALSE(r[0].has_value());
    EXPECT_EQ(*r[1], ColumnType::INT32);
}

TEST(SpecParse, MixedFormsThrow) {
    EXPECT_THROW(parseTypeSpec("0=int32,float", 4), std::invalid_argument);
}

TEST(SpecParse, EmptyAndStrayComma) {
    EXPECT_THROW(parseTypeSpec("", 4), std::invalid_argument);
    EXPECT_THROW(parseTypeSpec("0=int32,,1=float", 4), std::invalid_argument);
}

TEST(SpecParse, BracesStripped) {
    auto r = parseTypeSpec("{0=int32}", 4);
    EXPECT_EQ(*r[0], ColumnType::INT32);
}

TEST(NameSpec, MapAndListForms) {
    auto r = parseNameSpec("0=timestamp,-1=comment", 4, NAMES);
    EXPECT_EQ(*r[0], "timestamp");
    EXPECT_EQ(*r[3], "comment");
    EXPECT_FALSE(r[1].has_value());

    auto l = parseNameSpec("a,b,c,d", 4);
    EXPECT_EQ(*l[1], "b");
}

TEST(NameSpec, RenameByOldName) {
    auto r = parseNameSpec("temp=temperature", 4, NAMES);
    EXPECT_EQ(*r[1], "temperature");
}

TEST(NameSpec, RangeKeyRejected) {
    // A range cannot receive a single name.
    EXPECT_THROW(parseNameSpec("0:2=x", 4), std::invalid_argument);
}

TEST(NameSpec, EmptyNameRejected) {
    EXPECT_THROW(parseNameSpec("0=", 4), std::invalid_argument);
    EXPECT_THROW(parseNameSpec("a,,c,d", 4), std::invalid_argument);
}

TEST(ColumnSelection, MixedNamesAndRanges) {
    auto sel = parseColumnSelection("time,2:3", 4, NAMES).toIndices(4);
    EXPECT_EQ(sel, (std::vector<size_t>{0, 2, 3}));
    auto neg = parseColumnSelection("-1", 4, NAMES).toIndices(4);
    EXPECT_EQ(neg, (std::vector<size_t>{3}));
}

TEST(ColumnSelection, OverlapsMerge) {
    auto sel = parseColumnSelection("0:2,1:3", 4, {}).toIndices(4);
    EXPECT_EQ(sel, (std::vector<size_t>{0, 1, 2, 3}));
}

TEST(ResolveColumnKey, IndexExprAndName) {
    EXPECT_EQ(resolveColumnKey("1:2", 4, NAMES), (std::vector<size_t>{1, 2}));
    EXPECT_EQ(resolveColumnKey("note", 4, NAMES), (std::vector<size_t>{3}));
    EXPECT_THROW(resolveColumnKey("9", 4, NAMES), std::exception);  // out of range stays an index error
}

// ── Regressions from the 2026-07-13 review ──────────────────────────

TEST(ColumnProbe, TextualBoolDoesNotSettleWhileBoolLadderAlive) {
    // Regression: settled() treated textual_bool as terminal, so the probe
    // stopped after the first "true" and a later non-bool value made the
    // whole conversion abort instead of widening to STRING.
    CsvColumnProbe p;
    std::string scratch;
    p.init(0.0);
    p.visit("true", '.', scratch);
    EXPECT_FALSE(p.settled());   // BOOL can still widen
    EXPECT_EQ(p.derive(), ColumnType::BOOL);
    p.visit("false", '.', scratch);
    EXPECT_EQ(p.derive(), ColumnType::BOOL);
    p.visit("hello", '.', scratch);
    EXPECT_EQ(p.derive(), ColumnType::STRING);
    EXPECT_TRUE(p.settled());    // now genuinely terminal
    EXPECT_FALSE(p.overflowWarning());
}

TEST(ColumnProbe, TextualBoolThenNumericIsString) {
    // "true" cannot be stored numerically, so mixing it with numbers → STRING.
    CsvColumnProbe p;
    std::string scratch;
    p.init(0.0);
    p.visit("true", '.', scratch);
    p.visit("5", '.', scratch);
    EXPECT_EQ(p.derive(), ColumnType::STRING);
    EXPECT_FALSE(p.overflowWarning());
}

TEST(IndexRanges, RejectsTrailingJunkStrictly) {
    // Regression: std::stoll parsed '1+2' as 1 and '5-' as 5, silently
    // assigning the wrong column via SPEC keys and --cols.
    EXPECT_THROW(bcsv_cli::parseIndexRanges("1+2", 10), std::exception);
    EXPECT_THROW(bcsv_cli::parseIndexRanges("5-", 10), std::exception);
    EXPECT_THROW(bcsv_cli::parseIndexRanges("3:x", 10), std::exception);
    EXPECT_THROW(resolveColumnKey("1+2", 10, {}), std::exception);
    // Legitimate forms still parse.
    EXPECT_EQ(bcsv_cli::parseIndexRanges("1", 10).toIndices(10),
              (std::vector<size_t>{1}));
    EXPECT_EQ(bcsv_cli::parseIndexRanges("-2:", 10).toIndices(10),
              (std::vector<size_t>{8, 9}));
}
