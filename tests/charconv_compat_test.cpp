/*
 * Copyright (c) 2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file charconv_compat_test.cpp
 * @brief bcsv::compat::fallback — the strtod/snprintf charconv shim.
 *
 * These tests call `bcsv::compat::fallback::*` **directly**, not through the
 * `bcsv::compat::from_chars` dispatcher.  That is the point: the fallback is
 * only *used* where the standard library lacks the floating-point charconv
 * overloads (Apple libc++, and likely the STM32/Zynq toolchains), so on Linux
 * and Windows the dispatcher never reaches it.  Before this file the fallback
 * was not even compiled off macOS, and two silent wrong-value bugs lived there
 * through a green CI:
 *
 *   1. A representable subnormal was discarded.  strtod may set ERANGE when the
 *      result underflows to a subnormal — a correct parse — and the shim
 *      reported that as result_out_of_range without assigning the value, so the
 *      caller kept its zero.  Caught by macOS CI as
 *      NanInfFileTest.CsvBridgeSpecialValues.
 *   2. Parsing and formatting followed the process locale.  Under a
 *      comma-decimal locale strtod("1.5") returns 1, and snprintf("%g") emits
 *      "1,5" — while this shim's contract, like std::from_chars, is always '.'.
 *
 * The differential tests at the bottom pin the fallback against
 * std::from_chars/to_chars wherever both exist, so Linux CI now guards macOS
 * behaviour.
 */

#include <gtest/gtest.h>

#include <bcsv/std_charconv_compat.h>

#include <clocale>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace {

using bcsv::compat::fallback::from_chars;
using bcsv::compat::fallback::to_chars;

template <typename T>
std::from_chars_result parse(const std::string& text, T& value) {
    return from_chars(text.data(), text.data() + text.size(), value);
}

template <typename T>
std::string format(T value) {
    char buf[64];
    auto r = to_chars(buf, buf + sizeof(buf), value);
    EXPECT_EQ(r.ec, std::errc{});
    return std::string(buf, r.ptr);
}

// ── The bug macOS CI caught ────────────────────────────────────────────

TEST(CharconvFallback, ParsesSmallestSubnormalDouble) {
    // strtod sets ERANGE here even though the value is representable.
    const double expected = std::numeric_limits<double>::denorm_min();
    double v = 0;
    auto r = parse("4.9406564584124654e-324", v);
    EXPECT_EQ(r.ec, std::errc{});
    EXPECT_EQ(v, expected);
}

TEST(CharconvFallback, ParsesSmallestSubnormalFloat) {
    const float expected = std::numeric_limits<float>::denorm_min();
    float v = 0;
    auto r = parse("1.4012985e-45", v);
    EXPECT_EQ(r.ec, std::errc{});
    EXPECT_EQ(v, expected);
}

TEST(CharconvFallback, SubnormalsRoundTripThroughBothDirections) {
    for (double d : {std::numeric_limits<double>::denorm_min(),
                     std::numeric_limits<double>::min() / 2.0}) {
        double back = 0;
        ASSERT_EQ(parse(format(d), back).ec, std::errc{}) << d;
        EXPECT_EQ(back, d);
    }
    for (float f : {std::numeric_limits<float>::denorm_min(),
                    std::numeric_limits<float>::min() / 2.0f}) {
        float back = 0;
        ASSERT_EQ(parse(format(f), back).ec, std::errc{}) << f;
        EXPECT_EQ(back, f);
    }
}

// ── Genuine range errors must still be reported, value left alone ──────

TEST(CharconvFallback, RejectsOverflowAndLeavesValueUntouched) {
    for (const char* text : {"1e400", "-1e400"}) {
        double v = 12345.0;  // sentinel
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc::result_out_of_range) << text;
        EXPECT_EQ(v, 12345.0) << text << ": std::from_chars leaves value untouched";
    }
    float f = 42.0f;
    EXPECT_EQ(parse("1e60", f).ec, std::errc::result_out_of_range);
    EXPECT_EQ(f, 42.0f);
}

