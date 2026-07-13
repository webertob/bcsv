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
#include "cli_app.h"
#include "test_datasets.hpp"   // src/shared/ — dataset profiles for generation

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
    size_t      block_size_kb     = bcsv::DEFAULT_PACKET_SIZE_KB;

    // Flags
    bool        overwrite         = false;
    bool        list_profiles     = false;
    bool        verbose           = false;
};

// ── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    Config cfg;

    CLI::App app{"Generate a synthetic BCSV test dataset.", "bcsvGenerator"};
    argv = app.ensure_utf8(argv);
    bcsv_cli::setupVersionFlag(app, bcsv_cli::programName(argv[0]));

    app.add_option("-o,--output", cfg.output_file,
                   "Output BCSV file (required unless --list)");
    app.add_option("output_file", cfg.output_file, "Output BCSV file")
        ->type_name("OUTPUT_FILE");
    app.add_option("-p,--profile", cfg.profile, "Dataset profile")
        ->capture_default_str();
    app.add_option("-n,--rows", cfg.rows, "Number of rows")
        ->check(CLI::PositiveNumber)
        ->capture_default_str();
    app.add_option("-d,--data-mode", cfg.data_mode, "Data mode: timeseries or random")
        ->check(CLI::IsMember({"timeseries", "random"}))
        ->capture_default_str();
    app.add_flag("--list", cfg.list_profiles, "List available profiles and exit");
    app.add_flag("-f,--overwrite", cfg.overwrite, "Overwrite output file if it exists");
    bcsv_cli::addCodecOptions(app, cfg.row_codec, cfg.file_codec,
                              cfg.compression_level, cfg.block_size_kb);
    app.add_flag("-v,--verbose", cfg.verbose, "Verbose progress output");

    app.footer(
        "Examples:\n"
        "  bcsvGenerator -o test.bcsv\n"
        "  bcsvGenerator -p sensor_noisy -n 100000 -o sensor.bcsv\n"
        "  bcsvGenerator -p weather_timeseries -d random -o weather.bcsv\n"
        "  bcsvGenerator -p string_heavy --file-codec packet --row-codec flat -o strings.bcsv\n"
        "  bcsvGenerator --list");

    CLI11_PARSE(app, argc, argv);

    try {
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

        if (cfg.output_file.empty()) {
            std::cerr << "Error: Output file is required (-o FILE). "
                         "Use --list to see profiles.\n";
            return 1;
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
        auto& generator = timeseries ? profile.generateTimeSeries : profile.generate;

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
