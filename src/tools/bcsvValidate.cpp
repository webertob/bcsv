/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bcsvValidate.cpp
 * @brief CLI tool to validate BCSV files — structure and content
 *
 * Three validation modes:
 *
 *   Mode 1 — Structure validation  (bcsvValidate -i FILE)
 *       Inspects header, walks all rows, reports schema & statistics.
 *
 *   Mode 2 — Pattern validation    (bcsvValidate -i FILE -p PROFILE [-d MODE] [-n ROWS])
 *       Regenerates expected data from a known benchmark profile and
 *       compares every cell to what is actually stored in the file.
 *
 *   Mode 3 — File comparison       (bcsvValidate -i FILE_A --compare FILE_B)
 *       Opens two BCSV files and compares them row-by-row, cell-by-cell.
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <bcsv/bcsv.h>
#include "cli_common.h"
#include "test_datasets.hpp"
#include "validation.h"

// ── Configuration ───────────────────────────────────────────────────

struct Config {
    std::string input_file;

    // Pattern validation
    std::string profile;                      // empty → skip pattern mode
    std::string data_mode  = "timeseries";    // timeseries | random
    size_t      rows       = 0;               // 0 → use profile default_rows

    // File comparison
    std::string compare_file;                 // empty → skip comparison mode

    // Output
    bool   json          = false;
    double tolerance      = 0.0;
    size_t max_errors     = 10;

    // General
    bool   verbose       = false;
    bool   deep          = false;             // decompress and parse every row
    bool   list_profiles = false;
    bool   help          = false;
};

// ── Usage ───────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog
        << " [OPTIONS] -i INPUT_FILE\n\n"

        << "Validate BCSV files — structure and content.\n\n"

        << "Modes:\n"
        << "  Structure   " << prog << " -i FILE\n"
        << "  Pattern     " << prog << " -i FILE -p PROFILE [-d MODE] [-n ROWS]\n"
        << "  Comparison  " << prog << " -i FILE --compare FILE_B\n\n"

        << "Arguments:\n"
        << "  -i, --input FILE         Input BCSV file (required)\n\n"

        << "Pattern validation:\n"
        << "  -p, --profile NAME       Benchmark profile name\n"
        << "  -n, --rows N             Expected row count (default: profile default)\n"
        << "  -d, --data-mode MODE     Data mode: timeseries (default) or random\n"
        << "  --list                   List available profiles and exit\n\n"

        << "File comparison:\n"
        << "  --compare FILE_B         Second file to compare (BCSV or CSV)\n\n"

        << "Validation:\n"
        << "  --tolerance TOL          Float/double comparison tolerance (default: 0.0)\n"
        << "  --max-errors N           Max mismatches to report (default: 10)\n"
        << "  --deep                   Parse every row during structure check\n\n"

        << "Output:\n"
        << "  --json                   Machine-readable JSON output to stdout\n"
        << "  -v, --verbose            Verbose progress output\n"
        << "  -h, --help               Show this help message\n\n"

        << "Exit codes:\n"
        << "  0  Validation passed\n"
        << "  1  Validation failed (mismatches found)\n"
        << "  2  Error (file not found, bad arguments, etc.)\n\n"

        << "Examples:\n"
        << "  " << prog << " -i data.bcsv\n"
        << "  " << prog << " -i data.bcsv -p sensor_noisy -n 10000\n"
        << "  " << prog << " -i a.bcsv --compare b.bcsv --tolerance 1e-6\n"
        << "  " << prog << " -i data.bcsv -p mixed_generic --json\n"
        << "  " << prog << " --list\n";
}

// ── Argument parsing ────────────────────────────────────────────────