TEST(CharconvFallback, RejectsUnderflowToZeroAndLeavesValueUntouched) {
    double v = 12345.0;
    auto r = parse("1e-400", v);
    EXPECT_EQ(r.ec, std::errc::result_out_of_range);
    EXPECT_EQ(v, 12345.0);

    float f = 42.0f;
    EXPECT_EQ(parse("1e-60", f).ec, std::errc::result_out_of_range);
    EXPECT_EQ(f, 42.0f);
}

// ── Ordinary and special values ────────────────────────────────────────

TEST(CharconvFallback, ParsesOrdinaryValues) {
    struct { const char* text; double expected; } cases[] = {
        {"0", 0.0}, {"1", 1.0}, {"1.5", 1.5}, {"-2.25", -2.25},
        {"3.141592653589793", 3.141592653589793},
        {"1e10", 1e10}, {"-1.5e-8", -1.5e-8},
    };
    for (const auto& c : cases) {
        double v = 0;
        auto r = parse(c.text, v);
        EXPECT_EQ(r.ec, std::errc{}) << c.text;
        EXPECT_EQ(v, c.expected) << c.text;
    }
}

TEST(CharconvFallback, PreservesSignedZero) {
    double pos = 1.0, neg = 1.0;
    ASSERT_EQ(parse("0", pos).ec, std::errc{});
    ASSERT_EQ(parse("-0", neg).ec, std::errc{});
    EXPECT_EQ(pos, 0.0);
    EXPECT_EQ(neg, 0.0);
    EXPECT_FALSE(std::signbit(pos));
    EXPECT_TRUE(std::signbit(neg)) << "-0.0 must keep its sign bit";
    EXPECT_TRUE(std::signbit(-0.0)) << "sanity";

    double back = 1.0;
    ASSERT_EQ(parse(format(-0.0), back).ec, std::errc{});
    EXPECT_TRUE(std::signbit(back)) << "round trip must keep the sign of -0.0";
}

TEST(CharconvFallback, ParsesInfinityAndNaN) {
    double v = 0;
    ASSERT_EQ(parse("inf", v).ec, std::errc{});
    EXPECT_TRUE(std::isinf(v));
    EXPECT_FALSE(std::signbit(v));

    ASSERT_EQ(parse("-inf", v).ec, std::errc{});
    EXPECT_TRUE(std::isinf(v));
    EXPECT_TRUE(std::signbit(v));

    ASSERT_EQ(parse("nan", v).ec, std::errc{});
    EXPECT_TRUE(std::isnan(v));
}

TEST(CharconvFallback, InfinityIsNotMistakenForOverflow) {
    // strtod returns HUGE_VAL for a literal "inf" but does *not* set ERANGE;
    // the ERANGE guard must not reject it.
    double v = 0;
    auto r = parse("inf", v);
    EXPECT_EQ(r.ec, std::errc{});
    EXPECT_TRUE(std::isinf(v));
}

TEST(CharconvFallback, RoundTripsExtremes) {
    for (double d : {std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max(),
                     std::numeric_limits<double>::min()}) {
        double back = 0;
        ASSERT_EQ(parse(format(d), back).ec, std::errc{}) << d;
        EXPECT_EQ(back, d);
    }
    for (float f : {std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::min()}) {
        float back = 0;
        ASSERT_EQ(parse(format(f), back).ec, std::errc{}) << f;
        EXPECT_EQ(back, f);
    }
}

// ── Malformed input ────────────────────────────────────────────────────

TEST(CharconvFallback, RejectsMalformedInput) {
    for (const char* text : {"", "abc", "e5", "."}) {
        double v = 99.0;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc::invalid_argument) << "[" << text << "]";
        EXPECT_EQ(v, 99.0) << "[" << text << "]";
    }
}

TEST(CharconvFallback, ReportsHowMuchItConsumed) {
    const std::string text = "1.5xyz";
    double v = 0;
    auto r = parse(text, v);
    EXPECT_EQ(r.ec, std::errc{});
    EXPECT_EQ(v, 1.5);
    EXPECT_EQ(r.ptr, text.data() + 3) << "ptr must point just past the number";
}

// ── Accept/reject parity with std::from_chars ──────────────────────────
// strtod is more permissive than from_chars, so before the grammar scanner a
// CSV cell could parse on macOS and fail on Linux for the same file. These pin
// the three classes where they used to diverge.

