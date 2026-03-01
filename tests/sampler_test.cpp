/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file sampler_test.cpp
 * @brief Sampler unit tests — validates all 36 test vectors from the plan.
 *
 * Each test writes the canonical 7-row dataset to a temporary BCSV file,
 * opens it with a Reader, wraps with a Sampler, configures conditional /
 * selection / mode, calls bulk() and verifies against the plan's expected
 * output rows.
 *
 * Canonical dataset (7 rows × 5 columns):
 *   timestamp(DOUBLE)  temperature(FLOAT)  status(STRING)  flags(UINT16)  counter(INT32)
 *   1.0                20.5                "ok"            0x06           0
 *   2.0                21.0                "ok"            0x07           1
 *   3.0                21.0                "warn"          0x03           2
 *   4.0                55.0                "alarm"         0x05           3
 *   5.0                55.0                "alarm"         0x05           100
 *   6.0                22.0                "ok"            0x07           101
 *   7.0                22.5                "ok"            0x06           102
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cmath>

#include <bcsv/bcsv.h>
#include <bcsv/sampler.h>
#include <bcsv/sampler.hpp>

using namespace bcsv;
namespace fs = std::filesystem;

// ============================================================================
// Fixture — creates the canonical dataset as a temp BCSV file
// ============================================================================

class SamplerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = (fs::temp_directory_path() / "bcsv_sampler_test"
                     / (std::string(info->test_suite_name()) + "_" + info->name())).string();
        fs::create_directories(test_dir_);
        data_file_ = testFile("canonical.bcsv");
        writeCanonicalDataset();
    }

    void TearDown() override {
        fs::remove_all(test_dir_);
    }

    std::string testFile(const std::string& name) {
        return test_dir_ + "/" + name;
    }

    Layout canonicalLayout() {
        Layout layout;
        layout.addColumn({"timestamp",   ColumnType::DOUBLE});
        layout.addColumn({"temperature", ColumnType::FLOAT});
        layout.addColumn({"status",      ColumnType::STRING});
        layout.addColumn({"flags",       ColumnType::UINT16});
        layout.addColumn({"counter",     ColumnType::INT32});
        return layout;
    }

    void writeCanonicalDataset() {
        Layout layout = canonicalLayout();
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(data_file_, true));

        struct RowData {
            double ts; float temp; std::string status; uint16_t flags; int32_t counter;
        };
        std::vector<RowData> data = {
            {1.0, 20.5f, "ok",    0x06, 0},
            {2.0, 21.0f, "ok",    0x07, 1},
            {3.0, 21.0f, "warn",  0x03, 2},
            {4.0, 55.0f, "alarm", 0x05, 3},
            {5.0, 55.0f, "alarm", 0x05, 100},
            {6.0, 22.0f, "ok",    0x07, 101},
            {7.0, 22.5f, "ok",    0x06, 102},
        };

        for (auto& d : data) {
            writer.row().set(0, d.ts);
            writer.row().set(1, d.temp);
            writer.row().set(2, d.status);
            writer.row().set(3, d.flags);
            writer.row().set(4, d.counter);
            writer.writeRow();
        }
        writer.close();
    }

    /// Open a reader and create a sampler with the given expressions.
    /// Returns the bulk() result rows.
    std::vector<Row> runSampler(
        const std::string& conditional,
        const std::string& selection = "",
        SamplerMode mode = SamplerMode::TRUNCATE)
    {
        Reader<Layout> reader;
        reader.open(data_file_);
        Sampler<Layout> sampler(reader);
        sampler.setMode(mode);

        auto cond_result = sampler.setConditional(conditional);
        EXPECT_TRUE(cond_result.success) << "Conditional compile error: " << cond_result.error_msg;

        if (!selection.empty()) {
            auto sel_result = sampler.setSelection(selection);
            EXPECT_TRUE(sel_result.success) << "Selection compile error: " << sel_result.error_msg;
        }

        return sampler.bulk();
    }

    /// Tolerance for floating-point comparisons
    static constexpr double kEps = 1e-4;

    std::string test_dir_;
    std::string data_file_;
};

// ============================================================================
// TV-01: Baseline — true / wildcard → all 7 rows
// ============================================================================
TEST_F(SamplerTest, TV01_True_Wildcard_AllRows) {
    auto rows = runSampler("true", "X[0][*]");
    ASSERT_EQ(rows.size(), 7u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[6].get<double>(0), 7.0);
}

