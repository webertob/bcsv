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
 *     --tracking=MODE  both|enabled|disabled (default: both)
 *     --storage=MODE   both|flexible|static (default: both)
 *     --codec=MODE     both|dense|zoh (default: both)
 *     --list           List available profiles and exit
 *     --list-scenarios List available sparse scenarios and exit
 *     --help           Show CLI help and examples
 *     --compression=N  LZ4 compression level 1-9 (default: 1; 1=fast, 9=best ratio)
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
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

template<typename... Ts>
struct TypeList {};

template<typename... Lists>
struct TypeListConcat;

template<>
struct TypeListConcat<> { using type = TypeList<>; };

template<typename... Ts>
struct TypeListConcat<TypeList<Ts...>> { using type = TypeList<Ts...>; };

template<typename... A, typename... B, typename... Rest>
struct TypeListConcat<TypeList<A...>, TypeList<B...>, Rest...> {
    using type = typename TypeListConcat<TypeList<A..., B...>, Rest...>::type;
};

template<typename T, typename Seq>
struct RepeatAsTypeListImpl;

template<typename T, size_t... I>
struct RepeatAsTypeListImpl<T, std::index_sequence<I...>> {
    using type = TypeList<std::conditional_t<true, T, std::integral_constant<size_t, I>>...>;
};

template<typename T, size_t N>
using RepeatAsTypeList = typename RepeatAsTypeListImpl<T, std::make_index_sequence<N>>::type;

template<typename TL>
struct LayoutFromTypeList;

template<typename... Ts>
struct LayoutFromTypeList<TypeList<Ts...>> {
    using type = bcsv::LayoutStatic<Ts...>;
};

template<typename... Lists>
using ConcatTypeLists = typename TypeListConcat<Lists...>::type;

template<typename TL>
using LayoutFromTypeList_t = typename LayoutFromTypeList<TL>::type;

using SparseEventsLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    RepeatAsTypeList<bool, 20>, RepeatAsTypeList<int32_t, 30>, RepeatAsTypeList<float, 20>,
    RepeatAsTypeList<double, 20>, RepeatAsTypeList<std::string, 10>
>>;

using SensorNoisyLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint32_t>, RepeatAsTypeList<float, 24>, RepeatAsTypeList<double, 24>
>>;

using StringHeavyLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<int32_t, int32_t, int32_t, float, float, float, double, double, uint64_t, uint64_t>,
    RepeatAsTypeList<std::string, 20>
>>;

using SimulationSmoothLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, double, uint32_t, bool>, RepeatAsTypeList<float, 48>, RepeatAsTypeList<double, 48>
>>;

using WeatherTimeseriesLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, std::string, std::string, uint8_t>, RepeatAsTypeList<float, 10>, RepeatAsTypeList<float, 6>,
    TypeList<float, uint16_t, float, uint16_t, float, uint16_t, float, uint16_t>,
    RepeatAsTypeList<double, 4>, TypeList<float, float, bool, uint8_t>
>>;

using HighCardinalityStringLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint32_t>, RepeatAsTypeList<std::string, 48>
>>;

using EventLogLayoutStatic = bcsv::LayoutStatic<
    uint64_t, uint64_t,
    std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string,
    float, uint32_t, uint16_t,
    bool, bool,
    double, double, double, double, double, double, double, double,
    uint32_t, uint32_t, uint32_t, uint32_t
>;

using IotFleetLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint64_t,
             std::string, std::string, std::string, std::string, std::string, std::string,
             double, float, float,
             uint8_t, int8_t, uint32_t, uint64_t,
             bool, bool>,
    RepeatAsTypeList<float, 8>
>>;

using FinancialOrdersLayoutStatic = bcsv::LayoutStatic<
    uint64_t, uint64_t,
    std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string,
    double, uint32_t, double, uint32_t, float,
    bool, bool, bool,
    double, double, float,
    uint64_t
>;

using RealisticMeasurementLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint64_t, std::string, std::string, std::string, uint8_t>,
    RepeatAsTypeList<float, 8>, RepeatAsTypeList<double, 8>, RepeatAsTypeList<int32_t, 8>,
    RepeatAsTypeList<bool, 4>, TypeList<uint32_t, uint32_t, uint32_t, uint32_t>
