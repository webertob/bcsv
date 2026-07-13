/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsvSampler.cpp
 * @brief CLI tool to filter and project BCSV files using Sampler expressions
 *
 * Reads an input BCSV file, applies an optional conditional (filter) and/or
 * selection (projection) expression via the Sampler bytecode VM, and writes
 * matching rows to a new BCSV file.
 *
 * Default output encoding: Packet + LZ4 + Batch + Delta
 */

#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <stdexcept>
#include <bcsv/bcsv.h>
#include <bcsv/sampler/sampler.h>
#include <bcsv/sampler/sampler.hpp>
#include "cli_app.h"

// ── Configuration ───────────────────────────────────────────────────

struct Config {
    std::string input_file;
    std::string output_file;

    // Sampler expressions
    std::string conditional;        // -c / --conditional
    std::string selection;          // -s / --selection

    // Sampler behaviour
    std::string mode = "truncate";  // -m / --mode  {truncate, expand}

    // Writer encoding knobs
    std::string row_codec    = bcsv_cli::DEFAULT_ROW_CODEC;      // --row-codec
    std::string file_codec   = bcsv_cli::DEFAULT_FILE_CODEC;     // --file-codec
    size_t      compression_level = 1;
    size_t      block_size_kb     = bcsv::DEFAULT_PACKET_SIZE_KB;

    // Flags
    bool        overwrite         = false;  // -f / --overwrite
    bool        disassemble       = false;  // --disassemble
    bool        verbose           = false;  // -v / --verbose
};

// Argument parsing is handled by CLI11 in main() below.

// ── Helpers ─────────────────────────────────────────────────────────

/// Print a compilation error with caret indicator.
static void printCompileError(const std::string& label,
                              const std::string& expr,
                              const bcsv::SamplerCompileResult& cr) {
    std::cerr << "Error: Failed to compile " << label << " expression.\n";
    std::cerr << "  Expression: " << expr << "\n";

    // Position indicator
    if (cr.error_position <= expr.size()) {
        std::cerr << "              ";
        for (size_t i = 0; i < cr.error_position; ++i)
            std::cerr << ' ';
        std::cerr << "^\n";
    }

    std::cerr << "  " << cr.error_msg << "\n";
}

// ── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    Config cfg;

    CLI::App app{"Filter and project a BCSV file using Sampler expressions.", "bcsvSampler"};
    argv = app.ensure_utf8(argv);
    bcsv_cli::setupVersionFlag(app, bcsv_cli::programName(argv[0]));

    app.add_option("INPUT_FILE", cfg.input_file, "Input BCSV file")
        ->required();
    app.add_option("OUTPUT_FILE", cfg.output_file,
                   "Output BCSV file (default: <input>_sampled.bcsv)");
    app.add_option("-c,--conditional", cfg.conditional, "Row filter (boolean expression)");
    app.add_option("-s,--selection", cfg.selection, "Column projection (comma-separated)");
    app.add_option("-m,--mode", cfg.mode, "Boundary mode: truncate or expand")
        ->check(CLI::IsMember({"truncate", "expand"}))
        ->capture_default_str();
    app.add_flag("-f,--overwrite", cfg.overwrite, "Overwrite output file if it exists");
    app.add_flag("--disassemble", cfg.disassemble, "Print compiled bytecode and exit");
    bcsv_cli::addCodecOptions(app, cfg.row_codec, cfg.file_codec,
                              cfg.compression_level, cfg.block_size_kb);
    app.add_flag("-v,--verbose", cfg.verbose, "Verbose progress output");

    app.footer(
        "Examples:\n"
        "  bcsvSampler data.bcsv\n"
        "  bcsvSampler -c 'X[0][0] > 100' data.bcsv filtered.bcsv\n"
        "  bcsvSampler -s 'X[0][0], X[0][2]' data.bcsv projected.bcsv\n"
        "  bcsvSampler -c 'X[0][1] != X[-1][1]' -s 'X[0][0], X[0][1]' -m expand in.bcsv out.bcsv\n"
        "  bcsvSampler --disassemble -c 'X[0][0] > 0' data.bcsv");

    CLI11_PARSE(app, argc, argv);

    // Default output filename: <stem>_sampled.bcsv
    if (cfg.output_file.empty() && !cfg.input_file.empty()) {
        std::filesystem::path p(cfg.input_file);
        cfg.output_file = p.stem().string() + "_sampled.bcsv";
    }

    try {
        // ── Validate input ──────────────────────────────────────────
        if (!std::filesystem::exists(cfg.input_file)) {
            std::cerr << "Error: Input file does not exist: "
                      << cfg.input_file << "\n";
            return 1;
        }

        if (cfg.conditional.empty() && cfg.selection.empty() && !cfg.disassemble) {
            std::cerr << "Error: At least one of -c (conditional) or "
                         "-s (selection) is required.\n";
            return 1;
        }

        // Check overwrite safety
        if (!cfg.overwrite && !cfg.disassemble &&
            std::filesystem::exists(cfg.output_file)) {
            std::cerr << "Error: Output file already exists: "
                      << cfg.output_file
                      << "\n       Use -f / --overwrite to replace.\n";
            return 1;
        }

        // ── Open reader & create sampler ────────────────────────────
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(cfg.input_file)) {
            std::cerr << "Error: Cannot open BCSV file: " << cfg.input_file << "\n"
                      << "  " << reader.getErrorMsg() << "\n";
            return 1;
        }

        const auto& src_layout = reader.layout();

        if (cfg.verbose) {
            std::cerr << "Opened: " << cfg.input_file << "\n";
            bcsv_cli::printLayoutSummary("Input layout", src_layout);
        }

        bcsv::Sampler<bcsv::Layout> sampler(reader);

        // Set boundary mode
        if (cfg.mode == "expand")
            sampler.setMode(bcsv::SamplerMode::EXPAND);
        else
            sampler.setMode(bcsv::SamplerMode::TRUNCATE);

        // ── Compile conditional ─────────────────────────────────────
        if (!cfg.conditional.empty()) {
            auto cr = sampler.setConditional(cfg.conditional);
            if (!cr.success) {
                printCompileError("conditional", cfg.conditional, cr);
                reader.close();
                return 1;
            }
            if (cfg.verbose)
                std::cerr << "Conditional compiled OK: " << cfg.conditional << "\n";
        }

        // ── Compile selection ───────────────────────────────────────
        if (!cfg.selection.empty()) {
            auto cr = sampler.setSelection(cfg.selection);
            if (!cr.success) {
                printCompileError("selection", cfg.selection, cr);
                reader.close();
                return 1;
            }
            if (cfg.verbose)
                std::cerr << "Selection compiled OK: " << cfg.selection << "\n";
        }

        // ── Disassemble mode ────────────────────────────────────────
        if (cfg.disassemble) {
            std::cout << sampler.disassemble();
            reader.close();
            return 0;
        }

        // ── Determine output layout ────────────────────────────────
        // If selection is set, the Sampler builds an output layout;
        // otherwise the output mirrors the source layout.
        const bcsv::Layout& out_layout = cfg.selection.empty()
            ? src_layout
            : sampler.outputLayout();

        if (cfg.verbose) {
            bcsv_cli::printLayoutSummary("Output layout", out_layout);
        }

        // ── Build FileFlags ─────────────────────────────────────────
        auto codec_settings = bcsv_cli::resolveCodecFlags(
            cfg.file_codec, cfg.row_codec, cfg.compression_level);

        // ── Write via lambda (selects Writer variant) ────────────────
        auto start_time = std::chrono::steady_clock::now();
        size_t rows_out = 0;

        auto do_write = [&](auto& writer) {
            writer.open(cfg.output_file, cfg.overwrite,
                        codec_settings.comp_level,
                        cfg.block_size_kb, codec_settings.flags);

            while (sampler.next()) {
                const auto& src_row = sampler.row();
                src_row.visitConst([&](size_t col, const auto& val) {
                    writer.row().set(col, val);
                });
                writer.writeRow();
                ++rows_out;

                if (cfg.verbose && (rows_out & 0x3FFF) == 0) {
                    std::cerr << "  Written " << rows_out << " rows...\n";
                }
            }

            writer.close();
        };

        bcsv_cli::withWriter(out_layout, cfg.row_codec, do_write);

        auto end_time = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               end_time - start_time).count();
        if (duration_ms == 0) duration_ms = 1;

        reader.close();

        // ── Summary ─────────────────────────────────────────────────
        auto input_size  = std::filesystem::file_size(cfg.input_file);
        auto output_size = std::filesystem::file_size(cfg.output_file);
        double duration_s = duration_ms / 1000.0;
        double rows_per_s = static_cast<double>(rows_out) / duration_s;

        // Count source rows: rows_in only counts rows passing the
        // sampler.  We need the reader's total.  Since the sampler
        // consumed the reader fully, sourceRowPos() is the 1-based
        // count of the last row read.
        size_t total_source_rows = sampler.sourceRowPos();

        std::cerr << "\n=== bcsvSampler Summary ===\n";

        bcsv_cli::printLayoutSummary("Input", src_layout, std::cerr);
        bcsv_cli::printLayoutSummary("Output", out_layout, std::cerr);

        std::cerr << "\nRows:\n"
                  << "  Source rows read:   " << total_source_rows << "\n"
                  << "  Rows written:       " << rows_out << "\n";
        if (total_source_rows > 0) {
            double pass_pct = 100.0 * static_cast<double>(rows_out)
                              / static_cast<double>(total_source_rows);
            std::cerr << "  Pass rate:          " << std::fixed
                      << std::setprecision(1) << pass_pct << "%\n";
        }

        std::cerr << "\nEncoding:  "
                  << bcsv_cli::encodingDescription(cfg.row_codec,
                                                    cfg.file_codec,
                                                    cfg.compression_level)
                  << "\n";

        std::cerr << "\nFile sizes:\n"
                  << "  Input:  " << input_size  << " bytes ("
                  << std::fixed << std::setprecision(2)
                  << (input_size / 1024.0)  << " KB)\n"
                  << "  Output: " << output_size << " bytes ("
                  << std::fixed << std::setprecision(2)
                  << (output_size / 1024.0) << " KB)\n";

        std::cerr << "\nPerformance:\n"
                  << "  Wall time:   " << duration_ms << " ms\n"
                  << "  Throughput:  " << std::fixed << std::setprecision(0)
                  << rows_per_s << " rows/s\n";

        std::cerr << "\nOutput written to: " << cfg.output_file << "\n";

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