// ============================================================================
// TV-02: Baseline — false → 0 rows
// ============================================================================
TEST_F(SamplerTest, TV02_False_ZeroRows) {
    auto rows = runSampler("false", "X[0][*]");
    EXPECT_EQ(rows.size(), 0u);
}

// ============================================================================
// TV-03: Threshold — temperature > 50
// ============================================================================
TEST_F(SamplerTest, TV03_FloatThreshold) {
    auto rows = runSampler("X[0][1] > 50.0", "X[0][0], X[0][1]");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 4.0);
    EXPECT_NEAR(rows[0].get<float>(1), 55.0f, 0.01f);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 5.0);
    EXPECT_NEAR(rows[1].get<float>(1), 55.0f, 0.01f);
}

// ============================================================================
// TV-04: Edge detect — lookbehind change, TRUNCATE
// ============================================================================
TEST_F(SamplerTest, TV04_EdgeDetect_Truncate) {
    auto rows = runSampler("X[0][1] != X[-1][1]", "X[0][0], X[-1][1], X[0][1]");
    // TRUNCATE: row 0 skipped (no X[-1]), 6 evaluated.
    // Changes: 20.5→21.0 ✓, 21.0→21.0 ✗, 21.0→55.0 ✓, 55.0→55.0 ✗, 55.0→22.0 ✓, 22.0→22.5 ✓ = 4 rows
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 2.0);   // 20.5→21.0
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 4.0);   // 21.0→55.0
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 6.0);   // 55.0→22.0
    EXPECT_DOUBLE_EQ(rows[3].get<double>(0), 7.0);   // 22.0→22.5
}

// ============================================================================
// TV-05: Edge detect — lookbehind, EXPAND
// ============================================================================
TEST_F(SamplerTest, TV05_EdgeDetect_Expand) {
    auto rows = runSampler("X[0][1] != X[-1][1]", "X[0][0], X[-1][1], X[0][1]",
                           SamplerMode::EXPAND);
    // EXPAND: row 0 self-compares (20.5!=20.5 → false).
    // Same 4 output rows as TV-04 (changes at rows 1,3,5,6)
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 2.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 6.0);
    EXPECT_DOUBLE_EQ(rows[3].get<double>(0), 7.0);
}

// ============================================================================
// TV-06: String equality
// ============================================================================
TEST_F(SamplerTest, TV06_StringEquality) {
    auto rows = runSampler("X[0][2] == \"alarm\"", "X[0][0], X[0][2]");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 5.0);
}

// ============================================================================
// TV-07: String inequality
// ============================================================================
TEST_F(SamplerTest, TV07_StringInequality) {
    auto rows = runSampler("X[0][2] != \"ok\"", "X[0][0], X[0][2]");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 3.0);  // warn
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 4.0);  // alarm
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 5.0);  // alarm
}

// ============================================================================
// TV-08: Short-circuit AND — div-by-zero guard
// ============================================================================
TEST_F(SamplerTest, TV08_ShortCircuitAnd) {
    auto rows = runSampler("X[0][4] != 0 && X[0][1] / X[0][4] > 10.0",
                           "X[0][0], X[0][1], X[0][4]");
    // Row 0: counter=0 → false (short-circuit). Rows 1-6:
    // 21/1=21>10 ✓, 21/2=10.5>10 ✓, 55/3=18.3>10 ✓, 55/100=0.55 ✗, 22/101=0.22 ✗, 22.5/102=0.22 ✗
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 2.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 4.0);
}

// ============================================================================
// TV-09: Short-circuit OR — mixed types
// ============================================================================
TEST_F(SamplerTest, TV09_ShortCircuitOr) {
    auto rows = runSampler("X[0][1] > 50.0 || X[0][2] == \"warn\"",
                           "X[0][0]");
    // temp>50: rows 3,4. status=="warn": row 2 (already not in temp>50).
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 5.0);
}

