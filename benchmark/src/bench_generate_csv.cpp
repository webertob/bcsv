/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_generate_csv.cpp
 * @brief Utility to produce reference CSV files from benchmark profiles
 * 
 * Used by the benchmark orchestrator to drive CLI-tool benchmarks:
 *   bench_generate_csv → ref.csv → csv2bcsv → file.bcsv → bcsv2csv → rt.csv
 * 
 * Usage:
 *   bench_generate_csv --profile=NAME --rows=N --output=FILE
 *   bench_generate_csv --list
 */

#include "bench_common.hpp"
#include "bench_datasets.hpp"

#include <bcsv/bcsv.h>

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    auto args = bench::parseArgs(argc, argv);

    if (bench::hasArg(args, "list")) {
        for (const auto& name : bench::getProfileNames()) {
            std::cout << name << "\n";
        }
        return 0;
    }

    std::string profileName = bench::getArgString(args, "profile", "");
    size_t numRows = bench::getArgSizeT(args, "rows", 0);
    std::string outputPath = bench::getArgString(args, "output", "");

    if (profileName.empty() || outputPath.empty()) {
        std::cerr << "Usage: bench_generate_csv --profile=NAME --rows=N --output=FILE\n"
                  << "       bench_generate_csv --list\n";
        return 1;
    }

    bench::DatasetProfile profile;
    try {
        profile = bench::getProfile(profileName);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    if (numRows == 0) numRows = profile.default_rows;

    // Write CSV using the same fair CsvWriter as the benchmarks
    {
        std::ofstream ofs(outputPath);
        if (!ofs.is_open()) {
            std::cerr << "ERROR: Cannot open output file: " << outputPath << "\n";
            return 1;
        }

        bench::CsvWriter writer(ofs);
        writer.writeHeader(profile.layout);

        bcsv::Row row(profile.layout);
        for (size_t i = 0; i < numRows; ++i) {
            profile.generate(row, i);
            writer.writeRow(row);
        }
        ofs.flush();
    }

    std::cerr << "Generated " << numRows << " rows (" << profile.layout.columnCount()
              << " cols) to " << outputPath << "\n";
    return 0;
}
