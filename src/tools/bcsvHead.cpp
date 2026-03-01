/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsvHead.cpp
 * @brief CLI tool to display the first few rows of a BCSV file in CSV format
 * 
 * Reads a BCSV file and prints the first N rows to stdout in CSV format,
 * including the header.  Designed for quick inspection and piping to other tools.
 *
 * Uses CsvWriter with std::cout for consistent RFC 4180 output.
 */

#include <iostream>
#include <string>
#include <filesystem>
#include <cstdint>
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
    std::cout << "Display the first few rows of a BCSV file in CSV format.\n\n";
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
    std::cout << "  " << program_name << " data.bcsv | grep \"pattern\"\n";
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
            std::cerr << "Include header: " << (config.include_header ? "yes" : "no") << std::endl;
            std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
        }
        
        // Open BCSV file
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(config.input_file)) {
            std::cerr << "Error: Cannot open BCSV file: " << config.input_file << std::endl;
            return 1;
        }
        
        const auto& layout = reader.layout();
        if (config.verbose) {
            std::cerr << "Layout contains " << layout.columnCount() << " columns" << std::endl;
        }
        
        // Use CsvWriter writing to stdout for consistent RFC 4180 output
        bcsv::CsvWriter<bcsv::Layout> csv_writer(layout, config.delimiter);
        csv_writer.open(std::cout, config.include_header);
        
        // Stream first N rows
        size_t rows_printed = 0;
        while (reader.readNext() && rows_printed < config.num_rows) {
            reader.row().visitConst([&](size_t col, const auto& val) {
                csv_writer.row().set(col, val);
            });
            csv_writer.writeRow();
            rows_printed++;
        }
        
        reader.close();
        csv_writer.close();
        
        if (config.verbose) {
            std::cerr << "Successfully displayed " << rows_printed << " rows" << std::endl;
        }
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