>>;

enum class TrackingSelection { Both, Enabled, Disabled };
enum class StorageSelection { Both, Flexible, Static };
enum class CodecSelection { Both, Dense, ZoH };

struct ModeSelection {
    TrackingSelection tracking = TrackingSelection::Both;
    StorageSelection storage = StorageSelection::Both;
    CodecSelection codec = CodecSelection::Both;
    size_t compressionLevel = 1;
};

// Global compression level for benchmark functions.
// Avoids threading through every template-dispatched call chain.
static size_t g_compression_level = 1;

struct ProfileCapabilities {
    bool hasStaticLayoutDispatch = false;
    bool supportsTrackedFlexibleNoCopy = false;
    bool supportsStaticNoCopy = false;
};

inline ProfileCapabilities capabilitiesForProfileName(std::string_view profileName) {
    if (profileName == "mixed_generic" || profileName == "sparse_events") {
        return {true, true, true};
    }

    if (profileName == "sensor_noisy"
        || profileName == "string_heavy"
        || profileName == "simulation_smooth"
        || profileName == "weather_timeseries"
        || profileName == "high_cardinality_string"
        || profileName == "event_log"
        || profileName == "iot_fleet"
        || profileName == "financial_orders"
        || profileName == "realistic_measurement") {
        return {true, false, false};
    }

    return {false, false, false};
}

inline bool includesTrackingEnabled(TrackingSelection s) {
    return s == TrackingSelection::Both || s == TrackingSelection::Enabled;
}

inline bool includesTrackingDisabled(TrackingSelection s) {
    return s == TrackingSelection::Both || s == TrackingSelection::Disabled;
}

inline bool includesFlexible(StorageSelection s) {
    return s == StorageSelection::Both || s == StorageSelection::Flexible;
}

inline bool includesStatic(StorageSelection s) {
    return s == StorageSelection::Both || s == StorageSelection::Static;
}

inline bool includesDense(CodecSelection c) {
    return c == CodecSelection::Both || c == CodecSelection::Dense;
}

inline bool includesZoH(CodecSelection c) {
    return c == CodecSelection::Both || c == CodecSelection::ZoH;
}

template<typename Fn>
bool dispatchStaticLayoutForProfile(const bench::DatasetProfile& profile, Fn&& fn) {
    if (profile.name == "mixed_generic") { fn.template operator()<MixedGenericLayoutStatic>(); return true; }
    if (profile.name == "sparse_events") { fn.template operator()<SparseEventsLayoutStatic>(); return true; }
    if (profile.name == "sensor_noisy") { fn.template operator()<SensorNoisyLayoutStatic>(); return true; }
    if (profile.name == "string_heavy") { fn.template operator()<StringHeavyLayoutStatic>(); return true; }
    if (profile.name == "simulation_smooth") { fn.template operator()<SimulationSmoothLayoutStatic>(); return true; }
    if (profile.name == "weather_timeseries") { fn.template operator()<WeatherTimeseriesLayoutStatic>(); return true; }
    if (profile.name == "high_cardinality_string") { fn.template operator()<HighCardinalityStringLayoutStatic>(); return true; }
    if (profile.name == "event_log") { fn.template operator()<EventLogLayoutStatic>(); return true; }
    if (profile.name == "iot_fleet") { fn.template operator()<IotFleetLayoutStatic>(); return true; }
    if (profile.name == "financial_orders") { fn.template operator()<FinancialOrdersLayoutStatic>(); return true; }
    if (profile.name == "realistic_measurement") { fn.template operator()<RealisticMeasurementLayoutStatic>(); return true; }
    return false;
}

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
    const auto capabilities = capabilitiesForProfileName(profile.name);
    return profile.layout.columnCount() <= 128
        && capabilities.hasStaticLayoutDispatch
        && capabilities.supportsStaticNoCopy;
}

bool supportsNoCopyTrackedFlexible(const bench::DatasetProfile& profile) {
    return capabilitiesForProfileName(profile.name).supportsTrackedFlexibleNoCopy;
}

