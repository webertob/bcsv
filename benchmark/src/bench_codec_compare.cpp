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
 * @brief Comprehensive codec comparison: all 5 file codecs × {Flat,ZoH} + CSV baseline
 *
 * Runs write/read round-trips for every codec × row-codec combination on all
 * dataset profiles, with interleaved iterations to neutralize thermal throttling.
 *
 * Candidates (12 total):
 *   File codecs:   CSV, PacketRaw, PacketLZ4, StreamRaw, StreamLZ4, BatchLZ4
 *   Row codecs:    Flat (Writer<Layout>), ZoH (WriterZoH<Layout>)
 *   CSV only runs with Flat (ZoH is a binary-only concept).
 *
 * Usage:
 *   bench_codec_compare [--rows=N] [--iterations=N] [--profile=NAME|all] [--json=PATH]
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
// Candidate descriptor  (file-codec × row-codec)
// ============================================================================

struct Candidate {
    std::string     label;
    size_t          compressionLevel;
    bcsv::FileFlags flags;
    bool            isCsv;
    bool            useZoH;          // true → WriterZoH + generateZoH
};

std::vector<Candidate> buildCandidates() {
    std::vector<Candidate> c;
    // --- Flat (dense) row codec ---
    c.push_back({"CSV",              0, bcsv::FileFlags::NONE,        true,  false});
    c.push_back({"PktRaw",           0, bcsv::FileFlags::NONE,        false, false});
    c.push_back({"PktLZ4",           1, bcsv::FileFlags::NONE,        false, false});
    c.push_back({"StrmRaw",          0, bcsv::FileFlags::STREAM_MODE, false, false});
    c.push_back({"StrmLZ4",          1, bcsv::FileFlags::STREAM_MODE, false, false});
#ifdef BCSV_HAS_BATCH_CODEC
    c.push_back({"BatchLZ4",         1, bcsv::FileFlags::BATCH_COMPRESS, false, false});
#endif
    // --- ZoH row codec (ZERO_ORDER_HOLD flag ORed in) ---
    c.push_back({"PktRaw+ZoH",       0, bcsv::FileFlags::ZERO_ORDER_HOLD,        false, true});
    c.push_back({"PktLZ4+ZoH",       1, bcsv::FileFlags::ZERO_ORDER_HOLD,        false, true});
    c.push_back({"StrmRaw+ZoH",      0, bcsv::FileFlags::STREAM_MODE | bcsv::FileFlags::ZERO_ORDER_HOLD, false, true});
    c.push_back({"StrmLZ4+ZoH",      1, bcsv::FileFlags::STREAM_MODE | bcsv::FileFlags::ZERO_ORDER_HOLD, false, true});
#ifdef BCSV_HAS_BATCH_CODEC
    c.push_back({"BatchLZ4+ZoH",     1, bcsv::FileFlags::BATCH_COMPRESS | bcsv::FileFlags::ZERO_ORDER_HOLD, false, true});
#endif
    return c;
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
// Run one write/read cycle for a BCSV candidate (flat or ZoH)
// ============================================================================

IterResult runBcsv(const bench::DatasetProfile& profile,
                   size_t numRows,
                   const Candidate& cand,
                   const std::string& filePath)
{
    IterResult r;
    bench::Timer timer;

    // ----- Write -----
    if (cand.useZoH) {
        bcsv::WriterZoH<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filePath, true, cand.compressionLevel, 64, cand.flags)) {
            std::cerr << "  ERROR: open failed for " << cand.label << ": "
                      << writer.getErrorMsg() << "\n";
            return r;
        }
        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            auto& row = writer.row();
            profile.generateZoH(row, i);
            writer.writeRow();
        }
        writer.close();
        timer.stop();
    } else {
        bcsv::Writer<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filePath, true, cand.compressionLevel, 64, cand.flags)) {
            std::cerr << "  ERROR: open failed for " << cand.label << ": "
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
            std::cerr << "  ERROR: read open failed for " << cand.label << "\n";
            return r;
        }

        bcsv::Row expected(profile.layout);
        size_t rowsRead = 0;
        bool mismatch = false;

        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();
            if (cand.useZoH)
                profile.generateZoH(expected, rowsRead);
            else
                profile.generate(expected, rowsRead);

            // spot-check first and every 1000th row
            if (rowsRead == 0 || rowsRead % 1000 == 0) {
                for (size_t c = 0; c < profile.layout.columnCount(); ++c) {
                    if (profile.layout.columnType(c) == bcsv::ColumnType::STRING) {
                        if (expected.get<std::string>(c) != row.get<std::string>(c))
                            mismatch = true;
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

    // ----- Write CSV using library CsvWriter -----
    {
        bcsv::CsvWriter<bcsv::Layout> csvWriter(profile.layout);
        csvWriter.open(filePath, true);  // writes header automatically

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            profile.generate(csvWriter.row(), i);
            csvWriter.writeRow();
        }
        csvWriter.close();
        timer.stop();
    }
    r.write_ms = timer.elapsedMs();
    r.file_size = std::filesystem::file_size(filePath);

    // ----- Read CSV using library CsvReader -----
    {
        bcsv::CsvReader<bcsv::Layout> csvReader(profile.layout);
        csvReader.open(filePath);
        size_t rowsRead = 0;

        timer.start();
        while (csvReader.readNext()) {
            bench::doNotOptimize(csvReader.row());
            ++rowsRead;
        }
        timer.stop();
        csvReader.close();

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

[[maybe_unused]]
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
        if (a.rfind("--" + key + "=", 0) == 0)
            return a.substr(key.size() + 3);
    }
    return def;
}

bool hasFlag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& a : args) {
        if (a == "--" + flag) return true;
    }
    return false;
}

