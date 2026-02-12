/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_macro_datasets.cpp
 * @brief Macro-benchmark: full write/read/validate cycles across dataset profiles
 * 
 * For each dataset profile, benchmarks:
 * - CSV baseline (fair visitConst-based write, real-parsing read)
 * - BCSV Flexible
 * - BCSV Flexible + ZoH
 * 
 * All modes perform full round-trip validation.
 * Results are emitted as JSON for the Python orchestrator.
 * 
 * Usage:
 *   bench_macro_datasets [options]
 *     --rows=N         Override default row count (0 = use profile default)
 *     --size=S|M|L|XL  Size preset: S=10K, M=100K, L=500K, XL=2M rows
 *     --output=PATH    Write JSON results to file (default: stdout summary)
 *     --profile=NAME   Run only this profile (default: all)
 *     --list           List available profiles and exit
 *     --quiet          Suppress progress output
 *     --no-cleanup     Keep temporary benchmark files
 *     --build-type=X   Tag results with build type (Debug/Release)
 *
 * --rows takes precedence over --size. Without either, profile defaults apply.
 */

#include "bench_common.hpp"
#include "bench_datasets.hpp"

#include <bcsv/bcsv.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ============================================================================
// Benchmark runners
// ============================================================================

/// Benchmark: CSV write/read with fair implementation using visitConst()
bench::BenchmarkResult benchmarkCSV(const bench::DatasetProfile& profile,
                                     size_t numRows, bool quiet)
{
    bench::BenchmarkResult result;
    result.dataset_name = profile.name;
    result.mode = "CSV";
    result.num_rows = numRows;
    result.num_columns = profile.layout.columnCount();

    const std::string filename = bench::tempFilePath(profile.name, ".csv");

    // ----- Write CSV -----
    bench::Timer timer;
    {
        std::ofstream ofs(filename);
        if (!ofs.is_open()) {
            result.validation_error = "Cannot open CSV file for writing: " + filename;
            return result;
        }

        bench::CsvWriter csvWriter(ofs);
        csvWriter.writeHeader(profile.layout);

        bcsv::Row row(profile.layout);
        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            profile.generate(row, i);
            csvWriter.writeRow(row);
        }
        ofs.flush();
        timer.stop();
    }
    result.write_time_ms = timer.elapsedMs();

    try {
        result.file_size = bench::validateFile(filename);
    } catch (const std::exception& e) {
        result.validation_error = e.what();
        return result;
    }

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] CSV write: " 
                  << std::fixed << std::setprecision(1) << result.write_time_ms << " ms\n";
    }

    // ----- Read CSV and validate -----
    bench::RoundTripValidator validator;
    bcsv::Row expectedRow(profile.layout);
    bcsv::Row readRow(profile.layout);
    bench::CsvReader csvReader;

    {
        std::ifstream ifs(filename);
        std::string line;
        std::getline(ifs, line); // skip header

        size_t rowsRead = 0;
        timer.start();
        while (std::getline(ifs, line)) {
            if (!csvReader.parseLine(line, profile.layout, readRow)) {
                result.validation_error = "CSV parse error at row " + std::to_string(rowsRead);
                timer.stop();
                result.read_time_ms = timer.elapsedMs();
                return result;
            }

            // Validate against expected data
            profile.generate(expectedRow, rowsRead);
            for (size_t c = 0; c < profile.layout.columnCount(); ++c) {
                validator.compareCell(rowsRead, c, expectedRow, readRow, profile.layout);
            }

            ++rowsRead;
            bench::doNotOptimize(readRow);
        }
        timer.stop();

        if (rowsRead != numRows) {
            result.validation_error = "Row count mismatch: expected " + std::to_string(numRows) 
                                    + " got " + std::to_string(rowsRead);
            result.read_time_ms = timer.elapsedMs();
            return result;
        }
    }
    result.read_time_ms = timer.elapsedMs();

    // Note: CSV string round-trip may lose precision for float/double.
    // We accept validation on integer and string types; float/double are
    // checked for exact match because we use sufficient precision.
    result.validation_passed = validator.passed();
    if (!validator.passed()) {
        result.validation_error = validator.summary();
    }

    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] CSV read:  " 
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }

    return result;
}

