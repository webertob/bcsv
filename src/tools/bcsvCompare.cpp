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
 * Compares two BCSV files with combining check modes:
 *   names  - column names must match (header-only)
 *   types  - column types must match (header-only)
 *   values - cell values must match (with cross-type coercion)
 *
 * Modes are combined with commas: --mode "names,types,values" or alias "--mode all".
 *
 * Exit codes: 0 = identical, 1 = different, 2 = error
 */

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <bcsv/bcsv.h>
#include "cli_common.h"
#include "../shared/comparison.h"
#include <sstream>

// ================================================================
// Range specification — shared helper in cli_common.h
// ================================================================

using RangeSpec = bcsv_cli::IndexRangeSet;

/** Parse "0:99,200:-10" → sorted, merged, inclusive ranges.
 * Thin wrapper over bcsv_cli::parseIndexRanges (shared with other tools). */
static RangeSpec parseRanges(const std::string& spec, size_t total) {
    return bcsv_cli::parseIndexRanges(spec, total);
}

// ================================================================
// Config & CheckFlags
// ================================================================

struct CheckFlags {
    bool checkNames     = true;
    bool checkTypes     = true;
    bool checkValues    = true;
    bool stringToValue  = false;
    bool allowImprecise = false;

    bool getCoerce() const noexcept { return checkValues && !checkTypes; }
};

struct Config {
    std::string file_a, file_b;
    CheckFlags  flags;
    double      tolerance = 0.0;
    std::string rows_str, cols_str;
    bool        verbose = false, help = false;
};

struct CheckMode {
    bool names = true, types = true, values = true;
};