// ============================================================================
// TV-10: Selection arithmetic — delta computation
// ============================================================================
TEST_F(SamplerTest, TV10_SelectionDelta) {
    auto rows = runSampler("true", "X[0][0], X[0][1] - X[-1][1]");
    // TRUNCATE: row 0 skipped (no X[-1]), 6 output rows
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 2.0);
    EXPECT_NEAR(rows[0].get<double>(1), 0.5, kEps);    // 21.0 - 20.5
    EXPECT_NEAR(rows[1].get<double>(1), 0.0, kEps);    // 21.0 - 21.0
    EXPECT_NEAR(rows[2].get<double>(1), 34.0, kEps);   // 55.0 - 21.0
    EXPECT_NEAR(rows[3].get<double>(1), 0.0, kEps);    // 55.0 - 55.0
    EXPECT_NEAR(rows[4].get<double>(1), -33.0, kEps);  // 22.0 - 55.0
    EXPECT_NEAR(rows[5].get<double>(1), 0.5, kEps);    // 22.5 - 22.0
}

// ============================================================================
// TV-11: Modulo operator
// ============================================================================
TEST_F(SamplerTest, TV11_Modulo) {
    auto rows = runSampler("X[0][4] % 2 == 0", "X[0][0], X[0][4]");
    // counter: 0,1,2,3,100,101,102.  Even: 0,2,100,102 → 4 rows
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 5.0);
    EXPECT_DOUBLE_EQ(rows[3].get<double>(0), 7.0);
}

// ============================================================================
// TV-12: Bitwise AND flag test
// ============================================================================
TEST_F(SamplerTest, TV12_BitwiseAnd) {
    auto rows = runSampler("(X[0][3] & 0x04) != 0", "X[0][0], X[0][3]");
    // flags: 0x06(✓),0x07(✓),0x03(✗),0x05(✓),0x05(✓),0x07(✓),0x06(✓) → 6 rows
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 2.0);
    // Row 2 (timestamp=3.0) excluded — flags=0x03 has bit 2 clear
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[3].get<double>(0), 5.0);
    EXPECT_DOUBLE_EQ(rows[4].get<double>(0), 6.0);
    EXPECT_DOUBLE_EQ(rows[5].get<double>(0), 7.0);
}

// ============================================================================
// TV-13: Bitwise NOT + AND
// ============================================================================
TEST_F(SamplerTest, TV13_BitwiseNotAnd) {
    auto rows = runSampler("(X[0][3] & ~0x02) == 0x04", "X[0][0], X[0][3]");
    // flags & ~0x02 = flags & 0xFFFD: 0x04,0x05,0x01,0x04,0x04,0x05,0x04
    // == 0x04: rows 0,3,4,6 → wait flags is uint16
    // Actually: ~0x02 for uint16 = 0xFFFD. 0x06&0xFFFD=0x04✓, 0x07&0xFFFD=0x05✗,
    // 0x03&0xFFFD=0x01✗, 0x05&0xFFFD=0x04✓, 0x05&0xFFFD=0x04✓, 0x07&0xFFFD=0x05✗, 0x06&0xFFFD=0x04✓
    // But plan says 2 rows. Let me re-check the plan.
    // Plan TV-13: `(X[0][3] & ~0x02) == 0x04` → 2 rows
    // Hmm the plan says the NOT promotion uses int, so ~0x02 on int would be a large negative number
    // The B1 fix promotes UINT→INT for bitwise ops. So flags (uint16) gets promoted to int.
    // ~0x02 = literal 0x02 is INT, ~INT = -(0x02+1) = -3 = 0xFFFF...FFFD
    // flags as INT: 6,7,3,5,5,7,6.
    // 6 & (-3) = 6 & 0xFFFD (64-bit) = 4. 4==4 ✓
    // 7 & (-3) = 5. 5==4 ✗
    // 3 & (-3) = 1. 1==4 ✗
    // 5 & (-3) = 4. 4==4 ✓  (but plan says 2 rows, not 4)
    // Wait, plan says flags values are 0x06,0x07,0x03,0x05,0x05,0x07,0x06
    // That's rows 0-6. (X[0][3] & ~0x02) == 0x04
    // row0: (0x06 & ~0x02) = (0x06 & 0xFFFD) = 0x04 → == 0x04 ✓
    // row1: (0x07 & 0xFFFD) = 0x05 → ✗
    // row2: (0x03 & 0xFFFD) = 0x01 → ✗
    // row3: (0x05 & 0xFFFD) = 0x04 → ✓
    // row4: (0x05 & 0xFFFD) = 0x04 → ✓
    // row5: (0x07 & 0xFFFD) = 0x05 → ✗
    // row6: (0x06 & 0xFFFD) = 0x04 → ✓
    // That's 4 rows, not 2. The plan says 2 rows though. Let me re-check...
    // Plan: "flags: 0x06(✓), 0x07(✗), 0x03(✗), 0x05(✗), 0x05(✗), 0x07(✗), 0x06(✓) → 2 rows"
    // Hmm, the plan marks 0x05 as ✗. Let me reconsider.
    // Maybe the plan's ~0x02 interpretation is different. If 0x02 is treated as
    // int literal, then ~0x02 in the EBNF means bitwise complement of 2.
    // In two's complement 64-bit: ~2 = 0xFFFFFFFFFFFFFFFFD = -3
    // 0x05 & (-3) in signed 64-bit:
    //   0x05 = 0x0000000000000005
    //   -3   = 0xFFFFFFFFFFFFFFFD
    //   AND  = 0x0000000000000005 & 0xFFFFFFFFFFFFFFFD = 0x0000000000000005 = 5
    // So 5 == 4 is false ✗.
    // But 0x06 & (-3) = 0x0000000000000006 & 0xFFFFFFFFFFFFFFFD = 0x0000000000000004 = 4 ✓
    // So the result is rows 0 and 6, which is 2 rows. The plan is correct!
    // My earlier calculation was wrong because 0x05=0b101, clearing bit 1 gives 0b101 still
    // (bit 1 of 0x05 is 0). Hmm wait: 0x05 = 0b0101, ~0x02 = ~0b0010 = ...11111101
    // 0b0101 & 0b...11111101 = 0b0101 = 5. So yes, 5 != 4.
    // 0x06 = 0b0110, & 0b...1101 = 0b0100 = 4. 4==4 ✓.
    // So rows 0 (flags=0x06) and row6 (flags=0x06). That's 2. Plan is correct.
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 7.0);
}

