/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#pragma once

/**
 * @file type_probe.h
 * @brief Shared column-type probing and cast machinery for the CLI tools.
 *
 * Extracted from bcsvCast so csv2bcsv can reuse the same round-trip-exact
 * inference and the same loss/coercion model:
 *   - ColumnProbeState — streaming narrowest-lossless-type probe per column
 *   - cellLoses()      — loss model: does casting value v to dst lose data?
 *   - coerce() / doubleToIntForced() / satToInt() — saturating conversions
 *   - visitTyped()     — typed row-cell dispatch for the above
 */

#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <bcsv/bcsv.h>
#include <bcsv/std_charconv_compat.h>

namespace bcsv_cli {

// Exact power-of-two boundaries for double→int64/uint64 range tests.
// static_cast<double>(INT64_MAX) rounds UP to 2^63 and static_cast<double>(UINT64_MAX)
// to 2^64, so comparing against those lets a double of exactly 2^63 / 2^64 slip
// through and overflow (UB) on cast. Compare against the first out-of-range value.
inline constexpr double TWO_POW_63 = 9223372036854775808.0;   // 2^63  (= -INT64_MIN)
inline constexpr double TWO_POW_64 = 18446744073709551616.0;  // 2^64

// ── ColumnProbeState ────────────────────────────────────────────────

struct ColumnProbeState {
    size_t           col_index     = 0;
    bcsv::ColumnType original_type = bcsv::ColumnType::VOID;
    bcsv::ColumnType optimal_type  = bcsv::ColumnType::VOID;
    bool             alive         = true;
    size_t           rows_probed   = 0;

    int64_t  min_s = std::numeric_limits<int64_t>::max();
    int64_t  max_s = std::numeric_limits<int64_t>::lowest();
    uint64_t min_u = std::numeric_limits<uint64_t>::max();
    uint64_t max_u = 0;

    bool    f_float_roundtrip = true;
    bool    f_bool_alive      = true;
    bool    f_ladder_alive    = true;
    int64_t int_min           = std::numeric_limits<int64_t>::max();
    int64_t int_max           = std::numeric_limits<int64_t>::lowest();
    bool    int_all_positive  = true;
    double  tol               = 0.0;   // --tolerance (absolute epsilon; float→int/float)

    bool str_done = false;

    void init(bcsv::ColumnType type, size_t idx, bool probeStrings, double tolerance) {
        col_index     = idx;
        original_type = type;
        optimal_type  = type;
        tol           = tolerance;

        switch (type) {
            case bcsv::ColumnType::BOOL:
                alive = false;
                break;
            case bcsv::ColumnType::STRING:
                if (!probeStrings)
                    alive = false;
                break;
            default:
                break;
        }
    }

    static bcsv::ColumnType deriveIntSigned(int64_t min, int64_t max) {
        if (min >= 0 && max <= 1)
            return bcsv::ColumnType::BOOL;
        if (min >= 0) {
            uint64_t umax = static_cast<uint64_t>(max);
            if (umax <= 255ULL)
                return bcsv::ColumnType::UINT8;
            if (umax <= 65535ULL)
                return bcsv::ColumnType::UINT16;
            if (umax <= UINT32_MAX)
                return bcsv::ColumnType::UINT32;
            // INT64→UINT64 yields 0% savings and risks precision loss.
            // Keep as INT64 (no narrowing).
            return bcsv::ColumnType::INT64;
        } else {
            if (min >= INT8_MIN && max <= INT8_MAX)
                return bcsv::ColumnType::INT8;
            if (min >= INT16_MIN && max <= INT16_MAX)
                return bcsv::ColumnType::INT16;
            if (min >= INT32_MIN && max <= INT32_MAX)
                return bcsv::ColumnType::INT32;
            return bcsv::ColumnType::INT64;
        }
    }

    static bcsv::ColumnType deriveIntUnsigned(uint64_t /*min*/, uint64_t max) {
        if (max <= 1)
            return bcsv::ColumnType::BOOL;
        if (max <= 255ULL)
            return bcsv::ColumnType::UINT8;
        if (max <= 65535ULL)
            return bcsv::ColumnType::UINT16;
        if (max <= UINT32_MAX)
            return bcsv::ColumnType::UINT32;
        return bcsv::ColumnType::UINT64;
    }

