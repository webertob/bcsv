#pragma once

/// @file std_charconv_compat.h
/// @brief Compatibility shims for std::from_chars / std::to_chars (float/double).
///
/// Apple libc++ (as of Xcode 15 / LLVM 16) does not implement the
/// floating-point overloads of std::from_chars and std::to_chars.
/// The standard feature-test macro __cpp_lib_to_chars (P0067R5, value 201611)
/// advertises their availability.
///
/// This header probes __cpp_lib_to_chars and, when the macro is absent or too
/// low, provides bcsv::compat:: wrappers that fall back to strtof/strtod and
/// snprintf.  When the platform does support the std overloads, the wrappers
/// simply forward to std::from_chars / std::to_chars — zero overhead.
///
/// Usage throughout the library:
///   #include "std_charconv_compat.h"
///   bcsv::compat::from_chars(first, last, floatValue);
///   bcsv::compat::to_chars(buf, buf + N, doubleValue);

#include <charconv>   // std::from_chars, std::to_chars (integers always available)
#include <version>    // __cpp_lib_to_chars
#include <type_traits>

// The fallback implementations below are compiled unconditionally, so their
// dependencies are unconditional too.  They are inline: an overload nobody
// calls is never emitted, so this costs the embedded targets nothing.
#include <cerrno>
#include <clocale>
#include <cmath>     // HUGE_VAL, HUGE_VALF
#include <cstdio>
#include <cstdlib>
#include <cstring>   // std::memcpy
#include <string>

// ── Feature detection ──────────────────────────────────────────────────
// __cpp_lib_to_chars >= 201611 means floating-point overloads are present.
// If the macro is missing or below that value, we supply our own.
// Overridable so a test can force the fallback path on a platform that has the
// std overloads -- otherwise fallback::* is only ever *run* on macOS.
#ifndef BCSV_HAS_FLOAT_CHARCONV
    #if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        #define BCSV_HAS_FLOAT_CHARCONV 1
    #else
        #define BCSV_HAS_FLOAT_CHARCONV 0
    #endif
#endif

namespace bcsv::compat {

    /// Fallback implementations for platforms whose standard library lacks the
    /// floating-point charconv overloads.
    ///
    /// These are compiled on **every** platform, not just the ones that use
    /// them.  Keeping them behind the feature `#if` meant they were not even
    /// parsed on Linux, so two silent wrong-value bugs (a discarded subnormal
    /// and locale-dependent parsing) survived a green CI and only surfaced on
    /// macOS.  Compiling them everywhere lets the test suite exercise them
    /// everywhere -- see tests/charconv_compat_test.cpp.
    namespace fallback {

        /// strtod/strtof read and write the *locale's* decimal point, while
        /// this shim's contract -- like std::from_chars -- is always '.'.
        /// Under a comma-decimal locale strtod("1.5") returns 1, silently.
        /// Callers translate at the boundary rather than touching the process
        /// locale, which is global and not safe to change from a library.
        inline char localeDecimalPoint() {
            const char* dp = std::localeconv()->decimal_point;
            return (dp && *dp) ? *dp : '.';
        }

        // ── std::from_chars grammar scanner ────────────────────────────
        //
        // strtod accepts more than std::from_chars does: leading whitespace, a
        // leading '+', and hex literals such as 0x1p3.  That made a CSV cell
        // parse on macOS and fail on Linux for the same file.  Scanning the
        // input against the from_chars grammar first, and handing strtod only
        // the matched prefix, makes the two agree -- and gives the length up
        // front, so the copy below fits in a stack buffer.
        //
        // Grammar (chars_format::general), per [charconv.from.chars]:
        //     [-] ( digits [ . [digits] ] | . digits ) [ (e|E) [+|-] digits ]
        //   | [-] inf | infinity                          (case-insensitive)
        //   | [-] nan [ ( alnum-or-underscore* ) ]        (case-insensitive)
        // Note there is no leading '+', no whitespace and no hex form.

        constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }

        /// ASCII lower-case. Deliberately not std::tolower, which is
        /// locale-dependent and takes an int.
        constexpr char toLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        constexpr bool isNanBodyChar(char c) {
            return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        /// True when [p, last) begins with `word`, compared case-insensitively.
        inline bool startsWithIgnoreCase(const char* p, const char* last, const char* word) {
            for (; *word; ++p, ++word) {
                if (p == last || toLowerAscii(*p) != *word) return false;
            }
            return true;
        }

        /// End of the longest prefix of [first, last) that std::from_chars
        /// would accept, or `first` when there is no valid prefix at all.
        inline const char* scanGeneralFormat(const char* first, const char* last) {
            const char* p = first;
            if (p != last && *p == '-') ++p;      // no '+', matching from_chars

            // "infinity" before "inf": the longest match wins.
            if (startsWithIgnoreCase(p, last, "infinity")) return p + 8;
            if (startsWithIgnoreCase(p, last, "inf"))      return p + 3;

            if (startsWithIgnoreCase(p, last, "nan")) {
                const char* afterNan = p + 3;
                if (afterNan != last && *afterNan == '(') {
                    const char* q = afterNan + 1;
                    while (q != last && isNanBodyChar(*q)) ++q;
                    if (q != last && *q == ')') return q + 1;   // nan(chars)
                }
                return afterNan;                                 // bare nan
            }

            const char* digitsStart = p;
            while (p != last && isDigit(*p)) ++p;
            const bool hasIntegerPart = p != digitsStart;

            bool hasFractionPart = false;
            if (p != last && *p == '.') {
                const char* fracStart = ++p;
                while (p != last && isDigit(*p)) ++p;
                hasFractionPart = p != fracStart;
            }
            // A lone "." or "-" is not a number.
            if (!hasIntegerPart && !hasFractionPart) return first;

            const char* mantissaEnd = p;

            // The exponent joins the match only when it is complete: for "1e"
            // and "1e+", from_chars consumes just "1".
            if (p != last && (*p == 'e' || *p == 'E')) {
                const char* q = p + 1;
                if (q != last && (*q == '+' || *q == '-')) ++q;
                const char* expDigits = q;
                while (q != last && isDigit(*q)) ++q;
                if (q != expDigits) return q;
            }
            return mantissaEnd;
        }

        /// Null-terminated, mutable copy of a scanned number.
        ///
        /// strtod needs null termination, which a string_view into a CSV buffer
        /// cannot give it, and the copy has to be mutable so the decimal point
        /// can be retargeted.  The grammar scan bounds the length, so in
        /// practice this never leaves the stack -- the previous std::string cost
        /// an allocation for every value parsed, which is a poor trade on the
        /// embedded targets that actually use this path.
        class ScannedNumber {
        public:
            ScannedNumber(const char* first, const char* end)
                : size_(static_cast<size_t>(end - first))
            {
                if (size_ < kInlineCapacity) {
                    std::memcpy(stack_, first, size_);
                    stack_[size_] = '\0';
                    data_ = stack_;
                } else {
                    heap_.assign(first, end);
                    data_ = heap_.data();
                }
            }

            char*  data() { return data_; }
            size_t size() const { return size_; }

        private:
            // Comfortably past the longest shortest-round-trip double ("%.17g"
            // is ~24 chars); anything longer is pathological and takes the heap.
            static constexpr size_t kInlineCapacity = 64;

            char        stack_[kInlineCapacity];
            std::string heap_;
            char*       data_;
            size_t      size_;
        };

        /// Rewrite the single decimal separator in `s` from `from` to `to`.
        /// A number carries at most one, so stop at the first.
        inline void retargetDecimalPoint(char* s, size_t n, char from, char to) {
            if (from == to) return;                  // the common case: C locale
            for (size_t i = 0; i < n; ++i) {
                if (s[i] == from) { s[i] = to; return; }
            }
        }

        /// float parse via strtof, with std::from_chars semantics.
        inline std::from_chars_result from_chars(const char* first, const char* last, float& value) {
            const char* end = scanGeneralFormat(first, last);
            if (end == first)
                return {first, std::errc::invalid_argument};

            ScannedNumber text(first, end);
            retargetDecimalPoint(text.data(), text.size(), '.', localeDecimalPoint());

            errno = 0;
            const float v = std::strtof(text.data(), nullptr);
            // C lets strtof raise ERANGE when the result underflows to a
            // *representable subnormal*, and both glibc and Apple's libc do.
            // That is a correct parse, not a range error -- reporting it as one
            // discarded the value and left the caller's variable at 0.  Only
            // overflow to +/-inf and flush-to-zero are genuine failures, and
            // std::from_chars leaves `value` untouched for those.
            if (errno == ERANGE && (v == 0.0f || v == HUGE_VALF || v == -HUGE_VALF))
                return {end, std::errc::result_out_of_range};
            value = v;
            return {end, std::errc{}};
        }

        /// double parse via strtod, with std::from_chars semantics.
        inline std::from_chars_result from_chars(const char* first, const char* last, double& value) {
            const char* end = scanGeneralFormat(first, last);
            if (end == first)
                return {first, std::errc::invalid_argument};

            ScannedNumber text(first, end);
            retargetDecimalPoint(text.data(), text.size(), '.', localeDecimalPoint());

            errno = 0;
            const double v = std::strtod(text.data(), nullptr);
            // See the float overload: a subnormal result is a successful parse.
            if (errno == ERANGE && (v == 0.0 || v == HUGE_VAL || v == -HUGE_VAL))
                return {end, std::errc::result_out_of_range};
            value = v;
            return {end, std::errc{}};
        }

        /// float format via snprintf — shortest round-trip representation.
        inline std::to_chars_result to_chars(char* first, char* last, float value) {
            auto bufSize = static_cast<size_t>(last - first);
            // Try digits10 first (6 digits); if round-trip fails, use max_digits10 (9).
            int n = std::snprintf(first, bufSize, "%.6g", value);
            if (n >= 0 && static_cast<size_t>(n) < bufSize) {
                // Check before normalising: snprintf wrote the locale's decimal
                // point and strtof expects that same one.
                float check = std::strtof(first, nullptr);
                if (check != value) {
                    n = std::snprintf(first, bufSize, "%.9g", value);
                }
            }
            if (n < 0 || static_cast<size_t>(n) >= bufSize)
                return {last, std::errc::value_too_large};
            // Emit '.' regardless of locale, matching std::to_chars.
            retargetDecimalPoint(first, static_cast<size_t>(n), localeDecimalPoint(), '.');
            return {first + n, std::errc{}};
        }

        /// double format via snprintf — shortest round-trip representation.
        inline std::to_chars_result to_chars(char* first, char* last, double value) {
            auto bufSize = static_cast<size_t>(last - first);
            // Try digits10 first (15 digits); if round-trip fails, use max_digits10 (17).
            int n = std::snprintf(first, bufSize, "%.15g", value);
            if (n >= 0 && static_cast<size_t>(n) < bufSize) {
                // See the float overload: check before normalising.
                double check = std::strtod(first, nullptr);
                if (check != value) {
                    n = std::snprintf(first, bufSize, "%.17g", value);
                }
            }
            if (n < 0 || static_cast<size_t>(n) >= bufSize)
                return {last, std::errc::value_too_large};
            retargetDecimalPoint(first, static_cast<size_t>(n), localeDecimalPoint(), '.');
            return {first + n, std::errc{}};
        }

    } // namespace fallback