// ============================================================================
// TV-14: Column-name indexing
// ============================================================================
TEST_F(SamplerTest, TV14_ColumnNameIndexing) {
    auto rows = runSampler("X[0][\"temperature\"] > 50.0",
                           "X[0][\"timestamp\"], X[0][\"temperature\"]");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 5.0);
}

// ============================================================================
// TV-15: Logical negation
// ============================================================================
TEST_F(SamplerTest, TV15_LogicalNegation) {
    auto rows = runSampler("!(X[0][2] == \"ok\")", "X[0][0], X[0][2]");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 5.0);
}

// ============================================================================
// TV-16: Numeric in boolean context
// ============================================================================
TEST_F(SamplerTest, TV16_NumericBoolContext) {
    auto rows = runSampler("X[0][4]", "X[0][0], X[0][4]");
    // counter: 0(false),1,2,3,100,101,102 → 6 rows (row 0 excluded)
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 2.0);
}

// ============================================================================
// TV-17: Lookahead — TRUNCATE
// ============================================================================
TEST_F(SamplerTest, TV17_Lookahead_Truncate) {
    auto rows = runSampler("X[+1][1] > X[0][1]", "X[0][0], X[0][1], X[+1][1]");
    // TRUNCATE: row 6 skipped. 20.5→21✓, 21→21✗, 21→55✓, 55→55✗, 55→22✗, 22→22.5✓
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 6.0);
}

// ============================================================================
// TV-18: Lookahead — EXPAND
// ============================================================================
TEST_F(SamplerTest, TV18_Lookahead_Expand) {
    auto rows = runSampler("X[+1][1] > X[0][1]", "X[0][0], X[0][1], X[+1][1]",
                           SamplerMode::EXPAND);
    // EXPAND: row 6 self-compares (22.5>22.5 → false). Same 3 output rows.
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 6.0);
}

// ============================================================================
// TV-19: Mixed lookbehind + lookahead, TRUNCATE — 0 rows
// ============================================================================
TEST_F(SamplerTest, TV19_MixedWindow_Truncate) {
    auto rows = runSampler("X[0][1] > X[-1][1] && X[0][1] > X[+1][1]",
                           "X[0][0], X[0][1]");
    EXPECT_EQ(rows.size(), 0u);
}

