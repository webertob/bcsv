/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file bench_common.hpp
 * @brief Shared infrastructure for the BCSV benchmark suite
 * 
 * Provides:
 * - BenchmarkResult struct for structured (JSON) output
 * - PlatformInfo for capturing host/build environment
 * - CsvWriter: fair CSV serialization using Row::visitConst()
 * - CsvReader: fair CSV deserialization with real type parsing
 * - RoundTripValidator: per-cell comparison with diagnostics
 * - Timing utilities and optimization prevention helpers
 */

#include <bcsv/bcsv.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

// ============================================================================
// Optimization prevention — ensures the compiler cannot elide benchmark work
// ============================================================================

/// Prevent the compiler from optimizing away a value
template<typename T>
inline void doNotOptimize(const T& value) {
    // GCC/Clang: the value is observable via the asm constraint
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(value) : "memory");
#else
    volatile auto sink = value;
    (void)sink;
#endif
}

/// Compiler memory fence — prevents reordering around measurement points
inline void clobberMemory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

// ============================================================================
// High-resolution timer wrapper
// ============================================================================

class Timer {
public:
    void start() { start_ = std::chrono::steady_clock::now(); }
    void stop()  { end_   = std::chrono::steady_clock::now(); }

    /// Elapsed time in milliseconds
    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(end_ - start_).count();
    }

    /// Elapsed time in seconds
    double elapsedSec() const {
        return std::chrono::duration<double>(end_ - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_{};
    std::chrono::steady_clock::time_point end_{};
};

// ============================================================================
// PlatformInfo — captures host and build environment
// ============================================================================

struct PlatformInfo {
    std::string hostname;
    std::string os;
    std::string cpu_model;
    std::string compiler;
    std::string bcsv_version;
    std::string git_describe;     // e.g. "v1.2.3-14-gabcdef0"
    std::string build_type;       // Debug / Release / RelWithDebInfo
    int         pointer_size = 0; // 4 or 8

    /// Gather platform info from the running system
    static PlatformInfo gather(const std::string& buildType = "Release") {
        PlatformInfo info;
        info.pointer_size = static_cast<int>(sizeof(void*));
        info.build_type = buildType;
        info.bcsv_version = bcsv::getVersion();

        // Hostname
#if defined(_WIN32)
        char buf[256]{};
        DWORD size = sizeof(buf);
        if (GetComputerNameA(buf, &size)) info.hostname = buf;
        info.os = "Windows";
#else
        char buf[256]{};
        if (gethostname(buf, sizeof(buf)) == 0) info.hostname = buf;
        info.os = "Linux"; // simplified
#endif

        // CPU model (Linux)
#if defined(__linux__)
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("model name") != std::string::npos) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    info.cpu_model = line.substr(pos + 2);
                }
                break;
            }
        }
#endif

        // Compiler
#if defined(__clang__)
        info.compiler = "Clang " + std::to_string(__clang_major__) + "." 
                      + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
        info.compiler = "GCC " + std::to_string(__GNUC__) + "." 
                      + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
        info.compiler = "MSVC " + std::to_string(_MSC_VER);
#else
        info.compiler = "Unknown";
#endif

        // Git describe — run at build time via CMake, fallback to runtime
        info.git_describe = info.bcsv_version; // fallback

        return info;
    }

    /// Serialize to JSON string
    std::string toJson() const {
        std::ostringstream ss;
        ss << "  \"platform\": {\n"
           << "    \"hostname\": \"" << hostname << "\",\n"
           << "    \"os\": \"" << os << "\",\n"
           << "    \"cpu_model\": \"" << cpu_model << "\",\n"
           << "    \"compiler\": \"" << compiler << "\",\n"
           << "    \"bcsv_version\": \"" << bcsv_version << "\",\n"
           << "    \"git_describe\": \"" << git_describe << "\",\n"
           << "    \"build_type\": \"" << build_type << "\",\n"
           << "    \"pointer_size\": " << pointer_size << "\n"
           << "  }";
        return ss.str();
    }
};

// ============================================================================
// BenchmarkResult — structured output for a single benchmark measurement
// ============================================================================

struct BenchmarkResult {
    std::string dataset_name;       // e.g. "mixed_generic"
    std::string mode;               // e.g. "BCSV Flexible", "CSV", "BCSV Static ZoH"
    size_t      num_rows     = 0;
    size_t      num_columns  = 0;

    double      write_time_ms = 0;  // milliseconds
    double      read_time_ms  = 0;
    size_t      file_size     = 0;  // bytes