TEST(CharconvFallbackParity, RejectsLeadingWhitespace) {
    for (const char* text : {" 1.5", "\t1.5", "\n1.5", "  42"}) {
        double v = 99.0;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc::invalid_argument) << "[" << text << "]";
        EXPECT_EQ(v, 99.0);
    }
}

TEST(CharconvFallbackParity, RejectsLeadingPlus) {
    for (const char* text : {"+1.5", "+0", "+inf"}) {
        double v = 99.0;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc::invalid_argument) << "[" << text << "]";
        EXPECT_EQ(v, 99.0);
    }
    // A leading '-' is still accepted -- only '+' is outside the grammar.
    double v = 0;
    ASSERT_EQ(parse("-1.5", v).ec, std::errc{});
    EXPECT_EQ(v, -1.5);
}

TEST(CharconvFallbackParity, DoesNotParseHexLiterals) {
    // from_chars(general) stops after the leading "0"; strtod would read 16.
    struct { const char* text; double value; std::ptrdiff_t used; } cases[] = {
        {"0x10", 0.0, 1}, {"0x1p3", 0.0, 1}, {"-0x10", -0.0, 2},
    };
    for (const auto& c : cases) {
        double v = 99.0;
        const std::string text = c.text;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc{}) << c.text;
        EXPECT_EQ(v, c.value) << c.text;
        EXPECT_EQ(r.ptr - text.data(), c.used) << c.text;
    }
}

TEST(CharconvFallbackParity, AcceptsTheSpellingsFromCharsAccepts) {
    // inf/infinity/nan are in the grammar, case-insensitively, and nan may
    // carry a parenthesised payload.
    for (const char* text : {"inf", "INF", "Inf", "infinity", "INFINITY"}) {
        double v = 0;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc{}) << text;
        EXPECT_TRUE(std::isinf(v)) << text;
    }
    for (const char* text : {"nan", "NAN", "NaN", "nan(123)", "nan(_x9)"}) {
        double v = 0;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc{}) << text;
        EXPECT_TRUE(std::isnan(v)) << text;
    }
    // ".5" and "5." are valid; a bare "." is not.
    double v = 0;
    EXPECT_EQ(parse(".5", v).ec, std::errc{});
    EXPECT_EQ(v, 0.5);
    EXPECT_EQ(parse("5.", v).ec, std::errc{});
    EXPECT_EQ(v, 5.0);
    v = 99.0;
    EXPECT_EQ(parse(".", v).ec, std::errc::invalid_argument);
    EXPECT_EQ(v, 99.0);
}

TEST(CharconvFallbackParity, TakesAnExponentOnlyWhenComplete) {
    // "1e" and "1e+" have no exponent digits, so only "1" is consumed.
    struct { const char* text; std::ptrdiff_t used; } cases[] = {
        {"1e", 1}, {"1e+", 1}, {"1e-", 1}, {"1e5", 3}, {"1e+5", 4}, {"1e-5", 4},
    };
    for (const auto& c : cases) {
        const std::string text = c.text;
        double v = 0;
        auto r = parse(text, v);
        EXPECT_EQ(r.ec, std::errc{}) << c.text;
        EXPECT_EQ(r.ptr - text.data(), c.used) << c.text;
    }
}

// ── No allocation on the hot path ──────────────────────────────────────

TEST(CharconvFallback, HandlesLiteralsTooLongForTheInlineBuffer) {
    // ScannedNumber keeps short numbers on the stack and spills to the heap
    // beyond its inline capacity; the spill path must still be correct.
    const std::string longZeros = "0." + std::string(200, '0') + "1";
    double v = -1;
    auto r = parse(longZeros, v);
    EXPECT_EQ(r.ec, std::errc{});
    EXPECT_EQ(r.ptr - longZeros.data(), static_cast<std::ptrdiff_t>(longZeros.size()));
    EXPECT_GT(v, 0.0);

    // 300 nines is ~1e299 -- long enough to spill, still inside double range.
    const std::string longDigits(300, '9');
    double w = 0;
    auto rw = parse(longDigits, w);
    EXPECT_EQ(rw.ec, std::errc{});
    EXPECT_GT(w, 1e298);

    // 400 nines genuinely overflows, on the heap path.
    const std::string overflowing(400, '9');
    double o = 5.0;
    EXPECT_EQ(parse(overflowing, o).ec, std::errc::result_out_of_range);
    EXPECT_EQ(o, 5.0) << "value untouched on overflow";
}