static CheckMode parseMode(const std::string& spec) {
    CheckMode c;

    if (spec.empty())
        return c;

    if (spec == "all")
        return {true, true, true};
    if (spec == "strict")
        return {true, true, true};
    if (spec == "compatible")
        return {false, true, true};
    if (spec == "value")
        return {false, false, true};

    CheckMode          zeroed{false, false, false};
    std::istringstream iss(spec);
    std::string        token;
    bool               any = false;
    while (std::getline(iss, token, ',')) {
        if (token == "names") {
            zeroed.names = true;
            any          = true;
        } else if (token == "types") {
            zeroed.types = true;
            any          = true;
        } else if (token == "values") {
            zeroed.values = true;
            any           = true;
        } else {
            throw std::runtime_error(
                "Unknown mode '" + token +
                "'. Allowed: names, types, values, all, strict, compatible, value");
        }
    }
    if (!any)
        throw std::runtime_error("Empty mode specification");
    return zeroed;
}

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [OPTIONS] FILE_A FILE_B\n\n"
        << "Compare two BCSV files and report whether they are identical.\n\n"
        << "Mode selection (comma-separated, no spaces):\n"
        << "  names          check column names only (header-only, fast)\n"
        << "  types          check column types only (header-only, fast)\n"
        << "  values         check cell values (cross-type coercion)\n"
        << "  all            check names + types + values (default)\n"
        << "  strict         alias: same as 'all'\n"
        << "  compatible     alias: same as 'types,values'\n"
        << "  value          alias: same as 'values'\n\n"
        << "Options:\n"
        << "  --mode MODE            comma-separated check modes (default: all)\n"
        << "  --string-to-value      in values mode: parse STRING cells as numbers\n"
        << "                          for comparison with numeric counterparts\n"
        << "  --allow-imprecise      allow integer-vs-float comparison when the integer\n"
        << "                          value exceeds the floating-point mantissa range:\n"
        << "                            INT64/UINT64 vs float/double  (> +/-2^53)\n"
        << "                          Default: refuse such comparisons, report as mismatch\n"
        << "  --tolerance TOL        absolute epsilon for float/double (default: 0.0)\n"
        << "  --rows RANGES          row indices to compare, e.g. 0:99,-10:\n"
        << "  --cols RANGES          column indices to compare\n"
        << "  -v, --verbose          report mismatch details to stdout\n"
        << "  -h, --help             show this help\n\n"
        << "Value coercion rules (when types may differ):\n"
        << "  int vs int              std::cmp_equal — lossless, handles signedness\n"
        << "  int16 or smaller vs float  compare in float — always exact\n"
        << "  int32/uint32 vs float/double promote both to double — always exact\n"
        << "  int64/uint64 vs float/double promote to double — exact only within +/-2^53\n"
        << "    (beyond that: mismatch unless --allow-imprecise is set)\n"
        << "  float vs double         widen to double — always exact\n"
        << "  string vs string        exact match\n"
        << "  string vs numeric       mismatch (unless --string-to-value)\n\n"
        << "Exit codes: 0 = identical, 1 = different, 2 = error\n";
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
        } else if (a == "--string-to-value") {
            cfg.flags.stringToValue = true;
        } else if (a == "--allow-imprecise") {
            cfg.flags.allowImprecise = true;
        } else if ((a == "--mode") && (i + 1 < argc)) {
            auto m                = parseMode(argv[++i]);
            cfg.flags.checkNames  = m.names;
            cfg.flags.checkTypes  = m.types;
            cfg.flags.checkValues = m.values;
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
// Emit result
// ================================================================

static void emitResult(const bcsv_compare::FileComparisonResult& result, bool verbose) {
    if (verbose)
        std::cout << bcsv_compare::formatSummary(result);
}

// ================================================================
// Comparison
// ================================================================

static int compareFiles(const Config& cfg) {
    const auto& f      = cfg.flags;
    bool        coerce = f.getCoerce();

    if (!std::filesystem::exists(cfg.file_a)) {
        std::cerr << "Error: File not found: " << cfg.file_a << "\n";
        return 2;
    }
    if (!std::filesystem::exists(cfg.file_b)) {
        std::cerr << "Error: File not found: " << cfg.file_b << "\n";
        return 2;
    }

    bcsv::ReaderDirectAccess<bcsv::Layout> readerA, readerB;
    if (!readerA.open(cfg.file_a)) {
        std::cerr << "Error: Cannot open: " << cfg.file_a
                  << "\n  " << readerA.getErrorMsg() << "\n";
        return 2;
    }
    if (!readerB.open(cfg.file_b)) {
        std::cerr << "Error: Cannot open: " << cfg.file_b
                  << "\n  " << readerB.getErrorMsg() << "\n";
        readerA.close();
        return 2;
    }

    const auto& layoutA = readerA.layout();
    const auto& layoutB = readerB.layout();
    size_t      ncols   = layoutA.columnCount();

    bcsv_compare::FileComparisonResult result;
    result.identical = true;
    result.tolerance = cfg.tolerance;
    result.mode      = coerce
                           ? bcsv_compare::CompareMode::VALUE
                           : (f.checkNames ? bcsv_compare::CompareMode::STRICT
                                           : bcsv_compare::CompareMode::COMPATIBLE);
    result.rows_a    = readerA.rowCount();
    result.rows_b    = readerB.rowCount();
    result.cols_a    = layoutA.columnCount();
    result.cols_b    = layoutB.columnCount();

    {
        auto lchk = bcsv_compare::compareLayouts(layoutA, layoutB,
                                                 f.checkNames, f.checkTypes);
        result.mismatches.insert(result.mismatches.end(),
                                 lchk.structural.begin(), lchk.structural.end());
        if (!lchk.ok) {
            result.identical = false;
            emitResult(result, cfg.verbose);
            readerA.close();
            readerB.close();
            return 1;
        }
    }

    if (f.checkValues && readerA.rowCount() != readerB.rowCount()) {
        result.identical = false;
        bcsv_compare::Mismatch m;
        m.kind       = bcsv_compare::MismatchKind::ROW_COUNT_MISMATCH;
        m.row        = ~size_t{0};
        m.col        = ~size_t{0};
        m.file_a_val = std::to_string(readerA.rowCount());
        m.file_b_val = std::to_string(readerB.rowCount());
        result.mismatches.push_back(std::move(m));
        emitResult(result, cfg.verbose);
        readerA.close();
        readerB.close();
        return 1;
    }

    if (!f.checkValues) {
        emitResult(result, cfg.verbose);
        readerA.close();
        readerB.close();
        return result.identical ? 0 : 1;
    }

    auto rowSpec     = parseRanges(cfg.rows_str, readerA.rowCount());
    auto colSpec     = parseRanges(cfg.cols_str, ncols);
    auto colsToCheck = colSpec.toIndices(ncols);

    struct ColStrategy {
        size_t                      col;
        bcsv::ColumnType            ta, tb;
        bcsv_compare::CompareTarget target;
    };
    std::vector<ColStrategy> strategies;
    strategies.reserve(colsToCheck.size());

    for (size_t c : colsToCheck) {
        ColStrategy s{c, layoutA.columnType(c), layoutB.columnType(c),
                      bcsv_compare::CompareTarget::MISMATCH};
        s.target = bcsv_compare::promoteType(s.ta, s.tb);
        if (s.target == bcsv_compare::CompareTarget::STR_VS_NUM)
            s.target = f.stringToValue
                           ? bcsv_compare::CompareTarget::STR_VS_NUM
                           : bcsv_compare::CompareTarget::MISMATCH;
        strategies.push_back(std::move(s));
    }

    size_t rowIdx = 0, valMismatchCount = 0;
    size_t maxMismatches = cfg.verbose ? 100 : 10;

    while (readerA.readNext() && readerB.readNext()) {
        if (rowSpec.contains(rowIdx)) {
            for (auto& s : strategies) {
                bool ok = bcsv_compare::compareCellWithStrategy(
                    readerA.row(), s.col,
                    readerB.row(), s.col,
                    s.ta, s.tb, s.target,
                    cfg.tolerance,
                    f.allowImprecise,
                    f.stringToValue);
                if (!ok) {
                    ++valMismatchCount;
                    result.identical              = false;
                    result.total_value_mismatches = valMismatchCount;
                    if (valMismatchCount <= maxMismatches) {
                        bcsv_compare::Mismatch mm;
                        if (coerce) {
                            bcsv_compare::recordValueMismatch(
                                rowIdx, s.col, readerA.row(), readerB.row(),
                                layoutA, layoutB, mm);
                        } else {
                            bcsv_compare::recordStrictValueMismatch(
                                rowIdx, s.col, readerA.row(), readerB.row(),
                                layoutA, mm);
                        }
                        result.mismatches.push_back(std::move(mm));
                    }
                }
            }
        }
        ++rowIdx;
    }

    readerA.close();
    readerB.close();
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
