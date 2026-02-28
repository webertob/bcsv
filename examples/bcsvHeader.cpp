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
 * This tool reads a BCSV file and prints its column structure in a vertical format,
 * showing column index, name, and type for quick file structure overview.
 */

#include <iostream>
#include <string>
#include <filesystem>
#include <iomanip>
#include <bcsv/bcsv.h>

struct Config {
    std::string input_file;
    bool verbose = false;
    bool help = false;
};

/**
 * @brief Converts a ColumnType enum to human-readable string
 * @param type The column type to convert
 * @return String representation of the column type
 */
std::string columnTypeToString(bcsv::ColumnType type) {
    switch (type) {
        case bcsv::ColumnType::BOOL:    return "bool";
        case bcsv::ColumnType::INT8:    return "int8";
        case bcsv::ColumnType::UINT8:   return "uint8";
        case bcsv::ColumnType::INT16:   return "int16";
        case bcsv::ColumnType::UINT16:  return "uint16";
        case bcsv::ColumnType::INT32:   return "int32";
        case bcsv::ColumnType::UINT32:  return "uint32";
        case bcsv::ColumnType::INT64:   return "int64";
        case bcsv::ColumnType::UINT64:  return "uint64";
        case bcsv::ColumnType::FLOAT:   return "float";
        case bcsv::ColumnType::DOUBLE:  return "double";
        case bcsv::ColumnType::STRING:  return "string";
        default:                        return "unknown";
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE\n\n";
    std::cout << "Display the header structure of a BCSV file.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input BCSV file path\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
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
        } else if (arg.substr(0, 1) == "-") {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            exit(1);
        } else {
            if (config.input_file.empty()) {
                config.input_file = arg;
            } else {
                std::cerr << "Error: Too many arguments. Only one input file expected." << std::endl;
                exit(1);
            }
        }
    }
    
    if (config.input_file.empty() && !config.help) {
        std::cerr << "Error: Input file is required" << std::endl;
        exit(1);
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
            std::cerr << "Reading header from: " << config.input_file << std::endl;
        }
        
        // Check if file exists
        if (!std::filesystem::exists(config.input_file)) {
            std::cerr << "Error: File does not exist: " << config.input_file << std::endl;
            return 1;
        }
        
        // Open BCSV file
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(config.input_file)) {
            std::cerr << "Error: Cannot open BCSV file: " << config.input_file << std::endl;
            return 1;
        }
        
        if (config.verbose) {
            std::cerr << "Opened BCSV file successfully" << std::endl;
        }
        
        const auto& layout = reader.layout();
        const size_t column_count = layout.columnCount();
        
        if (config.verbose) {
            std::cerr << "Layout contains " << column_count << " columns" << std::endl;
        }
        
        // Print header information
        std::cout << "BCSV Header Structure: " << config.input_file << std::endl;
        std::cout << "Columns: " << column_count << std::endl;
        std::cout << std::endl;
        
        if (column_count == 0) {
            std::cout << "No columns found in file." << std::endl;
        } else {
            // Calculate column widths for aligned output
            size_t max_index_width = std::max(std::to_string(column_count - 1).length(), (size_t)5);
            size_t max_name_width = 4; // Minimum width for "Name" header
            
            for (size_t i = 0; i < column_count; ++i) {
                max_name_width = std::max(max_name_width, layout.columnName(i).length());
            }
            
            // Print header
            std::cout << std::left 
                      << std::setw(max_index_width) << "Index" << "  "
                      << std::setw(max_name_width) << "Name" << "  "
                      << "Type" << std::endl;
            
            // Print separator line
            std::cout << std::string(max_index_width, '-') << "  "
                      << std::string(max_name_width, '-') << "  "
                      << std::string(6, '-') << std::endl;
            
            // Print column information
            for (size_t i = 0; i < column_count; ++i) {
                std::cout << std::left 
                          << std::setw(max_index_width) << i << "  "
                          << std::setw(max_name_width) << layout.columnName(i) << "  "
                          << columnTypeToString(layout.columnType(i)) << std::endl;
            }
        }
        
        reader.close();
        
        if (config.verbose) {
            std::cerr << "Successfully displayed header structure" << std::endl;
        }
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}