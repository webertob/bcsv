/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsvTail.cpp
 * @brief CLI tool to display the last few rows of a BCSV file in CSV format
 * 
 * Reads a BCSV file and prints the last N rows to stdout in CSV format.
 * Uses ReaderDirectAccess for efficient O(N_requested) random-access tail
 * when a file footer is available.  Falls back to sequential streaming with
 * a circular buffer when the footer is missing (stream-mode files).
 *
 * Uses CsvWriter with std::cout for consistent RFC 4180 output.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <bcsv/bcsv.h>
#include "cli_common.h"

struct Config {
    std::string input_file;
    size_t num_rows = 10;
    char delimiter = ',';
    bool include_header = true;
    bool verbose = false;
    bool help = false;
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE\n\n";
    std::cout << "Display the last few rows of a BCSV file in CSV format.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input BCSV file path\n\n";
    std::cout << "Options:\n";
    std::cout << "  -n, --lines N           Number of rows to display (default: 10)\n";
    std::cout << "  -d, --delimiter CHAR    Field delimiter (default: ',')\n";
    std::cout << "  --no-header             Don't include header row in output\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -h, --help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " data.bcsv\n";
    std::cout << "  " << program_name << " -n 20 data.bcsv\n";
    std::cout << "  " << program_name << " --no-header data.bcsv\n";
    std::cout << "  " << program_name << " -d ';' data.bcsv\n";
    std::cout << "  " << program_name << " data.bcsv | wc -l\n";
}

Config parseArgs(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            config.help = true;
            return config;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--no-header") {
            config.include_header = false;
        } else if ((arg == "-n" || arg == "--lines") && i + 1 < argc) {
            try {
                int num = std::stoi(argv[++i]);
                if (num <= 0) {
                    throw std::runtime_error("Number of lines must be positive: " + std::to_string(num));
                }
                config.num_rows = static_cast<size_t>(num);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error(std::string("Invalid number of lines: ") + argv[i]);
            }
        } else if ((arg == "-d" || arg == "--delimiter") && i + 1 < argc) {
            std::string delim = argv[++i];
            if (delim.length() != 1) {
                throw std::runtime_error("Delimiter must be a single character: " + delim);
            }
            config.delimiter = delim[0];
        } else if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + arg);
        } else {
            if (config.input_file.empty()) {
                config.input_file = arg;
            } else {
                throw std::runtime_error("Too many arguments. Only one input file expected.");
            }
        }
    }
    
    if (config.input_file.empty() && !config.help) {
        throw std::runtime_error("Input file is required");
    }
    
    return config;
}

// Helper: emit a single row from reader through CsvWriter
static void emitRow(bcsv::CsvWriter<bcsv::Layout>& csv_writer,
                    const bcsv::Row& row) {
    row.visitConst([&](size_t col, const auto& val) {
        csv_writer.row().set(col, val);
    });
    csv_writer.writeRow();
}

// Fast path using ReaderDirectAccess – reads only the required rows
static size_t tailDirectAccess(const Config& config) {
    bcsv::ReaderDirectAccess<bcsv::Layout> reader;
    if (!reader.open(config.input_file)) {
        // Footer missing or corrupt – signal caller to fall back
        return SIZE_MAX;
    }

    const auto& layout = reader.layout();
    const size_t total = reader.rowCount();

    if (config.verbose) {
        std::cerr << "Direct-access mode: " << total << " total rows" << std::endl;
    }

    bcsv::CsvWriter<bcsv::Layout> csv_writer(layout, config.delimiter);
    csv_writer.open(std::cout, config.include_header);

    const size_t start = (total > config.num_rows) ? total - config.num_rows : 0;
    size_t rows_printed = 0;
    for (size_t i = start; i < total; ++i) {
        if (!reader.read(i)) {
            std::cerr << "Warning: Failed to read row " << i << std::endl;
            continue;
        }
        emitRow(csv_writer, reader.row());
        rows_printed++;
    }

    reader.close();
    csv_writer.close();
    return rows_printed;
}

// Slow path – streams entire file, keeps last N formatted rows in a deque
static size_t tailSequential(const Config& config) {
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(config.input_file)) {
        std::cerr << "Error: Cannot open BCSV file: " << config.input_file << std::endl;
        return 0;
    }

    const auto& layout = reader.layout();

    if (config.verbose) {
        std::cerr << "Sequential mode (footer unavailable)" << std::endl;
    }

    // Buffer the last N rows as formatted CSV strings via a stringstream.
    // CsvWriter handles RFC 4180 escaping through its normal code path.
    std::deque<std::string> row_buffer;
    size_t total_rows = 0;

    // A CsvWriter writing into a reusable stringstream
    std::ostringstream oss;
    bcsv::CsvWriter<bcsv::Layout> csv_fmt(layout, config.delimiter);
    csv_fmt.open(oss, /*includeHeader=*/false);

    while (reader.readNext()) {
        oss.str("");
        oss.clear();

        reader.row().visitConst([&](size_t col, const auto& val) {
            csv_fmt.row().set(col, val);
        });
        csv_fmt.writeRow();

        row_buffer.push_back(oss.str());
        if (row_buffer.size() > config.num_rows) {
            row_buffer.pop_front();
        }
        total_rows++;
    }
    reader.close();
    csv_fmt.close();

    if (config.verbose) {
        std::cerr << "Total rows in file: " << total_rows << std::endl;
        std::cerr << "Displaying last " << row_buffer.size() << " rows" << std::endl;
    }

    // Print header (use CsvWriter for proper RFC 4180 quoting).
    // CsvWriter::open() writes the header immediately when includeHeader
    // is true, so we just open+close — no dummy row needed.
    if (config.include_header) {
        std::ostringstream hdr_oss;
        bcsv::CsvWriter<bcsv::Layout> hdr_writer(layout, config.delimiter);
        hdr_writer.open(hdr_oss, /*includeHeader=*/true);
        hdr_writer.close();
        std::cout << hdr_oss.str();
    }

    // Print buffered rows (each already ends with newline from CsvWriter)
    for (const auto& line : row_buffer) {
        std::cout << line;
    }

    return row_buffer.size();
}

int main(int argc, char* argv[]) {
    try {
        Config config = parseArgs(argc, argv);
        
        if (config.help) {
            printUsage(argv[0]);
            return 0;
        }
        
        if (config.verbose) {
            std::cerr << "Reading: " << config.input_file << std::endl;
            std::cerr << "Lines: " << config.num_rows << std::endl;
        }
        
        // Try fast direct-access first; fall back to sequential
        size_t printed = tailDirectAccess(config);
        if (printed == SIZE_MAX) {
            if (config.verbose) {
                std::cerr << "Direct-access unavailable, falling back to sequential" << std::endl;
            }
            printed = tailSequential(config);
        }
        
        if (config.verbose) {
            std::cerr << "Successfully displayed " << printed << " rows" << std::endl;
        }
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