static Config parseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            cfg.help = true;
            return cfg;
        } else if (arg == "--list") {
            cfg.list_profiles = true;
            return cfg;
        } else if (arg == "-v" || arg == "--verbose") {
            cfg.verbose = true;
        } else if (arg == "--json") {
            cfg.json = true;
        } else if (arg == "--deep") {
            cfg.deep = true;
        } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            cfg.input_file = argv[++i];
        } else if ((arg == "-p" || arg == "--profile") && i + 1 < argc) {
            cfg.profile = argv[++i];
        } else if ((arg == "-d" || arg == "--data-mode") && i + 1 < argc) {
            cfg.data_mode = argv[++i];
            if (cfg.data_mode != "timeseries" && cfg.data_mode != "random") {
                throw std::runtime_error(
                    "Unknown data mode '" + cfg.data_mode +
                    "'. Expected 'timeseries' or 'random'.");
            }
        } else if ((arg == "-n" || arg == "--rows") && i + 1 < argc) {
            int n = std::stoi(argv[++i]);
            if (n <= 0)
                throw std::runtime_error("Row count must be positive.");
            cfg.rows = static_cast<size_t>(n);
        } else if (arg == "--compare" && i + 1 < argc) {
            cfg.compare_file = argv[++i];
        } else if (arg == "--tolerance" && i + 1 < argc) {
            cfg.tolerance = std::stod(argv[++i]);
            if (cfg.tolerance < 0.0)
                throw std::runtime_error("Tolerance must be non-negative.");
        } else if (arg == "--max-errors" && i + 1 < argc) {
            int n = std::stoi(argv[++i]);
            if (n <= 0)
                throw std::runtime_error("Max errors must be positive.");
            cfg.max_errors = static_cast<size_t>(n);
        } else if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + arg);
        } else {
            // Bare positional arg → input file
            if (cfg.input_file.empty())
                cfg.input_file = arg;
            else
                throw std::runtime_error("Too many positional arguments.");
        }
    }

    if (cfg.input_file.empty() && !cfg.help && !cfg.list_profiles) {
        throw std::runtime_error("Input file is required (-i FILE).");
    }

    return cfg;
}

// ── JSON helpers ────────────────────────────────────────────────────

static std::string jsonStr(const std::string& s) {
    std::string out = "\"";
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
    out += '"';
    return out;
}

// ── Formatting helpers ──────────────────────────────────────────────

static std::string formatTimestamp(uint64_t epoch) {
    if (epoch == 0) return "(not set)";
    auto tp = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(epoch));
    auto dp = std::chrono::floor<std::chrono::days>(tp);
    std::chrono::year_month_day ymd{dp};
    std::chrono::hh_mm_ss hms{std::chrono::floor<std::chrono::seconds>(tp - dp)};
    std::ostringstream ss;
    ss << std::setfill('0')
       << static_cast<int>(ymd.year()) << '-'
       << std::setw(2) << static_cast<unsigned>(ymd.month()) << '-'
       << std::setw(2) << static_cast<unsigned>(ymd.day()) << ' '
       << std::setw(2) << hms.hours().count() << ':'
       << std::setw(2) << hms.minutes().count() << ':'
       << std::setw(2) << hms.seconds().count() << " UTC";
    return ss.str();
}

// ── Mode 1: Structure Validation ────────────────────────────────────

