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
#include <cstdint>
#include <bcsv/bcsv.h>
#include "cli_app.h"

struct Config {
    std::string input_file;
    size_t num_rows = 10;
    char delimiter = ',';
    bool include_header = true;
    bool verbose = false;
};

int main(int argc, char* argv[]) {
    Config config;

    CLI::App app{"Display the first few rows of a BCSV file in CSV format.", "bcsvHead"};
    argv = app.ensure_utf8(argv);
    bcsv_cli::setupVersionFlag(app, bcsv_cli::programName(argv[0]));

    app.add_option("INPUT_FILE", config.input_file, "Input BCSV file path")
        ->required();
    app.add_option("-n,--lines", config.num_rows, "Number of rows to display")
        ->check(CLI::PositiveNumber)
        ->capture_default_str();
    app.add_option("-d,--delimiter", config.delimiter, "Field delimiter")
        ->capture_default_str();
    bool no_header = false;
    app.add_flag("--no-header", no_header, "Don't include header row in output");
    app.add_flag("-v,--verbose", config.verbose, "Enable verbose output");

    app.footer(
        "Examples:\n"
        "  bcsvHead data.bcsv\n"
        "  bcsvHead -n 20 data.bcsv\n"
        "  bcsvHead --no-header data.bcsv\n"
        "  bcsvHead -d ';' data.bcsv\n"
        "  bcsvHead data.bcsv | grep \"pattern\"");

    CLI11_PARSE(app, argc, argv);
    config.include_header = !no_header;

    try {
        if (config.verbose) {
            std::cerr << "Reading: " << config.input_file << std::endl;
            std::cerr << "Lines: " << config.num_rows << std::endl;
            std::cerr << "Include header: " << (config.include_header ? "yes" : "no") << std::endl;
            std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
        }

        // Open BCSV file
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(config.input_file)) {
            std::cerr << "Error: Cannot open BCSV file: " << config.input_file << "\n"
                      << "  " << reader.getErrorMsg() << std::endl;
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
