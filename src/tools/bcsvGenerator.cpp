/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsvGenerator.cpp
 * @brief CLI tool to generate synthetic BCSV test datasets
 *
 * Uses the 14 MACRO benchmark dataset profiles to produce deterministic,
 * reproducible BCSV files with configurable row count, data mode, and
 * output encoding.
 *
 * Default: 10 000 rows, mixed_generic profile, timeseries data mode,
 *          packet + lz4 + batch + delta encoding.
 */

#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <map>
#include <bcsv/bcsv.h>
#include "bench_datasets.hpp"   // benchmark/src/ — added via target_include_directories

// ── Configuration ───────────────────────────────────────────────────

struct Config {
    std::string output_file;
    std::string profile       = "mixed_generic";
    std::string data_mode     = "timeseries";  // timeseries | random
    size_t      rows          = 10000;

    // Codec selection
    std::string file_codec    = "packet_lz4_batch"; // packet_lz4_batch | packet_lz4 | packet | stream_lz4 | stream
    std::string row_codec     = "delta";             // delta | zoh | flat
    size_t      compression_level = 1;
    size_t      block_size_kb     = 64;

    // Flags
    bool        overwrite         = false;
    bool        list_profiles     = false;
    bool        verbose           = false;
    bool        help              = false;
};

// ── Usage ───────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog
        << " [OPTIONS] -o OUTPUT_FILE\n\n"

        << "Generate a synthetic BCSV test dataset.\n\n"

        << "Arguments:\n"
        << "  -o, --output FILE        Output BCSV file (required)\n\n"

        << "Dataset:\n"
        << "  -p, --profile NAME       Dataset profile (default: mixed_generic)\n"
        << "  -n, --rows N             Number of rows (default: 10000)\n"
        << "  -d, --data-mode MODE     Data mode: timeseries (default) or random\n"
        << "  --list                   List available profiles and exit\n\n"

        << "Encoding:\n"
        << "  --file-codec CODEC       File codec (default: packet_lz4_batch)\n"
        << "                           Values: packet_lz4_batch, packet_lz4,\n"
        << "                                   packet, stream_lz4, stream\n"
        << "  --row-codec CODEC        Row codec (default: delta)\n"
        << "                           Values: delta, zoh, flat\n"
        << "  --compression-level N    LZ4 compression level (default: 1)\n"
        << "  --block-size N           Block size in KB (default: 64)\n\n"

        << "General:\n"
        << "  -f, --overwrite          Overwrite output file if it exists\n"
        << "  -v, --verbose            Verbose progress output\n"
        << "  -h, --help               Show this help message\n\n"

        << "Examples:\n"
        << "  " << prog << " -o test.bcsv\n"
        << "  " << prog << " -p sensor_noisy -n 100000 -o sensor.bcsv\n"
        << "  " << prog << " -p weather_timeseries -d random -o weather.bcsv\n"
        << "  " << prog << " -p string_heavy --file-codec packet --row-codec flat -o strings.bcsv\n"
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
        } else if (arg == "-f" || arg == "--overwrite") {
            cfg.overwrite = true;
        } else if (arg == "--file-codec" && i + 1 < argc) {
            cfg.file_codec = argv[++i];
            if (cfg.file_codec != "packet_lz4_batch" &&
                cfg.file_codec != "packet_lz4" &&
                cfg.file_codec != "packet" &&
                cfg.file_codec != "stream_lz4" &&
                cfg.file_codec != "stream") {
                std::cerr << "Error: Unknown file codec '" << cfg.file_codec
                          << "'. Expected: packet_lz4_batch, packet_lz4, "
                             "packet, stream_lz4, stream.\n";
                exit(1);
            }
        } else if (arg == "--row-codec" && i + 1 < argc) {
            cfg.row_codec = argv[++i];
            if (cfg.row_codec != "delta" && cfg.row_codec != "zoh" &&
                cfg.row_codec != "flat") {
                std::cerr << "Error: Unknown row codec '" << cfg.row_codec
                          << "'. Expected: delta, zoh, flat.\n";
                exit(1);
            }
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            cfg.output_file = argv[++i];
        } else if ((arg == "-p" || arg == "--profile") && i + 1 < argc) {
            cfg.profile = argv[++i];
        } else if ((arg == "-d" || arg == "--data-mode") && i + 1 < argc) {
            cfg.data_mode = argv[++i];
            if (cfg.data_mode != "timeseries" && cfg.data_mode != "random") {
                std::cerr << "Error: Unknown data mode '" << cfg.data_mode
                          << "'. Expected 'timeseries' or 'random'.\n";
                exit(1);
            }
        } else if ((arg == "-n" || arg == "--rows") && i + 1 < argc) {
            try {
                int n = std::stoi(argv[++i]);
                if (n <= 0) {
                    std::cerr << "Error: Row count must be positive.\n";
                    exit(1);
                }
                cfg.rows = static_cast<size_t>(n);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid row count: " << argv[i] << "\n";
                exit(1);
            }
        } else if (arg == "--compression-level" && i + 1 < argc) {
            try {
                int lvl = std::stoi(argv[++i]);
                if (lvl < 0) {
                    std::cerr << "Error: Compression level must be non-negative.\n";
                    exit(1);
                }
                cfg.compression_level = static_cast<size_t>(lvl);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid compression level: " << argv[i] << "\n";
                exit(1);
            }
        } else if (arg == "--block-size" && i + 1 < argc) {
            try {
                int bs = std::stoi(argv[++i]);
                if (bs <= 0) {
                    std::cerr << "Error: Block size must be positive.\n";
                    exit(1);
                }
                cfg.block_size_kb = static_cast<size_t>(bs);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid block size: " << argv[i] << "\n";
                exit(1);
            }
        } else if (arg.starts_with("-")) {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            exit(1);
        } else {
            // Treat bare positional arg as output file if -o not used
            if (cfg.output_file.empty()) {
                cfg.output_file = arg;
            } else {
                std::cerr << "Error: Too many positional arguments.\n";
                exit(1);
            }
        }
    }

    if (cfg.output_file.empty() && !cfg.help && !cfg.list_profiles) {
        std::cerr << "Error: Output file is required (-o FILE).\n";
        exit(1);
    }

    return cfg;
}