    // ── Public dispatchers ─────────────────────────────────────────────
    // The only thing the feature test decides: whether to use the standard
    // library's floating-point overloads or the fallbacks above. Integers are
    // always served by std::, which has had them since C++17.

#if BCSV_HAS_FLOAT_CHARCONV

    /// Forward to std::from_chars (all types including float/double).
    template<typename T>
    inline std::from_chars_result from_chars(const char* first, const char* last, T& value) {
        return std::from_chars(first, last, value);
    }

    /// Forward to std::to_chars (all types including float/double).
    template<typename T>
    inline std::to_chars_result to_chars(char* first, char* last, T value) {
        return std::to_chars(first, last, value);
    }

#else // !BCSV_HAS_FLOAT_CHARCONV

    /// Integer overloads — always available in the standard library.
    template<typename T>
    inline std::from_chars_result from_chars(const char* first, const char* last, T& value)
        requires (!std::is_floating_point_v<T>)
    {
        return std::from_chars(first, last, value);
    }

    inline std::from_chars_result from_chars(const char* first, const char* last, float& value) {
        return fallback::from_chars(first, last, value);
    }

    inline std::from_chars_result from_chars(const char* first, const char* last, double& value) {
        return fallback::from_chars(first, last, value);
    }

    /// Integer overloads — always available in the standard library.
    template<typename T>
    inline std::to_chars_result to_chars(char* first, char* last, T value)
        requires (!std::is_floating_point_v<T>)
    {
        return std::to_chars(first, last, value);
    }

    inline std::to_chars_result to_chars(char* first, char* last, float value) {
        return fallback::to_chars(first, last, value);
    }

    inline std::to_chars_result to_chars(char* first, char* last, double value) {
        return fallback::to_chars(first, last, value);
    }

#endif // BCSV_HAS_FLOAT_CHARCONV

} // namespace bcsv::compat
