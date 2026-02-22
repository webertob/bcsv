/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_external_csv.cpp
 * @brief External CSV library read benchmark — compares BCSV's CSV reader
 *        against vincentlaucsb/csv-parser (a well-known, high-performance
 *        memory-mapped CSV parser).
 * 
 * This benchmark generates CSV files from dataset profiles using BCSV's
 * CsvWriter, then times how long each parser takes to read and interpret
 * every cell value.  Both parsers convert values to native types (int,
 * double, string) — we compare actual parse throughput, not just I/O.
 * 
 * Usage:
 *   bench_external_csv [options]
 *     --rows=N          Override default row count
 *     --size=S|M|L|XL   Size preset
 *     --profile=NAME    Run only this profile (default: all)
 *     --output=PATH     Write JSON results to file
 *     --build-type=X    Tag results with build type
 *     --list            List available profiles and exit
 *     --quiet           Suppress progress output
 *     --no-cleanup      Keep temporary CSV files
 */

#include "bench_common.hpp"
#include "bench_datasets.hpp"

#include <bcsv/bcsv.h>
#include <csv.hpp>            // vincentlaucsb/csv-parser

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ============================================================================
// Read-only benchmark: BCSV's CsvReader
// ============================================================================

bench::BenchmarkResult benchmarkBcsvCsvRead(const std::string& csvFile,
                                             const bench::DatasetProfile& profile,
                                             size_t expectedRows, bool quiet)
{
    bench::BenchmarkResult result;
    result.dataset_name = profile.name;
    result.mode = "BCSV CsvReader";
    result.num_rows = expectedRows;
    result.num_columns = profile.layout.columnCount();

    try {
        result.file_size = bench::validateFile(csvFile);
    } catch (const std::exception& e) {
        result.validation_error = e.what();
        return result;
    }

    bench::CsvReader csvReader;
    bcsv::Row readRow(profile.layout);

    bench::Timer timer;
    std::ifstream ifs(csvFile);
    std::string line;
    std::getline(ifs, line); // skip header

    size_t rowsRead = 0;
    timer.start();
    while (std::getline(ifs, line)) {
        if (!csvReader.parseLine(line, profile.layout, readRow)) {
            result.validation_error = "Parse error at row " + std::to_string(rowsRead);
            break;
        }
        bench::doNotOptimize(readRow);
        ++rowsRead;
    }
    timer.stop();

    result.read_time_ms = timer.elapsedMs();
    result.num_rows = rowsRead;

    if (rowsRead == expectedRows) {
        result.validation_passed = true;
    } else {
        result.validation_error = "Row count mismatch: expected " + std::to_string(expectedRows)
                                + " got " + std::to_string(rowsRead);
    }

    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV CsvReader:     "
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << "  (" << rowsRead << " rows)\n";
    }

    return result;
}

// ============================================================================
// Read-only benchmark: vincentlaucsb/csv-parser (memory-mapped, typed access)
// ============================================================================