// ── Helpers ─────────────────────────────────────────────────────────

static std::string columnTypeStr(bcsv::ColumnType type) {
    switch (type) {
        case bcsv::ColumnType::BOOL:   return "bool";
        case bcsv::ColumnType::INT8:   return "int8";
        case bcsv::ColumnType::UINT8:  return "uint8";
        case bcsv::ColumnType::INT16:  return "int16";
        case bcsv::ColumnType::UINT16: return "uint16";
        case bcsv::ColumnType::INT32:  return "int32";
        case bcsv::ColumnType::UINT32: return "uint32";
        case bcsv::ColumnType::INT64:  return "int64";
        case bcsv::ColumnType::UINT64: return "uint64";
        case bcsv::ColumnType::FLOAT:  return "float";
        case bcsv::ColumnType::DOUBLE: return "double";
        case bcsv::ColumnType::STRING: return "string";
        default:                       return "unknown";
    }
}

/// Print vertical layout table: type histogram + full column listing.
static void printLayoutSummary(const bcsv::Layout& layout, std::ostream& os) {
    const size_t n = layout.columnCount();
    if (n == 0) { os << "  (empty layout)\n"; return; }

    // Build type histogram
    std::map<std::string, size_t> type_counts;
    size_t max_name_len = 4;   // minimum width for "Name" header
    size_t max_type_len = 4;   // minimum width for "Type" header
    for (size_t i = 0; i < n; ++i) {
        const std::string name = layout.columnName(i);
        const std::string type = columnTypeStr(layout.columnType(i));
        type_counts[type]++;
        if (name.size() > max_name_len) max_name_len = name.size();
        if (type.size() > max_type_len) max_type_len = type.size();
    }

    // Type histogram line
    os << "  Columns: " << n << "  [ ";
    bool first = true;
    for (const auto& [tname, cnt] : type_counts) {
        if (!first) os << ", ";
        os << cnt << "\xc3\x97" << tname;
        first = false;
    }
    os << " ]\n";

    // Column index width
    size_t idx_width = 1;
    for (size_t v = n - 1; v >= 10; v /= 10) ++idx_width;
    if (idx_width < 3) idx_width = 3;  // minimum for "Idx" header

    // Table header
    os << "\n"
       << "  " << std::right << std::setw(static_cast<int>(idx_width)) << "Idx"
       << "  " << std::left  << std::setw(static_cast<int>(max_name_len)) << "Name"
       << "  " << std::left  << std::setw(static_cast<int>(max_type_len)) << "Type"
       << "\n";

    // Separator
    os << "  " << std::string(idx_width, '-')
       << "  " << std::string(max_name_len, '-')
       << "  " << std::string(max_type_len, '-')
       << "\n";

    // Column rows
    for (size_t i = 0; i < n; ++i) {
        os << "  " << std::right << std::setw(static_cast<int>(idx_width)) << i
           << "  " << std::left  << std::setw(static_cast<int>(max_name_len)) << layout.columnName(i)
           << "  " << std::left  << std::setw(static_cast<int>(max_type_len)) << columnTypeStr(layout.columnType(i))
           << "\n";
    }
}

static std::string encodingDescription(const Config& cfg) {
    return cfg.row_codec + " + " + cfg.file_codec
         + " (level " + std::to_string(cfg.compression_level) + ")";
}

static std::string formatBytes(uintmax_t bytes) {
    std::ostringstream oss;
    if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        oss << bytes << " bytes";
    }
    return oss.str();
}

// ── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    try {
        Config cfg = parseArgs(argc, argv);

        if (cfg.help) {
            printUsage(argv[0]);
            return 0;
        }

        // ── List profiles ───────────────────────────────────────────
        if (cfg.list_profiles) {
            const auto& profiles = bench::getAllProfilesCached();
            std::cout << "Available dataset profiles (" << profiles.size() << "):\n\n";
            std::cout << std::left
                      << std::setw(28) << "Name"
                      << std::setw(8)  << "Cols"
                      << std::setw(10) << "DefRows"
                      << "Description\n";
            std::cout << std::string(28, '-') << "  "
                      << std::string(6, '-')  << "  "
                      << std::string(8, '-')  << "  "
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

        // ── Resolve profile ─────────────────────────────────────────
        bench::DatasetProfile profile;
        try {
            profile = bench::getProfile(cfg.profile);
        } catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << "\n";
            std::cerr << "Use --list to see available profiles.\n";
            return 1;
        }

        const auto& layout = profile.layout;
        const size_t num_cols = layout.columnCount();

        if (cfg.verbose) {
            std::cerr << "Profile:   " << profile.name << "\n";
            std::cerr << "           " << profile.description << "\n";
            std::cerr << "Data mode: " << cfg.data_mode << "\n";
            std::cerr << "Rows:      " << cfg.rows << "\n";
            std::cerr << "Encoding:  " << encodingDescription(cfg) << "\n";
            printLayoutSummary(layout, std::cerr);
        }

        // ── Validate output path ────────────────────────────────────
        if (!cfg.overwrite && std::filesystem::exists(cfg.output_file)) {
            std::cerr << "Error: Output file already exists: "
                      << cfg.output_file
                      << "\n       Use -f / --overwrite to replace.\n";
            return 1;
        }

        // ── Select generator ────────────────────────────────────────
        const bool timeseries = (cfg.data_mode == "timeseries");
        auto& generator = timeseries ? profile.generateZoH : profile.generate;

        // ── Build FileFlags ─────────────────────────────────────────
        bcsv::FileFlags flags = bcsv::FileFlags::NONE;

        const bool has_lz4   = (cfg.file_codec == "packet_lz4_batch" ||
                                cfg.file_codec == "packet_lz4" ||
                                cfg.file_codec == "stream_lz4");
        const bool has_batch = (cfg.file_codec == "packet_lz4_batch");
        const bool is_stream = (cfg.file_codec == "stream_lz4" ||
                                cfg.file_codec == "stream");

        if (has_batch) {
#ifdef BCSV_HAS_BATCH_CODEC
            flags = flags | bcsv::FileFlags::BATCH_COMPRESS;
#else
            std::cerr << "Error: Batch codec not available "
                         "(BCSV_ENABLE_BATCH_CODEC=OFF).\n"
                         "       Use --file-codec packet_lz4 instead.\n";
            return 1;
#endif
        }
        if (is_stream)
            flags = flags | bcsv::FileFlags::STREAM_MODE;
        if (cfg.row_codec == "zoh")
            flags = flags | bcsv::FileFlags::ZERO_ORDER_HOLD;

        size_t comp_level = has_lz4 ? cfg.compression_level : 0;

        // ── Write ───────────────────────────────────────────────────
        auto start_time = std::chrono::steady_clock::now();

        auto do_write = [&](auto& writer) {
            writer.open(cfg.output_file, cfg.overwrite, comp_level,
                        cfg.block_size_kb, flags);

            for (size_t r = 0; r < cfg.rows; ++r) {
                generator(writer.row(), r);
                writer.writeRow();

                if (cfg.verbose && ((r + 1) & 0xFFFF) == 0) {
                    std::cerr << "  Written " << (r + 1) << " / "
                              << cfg.rows << " rows...\n";
                }
            }

            writer.close();
        };

        if (cfg.row_codec == "delta") {
            bcsv::WriterDelta<bcsv::Layout> writer(layout);
            do_write(writer);
        } else if (cfg.row_codec == "zoh") {
            bcsv::WriterZoH<bcsv::Layout> writer(layout);
            do_write(writer);
        } else {
            bcsv::Writer<bcsv::Layout> writer(layout);
            do_write(writer);
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               end_time - start_time).count();
        if (duration_ms == 0) duration_ms = 1;

        // ── Summary ─────────────────────────────────────────────────
        auto file_size = std::filesystem::file_size(cfg.output_file);
        double duration_s  = duration_ms / 1000.0;
        double krows_per_s = (static_cast<double>(cfg.rows) / 1000.0) / duration_s;
        double mb_per_s    = (static_cast<double>(file_size) / (1024.0 * 1024.0))
                             / duration_s;

        std::cerr << "\n=== bcsvGenerator Summary ===\n";
        std::cerr << "Profile:    " << profile.name << "\n";
        std::cerr << "Data mode:  " << cfg.data_mode << "\n";

        printLayoutSummary(layout, std::cerr);

        std::cerr << "\n"
                  << "  Rows written: " << cfg.rows << "\n"
                  << "  Columns:      " << num_cols << "\n"
                  << "  Encoding:     " << encodingDescription(cfg) << "\n"
                  << "  File size:    " << file_size << " bytes ("
                  << formatBytes(file_size) << ")\n"
                  << "\n"
                  << "  Wall time:    " << duration_ms << " ms\n"
                  << "  Throughput:   " << std::fixed << std::setprecision(1)
                  << krows_per_s << " krows/s, "
                  << std::setprecision(2) << mb_per_s << " MB/s\n"
                  << "\n"
                  << "  Output: " << cfg.output_file << "\n";

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