// ============================================================================
// TV-20: Type promotion — int + float
// ============================================================================
TEST_F(SamplerTest, TV20_TypePromotion) {
    auto rows = runSampler("X[0][4] + X[0][1] > 60.0", "X[0][0], X[0][4] + X[0][1]");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 5.0);
    EXPECT_NEAR(rows[0].get<double>(1), 155.0, kEps);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 6.0);
    EXPECT_NEAR(rows[1].get<double>(1), 123.0, kEps);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 7.0);
    EXPECT_NEAR(rows[2].get<double>(1), 124.5, kEps);
}

// ============================================================================
// TV-21: Wildcard with offset — doubled output
// ============================================================================
TEST_F(SamplerTest, TV21_WildcardOffset) {
    auto rows = runSampler("X[0][0] == 3.0", "X[-1][*], X[0][*]");
    // TRUNCATE: row 2 (ts=3.0) matches. Output = 10 columns: row1 data + row2 data.
    ASSERT_EQ(rows.size(), 1u);
    // X[-1] columns (prev row: ts=2, temp=21, "ok", 0x07, 1)
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 2.0);       // prev timestamp
    EXPECT_NEAR(rows[0].get<float>(1), 21.0f, 0.01f);    // prev temperature
    // X[0] columns (current row: ts=3, temp=21, "warn", 0x03, 2)
    EXPECT_DOUBLE_EQ(rows[0].get<double>(5), 3.0);       // timestamp
    EXPECT_NEAR(rows[0].get<float>(6), 21.0f, 0.01f);    // temperature
}

// ============================================================================
// TV-22: Compile error — string in arithmetic
// ============================================================================
TEST_F(SamplerTest, TV22_CompileError_StringArithmetic) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    auto result = sampler.setConditional("X[0][2] + 1 > 0");
    EXPECT_FALSE(result.success);
}

// ============================================================================
// TV-23: Compile error — string ordering
// ============================================================================
TEST_F(SamplerTest, TV23_CompileError_StringOrdering) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    auto result = sampler.setConditional("X[0][2] > \"ok\"");
    EXPECT_FALSE(result.success);
}

// ============================================================================
// TV-24: Compile error — invalid column index
// ============================================================================
TEST_F(SamplerTest, TV24_CompileError_BadColumnIndex) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    auto result = sampler.setConditional("X[0][99] > 0");
    EXPECT_FALSE(result.success);
}

// ============================================================================
// TV-25: Compile error — unknown column name
// ============================================================================
TEST_F(SamplerTest, TV25_CompileError_UnknownColumnName) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    auto result = sampler.setConditional("X[0][\"nonexistent\"] > 0");
    EXPECT_FALSE(result.success);
}

// ============================================================================
// TV-26: Runtime error — div-by-zero, THROW policy
// ============================================================================
TEST_F(SamplerTest, TV26_RuntimeError_DivByZero_Throw) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    sampler.setErrorPolicy(SamplerErrorPolicy::THROW);
    // Use integer-only expression to get actual div-by-zero (float div yields Inf)
    auto result = sampler.setConditional("X[0][4] / (X[0][4] - X[0][4]) > 0");
    EXPECT_TRUE(result.success);
    // Integer division by zero should throw
    EXPECT_THROW(sampler.next(), std::runtime_error);
}

// ============================================================================
// TV-27: Runtime error — div-by-zero, SKIP_ROW policy
// ============================================================================
TEST_F(SamplerTest, TV27_RuntimeError_DivByZero_SkipRow) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    sampler.setErrorPolicy(SamplerErrorPolicy::SKIP_ROW);
    // Use integer division to trigger div-by-zero on every row
    auto cond = sampler.setConditional("X[0][4] / (X[0][4] - X[0][4]) > 0");
    EXPECT_TRUE(cond.success);
    auto sel = sampler.setSelection("X[0][0]");
    EXPECT_TRUE(sel.success);
    auto rows = sampler.bulk();
    // All rows hit integer div-by-zero → all skipped
    EXPECT_EQ(rows.size(), 0u);
}

