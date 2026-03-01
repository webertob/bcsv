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
#include <bcsv/bcsv.h>
#include <bcsv/sampler/sampler.h>
#include <bcsv/sampler/sampler.hpp>

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
    size_t      compression_level = 1;
    size_t      block_size_kb     = 64;
    bool        use_batch         = true;   // --no-batch to disable
    bool        use_delta         = true;   // --no-delta to disable
    bool        use_lz4           = true;   // --no-lz4  to disable (level 0)

    // Flags
    bool        overwrite         = false;  // -f / --overwrite
    bool        disassemble       = false;  // --disassemble
    bool        verbose           = false;  // -v / --verbose
    bool        help              = false;  // -h / --help
};

// ── Usage ───────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog
        << " [OPTIONS] INPUT_FILE [OUTPUT_FILE]\n\n"

        << "Filter and project a BCSV file using Sampler expressions.\n\n"

        << "Arguments:\n"
        << "  INPUT_FILE               Input BCSV file\n"
        << "  OUTPUT_FILE              Output BCSV file (default: INPUT_sampled.bcsv)\n\n"

        << "Sampler expressions:\n"
        << "  -c, --conditional EXPR   Row filter (boolean expression)\n"
        << "  -s, --selection EXPR     Column projection (comma-separated)\n"
        << "  -m, --mode MODE          Boundary mode: truncate (default) or expand\n\n"

        << "Encoding (defaults: packet + lz4 + batch + delta):\n"
        << "  --compression-level N    LZ4 compression level (default: 1)\n"
        << "  --block-size N           Block size in KB (default: 64)\n"
        << "  --no-batch               Disable batch compression\n"
        << "  --no-delta               Disable delta encoding (use flat codec)\n"
        << "  --no-lz4                 Disable LZ4 compression (level 0)\n\n"

        << "General:\n"
        << "  -f, --overwrite          Overwrite output file if it exists\n"
        << "  --disassemble            Print compiled bytecode and exit\n"
        << "  -v, --verbose            Verbose progress output\n"
        << "  -h, --help               Show this help message\n\n"

        << "Examples:\n"
        << "  " << prog << " data.bcsv\n"
        << "  " << prog << " -c 'X[0][0] > 100' data.bcsv filtered.bcsv\n"
        << "  " << prog << " -s 'X[0][0], X[0][2]' data.bcsv projected.bcsv\n"
        << "  " << prog << " -c 'X[0][1] != X[-1][1]' -s 'X[0][0], X[0][1]' -m expand in.bcsv out.bcsv\n"
        << "  " << prog << " --disassemble -c 'X[0][0] > 0' data.bcsv\n"
        << "  " << prog << " --no-batch --no-delta -c 'X[0][2] == 1' in.bcsv out.bcsv\n";
}

// ── Argument parsing ────────────────────────────────────────────────

static Config parseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            cfg.help = true;
            return cfg;
        } else if (arg == "-v" || arg == "--verbose") {
            cfg.verbose = true;
        } else if (arg == "-f" || arg == "--overwrite") {
            cfg.overwrite = true;
        } else if (arg == "--disassemble") {
            cfg.disassemble = true;
        } else if (arg == "--no-batch") {
            cfg.use_batch = false;
        } else if (arg == "--no-delta") {
            cfg.use_delta = false;
        } else if (arg == "--no-lz4") {
            cfg.use_lz4 = false;
        } else if ((arg == "-c" || arg == "--conditional") && i + 1 < argc) {
            cfg.conditional = argv[++i];
        } else if ((arg == "-s" || arg == "--selection") && i + 1 < argc) {
            cfg.selection = argv[++i];
        } else if ((arg == "-m" || arg == "--mode") && i + 1 < argc) {
            cfg.mode = argv[++i];
            if (cfg.mode != "truncate" && cfg.mode != "expand") {
                std::cerr << "Error: Unknown mode '" << cfg.mode
                          << "'. Expected 'truncate' or 'expand'.\n";
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
            // Positional arguments
            if (cfg.input_file.empty()) {
                cfg.input_file = arg;
            } else if (cfg.output_file.empty()) {
                cfg.output_file = arg;
            } else {
                std::cerr << "Error: Too many positional arguments.\n";
                exit(1);
            }
        }
    }

    if (cfg.input_file.empty() && !cfg.help) {
        std::cerr << "Error: Input file is required.\n";
        exit(1);
    }

    // Default output filename: <stem>_sampled.bcsv
    if (cfg.output_file.empty() && !cfg.input_file.empty()) {
        std::filesystem::path p(cfg.input_file);
        cfg.output_file = p.stem().string() + "_sampled.bcsv";
    }

    return cfg;
}