template<typename RowType>
inline void fillMixedGenericRowRandomTyped(RowType& row, size_t rowIndex);

template<typename RowType>
inline void fillMixedGenericRowZoHTyped(RowType& row, size_t rowIndex);

template<typename RowType>
bool generateProfileNonZoHNoCopy(const bench::DatasetProfile& profile, RowType& row, size_t rowIndex) {
    if (profile.name == "mixed_generic") {
        fillMixedGenericRowRandomTyped(row, rowIndex);
        return true;
    }
    if (profile.name == "sparse_events") {
        bench::datagen::fillRowRandom(row, rowIndex, profile.layout);
        return true;
    }
    return false;
}

template<typename RowType>
bool generateProfileZoHNoCopy(const bench::DatasetProfile& profile, RowType& row, size_t rowIndex) {
    if (profile.name == "mixed_generic") {
        fillMixedGenericRowZoHTyped(row, rowIndex);
        return true;
    }
    if (profile.name == "sparse_events") {
        bench::datagen::fillRowTimeSeries(row, rowIndex, profile.layout, 500);
        return true;
    }
    return false;
}

template<size_t Offset, typename RowType, typename Generator, size_t... I>
inline void fillSixTypedImpl(RowType& row, size_t rowIndex, Generator&& generator, std::index_sequence<I...>) {
    (row.set(Offset + I, generator(rowIndex, Offset + I)), ...);
}

template<size_t Offset, typename RowType, typename Generator>
inline void fillSixTyped(RowType& row, size_t rowIndex, Generator&& generator) {
    fillSixTypedImpl<Offset>(row, rowIndex, std::forward<Generator>(generator), std::make_index_sequence<6>{});
}

template<typename RowType>
inline void fillMixedGenericRowRandomTyped(RowType& row, size_t rowIndex) {
    using namespace bench::datagen;
    fillSixTyped<0>(row, rowIndex, [](size_t r, size_t c) { return genBool(r, c); });
    fillSixTyped<6>(row, rowIndex, [](size_t r, size_t c) { return genInt8(r, c); });
    fillSixTyped<12>(row, rowIndex, [](size_t r, size_t c) { return genInt16(r, c); });
    fillSixTyped<18>(row, rowIndex, [](size_t r, size_t c) { return genInt32(r, c); });
    fillSixTyped<24>(row, rowIndex, [](size_t r, size_t c) { return genInt64(r, c); });
    fillSixTyped<30>(row, rowIndex, [](size_t r, size_t c) { return genUInt8(r, c); });
    fillSixTyped<36>(row, rowIndex, [](size_t r, size_t c) { return genUInt16(r, c); });
    fillSixTyped<42>(row, rowIndex, [](size_t r, size_t c) { return genUInt32(r, c); });
    fillSixTyped<48>(row, rowIndex, [](size_t r, size_t c) { return genUInt64(r, c); });
    fillSixTyped<54>(row, rowIndex, [](size_t r, size_t c) { return genFloat(r, c); });
    fillSixTyped<60>(row, rowIndex, [](size_t r, size_t c) { return genDouble(r, c); });
    fillSixTyped<66>(row, rowIndex, [](size_t r, size_t c) { return genString(r, c); });
}

template<typename RowType>
inline void fillMixedGenericRowZoHTyped(RowType& row, size_t rowIndex) {
    using namespace bench::datagen;
    constexpr size_t CHANGE_INTERVAL = 100;
    fillSixTyped<0>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<bool>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<6>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<int8_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<12>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<int16_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<18>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<int32_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<24>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<int64_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<30>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<uint8_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<36>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<uint16_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<42>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<uint32_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<48>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<uint64_t>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<54>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<float>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<60>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeries<double>(r, c, CHANGE_INTERVAL); });
    fillSixTyped<66>(row, rowIndex, [](size_t r, size_t c) { return genTimeSeriesString(r, c, CHANGE_INTERVAL); });
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

TrackingSelection parseTrackingSelection(const std::string& value, std::string& error) {
    if (value.empty() || value == "both") {
        return TrackingSelection::Both;
    }
    if (value == "enabled" || value == "on") {
        return TrackingSelection::Enabled;
    }
    if (value == "disabled" || value == "off") {
        return TrackingSelection::Disabled;
    }
    error = "Unknown --tracking=" + value + " (expected both|enabled|disabled)";
    return TrackingSelection::Both;
}