// ============================================================================
// TV-28: Shift operators
// ============================================================================
TEST_F(SamplerTest, TV28_ShiftOperators) {
    auto rows = runSampler("(X[0][3] >> 1) & 0x01 != 0", "X[0][0], X[0][3]");
    // flags>>1: 0x03,0x03,0x01,0x02,0x02,0x03,0x03. Bit 0 set: rows 0,1,2,5,6
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 2.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 3.0);
    EXPECT_DOUBLE_EQ(rows[3].get<double>(0), 6.0);
    EXPECT_DOUBLE_EQ(rows[4].get<double>(0), 7.0);
}

// ============================================================================
// TV-29: Parenthesised precedence override
// ============================================================================
TEST_F(SamplerTest, TV29_Precedence) {
    auto rows = runSampler("X[0][4] % (2 + 1) == 0", "X[0][0], X[0][4]");
    // counter%3: 0,1,2,0,1,2,0 → rows 0,3,6
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 1.0);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 4.0);
    EXPECT_DOUBLE_EQ(rows[2].get<double>(0), 7.0);
}

// ============================================================================
// TV-30: Linear interpolation (first-order midpoint)
// ============================================================================
TEST_F(SamplerTest, TV30_LinearInterpolation) {
    auto rows = runSampler("true", "X[0][0], (X[-1][1] + X[0][1]) / 2.0");
    // TRUNCATE: row 0 skipped, 6 output rows
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_NEAR(rows[0].get<double>(1), 20.75, kEps);   // (20.5+21.0)/2
    EXPECT_NEAR(rows[1].get<double>(1), 21.0, kEps);    // (21.0+21.0)/2
    EXPECT_NEAR(rows[2].get<double>(1), 38.0, kEps);    // (21.0+55.0)/2
    EXPECT_NEAR(rows[3].get<double>(1), 55.0, kEps);    // (55.0+55.0)/2
    EXPECT_NEAR(rows[4].get<double>(1), 38.5, kEps);    // (55.0+22.0)/2
    EXPECT_NEAR(rows[5].get<double>(1), 22.25, kEps);   // (22.0+22.5)/2
}

// ============================================================================
// TV-31: Quadratic smoothing (second-order, 3-point)
// ============================================================================
TEST_F(SamplerTest, TV31_QuadraticSmoothing) {
    auto rows = runSampler("true",
        "X[0][0], (X[-1][1] + 2.0 * X[0][1] + X[+1][1]) / 4.0");
    // TRUNCATE: rows 0,6 skipped → 5 rows
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_NEAR(rows[0].get<double>(1), 20.875, kEps);  // (20.5+2*21.0+21.0)/4
    EXPECT_NEAR(rows[1].get<double>(1), 29.5, kEps);    // (21.0+2*21.0+55.0)/4 = 118/4
    EXPECT_NEAR(rows[2].get<double>(1), 46.5, kEps);    // (21.0+2*55.0+55.0)/4 = 186/4
    EXPECT_NEAR(rows[3].get<double>(1), 46.75, kEps);   // (55.0+2*55.0+22.0)/4 = 187/4
    EXPECT_NEAR(rows[4].get<double>(1), 30.375, kEps);  // (55.0+2*22.0+22.5)/4 = 121.5/4
}

// ============================================================================
// TV-32: Sliding window average (3-point)
// ============================================================================
TEST_F(SamplerTest, TV32_SlidingAvg3) {
    auto rows = runSampler("true",
        "X[0][0], (X[-1][1] + X[0][1] + X[+1][1]) / 3.0");
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_NEAR(rows[0].get<double>(1), 20.8333, kEps);
    EXPECT_NEAR(rows[1].get<double>(1), 32.3333, kEps);
    EXPECT_NEAR(rows[2].get<double>(1), 43.6667, kEps);
    EXPECT_NEAR(rows[3].get<double>(1), 44.0, kEps);
    EXPECT_NEAR(rows[4].get<double>(1), 33.1667, kEps);
}

// ============================================================================
// TV-33: Sliding window average (5-point, wide window)
// ============================================================================
TEST_F(SamplerTest, TV33_SlidingAvg5) {
    auto rows = runSampler("true",
        "X[0][0], (X[-2][1] + X[-1][1] + X[0][1] + X[+1][1] + X[+2][1]) / 5.0");
    // TRUNCATE: rows 0,1,5,6 skipped → 3 rows
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_NEAR(rows[0].get<double>(1), 34.5, kEps);
    EXPECT_NEAR(rows[1].get<double>(1), 34.8, kEps);
    EXPECT_NEAR(rows[2].get<double>(1), 35.1, kEps);
}