    double      write_throughput_rows_per_sec = 0;
    double      read_throughput_rows_per_sec  = 0;
    double      write_throughput_mb_per_sec   = 0;
    double      read_throughput_mb_per_sec    = 0;
    double      compression_ratio             = 0; // file_size / csv_file_size (0 = unknown)

    bool        validation_passed = false;
    std::string validation_error;                   // empty = OK 

    /// Compute throughput metrics from raw times
    void computeThroughput() {
        if (write_time_ms > 0) {
            write_throughput_rows_per_sec = num_rows / (write_time_ms / 1000.0);
            write_throughput_mb_per_sec   = (file_size / (1024.0 * 1024.0)) / (write_time_ms / 1000.0);
        }
        if (read_time_ms > 0) {
            read_throughput_rows_per_sec = num_rows / (read_time_ms / 1000.0);
            read_throughput_mb_per_sec   = (file_size / (1024.0 * 1024.0)) / (read_time_ms / 1000.0);
        }
    }

    /// Serialize to a JSON object string (no enclosing braces)
    std::string toJson() const {
        std::ostringstream ss;
        ss << std::fixed;
        ss << "    {\n"
           << "      \"dataset\": \"" << dataset_name << "\",\n"
           << "      \"mode\": \"" << mode << "\",\n"
           << "      \"num_rows\": " << num_rows << ",\n"
           << "      \"num_columns\": " << num_columns << ",\n"
           << "      \"write_time_ms\": " << std::setprecision(2) << write_time_ms << ",\n"
           << "      \"read_time_ms\": " << std::setprecision(2) << read_time_ms << ",\n"
           << "      \"file_size\": " << file_size << ",\n"
           << "      \"write_rows_per_sec\": " << std::setprecision(0) << write_throughput_rows_per_sec << ",\n"
           << "      \"read_rows_per_sec\": " << std::setprecision(0) << read_throughput_rows_per_sec << ",\n"
           << "      \"write_mb_per_sec\": " << std::setprecision(2) << write_throughput_mb_per_sec << ",\n"
           << "      \"read_mb_per_sec\": " << std::setprecision(2) << read_throughput_mb_per_sec << ",\n"
           << "      \"compression_ratio\": " << std::setprecision(4) << compression_ratio << ",\n"
           << "      \"validation_passed\": " << (validation_passed ? "true" : "false");
        if (!validation_error.empty()) {
            ss << ",\n      \"validation_error\": \"" << validation_error << "\"";
        }
        ss << "\n    }";
        return ss.str();
    }
};

/// Write a complete JSON results file
inline void writeResultsJson(const std::string& filepath,
                             const PlatformInfo& platform,
                             const std::vector<BenchmarkResult>& results,
                             double total_time_sec = 0)
{
    std::ofstream out(filepath);
    if (!out.is_open()) {
        std::cerr << "ERROR: Cannot write results to " << filepath << "\n";
        return;
    }

    out << "{\n";
    out << "  \"format_version\": 2,\n";
    out << platform.toJson() << ",\n";
    out << "  \"total_time_sec\": " << std::fixed << std::setprecision(2) << total_time_sec << ",\n";
    out << "  \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        out << results[i].toJson();
        if (i + 1 < results.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

// ============================================================================
// CsvWriter — fair CSV serialization using Row::visitConst()
// ============================================================================

/**
 * Writes a bcsv::Row to a std::ostream in CSV format using visitConst().
 * This is a fair comparison baseline: it does the same amount of type-dispatch
 * and string-formatting work that any serious CSV library would require.
 *
 * Performance-conscious design:
 * - Uses std::to_chars() for all numeric types (no locale, no virtual dispatch)
 * - Builds each row in a flat char buffer, then does a single os.write()
 * - Uses bcsv::visitors pattern — the visitor is a simple lambda
 * - String quoting uses direct buffer writes, not operator<<
 */
class CsvWriter {
public:
    explicit CsvWriter(std::ostream& os, char delimiter = ',')
        : os_(os), delimiter_(delimiter)
    {
        buf_.reserve(4096); // pre-allocate for typical row sizes
    }

    /// Write column header line from a layout
    void writeHeader(const bcsv::Layout& layout) {
        buf_.clear();
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            if (i > 0) buf_.push_back(delimiter_);
            const auto& name = layout.columnName(i);
            buf_.insert(buf_.end(), name.begin(), name.end());
        }
        buf_.push_back('\n');
        os_.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
    }

    /// Write a single data row using visitConst() + to_chars buffer
    template<bcsv::TrackingPolicy Policy>
    void writeRow(const bcsv::RowImpl<Policy>& row) {
        buf_.clear();
        row.visitConst([this](size_t index, const auto& value) {
            if (index > 0) buf_.push_back(delimiter_);

            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                if (value) {
                    buf_.insert(buf_.end(), {'t','r','u','e'});
                } else {
                    buf_.insert(buf_.end(), {'f','a','l','s','e'});
                }
            } else if constexpr (std::is_same_v<T, int8_t>) {
                appendToChars(static_cast<int>(value)); // avoid char interpretation
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                appendToChars(static_cast<unsigned>(value));
            } else if constexpr (std::is_same_v<T, std::string>) {
                appendString(value);
            } else {
                // All other numeric types (int16..uint64, float, double)
                appendToChars(value);
            }
        });
        buf_.push_back('\n');
        os_.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
    }

private:
    /// Append any numeric type via std::to_chars (no locale, no virtual dispatch)
    template<typename T>
    void appendToChars(T value) {
        // to_chars needs at most ~25 chars for any numeric type
        constexpr size_t kMaxDigits = 32;
        size_t oldSize = buf_.size();
        buf_.resize(oldSize + kMaxDigits);
        auto [ptr, ec] = std::to_chars(buf_.data() + oldSize, buf_.data() + oldSize + kMaxDigits, value);
        buf_.resize(static_cast<size_t>(ptr - buf_.data())); // shrink to actual length
    }

    /// Append a string with RFC 4180 quoting if needed
    void appendString(const std::string& value) {
        // Check if quoting is needed
        bool needsQuoting = false;
        for (char c : value) {
            if (c == delimiter_ || c == '"' || c == '\n') {
                needsQuoting = true;
                break;
            }
        }

        if (needsQuoting) {
            buf_.push_back('"');
            for (char c : value) {
                if (c == '"') buf_.push_back('"'); // escape quotes by doubling
                buf_.push_back(c);
            }
            buf_.push_back('"');
        } else {
            buf_.insert(buf_.end(), value.begin(), value.end());
        }
    }

    std::ostream& os_;
    char delimiter_;
    std::vector<char> buf_;  // reused across rows — amortized allocation
};