static int validateStructure(const Config& cfg) {
    if (!std::filesystem::exists(cfg.input_file)) {
        std::cerr << "Error: File does not exist: " << cfg.input_file << "\n";
        return 2;
    }

    auto file_size = std::filesystem::file_size(cfg.input_file);

    // Try ReaderDirectAccess first for full metadata
    bcsv::ReaderDirectAccess<bcsv::Layout> da_reader;
    bool has_footer = da_reader.open(cfg.input_file);

    // If no footer, fall back to sequential reader
    if (!has_footer) {
        da_reader.close();
    }

    bcsv::Reader<bcsv::Layout> reader;
    if (!has_footer) {
        if (!reader.open(cfg.input_file)) {
            std::cerr << "Error: Cannot open BCSV file: " << cfg.input_file
                      << "\n  " << reader.getErrorMsg() << "\n";
            return 2;
        }
    }

    // Get layout from whichever reader succeeded
    const auto& layout = has_footer ? da_reader.layout() : reader.layout();
    const size_t col_count = layout.columnCount();

    // Get codec info
    bcsv::FileFlags flags = has_footer ? da_reader.fileFlags() : reader.fileFlags();
    uint8_t comp_level = has_footer ? da_reader.compressionLevel() : reader.compressionLevel();
    auto codecs = bcsv_cli::codecNamesFromFlags(flags, comp_level);

    // Get creation time
    uint64_t creation_time = has_footer
        ? da_reader.fileHeader().getCreationTime()
        : reader.fileHeader().getCreationTime();

    // Get format version and packet size
    const auto& hdr = has_footer ? da_reader.fileHeader() : reader.fileHeader();
    std::string format_version = hdr.versionString();
    uint32_t packet_size = hdr.getPacketSize();

    // Get packet statistics (only available when footer is present)
    size_t total_packets = 0;
    double avg_rows_per_packet = 0.0;
    if (has_footer) {
        total_packets = da_reader.fileFooter().packetIndex().size();
    }

    // Count rows (walk all data if no footer)
    size_t row_count = 0;
    bool row_walk_ok = true;

    if (has_footer) {
        row_count = da_reader.rowCount();
        if (cfg.deep) {
            // Verify by reading every row
            for (size_t r = 0; r < row_count; ++r) {
                if (!da_reader.read(r)) {
                    std::cerr << "Error: Failed to read row " << r << "\n";
                    row_walk_ok = false;
                    break;
                }
            }
        }
    } else {
        while (reader.readNext()) {
            ++row_count;
        }
    }

    // Close
    if (has_footer) da_reader.close();
    else reader.close();

    // Compute avg rows per packet (needs row_count)
    if (total_packets > 0 && row_count > 0) {
        avg_rows_per_packet = static_cast<double>(row_count) / static_cast<double>(total_packets);
    }

    // ── Output ──────────────────────────────────────────────────

    if (cfg.json) {
        // JSON to stdout
        std::ostringstream ss;
        ss << "{\n"
           << "  \"file\": " << jsonStr(cfg.input_file) << ",\n"
           << "  \"valid\": " << (row_walk_ok ? "true" : "false") << ",\n"
           << "  \"format_version\": " << jsonStr(format_version) << ",\n"
           << "  \"file_size\": " << file_size << ",\n"
           << "  \"creation_time\": " << creation_time << ",\n"
           << "  \"creation_time_str\": " << jsonStr(formatTimestamp(creation_time)) << ",\n"
           << "  \"row_count\": " << row_count << ",\n"
           << "  \"column_count\": " << col_count << ",\n"
           << "  \"compression_level\": " << static_cast<int>(comp_level) << ",\n"
           << "  \"packet_size\": " << packet_size << ",\n"
           << "  \"total_packets\": " << total_packets << ",\n"
           << "  \"avg_rows_per_packet\": " << std::fixed << std::setprecision(1) << avg_rows_per_packet << ",\n"
           << "  \"row_codec\": " << jsonStr(codecs.row_codec) << ",\n"
           << "  \"file_codec\": " << jsonStr(codecs.file_codec) << ",\n"
           << "  \"has_footer\": " << (has_footer ? "true" : "false") << ",\n"
           << "  \"columns\": [";
        for (size_t c = 0; c < col_count; ++c) {
            if (c > 0) ss << ",";
            ss << "\n    {\"name\": " << jsonStr(layout.columnName(c))
               << ", \"type\": " << jsonStr(bcsv_cli::columnTypeStr(layout.columnType(c))) << "}";
        }
        ss << "\n  ]\n}";
        std::cout << ss.str() << "\n";
    } else {
        // Text to stderr
        std::cerr << "=== bcsvValidate: Structure ===\n"
                  << "File:            " << cfg.input_file << "\n"
                  << "Format version:  " << format_version << "\n"
                  << "File size:       " << bcsv_cli::formatBytes(file_size) << " (" << file_size << " bytes)\n"
                  << "Created:         " << formatTimestamp(creation_time) << "\n"
                  << "Row count:       " << row_count << "\n"
                  << "Column count:    " << col_count << "\n"
                  << "Compression:     level " << static_cast<int>(comp_level) << "\n"
                  << "Packet size:     " << bcsv_cli::formatBytes(packet_size) << "\n"
                  << "Total packets:   " << (has_footer ? std::to_string(total_packets) : "n/a (no footer)") << "\n"
                  << "Avg rows/packet: " << (has_footer ? std::to_string(static_cast<size_t>(avg_rows_per_packet)) : "n/a") << "\n"
                  << "Row codec:       " << codecs.row_codec << "\n"
                  << "File codec:      " << codecs.file_codec << "\n"
                  << "Footer present:  " << (has_footer ? "yes" : "no") << "\n\n";

        bcsv_cli::printLayoutSummary("Layout", layout, std::cerr);

        if (cfg.deep) {
            std::cerr << "\nDeep check:      " << (row_walk_ok ? "PASSED" : "FAILED") << "\n";
        }

        std::cerr << "\nResult: " << (row_walk_ok ? "PASSED" : "FAILED") << "\n";
    }

    return row_walk_ok ? 0 : 1;
}