// ============================================================================
// TV-34: Gradient over time (first derivative)
// ============================================================================
TEST_F(SamplerTest, TV34_Gradient) {
    auto rows = runSampler("true",
        "X[0][0], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])");
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_NEAR(rows[0].get<double>(1), 0.5, kEps);
    EXPECT_NEAR(rows[1].get<double>(1), 0.0, kEps);
    EXPECT_NEAR(rows[2].get<double>(1), 34.0, kEps);
    EXPECT_NEAR(rows[3].get<double>(1), 0.0, kEps);
    EXPECT_NEAR(rows[4].get<double>(1), -33.0, kEps);
    EXPECT_NEAR(rows[5].get<double>(1), 0.5, kEps);
}

// ============================================================================
// TV-35: Gradient filter (threshold on rate-of-change)
// ============================================================================
TEST_F(SamplerTest, TV35_GradientFilter) {
    auto rows = runSampler(
        "(X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0]) > 1.0 || "
        "(X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0]) < -1.0",
        "X[0][0], X[0][1], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].get<double>(0), 4.0);
    EXPECT_NEAR(rows[0].get<double>(2), 34.0, kEps);
    EXPECT_DOUBLE_EQ(rows[1].get<double>(0), 6.0);
    EXPECT_NEAR(rows[1].get<double>(2), -33.0, kEps);
}

// ============================================================================
// TV-36: Second-order gradient (acceleration / curvature)
// ============================================================================
TEST_F(SamplerTest, TV36_SecondDerivative) {
    auto rows = runSampler("true",
        "X[0][0], (X[+1][1] - 2.0 * X[0][1] + X[-1][1]) / "
        "((X[0][0] - X[-1][0]) * (X[+1][0] - X[0][0]))");
    // TRUNCATE: rows 0,6 skipped → 5 rows
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_NEAR(rows[0].get<double>(1), -0.5, kEps);
    EXPECT_NEAR(rows[1].get<double>(1), 34.0, kEps);
    EXPECT_NEAR(rows[2].get<double>(1), -34.0, kEps);
    EXPECT_NEAR(rows[3].get<double>(1), -33.0, kEps);
    EXPECT_NEAR(rows[4].get<double>(1), 33.5, kEps);
}

// ============================================================================
// Additional: Sampler basic API tests
// ============================================================================

TEST_F(SamplerTest, DefaultConditionalIsEmpty) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    EXPECT_TRUE(sampler.getConditional().empty());
    EXPECT_TRUE(sampler.getSelection().empty());
    EXPECT_EQ(sampler.getMode(), SamplerMode::TRUNCATE);
}

TEST_F(SamplerTest, ConditionalOnly_NoSelection_ReturnsSourceRows) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    sampler.setConditional("X[0][1] > 50.0");
    // No selection → row() returns the reader's current row (5 columns)
    int count = 0;
    while (sampler.next()) {
        const auto& r = sampler.row();
        EXPECT_GT(r.get<float>(1), 50.0f);
        ++count;
    }
    EXPECT_EQ(count, 2);
}

TEST_F(SamplerTest, BulkReturnsAllMatchingRows) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    sampler.setConditional("true");
    auto rows = sampler.bulk();
    EXPECT_EQ(rows.size(), 7u);
}

TEST_F(SamplerTest, Disassemble_ProducesNonEmpty) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    sampler.setConditional("X[0][1] > 50.0");
    sampler.setSelection("X[0][0], X[0][1]");
    std::string dis = sampler.disassemble();
    EXPECT_FALSE(dis.empty());
    EXPECT_NE(dis.find("Conditional"), std::string::npos);
    EXPECT_NE(dis.find("Selection"), std::string::npos);
}

TEST_F(SamplerTest, OutputLayout_HasCorrectColumns) {
    Reader<Layout> reader;
    reader.open(data_file_);
    Sampler<Layout> sampler(reader);
    sampler.setSelection("X[0][0], X[0][1]");
    auto& out = sampler.outputLayout();
    EXPECT_EQ(out.columnCount(), 2u);
}
