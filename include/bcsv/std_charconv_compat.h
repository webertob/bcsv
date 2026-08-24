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

        /// Rewrite the single decimal separator in `s` from `from` to `to`.
        /// A number carries at most one, so stop at the first.
        inline void retargetDecimalPoint(char* s, size_t n, char from, char to) {
            if (from == to) return;                  // the common case: C locale
            for (size_t i = 0; i < n; ++i) {
                if (s[i] == from) { s[i] = to; return; }
            }
        }

        /// float parse via strtof, with std::from_chars value semantics.
        inline std::from_chars_result from_chars(const char* first, const char* last, float& value) {
            std::string s(first, last);
            retargetDecimalPoint(s.data(), s.size(), '.', localeDecimalPoint());
            char* end = nullptr;
            errno = 0;
            float v = std::strtof(s.c_str(), &end);
            std::ptrdiff_t consumed = end - s.c_str();
            if (end == s.c_str())
                return {first, std::errc::invalid_argument};
            // C lets strtof raise ERANGE when the result underflows to a
            // *representable subnormal*, and both glibc and Apple's libc do.
            // That is a correct parse, not a range error -- reporting it as one
            // discarded the value and left the caller's variable at 0.  Only
            // overflow to +/-inf and flush-to-zero are genuine failures, and
            // std::from_chars leaves `value` untouched for those.
            if (errno == ERANGE && (v == 0.0f || v == HUGE_VALF || v == -HUGE_VALF))
                return {first + consumed, std::errc::result_out_of_range};
            value = v;
            return {first + consumed, std::errc{}};
        }

        /// double parse via strtod, with std::from_chars value semantics.
        inline std::from_chars_result from_chars(const char* first, const char* last, double& value) {
            std::string s(first, last);
            retargetDecimalPoint(s.data(), s.size(), '.', localeDecimalPoint());
            char* end = nullptr;
            errno = 0;
            double v = std::strtod(s.c_str(), &end);
            std::ptrdiff_t consumed = end - s.c_str();
            if (end == s.c_str())
                return {first, std::errc::invalid_argument};
            // See the float overload: a subnormal result is a successful parse.
            if (errno == ERANGE && (v == 0.0 || v == HUGE_VAL || v == -HUGE_VAL))
                return {first + consumed, std::errc::result_out_of_range};
            value = v;
            return {first + consumed, std::errc{}};
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