// ── Helpers ─────────────────────────────────────────────────────────

/// Convert ColumnType to human-readable string.
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

/// Print layout table to stderr.
static void printLayout(const std::string& label,
                        const bcsv::Layout& layout,
                        std::ostream& os = std::cerr) {
    const size_t n = layout.columnCount();
    if (n == 0) {
        os << label << ": (empty)\n";
        return;
    }

    // Compute column widths
    size_t w_idx  = std::max(std::to_string(n - 1).length(), static_cast<size_t>(3));
    size_t w_name = 4;
    for (size_t i = 0; i < n; ++i)
        w_name = std::max(w_name, layout.columnName(i).length());

    os << label << " (" << n << " columns):\n";
    os << "  " << std::left
       << std::setw(static_cast<int>(w_idx))  << "Idx" << "  "
       << std::setw(static_cast<int>(w_name)) << "Name" << "  "
       << "Type\n";
    os << "  " << std::string(w_idx, '-') << "  "
       << std::string(w_name, '-') << "  "
       << std::string(8, '-') << "\n";

    for (size_t i = 0; i < n; ++i) {
        os << "  " << std::left
           << std::setw(static_cast<int>(w_idx))  << i << "  "
           << std::setw(static_cast<int>(w_name)) << layout.columnName(i) << "  "
           << columnTypeStr(layout.columnType(i)) << "\n";
    }
}

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

/// Describe the encoding configuration for the summary.
static std::string encodingDescription(const Config& cfg) {
    std::string desc;
    desc += cfg.use_delta ? "delta" : "flat";
    if (cfg.use_lz4)   desc += " + lz4";
    if (cfg.use_batch)  desc += " + batch";
    desc += " (level " + std::to_string(cfg.compression_level) + ")";
    return desc;
}

// ── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    try {
        Config cfg = parseArgs(argc, argv);

        if (cfg.help) {
            printUsage(argv[0]);
            return 0;
        }

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
            std::cerr << "Error: Cannot open BCSV file: "
                      << cfg.input_file << "\n";
            return 1;
        }

        const auto& src_layout = reader.layout();

        if (cfg.verbose) {
            std::cerr << "Opened: " << cfg.input_file << "\n";
            printLayout("Input layout", src_layout);
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
            printLayout("Output layout", out_layout);
        }

        // ── Build FileFlags ─────────────────────────────────────────
        bcsv::FileFlags flags = bcsv::FileFlags::NONE;

        if (cfg.use_batch) {
#ifdef BCSV_HAS_BATCH_CODEC
            flags = flags | bcsv::FileFlags::BATCH_COMPRESS;
#else
            if (cfg.verbose)
                std::cerr << "Note: Batch codec not available "
                             "(BCSV_ENABLE_BATCH_CODEC=OFF). "
                             "Falling back to packet codec.\n";
            cfg.use_batch = false;
#endif
        }

        size_t comp_level = cfg.use_lz4 ? cfg.compression_level : 0;

        // ── Write via lambda (selects Writer vs WriterDelta) ────────
        auto start_time = std::chrono::steady_clock::now();
        size_t rows_in  = 0;
        size_t rows_out = 0;

        auto do_write = [&](auto& writer) {
            writer.open(cfg.output_file, cfg.overwrite, comp_level,
                        cfg.block_size_kb, flags);

            while (sampler.next()) {
                ++rows_in;   // source rows that reached the sampler

                const auto& src_row = sampler.row();
                // Copy each field from sampler row to writer row
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

        if (cfg.use_delta) {
            bcsv::WriterDelta<bcsv::Layout> writer(out_layout);
            do_write(writer);
        } else {
            bcsv::Writer<bcsv::Layout> writer(out_layout);
            do_write(writer);
        }

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

        printLayout("Input", src_layout, std::cerr);
        printLayout("Output", out_layout, std::cerr);

        std::cerr << "\nRows:\n"
                  << "  Source rows read:   " << total_source_rows << "\n"
                  << "  Rows written:       " << rows_out << "\n";
        if (total_source_rows > 0) {
            double pass_pct = 100.0 * static_cast<double>(rows_out)
                              / static_cast<double>(total_source_rows);
            std::cerr << "  Pass rate:          " << std::fixed
                      << std::setprecision(1) << pass_pct << "%\n";
        }

        std::cerr << "\nEncoding:  " << encodingDescription(cfg) << "\n";

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
