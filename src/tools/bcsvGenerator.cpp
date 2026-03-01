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
#include <stdexcept>
#include <bcsv/bcsv.h>
#include "cli_common.h"
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
            bcsv_cli::validateFileCodec(cfg.file_codec);
        } else if (arg == "--row-codec" && i + 1 < argc) {
            cfg.row_codec = argv[++i];
            bcsv_cli::validateRowCodec(cfg.row_codec);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            cfg.output_file = argv[++i];
        } else if ((arg == "-p" || arg == "--profile") && i + 1 < argc) {
            cfg.profile = argv[++i];
        } else if ((arg == "-d" || arg == "--data-mode") && i + 1 < argc) {
            cfg.data_mode = argv[++i];
            if (cfg.data_mode != "timeseries" && cfg.data_mode != "random") {
                throw std::runtime_error("Unknown data mode '" + cfg.data_mode + "'. Expected 'timeseries' or 'random'.");
            }
        } else if ((arg == "-n" || arg == "--rows") && i + 1 < argc) {
            try {
                int n = std::stoi(argv[++i]);
                if (n <= 0) {
                    throw std::runtime_error("Row count must be positive.");
                }
                cfg.rows = static_cast<size_t>(n);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error(std::string("Invalid row count: ") + argv[i]);
            }
        } else if (arg == "--compression-level" && i + 1 < argc) {
            try {
                int lvl = std::stoi(argv[++i]);
                if (lvl < 0) {
                    throw std::runtime_error("Compression level must be non-negative.");
                }
                cfg.compression_level = static_cast<size_t>(lvl);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error(std::string("Invalid compression level: ") + argv[i]);
            }
        } else if (arg == "--block-size" && i + 1 < argc) {
            try {
                int bs = std::stoi(argv[++i]);
                if (bs <= 0) {
                    throw std::runtime_error("Block size must be positive.");
                }
                cfg.block_size_kb = static_cast<size_t>(bs);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error(std::string("Invalid block size: ") + argv[i]);
            }
        } else if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + arg);
        } else {
            // Treat bare positional arg as output file if -o not used
            if (cfg.output_file.empty()) {
                cfg.output_file = arg;
            } else {
                throw std::runtime_error("Too many positional arguments.");
            }
        }
    }

    if (cfg.output_file.empty() && !cfg.help && !cfg.list_profiles) {
        throw std::runtime_error("Output file is required (-o FILE).");
    }

    return cfg;
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
            std::cerr << "Encoding:  " << bcsv_cli::encodingDescription(
                cfg.row_codec, cfg.file_codec, cfg.compression_level) << "\n";
            bcsv_cli::printLayoutSummary("Layout", layout);
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

        // ── Resolve codec flags ─────────────────────────────────────
        auto codec = bcsv_cli::resolveCodecFlags(cfg.file_codec,
                                                  cfg.row_codec,
                                                  cfg.compression_level);

        // ── Write ───────────────────────────────────────────────────
        auto start_time = std::chrono::steady_clock::now();

        auto do_write = [&](auto& writer) {
            writer.open(cfg.output_file, cfg.overwrite, codec.comp_level,
                        cfg.block_size_kb, codec.flags);

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

        bcsv_cli::withWriter(layout, cfg.row_codec, do_write);

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

        bcsv_cli::printLayoutSummary("Layout", layout, std::cerr);

        std::cerr << "\n"
                  << "  Rows written: " << cfg.rows << "\n"
                  << "  Columns:      " << num_cols << "\n"
                  << "  Encoding:     " << bcsv_cli::encodingDescription(
                         cfg.row_codec, cfg.file_codec, cfg.compression_level) << "\n"
                  << "  File size:    " << file_size << " bytes ("
                  << bcsv_cli::formatBytes(file_size) << ")\n"
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