// ── Locale independence ────────────────────────────────────────────────

/// Restores LC_NUMERIC on destruction. setlocale is process-global, and gtest
/// runs every test in one process when the binary is invoked directly.
class ScopedNumericLocale {
public:
    explicit ScopedNumericLocale(const char* name) {
        const char* prev = std::setlocale(LC_NUMERIC, nullptr);
        if (prev) saved_ = prev;
        applied_ = std::setlocale(LC_NUMERIC, name) != nullptr;
    }
    ~ScopedNumericLocale() { std::setlocale(LC_NUMERIC, saved_.c_str()); }
    bool applied() const { return applied_; }
private:
    std::string saved_ = "C";
    bool applied_ = false;
};

/// Try a few comma-decimal locales; CI images often ship only "C".
class CommaLocaleTest : public ::testing::Test {
protected:
    static const char* findCommaLocale() {
        // POSIX spellings first, then the Windows forms, so the suite runs
        // rather than skips on both.
        for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE",
                                 "fr_FR.UTF-8", "fr_FR", "nl_NL.UTF-8",
                                 "de-DE", "fr-FR", "German_Germany.1252"}) {
            ScopedNumericLocale probe(name);
            if (probe.applied() && *std::localeconv()->decimal_point == ',')
                return name;
        }
        return nullptr;
    }
};

TEST_F(CommaLocaleTest, ParsingIgnoresTheProcessLocale) {
    const char* name = findCommaLocale();
    if (!name) GTEST_SKIP() << "no comma-decimal locale installed";
    ScopedNumericLocale guard(name);
    ASSERT_TRUE(guard.applied());
    ASSERT_EQ(*std::localeconv()->decimal_point, ',');

    // The shim's contract is '.', like std::from_chars. A bare strtod would
    // return 1 here, silently truncating.
    double v = 0;
    auto r = parse("1.5", v);
    EXPECT_EQ(r.ec, std::errc{});
    EXPECT_EQ(v, 1.5) << "locale leaked into parsing";

    float f = 0;
    ASSERT_EQ(parse("2.25", f).ec, std::errc{});
    EXPECT_EQ(f, 2.25f);
}

TEST_F(CommaLocaleTest, FormattingIgnoresTheProcessLocale) {
    const char* name = findCommaLocale();
    if (!name) GTEST_SKIP() << "no comma-decimal locale installed";
    ScopedNumericLocale guard(name);
    ASSERT_TRUE(guard.applied());

    // snprintf("%g") would emit "1,5" and corrupt the CSV column layout.
    EXPECT_EQ(format(1.5), "1.5");
    EXPECT_EQ(format(2.25f), "2.25");
    EXPECT_EQ(format(-0.5), "-0.5");
}

TEST_F(CommaLocaleTest, RoundTripsUnderACommaLocale) {
    const char* name = findCommaLocale();
    if (!name) GTEST_SKIP() << "no comma-decimal locale installed";
    ScopedNumericLocale guard(name);
    ASSERT_TRUE(guard.applied());

    for (double d : {1.5, -2.25, 1e10, 3.141592653589793,
                     std::numeric_limits<double>::denorm_min()}) {
        double back = 0;
        ASSERT_EQ(parse(format(d), back).ec, std::errc{}) << d;
        EXPECT_EQ(back, d) << d;
    }
}

// ── Differential: fallback vs the standard library ─────────────────────
// Runs wherever the std overloads exist (Linux, Windows), which is exactly
// where the fallback is otherwise never exercised.

#if BCSV_HAS_FLOAT_CHARCONV