    // Single shared float/string derivation.  `orig` is the fallback type
    // (DOUBLE for native float cols, DOUBLE for string→numeric).
    static bcsv::ColumnType deriveFromFloat(
        bool bool_alive, bool ladder_alive, bool roundtrip_alive,
        int64_t i_min, int64_t i_max, bool i_all_positive,
        bcsv::ColumnType orig) {
        if (bool_alive)
            return bcsv::ColumnType::BOOL;
        if (ladder_alive) {
            if (i_all_positive) {
                if (i_max <= 255LL)
                    return bcsv::ColumnType::UINT8;
                if (i_max <= 65535LL)
                    return bcsv::ColumnType::UINT16;
                uint64_t umax = static_cast<uint64_t>(i_max);
                if (umax <= UINT32_MAX)
                    return bcsv::ColumnType::UINT32;
                return bcsv::ColumnType::UINT64;
            } else {
                if (i_min >= INT8_MIN && i_max <= INT8_MAX)
                    return bcsv::ColumnType::INT8;
                if (i_min >= INT16_MIN && i_max <= INT16_MAX)
                    return bcsv::ColumnType::INT16;
                if (i_min >= INT32_MIN && i_max <= INT32_MAX)
                    return bcsv::ColumnType::INT32;
                return bcsv::ColumnType::INT64;
            }
        }
        if (orig == bcsv::ColumnType::DOUBLE && roundtrip_alive)
            return bcsv::ColumnType::FLOAT;
        return orig;
    }

    void visitIntegerSigned(int64_t v) {
        if (min_s > v)
            min_s = v;
        if (max_s < v)
            max_s = v;
        bcsv::ColumnType cand = deriveIntSigned(min_s, max_s);
        // Don't flip a signed column to a same-width unsigned type: it saves 0
        // bytes and only changes signedness (mirrors the explicit INT64→UINT64
        // guard in deriveIntSigned).  BOOL (values in {0,1}) is a genuine
        // narrowing and is always kept.
        if (cand != bcsv::ColumnType::BOOL &&
            bcsv::sizeOf(cand) >= bcsv::sizeOf(original_type))
            cand = original_type;
        optimal_type = cand;
    }

    void visitIntegerUnsigned(uint64_t v) {
        if (min_u > v)
            min_u = v;
        if (max_u < v)
            max_u = v;
        optimal_type = deriveIntUnsigned(min_u, max_u);
    }

    void accumulateFinite(double v) {
        // Bool check: only exact 0.0 and 1.0 qualify (-0.0 == 0.0 in IEEE)
        if (v != 0.0 && v != 1.0)
            f_bool_alive = false;

        // Integer ladder: value is a whole number within tol of some integer.
        // With tol>0 the target int is round(v); the convert path also uses
        // std::round (coerce()), so probe and convert stay consistent.
        if (f_ladder_alive) {
            const double r = std::round(v);
            if (std::fabs(v - r) > tol) {
                f_ladder_alive = false;
            } else {
                if (r < 0.0)
                    int_all_positive = false;
                // Range guard: r must be castable to int64 ([-2^63, 2^63));
                // bounds are asymmetric (-2^63 valid, +2^63 not).
                if (r >= TWO_POW_63 || r < -TWO_POW_63) {
                    f_ladder_alive = false;
                } else {
                    const int64_t iv = static_cast<int64_t>(r);
                    if (iv < int_min)
                        int_min = iv;
                    if (iv > int_max)
                        int_max = iv;
                }
            }
        }

        // Float round-trip: does v survive double→float→double within tol?
        // Consulted when narrowing DOUBLE columns AND string→numeric columns to
        // FLOAT.  tol=0 is bit-exact; tol>0 allows lossy-within-tol narrowing.
        if (f_float_roundtrip) {
            const float  as_float = static_cast<float>(v);
            const double back     = static_cast<double>(as_float);
            if (std::fabs(v - back) > tol)
                f_float_roundtrip = false;
        }
    }

    void visitFloat(double v) {
        if (std::isfinite(v)) {
            // accumulateFinite updates the float round-trip flag for all origins.
            accumulateFinite(v);
        } else {
            // Non-finite values (NaN, ±Inf) cannot narrow to integer or bool
            f_ladder_alive = false;
            f_bool_alive   = false;
        }

        optimal_type = deriveFromFloat(
            f_bool_alive, f_ladder_alive, f_float_roundtrip,
            int_min, int_max, int_all_positive, original_type);
    }

