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
#include <bcsv/bcsv.h>
#include "cli_app.h"

struct Config {
    std::string input_file;
    bool verbose = false;
};

int main(int argc, char* argv[]) {
    Config config;

    CLI::App app{"Display the header structure of a BCSV file.", "bcsvHeader"};
    argv = app.ensure_utf8(argv);
    bcsv_cli::setupVersionFlag(app, bcsv_cli::programName(argv[0]));

    app.add_option("INPUT_FILE", config.input_file, "Input BCSV file path")
        ->required();
    app.add_flag("-v,--verbose", config.verbose,
                 "Verbose output (row count, file size, compression level)");

    app.footer(
        "Output Format:\n"
        "  Columns in vertical format: index (0-based), name, type.\n\n"
        "Examples:\n"
        "  bcsvHeader data.bcsv\n"
        "  bcsvHeader -v data.bcsv");

    CLI11_PARSE(app, argc, argv);

    try {
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
                std::cerr << "Error: Cannot open BCSV file: " << config.input_file << "\n"
                          << "  " << plain_reader.getErrorMsg() << std::endl;
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
                auto codecs = bcsv_cli::codecNamesFromFlags(
                    plain_reader.fileFlags(), plain_reader.compressionLevel());
                std::cout << "Row codec: " << codecs.row_codec << std::endl;
                std::cout << "File codec: " << codecs.file_codec << std::endl;
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
            auto codecs = bcsv_cli::codecNamesFromFlags(
                reader.fileFlags(), reader.compressionLevel());
            std::cout << "Row codec: " << codecs.row_codec << std::endl;
            std::cout << "File codec: " << codecs.file_codec << std::endl;
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