/// Benchmark: BCSV Flexible write/read with full validation
bench::BenchmarkResult benchmarkBCSVFlexible(const bench::DatasetProfile& profile,
                                              size_t numRows, bool quiet)
{
    bench::BenchmarkResult result;
    result.dataset_name = profile.name;
    result.mode = "BCSV Flexible";
    result.num_rows = numRows;
    result.num_columns = profile.layout.columnCount();

    const std::string filename = bench::tempFilePath(profile.name + "_flex", ".bcsv");

    // ----- Write -----
    bench::Timer timer;
    {
        bcsv::Writer<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filename, true, 1)) {
            result.validation_error = "Cannot open BCSV file: " + writer.getErrorMsg();
            return result;
        }

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            auto& row = writer.row();
            profile.generate(row, i);
            writer.writeRow();
        }
        writer.close();
        timer.stop();
    }
    result.write_time_ms = timer.elapsedMs();

    try {
        result.file_size = bench::validateFile(filename);
    } catch (const std::exception& e) {
        result.validation_error = e.what();
        return result;
    }

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV Flexible write: " 
                  << std::fixed << std::setprecision(1) << result.write_time_ms << " ms\n";
    }

    // ----- Read and validate -----
    bench::RoundTripValidator validator;
    bcsv::Row expectedRow(profile.layout);

    {
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(filename)) {
            result.validation_error = "Cannot read BCSV file: " + reader.getErrorMsg();
            return result;
        }

        size_t rowsRead = 0;
        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();

            // Validate every cell
            profile.generate(expectedRow, rowsRead);
            for (size_t c = 0; c < profile.layout.columnCount(); ++c) {
                validator.compareCell(rowsRead, c, expectedRow, row, profile.layout);
            }

            bench::doNotOptimize(row);
            ++rowsRead;
        }
        reader.close();
        timer.stop();

        if (rowsRead != numRows) {
            result.validation_error = "Row count mismatch: expected " + std::to_string(numRows) 
                                    + " got " + std::to_string(rowsRead);
            result.read_time_ms = timer.elapsedMs();
            return result;
        }
    }
    result.read_time_ms = timer.elapsedMs();

    result.validation_passed = validator.passed();
    if (!validator.passed()) {
        result.validation_error = validator.summary();
    }

    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV Flexible read:  " 
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }

    return result;
}

/// Benchmark: BCSV Flexible + ZoH write/read with full validation
bench::BenchmarkResult benchmarkBCSVFlexibleZoH(const bench::DatasetProfile& profile,
                                                  size_t numRows, bool quiet)
{
    bench::BenchmarkResult result;
    result.dataset_name = profile.name;
    result.mode = "BCSV Flexible ZoH";
    result.num_rows = numRows;
    result.num_columns = profile.layout.columnCount();

    const std::string filename = bench::tempFilePath(profile.name + "_flex_zoh", ".bcsv");

    // ----- Write (ZoH requires TrackingPolicy::Enabled) -----
    bench::Timer timer;
    {
        bcsv::Writer<bcsv::Layout, bcsv::TrackingPolicy::Enabled> writer(profile.layout);
        if (!writer.open(filename, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
            result.validation_error = "Cannot open BCSV ZoH file: " + writer.getErrorMsg();
            return result;
        }

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            auto& row = writer.row();
            profile.generateZoH(row, i);
            writer.writeRow();
        }
        writer.close();
        timer.stop();
    }
    result.write_time_ms = timer.elapsedMs();

    try {
        result.file_size = bench::validateFile(filename);
    } catch (const std::exception& e) {
        result.validation_error = e.what();
        return result;
    }

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV Flex ZoH write: " 
                  << std::fixed << std::setprecision(1) << result.write_time_ms << " ms\n";
    }

    // ----- Read and validate -----
    bench::RoundTripValidator validator;
    bcsv::RowTracking expectedRow(profile.layout);

    {
        bcsv::Reader<bcsv::Layout, bcsv::TrackingPolicy::Enabled> reader;
        if (!reader.open(filename)) {
            result.validation_error = "Cannot read BCSV ZoH file: " + reader.getErrorMsg();
            return result;
        }

        size_t rowsRead = 0;
        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();

            // Validate against ZoH-generated data
            profile.generateZoH(expectedRow, rowsRead);
            for (size_t c = 0; c < profile.layout.columnCount(); ++c) {
                validator.compareCell(rowsRead, c, expectedRow, row, profile.layout);
            }

            bench::doNotOptimize(row);
            ++rowsRead;
        }
        reader.close();
        timer.stop();

        if (rowsRead != numRows) {
            result.validation_error = "Row count mismatch: expected " + std::to_string(numRows) 
                                    + " got " + std::to_string(rowsRead);
            result.read_time_ms = timer.elapsedMs();
            return result;
        }
    }
    result.read_time_ms = timer.elapsedMs();

    result.validation_passed = validator.passed();
    if (!validator.passed()) {
        result.validation_error = validator.summary();
    }

    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV Flex ZoH read:  " 
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }

    return result;
}

