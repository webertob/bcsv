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
 * @file validation.h
 * @brief Shared validation utilities for BCSV tools, benchmarks, and tests
 *
 * Provides cell-by-cell row comparison with optional float tolerance,
 * mismatch tracking, and human-readable / JSON reporting.
 */

#include <bcsv/bcsv.h>

#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace bcsv_validation {

struct ValidationMismatch {
    size_t      row;
    size_t      col;
    std::string expected;
    std::string actual;
    std::string type;
};

/**
 * @brief Validates that two rows match cell-by-cell with optional float tolerance.
 *
 * Reports the first N mismatches with detailed diagnostic information.
 * Default tolerance is 0.0 (exact match), preserving existing benchmark behaviour.
 */
class RoundTripValidator {
public:
    explicit RoundTripValidator(size_t maxErrors = 10, double floatTol = 0.0)
        : max_errors_(maxErrors), float_tol_(floatTol) {}

    /// Compare a single cell between two rows. Returns true if match.
    bool compareCell(size_t rowIdx, size_t colIdx,
                     const bcsv::Row& expected,
                     const bcsv::Row& actual,
                     const bcsv::Layout& layout)
    {
        bool match = false;
        bcsv::ColumnType ct = layout.columnType(colIdx);

        switch (ct) {
            case bcsv::ColumnType::BOOL:   match = (expected.get<bool>(colIdx)     == actual.get<bool>(colIdx));     break;
            case bcsv::ColumnType::INT8:   match = (expected.get<int8_t>(colIdx)   == actual.get<int8_t>(colIdx));   break;
            case bcsv::ColumnType::INT16:  match = (expected.get<int16_t>(colIdx)  == actual.get<int16_t>(colIdx));  break;
            case bcsv::ColumnType::INT32:  match = (expected.get<int32_t>(colIdx)  == actual.get<int32_t>(colIdx));  break;
            case bcsv::ColumnType::INT64:  match = (expected.get<int64_t>(colIdx)  == actual.get<int64_t>(colIdx));  break;
            case bcsv::ColumnType::UINT8:  match = (expected.get<uint8_t>(colIdx)  == actual.get<uint8_t>(colIdx));  break;
            case bcsv::ColumnType::UINT16: match = (expected.get<uint16_t>(colIdx) == actual.get<uint16_t>(colIdx)); break;
            case bcsv::ColumnType::UINT32: match = (expected.get<uint32_t>(colIdx) == actual.get<uint32_t>(colIdx)); break;
            case bcsv::ColumnType::UINT64: match = (expected.get<uint64_t>(colIdx) == actual.get<uint64_t>(colIdx)); break;
            case bcsv::ColumnType::FLOAT:
                if (float_tol_ > 0.0)
                    match = std::fabs(static_cast<double>(expected.get<float>(colIdx)) -
                                     static_cast<double>(actual.get<float>(colIdx))) <= float_tol_;
                else
                    match = (expected.get<float>(colIdx) == actual.get<float>(colIdx));
                break;
            case bcsv::ColumnType::DOUBLE:
                if (float_tol_ > 0.0)
                    match = std::fabs(expected.get<double>(colIdx) - actual.get<double>(colIdx)) <= float_tol_;
                else
                    match = (expected.get<double>(colIdx) == actual.get<double>(colIdx));
                break;
            case bcsv::ColumnType::STRING:
                match = (expected.get<std::string>(colIdx) == actual.get<std::string>(colIdx));
                break;
            default: break;
        }

        if (!match && mismatches_.size() < max_errors_) {
            recordMismatch(rowIdx, colIdx, ct, expected, actual);
        }
        return match;
    }

    /// Compare all cells of two rows. Returns true if all match.
    bool compareRow(size_t rowIdx,
                    const bcsv::Row& expected,
                    const bcsv::Row& actual,
                    const bcsv::Layout& layout)
    {
        bool all_match = true;
        for (size_t c = 0; c < layout.columnCount(); ++c) {
            if (!compareCell(rowIdx, c, expected, actual, layout))
                all_match = false;
        }
        return all_match;
    }

    bool   passed()     const { return mismatches_.empty(); }
    size_t errorCount() const { return mismatches_.size(); }
    const std::vector<ValidationMismatch>& mismatches() const { return mismatches_; }

    /// Human-readable summary.
    std::string summary() const {
        if (mismatches_.empty()) return "PASSED";
        std::ostringstream ss;
        ss << "FAILED (" << mismatches_.size() << " mismatches)\n";
        for (const auto& m : mismatches_) {
            ss << "  Row " << m.row << " Col " << m.col
               << " [" << m.type << "]: expected=" << m.expected
               << " actual=" << m.actual << "\n";
        }
        return ss.str();
    }

    /// JSON array of mismatches.
    std::string toJson() const {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < mismatches_.size(); ++i) {
            const auto& m = mismatches_[i];
            if (i > 0) ss << ",";
            ss << "{\"row\":" << m.row
               << ",\"col\":" << m.col
               << ",\"type\":\"" << m.type << "\""
               << ",\"expected\":\"" << jsonEscape(m.expected) << "\""
               << ",\"actual\":\"" << jsonEscape(m.actual) << "\"}";
        }
        ss << "]";
        return ss.str();
    }

    void reset() { mismatches_.clear(); }

private:
    size_t max_errors_;
    double float_tol_;
    std::vector<ValidationMismatch> mismatches_;

    void recordMismatch(size_t rowIdx, size_t colIdx, bcsv::ColumnType ct,
                        const bcsv::Row& expected, const bcsv::Row& actual) {
        ValidationMismatch m;
        m.row  = rowIdx;
        m.col  = colIdx;
        m.type = bcsv::toString(ct);

        auto stringify = [](const bcsv::Row& row, size_t col) -> std::string {
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
        };
        m.expected = stringify(expected, colIdx);
        m.actual   = stringify(actual, colIdx);
        mismatches_.push_back(std::move(m));
    }

    static std::string jsonEscape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;      break;
            }
        }
        return out;
    }
};

} // namespace bcsv_validation
