/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bcsvCompare.cpp
 * @brief Deterministic file comparison tool for BCSV
 *
 * Compares two BCSV files in three modes:
 *   strict     - column names, types, and values must match
 *   compatible - types and values must match, names ignored
 *   value      - only values must match (cross-type coercion)
 *
 * Exit codes: 0 = identical, 1 = different, 2 = error
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <bcsv/bcsv.h>
#include "cli_common.h"
#include "../shared/comparison.h"

// ================================================================
// Range specification
// ================================================================

struct RangeSpec {
    std::vector<std::pair<size_t, size_t>> ranges; // sorted, non-overlapping

    /// Returns true if `idx` falls within at least one range.
    /// Empty ranges → match-all (no user scope requested).
    bool contains(size_t idx) const noexcept {
        if (ranges.empty())
            return true;
        for (const auto& [lo, hi] : ranges)
            if (idx >= lo && idx <= hi)
                return true;
        return false;
    }
};

/** Parse "0:99,200:-10" → sorted, merged, inclusive ranges.
 * Empty spec or total == 0 → empty ranges (match-all).
 * Throws std::runtime_error if user provided a spec but no ranges are valid. */
static RangeSpec parseRanges(const std::string& spec, size_t total) {
    RangeSpec r;
    bool      specified = !spec.empty();
    if (!specified || total == 0)
        return r;

    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : spec) {
            if (c == ',') {
                parts.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        parts.push_back(cur);
    }

    for (auto& p : parts) {
        auto p0 = p;
        // inline trim
        size_t a = 0, b = p0.size();
        while (a < b && std::isspace(static_cast<unsigned char>(p0[a])))
            ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(p0[b - 1])))
            --b;
        p0 = p0.substr(a, b - a);
        if (p0.empty())
            continue;

        int64_t lo, hi;
        auto    colon = p0.find(':');
        if (colon == std::string::npos) {
            lo = hi = std::stoll(p0);
        } else {
            std::string ls = p0.substr(0, colon);
            std::string hs = p0.substr(colon + 1);
            lo             = ls.empty() ? 0 : std::stoll(ls);
            hi             = hs.empty() ? static_cast<int64_t>(total) - 1 : std::stoll(hs);
        }

        if (lo < 0)
            lo += static_cast<int64_t>(total);
        if (hi < 0)
            hi += static_cast<int64_t>(total);

        if (lo < 0 || lo > hi || hi >= static_cast<int64_t>(total)) {
            throw std::runtime_error("Range [" + std::to_string(lo) + ":" + std::to_string(hi) +
                                     "] is out of bounds (0-" + std::to_string(total - 1) + ")");
        }
        r.ranges.emplace_back(static_cast<size_t>(lo), static_cast<size_t>(hi));
    }

    if (r.ranges.empty()) {
        throw std::runtime_error("No valid " + std::string(specified && parts.size() > 1 ? "range elements" : "ranges") +
                                 " found for spec '" + spec + "'");
    }

    std::sort(r.ranges.begin(), r.ranges.end());
    {
        std::vector<std::pair<size_t, size_t>> merged;
        for (auto& el : r.ranges) {
            if (merged.empty())
                merged.push_back(el);
            else if (el.first <= merged.back().second + 1)
                merged.back().second = std::max(merged.back().second, el.second);
            else
                merged.push_back(el);
        }
        r.ranges = std::move(merged);
    }
    return r;
}

// ================================================================
// Config
// ================================================================