// ── Mode 2: Pattern Validation ──────────────────────────────────────

static int validatePattern(const Config& cfg) {
    // First validate structure
    if (!std::filesystem::exists(cfg.input_file)) {
        std::cerr << "Error: File does not exist: " << cfg.input_file << "\n";
        return 2;
    }

    // Resolve profile
    bench::DatasetProfile profile;
    try {
        profile = bench::getProfile(cfg.profile);
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << "\n"
                  << "Use --list to see available profiles.\n";
        return 2;
    }

    const size_t expected_rows = cfg.rows > 0 ? cfg.rows : profile.default_rows;
    const bool timeseries = (cfg.data_mode == "timeseries");
    auto& generator = timeseries ? profile.generateTimeSeries : profile.generate;

    // Open file
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(cfg.input_file)) {
        std::cerr << "Error: Cannot open BCSV file: " << cfg.input_file
                  << "\n  " << reader.getErrorMsg() << "\n";
        return 2;
    }

    const auto& file_layout = reader.layout();
    const auto& profile_layout = profile.layout;

    // Verify layout compatibility
    if (file_layout.columnCount() != profile_layout.columnCount()) {
        std::cerr << "Error: Column count mismatch. File has "
                  << file_layout.columnCount() << " columns, profile '"
                  << cfg.profile << "' expects "
                  << profile_layout.columnCount() << ".\n";
        reader.close();
        return 1;
    }

    for (size_t c = 0; c < file_layout.columnCount(); ++c) {
        if (file_layout.columnType(c) != profile_layout.columnType(c)) {
            std::cerr << "Error: Column " << c << " type mismatch. File has "
                      << bcsv_cli::columnTypeStr(file_layout.columnType(c))
                      << ", profile expects "
                      << bcsv_cli::columnTypeStr(profile_layout.columnType(c))
                      << ".\n";
            reader.close();
            return 1;
        }
    }

    // Compare rows
    bcsv_validation::RoundTripValidator validator(cfg.max_errors, cfg.tolerance);

    // Generate expected rows and compare
    bcsv::Row expected_row(profile_layout);
    size_t actual_rows = 0;

    while (reader.readNext()) {
        if (actual_rows < expected_rows) {
            generator(expected_row, actual_rows);
            validator.compareRow(actual_rows, expected_row, reader.row(), file_layout);
        }
        ++actual_rows;

        if (cfg.verbose && ((actual_rows & 0xFFFF) == 0)) {
            std::cerr << "  Checked " << actual_rows << " / "
                      << expected_rows << " rows...\n";
        }
    }

    reader.close();

    bool row_count_ok = (actual_rows == expected_rows);
    bool content_ok = validator.passed();
    bool overall_ok = row_count_ok && content_ok;

    // ── Output ──────────────────────────────────────────────────

    if (cfg.json) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"file\": " << jsonStr(cfg.input_file) << ",\n"
           << "  \"profile\": " << jsonStr(cfg.profile) << ",\n"
           << "  \"data_mode\": " << jsonStr(cfg.data_mode) << ",\n"
           << "  \"expected_rows\": " << expected_rows << ",\n"
           << "  \"actual_rows\": " << actual_rows << ",\n"
           << "  \"row_count_match\": " << (row_count_ok ? "true" : "false") << ",\n"
           << "  \"tolerance\": " << cfg.tolerance << ",\n"
           << "  \"mismatches\": " << validator.errorCount() << ",\n"
           << "  \"valid\": " << (overall_ok ? "true" : "false") << ",\n"
           << "  \"details\": " << validator.toJson() << "\n"
           << "}";
        std::cout << ss.str() << "\n";
    } else {
        std::cerr << "=== bcsvValidate: Pattern ===\n"
                  << "File:     " << cfg.input_file << "\n"
                  << "Profile:  " << cfg.profile << "\n"
                  << "Mode:     " << cfg.data_mode << "\n"
                  << "Expected: " << expected_rows << " rows\n"
                  << "Actual:   " << actual_rows << " rows\n";

        if (cfg.tolerance > 0.0)
            std::cerr << "Tolerance: " << cfg.tolerance << "\n";

        if (!row_count_ok) {
            std::cerr << "\nRow count MISMATCH: expected " << expected_rows
                      << ", got " << actual_rows << "\n";
        }

        std::cerr << "\n" << validator.summary() << "\n";
        std::cerr << "Result: " << (overall_ok ? "PASSED" : "FAILED") << "\n";
    }

    return overall_ok ? 0 : 1;
}

