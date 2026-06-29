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

#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <type_traits>
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

    // ── cell comparison ─────────────────────────────────────────────

    /// Strict: same layout, exact match (+ tolerance).
    inline bool compareCellStrict(const bcsv::Row& rowA, const bcsv::Row& rowB,
                                  size_t col, const bcsv::Layout& layoutA,
                                  double tolerance) {
        bcsv::ColumnType ct = layoutA.columnType(col);
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
                float fa = rowA.get<float>(col);
                float fb = rowB.get<float>(col);
                if (std::isnan(fa))
                    return std::isnan(fb);
                if (tolerance > 0.0)
                    return std::fabs(static_cast<double>(fa) - static_cast<double>(fb)) <= tolerance;
                return fa == fb;
            }
            case bcsv::ColumnType::DOUBLE: {
                double a = rowA.get<double>(col);
                double b = rowB.get<double>(col);
                if (std::isnan(a))
                    return std::isnan(b);
                if (tolerance > 0.0)
                    return std::fabs(a - b) <= tolerance;
                return a == b;
            }
            case bcsv::ColumnType::STRING: return rowA.get<std::string>(col) == rowB.get<std::string>(col);
            default: return true;
        }
    }

    /// Value mode: cross-type coercion to double; string-only when both strings.
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

    /// Compare two layouts.
    ///
    /// strict      → names must match, types must match.
    /// compatible  → types must match, names ignored.
    /// value       → names/types ignored (col count still enforced).
    inline LayoutCheck compareLayouts(const bcsv::Layout& layoutA,
                                      const bcsv::Layout& layoutB,
                                      CompareMode         mode) {
        LayoutCheck res;
        bool        check_names = (mode == CompareMode::STRICT);
        bool        check_types = (mode == CompareMode::STRICT || mode == CompareMode::COMPATIBLE);

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

        res.ok = true;
        for (size_t c = 0; c < layoutA.columnCount(); ++c) {
            if (check_names) {
                std::string na = layoutA.columnName(c);
                std::string nb = layoutB.columnName(c);
                if (na != nb) {
                    res.ok = false;
                    Mismatch m;
                    m.kind       = MismatchKind::NAME;
                    m.row        = ~size_t{0};
                    m.col        = c;
                    m.file_a_val = na;
                    m.file_b_val = nb;
                    res.structural.push_back(std::move(m));
                }
            }
            if (check_types) {
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