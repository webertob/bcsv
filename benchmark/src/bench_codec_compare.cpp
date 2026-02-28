/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_codec_compare.cpp
 * @brief Focused codec comparison: all 5 file codecs + CSV baseline
 *
 * Runs write/read round-trips for each codec on representative datasets,
 * with interleaved iterations to neutralize thermal throttling.
 *
 * Codecs compared:
 *   1. CSV          (text baseline)
 *   2. PacketRaw    (compression=0, no flags)
 *   3. PacketLZ4    (compression=1, no flags)        [default]
 *   4. StreamRaw    (compression=0, STREAM_MODE)
 *   5. StreamLZ4    (compression=1, STREAM_MODE)
 *   6. BatchLZ4     (compression=1, BATCH_COMPRESS)  [async double-buffered]
 *
 * Usage:
 *   bench_codec_compare [--rows=N] [--iterations=N] [--profile=NAME]
 */

#include "bench_common.hpp"
#include "bench_datasets.hpp"

#include <bcsv/bcsv.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

// ============================================================================
// Codec descriptor
// ============================================================================

struct CodecConfig {
    std::string label;
    size_t      compressionLevel;
    bcsv::FileFlags flags;
    bool        isCsv;       // true = use CsvWriter/CsvReader instead of BCSV
};

std::vector<CodecConfig> buildCodecConfigs() {
    std::vector<CodecConfig> configs;
    configs.push_back({"CSV",         0, bcsv::FileFlags::NONE,           true });
    configs.push_back({"PacketRaw",   0, bcsv::FileFlags::NONE,           false});
    configs.push_back({"PacketLZ4",   1, bcsv::FileFlags::NONE,           false});
    configs.push_back({"StreamRaw",   0, bcsv::FileFlags::STREAM_MODE,    false});
    configs.push_back({"StreamLZ4",   1, bcsv::FileFlags::STREAM_MODE,    false});
#ifdef BCSV_HAS_BATCH_CODEC
    configs.push_back({"BatchLZ4",    1, bcsv::FileFlags::BATCH_COMPRESS, false});
#endif
    return configs;
}

// ============================================================================
// Single-iteration result
// ============================================================================

struct IterResult {
    double write_ms  = 0;
    double read_ms   = 0;
    size_t file_size = 0;
    bool   valid     = false;
};

// ============================================================================
// Run one write/read cycle for a BCSV codec
// ============================================================================

IterResult runBcsv(const bench::DatasetProfile& profile,
                   size_t numRows,
                   const CodecConfig& codec,
                   const std::string& filePath)
{
    IterResult r;
    bench::Timer timer;

    // ----- Write -----
    {
        bcsv::Writer<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filePath, true, codec.compressionLevel, 64, codec.flags)) {
            std::cerr << "  ERROR: open failed for " << codec.label << ": "
                      << writer.getErrorMsg() << "\n";
            return r;
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
    r.write_ms = timer.elapsedMs();
    r.file_size = std::filesystem::file_size(filePath);

    // ----- Read & validate -----
    {
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(filePath)) {
            std::cerr << "  ERROR: read open failed for " << codec.label << "\n";
            return r;
        }

        bcsv::Row expected(profile.layout);
        size_t rowsRead = 0;
        bool mismatch = false;

        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();
            profile.generate(expected, rowsRead);

            // spot-check first and every 1000th row
            if (rowsRead == 0 || rowsRead % 1000 == 0) {
                for (size_t c = 0; c < profile.layout.columnCount(); ++c) {
                    if (profile.layout.columnType(c) == bcsv::ColumnType::STRING) {
                        if (expected.get<std::string>(c) != row.get<std::string>(c)) {
                            mismatch = true;
                        }
                    }
                }
            }

            bench::doNotOptimize(row);
            ++rowsRead;
        }
        reader.close();
        timer.stop();

        r.read_ms = timer.elapsedMs();
        r.valid = (rowsRead == numRows && !mismatch);
    }

    return r;
}

// ============================================================================
// Run one write/read cycle for CSV baseline
// ============================================================================

IterResult runCsv(const bench::DatasetProfile& profile,
                  size_t numRows,
                  const std::string& filePath)
{
    IterResult r;
    bench::Timer timer;

    // ----- Write -----
    {
        std::ofstream ofs(filePath);
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
    r.write_ms = timer.elapsedMs();
    r.file_size = std::filesystem::file_size(filePath);

    // ----- Read -----
    {
        std::ifstream ifs(filePath);
        std::string line;
        std::getline(ifs, line); // skip header

        bcsv::Row row(profile.layout);
        bench::CsvReader csvReader;
        size_t rowsRead = 0;

        timer.start();
        while (std::getline(ifs, line)) {
            csvReader.parseLine(line, profile.layout, row);
            bench::doNotOptimize(row);
            ++rowsRead;
        }
        timer.stop();

        r.read_ms = timer.elapsedMs();
        r.valid = (rowsRead == numRows);
    }

    return r;
}

// ============================================================================
// Statistics helpers
// ============================================================================

double median(std::vector<double>& v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2 == 0) ? (v[n/2 - 1] + v[n/2]) / 2.0 : v[n/2];
}

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