StorageSelection parseStorageSelection(const std::string& value, std::string& error) {
    if (value.empty() || value == "both") {
        return StorageSelection::Both;
    }
    if (value == "flexible" || value == "flex") {
        return StorageSelection::Flexible;
    }
    if (value == "static") {
        return StorageSelection::Static;
    }
    error = "Unknown --storage=" + value + " (expected both|flexible|static)";
    return StorageSelection::Both;
}

CodecSelection parseCodecSelection(const std::string& value, std::string& error) {
    if (value.empty() || value == "both") {
        return CodecSelection::Both;
    }
    if (value == "dense" || value == "flat") {
        return CodecSelection::Dense;
    }
    if (value == "zoh") {
        return CodecSelection::ZoH;
    }
    error = "Unknown --codec=" + value + " (expected both|dense|zoh)";
    return CodecSelection::Both;
}

std::string makeScenarioDatasetName(const std::string& base, const SparseScenario& scenario) {
    if (scenario.kind == SparseKind::Baseline) {
        return base;
    }
    return base + "::" + scenario.id;
}

std::string makeScenarioRunLabel(const bench::DatasetProfile& profile, const SparseScenario& scenario) {
    return makeScenarioDatasetName(profile.name, scenario);
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

bench::BenchmarkResult makeSkippedResult(const bench::DatasetProfile& profile,
                                         size_t numRows,
                                         const SparseScenario& scenario,
                                         const std::string& mode,
                                         const std::string& reason)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, mode, "deserialize_first");
    result.status = "skipped";
    result.validation_passed = true;
    result.validation_error = "SKIPPED: " + reason;
    return result;
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

