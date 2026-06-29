/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <bcsv/bcsv.h>
#include "cli_common.h"

namespace fs = std::filesystem;

// ── Arg error type — exit code 2 ────────────────────────────────────

struct ArgError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

// ── CLI Config ──────────────────────────────────────────────────────

struct Config {
    std::string input_file;
    std::string output_file;
    bool        analyze        = true;
    bool        convert        = false;
    bool        force          = false;
    bool        stringsToValue = false;
    bool        verbose        = false;
    bool        help           = false;
};

static void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE\n"
                                              "\n"
                                              "Scan BCSV file for type-narrowing opportunities and optionally convert.\n"
                                              "\n"
                                              "Arguments:\n"
                                              "  INPUT_FILE         Input BCSV file\n"
                                              "\n"
                                              "Output:\n"
                                              "  -o, --output FILE  Write converted file to new location\n"
                                              "  -f, --force        Overwrite INPUT_FILE in place (temp + atomic rename)\n"
                                              "\n"
                                              "Options:\n"
                                              "  --analyze          Scan-only, print findings (default, explicit alias)\n"
                                              "  --convert          Scan + rewrite file with narrower types\n"
                                              "  --stringsToValue   Also attempt string->numeric/bool conversion (opt-in)\n"
                                              "  -v, --verbose      Per-column details and progress to stderr\n"
                                              "  -h, --help         Show help message\n"
                                              "\n"
                                              "Exit codes:\n"
                                              "  0  Success\n"
                                              "  1  Error (invalid file, conversion failed)\n"
                                              "  2  Argument error\n";
}