bench::BenchmarkResult benchmarkExternalCsvRead(const std::string& csvFile,
                                                 const bench::DatasetProfile& profile,
                                                 size_t expectedRows, bool quiet)
{
    bench::BenchmarkResult result;
    result.dataset_name = profile.name;
    result.mode = "External csv-parser";
    result.num_rows = expectedRows;
    result.num_columns = profile.layout.columnCount();

    try {
        result.file_size = bench::validateFile(csvFile);
    } catch (const std::exception& e) {
        result.validation_error = e.what();
        return result;
    }

    const size_t numCols = profile.layout.columnCount();

    bench::Timer timer;
    size_t rowsRead = 0;

    timer.start();
    {
        csv::CSVReader reader(csvFile);

        for (auto& row : reader) {
            // Parse every cell to a typed value — same work as BCSV CsvReader.
            // We use get_sv() + std::from_chars() for numeric types rather than
            // csv-parser's own get<T>() which has stricter overflow checks.
            // This is the fairest comparison: both parsers tokenize the CSV,
            // then both use from_chars/manual conversion for the final step.
            for (size_t c = 0; c < numCols; ++c) {
                auto sv = row[c].get_sv();
                const char* first = sv.data();
                const char* last  = sv.data() + sv.size();

                switch (profile.layout.columnType(c)) {
                    case bcsv::ColumnType::BOOL: {
                        bool val = (sv == "true" || sv == "1" || sv == "TRUE");
                        bench::doNotOptimize(val);
                        break;
                    }
                    case bcsv::ColumnType::INT8: {
                        int v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::INT16: {
                        int16_t v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::INT32: {
                        int32_t v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::INT64: {
                        int64_t v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::UINT8: {
                        unsigned v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::UINT16: {
                        uint16_t v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::UINT32: {
                        uint32_t v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::UINT64: {
                        uint64_t v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::FLOAT: {
                        float v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::DOUBLE: {
                        double v = 0;
                        std::from_chars(first, last, v);
                        bench::doNotOptimize(v);
                        break;
                    }
                    case bcsv::ColumnType::STRING: {
                        bench::doNotOptimize(sv);
                        break;
                    }
                    default:
                        break;
                }
            }
            ++rowsRead;
        }
    }
    timer.stop();

    result.read_time_ms = timer.elapsedMs();
    result.num_rows = rowsRead;

    if (rowsRead == expectedRows) {
        result.validation_passed = true;
    } else {
        result.validation_error = "Row count mismatch: expected " + std::to_string(expectedRows)
                                + " got " + std::to_string(rowsRead);
    }

    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] External csv-parser: "
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << "  (" << rowsRead << " rows)\n";
    }

    return result;
}

// ============================================================================
// Run both readers on one profile
// ============================================================================

std::vector<bench::BenchmarkResult> benchmarkProfile(const bench::DatasetProfile& profile,
                                                      size_t numRows, bool quiet)
{
    std::vector<bench::BenchmarkResult> results;

    if (!quiet) {
        std::cerr << "\n=== External CSV Comparison: " << profile.name << " ===\n"
                  << "  " << profile.description << "\n"
                  << "  Rows: " << numRows
                  << "  Columns: " << profile.layout.columnCount() << "\n\n";
    }

    // Generate the CSV file once
    const std::string csvFile = bench::tempFilePath("ext_" + profile.name, ".csv");
    {
        std::ofstream ofs(csvFile);
        if (!ofs.is_open()) {
            bench::BenchmarkResult err;
            err.dataset_name = profile.name;
            err.mode = "ERROR";
            err.validation_error = "Cannot create CSV file: " + csvFile;
            results.push_back(err);
            return results;
        }

        bench::CsvWriter csvWriter(ofs);
        csvWriter.writeHeader(profile.layout);

        bcsv::Row row(profile.layout);
        for (size_t i = 0; i < numRows; ++i) {
            profile.generate(row, i);
            csvWriter.writeRow(row);
        }
        ofs.flush();
    }

    if (!quiet) {
        auto sz = std::filesystem::file_size(csvFile);
        std::cerr << "  CSV file: " << std::fixed << std::setprecision(1)
                  << (sz / (1024.0 * 1024.0)) << " MB\n\n";
    }

    // 1. BCSV's CsvReader
    auto bcsvResult = benchmarkBcsvCsvRead(csvFile, profile, numRows, quiet);
    results.push_back(bcsvResult);

    // 2. External csv-parser (memory-mapped)
    auto extResult = benchmarkExternalCsvRead(csvFile, profile, numRows, quiet);
    results.push_back(extResult);

    // Compute speedup
    if (bcsvResult.read_time_ms > 0 && extResult.read_time_ms > 0) {
        double ratio = extResult.read_time_ms / bcsvResult.read_time_ms;
        if (!quiet) {
            std::cerr << "  Speedup (BCSV vs External): " << std::fixed
                      << std::setprecision(2) << ratio << "x\n";
            if (ratio > 1.0) {
                std::cerr << "  → BCSV CsvReader is faster\n";
            } else {
                std::cerr << "  → External csv-parser is faster\n";
            }
        }
    }

    return results;
}

void cleanupProfile(const bench::DatasetProfile& profile) {
    std::string path = bench::tempFilePath("ext_" + profile.name, ".csv");
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    auto args = bench::parseArgs(argc, argv);

    if (bench::hasArg(args, "list")) {
        for (const auto& name : bench::getProfileNames()) {
            std::cout << name << "\n";
        }
        return 0;
    }

    size_t rowOverride = bench::getArgSizeT(args, "rows", 0);
    std::string sizePreset = bench::getArgString(args, "size", "");

    if (rowOverride == 0 && !sizePreset.empty()) {
        if      (sizePreset == "S"  || sizePreset == "s")  rowOverride = 10000;
        else if (sizePreset == "M"  || sizePreset == "m")  rowOverride = 100000;
        else if (sizePreset == "L"  || sizePreset == "l")  rowOverride = 500000;
        else if (sizePreset == "XL" || sizePreset == "xl") rowOverride = 2000000;
        else {
            std::cerr << "ERROR: unknown --size=" << sizePreset
                      << " (expected S, M, L, or XL)\n";
            return 1;
        }
    }

    std::string outputPath = bench::getArgString(args, "output", "");
    std::string profileFilter = bench::getArgString(args, "profile", "");
    bool quiet = bench::hasArg(args, "quiet");
    bool noCleanup = bench::hasArg(args, "no-cleanup");
    std::string buildType = bench::getArgString(args, "build-type", "Release");

    std::vector<bench::DatasetProfile> profiles;
    if (!profileFilter.empty()) {
        try {
            profiles.push_back(bench::getProfile(profileFilter));
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            return 1;
        }
    } else {
        profiles = bench::getAllProfiles();
    }

    if (!quiet) {
        std::cerr << "BCSV External CSV Comparison Benchmark\n"
                  << "======================================\n"
                  << "Profiles: " << profiles.size() << "\n"
                  << "Rows: " << (rowOverride > 0 ? std::to_string(rowOverride) : "profile defaults") << "\n"
                  << "Build: " << buildType << "\n"
                  << "External: vincentlaucsb/csv-parser (memory-mapped)\n\n";
    }

    bench::Timer totalTimer;
    totalTimer.start();

    std::vector<bench::BenchmarkResult> allResults;

    for (auto& profile : profiles) {
        size_t numRows = (rowOverride > 0) ? rowOverride : profile.default_rows;

        try {
            auto results = benchmarkProfile(profile, numRows, quiet);
            allResults.insert(allResults.end(), results.begin(), results.end());
        } catch (const std::exception& e) {
            std::cerr << "ERROR in profile " << profile.name << ": " << e.what() << "\n";
            bench::BenchmarkResult errorResult;
            errorResult.dataset_name = profile.name;
            errorResult.mode = "ERROR";
            errorResult.validation_error = e.what();
            allResults.push_back(errorResult);
        }

        if (!noCleanup) {
            cleanupProfile(profile);
        }
    }

    totalTimer.stop();

    // Print summary table
    if (!quiet) {
        std::cerr << "\n\n";
        std::cerr << "+--------------------------+-------------------+------------+------------+\n";
        std::cerr << "| Profile                  | Parser            | Read (ms)  | Read MB/s  |\n";
        std::cerr << "+--------------------------+-------------------+------------+------------+\n";

        // Group by dataset
        for (size_t i = 0; i + 1 < allResults.size(); i += 2) {
            const auto& bcsv_r = allResults[i];
            const auto& ext_r  = allResults[i + 1];

            auto fmtRow = [&](const bench::BenchmarkResult& r) {
                std::cerr << "| " << std::left << std::setw(24) << r.dataset_name << " | "
                          << std::setw(17) << r.mode << " | "
                          << std::right << std::setw(10) << std::fixed << std::setprecision(1)
                          << r.read_time_ms << " | "
                          << std::setw(10) << std::setprecision(1)
                          << r.read_throughput_mb_per_sec << " |\n";
            };
            fmtRow(bcsv_r);
            fmtRow(ext_r);
            std::cerr << "+--------------------------+-------------------+------------+------------+\n";
        }

        std::cerr << "\nTotal time: " << std::fixed << std::setprecision(1)
                  << totalTimer.elapsedSec() << " s\n\n";
    }

    // Write JSON
    if (!outputPath.empty()) {
        auto platform = bench::PlatformInfo::gather(buildType);
        bench::writeResultsJson(outputPath, platform, allResults, totalTimer.elapsedSec());
        if (!quiet) {
            std::cerr << "Results written to: " << outputPath << "\n";
        }
    }

    return 0;
}