void validateRowByScenario(const SparseScenario& scenario,
                           size_t rowIndex,
                           const bcsv::Row& expected,
                           const bcsv::Row& actual,
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
        applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Flexible [trk=off]", "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_flex", ".bcsv");

    // ----- Write -----
    bench::Timer timer;
    {
        bcsv::Writer<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filename, true, g_compression_level)) {
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
        applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Flexible ZoH [trk=on]", "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_flex_zoh", ".bcsv");

    // ----- Write (ZoH codec) -----
    bench::Timer timer;
    {
        bcsv::WriterZoH<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filename, true, g_compression_level, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
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
    bcsv::Row expectedRow(profile.layout);
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;

    {
        bcsv::Reader<bcsv::Layout> reader;
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

bench::BenchmarkResult benchmarkBCSVFlexibleTracked(const bench::DatasetProfile& profile,
                                                    size_t numRows,
                                                    const SparseScenario& scenario,
                                                    bool quiet)
{
    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, "BCSV Flexible [trk=on]", "deserialize_first");
    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + "_flex_trk_on", ".bcsv");

    bench::Timer timer;
    {
        bcsv::WriterZoH<bcsv::Layout> writer(profile.layout);
        if (!writer.open(filename, true, g_compression_level)) {
            result.validation_error = "Cannot open BCSV file: " + writer.getErrorMsg();
            return result;
        }

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            if (!generateProfileNonZoHNoCopy(profile, writer.row(), i)) {
                result.validation_error = "No-copy tracked-flex generator unavailable for profile: " + profile.name;
                writer.close();
                return result;
            }
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
        std::cerr << "  [" << profile.name << "] BCSV Flexible [trk=on] read:  "
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }
    return result;
}

template<typename StaticLayout, bool UseZoH = false>
bench::BenchmarkResult runStaticLayoutVariant(const bench::DatasetProfile& profile,
                                              size_t numRows,
                                              const SparseScenario& scenario,
                                              bool quiet,
                                              const std::string& modeLabel,
                                              const std::string& suffix)
{
    using WriterType = std::conditional_t<UseZoH,
        bcsv::WriterZoH<StaticLayout>, bcsv::Writer<StaticLayout>>;

    bench::BenchmarkResult result;
    applyScenarioMetadata(result, profile, numRows, scenario, modeLabel, "deserialize_first");

    const std::string filename = bench::tempFilePath(profile.name + scenarioFileTag(scenario) + suffix, ".bcsv");
    bench::Timer timer;

    {
        StaticLayout layoutStatic;
        layoutStatic = profile.layout;
        WriterType writer(layoutStatic);
        const bool opened = UseZoH
            ? writer.open(filename, true, g_compression_level, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)
            : writer.open(filename, true, g_compression_level);
        if (!opened) {
            result.validation_error = "Cannot open BCSV Static file: " + writer.getErrorMsg();
            return result;
        }

        timer.start();
        for (size_t i = 0; i < numRows; ++i) {
            auto& row = writer.row();
            const bool generated = UseZoH
                ? generateProfileZoHNoCopy(profile, row, i)
                : generateProfileNonZoHNoCopy(profile, row, i);
            if (!generated) {
                result.validation_error = "No-copy static generator unavailable for profile: " + profile.name;
                writer.close();
                return result;
            }
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
    bcsv::Row expectedZoHRow(profile.layout);
    const auto selectedColumns = buildSelectedColumns(profile.layout, scenario.columns_k);
    const auto predicateColumn = findFirstNumericColumn(profile.layout);
    size_t processedRows = 0;
    bool validationOk = true;
    std::string firstError;

    {
        StaticLayout layoutStatic;
        layoutStatic = profile.layout;
        bcsv::Reader<StaticLayout> reader;
        if (!reader.open(filename)) {
            result.validation_error = "Cannot read BCSV Static file: " + reader.getErrorMsg();
            return result;
        }

        size_t rowsRead = 0;
        timer.start();
        while (reader.readNext()) {
            const auto& row = reader.row();
            if constexpr (UseZoH) {
                if (!generateProfileZoHNoCopy(profile, expectedZoHRow, rowsRead)) {
                    result.validation_error = "No-copy static expected-row generator unavailable for profile: " + profile.name;
                    reader.close();
                    return result;
                }
                if (shouldProcessRow(scenario, rowsRead, expectedZoHRow, profile.layout, predicateColumn)) {
                    std::string err;
                    if (!validateRowByScenarioExact(scenario, rowsRead, expectedZoHRow, row, profile.layout,
                                                    selectedColumns, err)) {
                        validationOk = false;
                        if (firstError.empty()) firstError = err;
                    }
                    ++processedRows;
                }
            } else {
                if (!generateProfileNonZoHNoCopy(profile, expectedRow, rowsRead)) {
                    result.validation_error = "No-copy static expected-row generator unavailable for profile: " + profile.name;
                    reader.close();
                    return result;
                }
                if (shouldProcessRow(scenario, rowsRead, expectedRow, profile.layout, predicateColumn)) {
                    std::string err;
                    if (!validateRowByScenarioExact(scenario, rowsRead, expectedRow, row, profile.layout,
                                                    selectedColumns, err)) {
                        validationOk = false;
                        if (firstError.empty()) firstError = err;
                    }
                    ++processedRows;
                }
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
        std::cerr << "  [" << profile.name << "] " << modeLabel << " read: "
                  << std::fixed << std::setprecision(1) << result.read_time_ms << " ms"
                  << " — " << (result.validation_passed ? "PASS" : "FAIL") << "\n";
    }
    return result;
}

bench::BenchmarkResult benchmarkBCSVStaticVariant(const bench::DatasetProfile& profile,
                                                  size_t numRows,
                                                  const SparseScenario& scenario,
                                                  bool quiet,
                                                  const std::string& modeLabel,
                                                  const std::string& suffix,
                                                  bool useZoH)
{
    bench::BenchmarkResult result;
    bool dispatched = false;

    if (useZoH) {
        dispatched = dispatchStaticLayoutForProfile(profile, [&]<typename StaticLayout>() {
            result = runStaticLayoutVariant<StaticLayout, true>(
                profile, numRows, scenario, quiet, modeLabel, suffix);
        });
    } else {
        dispatched = dispatchStaticLayoutForProfile(profile, [&]<typename StaticLayout>() {
            result = runStaticLayoutVariant<StaticLayout, false>(
                profile, numRows, scenario, quiet, modeLabel, suffix);
        });
    }

    if (!dispatched) {
        applyScenarioMetadata(result, profile, numRows, scenario, modeLabel, "deserialize_first");
        result.status = "error";
        result.validation_error = "Static layout dispatch unavailable for profile: " + profile.name;
    }

    return result;
}

bench::BenchmarkResult benchmarkBCSVStatic(const bench::DatasetProfile& profile,
                                           size_t numRows,
                                           const SparseScenario& scenario,
                                           bool quiet)
{
    return benchmarkBCSVStaticVariant(profile, numRows, scenario, quiet, "BCSV Static", "_static", false);
}

bench::BenchmarkResult benchmarkBCSVStaticTracked(const bench::DatasetProfile& profile,
                                                  size_t numRows,
                                                  const SparseScenario& scenario,
                                                  bool quiet)
{
    return benchmarkBCSVStaticVariant(profile, numRows, scenario, quiet, "BCSV Static", "_static", false);
}

bench::BenchmarkResult benchmarkBCSVStaticZoH(const bench::DatasetProfile& profile,
                                              size_t numRows,
                                              const SparseScenario& scenario,
                                              bool quiet)
{
    return benchmarkBCSVStaticVariant(profile, numRows, scenario, quiet, "BCSV Static ZoH", "_static_zoh", true);
}

/// Run all benchmarks for a single dataset profile
std::vector<bench::BenchmarkResult> benchmarkProfile(const bench::DatasetProfile& profile,
                                                      size_t numRows,
                                                      bool quiet,
                                                      const std::vector<SparseScenario>& scenarios,
                                                      const ModeSelection& modeSelection)
{
    std::vector<bench::BenchmarkResult> results;

    if (!quiet) {
        const auto capabilities = capabilitiesForProfileName(profile.name);
        std::cerr << "\n=== Dataset: " << profile.name << " ===\n"
                  << "  " << profile.description << "\n"
                  << "  Rows: " << numRows 
                  << "  Columns: " << profile.layout.columnCount() << "\n"
                  << "  Capabilities: tracked-flex(no-copy)="
                  << (capabilities.supportsTrackedFlexibleNoCopy ? "yes" : "no")
                  << ", static(no-copy)="
                  << ((capabilities.supportsStaticNoCopy && profile.layout.columnCount() <= 128) ? "yes" : "no")
                  << " (layout<=128=" << (profile.layout.columnCount() <= 128 ? "yes" : "no") << ")\n\n";
    }

    for (const auto& scenario : scenarios) {
        bench::BenchmarkResult csvResult;
        bool hasCsvBaseline = false;
        if (modeSelection.storage == StorageSelection::Both) {
            csvResult = benchmarkCSV(profile, numRows, scenario, quiet);
            results.push_back(csvResult);
            hasCsvBaseline = true;
        }

        if (includesFlexible(modeSelection.storage)) {
            if (includesTrackingDisabled(modeSelection.tracking)) {
                if (includesDense(modeSelection.codec)) {
                    auto flexResult = benchmarkBCSVFlexible(profile, numRows, scenario, quiet);
                    if (hasCsvBaseline && csvResult.file_size > 0) {
                        flexResult.compression_ratio = static_cast<double>(flexResult.file_size) / csvResult.file_size;
                    }
                    results.push_back(flexResult);
                }
            }

            if (includesTrackingEnabled(modeSelection.tracking)) {
                if (includesDense(modeSelection.codec)) {
                    if (supportsNoCopyTrackedFlexible(profile)) {
                        auto flexTrackedResult = benchmarkBCSVFlexibleTracked(profile, numRows, scenario, quiet);
                        if (hasCsvBaseline && csvResult.file_size > 0) {
                            flexTrackedResult.compression_ratio = static_cast<double>(flexTrackedResult.file_size) / csvResult.file_size;
                        }
                        results.push_back(flexTrackedResult);
                    } else {
                        results.push_back(makeSkippedResult(
                            profile,
                            numRows,
                            scenario,
                            "BCSV Flexible [trk=on]",
                            "no-copy generator unavailable"));
                        if (!quiet) {
                            std::cerr << "  [" << makeScenarioRunLabel(profile, scenario)
                                      << "] skip BCSV Flexible [trk=on]: no-copy generator unavailable\n";
                        }
                    }
                }

                if (includesZoH(modeSelection.codec)) {
                    auto zohResult = benchmarkBCSVFlexibleZoH(profile, numRows, scenario, quiet);
                    if (hasCsvBaseline && csvResult.file_size > 0) {
                        zohResult.compression_ratio = static_cast<double>(zohResult.file_size) / csvResult.file_size;
                    }
                    results.push_back(zohResult);
                }
            }
        }

        if (includesStatic(modeSelection.storage) && supportsStaticMode(profile)) {
            if (includesTrackingDisabled(modeSelection.tracking)) {
                if (includesDense(modeSelection.codec)) {
                    auto staticResult = benchmarkBCSVStatic(profile, numRows, scenario, quiet);
                    if (hasCsvBaseline && csvResult.file_size > 0) {
                        staticResult.compression_ratio = static_cast<double>(staticResult.file_size) / csvResult.file_size;
                    }
                    results.push_back(staticResult);
                }
            }

            if (includesTrackingEnabled(modeSelection.tracking)) {
                if (includesDense(modeSelection.codec)) {
                    auto staticTrackedResult = benchmarkBCSVStaticTracked(profile, numRows, scenario, quiet);
                    if (hasCsvBaseline && csvResult.file_size > 0) {
                        staticTrackedResult.compression_ratio = static_cast<double>(staticTrackedResult.file_size) / csvResult.file_size;
                    }
                    results.push_back(staticTrackedResult);
                }

                if (includesZoH(modeSelection.codec)) {
                    auto staticZoHResult = benchmarkBCSVStaticZoH(profile, numRows, scenario, quiet);
                    if (hasCsvBaseline && csvResult.file_size > 0) {
                        staticZoHResult.compression_ratio = static_cast<double>(staticZoHResult.file_size) / csvResult.file_size;
                    }
                    results.push_back(staticZoHResult);
                }
            }
        } else if (includesStatic(modeSelection.storage)) {
            const std::string staticSkipReason = "no-copy static generator unavailable or layout >128 cols";
            if (includesTrackingDisabled(modeSelection.tracking) && includesDense(modeSelection.codec)) {
                results.push_back(makeSkippedResult(
                    profile,
                    numRows,
                    scenario,
                    "BCSV Static [trk=off]",
                    staticSkipReason));
            }
            if (includesTrackingEnabled(modeSelection.tracking) && includesDense(modeSelection.codec)) {
                results.push_back(makeSkippedResult(
                    profile,
                    numRows,
                    scenario,
                    "BCSV Static [trk=on]",
                    staticSkipReason));
            }
            if (includesTrackingEnabled(modeSelection.tracking) && includesZoH(modeSelection.codec)) {
                results.push_back(makeSkippedResult(
                    profile,
                    numRows,
                    scenario,
                    "BCSV Static ZoH [trk=on]",
                    staticSkipReason));
            }
            if (!quiet) {
                std::cerr << "  [" << makeScenarioRunLabel(profile, scenario)
                          << "] skip static modes: " << staticSkipReason << "\n";
            }
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
    const auto profileNames = bench::getProfileNames();

    if (bench::hasArg(args, "help") || bench::hasArg(args, "h")) {
        std::cout
            << "BCSV Macro Benchmark Suite\n\n"
            << "Usage:\n"
            << "  bench_macro_datasets [options]\n\n"
            << "Options:\n"
            << "  --rows=N\n"
            << "  --size=S|M|L|XL\n"
            << "  --output=PATH\n"
            << "  --profile=NAME\n"
            << "  --scenario=LIST\n"
            << "  --tracking=both|enabled|disabled   (default: both)\n"
            << "  --storage=both|flexible|static     (default: both)\n"
            << "  --codec=both|dense|zoh            (default: both)\n"
            << "  --compression=N                   LZ4 compression level 1-9 (default: 1; 1=fast, 9=best ratio)\n"
            << "  --list\n"
            << "  --list-scenarios\n"
            << "  --quiet\n"
            << "  --no-cleanup\n"
            << "  --build-type=Debug|Release\n\n"
            << "Examples:\n"
            << "  bench_macro_datasets --profile=rtl_waveform --rows=10000 --tracking=enabled\n"
            << "  bench_macro_datasets --storage=static --scenario=baseline,sparse_columns_k1\n"
            << "  bench_macro_datasets --tracking=disabled --storage=flexible --codec=dense\n"
            << "  bench_macro_datasets --tracking=enabled --storage=static --codec=zoh\n\n"
            << "Profiles (" << profileNames.size() << "):\n";
        for (const auto& name : profileNames) {
            std::cout << "  - " << name << "\n";
        }
        std::cout << "\nScenarios (" << allScenarios.size() << "):\n";
        for (const auto& s : allScenarios) {
            std::cout << "  - " << s.id << "\n";
        }
        std::cout << "\n";
        return 0;
    }

    // --list: print profile names and exit
    if (bench::hasArg(args, "list")) {
        for (const auto& name : profileNames) {
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
    std::string trackingFilter = bench::getArgString(args, "tracking", "both");
    std::string storageFilter = bench::getArgString(args, "storage", "both");
    std::string codecFilter = bench::getArgString(args, "codec", "both");
    bool quiet = bench::hasArg(args, "quiet");
    bool noCleanup = bench::hasArg(args, "no-cleanup");
    std::string buildType = bench::getArgString(args, "build-type", "Release");

    // Parse --compression=N (1-9, default 1)
    size_t compressionLevelArg = bench::getArgSizeT(args, "compression", 1);
    if (compressionLevelArg < 1 || compressionLevelArg > 9) {
        std::cerr << "ERROR: --compression must be 1-9 (got " << compressionLevelArg << ")\n";
        return 1;
    }
    g_compression_level = compressionLevelArg;

    ModeSelection modeSelection;
    std::string modeError;
    modeSelection.tracking = parseTrackingSelection(trackingFilter, modeError);
    if (!modeError.empty()) {
        std::cerr << "ERROR: " << modeError << "\n";
        return 1;
    }
    modeSelection.storage = parseStorageSelection(storageFilter, modeError);
    if (!modeError.empty()) {
        std::cerr << "ERROR: " << modeError << "\n";
        return 1;
    }
    modeSelection.codec = parseCodecSelection(codecFilter, modeError);
    if (!modeError.empty()) {
        std::cerr << "ERROR: " << modeError << "\n";
        return 1;
    }
    if (modeSelection.codec == CodecSelection::ZoH
        && modeSelection.tracking == TrackingSelection::Disabled) {
        std::cerr << "ERROR: --codec=zoh requires --tracking=enabled or --tracking=both\n";
        return 1;
    }

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
            for (const auto& n : profileNames) std::cerr << n << " ";
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
                  << "Tracking: " << trackingFilter << "\n"
                  << "Storage: " << storageFilter << "\n"
                  << "Codec: " << codecFilter << "\n"
                  << "Compression: " << g_compression_level << "\n"
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
            benchmarkProfile(profiles.front(), 100, /*quiet=*/true, {scenarios.front()}, modeSelection);
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
            auto results = benchmarkProfile(profile, numRows, quiet, scenarios, modeSelection);
            allResults.insert(allResults.end(), results.begin(), results.end());
        } catch (const std::exception& e) {
            std::cerr << "ERROR in profile " << profile.name << ": " << e.what() << "\n";
            bench::BenchmarkResult errorResult;
            errorResult.dataset_name = profile.name;
            errorResult.mode = "ERROR";
            errorResult.status = "error";
            errorResult.validation_error = e.what();
            allResults.push_back(errorResult);
        }

        if (!noCleanup) {
            cleanupProfile(profile);
        }
    }

    totalTimer.stop();

    for (auto& result : allResults) {
        if (result.status == "ok" && result.validation_error.rfind("SKIPPED:", 0) == 0) {
            result.status = "skipped";
        }
        if (result.status == "ok" && !result.validation_passed) {
            result.status = "error";
        }
    }

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