static Config parseArgs(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            config.help = true;
            return config;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--analyze") {
            config.analyze = true;
        } else if (arg == "--convert") {
            config.convert = true;
            config.analyze = true;
        } else if (arg == "--stringsToValue") {
            config.stringsToValue = true;
        } else if (arg == "-f" || arg == "--force") {
            config.force = true;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg.starts_with("-")) {
            throw ArgError("Unknown option: " + arg);
        } else {
            if (config.input_file.empty()) {
                config.input_file = arg;
            } else {
                throw ArgError("Too many arguments. Only one input file expected.");
            }
        }
    }

    if (config.input_file.empty() && !config.help) {
        throw ArgError("Input file is required");
    }

    return config;
}

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

    double  min_f             = std::numeric_limits<double>::max();
    double  max_f             = std::numeric_limits<double>::lowest();
    bool    f_float_roundtrip = true;
    bool    f_bool_alive      = true;
    bool    f_ladder_alive    = true;
    int64_t int_min           = std::numeric_limits<int64_t>::max();
    int64_t int_max           = std::numeric_limits<int64_t>::lowest();
    bool    int_all_positive  = true;

    bool str_all_numeric = true;
    bool str_done        = false;

    void init(bcsv::ColumnType type, size_t idx, bool probeStrings) {
        col_index     = idx;
        original_type = type;
        optimal_type  = type;

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
        optimal_type = deriveIntSigned(min_s, max_s);
    }

    void visitIntegerUnsigned(uint64_t v) {
        if (min_u > v)
            min_u = v;
        if (max_u < v)
            max_u = v;
        optimal_type = deriveIntUnsigned(min_u, max_u);
    }

    void accumulateFinite(double v) {
        if (v < min_f)
            min_f = v;
        if (v > max_f)
            max_f = v;

        // Sign-normalize: -0.0 -> 0.0, otherwise preserve value (negatives stay negative)
        double norm_v = (v == -0.0) ? 0.0 : v;

        if (v == 0.0 || v == -0.0) {
            // zero is whole
        } else {
            double fv       = std::floor(norm_v);
            bool   is_whole = (fv == norm_v) && !std::isinf(fv);
            if (!is_whole) {
                f_ladder_alive = false;
            }

            if (v < 0.0 && v != -0.0)
                int_all_positive = false;

            if (f_ladder_alive) {
                // Range guard: avoid UB in static_cast<int64_t> for out-of-range doubles
                double abs_v = std::abs(norm_v);
                if (abs_v > static_cast<double>(INT64_MAX)) {
                    f_ladder_alive = false;
                } else {
                    int64_t iv = static_cast<int64_t>(std::round(norm_v));
                    if (iv < int_min)
                        int_min = iv;
                    if (iv > int_max)
                        int_max = iv;
                    // Round-trip check: does int64 -> double reproduce the original?
                    if (static_cast<double>(iv) != v) {
                        f_ladder_alive = false;
                    }
                }
            }
        }

        if (!(v == 0.0 || v == 1.0 || v == -0.0))
            f_bool_alive = false;
    }

    void visitFloat(double v) {
        if (std::isfinite(v)) {
            accumulateFinite(v);

            if (original_type == bcsv::ColumnType::DOUBLE) {
                float  as_float  = static_cast<float>(v);
                double back      = static_cast<double>(as_float);
                bool   same_bits = (std::memcmp(&v, &back, sizeof(double)) == 0);
                if (!same_bits)
                    f_float_roundtrip = false;
            }
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
            str_all_numeric = false;
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        if (std::isspace(static_cast<unsigned char>(s.front())) ||
            std::isspace(static_cast<unsigned char>(s.back()))) {
            str_all_numeric = false;
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        char*  endptr = nullptr;
        double d      = std::strtod(s.c_str(), &endptr);

        if (endptr != s.c_str() + s.size()) {
            str_all_numeric = false;
            str_done        = true;
            optimal_type    = original_type;
            return;
        }

        if (!std::isfinite(d)) {
            str_all_numeric = false;
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

// ── Coerce functions ────────────────────────────────────────────────

// Integer source → any destination.  int→int cast avoids double (no precision loss).
template<typename Src>
static inline bcsv::ValueType coerceInt(bcsv::ColumnType dst, Src src,
                                        size_t col_idx, size_t row_num) {
    // --- Direct int → int cast (avoids double intermediate) ---
    if constexpr (std::is_integral_v<Src> && !std::is_same_v<Src, bool>) {
        switch (dst) {
            case bcsv::ColumnType::BOOL:
                return bcsv::ValueType{src != 0};
            case bcsv::ColumnType::UINT8: {
                uint8_t v = static_cast<uint8_t>(src);
                if (static_cast<Src>(v) != src)
                    throw std::runtime_error("coerceInt UINT8 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::UINT16: {
                uint16_t v = static_cast<uint16_t>(src);
                if (static_cast<Src>(v) != src)
                    throw std::runtime_error("coerceInt UINT16 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::UINT32: {
                uint32_t v = static_cast<uint32_t>(src);
                if (static_cast<Src>(v) != src)
                    throw std::runtime_error("coerceInt UINT32 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::UINT64: {
                uint64_t v = static_cast<uint64_t>(src);
                if (static_cast<Src>(v) != src)
                    throw std::runtime_error("coerceInt UINT64 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::INT8: {
                int8_t v = static_cast<int8_t>(src);
                if (static_cast<int64_t>(v) != static_cast<int64_t>(src))
                    throw std::runtime_error("coerceInt INT8 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::INT16: {
                int16_t v = static_cast<int16_t>(src);
                if (static_cast<int64_t>(v) != static_cast<int64_t>(src))
                    throw std::runtime_error("coerceInt INT16 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::INT32: {
                int32_t v = static_cast<int32_t>(src);
                if (static_cast<int64_t>(v) != static_cast<int64_t>(src))
                    throw std::runtime_error("coerceInt INT32 overflow");
                return bcsv::ValueType{v};
            }
            case bcsv::ColumnType::INT64: {
                int64_t v = static_cast<int64_t>(src);
                if (static_cast<uint64_t>(v) != static_cast<uint64_t>(src))
                    throw std::runtime_error("coerceInt INT64 overflow");
                return bcsv::ValueType{v};
            }
            default: break; // fall through to float dst
        }
    }
    (void)col_idx;
    (void)row_num;

    // --- Float destination ---
    if constexpr (std::is_integral_v<Src>) {
        switch (dst) {
            case bcsv::ColumnType::FLOAT:
                return bcsv::ValueType{static_cast<float>(static_cast<double>(src))};
            case bcsv::ColumnType::DOUBLE:
                return bcsv::ValueType{static_cast<double>(src)};
            default:
                break;
        }
    }

    throw std::runtime_error("bcsvNarrowType: unsupported coercion");
}

// Float/double/string source → any destination (uses double intermediate).
template<typename Src>
static inline bcsv::ValueType coerceValue(bcsv::ColumnType dst, Src src,
                                          size_t col_idx, size_t row_num) {
    double intermediate = static_cast<double>(src);

    // Non-finite values pass through to FLOAT/DOUBLE; integer dtypes
    // should have been blocked by the scan phase.
    if (!std::isfinite(intermediate)) {
        switch (dst) {
            case bcsv::ColumnType::FLOAT:
                return bcsv::ValueType{static_cast<float>(intermediate)};
            case bcsv::ColumnType::DOUBLE:
                return bcsv::ValueType{intermediate};
            default: break;
        }
    }

    // --- Float dst ---
    switch (dst) {
        case bcsv::ColumnType::FLOAT:
            return bcsv::ValueType{static_cast<float>(intermediate)};
        case bcsv::ColumnType::DOUBLE:
            return bcsv::ValueType{intermediate};
        default: break;
    }

    // --- Bool dst ---
    if (dst == bcsv::ColumnType::BOOL)
        return bcsv::ValueType{intermediate != 0.0};

    // --- Integer dst: range-guard before cast to avoid UB ---
    switch (dst) {
        case bcsv::ColumnType::UINT8: {
            uint8_t v = static_cast<uint8_t>(std::round(intermediate));
            return bcsv::ValueType{v};
        }
        case bcsv::ColumnType::UINT16: {
            uint16_t v = static_cast<uint16_t>(std::round(intermediate));
            return bcsv::ValueType{v};
        }
        case bcsv::ColumnType::UINT32: {
            uint32_t v = static_cast<uint32_t>(std::round(intermediate));
            return bcsv::ValueType{v};
        }
        case bcsv::ColumnType::UINT64:
            if (intermediate > static_cast<double>(UINT64_MAX))
                throw std::runtime_error(
                    "coerceValue: " + std::to_string(col_idx) + " row " +
                    std::to_string(row_num) + " exceeds UINT64_MAX");
            return bcsv::ValueType{static_cast<uint64_t>(std::round(intermediate))};
        case bcsv::ColumnType::INT8: {
            int8_t v = static_cast<int8_t>(std::round(intermediate));
            return bcsv::ValueType{v};
        }
        case bcsv::ColumnType::INT16: {
            int16_t v = static_cast<int16_t>(std::round(intermediate));
            return bcsv::ValueType{v};
        }
        case bcsv::ColumnType::INT32: {
            int32_t v = static_cast<int32_t>(std::round(intermediate));
            return bcsv::ValueType{v};
        }
        case bcsv::ColumnType::INT64:
            if (std::fabs(intermediate) > static_cast<double>(INT64_MAX))
                throw std::runtime_error(
                    "coerceValue: " + std::to_string(col_idx) + " row " +
                    std::to_string(row_num) + " exceeds INT64 range");
            return bcsv::ValueType{static_cast<int64_t>(std::round(intermediate))};
        default:
            throw std::runtime_error("coerceValue: unsupported destination type");
    }
}

static std::string wideTypeName(bcsv::ColumnType t) {
    return bcsv_cli::columnTypeStr(t);
}

static bool anyNarrowed(const std::vector<ColumnProbeState>& probes) {
    for (const auto& p : probes) {
        if (p.optimal_type != p.original_type)
            return true;
    }
    return false;
}

// ── Scan phase ──────────────────────────────────────────────────────

static std::vector<ColumnProbeState> scanFile(
    const std::string& input_path,
    bool               stringsToValue,
    bool               verbose,
    size_t&            total_rows_out,
    bcsv::FileFlags&   flags_out,
    uint8_t&           comp_out) {

    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(input_path)) {
        throw std::runtime_error("Cannot open BCSV file: " + input_path +
                                 " (" + reader.getErrorMsg() + ")");
    }

    const auto& layout   = reader.layout();
    size_t      num_cols = layout.columnCount();

    std::vector<ColumnProbeState> probes(num_cols);
    size_t                        alive_count = 0;

    for (size_t i = 0; i < num_cols; ++i) {
        probes[i].init(layout.columnType(i), i, stringsToValue);
        if (probes[i].alive)
            ++alive_count;
    }

    if (verbose) {
        std::cerr << "Scanning file: " << input_path << " ("
                  << num_cols << " columns, " << alive_count << " to probe)" << std::endl;
    }

    size_t total_rows = 0;

    while (reader.readNext() && alive_count > 0) {
        ++total_rows;

        for (size_t i = 0; i < num_cols; ++i) {
            auto& p = probes[i];
            if (!p.alive)
                continue;

            switch (p.original_type) {
                case bcsv::ColumnType::INT8:
                    reader.row().visitConst<int8_t>(i, [&](size_t, const int8_t& v) { p.visitIntegerSigned(static_cast<int64_t>(v)); }, 1);
                    break;
                case bcsv::ColumnType::INT16:
                    reader.row().visitConst<int16_t>(i, [&](size_t, const int16_t& v) { p.visitIntegerSigned(static_cast<int64_t>(v)); }, 1);
                    break;
                case bcsv::ColumnType::INT32:
                    reader.row().visitConst<int32_t>(i, [&](size_t, const int32_t& v) { p.visitIntegerSigned(static_cast<int64_t>(v)); }, 1);
                    break;
                case bcsv::ColumnType::INT64:
                    reader.row().visitConst<int64_t>(i, [&](size_t, const int64_t& v) { p.visitIntegerSigned(v); }, 1);
                    break;
                case bcsv::ColumnType::UINT8:
                    reader.row().visitConst<uint8_t>(i, [&](size_t, const uint8_t& v) { p.visitIntegerUnsigned(static_cast<uint64_t>(v)); }, 1);
                    break;
                case bcsv::ColumnType::UINT16:
                    reader.row().visitConst<uint16_t>(i, [&](size_t, const uint16_t& v) { p.visitIntegerUnsigned(static_cast<uint64_t>(v)); }, 1);
                    break;
                case bcsv::ColumnType::UINT32:
                    reader.row().visitConst<uint32_t>(i, [&](size_t, const uint32_t& v) { p.visitIntegerUnsigned(static_cast<uint64_t>(v)); }, 1);
                    break;
                case bcsv::ColumnType::UINT64:
                    reader.row().visitConst<uint64_t>(i, [&](size_t, const uint64_t& v) { p.visitIntegerUnsigned(v); }, 1);
                    break;
                case bcsv::ColumnType::FLOAT:
                    reader.row().visitConst<float>(i, [&](size_t, const float& v) { p.visitFloat(static_cast<double>(v)); }, 1);
                    break;
                case bcsv::ColumnType::DOUBLE:
                    reader.row().visitConst<double>(i, [&](size_t, const double& v) { p.visitFloat(v); }, 1);
                    break;
                case bcsv::ColumnType::STRING:
                    reader.row().visitConst<std::string>(i, [&](size_t, const std::string& s) { p.visitString(s); }, 1);
                    break;
                default:
                    p.alive = false;
                    --alive_count;
                    break;
            }
        }

        for (size_t i = 0; i < num_cols; ++i) {
            auto& p = probes[i];
            if (!p.alive)
                continue;
            p.rows_probed++;
            p.checkStabilization();
            if (!p.alive)
                --alive_count;
        }

        if (alive_count == 0)
            break;

        if (verbose && (total_rows & 0x3FFF) == 0) {
            std::cerr << "  Scanned " << total_rows << " rows, "
                      << alive_count << " columns still probing..." << std::endl;
        }
    }
    // Capture file metadata while reader is still open
    flags_out = reader.fileHeader().getFlags();
    comp_out  = reader.fileHeader().getCompressionLevel();
    reader.close();

    // Empty-column guard
    for (auto& p : probes) {
        if (p.rows_probed == 0) {
            p.optimal_type = p.original_type;
            p.alive        = false;
        }
    }

    total_rows_out = total_rows;
    return probes;
}

// ── Analyze output ──────────────────────────────────────────────────

static bool printAnalysis(
    const std::string&                   input_path,
    const std::vector<ColumnProbeState>& probes,
    size_t                               rows_scanned,
    bcsv::FileFlags                      flags,
    uint8_t                              comp,
    bool /*verbose*/) {

    // Need column names for the table — open reader just for layout
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(input_path)) {
        throw std::runtime_error("Cannot reopen for analysis: " + input_path);
    }
    const auto& layout   = reader.layout();
    size_t      num_cols = layout.columnCount();
    reader.close();

    auto names = bcsv_cli::codecNamesFromFlags(flags, comp);

    bool has_narrowed = anyNarrowed(probes);

    std::cout << "bcsvNarrowType: analysis of " << input_path << "\n";
    std::cout << "File codec: " << names.file_codec << " | Row codec: " << names.row_codec
              << " | Compression: " << static_cast<int>(comp) << "\n";

    if (!has_narrowed) {
        // Try to get total row count for early-termination display only when needed
        size_t total_possible = 0;
        try {
            bcsv::ReaderDirectAccess<bcsv::Layout> da;
            if (da.open(input_path)) {
                total_possible = da.rowCount();
            }
        } catch (...) {}

        // Check for early termination: all columns stabilized before EOF, nothing narrowable
        bool early_terminated = total_possible > 0 && rows_scanned < total_possible;

        if (early_terminated) {
            std::cout << "Rows scanned: " << rows_scanned << " (out of " << total_possible
                      << ", terminated early)\n";
            std::cout << "Columns: " << num_cols << "\n";
            std::cout << "\nAll " << num_cols << " columns are already at their narrowest type.\n";
            std::cout << "No conversion possible.\n";
            return false;
        }

        std::cout << "Rows scanned: " << rows_scanned << ", Columns: " << num_cols << "\n";
        std::cout << "\nAll " << num_cols << " columns are already at their narrowest type.\n";
        std::cout << "No conversion possible.\n";
        return false;
    }

    std::cout << "Rows scanned: " << rows_scanned << ", Columns: " << num_cols << "\n";

    // Table widths
    size_t max_idx = 3;
    if (num_cols > 99) {
        max_idx = std::to_string(num_cols - 1).size();
        if (max_idx < 3)
            max_idx = 3;
    }

    size_t max_name = 4;
    for (size_t i = 0; i < num_cols; ++i) {
        if (layout.columnName(i).size() > max_name)
            max_name = layout.columnName(i).size();
    }

    size_t opt_width = 0;
    for (size_t i = 0; i < num_cols; ++i) {
        std::string s = bcsv_cli::columnTypeStr(probes[i].optimal_type);
        if (probes[i].optimal_type == probes[i].original_type)
            s += "       (unchanged)";
        if (s.size() > opt_width)
            opt_width = s.size();
    }

    std::cout << "\n"
              << "  " << std::right << std::setw(static_cast<int>(max_idx)) << "Idx"
              << "  " << std::left << std::setw(static_cast<int>(max_name)) << "Name"
              << "  " << std::right << std::setw(8) << "Original"
              << "  " << std::left << std::setw(static_cast<int>(opt_width)) << "Optimal"
              << "\n"
              << "  " << std::string(max_idx, '-')
              << "  " << std::string(max_name, '-')
              << "  " << std::string(8, '-')
              << "  " << std::string(opt_width, '-')
              << "\n";

    size_t narrowable = 0;
    for (size_t i = 0; i < num_cols; ++i) {
        auto& p = probes[i];
        std::cout << "  " << std::right << std::setw(static_cast<int>(max_idx)) << i
                  << "  " << std::left << std::setw(static_cast<int>(max_name))
                  << layout.columnName(i)
                  << "  " << std::right << std::setw(8)
                  << wideTypeName(p.original_type)
                  << "  ";

        if (p.optimal_type == p.original_type) {
            std::cout << std::left << std::setw(static_cast<int>(opt_width))
                      << wideTypeName(p.optimal_type) << "       (unchanged)";
        } else {
            std::cout << std::left << std::setw(static_cast<int>(opt_width))
                      << wideTypeName(p.optimal_type);
            ++narrowable;
        }
        std::cout << "\n";
    }

    size_t orig_bytes = 0, opt_bytes = 0;
    for (const auto& p : probes) {
        orig_bytes += bcsv::sizeOf(p.original_type);
        opt_bytes += bcsv::sizeOf(p.optimal_type);
    }
    double savings_pct = (orig_bytes > 0)
                             ? 100.0 * (1.0 - static_cast<double>(opt_bytes) / static_cast<double>(orig_bytes))
                             : 0.0;

    std::cout << "\nNarrowable columns: " << narrowable << " / " << num_cols << "\n";
    std::cout << "Max theoretical savings (flat codec): " << std::fixed
              << std::setprecision(1) << savings_pct
              << "% (estimated type widths only)\n";
    std::cout << "\nUse --convert -o output.bcsv to apply, or --convert -f to overwrite.\n";

    return true; // has narrowed columns
}

// ── Convert phase ───────────────────────────────────────────────────

static void convertFile(
    const std::string&                   input_path,
    const std::string&                   output_path,
    const std::vector<ColumnProbeState>& probes,
    bcsv::FileFlags                      flags,
    uint8_t                              comp_level,
    bool                                 verbose) {

    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(input_path)) {
        throw std::runtime_error("Cannot open for conversion: " + input_path +
                                 " (" + reader.getErrorMsg() + ")");
    }

    const auto& src_layout = reader.layout();
    size_t      num_cols   = src_layout.columnCount();

    bcsv::Layout dst_layout;
    for (size_t i = 0; i < num_cols; ++i) {
        dst_layout.addColumn({src_layout.columnName(i), probes[i].optimal_type});
    }

    auto names = bcsv_cli::codecNamesFromFlags(flags, comp_level);

    if (verbose) {
        std::cerr << "Converting to: " << output_path << std::endl;
        std::cerr << "Row codec: " << names.row_codec << std::endl;
    }

    size_t total_rows = 0;
    bcsv_cli::withWriter(dst_layout, names.row_codec, [&](auto& writer) {
        writer.open(output_path, true, comp_level, bcsv::DEFAULT_PACKET_SIZE_KB, flags);

        while (reader.readNext()) {
            ++total_rows;
            auto& dst_row = writer.row();

            for (size_t i = 0; i < num_cols; ++i) {
                auto&            p        = probes[i];
                bcsv::ColumnType dst_type = p.optimal_type;
                bcsv::ColumnType src_type = p.original_type;

                if (src_type == dst_type) {
                    reader.row().visitConst(i, [&dst_row](size_t col, const auto& val) { dst_row.set(col, val); }, 1);
                } else {
                    auto set_coerced = [&](auto src_val) {
                        bcsv::ValueType vt;
                        if constexpr (std::is_integral_v<decltype(src_val)>) {
                            vt = coerceInt(dst_type, src_val, i, total_rows);
                        } else {
                            vt = coerceValue(dst_type, src_val, i, total_rows);
                        }
                        std::visit([&i, &dst_row](const auto& v) {
                            dst_row.set(i, v);
                        },
                                   vt);
                    };

                    switch (src_type) {
                        case bcsv::ColumnType::INT8:
                            reader.row().visitConst<int8_t>(i, [&set_coerced](size_t, const int8_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::INT16:
                            reader.row().visitConst<int16_t>(i, [&set_coerced](size_t, const int16_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::INT32:
                            reader.row().visitConst<int32_t>(i, [&set_coerced](size_t, const int32_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::INT64:
                            reader.row().visitConst<int64_t>(i, [&set_coerced](size_t, const int64_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::UINT8:
                            reader.row().visitConst<uint8_t>(i, [&set_coerced](size_t, const uint8_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::UINT16:
                            reader.row().visitConst<uint16_t>(i, [&set_coerced](size_t, const uint16_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::UINT32:
                            reader.row().visitConst<uint32_t>(i, [&set_coerced](size_t, const uint32_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::UINT64:
                            reader.row().visitConst<uint64_t>(i, [&set_coerced](size_t, const uint64_t& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::FLOAT:
                            reader.row().visitConst<float>(i, [&set_coerced](size_t, const float& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::DOUBLE:
                            reader.row().visitConst<double>(i, [&set_coerced](size_t, const double& v) { set_coerced(v); }, 1);
                            break;
                        case bcsv::ColumnType::STRING:
                            reader.row().visitConst<std::string>(i, [&set_coerced, &total_rows](size_t, const std::string& v) {
                                char*  endptr = nullptr;
                                double d      = std::strtod(v.c_str(), &endptr);
                                if (endptr != v.c_str() + v.size()) {
                                    throw std::runtime_error("bcsvNarrowType: invalid string value at row " +
                                                             std::to_string(total_rows) + ": " + v);
                                }
                                set_coerced(d); }, 1);
                            break;
                        default:
                            break;
                    }
                }
            }
            writer.writeRow();

            if (verbose && (total_rows & 0x3FFF) == 0) {
                std::cerr << "  Written " << total_rows << " rows..." << std::endl;
            }
        }

        writer.close();

        if (verbose) {
            std::cerr << "  Written " << total_rows << " rows to " << output_path << std::endl;
        }
    });
}

// ── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    try {
        Config config = parseArgs(argc, argv);

        if (config.help) {
            printUsage(argv[0]);
            return 0;
        }

        if (!fs::exists(config.input_file)) {
            throw std::runtime_error("Input file does not exist: " + config.input_file);
        }

        if (config.convert && !config.force && config.output_file.empty()) {
            throw ArgError("--convert requires -o FILE or -f");
        }

        if (config.convert && config.force && !config.output_file.empty()) {
            std::cerr << "Warning: -f and -o both specified; using -o output path" << std::endl;
        }

        // Scan phase — also captures flags/comp to avoid redundant re-opens
        size_t          total_rows = 0;
        bcsv::FileFlags flags;
        uint8_t         comp;
        auto            probes = scanFile(config.input_file, config.stringsToValue,
                                          config.verbose, total_rows, flags, comp);

        // Analyze output (passes flags/comp from scan, no extra open)
        bool has_narrowed = printAnalysis(config.input_file, probes, total_rows,
                                          flags, comp, config.verbose);

        // Convert phase
        if (config.convert) {
            if (has_narrowed) {
                std::string effective_output;
                fs::path    input_fs(config.input_file);

                if (config.force && config.output_file.empty()) {
                    // In-place: temp file + atomic rename
                    fs::path tmp_path = input_fs.parent_path() /
                                        ("bcsvNarrowType.tmp." + std::to_string(
                                                                     std::hash<std::string>{}(config.input_file)));

                    try {
                        convertFile(config.input_file, tmp_path.string(), probes,
                                    flags, comp, config.verbose);
                        fs::rename(tmp_path, input_fs);
                        if (config.verbose) {
                            std::cerr << "In-place overwrite complete." << std::endl;
                        }
                    } catch (...) {
                        if (fs::exists(tmp_path))
                            fs::remove(tmp_path);
                        throw;
                    }
                } else {
                    effective_output = config.output_file;
                    fs::path out_fs(effective_output);

                    // Warn before overwriting existing output file
                    if (fs::exists(out_fs)) {
                        if (!fs::is_regular_file(out_fs)) {
                            throw std::runtime_error(
                                "Output path exists and is not a regular file: " +
                                effective_output);
                        }
                        std::cerr << "Warning: overwriting existing output file: "
                                  << effective_output << std::endl;
                    }

                    try {
                        convertFile(config.input_file, effective_output, probes,
                                    flags, comp, config.verbose);
                        if (config.verbose) {
                            std::cerr << "Converted file written to " << effective_output << std::endl;
                        }
                    } catch (...) {
                        if (fs::exists(effective_output))
                            fs::remove(effective_output);
                        throw;
                    }
                }
            } else {
                std::cerr << "No columns to convert — file already at narrowest types." << std::endl;
            }
        }

        return 0;
    } catch (const ArgError& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