struct Config {
    std::string file_a, file_b;
    std::string mode_str  = "strict";
    double      tolerance = 0.0;
    std::string rows_str, cols_str;
    bool        verbose = false, help = false;
};

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [OPTIONS] FILE_A FILE_B\n\n"
        << "Compare two BCSV files and report whether they are identical.\n\n"
        << "Modes:\n"
        << "  strict      (default) - names + types + values must match\n"
        << "  compatible        - types + values must match, names ignored\n"
        << "  value             - only values must match (cross-type coercion)\n\n"
        << "Arguments:\n"
        << "  FILE_A          First BCSV file\n"
        << "  FILE_B          Second BCSV file\n\n"
        << "Options:\n"
        << "  --mode MODE       strict | compatible | value\n"
        << "  --tolerance TOL   float/double epsilon (default: 0.0)\n"
        << "  --rows RANGES     row indices to compare, e.g. 0:99,-10:\n"
        << "  --cols RANGES     column indices to compare\n"
        << "  -v, --verbose     report mismatch details\n"
        << "  -h, --help        show this help\n\n"
        << "Range syntax (inclusive):\n"
        << "  5               single index\n"
        << "  0:99            indices 0-99\n"
        << "  :100            0-100\n"
        << "  -10:            last 10\n"
        << "  0:99,200:       multiple ranges\n\n"
        << "Exit codes: 0 = identical, 1 = different, 2 = error\n\n"
        << "Examples:\n"
        << "  " << prog << " a.bcsv b.bcsv\n"
        << "  " << prog << " --mode compatible a.bcsv b.bcsv\n"
        << "  " << prog << " --mode value --tolerance 1e-6 a.bcsv b.bcsv\n"
        << "  " << prog << " --rows 0:99 --cols 2:5 a.bcsv b.bcsv\n";
}

static Config parseArgs(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            cfg.help = true;
            return cfg;
        } else if (a == "-v" || a == "--verbose") {
            cfg.verbose = true;
        } else if ((a == "--mode") && (i + 1 < argc)) {
            cfg.mode_str = argv[++i];
        } else if ((a == "--tolerance") && (i + 1 < argc)) {
            cfg.tolerance = std::stod(argv[++i]);
        } else if ((a == "--rows") && (i + 1 < argc)) {
            cfg.rows_str = argv[++i];
        } else if ((a == "--cols") && (i + 1 < argc)) {
            cfg.cols_str = argv[++i];
        } else if (a.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + a);
        } else {
            if (cfg.file_a.empty())
                cfg.file_a = a;
            else if (cfg.file_b.empty())
                cfg.file_b = a;
            else
                throw std::runtime_error("Too many arguments");
        }
    }
    if (cfg.file_a.empty())
        throw std::runtime_error("FILE_A and FILE_B are required (-h for help)");
    if (cfg.file_b.empty())
        throw std::runtime_error("FILE_B is required");
    return cfg;
}

// ================================================================
// Emit result (verbose: stdout, otherwise silent)
// ================================================================

static void emitResult(const bcsv_compare::FileComparisonResult& result, bool verbose) {
    if (verbose)
        std::cout << bcsv_compare::formatSummary(result);
}

// ================================================================
// Comparison
// ================================================================