const char* const kAgreedInputs[] = {
    "0", "-0", "1", "1.5", "-2.25", "3.141592653589793",
    "1e10", "-1.5e-8", "4.9406564584124654e-324", "1.4012985e-45",
    "2.2250738585072014e-308", "1.7976931348623157e308",
    "1e400", "-1e400", "1e-400",
    "abc", "", "1.5xyz",
    // The classes where strtod used to be more permissive than from_chars.
    " 1.5", "\t1.5", "+1.5", "0x10", "0x1p3", "-0x10",
    "inf", "INF", "infinity", "nan", "NaN", "nan(123)",
    ".5", "5.", ".", "1e", "1e+", "1e-5", "--1", "-",
};

TEST(CharconvFallbackDifferential, MatchesStdFromCharsDouble) {
    for (const char* text : kAgreedInputs) {
        const size_t n = std::strlen(text);
        double mine = 7.0, theirs = 7.0;
        auto rm = from_chars(text, text + n, mine);
        auto rs = std::from_chars(text, text + n, theirs);

        EXPECT_EQ(rm.ec, rs.ec) << "[" << text << "] error codes diverge";
        EXPECT_EQ(rm.ptr - text, rs.ptr - text) << "[" << text << "] consumed length diverges";
        if (rs.ec == std::errc{}) {
            if (std::isnan(theirs)) {
                EXPECT_TRUE(std::isnan(mine)) << "[" << text << "] expected NaN";
            } else {
                EXPECT_EQ(mine, theirs) << "[" << text << "] parsed value diverges";
                EXPECT_EQ(std::signbit(mine), std::signbit(theirs))
                    << "[" << text << "] sign diverges";
            }
        } else {
            // The fallback leaves `value` alone on a range error, which is what
            // [charconv.from.chars] specifies and what libstdc++ does.
            EXPECT_EQ(mine, 7.0) << "[" << text << "] value must be untouched on failure";
            // Deliberately no assertion about `theirs` here: implementations
            // disagree. Microsoft's STL stores the clamped result (+/-inf for
            // "1e400", 0 for "1e-400") where libstdc++ leaves the value
            // untouched, so asserting either one fails on the other platform.
            // Only the error code and consumed length are portable, and both
            // are checked above.
        }
    }
}

TEST(CharconvFallbackDifferential, MatchesStdFromCharsFloat) {
    for (const char* text : kAgreedInputs) {
        const size_t n = std::strlen(text);
        float mine = 7.0f, theirs = 7.0f;
        auto rm = from_chars(text, text + n, mine);
        auto rs = std::from_chars(text, text + n, theirs);

        EXPECT_EQ(rm.ec, rs.ec) << "[" << text << "] error codes diverge";
        EXPECT_EQ(rm.ptr - text, rs.ptr - text) << "[" << text << "] consumed length diverges";
        if (rs.ec == std::errc{}) {
            if (std::isnan(theirs)) {
                EXPECT_TRUE(std::isnan(mine)) << "[" << text << "] expected NaN";
            } else {
                EXPECT_EQ(mine, theirs) << "[" << text << "] parsed value diverges";
            }
        }
    }
}

TEST(CharconvFallbackDifferential, FormattingRoundTripsLikeStd) {
    // to_chars need not produce byte-identical text (shortest-representation
    // algorithms differ), but both must round-trip to the same value.
    for (double d : {0.0, -0.0, 1.5, -2.25, 3.141592653589793, 1e10, 1e-10,
                     std::numeric_limits<double>::denorm_min(),
                     std::numeric_limits<double>::min(),
                     std::numeric_limits<double>::max()}) {
        char stdbuf[64];
        auto rs = std::to_chars(stdbuf, stdbuf + sizeof(stdbuf), d);
        ASSERT_EQ(rs.ec, std::errc{});

        double viaMine = 0, viaStd = 0;
        ASSERT_EQ(parse(format(d), viaMine).ec, std::errc{}) << d;
        ASSERT_EQ(std::from_chars(stdbuf, rs.ptr, viaStd).ec, std::errc{}) << d;
        EXPECT_EQ(viaMine, viaStd) << d;
        EXPECT_EQ(std::signbit(viaMine), std::signbit(viaStd)) << d;
    }
}

#endif // BCSV_HAS_FLOAT_CHARCONV

}  // namespace
