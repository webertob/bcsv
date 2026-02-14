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
 *     --scenario=LIST  Comma-separated sparse scenarios to run (default: all)
 *     --list           List available profiles and exit
 *     --list-scenarios List available sparse scenarios and exit
 *     --quiet          Suppress progress output
 *     --no-cleanup     Keep temporary benchmark files
 *     --build-type=X   Tag results with build type (Debug/Release)
 *
 * --rows takes precedence over --size. Without either, profile defaults apply.
 */

#include "bench_common.hpp"
#include "bench_datasets.hpp"

#include <bcsv/bcsv.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using MixedGenericLayoutStatic = bcsv::LayoutStatic<
    bool, bool, bool, bool, bool, bool,
    int8_t, int8_t, int8_t, int8_t, int8_t, int8_t,
    int16_t, int16_t, int16_t, int16_t, int16_t, int16_t,
    int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
    int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
    uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t,
    uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
    float, float, float, float, float, float,
    double, double, double, double, double, double,
    std::string, std::string, std::string, std::string, std::string, std::string
>;

enum class SparseKind {
    Baseline,
    Columns,
    EveryN,
    PredicatePercent
};

struct SparseScenario {
    std::string id;
    SparseKind kind = SparseKind::Baseline;
    size_t columns_k = 0;
    size_t every_n = 0;
    size_t predicate_percent = 0;
};

std::vector<SparseScenario> buildSparseScenarios() {
    return {
        {"baseline", SparseKind::Baseline, 0, 0, 0},
        {"sparse_columns_k1", SparseKind::Columns, 1, 0, 0},
        {"sparse_columns_k3", SparseKind::Columns, 3, 0, 0},
        {"sparse_columns_k8", SparseKind::Columns, 8, 0, 0},
        {"sample_every_n10", SparseKind::EveryN, 0, 10, 0},
        {"sample_every_n100", SparseKind::EveryN, 0, 100, 0},
        {"predicate_selectivity_1", SparseKind::PredicatePercent, 0, 0, 1},
        {"predicate_selectivity_10", SparseKind::PredicatePercent, 0, 0, 10},
        {"predicate_selectivity_25", SparseKind::PredicatePercent, 0, 0, 25},
    };
}

bool supportsStaticMode(const bench::DatasetProfile& profile) {
    return profile.name == "mixed_generic" && profile.layout.columnCount() == 72;
}

MixedGenericLayoutStatic createMixedGenericStaticLayout() {
    std::array<std::string, 72> columnNames{};
    const std::vector<std::string> typeNames = {
        "bool", "int8", "int16", "int32", "int64", "uint8",
        "uint16", "uint32", "uint64", "float", "double", "string"
    };

    size_t idx = 0;
    for (size_t typeIdx = 0; typeIdx < typeNames.size(); ++typeIdx) {
        for (size_t colIdx = 0; colIdx < 6; ++colIdx) {
            columnNames[idx++] = typeNames[typeIdx] + "_" + std::to_string(colIdx);
        }
    }

    return MixedGenericLayoutStatic(columnNames);
}

template<typename RowType>
void fillRowRandomByLayout(RowType& row, size_t rowIndex, const bcsv::Layout& layout) {
    bench::datagen::fillRowRandom(row, rowIndex, layout);
}

template<typename RowType>
void fillRowZoHByLayout(RowType& row, size_t rowIndex, const bcsv::Layout& layout) {
    bench::datagen::fillRowTimeSeries(row, rowIndex, layout, 100);
}