static int compareFiles(const Config& cfg) {
    // --- mode ---
    bool                      is_value_mode = false;
    bcsv_compare::CompareMode mode;
    if (cfg.mode_str == "strict") {
        mode = bcsv_compare::CompareMode::STRICT;
    } else if (cfg.mode_str == "compatible") {
        mode = bcsv_compare::CompareMode::COMPATIBLE;
    } else if (cfg.mode_str == "value") {
        mode          = bcsv_compare::CompareMode::VALUE;
        is_value_mode = true;
    } else
        throw std::runtime_error("Unknown mode '" + cfg.mode_str +
                                 "'. Expected: strict, compatible, value.");

    // --- existence ---
    if (!std::filesystem::exists(cfg.file_a)) {
        std::cerr << "Error: File not found: " << cfg.file_a << "\n";
        return 2;
    }
    if (!std::filesystem::exists(cfg.file_b)) {
        std::cerr << "Error: File not found: " << cfg.file_b << "\n";
        return 2;
    }

    // --- open files ---
    // NOTE: ReaderDirectAccess scans the footer on open() to populate rowCount().
    // This is O(1) for packet-mode files with a valid footer, but for stream-mode
    // files it requires a full read.  The subsequent cell loop is always
    // sequential (readNext).  TODO: for --rows 0:99 on large files, use the
    // footer to check row count and skip early if counts differ, or use
    // random-access read(index) for tight ranges to avoid streaming the full file.
    bcsv::ReaderDirectAccess<bcsv::Layout> reader_a, reader_b;
    if (!reader_a.open(cfg.file_a)) {
        std::cerr << "Error: Cannot open file A: " << cfg.file_a
                  << "\n  " << reader_a.getErrorMsg() << "\n";
        return 2;
    }
    if (!reader_b.open(cfg.file_b)) {
        std::cerr << "Error: Cannot open file B: " << cfg.file_b
                  << "\n  " << reader_b.getErrorMsg() << "\n";
        reader_a.close();
        return 2;
    }

    const auto& layout_a     = reader_a.layout();
    const auto& layout_b     = reader_b.layout();
    size_t      ncols        = layout_a.columnCount();
    size_t      total_rows_a = reader_a.rowCount();
    size_t      total_rows_b = reader_b.rowCount();

    bcsv_compare::FileComparisonResult result;
    result.identical = true;
    result.tolerance = cfg.tolerance;
    result.mode      = mode;
    result.rows_a    = total_rows_a;
    result.rows_b    = total_rows_b;
    result.cols_a    = layout_a.columnCount();
    result.cols_b    = layout_b.columnCount();

    // --- layout check (constant — only names/types, not values) ---
    auto lchk = bcsv_compare::compareLayouts(layout_a, layout_b, mode);
    result.mismatches.insert(result.mismatches.end(),
                             lchk.structural.begin(), lchk.structural.end());
    if (!lchk.ok) {
        result.identical = false;
        emitResult(result, cfg.verbose);
        reader_a.close();
        reader_b.close();
        return 1;
    }

    // --- row count ---
    if (total_rows_a != total_rows_b) {
        result.identical = false;
        bcsv_compare::Mismatch m;
        m.kind       = bcsv_compare::MismatchKind::ROW_COUNT_MISMATCH;
        m.row        = ~size_t{0};
        m.col        = ~size_t{0};
        m.file_a_val = std::to_string(total_rows_a);
        m.file_b_val = std::to_string(total_rows_b);
        result.mismatches.push_back(std::move(m));
        emitResult(result, cfg.verbose);
        reader_a.close();
        reader_b.close();
        return 1;
    }

    // --- ranges ---
    auto row_spec = parseRanges(cfg.rows_str, total_rows_a);
    auto col_spec = parseRanges(cfg.cols_str, ncols);

    // Extract column indices upfront so the inner loop iterates only
    // the columns that matter (avoids per-row contains() scan).
    std::vector<size_t> cols_to_check;
    if (col_spec.ranges.empty()) {
        cols_to_check.reserve(ncols);
        for (size_t c = 0; c < ncols; ++c)
            cols_to_check.push_back(c);
    } else {
        cols_to_check.reserve(ncols);
        for (const auto& [lo, hi] : col_spec.ranges)
            for (size_t c = lo; c <= hi; ++c)
                cols_to_check.push_back(c);
    }

    // --- cell loop ---
    size_t max_val_mismatches   = cfg.verbose ? 100u : 10u;
    size_t row_idx              = 0;
    size_t value_mismatch_count = 0;

    while (reader_a.readNext() && reader_b.readNext()) {
        if (row_spec.contains(row_idx)) {
            for (size_t c : cols_to_check) {
                bool ok = is_value_mode
                              ? bcsv_compare::compareCellValue(reader_a.row(), reader_b.row(), c, c,
                                                               layout_a, layout_b, cfg.tolerance)
                              : bcsv_compare::compareCellStrict(reader_a.row(), reader_b.row(), c,
                                                                layout_a, cfg.tolerance);
                if (!ok) {
                    ++value_mismatch_count;
                    result.identical              = false;
                    result.total_value_mismatches = value_mismatch_count;
                    if (value_mismatch_count <= max_val_mismatches) {
                        bcsv_compare::Mismatch m;
                        if (is_value_mode)
                            bcsv_compare::recordValueMismatch(
                                row_idx, c, reader_a.row(), reader_b.row(),
                                layout_a, layout_b, m);
                        else
                            bcsv_compare::recordStrictValueMismatch(
                                row_idx, c, reader_a.row(), reader_b.row(),
                                layout_a, m);
                        result.mismatches.push_back(std::move(m));
                    }
                }
            }
        }
        ++row_idx;
    }

    reader_a.close();
    reader_b.close();

    emitResult(result, cfg.verbose);
    return result.identical ? 0 : 1;
}

// ================================================================
// Entry
// ================================================================

int main(int argc, char* argv[]) {
    try {
        Config cfg = parseArgs(argc, argv);
        if (cfg.help) {
            printUsage(argv[0]);
            return 0;
        }
        return compareFiles(cfg);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
