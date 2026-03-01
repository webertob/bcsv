/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file example_sampler.cpp
 * @brief Demonstrates the BCSV Sampler — streaming filter-and-project operator
 *
 * The Sampler wraps a Reader and applies an expression-based conditional
 * (filter) and selection (projection) to each row.  Expressions reference
 * cells via X[row_offset][column], supporting lookbehind/lookahead,
 * arithmetic, bitwise ops, string comparisons, and wildcards.
 *
 * This example:
 *  1. Creates a small sensor-data BCSV file
 *  2. Filters rows where temperature exceeds a threshold
 *  3. Computes a first-derivative (gradient) as a selection expression
 *  4. Shows wildcard and boundary-mode usage
 */

#include <iostream>
#include <iomanip>
#include <filesystem>
#include <bcsv/bcsv.h>
#include <bcsv/sampler/sampler.h>
#include <bcsv/sampler/sampler.hpp>

using namespace bcsv;

// ── Helper: write a 7-row sensor dataset ────────────────────────────

static std::string writeSensorData(const std::string& dir) {
    Layout layout;
    layout.addColumn({"timestamp",   ColumnType::DOUBLE});
    layout.addColumn({"temperature", ColumnType::FLOAT});
    layout.addColumn({"status",      ColumnType::STRING});
    layout.addColumn({"flags",       ColumnType::UINT16});
    layout.addColumn({"counter",     ColumnType::INT32});

    std::string path = dir + "/sensor_data.bcsv";
    Writer<Layout> writer(layout);
    writer.open(path, true);

    struct Row { double ts; float temp; const char* status; uint16_t flags; int32_t ctr; };
    Row data[] = {
        {1.0, 20.5f, "ok",    0x06, 0},
        {2.0, 21.0f, "ok",    0x07, 1},
        {3.0, 21.0f, "warn",  0x03, 2},
        {4.0, 55.0f, "alarm", 0x05, 3},
        {5.0, 55.0f, "alarm", 0x05, 100},
        {6.0, 22.0f, "ok",    0x07, 101},
        {7.0, 22.5f, "ok",    0x06, 102},
    };
    for (auto& d : data) {
        writer.row().set(0, d.ts);
        writer.row().set(1, d.temp);
        writer.row().set(2, std::string(d.status));
        writer.row().set(3, d.flags);
        writer.row().set(4, d.ctr);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Wrote " << 7 << " rows to " << path << "\n\n";
    return path;
}

// ── Demo 1: Simple filter ───────────────────────────────────────────

static void demoFilter(const std::string& path) {
    std::cout << "=== Demo 1: Filter — temperature > 50 ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);
    sampler.setConditional("X[0][\"temperature\"] > 50.0");
    sampler.setSelection("X[0][\"timestamp\"], X[0][\"temperature\"], X[0][\"status\"]");

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  timestamp  temperature  status\n";
    std::cout << "  ---------  -----------  ------\n";
    while (sampler.next()) {
        const auto& r = sampler.row();
        std::cout << "  " << std::setw(9) << r.get<double>(0)
                  << "  " << std::setw(11) << static_cast<double>(r.get<float>(1))
                  << "  " << r.get<std::string>(2) << "\n";
    }
    std::cout << "\n";
}

// ── Demo 2: Gradient (first derivative dT/dt) ──────────────────────

static void demoGradient(const std::string& path) {
    std::cout << "=== Demo 2: Gradient — dT/dt via lookbehind ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);
    sampler.setConditional("true");
    sampler.setSelection(
        "X[0][0], X[0][1], "
        "(X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])");

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  timestamp  temperature  gradient\n";
    std::cout << "  ---------  -----------  --------\n";
    while (sampler.next()) {
        const auto& r = sampler.row();
        std::cout << "  " << std::setw(9) << r.get<double>(0)
                  << "  " << std::setw(11) << static_cast<double>(r.get<float>(1))
                  << "  " << std::setw(8) << r.get<double>(2) << "\n";
    }
    std::cout << "  (Row 0 truncated — no lookbehind available)\n\n";
}

// ── Demo 3: Edge detection — value change with lookbehind ───────────

static void demoEdgeDetect(const std::string& path) {
    std::cout << "=== Demo 3: Edge Detect — temperature change ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);
    sampler.setConditional("X[0][1] != X[-1][1]");
    sampler.setSelection("X[0][0], X[-1][1], X[0][1]");

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  timestamp   prev_temp   curr_temp\n";
    std::cout << "  ---------  ----------  ----------\n";
    while (sampler.next()) {
        const auto& r = sampler.row();
        std::cout << "  " << std::setw(9) << r.get<double>(0)
                  << "  " << std::setw(10) << static_cast<double>(r.get<float>(1))
                  << "  " << std::setw(10) << static_cast<double>(r.get<float>(2)) << "\n";
    }
    std::cout << "\n";
}

// ── Demo 4: 3-point moving average with lookahead ───────────────────

static void demoMovingAverage(const std::string& path) {
    std::cout << "=== Demo 4: 3-Point Moving Average ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);
    sampler.setConditional("true");
    sampler.setSelection(
        "X[0][0], X[0][1], "
        "(X[-1][1] + X[0][1] + X[+1][1]) / 3.0");

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  timestamp  raw_temp  avg_temp\n";
    std::cout << "  ---------  --------  --------\n";
    while (sampler.next()) {
        const auto& r = sampler.row();
        std::cout << "  " << std::setw(9) << r.get<double>(0)
                  << "  " << std::setw(8) << static_cast<double>(r.get<float>(1))
                  << "  " << std::setw(8) << r.get<double>(2) << "\n";
    }
    std::cout << "  (First/last rows truncated — window incomplete)\n\n";
}

// ── Demo 5: Bulk mode + bitwise flag filter ─────────────────────────

static void demoBulkAndBitwise(const std::string& path) {
    std::cout << "=== Demo 5: Bulk Mode + Bitwise Flag Filter ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);
    sampler.setConditional("(X[0][3] & 0x04) != 0");
    sampler.setSelection("X[0][0], X[0][3]");

    auto rows = sampler.bulk();
    std::cout << "  " << rows.size() << " rows have bit 2 set in flags:\n";
    for (const auto& r : rows) {
        std::cout << "    timestamp=" << r.get<double>(0)
                  << "  flags=0x" << std::hex
                  << static_cast<int>(r.get<uint16_t>(1))
                  << std::dec << "\n";
    }
    std::cout << "\n";
}

// ── Demo 6: Compile-time error handling ─────────────────────────────

static void demoErrorHandling(const std::string& path) {
    std::cout << "=== Demo 6: Compile-Time Error Handling ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);

    // String + arithmetic → type error
    auto result = sampler.setConditional("X[0][2] + 1 > 0");
    std::cout << "  Expression: X[0][2] + 1 > 0\n";
    std::cout << "  Compiled: " << (result.success ? "yes" : "no") << "\n";
    if (!result.success)
        std::cout << "  Error: " << result.error_msg << "\n";

    // Out-of-range column
    result = sampler.setConditional("X[0][99] > 0");
    std::cout << "\n  Expression: X[0][99] > 0\n";
    std::cout << "  Compiled: " << (result.success ? "yes" : "no") << "\n";
    if (!result.success)
        std::cout << "  Error: " << result.error_msg << "\n";
    std::cout << "\n";
}

// ── Demo 7: Disassembly view ────────────────────────────────────────

static void demoDisassembly(const std::string& path) {
    std::cout << "=== Demo 7: Bytecode Disassembly ===\n";
    Reader<Layout> reader;
    reader.open(path);

    Sampler<Layout> sampler(reader);
    sampler.setConditional("X[0][1] > 50.0");
    sampler.setSelection("X[0][0], X[0][1]");

    std::cout << sampler.disassemble() << "\n";
}

// ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "BCSV Sampler Example\n";
    std::cout << "====================\n\n";

    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "bcsv_sampler_example";
    fs::create_directories(tmp);

    std::string path = writeSensorData(tmp.string());

    demoFilter(path);
    demoGradient(path);
    demoEdgeDetect(path);
    demoMovingAverage(path);
    demoBulkAndBitwise(path);
    demoErrorHandling(path);
    demoDisassembly(path);

    fs::remove_all(tmp);
    std::cout << "Done.\n";
    return 0;
}
