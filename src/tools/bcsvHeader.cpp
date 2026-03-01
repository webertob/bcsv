/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsvHeader.cpp
 * @brief CLI tool to display the header structure of a BCSV file
 * 
 * Reads a BCSV file and prints its column structure in a vertical format,
 * showing column index, name, and type for quick file structure overview.
 * Optionally reports file-level metadata (row count, file size).
 */

#include <iostream>
#include <string>
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <bcsv/bcsv.h>
#include "cli_common.h"

struct Config {
    std::string input_file;
    bool verbose = false;
    bool help = false;
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE\n\n";
    std::cout << "Display the header structure of a BCSV file.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input BCSV file path\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v, --verbose           Enable verbose output (includes row count,\n";
    std::cout << "                          file size, compression level)\n";
    std::cout << "  -h, --help              Show this help message\n\n";
    std::cout << "Output Format:\n";
    std::cout << "  Shows columns in vertical format with:\n";
    std::cout << "  - Column index (0-based)\n";
    std::cout << "  - Column name\n";
    std::cout << "  - Column type\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " data.bcsv\n";
    std::cout << "  " << program_name << " -v data.bcsv\n";
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
        
        // Check if file exists
        if (!std::filesystem::exists(config.input_file)) {
            std::cerr << "Error: File does not exist: " << config.input_file << std::endl;
            return 1;
        }
        
        // Try ReaderDirectAccess first to get row count
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        bool has_row_count = reader.open(config.input_file);
        
        if (!has_row_count) {
            // Fall back to plain Reader (footer may be missing)
            bcsv::Reader<bcsv::Layout> plain_reader;
            if (!plain_reader.open(config.input_file)) {
                std::cerr << "Error: Cannot open BCSV file: " << config.input_file << std::endl;
                return 1;
            }
            // We only need the layout; close immediately
            const auto& layout = plain_reader.layout();
            const size_t column_count = layout.columnCount();
            
            std::cout << "BCSV Header Structure: " << config.input_file << std::endl;
            std::cout << "Columns: " << column_count << std::endl;
            if (config.verbose) {
                auto file_size = std::filesystem::file_size(config.input_file);
                std::cout << "File size: " << bcsv_cli::formatBytes(file_size) << std::endl;
                std::cout << "Compression level: " << (int)plain_reader.compressionLevel() << std::endl;
                std::cout << "Row count: (unavailable – no file footer)" << std::endl;
            }
            std::cout << std::endl;
            bcsv_cli::printLayoutSummary("Layout", layout, std::cout);
            plain_reader.close();
            return 0;
        }
        
        const auto& layout = reader.layout();
        const size_t column_count = layout.columnCount();
        
        // Print file-level info
        std::cout << "BCSV Header Structure: " << config.input_file << std::endl;
        std::cout << "Columns: " << column_count << std::endl;
        if (config.verbose) {
            auto file_size = std::filesystem::file_size(config.input_file);
            std::cout << "File size: " << bcsv_cli::formatBytes(file_size) << std::endl;
            std::cout << "Compression level: " << (int)reader.compressionLevel() << std::endl;
            std::cout << "Row count: " << reader.rowCount() << std::endl;
        }
        std::cout << std::endl;
        
        // Print column table using shared helper
        bcsv_cli::printLayoutSummary("Layout", layout, std::cout);
        
        reader.close();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}