/// Run all benchmarks for a single dataset profile
std::vector<bench::BenchmarkResult> benchmarkProfile(const bench::DatasetProfile& profile,
                                                      size_t numRows, bool quiet)
{
    std::vector<bench::BenchmarkResult> results;

    if (!quiet) {
        std::cerr << "\n=== Dataset: " << profile.name << " ===\n"
                  << "  " << profile.description << "\n"
                  << "  Rows: " << numRows 
                  << "  Columns: " << profile.layout.columnCount() << "\n\n";
    }

    // 1. CSV baseline (fair)
    auto csvResult = benchmarkCSV(profile, numRows, quiet);
    results.push_back(csvResult);

    // 2. BCSV Flexible
    auto flexResult = benchmarkBCSVFlexible(profile, numRows, quiet);
    // Compute compression ratio vs CSV
    if (csvResult.file_size > 0) {
        flexResult.compression_ratio = static_cast<double>(flexResult.file_size) / csvResult.file_size;
    }
    results.push_back(flexResult);

    // 3. BCSV Flexible + ZoH
    auto zohResult = benchmarkBCSVFlexibleZoH(profile, numRows, quiet);
    if (csvResult.file_size > 0) {
        zohResult.compression_ratio = static_cast<double>(zohResult.file_size) / csvResult.file_size;
    }
    results.push_back(zohResult);

    return results;
}

/// Clean up temporary benchmark files for a profile
void cleanupProfile(const bench::DatasetProfile& profile) {
    const std::vector<std::string> extensions = {".csv", ".bcsv"};
    const std::vector<std::string> suffixes = {"", "_flex", "_flex_zoh"};
    
    for (const auto& suffix : suffixes) {
        for (const auto& ext : extensions) {
            std::string path = bench::tempFilePath(profile.name + suffix, ext);
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    }
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    auto args = bench::parseArgs(argc, argv);

    // --list: print profile names and exit
    if (bench::hasArg(args, "list")) {
        for (const auto& name : bench::getProfileNames()) {
            std::cout << name << "\n";
        }
        return 0;
    }

    size_t rowOverride = bench::getArgSizeT(args, "rows", 0);
    std::string sizePreset = bench::getArgString(args, "size", "");

    // --size preset (overridden by explicit --rows)
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

    // Select profiles to run
    std::vector<bench::DatasetProfile> profiles;
    if (!profileFilter.empty()) {
        try {
            profiles.push_back(bench::getProfile(profileFilter));
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            std::cerr << "Available profiles: ";
            for (const auto& n : bench::getProfileNames()) std::cerr << n << " ";
            std::cerr << "\n";
            return 1;
        }
    } else {
        profiles = bench::getAllProfiles();
    }

    if (!quiet) {
        std::cerr << "BCSV Macro Benchmark Suite\n"
                  << "==========================\n"
                  << "Profiles: " << profiles.size() << "\n"
                  << "Rows: " << (rowOverride > 0 ? std::to_string(rowOverride) : "profile defaults") << "\n"
                  << "Build: " << buildType << "\n\n";
    }

    // Run benchmarks
    bench::Timer totalTimer;
    totalTimer.start();
    
    // Warmup: run the first profile at minimal row count to prime
    // filesystem caches, dynamic linker, and CPU branch predictors
    if (!profiles.empty()) {
        if (!quiet) {
            std::cerr << "Warmup: " << profiles.front().name << " (100 rows)...\n";
        }
        try {
            benchmarkProfile(profiles.front(), 100, /*quiet=*/true);
            cleanupProfile(profiles.front());
        } catch (...) {
            // Warmup failure is non-fatal
        }
        if (!quiet) {
            std::cerr << "Warmup complete.\n\n";
        }
    }

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

    // Print human-readable summary to stderr
    if (!quiet) {
        bench::printResultsTable(allResults);
        std::cerr << "Total time: " << std::fixed << std::setprecision(1) 
                  << totalTimer.elapsedSec() << " s\n\n";
    }

    // Write JSON output
    if (!outputPath.empty()) {
        auto platform = bench::PlatformInfo::gather(buildType);
        bench::writeResultsJson(outputPath, platform, allResults, totalTimer.elapsedSec());
        if (!quiet) {
            std::cerr << "Results written to: " << outputPath << "\n";
        }
    }

    // Check for validation failures
    bool allPassed = true;
    for (const auto& r : allResults) {
        if (!r.validation_passed && r.mode != "ERROR") {
            allPassed = false;
            std::cerr << "VALIDATION FAILED: " << r.dataset_name << " / " << r.mode << "\n";
            if (!r.validation_error.empty()) {
                std::cerr << "  " << r.validation_error << "\n";
            }
        }
    }

    return allPassed ? 0 : 1;
}