// ============================================================================
// CsvReader — fair CSV deserialization with real type parsing
// ============================================================================

/**
 * Reads CSV text and parses values into native types according to a bcsv::Layout.
 * This is the fair counterpart to CsvWriter: it does real std::from_chars
 * conversion on all types — integers, floats and doubles — with zero heap
 * allocations per numeric cell.  String cells use a reusable std::string
 * buffer to minimize allocation overhead.
 */
class CsvReader {
public:
    explicit CsvReader(char delimiter = ',') : delimiter_(delimiter) {}

    /// Parse one CSV line into a Row, using the layout for type information.
    /// Returns false if the line could not be parsed (e.g., wrong column count).
    template<bcsv::TrackingPolicy Policy>
    bool parseLine(const std::string& line, const bcsv::Layout& layout, bcsv::RowImpl<Policy>& row) {
        cells_.clear();
        splitLine(line, cells_);

        if (cells_.size() != layout.columnCount()) {
            return false; // column count mismatch
        }

        for (size_t i = 0; i < cells_.size(); ++i) {
            const std::string_view cell = cells_[i];
            const char* first = cell.data();
            const char* last  = cell.data() + cell.size();

            switch (layout.columnType(i)) {
                case bcsv::ColumnType::BOOL: {
                    bool val = (cell == "true" || cell == "1" || cell == "TRUE");
                    row.set(i, val);
                    break;
                }
                case bcsv::ColumnType::INT8: {
                    int v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, static_cast<int8_t>(v));
                    break;
                }
                case bcsv::ColumnType::INT16: {
                    int16_t v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::INT32: {
                    int32_t v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::INT64: {
                    int64_t v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::UINT8: {
                    unsigned v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, static_cast<uint8_t>(v));
                    break;
                }
                case bcsv::ColumnType::UINT16: {
                    uint16_t v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::UINT32: {
                    uint32_t v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::UINT64: {
                    uint64_t v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::FLOAT: {
                    float v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::DOUBLE: {
                    double v = 0;
                    std::from_chars(first, last, v);
                    row.set(i, v);
                    break;
                }
                case bcsv::ColumnType::STRING: {
                    // Unquote if quoted, reuse strbuf_ to reduce allocations
                    if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"') {
                        strbuf_.assign(cell.data() + 1, cell.size() - 2);
                        // Unescape doubled quotes
                        size_t pos = 0;
                        while ((pos = strbuf_.find("\"\"", pos)) != std::string::npos) {
                            strbuf_.erase(pos, 1);
                            pos += 1;
                        }
                        row.set(i, strbuf_);
                    } else {
                        strbuf_.assign(cell.data(), cell.size());
                        row.set(i, strbuf_);
                    }
                    break;
                }
                default:
                    return false; // unknown type
            }
        }
        return true;
    }

private:
    /// Split a CSV line into cells, handling quoted fields
    void splitLine(const std::string& line, std::vector<std::string_view>& out) {
        // Store offsets in temp buffer, then create string_views
        // Simple state machine: tracks whether we're inside quotes
        const char* data = line.data();
        size_t len = line.size();
        size_t start = 0;
        bool inQuotes = false;

        for (size_t i = 0; i <= len; ++i) {
            if (i == len || (!inQuotes && data[i] == delimiter_)) {
                out.emplace_back(data + start, i - start);
                start = i + 1;
            } else if (data[i] == '"') {
                inQuotes = !inQuotes;
            }
        }
    }

    char delimiter_;
    std::vector<std::string_view> cells_;  // reused across calls
    std::string strbuf_;                   // reused across string cells
};

// ============================================================================
// RoundTripValidator — compare two rows cell-by-cell
// ============================================================================

struct ValidationMismatch {
    size_t row;
    size_t col;
    std::string expected;
    std::string actual;
    std::string type;
};

/**
 * Validates that data read back from a file matches the originally written data.
 * Reports the first N mismatches with detailed diagnostic information.
 */
class RoundTripValidator {
public:
    explicit RoundTripValidator(size_t maxErrors = 10) : maxErrors_(maxErrors) {}

    /// Compare a single cell between two rows. Returns true if match.
    template<bcsv::TrackingPolicy P1, bcsv::TrackingPolicy P2>
    bool compareCell(size_t rowIdx, size_t colIdx,
                     const bcsv::RowImpl<P1>& expected,
                     const bcsv::RowImpl<P2>& actual,
                     const bcsv::Layout& layout) 
    {
        bool match = false;
        bcsv::ColumnType type = layout.columnType(colIdx);

        switch (type) {
            case bcsv::ColumnType::BOOL:
                match = (expected.template get<bool>(colIdx) == actual.template get<bool>(colIdx));
                break;
            case bcsv::ColumnType::INT8:
                match = (expected.template get<int8_t>(colIdx) == actual.template get<int8_t>(colIdx));
                break;
            case bcsv::ColumnType::INT16:
                match = (expected.template get<int16_t>(colIdx) == actual.template get<int16_t>(colIdx));
                break;
            case bcsv::ColumnType::INT32:
                match = (expected.template get<int32_t>(colIdx) == actual.template get<int32_t>(colIdx));
                break;
            case bcsv::ColumnType::INT64:
                match = (expected.template get<int64_t>(colIdx) == actual.template get<int64_t>(colIdx));
                break;
            case bcsv::ColumnType::UINT8:
                match = (expected.template get<uint8_t>(colIdx) == actual.template get<uint8_t>(colIdx));
                break;
            case bcsv::ColumnType::UINT16:
                match = (expected.template get<uint16_t>(colIdx) == actual.template get<uint16_t>(colIdx));
                break;
            case bcsv::ColumnType::UINT32:
                match = (expected.template get<uint32_t>(colIdx) == actual.template get<uint32_t>(colIdx));
                break;
            case bcsv::ColumnType::UINT64:
                match = (expected.template get<uint64_t>(colIdx) == actual.template get<uint64_t>(colIdx));
                break;
            case bcsv::ColumnType::FLOAT:
                match = (expected.template get<float>(colIdx) == actual.template get<float>(colIdx));
                break;
            case bcsv::ColumnType::DOUBLE:
                match = (expected.template get<double>(colIdx) == actual.template get<double>(colIdx));
                break;
            case bcsv::ColumnType::STRING:
                match = (expected.template get<std::string>(colIdx) == actual.template get<std::string>(colIdx));
                break;
            default:
                break;
        }

        if (!match && mismatches_.size() < maxErrors_) {
            // Record the mismatch details
            ValidationMismatch m;
            m.row = rowIdx;
            m.col = colIdx;
            m.type = bcsv::toString(type);
            // Stringify values via visitConst for genericity
            expected.visitConst(colIdx, [&](size_t, const auto& v) {
                std::ostringstream ss;
                using VT = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<VT, int8_t> || std::is_same_v<VT, uint8_t>)
                    ss << static_cast<int>(v);
                else if constexpr (std::is_same_v<VT, bool>)
                    ss << (v ? "true" : "false");
                else
                    ss << v;
                m.expected = ss.str();
            });
            actual.visitConst(colIdx, [&](size_t, const auto& v) {
                std::ostringstream ss;
                using VT = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<VT, int8_t> || std::is_same_v<VT, uint8_t>)
                    ss << static_cast<int>(v);
                else if constexpr (std::is_same_v<VT, bool>)
                    ss << (v ? "true" : "false");
                else
                    ss << v;
                m.actual = ss.str();
            });
            mismatches_.push_back(std::move(m));
        }
        return match;
    }

    bool passed() const { return mismatches_.empty(); }
    size_t errorCount() const { return mismatches_.size(); }
    const std::vector<ValidationMismatch>& mismatches() const { return mismatches_; }

    std::string summary() const {
        if (mismatches_.empty()) return "PASSED";
        std::ostringstream ss;
        ss << "FAILED (" << mismatches_.size() << " mismatches)\n";
        for (const auto& m : mismatches_) {
            ss << "  Row " << m.row << " Col " << m.col 
               << " [" << m.type << "]: expected=" << m.expected 
               << " actual=" << m.actual << "\n";
        }
        return ss.str();
    }

    void reset() { mismatches_.clear(); }

private:
    size_t maxErrors_;
    std::vector<ValidationMismatch> mismatches_;
};

// ============================================================================
// Utility functions
// ============================================================================

/// Validate that a file exists and has non-zero size. Returns file size.
inline size_t validateFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        throw std::runtime_error("File does not exist: " + filepath);
    }
    size_t size = std::filesystem::file_size(filepath);
    if (size == 0) {
        throw std::runtime_error("File has zero size: " + filepath);
    }
    return size;
}