double stdev(const std::vector<double>& v) {
    if (v.size() < 2) return 0;
    double m = mean(v);
    double sum = 0;
    for (auto x : v) sum += (x - m) * (x - m);
    return std::sqrt(sum / (v.size() - 1));
}

// ============================================================================
// CLI argument parsing
// ============================================================================

std::string getArg(const std::vector<std::string>& args, const std::string& key, const std::string& def) {
    for (const auto& a : args) {
        if (a.rfind("--" + key + "=", 0) == 0) {
            return a.substr(key.size() + 3);
        }
    }
    return def;
}

bool hasFlag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& a : args) {
        if (a == "--" + flag) return true;
    }
    return false;
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (hasFlag(args, "help")) {
        std::cout << "bench_codec_compare — Focused codec comparison benchmark\n\n"
                  << "Usage: bench_codec_compare [options]\n"
                  << "  --rows=N         Number of rows (default: 10000)\n"
                  << "  --iterations=N   Number of interleaved iterations (default: 5)\n"
                  << "  --profile=NAME   Dataset profile (default: mixed_generic)\n"
                  << "  --help           Show this help\n";
        return 0;
    }

    const size_t numRows    = std::stoull(getArg(args, "rows", "10000"));
    const size_t iterations = std::stoull(getArg(args, "iterations", "5"));
    const std::string profileName = getArg(args, "profile", "mixed_generic");

    // Resolve profile
    const auto& allProfiles = bench::getAllProfiles();
    const bench::DatasetProfile* profile = nullptr;
    for (const auto& p : allProfiles) {
        if (p.name == profileName) { profile = &p; break; }
    }
    if (!profile) {
        std::cerr << "ERROR: Unknown profile '" << profileName << "'\n"
                  << "Available: ";
        for (const auto& p : allProfiles) std::cerr << p.name << " ";
        std::cerr << "\n";
        return 1;
    }

    auto codecs = buildCodecConfigs();
    const size_t numCodecs = codecs.size();

    std::cerr << "=== Codec Comparison Benchmark ===\n"
              << "  Profile:    " << profile->name << " (" << profile->layout.columnCount() << " cols)\n"
              << "  Rows:       " << numRows << "\n"
              << "  Iterations: " << iterations << " (interleaved)\n"
              << "  Codecs:     " << numCodecs << "\n\n";

    // Storage: [codec_index][iteration]
    std::vector<std::vector<IterResult>> results(numCodecs, std::vector<IterResult>(iterations));

    // ----- Interleaved iterations -----
    for (size_t iter = 0; iter < iterations; ++iter) {
        std::cerr << "--- Iteration " << (iter + 1) << "/" << iterations << " ---\n";

        for (size_t ci = 0; ci < numCodecs; ++ci) {
            const auto& codec = codecs[ci];
            std::string ext = codec.isCsv ? ".csv" : ".bcsv";
            std::string filePath = bench::tempFilePath(
                profile->name + "_codec_" + codec.label, ext);

            IterResult r;
            if (codec.isCsv) {
                r = runCsv(*profile, numRows, filePath);
            } else {
                r = runBcsv(*profile, numRows, codec, filePath);
            }

            results[ci][iter] = r;

            std::cerr << "  " << std::left << std::setw(12) << codec.label
                      << std::right
                      << "  write=" << std::fixed << std::setprecision(1) << std::setw(8) << r.write_ms << " ms"
                      << "  read=" << std::setw(8) << r.read_ms << " ms"
                      << "  size=" << std::setw(10) << r.file_size
                      << (r.valid ? "  OK" : "  FAIL") << "\n";

            // Clean up
            std::filesystem::remove(filePath);
        }
    }

    // ============================================================================
    // Aggregate & report
    // ============================================================================

    // Find CSV file size for compression ratio
    size_t csvFileSize = 0;
    for (size_t ci = 0; ci < numCodecs; ++ci) {
        if (codecs[ci].isCsv) {
            // Use median file size
            std::vector<double> sizes;
            for (const auto& r : results[ci]) sizes.push_back(static_cast<double>(r.file_size));
            csvFileSize = static_cast<size_t>(median(sizes));
            break;
        }
    }

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  CODEC COMPARISON — " << std::left << std::setw(20) << profile->name
              << " — " << numRows << " rows × " << profile->layout.columnCount() << " cols"
              << " — " << iterations << " iterations" << std::setw(15) << "" << "║\n";
    std::cout << "╠══════════════╦══════════════════════════════╦══════════════════════════════╦═════════════╦═══════════════════╣\n";
    std::cout << "║ Codec        ║  Write (ms)                  ║  Read (ms)                   ║  File Size  ║ Compression Ratio ║\n";
    std::cout << "║              ║  median    mean    stdev     ║  median    mean    stdev      ║  (bytes)    ║ vs CSV            ║\n";
    std::cout << "╠══════════════╬══════════════════════════════╬══════════════════════════════╬═════════════╬═══════════════════╣\n";

    for (size_t ci = 0; ci < numCodecs; ++ci) {
        std::vector<double> writeTimes, readTimes;
        size_t fileSize = 0;
        bool allValid = true;

        for (const auto& r : results[ci]) {
            writeTimes.push_back(r.write_ms);
            readTimes.push_back(r.read_ms);
            fileSize = r.file_size;
            if (!r.valid) allValid = false;
        }

        double wMed = median(writeTimes);
        double wMean = mean(writeTimes);
        double wStd = stdev(writeTimes);
        double rMed = median(readTimes);
        double rMean = mean(readTimes);
        double rStd = stdev(readTimes);

        double ratio = (csvFileSize > 0) ? static_cast<double>(fileSize) / csvFileSize : 0;

        std::cout << "║ " << std::left << std::setw(13) << codecs[ci].label
                  << "║ " << std::right << std::fixed
                  << std::setprecision(1) << std::setw(7) << wMed
                  << std::setprecision(1) << std::setw(8) << wMean
                  << std::setprecision(1) << std::setw(9) << wStd << "     "
                  << "║ " << std::setprecision(1) << std::setw(7) << rMed
                  << std::setprecision(1) << std::setw(8) << rMean
                  << std::setprecision(1) << std::setw(9) << rStd << "      "
                  << "║ " << std::setw(10) << fileSize << "  "
                  << "║ " << std::setprecision(4) << std::setw(7) << ratio
                  << (allValid ? "  OK       " : "  FAIL     ")
                  << "║\n";
    }

    std::cout << "╠══════════════╩══════════════════════════════╩══════════════════════════════╩═════════════╩═══════════════════╣\n";

    // Throughput summary
    std::cout << "║                                                                                                             ║\n";
    std::cout << "║  THROUGHPUT (median, rows/sec)                                                                              ║\n";
    std::cout << "╠══════════════╦═══════════════════╦═══════════════════╦══════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Codec        ║  Write rows/s     ║  Read rows/s      ║  Speedup vs CSV (write / read)                         ║\n";
    std::cout << "╠══════════════╬═══════════════════╬═══════════════════╬══════════════════════════════════════════════════════════╣\n";

    double csvWriteRowsPerSec = 0, csvReadRowsPerSec = 0;
    // Compute CSV baseline throughput first
    for (size_t ci = 0; ci < numCodecs; ++ci) {
        if (codecs[ci].isCsv) {
            std::vector<double> writeTimes, readTimes;
            for (const auto& r : results[ci]) {
                writeTimes.push_back(r.write_ms);
                readTimes.push_back(r.read_ms);
            }
            double wMed = median(writeTimes);
            double rMed = median(readTimes);
            csvWriteRowsPerSec = (wMed > 0) ? numRows / (wMed / 1000.0) : 0;
            csvReadRowsPerSec  = (rMed > 0) ? numRows / (rMed / 1000.0) : 0;
            break;
        }
    }

    for (size_t ci = 0; ci < numCodecs; ++ci) {
        std::vector<double> writeTimes, readTimes;
        for (const auto& r : results[ci]) {
            writeTimes.push_back(r.write_ms);
            readTimes.push_back(r.read_ms);
        }
        double wMed = median(writeTimes);
        double rMed = median(readTimes);
        double wRps = (wMed > 0) ? numRows / (wMed / 1000.0) : 0;
        double rRps = (rMed > 0) ? numRows / (rMed / 1000.0) : 0;

        double wSpeedup = (csvWriteRowsPerSec > 0) ? wRps / csvWriteRowsPerSec : 0;
        double rSpeedup = (csvReadRowsPerSec > 0)  ? rRps / csvReadRowsPerSec  : 0;

        std::ostringstream speedup;
        if (codecs[ci].isCsv) {
            speedup << "  (baseline)";
        } else {
            speedup << "  " << std::fixed << std::setprecision(2)
                    << wSpeedup << "x / " << rSpeedup << "x";
        }

        std::cout << "║ " << std::left << std::setw(13) << codecs[ci].label
                  << "║ " << std::right << std::setw(14) << static_cast<size_t>(wRps) << "    "
                  << "║ " << std::setw(14) << static_cast<size_t>(rRps) << "    "
                  << "║ " << std::left << std::setw(55) << speedup.str()
                  << "║\n";
    }

    std::cout << "╚══════════════╩═══════════════════╩═══════════════════╩══════════════════════════════════════════════════════════╝\n";

    // Validation summary
    bool allOk = true;
    for (size_t ci = 0; ci < numCodecs; ++ci) {
        for (const auto& r : results[ci]) {
            if (!r.valid) { allOk = false; break; }
        }
    }
    std::cout << "\nValidation: " << (allOk ? "ALL PASSED" : "SOME FAILED") << "\n";

    return allOk ? 0 : 1;
}