    void visitString(const std::string& s) {
        if (str_done)
            return;

        if (s.empty()) {
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        if (std::isspace(static_cast<unsigned char>(s.front())) ||
            std::isspace(static_cast<unsigned char>(s.back()))) {
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        char*  endptr = nullptr;
        double d      = std::strtod(s.c_str(), &endptr);

        if (endptr != s.c_str() + s.size()) {
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        if (!std::isfinite(d)) {
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        accumulateFinite(d);

        optimal_type = deriveFromFloat(
            f_bool_alive, f_ladder_alive, f_float_roundtrip,
            int_min, int_max, int_all_positive, bcsv::ColumnType::DOUBLE);
    }

    void checkStabilization() {
        if (optimal_type == original_type) {
            alive = false;
        } else if (original_type == bcsv::ColumnType::STRING && str_done) {
            alive = false;
        }
    }
};

// ── Cast machinery: loss model + saturating coercion ────────────────
//
// Every applied cast (auto or explicit) goes through coerce() below, which
// rounds and saturates rather than throwing: the scan phase classifies each
// column first, so out-of-range / fractional cells only reach coerce() under
// forced casts, which clamp by design. Bool sources are mapped to uint8 by the
// call sites, so the integer paths never instantiate for bool.

// Significant bits of |mag| ≤ mant_bits ?  (exact int→float / int→double test)
inline bool magFitsMantissa(uint64_t mag, int mant_bits) {
    if (mag == 0) return true;
    const int significant = std::bit_width(mag) - std::countr_zero(mag);
    return significant <= mant_bits;
}

template<typename Src>
inline uint64_t absMag(Src v) {
    if constexpr (std::is_signed_v<Src>)
        return v < 0 ? (0ull - static_cast<uint64_t>(v)) : static_cast<uint64_t>(v);
    else
        return static_cast<uint64_t>(v);
}

// Inclusive double bounds for the ≤32-bit integer types (single source of truth for
// the loss and clamp paths below). The 64-bit ranges aren't exactly representable as
// double, so INT64/UINT64 are handled separately via TWO_POW_63/64.
struct IntRangeD { double lo, hi; };
inline IntRangeD intBoundsD(bcsv::ColumnType t) {
    switch (t) {
        case bcsv::ColumnType::INT8:   return {-128.0, 127.0};
        case bcsv::ColumnType::INT16:  return {-32768.0, 32767.0};
        case bcsv::ColumnType::INT32:  return {-2147483648.0, 2147483647.0};
        case bcsv::ColumnType::UINT8:  return {0.0, 255.0};
        case bcsv::ColumnType::UINT16: return {0.0, 65535.0};
        case bcsv::ColumnType::UINT32: return {0.0, 4294967295.0};
        default:                       return {1.0, 0.0};  // empty range (nothing fits)
    }
}

// Is integral double r within the inclusive range of integer type dst?
inline bool roundedFitsInt(bcsv::ColumnType dst, double r) {
    switch (dst) {
        case bcsv::ColumnType::BOOL:   return r == 0.0 || r == 1.0;
        case bcsv::ColumnType::INT64:  return r >= -TWO_POW_63 && r < TWO_POW_63;
        case bcsv::ColumnType::UINT64: return r >= 0.0 && r < TWO_POW_64;
        case bcsv::ColumnType::INT8:  case bcsv::ColumnType::INT16:
        case bcsv::ColumnType::INT32: case bcsv::ColumnType::UINT8:
        case bcsv::ColumnType::UINT16: case bcsv::ColumnType::UINT32: {
            const IntRangeD b = intBoundsD(dst);
            return r >= b.lo && r <= b.hi;
        }
        default:                       return false;
    }
}

// ── Loss model (bcsvCast spec §9) ──
// True iff casting a cell of value v (source Src) to dst loses data within tol.
// Increments oor on a range overflow, and unparseable on a non-numeric string cell
// (which a forced cast cannot clamp — see the fail-fast check in bcsvCast main).
template<typename Src>
inline bool cellLoses(bcsv::ColumnType dst, const Src& v, double tol,
                      uint64_t& oor, uint64_t& unparseable) {
    if constexpr (std::is_same_v<Src, std::string>) {
        if (dst == bcsv::ColumnType::STRING) return false;
        if (v.empty()) { ++unparseable; return true; }
        const char* s   = v.c_str();
        char*       end = nullptr;
        double      d   = std::strtod(s, &end);
        if (end != s + v.size()) { ++unparseable; return true; }  // trailing junk / unparseable
        return cellLoses<double>(dst, d, tol, oor, unparseable);
    } else {
        (void)unparseable;                                 // only string cells can be unparseable
        if (dst == bcsv::ColumnType::STRING) return false; // any → string is lossless
        if constexpr (std::is_integral_v<Src>) {           // integer source (never bool)
            switch (dst) {
                case bcsv::ColumnType::BOOL:   return !(v == Src(0) || v == Src(1));
                case bcsv::ColumnType::FLOAT:  return !magFitsMantissa(absMag(v), 24);
                case bcsv::ColumnType::DOUBLE: return !magFitsMantissa(absMag(v), 53);
                case bcsv::ColumnType::INT8:   if (!std::in_range<int8_t>(v))   { ++oor; return true; } return false;
                case bcsv::ColumnType::INT16:  if (!std::in_range<int16_t>(v))  { ++oor; return true; } return false;
                case bcsv::ColumnType::INT32:  if (!std::in_range<int32_t>(v))  { ++oor; return true; } return false;
                case bcsv::ColumnType::INT64:  if (!std::in_range<int64_t>(v))  { ++oor; return true; } return false;
                case bcsv::ColumnType::UINT8:  if (!std::in_range<uint8_t>(v))  { ++oor; return true; } return false;
                case bcsv::ColumnType::UINT16: if (!std::in_range<uint16_t>(v)) { ++oor; return true; } return false;
                case bcsv::ColumnType::UINT32: if (!std::in_range<uint32_t>(v)) { ++oor; return true; } return false;
                case bcsv::ColumnType::UINT64: if (!std::in_range<uint64_t>(v)) { ++oor; return true; } return false;
                default: return true;
            }
        } else {                                           // float / double source
            const double dv = static_cast<double>(v);
            switch (dst) {
                case bcsv::ColumnType::DOUBLE: return false;   // float→double exact
                case bcsv::ColumnType::FLOAT: {
                    if (std::isnan(dv)) return false;          // NaN canonicalization accepted
                    const float  f    = static_cast<float>(dv);
                    const double back = static_cast<double>(f);
                    return std::fabs(dv - back) > tol;
                }
                case bcsv::ColumnType::BOOL:   return !(dv == 0.0 || dv == 1.0);
                default: {                                     // integer target
                    if (!std::isfinite(dv)) return true;       // NaN/±Inf → int = loss
                    const double r = std::round(dv);
                    if (std::fabs(dv - r) > tol) return true;  // not whole within tol
                    if (!roundedFitsInt(dst, r)) { ++oor; return true; }
                    return false;
                }
            }
        }
    }
}

// ── Saturating coercion (used for every applied cast) ──
template<typename T, typename Src>
inline T satToInt(Src v, uint64_t& clamped) {
    if (std::in_range<T>(v)) return static_cast<T>(v);
    ++clamped;
    if constexpr (std::is_signed_v<Src>)
        return v < 0 ? std::numeric_limits<T>::min() : std::numeric_limits<T>::max();
    else
        return std::numeric_limits<T>::max();
}

inline std::string numToString(float v) {
    char buf[64];
    auto r = bcsv::compat::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
}
inline std::string numToString(double v) {
    char buf[64];
    auto r = bcsv::compat::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
}

inline bcsv::ValueType zeroOf(bcsv::ColumnType dst) {
    switch (dst) {
        case bcsv::ColumnType::INT8:   return bcsv::ValueType{int8_t(0)};
        case bcsv::ColumnType::INT16:  return bcsv::ValueType{int16_t(0)};
        case bcsv::ColumnType::INT32:  return bcsv::ValueType{int32_t(0)};
        case bcsv::ColumnType::INT64:  return bcsv::ValueType{int64_t(0)};
        case bcsv::ColumnType::UINT8:  return bcsv::ValueType{uint8_t(0)};
        case bcsv::ColumnType::UINT16: return bcsv::ValueType{uint16_t(0)};
        case bcsv::ColumnType::UINT32: return bcsv::ValueType{uint32_t(0)};
        case bcsv::ColumnType::UINT64: return bcsv::ValueType{uint64_t(0)};
        default:                       return bcsv::ValueType{int64_t(0)};
    }
}

// double → integer/bool target, saturating. BOOL is handled first (NaN→true, per
// bcsvCast spec §10, since NaN≠0); then NaN→0, ±Inf/overflow→min/max, else round+clamp.
// A cell counts as clamped only when the stored value differs from the source by more
// than tol — so a within-tolerance rounding (used by --optimize/--dynamic) is not
// flagged, keeping the clamp count consistent with the loss verdict.
inline bcsv::ValueType doubleToIntForced(bcsv::ColumnType dst, double dv, double tol,
                                         uint64_t& clamped) {
    if (dst == bcsv::ColumnType::BOOL) {
        const bool b = (dv != 0.0);   // NaN != 0.0 → true
        if (!(std::fabs(dv) <= tol || std::fabs(dv - 1.0) <= tol)) ++clamped;
        return bcsv::ValueType{b};
    }
    if (std::isnan(dv)) { ++clamped; return zeroOf(dst); }

    const double r     = std::round(dv);
    auto         count = [&](double stored) { if (std::fabs(stored - dv) > tol) ++clamped; };
    auto         clampD = [&](bcsv::ColumnType t) { const IntRangeD b = intBoundsD(t); double c = std::clamp(r, b.lo, b.hi); count(c); return c; };

    switch (dst) {
        case bcsv::ColumnType::INT8:   return bcsv::ValueType{static_cast<int8_t>(clampD(dst))};
        case bcsv::ColumnType::INT16:  return bcsv::ValueType{static_cast<int16_t>(clampD(dst))};
        case bcsv::ColumnType::INT32:  return bcsv::ValueType{static_cast<int32_t>(clampD(dst))};
        case bcsv::ColumnType::UINT8:  return bcsv::ValueType{static_cast<uint8_t>(clampD(dst))};
        case bcsv::ColumnType::UINT16: return bcsv::ValueType{static_cast<uint16_t>(clampD(dst))};
        case bcsv::ColumnType::UINT32: return bcsv::ValueType{static_cast<uint32_t>(clampD(dst))};
        case bcsv::ColumnType::INT64:
            if (r < -TWO_POW_63) { count(-TWO_POW_63); return bcsv::ValueType{std::numeric_limits<int64_t>::min()}; }
            if (r >= TWO_POW_63) { count(TWO_POW_63);  return bcsv::ValueType{std::numeric_limits<int64_t>::max()}; }
            count(r); return bcsv::ValueType{static_cast<int64_t>(r)};
        case bcsv::ColumnType::UINT64:
            if (r < 0.0)         { count(0.0);         return bcsv::ValueType{uint64_t(0)}; }
            if (r >= TWO_POW_64) { count(TWO_POW_64);  return bcsv::ValueType{std::numeric_limits<uint64_t>::max()}; }
            count(r); return bcsv::ValueType{static_cast<uint64_t>(r)};
        default: throw std::runtime_error("doubleToIntForced: bad target");
    }
}

// Coerce one numeric cell (Src ∈ int8..uint64, float, double) to dst, saturating.
// `tol` relaxes the "clamped" count for float/double sources (integer casts are exact).
template<typename Src>
inline bcsv::ValueType coerce(bcsv::ColumnType dst, Src v, double tol, uint64_t& clamped) {
    if constexpr (std::is_integral_v<Src>) {           // integer source (never bool)
        (void)tol;                                     // integer casts are exact
        switch (dst) {
            case bcsv::ColumnType::STRING: return bcsv::ValueType{std::to_string(v)};
            case bcsv::ColumnType::BOOL:
                if (!(v == Src(0) || v == Src(1))) ++clamped;
                return bcsv::ValueType{v != Src(0)};
            case bcsv::ColumnType::INT8:   return bcsv::ValueType{satToInt<int8_t>(v, clamped)};
            case bcsv::ColumnType::INT16:  return bcsv::ValueType{satToInt<int16_t>(v, clamped)};
            case bcsv::ColumnType::INT32:  return bcsv::ValueType{satToInt<int32_t>(v, clamped)};
            case bcsv::ColumnType::INT64:  return bcsv::ValueType{satToInt<int64_t>(v, clamped)};
            case bcsv::ColumnType::UINT8:  return bcsv::ValueType{satToInt<uint8_t>(v, clamped)};
            case bcsv::ColumnType::UINT16: return bcsv::ValueType{satToInt<uint16_t>(v, clamped)};
            case bcsv::ColumnType::UINT32: return bcsv::ValueType{satToInt<uint32_t>(v, clamped)};
            case bcsv::ColumnType::UINT64: return bcsv::ValueType{satToInt<uint64_t>(v, clamped)};
            case bcsv::ColumnType::FLOAT:
                if (!magFitsMantissa(absMag(v), 24)) ++clamped;
                return bcsv::ValueType{static_cast<float>(v)};
            case bcsv::ColumnType::DOUBLE:
                if (!magFitsMantissa(absMag(v), 53)) ++clamped;
                return bcsv::ValueType{static_cast<double>(v)};
            default: throw std::runtime_error("coerce: bad integer-source target");
        }
    } else {                                           // float / double source
        switch (dst) {
            case bcsv::ColumnType::STRING: return bcsv::ValueType{numToString(v)};
            case bcsv::ColumnType::DOUBLE: return bcsv::ValueType{static_cast<double>(v)};
            case bcsv::ColumnType::FLOAT: {
                const double dv = static_cast<double>(v);
                const float  f  = static_cast<float>(dv);
                if (!std::isnan(dv) && std::fabs(static_cast<double>(f) - dv) > tol) ++clamped;
                return bcsv::ValueType{f};
            }
            default: return doubleToIntForced(dst, static_cast<double>(v), tol, clamped);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// CSV string-cell classification + inference probe (csv2bcsv)
// ════════════════════════════════════════════════════════════════════

/// Classification of one raw CSV cell (whitespace-trimmed, unquoted).
enum class CellClass : uint8_t {
    Empty,        // ""
    IntUnsigned,  // pure integer syntax, fits uint64 (value in CellValue::u)
    IntSigned,    // pure integer syntax, negative, fits int64 (CellValue::i)
    HugeInt,      // pure integer syntax but overflows both int64 and uint64
    FloatNum,     // finite numeric (fraction/exponent, or by fallthrough; CellValue::d)
    NonFinite,    // numeric inf/nan literal (CellValue::d)
    HugeFloat,    // numeric syntax but overflows double (e.g. "1e400")
    NonNumeric    // anything else
};

struct CellValue {
    uint64_t u = 0;
    int64_t  i = 0;
    double   d = 0.0;
};

/// True when `s` is one of the boolean tokens {true, false, 1, 0}
/// (case-insensitive); the parsed value is stored in `out`.
inline bool parseBoolToken(std::string_view s, bool& out) {
    switch (s.size()) {
        case 1:
            if (s[0] == '1') { out = true;  return true; }
            if (s[0] == '0') { out = false; return true; }
            return false;
        case 4:
            if ((s[0] == 't' || s[0] == 'T') && (s[1] == 'r' || s[1] == 'R') &&
                (s[2] == 'u' || s[2] == 'U') && (s[3] == 'e' || s[3] == 'E')) {
                out = true;
                return true;
            }
            return false;
        case 5:
            if ((s[0] == 'f' || s[0] == 'F') && (s[1] == 'a' || s[1] == 'A') &&
                (s[2] == 'l' || s[2] == 'L') && (s[3] == 's' || s[3] == 'S') &&
                (s[4] == 'e' || s[4] == 'E')) {
                out = false;
                return true;
            }
            return false;
        default:
            return false;
    }
}

/// Classify one trimmed, unquoted CSV cell. Integer syntax is parsed exactly
/// via from_chars<uint64/int64> FIRST so large IDs keep full precision (strtod
/// would round anything above 2^53). `scratch` is a reusable buffer for the
/// decimal-separator normalization (only touched when decimal_sep != '.').
/// A single leading '+' is accepted (strtod semantics; from_chars rejects it).
inline CellClass classifyCell(std::string_view cell, char decimal_sep,
                              std::string& scratch, CellValue& out) {
    if (cell.empty())
        return CellClass::Empty;

    if (decimal_sep != '.' && cell.find(decimal_sep) != std::string_view::npos) {
        scratch.assign(cell);
        scratch[scratch.find(decimal_sep)] = '.';
        cell = scratch;
    }

    if (cell.front() == '+') {
        cell.remove_prefix(1);
        if (cell.empty() || cell.front() == '-' || cell.front() == '+')
            return CellClass::NonNumeric;
    }

    const char* b = cell.data();
    const char* e = b + cell.size();

    // One cheap syntax scan decides integer vs float handling so fractional
    // cells don't pay two doomed integer from_chars attempts (and vice versa).
    bool int_syntax = true;
    {
        const char* s = b + (cell.front() == '-' ? 1 : 0);
        if (s == e)
            int_syntax = false;
        for (; s != e; ++s) {
            if (*s < '0' || *s > '9') {
                int_syntax = false;
                break;
            }
        }
    }

    if (int_syntax) {
        // Exact integer parse (uint64 covers all non-negatives).
        if (cell.front() != '-') {
            auto [p, ec] = std::from_chars(b, e, out.u);
            if (p == e && ec == std::errc{})
                return CellClass::IntUnsigned;
            return CellClass::HugeInt;   // pure digits, > uint64 max
        }
        auto [p, ec] = std::from_chars(b, e, out.i);
        if (p == e && ec == std::errc{})
            return CellClass::IntSigned;
        return CellClass::HugeInt;       // < int64 min
    }

    auto [p, ec] = bcsv::compat::from_chars(b, e, out.d);
    if (p == e) {
        if (ec == std::errc{})
            return std::isfinite(out.d) ? CellClass::FloatNum : CellClass::NonFinite;
        if (ec == std::errc::result_out_of_range) {
            // Overflow vs underflow: from_chars reports both as out_of_range
            // without a value. strtod distinguishes: ±HUGE_VAL = overflow,
            // anything else is an underflow-to-(sub)normal/zero we accept.
            errno = 0;
            char*  end = nullptr;
            double d   = std::strtod(std::string(cell).c_str(), &end);
            if (d == HUGE_VAL || d == -HUGE_VAL)
                return CellClass::HugeFloat;
            out.d = d;
            return CellClass::FloatNum;
        }
    }
    return CellClass::NonNumeric;
}

/// Streaming type-inference probe over raw CSV cells for one column.
/// Semantics (csv2bcsv inference lattice; STRING is the top):
///   - any non-numeric, non-bool cell → STRING (clearly a string, no warning)
///   - all cells bool tokens          → BOOL
///   - all pure-integer syntax        → smallest UINT/INT via the bcsvCast rules
///   - numeric but not losslessly representable in int64/uint64/double
///     (HugeInt, double overflow, negatives mixed with values > int64 max,
///      integers not exactly double-representable mixed with fractions)
///                                    → STRING with overflowWarning()
///   - fraction/exponent cells        → whole-within-tolerance ladder to ints,
///     else FLOAT when the double→float→double round-trip holds within
///     tolerance, else DOUBLE (inference never picks FLOAT16/FLOAT128)
///   - empty cells are ignored; an all-empty column stays STRING
struct CsvColumnProbe {
    size_t non_empty       = 0;
    bool   all_bool        = true;   // every non-empty cell is a bool token
    bool   textual_bool    = false;  // saw a textual true/false token
    bool   non_numeric     = false;  // saw a clearly-non-numeric cell
    bool   pure_int        = true;   // no fraction/exponent/non-finite cell yet
    bool   saw_negative    = false;
    bool   pos_gt_i64      = false;  // a positive integer above int64 max
    bool   huge            = false;  // HugeInt / double-overflow cell seen
    bool   int_inexact_d   = false;  // integer cell not exactly double-representable
    uint64_t umax          = 0;      // max non-negative integer
    int64_t  smin          = std::numeric_limits<int64_t>::max();
    int64_t  smax          = std::numeric_limits<int64_t>::lowest();

    // Whole-number ladder + float round-trip machinery (shared with bcsvCast).
    ColumnProbeState core;

    void init(double tolerance) {
        core.init(bcsv::ColumnType::DOUBLE, 0, false, tolerance);
    }

    /// True when numeric data cannot be held losslessly by any 64-bit integer
    /// or double — the column must become STRING with a warning. Single source
    /// for settled()/derive()/overflowWarning() so they cannot drift apart.
    bool numericOverflow() const {
        return huge || (saw_negative && pos_gt_i64) || (!pure_int && int_inexact_d);
    }

    /// True once the derived type can no longer change — callers may skip
    /// further cells of this column. NOTE: textual_bool is terminal only once
    /// the bool ladder is dead; while all_bool holds, the column may still be
    /// BOOL and a later cell can legally push it anywhere in the lattice.
    bool settled() const {
        return non_numeric || (textual_bool && !all_bool) || numericOverflow();
    }

    void visit(std::string_view trimmed_cell, char decimal_sep, std::string& scratch) {
        CellValue v;
        const CellClass cls = classifyCell(trimmed_cell, decimal_sep, scratch, v);
        if (cls == CellClass::Empty)
            return;
        ++non_empty;

        bool bool_val = false;
        // Once the bool ladder is dead, the token check only matters for the
        // textual_bool flag on non-numeric cells (handled below).
        const bool is_bool_token = (all_bool || cls == CellClass::NonNumeric) &&
                                   parseBoolToken(trimmed_cell, bool_val);
        if (!is_bool_token)
            all_bool = false;

        switch (cls) {
            case CellClass::IntUnsigned: {
                if (v.u > umax) umax = v.u;
                if (v.u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                    pos_gt_i64 = true;
                else {
                    if (static_cast<int64_t>(v.u) > smax) smax = static_cast<int64_t>(v.u);
                    if (static_cast<int64_t>(v.u) < smin) smin = static_cast<int64_t>(v.u);
                }
                if (!magFitsMantissa(v.u, 53))
                    int_inexact_d = true;
                core.accumulateFinite(static_cast<double>(v.u));
                break;
            }
            case CellClass::IntSigned: {
                saw_negative = true;
                if (v.i < smin) smin = v.i;
                if (v.i > smax) smax = v.i;
                if (!magFitsMantissa(absMag(v.i), 53))
                    int_inexact_d = true;
                core.accumulateFinite(static_cast<double>(v.i));
                break;
            }
            case CellClass::FloatNum:
                pure_int = false;
                core.accumulateFinite(v.d);
                break;
            case CellClass::NonFinite:
                pure_int = false;
                core.f_ladder_alive = false;
                core.f_bool_alive   = false;
                break;
            case CellClass::HugeInt:
            case CellClass::HugeFloat:
                huge = true;
                if (cls == CellClass::HugeFloat)
                    pure_int = false;
                break;
            case CellClass::NonNumeric:
                if (is_bool_token)
                    textual_bool = true;   // "true"/"false" — fine while all_bool holds
                else
                    non_numeric = true;
                break;
            case CellClass::Empty:
                break;  // unreachable
        }
    }

    /// The derived column type over everything visited so far.
    bcsv::ColumnType derive() const {
        if (non_empty == 0)
            return bcsv::ColumnType::STRING;
        if (all_bool)
            return bcsv::ColumnType::BOOL;
        if (non_numeric || textual_bool)
            return bcsv::ColumnType::STRING;             // clearly strings — no warning
        if (numericOverflow())
            return bcsv::ColumnType::STRING;             // numeric overflow — warning
        if (pure_int) {
            if (!saw_negative)
                return ColumnProbeState::deriveIntUnsigned(0, umax);
            return ColumnProbeState::deriveIntSigned(smin, smax);
        }
        if (core.f_ladder_alive) {
            if (core.int_all_positive)
                return ColumnProbeState::deriveIntUnsigned(
                    0, static_cast<uint64_t>(core.int_max < 0 ? 0 : core.int_max));
            return ColumnProbeState::deriveIntSigned(core.int_min, core.int_max);
        }
        return core.f_float_roundtrip ? bcsv::ColumnType::FLOAT : bcsv::ColumnType::DOUBLE;
    }

    /// True when derive() returned STRING because numeric data cannot be held
    /// losslessly by any 64-bit integer or double — the caller should warn.
    bool overflowWarning() const {
        if (non_empty == 0 || all_bool || non_numeric || textual_bool)
            return false;
        return numericOverflow();
    }
};

// Read column i's typed cell from `row` and invoke vis(value). BOOL is surfaced
// as uint8 (0/1) so downstream templates (cellLoses/coerce) never instantiate for
// bool. This is the single dispatch shared by the loss scan and the convert pass.
template<typename RowT, typename Visitor>
inline void visitTyped(const RowT& row, size_t i, bcsv::ColumnType t, Visitor&& vis) {
    switch (t) {
        case bcsv::ColumnType::BOOL:   row.template visitConst<bool>(i, [&](size_t, const bool& b) { vis(static_cast<uint8_t>(b ? 1 : 0)); }, 1); break;
        case bcsv::ColumnType::INT8:   row.template visitConst<int8_t>(i, [&](size_t, const int8_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::INT16:  row.template visitConst<int16_t>(i, [&](size_t, const int16_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::INT32:  row.template visitConst<int32_t>(i, [&](size_t, const int32_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::INT64:  row.template visitConst<int64_t>(i, [&](size_t, const int64_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::UINT8:  row.template visitConst<uint8_t>(i, [&](size_t, const uint8_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::UINT16: row.template visitConst<uint16_t>(i, [&](size_t, const uint16_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::UINT32: row.template visitConst<uint32_t>(i, [&](size_t, const uint32_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::UINT64: row.template visitConst<uint64_t>(i, [&](size_t, const uint64_t& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::FLOAT:  row.template visitConst<float>(i, [&](size_t, const float& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::DOUBLE: row.template visitConst<double>(i, [&](size_t, const double& v) { vis(v); }, 1); break;
        case bcsv::ColumnType::STRING: row.template visitConst<std::string>(i, [&](size_t, const std::string& s) { vis(s); }, 1); break;
        default: break;
    }
}

} // namespace bcsv_cli