// ── Mode 3: File Comparison ─────────────────────────────────────────

static bool isCsvFile(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".csv" || ext == ".tsv";
}

static int validateComparison(const Config& cfg) {
    // Check both files exist
    if (!std::filesystem::exists(cfg.input_file)) {
        std::cerr << "Error: File does not exist: " << cfg.input_file << "\n";
        return 2;
    }
    if (!std::filesystem::exists(cfg.compare_file)) {
        std::cerr << "Error: File does not exist: " << cfg.compare_file << "\n";
        return 2;
    }

    // Open file A (always BCSV)
    bcsv::Reader<bcsv::Layout> reader_a;
    if (!reader_a.open(cfg.input_file)) {
        std::cerr << "Error: Cannot open file A: " << cfg.input_file
                  << "\n  " << reader_a.getErrorMsg() << "\n";
        return 2;
    }

    const auto& layout_a = reader_a.layout();
    const bool csv_b = isCsvFile(cfg.compare_file);

    // Open file B (BCSV or CSV)
    bcsv::Reader<bcsv::Layout> reader_bcsv_b;
    std::unique_ptr<bcsv::CsvReader<bcsv::Layout>> reader_csv_b;

    const bcsv::Layout* layout_b_ptr = nullptr;

    if (csv_b) {
        reader_csv_b = std::make_unique<bcsv::CsvReader<bcsv::Layout>>(layout_a);
        if (!reader_csv_b->open(cfg.compare_file)) {
            std::cerr << "Error: Cannot open CSV file B: " << cfg.compare_file
                      << "\n  " << reader_csv_b->getErrorMsg() << "\n";
            reader_a.close();
            return 2;
        }
        layout_b_ptr = &reader_csv_b->layout();
    } else {
        if (!reader_bcsv_b.open(cfg.compare_file)) {
            std::cerr << "Error: Cannot open file B: " << cfg.compare_file
                      << "\n  " << reader_bcsv_b.getErrorMsg() << "\n";
            reader_a.close();
            return 2;
        }
        layout_b_ptr = &reader_bcsv_b.layout();
    }

    const auto& layout_b = *layout_b_ptr;

    // Compare layouts
    bool layout_ok = true;
    std::string layout_msg;

    if (layout_a.columnCount() != layout_b.columnCount()) {
        layout_ok = false;
        layout_msg = "Column count mismatch: A has " +
                     std::to_string(layout_a.columnCount()) +
                     ", B has " + std::to_string(layout_b.columnCount());
    } else {
        for (size_t c = 0; c < layout_a.columnCount(); ++c) {
            if (layout_a.columnType(c) != layout_b.columnType(c)) {
                layout_ok = false;
                layout_msg = "Column " + std::to_string(c) + " type mismatch: A=" +
                             bcsv_cli::columnTypeStr(layout_a.columnType(c)) +
                             " B=" + bcsv_cli::columnTypeStr(layout_b.columnType(c));
                break;
            }
        }
    }

    if (!layout_ok) {
        reader_a.close();
        if (csv_b) reader_csv_b->close();
        else reader_bcsv_b.close();

        if (cfg.json) {
            std::cout << "{\n"
                      << "  \"file_a\": " << jsonStr(cfg.input_file) << ",\n"
                      << "  \"file_b\": " << jsonStr(cfg.compare_file) << ",\n"
                      << "  \"valid\": false,\n"
                      << "  \"layout_match\": false,\n"
                      << "  \"layout_error\": " << jsonStr(layout_msg) << "\n"
                      << "}\n";
        } else {
            std::cerr << "=== bcsvValidate: Comparison ===\n"
                      << "File A: " << cfg.input_file << "\n"
                      << "File B: " << cfg.compare_file << "\n\n"
                      << "Layout MISMATCH: " << layout_msg << "\n"
                      << "\nResult: FAILED\n";
        }
        return 1;
    }

    // Compare rows
    bcsv_validation::RoundTripValidator validator(cfg.max_errors, cfg.tolerance);
    size_t rows_a = 0, rows_b = 0;

    // Lambdas to abstract over the two reader types
    auto readNextB = [&]() -> bool {
        return csv_b ? reader_csv_b->readNext() : reader_bcsv_b.readNext();
    };
    auto rowB = [&]() -> const bcsv::Layout::RowType& {
        return csv_b ? reader_csv_b->row() : reader_bcsv_b.row();
    };

    while (true) {
        bool has_a = reader_a.readNext();
        bool has_b = readNextB();

        if (!has_a && !has_b) break;

        if (has_a) ++rows_a;
        if (has_b) ++rows_b;

        if (has_a && has_b) {
            validator.compareRow(rows_a - 1, reader_a.row(), rowB(), layout_a);

            if (cfg.verbose && ((rows_a & 0xFFFF) == 0)) {
                std::cerr << "  Compared " << rows_a << " rows...\n";
            }
        }

        if (has_a != has_b) {
            // One file ended before the other — count remaining rows
            if (has_a) {
                while (reader_a.readNext()) ++rows_a;
            }
            if (has_b) {
                while (readNextB()) ++rows_b;
            }
            break;
        }
    }

    reader_a.close();
    if (csv_b) reader_csv_b->close();
    else reader_bcsv_b.close();

    bool row_count_ok = (rows_a == rows_b);
    bool content_ok = validator.passed();
    bool overall_ok = row_count_ok && content_ok;

    // ── Output ──────────────────────────────────────────────────

    if (cfg.json) {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"file_a\": " << jsonStr(cfg.input_file) << ",\n"
           << "  \"file_b\": " << jsonStr(cfg.compare_file) << ",\n"
           << "  \"rows_a\": " << rows_a << ",\n"
           << "  \"rows_b\": " << rows_b << ",\n"
           << "  \"row_count_match\": " << (row_count_ok ? "true" : "false") << ",\n"
           << "  \"tolerance\": " << cfg.tolerance << ",\n"
           << "  \"mismatches\": " << validator.errorCount() << ",\n"
           << "  \"valid\": " << (overall_ok ? "true" : "false") << ",\n"
           << "  \"details\": " << validator.toJson() << "\n"
           << "}";
        std::cout << ss.str() << "\n";
    } else {
        std::cerr << "=== bcsvValidate: Comparison ===\n"
                  << "File A: " << cfg.input_file << "\n"
                  << "File B: " << cfg.compare_file << "\n"
                  << "Rows A: " << rows_a << "\n"
                  << "Rows B: " << rows_b << "\n";

        if (cfg.tolerance > 0.0)
            std::cerr << "Tolerance: " << cfg.tolerance << "\n";

        if (!row_count_ok) {
            std::cerr << "\nRow count MISMATCH: A=" << rows_a
                      << " B=" << rows_b << "\n";
        }

        std::cerr << "\n" << validator.summary() << "\n";
        std::cerr << "Result: " << (overall_ok ? "PASSED" : "FAILED") << "\n";
    }

    return overall_ok ? 0 : 1;
}

// ── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    try {
        Config cfg = parseArgs(argc, argv);

        if (cfg.help) {
            printUsage(argv[0]);
            return 0;
        }

        // ── List profiles ───────────────────────────────────────
        if (cfg.list_profiles) {
            const auto& profiles = bench::getAllProfilesCached();
            std::cout << "Available dataset profiles (" << profiles.size() << "):\n\n";
            std::cout << std::left
                      << std::setw(28) << "Name"
                      << std::setw(8)  << "Cols"
                      << std::setw(10) << "DefRows"
                      << "Description\n";
            std::cout << std::string(28, '-') << "  "
                      << std::string(6,  '-') << "  "
                      << std::string(8,  '-') << "  "
                      << std::string(40, '-') << "\n";
            for (const auto& p : profiles) {
                std::cout << std::left
                          << std::setw(28) << p.name
                          << std::setw(8)  << p.layout.columnCount()
                          << std::setw(10) << p.default_rows
                          << p.description << "\n";
            }
            return 0;
        }

        // ── Dispatch mode ───────────────────────────────────────
        if (!cfg.compare_file.empty()) {
            return validateComparison(cfg);
        } else if (!cfg.profile.empty()) {
            return validatePattern(cfg);
        } else {
            return validateStructure(cfg);
        }

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
