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
 * @file comparison.h
 * @brief Shared file-comparison primitives for BCSV tools
 *
 * Provides multi-mode cell-by-cell comparison (strict / compatible / value),
 * layout checking, mismatch collection, and human-readable reporting.
 *
 * Used by:
 *   src/tools/bcsvCompare.cpp    — dedicated diff CLI
 *   src/tools/bcsvValidate.cpp   — comparison mode (future migration)
 */

#include <bcsv/bcsv.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bcsv_compare {

    /// Comparison strictness.
    enum class CompareMode {
        STRICT,
        COMPATIBLE,
        VALUE
    };

    /// Mismatch kind.
    enum class MismatchKind {
        VALUE,
        NAME,
        TYPE,
        COLUMN_COUNT_MISMATCH,
        ROW_COUNT_MISMATCH,
    };

    /// One mismatch between two cells (or a structural discrepancy).
    struct Mismatch {
        MismatchKind kind;
        size_t       row; // 0-based; ~size_t{0} for structural
        size_t       col;
        std::string  file_a_val;
        std::string  file_b_val;
        std::string  file_a_type;
        std::string  file_b_type;
    };

    /// Result of comparing two files.
    struct FileComparisonResult {
        bool                  identical;
        size_t                rows_a, rows_b;
        size_t                cols_a, cols_b;
        double                tolerance;
        CompareMode           mode;
        std::vector<Mismatch> mismatches;
        size_t                total_value_mismatches = 0; // all value mismatches before cap
    };

    // ── helpers ─────────────────────────────────────────────────────

    /// Stringify a single cell (mirrors validation.h for consistency).
    inline std::string stringifyCell(const bcsv::Row& row, size_t col) {
        std::string result;
        row.visitConst(col, [&](size_t, const auto& v) {
            std::ostringstream ss;
            using VT = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<VT, int8_t> || std::is_same_v<VT, uint8_t>)
                ss << static_cast<int>(v);
            else if constexpr (std::is_same_v<VT, bool>)
                ss << (v ? "true" : "false");
            else
                ss << v;
            result = ss.str();
        });
        return result;
    }

    /// Convert a numeric or bool cell value to double.
    inline std::optional<double> cellToDouble(const bcsv::Row& row, size_t col,
                                              const bcsv::Layout& layout) {
        bcsv::ColumnType ct = layout.columnType(col);
        switch (ct) {
            case bcsv::ColumnType::BOOL:
                return static_cast<double>(row.get<bool>(col) ? 1.0 : 0.0);
            case bcsv::ColumnType::INT8: return static_cast<double>(row.get<int8_t>(col));
            case bcsv::ColumnType::INT16: return static_cast<double>(row.get<int16_t>(col));
            case bcsv::ColumnType::INT32: return static_cast<double>(row.get<int32_t>(col));
            case bcsv::ColumnType::INT64: return static_cast<double>(row.get<int64_t>(col));
            case bcsv::ColumnType::UINT8: return static_cast<double>(row.get<uint8_t>(col));
            case bcsv::ColumnType::UINT16: return static_cast<double>(row.get<uint16_t>(col));
            case bcsv::ColumnType::UINT32: return static_cast<double>(row.get<uint32_t>(col));
            case bcsv::ColumnType::UINT64: return static_cast<double>(row.get<uint64_t>(col));
            case bcsv::ColumnType::FLOAT: return static_cast<double>(row.get<float>(col));
            case bcsv::ColumnType::DOUBLE: return row.get<double>(col);
            default: return std::nullopt;
        }
    }

    // ── type predicates ─────────────────────────────────────────────

    inline bool isInteger(bcsv::ColumnType t) noexcept {
        return t == bcsv::ColumnType::BOOL || t == bcsv::ColumnType::UINT8 ||
               t == bcsv::ColumnType::UINT16 || t == bcsv::ColumnType::UINT32 ||
               t == bcsv::ColumnType::UINT64 || t == bcsv::ColumnType::INT8 ||
               t == bcsv::ColumnType::INT16 || t == bcsv::ColumnType::INT32 ||
               t == bcsv::ColumnType::INT64;
    }

    inline bool isSignedInt(bcsv::ColumnType t) noexcept {
        return t == bcsv::ColumnType::INT8 || t == bcsv::ColumnType::INT16 ||
               t == bcsv::ColumnType::INT32 || t == bcsv::ColumnType::INT64;
    }

    inline bool isFloat(bcsv::ColumnType t) noexcept {
        return t == bcsv::ColumnType::FLOAT || t == bcsv::ColumnType::DOUBLE;
    }

    inline bool isSmallInt(bcsv::ColumnType t) noexcept {
        return t == bcsv::ColumnType::BOOL || t == bcsv::ColumnType::INT8 ||
               t == bcsv::ColumnType::UINT8 || t == bcsv::ColumnType::INT16 ||
               t == bcsv::ColumnType::UINT16;
    }

    // ── integer extraction helper ───────────────────────────────────

    inline void extractIntVal(const bcsv::Row& row, size_t col, bcsv::ColumnType intType,
                              int64_t& sVal, uint64_t& uVal) {
        assert(isInteger(intType) && "extractIntVal called on non-integer column");
        switch (intType) {
            case bcsv::ColumnType::INT8: (void)row.get(col, sVal); break;
            case bcsv::ColumnType::INT16: (void)row.get(col, sVal); break;
            case bcsv::ColumnType::INT32: (void)row.get(col, sVal); break;
            case bcsv::ColumnType::INT64: (void)row.get(col, sVal); break;
            case bcsv::ColumnType::UINT8: (void)row.get(col, uVal); break;
            case bcsv::ColumnType::UINT16: (void)row.get(col, uVal); break;
            case bcsv::ColumnType::UINT32: (void)row.get(col, uVal); break;
            case bcsv::ColumnType::UINT64: (void)row.get(col, uVal); break;
            case bcsv::ColumnType::BOOL: {
                bool b = false;
                (void)row.get(col, b);
                uVal = b ? 1 : 0;
                break;
            }
            default: break;
        }
    }

    // ── CompareTarget enum + promoteType ────────────────────────────

    enum class CompareTarget {
        SAME,
        INT_EQUAL,
        PROMOTE_FLOAT,
        PROMOTE_DOUBLE,
        STR_VS_NUM,
        MISMATCH
    };

    inline CompareTarget promoteType(bcsv::ColumnType a, bcsv::ColumnType b) noexcept {
        if (a == b)
            return CompareTarget::SAME;
        if (a == bcsv::ColumnType::VOID || b == bcsv::ColumnType::VOID)
            return CompareTarget::MISMATCH;

        if (isInteger(a) && isInteger(b))
            return CompareTarget::INT_EQUAL;

        if (isFloat(a) && isFloat(b))
            return CompareTarget::PROMOTE_DOUBLE;

        if (isInteger(a) && isFloat(b)) {
            if (isSmallInt(a) && b == bcsv::ColumnType::FLOAT)
                return CompareTarget::PROMOTE_FLOAT;
            return CompareTarget::PROMOTE_DOUBLE;
        }
        if (isFloat(a) && isInteger(b)) {
            if (isSmallInt(b) && a == bcsv::ColumnType::FLOAT)
                return CompareTarget::PROMOTE_FLOAT;
            return CompareTarget::PROMOTE_DOUBLE;
        }

        if (a == bcsv::ColumnType::STRING && b == bcsv::ColumnType::STRING)
            return CompareTarget::SAME;

        if (a == bcsv::ColumnType::STRING || b == bcsv::ColumnType::STRING)
            return CompareTarget::STR_VS_NUM;

        return CompareTarget::MISMATCH;
    }

    // ── cell comparison ─────────────────────────────────────────────

    /// NaN semantics differ across comparison paths:
    ///   - Same-type FLOAT/DOUBLE: NaN == NaN is considered equal.
    ///   - Cross-type (INT vs FLOAT/DOUBLE, STRING vs numeric): NaN is always
    ///     a mismatch.  This is intentional — comparing an integer to NaN
    ///     cannot yield "equal" in any useful sense.

    /// Strict: ColumnType overload for same-type fast path.
    inline bool compareCellStrict(const bcsv::Row& rowA, const bcsv::Row& rowB,
                                  size_t col, bcsv::ColumnType ct, double tolerance) {
        switch (ct) {
            case bcsv::ColumnType::BOOL: return rowA.get<bool>(col) == rowB.get<bool>(col);
            case bcsv::ColumnType::INT8: return rowA.get<int8_t>(col) == rowB.get<int8_t>(col);
            case bcsv::ColumnType::INT16: return rowA.get<int16_t>(col) == rowB.get<int16_t>(col);
            case bcsv::ColumnType::INT32: return rowA.get<int32_t>(col) == rowB.get<int32_t>(col);
            case bcsv::ColumnType::INT64: return rowA.get<int64_t>(col) == rowB.get<int64_t>(col);
            case bcsv::ColumnType::UINT8: return rowA.get<uint8_t>(col) == rowB.get<uint8_t>(col);
            case bcsv::ColumnType::UINT16: return rowA.get<uint16_t>(col) == rowB.get<uint16_t>(col);
            case bcsv::ColumnType::UINT32: return rowA.get<uint32_t>(col) == rowB.get<uint32_t>(col);
            case bcsv::ColumnType::UINT64: return rowA.get<uint64_t>(col) == rowB.get<uint64_t>(col);
            case bcsv::ColumnType::FLOAT: {
                float fa = rowA.get<float>(col), fb = rowB.get<float>(col);
                if (std::isnan(fa))
                    return std::isnan(fb);
                if (tolerance > 0.0)
                    return std::fabs(static_cast<double>(fa) - static_cast<double>(fb)) <= tolerance;
                return fa == fb;
            }
            case bcsv::ColumnType::DOUBLE: {
                double da = rowA.get<double>(col), db = rowB.get<double>(col);
                if (std::isnan(da))
                    return std::isnan(db);
                if (tolerance > 0.0)
                    return std::fabs(da - db) <= tolerance;
                return da == db;
            }
            case bcsv::ColumnType::STRING:
                return rowA.get<std::string>(col) == rowB.get<std::string>(col);
            default: return true;
        }
    }

    /// Strict: same layout, exact match (+ tolerance); delegates to ColumnType overload.
    inline bool compareCellStrict(const bcsv::Row& rowA, const bcsv::Row& rowB,
                                  size_t col, const bcsv::Layout& layoutA,
                                  double tolerance) {
        return compareCellStrict(rowA, rowB, col, layoutA.columnType(col), tolerance);
    }

    /// Value mode: cross-type coercion to double; string-only when both strings.
    /// Retained for backward compatibility (bcsvValidate may use it).
    inline bool compareCellValue(const bcsv::Row& rowA, const bcsv::Row& rowB,
                                 size_t colA, size_t colB,
                                 const bcsv::Layout& layoutA, const bcsv::Layout& layoutB,
                                 double tolerance) {
        bool a_is_str = layoutA.columnType(colA) == bcsv::ColumnType::STRING;
        bool b_is_str = layoutB.columnType(colB) == bcsv::ColumnType::STRING;

        if (a_is_str && b_is_str)
            return rowA.get<std::string>(colA) == rowB.get<std::string>(colB);
        if (a_is_str || b_is_str)
            return false;

        auto da = cellToDouble(rowA, colA, layoutA);
        auto db = cellToDouble(rowB, colB, layoutB);
        if (!da.has_value() || !db.has_value())
            return false;

        if (tolerance > 0.0)
            return std::fabs(da.value() - db.value()) <= tolerance;
        return da.value() == db.value();
    }

    // ── cross-type comparison functions ─────────────────────────────

    /// Int vs int via std::cmp_equal (C++20).
    inline bool cmpEqualInteger(const bcsv::Row& rowA, size_t colA, bcsv::ColumnType ta,
                                const bcsv::Row& rowB, size_t colB, bcsv::ColumnType tb) {
        int64_t  a_s = 0, b_s = 0;
        uint64_t a_u = 0, b_u = 0;
        extractIntVal(rowA, colA, ta, a_s, a_u);
        extractIntVal(rowB, colB, tb, b_s, b_u);

        // Branch on tb *before* calling cmp_equal so the two arguments have
        // their correct distinct types.  Using a ternary inside cmp_equal
        // (e.g. cmp_equal(a, isSignedInt(tb) ? b_s : b_u)) forces the ternary
        // to produce a common type (uint64_t), which silently converts a_s to
        // uint64_t — turning a negative value into a huge unsigned and breaking
        // signed-vs-signed cross-type comparisons.
        switch (ta) {
            case bcsv::ColumnType::INT8:
            case bcsv::ColumnType::INT16:
            case bcsv::ColumnType::INT32:
            case bcsv::ColumnType::INT64:
                return isSignedInt(tb) ? std::cmp_equal(a_s, b_s) : std::cmp_equal(a_s, b_u);
            case bcsv::ColumnType::UINT8:
            case bcsv::ColumnType::UINT16:
            case bcsv::ColumnType::UINT32:
            case bcsv::ColumnType::UINT64:
                return isSignedInt(tb) ? std::cmp_equal(a_u, b_s) : std::cmp_equal(a_u, b_u);
            case bcsv::ColumnType::BOOL: {
                int64_t as = static_cast<int64_t>(a_u);
                return isSignedInt(tb)
                           ? std::cmp_equal(as, b_s)
                           : std::cmp_equal(as, static_cast<int64_t>(b_u));
            }
            default: return false;
        }
    }

    /// Int vs float/double with precision guard.
    inline bool cmpIntVsFloat(const bcsv::Row& rowInt, size_t colInt, bcsv::ColumnType intType,
                              const bcsv::Row& rowFlt, size_t colFlt, bcsv::ColumnType fltType,
                              double tolerance, bool allowImprecise, bool inFloatSpace) {
        int64_t  sVal = 0;
        uint64_t uVal = 0;
        extractIntVal(rowInt, colInt, intType, sVal, uVal);

        if (intType == bcsv::ColumnType::INT64 || intType == bcsv::ColumnType::UINT64) {
            constexpr uint64_t D53     = UINT64_C(9007199254740992);
            bool               mayLose = (intType == bcsv::ColumnType::INT64)
                                             ? (sVal < static_cast<int64_t>(-D53) || sVal > static_cast<int64_t>(D53))
                                             : (uVal > D53);
            if (mayLose && !allowImprecise)
                return false;
        }

        if (inFloatSpace) {
            float fInt = isSignedInt(intType)
                             ? static_cast<float>(sVal)
                             : static_cast<float>(uVal);
            float fFlt = 0;
            (void)rowFlt.get(colFlt, fFlt);
            if (std::isnan(fFlt))
                return false;
            return tolerance > 0.0
                       ? std::fabs(static_cast<double>(fInt) - static_cast<double>(fFlt)) <= tolerance
                       : fInt == fFlt;
        } else {
            double dInt = isSignedInt(intType)
                              ? static_cast<double>(sVal)
                              : static_cast<double>(uVal);
            double dFlt = 0;
            if (fltType == bcsv::ColumnType::FLOAT) {
                float f = 0;
                (void)rowFlt.get(colFlt, f);
                dFlt = static_cast<double>(f);
            } else {
                (void)rowFlt.get(colFlt, dFlt);
            }
            if (std::isnan(dFlt))
                return false;
            return tolerance > 0.0
                       ? std::fabs(dInt - dFlt) <= tolerance
                       : dInt == dFlt;
        }
    }

    /// FLOAT vs DOUBLE — widen to double.
    inline bool cmpFloatVsDouble(const bcsv::Row& rowA, size_t colA, bcsv::ColumnType ta,
                                 const bcsv::Row& rowB, size_t colB, bcsv::ColumnType tb,
                                 double tolerance) {
        double da = 0, db = 0;
        if (ta == bcsv::ColumnType::FLOAT) {
            float f = 0;
            (void)rowA.get(colA, f);
            da = static_cast<double>(f);
        } else {
            (void)rowA.get(colA, da);
        }
        if (tb == bcsv::ColumnType::FLOAT) {
            float f = 0;
            (void)rowB.get(colB, f);
            db = static_cast<double>(f);
        } else {
            (void)rowB.get(colB, db);
        }
        if (std::isnan(da))
            return std::isnan(db);
        return tolerance > 0.0
                   ? std::fabs(da - db) <= tolerance
                   : da == db;
    }

    /// Parse string to number with whitespace trimming.
    inline std::optional<double> parseStringToNumber(const std::string& s) {
        constexpr const char* WS    = " \t\r\n\f\v";
        size_t                begin = s.find_first_not_of(WS);
        if (begin == std::string::npos)
            return std::nullopt;
        size_t      end = s.find_last_not_of(WS);
        std::string t   = s.substr(begin, end - begin + 1);

        {
            bool hasFloatHint = t.find('.') != std::string::npos ||
                                t.find('e') != std::string::npos ||
                                t.find('E') != std::string::npos;
            if (hasFloatHint) {
                try {
                    size_t pos;
                    double d = std::stod(t, &pos);
                    if (pos == t.size() && !std::isinf(d))
                        return d;
                } catch (...) {}
            }
        }

        try {
            size_t    pos;
            long long ll = std::stoll(t, &pos);
            if (pos == t.size())
                return static_cast<double>(ll);
        } catch (...) {}

        try {
            size_t             pos;
            unsigned long long ull = std::stoull(t, &pos);
            if (pos == t.size())
                return static_cast<double>(ull);
        } catch (...) {}

        return std::nullopt;
    }

    /// String vs numeric comparison.
    inline bool cmpStrVsNum(
        const bcsv::Row& rowA, size_t colA, bcsv::ColumnType ta,
        const bcsv::Row& rowB, size_t colB, bcsv::ColumnType tb,
        double tolerance) {

        bool               aIsStr = ta == bcsv::ColumnType::STRING;
        const std::string& str    = aIsStr
                                        ? rowA.get<std::string>(colA)
                                        : rowB.get<std::string>(colB);
        auto               parsed = parseStringToNumber(str);
        if (!parsed)
            return false;

        bcsv::ColumnType numType = aIsStr ? tb : ta;
        const bcsv::Row& numRow  = aIsStr ? rowB : rowA;
        size_t           numCol  = aIsStr ? colB : colA;

        double numVal = 0;
        if (isInteger(numType)) {
            int64_t  s = 0;
            uint64_t u = 0;
            extractIntVal(numRow, numCol, numType, s, u);
            numVal = isSignedInt(numType) ? static_cast<double>(s) : static_cast<double>(u);
        } else if (numType == bcsv::ColumnType::FLOAT) {
            float f = 0;
            (void)numRow.get(numCol, f);
            numVal = static_cast<double>(f);
        } else {
            (void)numRow.get(numCol, numVal);
        }

        return tolerance > 0.0
                   ? std::fabs(parsed.value() - numVal) <= tolerance
                   : parsed.value() == numVal;
    }

    /// Hot-loop dispatcher.
    inline bool compareCellWithStrategy(
        const bcsv::Row& rowA, size_t colA,
        const bcsv::Row& rowB, size_t colB,
        bcsv::ColumnType ta, bcsv::ColumnType tb,
        CompareTarget target,
        double tolerance, bool allowImprecise, bool stringToValue) {
        switch (target) {
            case CompareTarget::SAME:
                return compareCellStrict(rowA, rowB, colA, ta, tolerance);

            case CompareTarget::INT_EQUAL:
                return cmpEqualInteger(rowA, colA, ta, rowB, colB, tb);

            case CompareTarget::PROMOTE_DOUBLE:
                if (isFloat(ta) && isFloat(tb)) {
                    return cmpFloatVsDouble(rowA, colA, ta, rowB, colB, tb, tolerance);
                }
                if (isInteger(ta))
                    return cmpIntVsFloat(rowA, colA, ta, rowB, colB, tb,
                                         tolerance, allowImprecise, false);
                else
                    return cmpIntVsFloat(rowB, colB, tb, rowA, colA, ta,
                                         tolerance, allowImprecise, false);

            case CompareTarget::PROMOTE_FLOAT:
                if (isInteger(ta))
                    return cmpIntVsFloat(rowA, colA, ta, rowB, colB, tb,
                                         tolerance, allowImprecise, true);
                else
                    return cmpIntVsFloat(rowB, colB, tb, rowA, colA, ta,
                                         tolerance, allowImprecise, true);

            case CompareTarget::STR_VS_NUM:
                if (!stringToValue)
                    return false;
                return cmpStrVsNum(rowA, colA, ta, rowB, colB, tb, tolerance);

            case CompareTarget::MISMATCH:
                return false;
        }
        return false;
    }

    // ── mismatch recording ──────────────────────────────────────────

    inline void recordStrictValueMismatch(size_t row, size_t col,
                                          const bcsv::Row& rowA, const bcsv::Row& rowB,
                                          const bcsv::Layout& layoutA,
                                          Mismatch&           m) {
        m.kind        = MismatchKind::VALUE;
        m.row         = row;
        m.col         = col;
        m.file_a_val  = stringifyCell(rowA, col);
        m.file_b_val  = stringifyCell(rowB, col);
        m.file_a_type = bcsv::toString(layoutA.columnType(col));
        m.file_b_type = m.file_a_type;
    }

    inline void recordValueMismatch(size_t row, size_t col,
                                    const bcsv::Row& rowA, const bcsv::Row& rowB,
                                    const bcsv::Layout& layoutA, const bcsv::Layout& layoutB,
                                    Mismatch& m) {
        m.kind        = MismatchKind::VALUE;
        m.row         = row;
        m.col         = col;
        m.file_a_val  = stringifyCell(rowA, col);
        m.file_b_val  = stringifyCell(rowB, col);
        m.file_a_type = bcsv::toString(layoutA.columnType(col));
        m.file_b_type = bcsv::toString(layoutB.columnType(col));
    }

    // ── layout comparison ──────────────────────────────────────────

    /// Result of checking two layouts against each other.
    struct LayoutCheck {
        bool                  ok = true;
        std::vector<Mismatch> structural; // name/type/count mismatches
    };

    /// Compare two layouts with explicit name/type check flags.
    inline LayoutCheck compareLayouts(const bcsv::Layout& layoutA,
                                      const bcsv::Layout& layoutB,
                                      bool checkNames, bool checkTypes) {
        LayoutCheck res;

        if (layoutA.columnCount() != layoutB.columnCount()) {
            res.ok = false;
            Mismatch m;
            m.kind       = MismatchKind::COLUMN_COUNT_MISMATCH;
            m.row        = ~size_t{0};
            m.col        = ~size_t{0};
            m.file_a_val = std::to_string(layoutA.columnCount());
            m.file_b_val = std::to_string(layoutB.columnCount());
            res.structural.push_back(std::move(m));
            return res;
        }

        for (size_t c = 0; c < layoutA.columnCount(); ++c) {
            if (checkNames) {
                std::string na = layoutA.columnName(c);
                std::string nb = layoutB.columnName(c);
                if (na != nb) {
                    res.ok = false;
                    Mismatch m;
                    m.kind       = MismatchKind::NAME;
                    m.row        = ~size_t{0};
                    m.col        = c;
                    m.file_a_val = std::move(na);
                    m.file_b_val = std::move(nb);
                    res.structural.push_back(std::move(m));
                }
            }
            if (checkTypes) {
                bcsv::ColumnType ta = layoutA.columnType(c);
                bcsv::ColumnType tb = layoutB.columnType(c);
                if (ta != tb) {
                    res.ok = false;
                    Mismatch m;
                    m.kind        = MismatchKind::TYPE;
                    m.row         = ~size_t{0};
                    m.col         = c;
                    m.file_a_type = bcsv::toString(ta);
                    m.file_b_type = bcsv::toString(tb);
                    res.structural.push_back(std::move(m));
                }
            }
        }
        return res;
    }

    /// Compare two layouts.
    ///
    /// strict      → names must match, types must match.
    /// compatible  → types must match, names ignored.
    /// value       → names/types ignored (col count still enforced).
    inline LayoutCheck compareLayouts(const bcsv::Layout& layoutA,
                                      const bcsv::Layout& layoutB,
                                      CompareMode         mode) {
        return compareLayouts(layoutA, layoutB,
                              mode == CompareMode::STRICT,
                              mode != CompareMode::VALUE);
    }

    // ── human-readable summary ───────────────────────────────────

    /// Format mismatch details for verbose output.
    inline std::string formatSummary(const FileComparisonResult& r) {
        std::ostringstream ss;
        if (!r.mismatches.empty()) {
            for (const auto& m : r.mismatches) {
                switch (m.kind) {
                    case MismatchKind::VALUE:
                        ss << "  Row " << m.row << "  Col " << m.col
                           << " [" << m.file_a_type << "]: "
                           << m.file_a_val << " != " << m.file_b_val << "\n";
                        break;
                    case MismatchKind::NAME:
                        ss << "  Column name mismatch  Col " << m.col
                           << ": \"" << m.file_a_val << "\" != \"" << m.file_b_val << "\"\n";
                        break;
                    case MismatchKind::TYPE:
                        ss << "  Column type mismatch  Col " << m.col
                           << ": " << m.file_a_type << " != " << m.file_b_type << "\n";
                        break;
                    case MismatchKind::COLUMN_COUNT_MISMATCH:
                        ss << "  Column count mismatch: A=" << m.file_a_val
                           << " B=" << m.file_b_val << "\n";
                        break;
                    case MismatchKind::ROW_COUNT_MISMATCH:
                        ss << "  Row count mismatch: A=" << m.file_a_val
                           << " B=" << m.file_b_val << "\n";
                        break;
                }
            }
            if (r.total_value_mismatches > r.mismatches.size())
                ss << "  (" << r.total_value_mismatches - r.mismatches.size()
                   << " more mismatches not shown)\n";
            ss << "Result: different\n";
        } else {
            ss << "Result: identical\n";
        }
        return ss.str();
    }

} // namespace bcsv_compare