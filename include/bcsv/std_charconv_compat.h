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

// ── Feature detection ──────────────────────────────────────────────────
// __cpp_lib_to_chars >= 201611 means floating-point overloads are present.
// If the macro is missing or below that value, we supply our own.
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    #define BCSV_HAS_FLOAT_CHARCONV 1
#else
    #define BCSV_HAS_FLOAT_CHARCONV 0
#endif

#if !BCSV_HAS_FLOAT_CHARCONV
    #include <cerrno>
    #include <cstdio>
    #include <cstdlib>
    #include <string>
#endif

namespace bcsv::compat {

    // ── from_chars ─────────────────────────────────────────────────────

#if BCSV_HAS_FLOAT_CHARCONV

    /// Forward to std::from_chars (all types including float/double).
    template<typename T>
    inline std::from_chars_result from_chars(const char* first, const char* last, T& value) {
        return std::from_chars(first, last, value);
    }

#else // !BCSV_HAS_FLOAT_CHARCONV

    /// Integer overloads — always available in the standard library.
    template<typename T>
    inline std::from_chars_result from_chars(const char* first, const char* last, T& value)
        requires (!std::is_floating_point_v<T>)
    {
        return std::from_chars(first, last, value);
    }

    /// float fallback via strtof.
    inline std::from_chars_result from_chars(const char* first, const char* last, float& value) {
        std::string s(first, last);
        char* end = nullptr;
        errno = 0;
        float v = std::strtof(s.c_str(), &end);
        std::ptrdiff_t consumed = end - s.c_str();
        if (end == s.c_str())
            return {first, std::errc::invalid_argument};
        if (errno == ERANGE)
            return {first + consumed, std::errc::result_out_of_range};
        value = v;
        return {first + consumed, std::errc{}};
    }

    /// double fallback via strtod.
    inline std::from_chars_result from_chars(const char* first, const char* last, double& value) {
        std::string s(first, last);
        char* end = nullptr;
        errno = 0;
        double v = std::strtod(s.c_str(), &end);
        std::ptrdiff_t consumed = end - s.c_str();
        if (end == s.c_str())
            return {first, std::errc::invalid_argument};
        if (errno == ERANGE)
            return {first + consumed, std::errc::result_out_of_range};
        value = v;
        return {first + consumed, std::errc{}};
    }

#endif // BCSV_HAS_FLOAT_CHARCONV


    // ── to_chars ───────────────────────────────────────────────────────

#if BCSV_HAS_FLOAT_CHARCONV

    /// Forward to std::to_chars (all types including float/double).
    template<typename T>
    inline std::to_chars_result to_chars(char* first, char* last, T value) {
        return std::to_chars(first, last, value);
    }

#else // !BCSV_HAS_FLOAT_CHARCONV

    /// Integer overloads — always available in the standard library.
    template<typename T>
    inline std::to_chars_result to_chars(char* first, char* last, T value)
        requires (!std::is_floating_point_v<T>)
    {
        return std::to_chars(first, last, value);
    }

    /// float fallback via snprintf.
    inline std::to_chars_result to_chars(char* first, char* last, float value) {
        auto bufSize = static_cast<size_t>(last - first);
        int n = std::snprintf(first, bufSize, "%.9g", value);
        if (n < 0 || static_cast<size_t>(n) >= bufSize)
            return {last, std::errc::value_too_large};
        return {first + n, std::errc{}};
    }

    /// double fallback via snprintf.
    inline std::to_chars_result to_chars(char* first, char* last, double value) {
        auto bufSize = static_cast<size_t>(last - first);
        int n = std::snprintf(first, bufSize, "%.17g", value);
        if (n < 0 || static_cast<size_t>(n) >= bufSize)
            return {last, std::errc::value_too_large};
        return {first + n, std::errc{}};
    }

#endif // BCSV_HAS_FLOAT_CHARCONV

} // namespace bcsv::compat