std::vector<std::string> splitCsvList(const std::string& input) {
    std::vector<std::string> out;
    std::string token;
    std::stringstream ss(input);
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

std::vector<SparseScenario> filterScenarios(const std::vector<SparseScenario>& all,
                                            const std::string& filterCsv,
                                            std::string& error)
{
    if (filterCsv.empty()) {
        return all;
    }

    std::vector<SparseScenario> selected;
    const auto requested = splitCsvList(filterCsv);
    if (requested.empty()) {
        error = "--scenario provided but empty";
        return {};
    }

    for (const auto& id : requested) {
        auto it = std::find_if(all.begin(), all.end(), [&](const SparseScenario& s) {
            return s.id == id;
        });
        if (it == all.end()) {
            error = "Unknown scenario: " + id;
            return {};
        }
        selected.push_back(*it);
    }

    return selected;
}

std::string makeScenarioDatasetName(const std::string& base, const SparseScenario& scenario) {
    if (scenario.kind == SparseKind::Baseline) {
        return base;
    }
    return base + "::" + scenario.id;
}

std::string scenarioFileTag(const SparseScenario& scenario) {
    return (scenario.kind == SparseKind::Baseline) ? std::string{} : ("_" + scenario.id);
}

void applyScenarioMetadata(bench::BenchmarkResult& result,
                           const bench::DatasetProfile& profile,
                           size_t numRows,
                           const SparseScenario& scenario,
                           const std::string& mode,
                           const std::string& accessPath)
{
    result.dataset_name = makeScenarioDatasetName(profile.name, scenario);
    result.mode = mode;
    result.num_rows = numRows;
    result.num_columns = profile.layout.columnCount();
    result.scenario_id = scenario.id;
    result.access_path = accessPath;
    result.selected_columns = (scenario.kind == SparseKind::Columns)
        ? std::min(scenario.columns_k, profile.layout.columnCount())
        : profile.layout.columnCount();
}

double computeProcessedRowRatio(size_t processedRows, size_t totalRows) {
    return (totalRows > 0)
        ? static_cast<double>(processedRows) / static_cast<double>(totalRows)
        : 0.0;
}

std::vector<size_t> buildSelectedColumns(const bcsv::Layout& layout, size_t k) {
    std::vector<size_t> selected;
    const size_t count = layout.columnCount();
    if (count == 0 || k == 0) {
        return selected;
    }

    k = std::min(k, count);
    selected.reserve(k);

    for (size_t i = 0; i < k; ++i) {
        const size_t idx = (i * count) / k;
        if (selected.empty() || selected.back() != idx) {
            selected.push_back(idx);
        }
    }

    while (selected.size() < k) {
        const size_t idx = selected.size();
        if (idx < count) {
            selected.push_back(idx);
        } else {
            break;
        }
    }

    return selected;
}

std::optional<size_t> findFirstNumericColumn(const bcsv::Layout& layout) {
    for (size_t i = 0; i < layout.columnCount(); ++i) {
        if (layout.columnType(i) != bcsv::ColumnType::STRING) {
            return i;
        }
    }
    return std::nullopt;
}

template<typename RowType>
double numericCellAsDouble(const RowType& row, size_t colIdx, const bcsv::Layout& layout) {
    switch (layout.columnType(colIdx)) {
        case bcsv::ColumnType::BOOL:   return row.template get<bool>(colIdx) ? 1.0 : 0.0;
        case bcsv::ColumnType::INT8:   return static_cast<double>(row.template get<int8_t>(colIdx));
        case bcsv::ColumnType::INT16:  return static_cast<double>(row.template get<int16_t>(colIdx));
        case bcsv::ColumnType::INT32:  return static_cast<double>(row.template get<int32_t>(colIdx));
        case bcsv::ColumnType::INT64:  return static_cast<double>(row.template get<int64_t>(colIdx));
        case bcsv::ColumnType::UINT8:  return static_cast<double>(row.template get<uint8_t>(colIdx));
        case bcsv::ColumnType::UINT16: return static_cast<double>(row.template get<uint16_t>(colIdx));
        case bcsv::ColumnType::UINT32: return static_cast<double>(row.template get<uint32_t>(colIdx));
        case bcsv::ColumnType::UINT64: return static_cast<double>(row.template get<uint64_t>(colIdx));
        case bcsv::ColumnType::FLOAT:  return static_cast<double>(row.template get<float>(colIdx));
        case bcsv::ColumnType::DOUBLE: return row.template get<double>(colIdx);
        case bcsv::ColumnType::STRING: return 0.0;
        default:                        return 0.0;
    }
}

template<typename RowType>
bool shouldProcessRow(const SparseScenario& scenario,
                      size_t rowIndex,
                      const RowType& expectedRow,
                      const bcsv::Layout& layout,
                      const std::optional<size_t>& predicateColumn)
{
    switch (scenario.kind) {
        case SparseKind::Baseline:
        case SparseKind::Columns:
            return true;
        case SparseKind::EveryN:
            return scenario.every_n > 0 && (rowIndex % scenario.every_n) == 0;
        case SparseKind::PredicatePercent: {
            const size_t pct = std::max<size_t>(1, std::min<size_t>(100, scenario.predicate_percent));
            uint64_t token = static_cast<uint64_t>(rowIndex) * 11400714819323198485ull;
            if (predicateColumn.has_value()) {
                const double v = numericCellAsDouble(expectedRow, predicateColumn.value(), layout);
                const int64_t scaled = static_cast<int64_t>(v * 1000.0);
                const uint64_t mag = static_cast<uint64_t>(scaled < 0 ? -scaled : scaled);
                token ^= (mag + 0x9e3779b97f4a7c15ull + (token << 6) + (token >> 2));
            }
            return (token % 100) < pct;
        }
        default:
            return true;
    }
}

template<typename ExpectedRow, typename ActualRow>
bool compareCellExact(const ExpectedRow& expected,
                      const ActualRow& actual,
                      size_t colIdx,
                      const bcsv::Layout& layout)
{
    switch (layout.columnType(colIdx)) {
        case bcsv::ColumnType::BOOL:   return expected.template get<bool>(colIdx) == actual.template get<bool>(colIdx);
        case bcsv::ColumnType::INT8:   return expected.template get<int8_t>(colIdx) == actual.template get<int8_t>(colIdx);
        case bcsv::ColumnType::INT16:  return expected.template get<int16_t>(colIdx) == actual.template get<int16_t>(colIdx);
        case bcsv::ColumnType::INT32:  return expected.template get<int32_t>(colIdx) == actual.template get<int32_t>(colIdx);
        case bcsv::ColumnType::INT64:  return expected.template get<int64_t>(colIdx) == actual.template get<int64_t>(colIdx);
        case bcsv::ColumnType::UINT8:  return expected.template get<uint8_t>(colIdx) == actual.template get<uint8_t>(colIdx);
        case bcsv::ColumnType::UINT16: return expected.template get<uint16_t>(colIdx) == actual.template get<uint16_t>(colIdx);
        case bcsv::ColumnType::UINT32: return expected.template get<uint32_t>(colIdx) == actual.template get<uint32_t>(colIdx);
        case bcsv::ColumnType::UINT64: return expected.template get<uint64_t>(colIdx) == actual.template get<uint64_t>(colIdx);
        case bcsv::ColumnType::FLOAT:  return expected.template get<float>(colIdx) == actual.template get<float>(colIdx);
        case bcsv::ColumnType::DOUBLE: return expected.template get<double>(colIdx) == actual.template get<double>(colIdx);
        case bcsv::ColumnType::STRING: return expected.template get<std::string>(colIdx) == actual.template get<std::string>(colIdx);
        default:                        return false;
    }
}

template<typename ExpectedRow, typename ActualRow>
bool validateRowByScenarioExact(const SparseScenario& scenario,
                                size_t rowIndex,
                                const ExpectedRow& expected,
                                const ActualRow& actual,
                                const bcsv::Layout& layout,
                                const std::vector<size_t>& selectedColumns,
                                std::string& error)
{
    if (scenario.kind == SparseKind::Columns) {
        for (size_t c : selectedColumns) {
            if (!compareCellExact(expected, actual, c, layout)) {
                error = "Mismatch row=" + std::to_string(rowIndex) + " col=" + std::to_string(c);
                return false;
            }
        }
        return true;
    }

    for (size_t c = 0; c < layout.columnCount(); ++c) {
        if (!compareCellExact(expected, actual, c, layout)) {
            error = "Mismatch row=" + std::to_string(rowIndex) + " col=" + std::to_string(c);
            return false;
        }
    }
    return true;
}

template<bcsv::TrackingPolicy P1, bcsv::TrackingPolicy P2>
void validateRowByScenario(const SparseScenario& scenario,
                           size_t rowIndex,
                           const bcsv::RowImpl<P1>& expected,
                           const bcsv::RowImpl<P2>& actual,
                           const bcsv::Layout& layout,
                           const std::vector<size_t>& selectedColumns,
                           bench::RoundTripValidator& validator)
{
    if (scenario.kind == SparseKind::Columns) {
        for (size_t c : selectedColumns) {
            validator.compareCell(rowIndex, c, expected, actual, layout);
        }
        return;
    }

    for (size_t c = 0; c < layout.columnCount(); ++c) {
        validator.compareCell(rowIndex, c, expected, actual, layout);
    }
}

// ============================================================================
// Benchmark runners
// ============================================================================

/// Benchmark: CSV write/read with fair implementation using visitConst()
bench::BenchmarkResult benchmarkCSV(const bench::DatasetProfile& profile,
                                     size_t numRows,
                                     const SparseScenario& scenario,
                                     bool quiet)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, "CSV", "parse_then_project");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario), ".csv");

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
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;

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

            profile.generate(expectedRow, rowsRead);
            if (shouldProcessRow(scenario, rowsRead, expectedRow, profile.layout, predicateColumn)) {
                validateRowByScenario(scenario, rowsRead, expectedRow, readRow, profile.layout,
                                      selectedColumns, validator);
                ++processedRows;
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
    result.processed_row_ratio = computeProcessedRowRatio(processedRows, numRows);

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
                                              size_t numRows,
                                              const SparseScenario& scenario,
                                              bool quiet)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Flexible", "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_flex", ".bcsv");

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
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;

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

            profile.generate(expectedRow, rowsRead);
            if (shouldProcessRow(scenario, rowsRead, expectedRow, profile.layout, predicateColumn)) {
                validateRowByScenario(scenario, rowsRead, expectedRow, row, profile.layout,
                                      selectedColumns, validator);
                ++processedRows;
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
    result.processed_row_ratio = computeProcessedRowRatio(processedRows, numRows);

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
                                                  size_t numRows,
                                                  const SparseScenario& scenario,
                                                  bool quiet)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Flexible ZoH", "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_flex_zoh", ".bcsv");

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
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;

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

            profile.generateZoH(expectedRow, rowsRead);
            if (shouldProcessRow(scenario, rowsRead, expectedRow, profile.layout, predicateColumn)) {
                validateRowByScenario(scenario, rowsRead, expectedRow, row, profile.layout,
                                      selectedColumns, validator);
                ++processedRows;
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
    result.processed_row_ratio = computeProcessedRowRatio(processedRows, numRows);

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

bench::BenchmarkResult benchmarkBCSVStatic(const bench::DatasetProfile& profile,
                                           size_t numRows,
                                           const SparseScenario& scenario,
                                           bool quiet)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Static", "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_static", ".bcsv");

    bench::Timer timer;
    {
        auto layoutStatic = createMixedGenericStaticLayout();
        bcsv::Writer<MixedGenericLayoutStatic> writer(layoutStatic);
        if (!writer.open(filename, true, 1)) {
            result.validation_error = "Cannot open BCSV Static file: " + writer.getErrorMsg();
            return result;
        }

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            auto& row = writer.row();
            fillRowRandomByLayout(row, i, profile.layout);
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

    bcsv::Row expectedRow(profile.layout);
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;
    bool validationOk = true;
    std::string firstError;

    {
        bcsv::Reader<MixedGenericLayoutStatic> reader;
        if (!reader.open(filename)) {
            result.validation_error = "Cannot read BCSV Static file: " + reader.getErrorMsg();
            return result;
        }

        size_t rowsRead = 0;
        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();
            fillRowRandomByLayout(expectedRow, rowsRead, profile.layout);

            if (shouldProcessRow(scenario, rowsRead, expectedRow, profile.layout, predicateColumn)) {
                std::string err;
                if (!validateRowByScenarioExact(scenario, rowsRead, expectedRow, row, profile.layout,
                                                selectedColumns, err)) {
                    validationOk = false;
                    if (firstError.empty()) firstError = err;
                }
                ++processedRows;
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
    result.processed_row_ratio = computeProcessedRowRatio(processedRows, numRows);
    result.validation_passed = validationOk;
    if (!validationOk) {
        result.validation_error = firstError;
    }
    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV Static read: "
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }

    return result;
}

bench::BenchmarkResult benchmarkBCSVStaticZoH(const bench::DatasetProfile& profile,
                                              size_t numRows,
                                              const SparseScenario& scenario,
                                              bool quiet)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Static ZoH", "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_static_zoh", ".bcsv");

    bench::Timer timer;
    {
        auto layoutStatic = createMixedGenericStaticLayout();
        bcsv::Writer<MixedGenericLayoutStatic, bcsv::TrackingPolicy::Enabled> writer(layoutStatic);
        if (!writer.open(filename, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
            result.validation_error = "Cannot open BCSV Static ZoH file: " + writer.getErrorMsg();
            return result;
        }

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            auto& row = writer.row();
            fillRowZoHByLayout(row, i, profile.layout);
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

    bcsv::RowTracking expectedRow(profile.layout);
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;
    bool validationOk = true;
    std::string firstError;

    {
        bcsv::Reader<MixedGenericLayoutStatic, bcsv::TrackingPolicy::Enabled> reader;
        if (!reader.open(filename)) {
            result.validation_error = "Cannot read BCSV Static ZoH file: " + reader.getErrorMsg();
            return result;
        }

        size_t rowsRead = 0;
        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();
            fillRowZoHByLayout(expectedRow, rowsRead, profile.layout);

            if (shouldProcessRow(scenario, rowsRead, expectedRow, profile.layout, predicateColumn)) {
                std::string err;
                if (!validateRowByScenarioExact(scenario, rowsRead, expectedRow, row, profile.layout,
                                                selectedColumns, err)) {
                    validationOk = false;
                    if (firstError.empty()) firstError = err;
                }
                ++processedRows;
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
    result.processed_row_ratio = computeProcessedRowRatio(processedRows, numRows);
    result.validation_passed = validationOk;
    if (!validationOk) {
        result.validation_error = firstError;
    }
    result.computeThroughput();

    if (!quiet) {
        std::cerr << "  [" << profile.name << "] BCSV Static ZoH read: "
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }

    return result;
}

/// Run all benchmarks for a single dataset profile
std::vector<bench::BenchmarkResult> benchmarkProfile(const bench::DatasetProfile& profile,
                                                      size_t numRows,
                                                      bool quiet,
                                                      const std::vector<SparseScenario>& scenarios)
{
    std::vector<bench::BenchmarkResult> results;

    if (!quiet) {
        std::cerr << "\n=== Dataset: " << profile.name << " ===\n"
                  << "  " << profile.description << "\n"
                  << "  Rows: " << numRows 
                  << "  Columns: " << profile.layout.columnCount() << "\n\n";
    }

    for (const auto& scenario : scenarios) {
        auto csvResult = benchmarkCSV(profile, numRows, scenario, quiet);
        results.push_back(csvResult);

        auto flexResult = benchmarkBCSVFlexible(profile, numRows, scenario, quiet);
        if (csvResult.file_size > 0) {
            flexResult.compression_ratio = static_cast<double>(flexResult.file_size) / csvResult.file_size;
        }
        results.push_back(flexResult);

        auto zohResult = benchmarkBCSVFlexibleZoH(profile, numRows, scenario, quiet);
        if (csvResult.file_size > 0) {
            zohResult.compression_ratio = static_cast<double>(zohResult.file_size) / csvResult.file_size;
        }
        results.push_back(zohResult);

        if (supportsStaticMode(profile)) {
            auto staticResult = benchmarkBCSVStatic(profile, numRows, scenario, quiet);
            if (csvResult.file_size > 0) {
                staticResult.compression_ratio = static_cast<double>(staticResult.file_size) / csvResult.file_size;
            }
            results.push_back(staticResult);

            auto staticZoHResult = benchmarkBCSVStaticZoH(profile, numRows, scenario, quiet);
            if (csvResult.file_size > 0) {
                staticZoHResult.compression_ratio = static_cast<double>(staticZoHResult.file_size) / csvResult.file_size;
            }
            results.push_back(staticZoHResult);
        }
    }

    return results;
}

/// Clean up temporary benchmark files for a profile
void cleanupProfile(const bench::DatasetProfile& profile) {
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path())) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        const bool isBench = filename.find("_bench") != std::string::npos;
        const bool isKnownExt = entry.path().extension() == ".csv" || entry.path().extension() == ".bcsv";
        const bool matchesProfile = filename.rfind(profile.name, 0) == 0;

        if (isBench && isKnownExt && matchesProfile) {
            std::filesystem::remove(entry.path());
        }
    }
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    auto args = bench::parseArgs(argc, argv);
    const auto allScenarios = buildSparseScenarios();

    // --list: print profile names and exit
    if (bench::hasArg(args, "list")) {
        for (const auto& name : bench::getProfileNames()) {
            std::cout << name << "\n";
        }
        return 0;
    }

    // --list-scenarios: print sparse scenario ids and exit
    if (bench::hasArg(args, "list-scenarios")) {
        for (const auto& s : allScenarios) {
            std::cout << s.id << "\n";
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
    std::string scenarioFilter = bench::getArgString(args, "scenario", "");
    bool quiet = bench::hasArg(args, "quiet");
    bool noCleanup = bench::hasArg(args, "no-cleanup");
    std::string buildType = bench::getArgString(args, "build-type", "Release");

    std::string scenarioError;
    auto scenarios = filterScenarios(allScenarios, scenarioFilter, scenarioError);
    if (!scenarioError.empty()) {
        std::cerr << "ERROR: " << scenarioError << "\n";
        std::cerr << "Available scenarios: ";
        for (const auto& s : allScenarios) std::cerr << s.id << " ";
        std::cerr << "\n";
        return 1;
    }

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
                  << "Scenarios: " << scenarios.size() << "\n"
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
            benchmarkProfile(profiles.front(), 100, /*quiet=*/true, {scenarios.front()});
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
            auto results = benchmarkProfile(profile, numRows, quiet, scenarios);
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