// ============================================================================
// Per-profile result row (for aggregate reporting)
// ============================================================================

struct ProfileResult {
    std::string profileName;
    size_t      numCols;
    std::string candidateLabel;
    double      medianWriteMs;
    double      medianReadMs;
    size_t      fileSize;
    double      ratioVsCsv;       // file_size / csv_file_size
    double      writeRowsPerSec;
    double      readRowsPerSec;
    bool        allValid;
};

// ============================================================================
// Run all candidates on one profile, return ProfileResults
// ============================================================================

std::vector<ProfileResult> runProfile(
    const bench::DatasetProfile& profile,
    size_t numRows,
    size_t iterations,
    const std::vector<Candidate>& candidates,
    bool quiet)
{
    const size_t numCands = candidates.size();

    // results[cand][iter]
    std::vector<std::vector<IterResult>> results(numCands, std::vector<IterResult>(iterations));

    for (size_t iter = 0; iter < iterations; ++iter) {
        if (!quiet)
            std::cerr << "  iter " << (iter + 1) << "/" << iterations;

        for (size_t ci = 0; ci < numCands; ++ci) {
            const auto& cand = candidates[ci];
            std::string ext = cand.isCsv ? ".csv" : ".bcsv";
            std::string filePath = bench::tempFilePath(
                profile.name + "_c_" + cand.label, ext);

            IterResult r;
            if (cand.isCsv)
                r = runCsv(profile, numRows, filePath);
            else
                r = runBcsv(profile, numRows, cand, filePath);

            results[ci][iter] = r;
            std::filesystem::remove(filePath);
        }
        if (!quiet) std::cerr << "  done\n";
    }

    // Find CSV file size for ratio
    size_t csvFileSize = 0;
    for (size_t ci = 0; ci < numCands; ++ci) {
        if (candidates[ci].isCsv && !candidates[ci].useZoH) {
            std::vector<double> sizes;
            for (auto& r : results[ci]) sizes.push_back(static_cast<double>(r.file_size));
            csvFileSize = static_cast<size_t>(median(sizes));
            break;
        }
    }

    std::vector<ProfileResult> out;
    for (size_t ci = 0; ci < numCands; ++ci) {
        std::vector<double> wt, rt;
        size_t fsize = 0;
        bool allValid = true;
        for (auto& r : results[ci]) {
            wt.push_back(r.write_ms);
            rt.push_back(r.read_ms);
            fsize = r.file_size;
            if (!r.valid) allValid = false;
        }
        double wMed = median(wt);
        double rMed = median(rt);
        ProfileResult pr;
        pr.profileName     = profile.name;
        pr.numCols         = profile.layout.columnCount();
        pr.candidateLabel  = candidates[ci].label;
        pr.medianWriteMs   = wMed;
        pr.medianReadMs    = rMed;
        pr.fileSize        = fsize;
        pr.ratioVsCsv      = (csvFileSize > 0) ? static_cast<double>(fsize) / csvFileSize : 0;
        pr.writeRowsPerSec = (wMed > 0) ? numRows / (wMed / 1000.0) : 0;
        pr.readRowsPerSec  = (rMed > 0) ? numRows / (rMed / 1000.0) : 0;
        pr.allValid        = allValid;
        out.push_back(pr);
    }
    return out;
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (hasFlag(args, "help")) {
        std::cout << "bench_codec_compare — Comprehensive codec×row-codec comparison\n\n"
                  << "Usage: bench_codec_compare [options]\n"
                  << "  --rows=N          Number of rows (default: 10000)\n"
                  << "  --iterations=N    Number of interleaved iterations (default: 5)\n"
                  << "  --profile=NAME    Dataset profile, or 'all' (default: all)\n"
                  << "  --json=PATH       Write JSON results to file\n"
                  << "  --quiet           Suppress per-iteration progress\n"
                  << "  --help            Show this help\n";
        return 0;
    }

    const size_t numRows    = std::stoull(getArg(args, "rows", "10000"));
    const size_t iterations = std::stoull(getArg(args, "iterations", "5"));
    const std::string profileFilter = getArg(args, "profile", "all");
    const std::string jsonPath      = getArg(args, "json", "");
    const bool quiet = hasFlag(args, "quiet");

    // Resolve profiles
    const auto& allProfiles = bench::getAllProfiles();
    std::vector<const bench::DatasetProfile*> profiles;
    if (profileFilter == "all") {
        for (const auto& p : allProfiles) profiles.push_back(&p);
    } else {
        for (const auto& p : allProfiles) {
            if (p.name == profileFilter) { profiles.push_back(&p); break; }
        }
        if (profiles.empty()) {
            std::cerr << "ERROR: Unknown profile '" << profileFilter << "'\nAvailable: ";
            for (const auto& p : allProfiles) std::cerr << p.name << " ";
            std::cerr << "\n";
            return 1;
        }
    }

    auto candidates = buildCandidates();

    std::cerr << "=== Codec Comparison Benchmark ===\n"
              << "  Profiles:   " << profiles.size() << "\n"
              << "  Rows:       " << numRows << "\n"
              << "  Iterations: " << iterations << " (interleaved)\n"
              << "  Candidates: " << candidates.size() << "\n\n";

    // Gather all results
    std::vector<ProfileResult> allResults;
    bench::Timer totalTimer;
    totalTimer.start();

    for (const auto* profile : profiles) {
        std::cerr << "=== " << profile->name << " (" << profile->layout.columnCount() << " cols) ===\n";
        auto pr = runProfile(*profile, numRows, iterations, candidates, quiet);
        allResults.insert(allResults.end(), pr.begin(), pr.end());
    }

    totalTimer.stop();

    // ============================================================================
    // Print summary table
    // ============================================================================

    // Determine column widths
    const int wCand = 15, wTime = 10, wSize = 12, wRatio = 8, wTput = 14;

    std::cout << "\n========== CODEC COMPARISON: " << numRows << " rows, "
              << iterations << " iterations, " << profiles.size() << " profiles ==========\n\n";

    // Group by profile
    for (const auto* profile : profiles) {
        std::cout << "--- " << profile->name << " (" << profile->layout.columnCount() << " cols) ---\n";
        std::cout << std::left << std::setw(wCand) << "Candidate"
                  << std::right
                  << std::setw(wTime)  << "Wr(ms)"
                  << std::setw(wTime)  << "Rd(ms)"
                  << std::setw(wSize)  << "Size(B)"
                  << std::setw(wRatio) << "Ratio"
                  << std::setw(wTput)  << "Wr(Krow/s)"
                  << std::setw(wTput)  << "Rd(Krow/s)"
                  << "  Valid\n";
        std::cout << std::string(wCand + wTime*2 + wSize + wRatio + wTput*2 + 7, '-') << "\n";

        for (const auto& r : allResults) {
            if (r.profileName != profile->name) continue;
            std::cout << std::left << std::setw(wCand) << r.candidateLabel
                      << std::right << std::fixed
                      << std::setprecision(1) << std::setw(wTime)  << r.medianWriteMs
                      << std::setprecision(1) << std::setw(wTime)  << r.medianReadMs
                      << std::setw(wSize)  << r.fileSize
                      << std::setprecision(3) << std::setw(wRatio) << r.ratioVsCsv
                      << std::setprecision(0) << std::setw(wTput)  << (r.writeRowsPerSec / 1000.0)
                      << std::setprecision(0) << std::setw(wTput)  << (r.readRowsPerSec / 1000.0)
                      << (r.allValid ? "  OK" : "  FAIL") << "\n";
        }
        std::cout << "\n";
    }

    // ============================================================================
    // Aggregate across profiles (median per candidate)
    // ============================================================================

    std::cout << "========== AGGREGATE (median across " << profiles.size() << " profiles) ==========\n\n";
    std::cout << std::left << std::setw(wCand) << "Candidate"
              << std::right
              << std::setw(wTput)  << "Wr(Krow/s)"
              << std::setw(wTput)  << "Rd(Krow/s)"
              << std::setw(wRatio+2) << "Ratio"
              << "\n";
    std::cout << std::string(wCand + wTput*2 + wRatio + 2, '-') << "\n";

    for (const auto& cand : candidates) {
        std::vector<double> wrs, rrs, ratios;
        for (const auto& r : allResults) {
            if (r.candidateLabel == cand.label) {
                wrs.push_back(r.writeRowsPerSec / 1000.0);
                rrs.push_back(r.readRowsPerSec / 1000.0);
                ratios.push_back(r.ratioVsCsv);
            }
        }
        std::cout << std::left << std::setw(wCand) << cand.label
                  << std::right << std::fixed
                  << std::setprecision(0) << std::setw(wTput) << median(wrs)
                  << std::setprecision(0) << std::setw(wTput) << median(rrs)
                  << std::setprecision(3) << std::setw(wRatio+2) << median(ratios)
                  << "\n";
    }

    std::cout << "\nTotal time: " << std::fixed << std::setprecision(1) << totalTimer.elapsedSec() << " s\n";

    // Validation
    bool allOk = true;
    for (const auto& r : allResults) {
        if (!r.allValid) { allOk = false; break; }
    }
    std::cout << "Validation: " << (allOk ? "ALL PASSED" : "SOME FAILED") << "\n";

    // ============================================================================
    // Optional JSON output
    // ============================================================================

    if (!jsonPath.empty()) {
        std::ofstream jf(jsonPath);
        jf << "[\n";
        for (size_t i = 0; i < allResults.size(); ++i) {
            const auto& r = allResults[i];
            jf << "  {"
               << "\"profile\":\"" << r.profileName << "\""
               << ",\"cols\":" << r.numCols
               << ",\"candidate\":\"" << r.candidateLabel << "\""
               << ",\"rows\":" << numRows
               << ",\"write_ms\":" << std::fixed << std::setprecision(2) << r.medianWriteMs
               << ",\"read_ms\":" << std::setprecision(2) << r.medianReadMs
               << ",\"file_size\":" << r.fileSize
               << ",\"ratio_vs_csv\":" << std::setprecision(4) << r.ratioVsCsv
               << ",\"write_krows_sec\":" << std::setprecision(1) << (r.writeRowsPerSec / 1000.0)
               << ",\"read_krows_sec\":" << std::setprecision(1) << (r.readRowsPerSec / 1000.0)
               << ",\"valid\":" << (r.allValid ? "true" : "false")
               << "}";
            if (i + 1 < allResults.size()) jf << ",";
            jf << "\n";
        }
        jf << "]\n";
        std::cerr << "JSON results written to: " << jsonPath << "\n";
    }

    return allOk ? 0 : 1;
}