/// Generate a temporary file path for benchmark artifacts
inline std::string tempFilePath(const std::string& prefix, const std::string& extension) {
    return prefix + "_bench" + extension;
}

/// Print a results table to stdout for human consumption
inline void printResultsTable(const std::vector<BenchmarkResult>& results) {
    if (results.empty()) return;

    std::cout << "\n";
    std::cout << std::left << std::setw(24) << "Mode"
              << std::right << std::setw(12) << "Write(ms)"
              << std::setw(12) << "Read(ms)"
              << std::setw(12) << "Total(ms)"
              << std::setw(14) << "FileSize(MB)"
              << std::setw(14) << "Write(Mrow/s)"
              << std::setw(14) << "Read(Mrow/s)"
              << std::setw(10) << "Valid"
              << "\n";
    std::cout << std::string(112, '-') << "\n";

    for (const auto& r : results) {
        double totalMs = r.write_time_ms + r.read_time_ms;
        double fileSizeMB = r.file_size / (1024.0 * 1024.0);
        double writeMrowS = r.write_throughput_rows_per_sec / 1e6;
        double readMrowS  = r.read_throughput_rows_per_sec / 1e6;

        std::cout << std::left << std::setw(24) << r.mode
                  << std::right << std::fixed
                  << std::setw(12) << std::setprecision(1) << r.write_time_ms
                  << std::setw(12) << std::setprecision(1) << r.read_time_ms
                  << std::setw(12) << std::setprecision(1) << totalMs
                  << std::setw(14) << std::setprecision(2) << fileSizeMB
                  << std::setw(14) << std::setprecision(3) << writeMrowS
                  << std::setw(14) << std::setprecision(3) << readMrowS
                  << std::setw(10) << (r.validation_passed ? "PASS" : "FAIL")
                  << "\n";
    }
    std::cout << "\n";
}

/// Parse command-line arguments into a map
inline std::map<std::string, std::string> parseArgs(int argc, char* argv[]) {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.starts_with("--")) {
            auto eq = arg.find('=');
            if (eq != std::string::npos) {
                args[arg.substr(2, eq - 2)] = arg.substr(eq + 1);
            } else {
                args[arg.substr(2)] = "true";
            }
        }
    }
    return args;
}

/// Get a size_t argument with default
inline size_t getArgSizeT(const std::map<std::string, std::string>& args, 
                           const std::string& key, size_t defaultValue) {
    auto it = args.find(key);
    if (it != args.end()) {
        return std::stoull(it->second);
    }
    return defaultValue;
}

/// Get a string argument with default
inline std::string getArgString(const std::map<std::string, std::string>& args,
                                 const std::string& key, const std::string& defaultValue) {
    auto it = args.find(key);
    if (it != args.end()) {
        return it->second;
    }
    return defaultValue;
}

/// Check if an argument flag is present
inline bool hasArg(const std::map<std::string, std::string>& args, const std::string& key) {
    return args.find(key) != args.end();
}

} // namespace bench
