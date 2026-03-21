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
 * @file test_datasets.hpp
 * @brief Dataset profile definitions for the BCSV test and benchmark suite
 * 
 * Each DatasetProfile defines:
 * - A Layout factory (column names, types)
 * - A data generator (deterministic, reproducible)
 * - A description of what use-case it represents
 * 
 * Profiles:
 * 1. mixed_generic           — 72 columns, all 12 types, random data (baseline)
 * 2. sparse_events           — 100 columns, ~1% activity (ZoH best-case)
 * 3. sensor_noisy            — 50 float/double columns, Gaussian noise
 * 4. string_heavy            — 20 string + 10 scalar columns, varied cardinality
 * 5. bool_heavy              — 128 bool + 4 scalar columns, bitset performance
 * 6. arithmetic_wide         — 200 numeric columns, no strings, ZoH worst-case
 * 7. simulation_smooth       — 100 float/double columns, slow linear drift
 * 8. weather_timeseries      — 40 columns, realistic weather pattern
 * 9. high_cardinality_string — 50 string columns, near-unique UUIDs
 * 10. realistic_measurement  — DAQ session: phases, mixed sensor rates, static metadata
 * 11. rtl_waveform           — RTL simulation: 256 bools + uint registers, clock + timer
 * 12. event_log              — backend event stream, 8 low-cardinality categorical strings
 * 13. iot_fleet              — fleet telemetry, round-robin devices with bounded metadata vocab
 * 14. financial_orders       — order/trade feed with 8 categorical strings per event
 * 15. measurement_campaign   — 84 columns, SIMD-grouped float/double, measurement campaign time-series
 */

#include <bcsv/bcsv.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

namespace bench {

// ============================================================================
// DatasetProfile — describes a benchmark scenario
// ============================================================================

/// Callback type: populate one row given its index
using RowGenerator = std::function<void(bcsv::Row& row, size_t rowIndex)>;

/// Callback type for generating time-series data (measurement campaign pattern)
using RowGeneratorTimeSeries = std::function<void(bcsv::Row& row, size_t rowIndex)>;

struct DatasetProfile {
    std::string  name;
    std::string  description;
    bcsv::Layout layout;
    size_t       default_rows;     // recommended number of rows for "full" benchmark
    RowGenerator generate;         // random/volatile data (worst-case for ZoH/Delta)
    RowGeneratorTimeSeries generateTimeSeries;   // measurement campaign data (mixed codec characteristics)
};

// ============================================================================
// Deterministic hash helpers (from original TestDataGenerator)
// ============================================================================

namespace datagen {

// ----- Hash functions — deterministic, fast, no state -----

constexpr uint64_t hash64(size_t row, size_t col) {
    return (row * 6364136223846793005ULL) ^ (col * 1442695040888963407ULL);
}

constexpr uint32_t hash32(size_t row, size_t col) {
    return static_cast<uint32_t>(row * 2654435761ULL + col * 1597334677ULL);
}

// Simple xoshiro-like mixer for better distribution
constexpr uint64_t mix(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// ----- Type-specific generators -----

constexpr bool genBool(size_t row, size_t col) {
    return (hash64(row, col) & 1) != 0;
}

constexpr int8_t genInt8(size_t row, size_t col) {
    return static_cast<int8_t>(hash32(row, col));
}

constexpr int16_t genInt16(size_t row, size_t col) {
    return static_cast<int16_t>(row * 1000003ULL + col * 7919ULL);
}

constexpr int32_t genInt32(size_t row, size_t col) {
    return static_cast<int32_t>(hash32(row, col));
}

constexpr int64_t genInt64(size_t row, size_t col) {
    return static_cast<int64_t>(hash64(row, col));
}

constexpr uint8_t genUInt8(size_t row, size_t col) {
    return static_cast<uint8_t>(row * 7919ULL + col * 6947ULL);
}

constexpr uint16_t genUInt16(size_t row, size_t col) {
    return static_cast<uint16_t>(row * 48271ULL + col * 22695477ULL);
}

constexpr uint32_t genUInt32(size_t row, size_t col) {
    return static_cast<uint32_t>(row * 1597334677ULL + col * 2654435761ULL);
}

constexpr uint64_t genUInt64(size_t row, size_t col) {
    return (row * 11400714819323198485ULL) ^ (col * 14029467366897019727ULL);
}

constexpr float genFloat(size_t row, size_t col) {
    uint32_t h = hash32(row, col);
    return static_cast<float>(static_cast<int32_t>(h % 2000000U) - 1000000) / 1000.0f;
}

constexpr double genDouble(size_t row, size_t col) {
    uint64_t h = hash64(row, col);
    return static_cast<double>(static_cast<int64_t>(h % 20000000ULL) - 10000000) / 1000.0;
}

inline std::string genString(size_t row, size_t col, size_t maxLen = 48) {
    uint64_t h = genUInt64(row, col);
    size_t len = (h % maxLen) + 1;
    std::string result(len, 'A');
    char base = 'A' + static_cast<char>(h % 26);
    for (size_t i = 0; i < len; ++i) {
        result[i] = static_cast<char>(base + (i % 26));
    }
    return result;
}

// ----- UUID generator (deterministic, 8-4-4-4-12 hex format) -----
inline std::string genUuid(size_t row, size_t col) {
    uint64_t h1 = mix(hash64(row, col));
    uint64_t h2 = mix(h1 ^ col);
    char buf[37];
    snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
             static_cast<uint32_t>(h1),
             static_cast<uint16_t>(h1 >> 32),
             static_cast<uint16_t>((h1 >> 48) | 0x4000),  // version 4
             static_cast<uint16_t>((h2 & 0x3FFF) | 0x8000),  // variant 1
             static_cast<unsigned long long>(h2 >> 16));
    return std::string(buf);
}

// ----- Gaussian noise approximation (Box-Muller, deterministic seed) -----
inline double gaussianNoise(size_t row, size_t col, double mean, double stddev) {
    uint64_t h1 = mix(hash64(row, col * 2));
    uint64_t h2 = mix(hash64(row, col * 2 + 1));
    // Map to (0,1) range
    double u1 = (static_cast<double>(h1 >> 11) + 0.5) / static_cast<double>(1ULL << 53);
    double u2 = (static_cast<double>(h2 >> 11) + 0.5) / static_cast<double>(1ULL << 53);
    // Box-Muller transform
    double z0 = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
    return mean + z0 * stddev;
}

// ----- Measurement campaign helpers -----

/// Returns true if the given row is in the active phase of its measurement cycle.
/// Active phase is the first 70% of each cycle; standstill is the last 30%.
constexpr bool isActive(size_t row, size_t cycleLength) {
    return (row % cycleLength) < (cycleLength * 7 / 10);
}

/// Returns the effective row index for generation:
/// During active phase, returns the actual row.
/// During standstill, returns the last row of the active phase (values freeze).
constexpr size_t effectiveRow(size_t row, size_t cycleLength) {
    const size_t activeLen = cycleLength * 7 / 10;
    const size_t cycle = row / cycleLength;
    const size_t posInCycle = row % cycleLength;
    if (posInCycle < activeLen) return row;
    return cycle * cycleLength + activeLen - 1;
}

// ----- Time-series generators (measurement campaign pattern) -----
// Each cycle has active (70%) and standstill (30%) phases.
// Active: monotonic counters, linear drift + jitter on floats → Delta FoC excels
// Standstill: all values frozen at last active value → ZoH excels

template<typename T>
inline T genTimeSeries(size_t row, size_t col, size_t cycleLength = 100) {
    const size_t activeLen = cycleLength * 7 / 10;
    const size_t cycle = row / cycleLength;
    const size_t posInCycle = row % cycleLength;
    const bool active = posInCycle < activeLen;
    const size_t effPos = active ? posInCycle : (activeLen > 0 ? activeLen - 1 : 0);

    if constexpr (std::is_same_v<T, bool>) {
        // Toggle per segment within active phase, frozen during standstill
        size_t segment = effPos / (cycleLength > 20 ? cycleLength / 10 : 5);
        return ((segment + col + cycle) % 3) == 0;
    } else if constexpr (std::is_same_v<T, float>) {
        // Linear drift during active + small jitter, frozen during standstill
        float base = 50.0f + static_cast<float>(col) * 10.0f + static_cast<float>(cycle) * 5.0f;
        float drift = static_cast<float>(effPos) * 0.01f;
        float jitter = active
            ? static_cast<float>(static_cast<int32_t>(mix(hash64(row, col)) % 100) - 50) * 0.001f
            : 0.0f;
        return base + drift + jitter;
    } else if constexpr (std::is_same_v<T, double>) {
        double base = 100.0 + static_cast<double>(col) * 25.0 + static_cast<double>(cycle) * 10.0;
        double drift = static_cast<double>(effPos) * 0.005;
        double jitter = active
            ? static_cast<double>(static_cast<int64_t>(mix(hash64(row, col)) % 200) - 100) * 0.0001
            : 0.0;
        return base + drift + jitter;
    } else if constexpr (std::is_unsigned_v<T>) {
        // Monotonic counter during active, frozen during standstill
        T base = static_cast<T>(cycle * (activeLen > 0 ? activeLen : 1) + col * 1000);
        return static_cast<T>(base + effPos);
    } else {
        // Signed integer: monotonic increment with col-dependent offset
        auto base = static_cast<int64_t>(cycle) * static_cast<int64_t>(activeLen > 0 ? activeLen : 1)
                   + static_cast<int64_t>(col) * 100;
        return static_cast<T>(base + static_cast<int64_t>(effPos));
    }
}

inline std::string genTimeSeriesString(size_t row, size_t col, size_t cycleLength = 100) {
    const char* categories[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta",
                                 "Eta", "Theta", "Iota", "Kappa"};
    size_t effRow = effectiveRow(row, cycleLength);
    size_t segment = effRow / (cycleLength > 20 ? cycleLength / 10 : 5);
    return categories[(segment + col) % 10];
}

/// Fill a row with random data based on its layout (generic, works with any layout)
template<typename RowType>
inline void fillRowRandom(RowType& row, size_t rowIndex, const bcsv::Layout& layout) {
    for (size_t col = 0; col < layout.columnCount(); ++col) {
        switch (layout.columnType(col)) {
            case bcsv::ColumnType::BOOL:    row.set(col, genBool(rowIndex, col)); break;
            case bcsv::ColumnType::INT8:    row.set(col, genInt8(rowIndex, col)); break;
            case bcsv::ColumnType::INT16:   row.set(col, genInt16(rowIndex, col)); break;
            case bcsv::ColumnType::INT32:   row.set(col, genInt32(rowIndex, col)); break;
            case bcsv::ColumnType::INT64:   row.set(col, genInt64(rowIndex, col)); break;
            case bcsv::ColumnType::UINT8:   row.set(col, genUInt8(rowIndex, col)); break;
            case bcsv::ColumnType::UINT16:  row.set(col, genUInt16(rowIndex, col)); break;
            case bcsv::ColumnType::UINT32:  row.set(col, genUInt32(rowIndex, col)); break;
            case bcsv::ColumnType::UINT64:  row.set(col, genUInt64(rowIndex, col)); break;
            case bcsv::ColumnType::FLOAT:   row.set(col, genFloat(rowIndex, col)); break;
            case bcsv::ColumnType::DOUBLE:  row.set(col, genDouble(rowIndex, col)); break;
            case bcsv::ColumnType::STRING:  row.set(col, genString(rowIndex, col)); break;
            default: break;
        }
    }
}

/// Fill a row with time-series data (measurement campaign pattern)
template<typename RowType>
inline void fillRowTimeSeries(RowType& row, size_t rowIndex, const bcsv::Layout& layout, 
                               size_t cycleLength = 100) {
    for (size_t col = 0; col < layout.columnCount(); ++col) {
        switch (layout.columnType(col)) {
            case bcsv::ColumnType::BOOL:    row.set(col, genTimeSeries<bool>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::INT8:    row.set(col, genTimeSeries<int8_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::INT16:   row.set(col, genTimeSeries<int16_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::INT32:   row.set(col, genTimeSeries<int32_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::INT64:   row.set(col, genTimeSeries<int64_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::UINT8:   row.set(col, genTimeSeries<uint8_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::UINT16:  row.set(col, genTimeSeries<uint16_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::UINT32:  row.set(col, genTimeSeries<uint32_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::UINT64:  row.set(col, genTimeSeries<uint64_t>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::FLOAT:   row.set(col, genTimeSeries<float>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::DOUBLE:  row.set(col, genTimeSeries<double>(rowIndex, col, cycleLength)); break;
            case bcsv::ColumnType::STRING:  row.set(col, genTimeSeriesString(rowIndex, col, cycleLength)); break;
            default: break;
        }
    }
}

} // namespace datagen

// ============================================================================
// Profile 1: mixed_generic — all 12 types, 72 columns, random data
// ============================================================================

inline DatasetProfile createMixedGenericProfile() {
    DatasetProfile p;
    p.name = "mixed_generic";
    p.description = "72 columns (6 per type x 12 types), random data — baseline benchmark";
    p.default_rows = 500000;

    const std::vector<std::string> typeNames = {
        "bool", "int8", "int16", "int32", "int64", "uint8",
        "uint16", "uint32", "uint64", "float", "double", "string"
    };
    const std::vector<bcsv::ColumnType> types = {
        bcsv::ColumnType::BOOL, bcsv::ColumnType::INT8, bcsv::ColumnType::INT16,
        bcsv::ColumnType::INT32, bcsv::ColumnType::INT64, bcsv::ColumnType::UINT8,
        bcsv::ColumnType::UINT16, bcsv::ColumnType::UINT32, bcsv::ColumnType::UINT64,
        bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE, bcsv::ColumnType::STRING
    };

    for (size_t t = 0; t < types.size(); ++t) {
        for (size_t c = 0; c < 6; ++c) {
            p.layout.addColumn({typeNames[t] + "_" + std::to_string(c), types[t]});
        }
    }

    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowRandom(row, rowIndex, layout);
    };

    p.generateTimeSeries = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowTimeSeries(row, rowIndex, layout, 100);
    };

    return p;
}

// ============================================================================
// Profile 2: sparse_events — 100 columns, ~1% activity
// ============================================================================

inline DatasetProfile createSparseEventsProfile() {
    DatasetProfile p;
    p.name = "sparse_events";
    p.description = "100 mixed columns, ~1% rows have changes — ZoH best-case scenario";
    p.default_rows = 500000;

    // 20 bool + 30 int32 + 20 float + 20 double + 10 string
    for (size_t i = 0; i < 20; ++i)
        p.layout.addColumn({"event_" + std::to_string(i), bcsv::ColumnType::BOOL});
    for (size_t i = 0; i < 30; ++i)
        p.layout.addColumn({"counter_" + std::to_string(i), bcsv::ColumnType::INT32});
    for (size_t i = 0; i < 20; ++i)
        p.layout.addColumn({"measure_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    for (size_t i = 0; i < 20; ++i)
        p.layout.addColumn({"precision_" + std::to_string(i), bcsv::ColumnType::DOUBLE});
    for (size_t i = 0; i < 10; ++i)
        p.layout.addColumn({"label_" + std::to_string(i), bcsv::ColumnType::STRING});

    // Random: all values change every row
    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowRandom(row, rowIndex, layout);
    };

    // Sparse time-series: long 500-row cycles (350 active + 150 standstill)
    p.generateTimeSeries = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowTimeSeries(row, rowIndex, layout, 500);
    };

    return p;
}

// ============================================================================
// Profile 3: sensor_noisy — 50 float/double columns with Gaussian noise
// ============================================================================

inline DatasetProfile createSensorNoisyProfile() {
    DatasetProfile p;
    p.name = "sensor_noisy";
    p.description = "50 float/double sensor channels with Gaussian noise + occasional outages";
    p.default_rows = 500000;

    // 2 timestamp columns + 24 float sensors + 24 double sensors
    p.layout.addColumn({"timestamp", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"sample_id", bcsv::ColumnType::UINT32});
    for (size_t i = 0; i < 24; ++i)
        p.layout.addColumn({"sensor_f_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    for (size_t i = 0; i < 24; ++i)
        p.layout.addColumn({"sensor_d_" + std::to_string(i), bcsv::ColumnType::DOUBLE});

    // Random: different noise levels per column group
    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        // Timestamps
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(1640995200000ULL + rowIndex * 1000));
        row.set(static_cast<size_t>(1), static_cast<uint32_t>(rowIndex));

        // Float sensors: 3 noise levels
        for (size_t i = 0; i < 24; ++i) {
            size_t col = 2 + i;
            double baseValue = 20.0 + i * 5.0; // different base per sensor
            double stddev = (i < 8) ? 0.01 : (i < 16) ? 0.5 : 5.0; // low/med/high noise
            
            // Occasional outage (stuck at zero) — ~0.1% of samples
            uint64_t h = datagen::mix(datagen::hash64(rowIndex, col));
            bool outage = (h % 1000) == 0;
            
            float val = outage ? 0.0f : static_cast<float>(datagen::gaussianNoise(rowIndex, col, baseValue, stddev));
            row.set(col, val);
        }

        // Double sensors: same pattern
        for (size_t i = 0; i < 24; ++i) {
            size_t col = 26 + i;
            double baseValue = 100.0 + i * 10.0;
            double stddev = (i < 8) ? 0.001 : (i < 16) ? 1.0 : 10.0;
            
            uint64_t h = datagen::mix(datagen::hash64(rowIndex, col));
            bool outage = (h % 1000) == 0;
            
            double val = outage ? 0.0 : datagen::gaussianNoise(rowIndex, col, baseValue, stddev);
            row.set(col, val);
        }
    };

    // Measurement campaign: 2000-row cycles with active drift and standstill freeze
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 2000;
        const bool active = datagen::isActive(rowIndex, cycleLen);
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        // Timestamp: always monotonic. Counter: monotonic during active, frozen during standstill.
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(1640995200000ULL + rowIndex * 1000));
        row.set(static_cast<size_t>(1), static_cast<uint32_t>(effRow));

        // Float sensors: linear drift + small jitter during active, frozen during standstill
        for (size_t i = 0; i < 24; ++i) {
            size_t col = 2 + i;
            float base = 20.0f + static_cast<float>(i) * 5.0f;
            float drift = static_cast<float>(effRow % 10000) * 0.001f;
            float jitter = active
                ? static_cast<float>(static_cast<int32_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 100) - 50) * 0.01f
                : 0.0f;
            row.set(col, base + drift + jitter);
        }

        // Double sensors: same pattern with finer drift
        for (size_t i = 0; i < 24; ++i) {
            size_t col = 26 + i;
            double base = 100.0 + static_cast<double>(i) * 10.0;
            double drift = static_cast<double>(effRow % 10000) * 0.0005;
            double jitter = active
                ? static_cast<double>(static_cast<int64_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 200) - 100) * 0.001
                : 0.0;
            row.set(col, base + drift + jitter);
        }
    };

    return p;
}

// ============================================================================
// Profile 4: string_heavy — 20 string + 10 scalar columns
// ============================================================================

inline DatasetProfile createStringHeavyProfile() {
    DatasetProfile p;
    p.name = "string_heavy";
    p.description = "20 string columns (varied cardinality) + 10 scalar columns";
    p.default_rows = 200000;

    // 3 int32 + 3 float + 2 double + 2 uint64
    p.layout.addColumn({"id", bcsv::ColumnType::INT32});
    p.layout.addColumn({"category_id", bcsv::ColumnType::INT32});
    p.layout.addColumn({"status_code", bcsv::ColumnType::INT32});
    p.layout.addColumn({"value_f1", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"value_f2", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"value_f3", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"timestamp_d1", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"timestamp_d2", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"counter1", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"counter2", bcsv::ColumnType::UINT64});

    // 4 low-cardinality strings (10 unique values)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"tag_" + std::to_string(i), bcsv::ColumnType::STRING});
    
    // 4 medium-cardinality strings (1000 unique values)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"label_" + std::to_string(i), bcsv::ColumnType::STRING});
    
    // 4 high-cardinality strings (near-unique, UUID-like)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"uuid_" + std::to_string(i), bcsv::ColumnType::STRING});
    
    // 4 long variable-length strings (descriptions, 10-200 chars)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"desc_" + std::to_string(i), bcsv::ColumnType::STRING});
    
    // 4 short strings (2-10 chars, codes)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"code_" + std::to_string(i), bcsv::ColumnType::STRING});

    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        // Scalars
        row.set(static_cast<size_t>(0), static_cast<int32_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<int32_t>(datagen::hash32(rowIndex, 1) % 100));
        row.set(static_cast<size_t>(2), static_cast<int32_t>(datagen::hash32(rowIndex, 2) % 10));
        row.set(static_cast<size_t>(3), datagen::genFloat(rowIndex, 3));
        row.set(static_cast<size_t>(4), datagen::genFloat(rowIndex, 4));
        row.set(static_cast<size_t>(5), datagen::genFloat(rowIndex, 5));
        row.set(static_cast<size_t>(6), datagen::genDouble(rowIndex, 6));
        row.set(static_cast<size_t>(7), datagen::genDouble(rowIndex, 7));
        row.set(static_cast<size_t>(8), datagen::genUInt64(rowIndex, 8));
        row.set(static_cast<size_t>(9), datagen::genUInt64(rowIndex, 9));

        // Low-cardinality strings (10 unique)
        static const std::array<std::string, 10> lowCardTags = {
            "alpha", "beta", "gamma", "delta", "epsilon",
            "zeta", "eta", "theta", "iota", "kappa"
        };
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 10 + i;
            row.set(col, lowCardTags[datagen::hash64(rowIndex, col) % lowCardTags.size()]);
        }

        // Medium-cardinality strings (1000 unique)
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 14 + i;
            uint64_t h = datagen::hash64(rowIndex, col);
            size_t idx = h % 1000;
            std::string val = "label_" + std::to_string(idx);
            row.set(col, val);
        }

        // High-cardinality (near-unique UUID-like)
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 18 + i;
            row.set(col, datagen::genUuid(rowIndex, col));
        }

        // Long description strings (10-200 chars)
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 22 + i;
            size_t len = 10 + (datagen::hash64(rowIndex, col) % 191);
            row.set(col, datagen::genString(rowIndex, col, len));
        }

        // Short code strings (2-10 chars)
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 26 + i;
            size_t len = 2 + (datagen::hash64(rowIndex, col) % 9);
            row.set(col, datagen::genString(rowIndex, col, len));
        }
    };

    // Measurement campaign: 1000-row cycles with active/standstill
    p.generateTimeSeries = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 1000;
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        // Scalars: counters drift during active, freeze during standstill
        size_t segment = effRow / 100;
        row.set(static_cast<size_t>(0), static_cast<int32_t>(effRow));  // monotonic counter
        row.set(static_cast<size_t>(1), static_cast<int32_t>(segment % 100));
        row.set(static_cast<size_t>(2), static_cast<int32_t>(segment % 10));
        row.set(static_cast<size_t>(3), static_cast<float>(static_cast<float>(segment) * 0.5f));
        row.set(static_cast<size_t>(4), static_cast<float>(static_cast<float>(segment) * 1.5f));
        row.set(static_cast<size_t>(5), static_cast<float>(static_cast<float>(segment) * 0.1f));
        row.set(static_cast<size_t>(6), static_cast<double>(segment) * 0.01);
        row.set(static_cast<size_t>(7), static_cast<double>(segment) * 0.05);
        row.set(static_cast<size_t>(8), static_cast<uint64_t>(effRow * 1000));   // monotonic timer
        row.set(static_cast<size_t>(9), static_cast<uint64_t>(effRow * 10000));  // monotonic timer

        // Strings: change during active phase, frozen during standstill
        static const std::array<std::string, 10> tags = {
            "alpha", "beta", "gamma", "delta", "epsilon",
            "zeta", "eta", "theta", "iota", "kappa"
        };
        size_t strSeg = effRow / 200;
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 10 + i;
            row.set(col, tags[(strSeg + i) % tags.size()]);
        }
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 14 + i;
            row.set(col, std::string("label_") + std::to_string((strSeg + i) % 50));
        }
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 18 + i;
            row.set(col, std::string("uuid_fixed_") + std::to_string((strSeg + i) % 100));
        }
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 22 + i;
            row.set(col, std::string("description block ") + std::to_string(strSeg % 100));
        }
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 26 + i;
            row.set(col, std::string("CD") + std::to_string(strSeg % 20));
        }
    };

    return p;
}

// ============================================================================
// Profile 5: bool_heavy — 128 bool columns + 4 scalars, bitset performance
// ============================================================================

inline DatasetProfile createBoolHeavyProfile() {
    DatasetProfile p;
    p.name = "bool_heavy";
    p.description = "128 bool + 2 uint32 + 2 int64 columns — exercises bitset storage path";
    p.default_rows = 500000;

    for (size_t i = 0; i < 128; ++i)
        p.layout.addColumn({"flag_" + std::to_string(i), bcsv::ColumnType::BOOL});
    p.layout.addColumn({"counter_a", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"counter_b", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"ts_a", bcsv::ColumnType::INT64});
    p.layout.addColumn({"ts_b", bcsv::ColumnType::INT64});

    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowRandom(row, rowIndex, layout);
    };

    // Measurement campaign: 1000-row cycles, bools toggle during active, frozen during standstill
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 1000;
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        size_t boolSeg = effRow / 200;
        for (size_t i = 0; i < 128; ++i) {
            bool val = ((boolSeg + i) % 3) == 0;
            row.set(i, val);
        }
        // Counters: monotonic during active, frozen during standstill
        row.set(static_cast<size_t>(128), static_cast<uint32_t>(effRow * 10));
        row.set(static_cast<size_t>(129), static_cast<uint32_t>(effRow * 100));
        row.set(static_cast<size_t>(130), static_cast<int64_t>(effRow * 1000));
        row.set(static_cast<size_t>(131), static_cast<int64_t>(effRow * 10000));
    };

    return p;
}

// ============================================================================
// Profile 6: arithmetic_wide — 200 numeric columns, no strings, ZoH worst-case
// ============================================================================

inline DatasetProfile createArithmeticWideProfile() {
    DatasetProfile p;
    p.name = "arithmetic_wide";
    p.description = "200 numeric columns (40 each: int32, int64, uint32, float, double), no strings — volatile random";
    p.default_rows = 300000;

    for (size_t i = 0; i < 40; ++i)
        p.layout.addColumn({"i32_" + std::to_string(i), bcsv::ColumnType::INT32});
    for (size_t i = 0; i < 40; ++i)
        p.layout.addColumn({"i64_" + std::to_string(i), bcsv::ColumnType::INT64});
    for (size_t i = 0; i < 40; ++i)
        p.layout.addColumn({"u32_" + std::to_string(i), bcsv::ColumnType::UINT32});
    for (size_t i = 0; i < 40; ++i)
        p.layout.addColumn({"f_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    for (size_t i = 0; i < 40; ++i)
        p.layout.addColumn({"d_" + std::to_string(i), bcsv::ColumnType::DOUBLE});

    // Random: every value changes every row — worst case for ZoH
    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowRandom(row, rowIndex, layout);
    };

    // Short 5-row cycles (3 active + 2 standstill) — stress test for all codecs
    p.generateTimeSeries = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowTimeSeries(row, rowIndex, layout, 5);
    };

    return p;
}

// ============================================================================
// Profile 7: simulation_smooth — 100 float/double, slow linear drift
// ============================================================================

inline DatasetProfile createSimulationSmoothProfile() {
    DatasetProfile p;
    p.name = "simulation_smooth";
    p.description = "100 float/double columns with slow linear drift — ideal for ZoH/FOH compression";
    p.default_rows = 500000;

    // 4 metadata columns + 48 float + 48 double
    p.layout.addColumn({"step", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"time", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"iteration", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"converged", bcsv::ColumnType::BOOL});
    for (size_t i = 0; i < 48; ++i)
        p.layout.addColumn({"state_f_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    for (size_t i = 0; i < 48; ++i)
        p.layout.addColumn({"state_d_" + std::to_string(i), bcsv::ColumnType::DOUBLE});

    // Random: noisy simulation data — lots of precision
    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<double>(rowIndex) * 0.001);
        row.set(static_cast<size_t>(2), static_cast<uint32_t>(rowIndex / 1000));
        row.set(static_cast<size_t>(3), (rowIndex % 1000) > 900);

        for (size_t i = 0; i < 48; ++i) {
            size_t col = 4 + i;
            // Each state variable: base + small noise
            double base = static_cast<double>(i) * 10.0 + static_cast<double>(rowIndex) * 0.0001;
            float val = static_cast<float>(datagen::gaussianNoise(rowIndex, col, base, 0.001));
            row.set(col, val);
        }
        for (size_t i = 0; i < 48; ++i) {
            size_t col = 52 + i;
            double base = 1000.0 + static_cast<double>(i) * 50.0 + static_cast<double>(rowIndex) * 0.00001;
            double val = datagen::gaussianNoise(rowIndex, col, base, 0.0001);
            row.set(col, val);
        }
    };

    // Measurement campaign: 5000-row cycles with smooth drift during active, frozen during standstill
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 5000;
        const bool active = datagen::isActive(rowIndex, cycleLen);
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        // Monotonic tick and time — always change (Delta FoC ideal)
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<double>(rowIndex) * 0.001);
        row.set(static_cast<size_t>(2), static_cast<uint32_t>(effRow / 1000));
        row.set(static_cast<size_t>(3), (effRow / 1000 % 10) > 8);

        // Float channels: linear drift during active, frozen during standstill
        for (size_t i = 0; i < 48; ++i) {
            size_t col = 4 + i;
            float base = static_cast<float>(static_cast<double>(i) * 10.0);
            float drift = static_cast<float>(effRow) * 0.001f;
            float jitter = active
                ? static_cast<float>(static_cast<int32_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 60) - 30) * 0.0001f
                : 0.0f;
            row.set(col, base + drift + jitter);
        }
        // Double channels: finer drift + tiny jitter
        for (size_t i = 0; i < 48; ++i) {
            size_t col = 52 + i;
            double base = 1000.0 + static_cast<double>(i) * 50.0;
            double drift = static_cast<double>(effRow) * 0.0005;
            double jitter = active
                ? static_cast<double>(static_cast<int64_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 40) - 20) * 0.00001
                : 0.0;
            row.set(col, base + drift + jitter);
        }
    };

    return p;
}

// ============================================================================
// Profile 8: weather_timeseries — 40 columns, realistic weather patterns
// ============================================================================

inline DatasetProfile createWeatherTimeseriesProfile() {
    DatasetProfile p;
    p.name = "weather_timeseries";
    p.description = "40 columns: temperature, humidity, wind, pressure + string station IDs";
    p.default_rows = 500000;

    // Metadata
    p.layout.addColumn({"timestamp", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"station_id", bcsv::ColumnType::STRING});
    p.layout.addColumn({"region", bcsv::ColumnType::STRING});
    p.layout.addColumn({"quality_flag", bcsv::ColumnType::UINT8});
    // Temperature (10 sensors)
    for (size_t i = 0; i < 10; ++i)
        p.layout.addColumn({"temp_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    // Humidity (6 sensors)
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"humidity_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    // Wind speed + direction (4 pairs)
    for (size_t i = 0; i < 4; ++i) {
        p.layout.addColumn({"wind_speed_" + std::to_string(i), bcsv::ColumnType::FLOAT});
        p.layout.addColumn({"wind_dir_" + std::to_string(i), bcsv::ColumnType::UINT16});
    }
    // Pressure (4 sensors)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"pressure_" + std::to_string(i), bcsv::ColumnType::DOUBLE});
    // Precipitation + solar
    p.layout.addColumn({"precip_mm", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"solar_w_m2", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"is_raining", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"alert_level", bcsv::ColumnType::UINT8});

    static const std::array<std::string, 8> stations = {
        "WS-001", "WS-002", "WS-003", "WS-004",
        "WS-005", "WS-006", "WS-007", "WS-008"
    };
    static const std::array<std::string, 4> regions = {
        "North", "South", "East", "West"
    };

    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        size_t col = 0;
        // Timestamp: 1-minute intervals from 2024-01-01
        row.set(col++, static_cast<uint64_t>(1704067200000ULL + rowIndex * 60000));
        row.set(col++, stations[datagen::hash64(rowIndex, 1) % stations.size()]);
        row.set(col++, regions[datagen::hash64(rowIndex, 2) % regions.size()]);
        row.set(col++, static_cast<uint8_t>(datagen::hash32(rowIndex, 3) % 4));

        // Temperature: ~20°C with ±10 range and noise
        for (size_t i = 0; i < 10; ++i) {
            float temp = static_cast<float>(datagen::gaussianNoise(rowIndex, col, 20.0, 5.0));
            row.set(col++, temp);
        }
        // Humidity: 40-90%
        for (size_t i = 0; i < 6; ++i) {
            float hum = static_cast<float>(datagen::gaussianNoise(rowIndex, col, 65.0, 15.0));
            hum = std::max(0.0f, std::min(100.0f, hum));
            row.set(col++, hum);
        }
        // Wind: speed 0-30 m/s, dir 0-359°
        for (size_t i = 0; i < 4; ++i) {
            float speed = static_cast<float>(std::abs(datagen::gaussianNoise(rowIndex, col, 8.0, 5.0)));
            row.set(col, speed);
            ++col;
            row.set(col, static_cast<uint16_t>(datagen::hash32(rowIndex, col) % 360));
            ++col;
        }
        // Pressure: ~1013.25 hPa
        for (size_t i = 0; i < 4; ++i) {
            double pres = datagen::gaussianNoise(rowIndex, col, 1013.25, 5.0);
            row.set(col, pres);
            ++col;
        }
        // Precipitation
        row.set(col, std::max(0.0f, static_cast<float>(datagen::gaussianNoise(rowIndex, col, 0.5, 2.0))));
        ++col;
        row.set(col, std::max(0.0f, static_cast<float>(datagen::gaussianNoise(rowIndex, col, 400.0, 200.0))));
        ++col;
        row.set(col, datagen::genBool(rowIndex, col));
        ++col;
        row.set(col, static_cast<uint8_t>(datagen::hash32(rowIndex, col) % 5));
    };

    // Measurement campaign: 3000-row cycles (measurement interruptions)
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 3000;
        const bool active = datagen::isActive(rowIndex, cycleLen);
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        size_t col = 0;
        // Timestamp: always monotonic
        row.set(col++, static_cast<uint64_t>(1704067200000ULL + rowIndex * 60000));
        // Station and region: frozen during standstill (deployment change only when active)
        size_t stationSeg = effRow / 10000;
        row.set(col++, stations[stationSeg % stations.size()]);
        row.set(col++, regions[stationSeg % regions.size()]);
        row.set(col++, static_cast<uint8_t>(stationSeg % 4));

        // Temperature: drift during active, frozen during standstill
        for (size_t i = 0; i < 10; ++i) {
            float base = 15.0f + static_cast<float>(i) * 0.1f;
            float drift = static_cast<float>(effRow) * 0.001f;
            float jitter = active
                ? static_cast<float>(static_cast<int32_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 100) - 50) * 0.005f
                : 0.0f;
            row.set(col++, base + drift + jitter);
        }
        // Humidity: similar pattern
        for (size_t i = 0; i < 6; ++i) {
            float base = 50.0f;
            float drift = static_cast<float>(effRow % 5000) * 0.01f;
            float jitter = active
                ? static_cast<float>(static_cast<int32_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 80) - 40) * 0.01f
                : 0.0f;
            row.set(col++, base + drift + jitter);
        }
        // Wind: moderate drift
        for (size_t i = 0; i < 4; ++i) {
            float base = 5.0f;
            float drift = static_cast<float>(effRow % 2000) * 0.003f;
            float jitter = active
                ? static_cast<float>(static_cast<int32_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 60) - 30) * 0.01f
                : 0.0f;
            row.set(col++, base + drift + jitter);
            // Wind direction: monotonic-ish during active
            row.set(col++, static_cast<uint16_t>((effRow / 30 * 37 + i * 90) % 360));
        }
        // Pressure: very stable, tiny drift
        for (size_t i = 0; i < 4; ++i) {
            double base = 1005.0;
            double drift = static_cast<double>(effRow % 10000) * 0.0001;
            double jitter = active
                ? static_cast<double>(static_cast<int64_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 20) - 10) * 0.00001
                : 0.0;
            row.set(col++, base + drift + jitter);
        }
        // Precipitation/solar
        size_t precipSeg = effRow / 180;
        row.set(col++, static_cast<float>((precipSeg % 10) * 0.2f));
        row.set(col++, static_cast<float>(200.0f + (precipSeg % 8) * 50.0f));
        row.set(col++, (precipSeg % 5) == 0);
        row.set(col, static_cast<uint8_t>(precipSeg % 5));
    };

    return p;
}

// ============================================================================
// Profile 9: high_cardinality_string — 50 string cols, near-unique UUIDs
// ============================================================================

inline DatasetProfile createHighCardinalityStringProfile() {
    DatasetProfile p;
    p.name = "high_cardinality_string";
    p.description = "50 string columns with near-unique UUIDs — worst case for string compression";
    p.default_rows = 100000;

    // 2 scalar metadata columns + 48 string columns
    p.layout.addColumn({"row_id", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"batch", bcsv::ColumnType::UINT32});
    for (size_t i = 0; i < 48; ++i)
        p.layout.addColumn({"uid_" + std::to_string(i), bcsv::ColumnType::STRING});

    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint32_t>(rowIndex / 1000));

        for (size_t i = 0; i < 48; ++i) {
            size_t col = 2 + i;
            row.set(col, datagen::genUuid(rowIndex, col));
        }
    };

    // Measurement campaign: 2000-row cycles, UUID generation pauses during standstill
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 2000;
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));  // always monotonic
        size_t batchSeg = effRow / 500;
        row.set(static_cast<size_t>(1), static_cast<uint32_t>(batchSeg));

        for (size_t i = 0; i < 48; ++i) {
            size_t col = 2 + i;
            // Use batchSeg (derived from effRow) → frozen during standstill
            row.set(col, datagen::genUuid(batchSeg, col));
        }
    };

    return p;
}

// ============================================================================
// Profile 12: event_log — backend event stream with categorical strings
// ============================================================================

inline DatasetProfile createEventLogProfile() {
    DatasetProfile p;
    p.name = "event_log";
    p.description = "Application event stream: 8 categorical strings changing every row + telemetry metrics";
    p.default_rows = 500000;

    p.layout.addColumn({"tick", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"timestamp_ns", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"log_level", bcsv::ColumnType::STRING});
    p.layout.addColumn({"source_module", bcsv::ColumnType::STRING});
    p.layout.addColumn({"event_category", bcsv::ColumnType::STRING});
    p.layout.addColumn({"action", bcsv::ColumnType::STRING});
    p.layout.addColumn({"result_status", bcsv::ColumnType::STRING});
    p.layout.addColumn({"client_region", bcsv::ColumnType::STRING});
    p.layout.addColumn({"http_method", bcsv::ColumnType::STRING});
    p.layout.addColumn({"content_type", bcsv::ColumnType::STRING});
    p.layout.addColumn({"response_time_ms", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"payload_size_bytes", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"http_status", bcsv::ColumnType::UINT16});
    p.layout.addColumn({"is_error", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"is_authenticated", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"cpu_pct", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"mem_mb", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"latency_p50", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"latency_p95", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"latency_p99", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"queue_depth", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"db_ms", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"cache_hit_pct", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"req_total", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"req_success", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"req_failure", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"retries", bcsv::ColumnType::UINT32});

    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        static const std::array<std::string, 5> logLevels = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR"
        };
        static const std::array<std::string, 20> sourceModules = {
            "auth_service", "payment_gateway", "order_router", "billing_worker", "search_api",
            "inventory_sync", "metrics_collector", "cache_warmer", "email_sender", "scheduler",
            "profile_api", "fraud_detector", "reco_engine", "cdn_edge", "session_manager",
            "rate_limiter", "db_proxy", "alert_dispatch", "queue_worker", "audit_logger"
        };
        static const std::array<std::string, 8> eventCategories = {
            "security", "performance", "billing", "auth", "storage", "network", "api", "jobs"
        };
        static const std::array<std::string, 30> actions = {
            "login", "logout", "purchase", "refund", "api_call", "cache_miss", "cache_hit", "retry",
            "timeout", "enqueue", "dequeue", "db_query", "db_write", "sync_start", "sync_finish",
            "heartbeat", "token_refresh", "password_reset", "session_start", "session_end", "webhook",
            "batch_open", "batch_close", "upload", "download", "validate", "rate_limit", "throttle",
            "audit", "cleanup"
        };
        static const std::array<std::string, 6> resultStatuses = {
            "success", "failure", "timeout", "retrying", "cancelled", "degraded"
        };
        static const std::array<std::string, 12> clientRegions = {
            "us-east-1", "us-west-2", "eu-west-1", "eu-central-1", "ap-south-1", "ap-northeast-1",
            "sa-east-1", "ca-central-1", "af-south-1", "me-central-1", "us-gov-west-1", "ap-southeast-2"
        };
        static const std::array<std::string, 5> httpMethods = {
            "GET", "POST", "PUT", "DELETE", "PATCH"
        };
        static const std::array<std::string, 8> contentTypes = {
            "application/json", "text/html", "text/plain", "application/xml",
            "application/octet-stream", "multipart/form-data", "application/grpc", "application/x-www-form-urlencoded"
        };

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1704067200000000000ULL + rowIndex * 1000000ULL));
        row.set(static_cast<size_t>(2), logLevels[datagen::hash64(rowIndex, 2) % logLevels.size()]);
        row.set(static_cast<size_t>(3), sourceModules[datagen::hash64(rowIndex, 3) % sourceModules.size()]);
        row.set(static_cast<size_t>(4), eventCategories[datagen::hash64(rowIndex, 4) % eventCategories.size()]);
        row.set(static_cast<size_t>(5), actions[datagen::hash64(rowIndex, 5) % actions.size()]);
        row.set(static_cast<size_t>(6), resultStatuses[datagen::hash64(rowIndex, 6) % resultStatuses.size()]);
        row.set(static_cast<size_t>(7), clientRegions[datagen::hash64(rowIndex, 7) % clientRegions.size()]);
        row.set(static_cast<size_t>(8), httpMethods[datagen::hash64(rowIndex, 8) % httpMethods.size()]);
        row.set(static_cast<size_t>(9), contentTypes[datagen::hash64(rowIndex, 9) % contentTypes.size()]);
        row.set(static_cast<size_t>(10), static_cast<float>(datagen::hash64(rowIndex, 10) % 501));
        row.set(static_cast<size_t>(11), static_cast<uint32_t>(datagen::hash64(rowIndex, 11) % 1000001ULL));
        row.set(static_cast<size_t>(12), static_cast<uint16_t>(200 + (datagen::hash64(rowIndex, 12) % 300)));
        row.set(static_cast<size_t>(13), (datagen::hash64(rowIndex, 13) % 100) >= 90);
        row.set(static_cast<size_t>(14), (datagen::hash64(rowIndex, 14) % 100) < 85);

        for (size_t i = 0; i < 8; ++i) {
            const size_t col = 15 + i;
            const double metric = 10.0 * (i + 1) + static_cast<double>(datagen::hash64(rowIndex, col) % 10000) / 100.0;
            row.set(col, metric);
        }

        row.set(static_cast<size_t>(23), static_cast<uint32_t>(rowIndex));
        row.set(static_cast<size_t>(24), static_cast<uint32_t>(rowIndex - (rowIndex / 20)));
        row.set(static_cast<size_t>(25), static_cast<uint32_t>(rowIndex / 20));
        row.set(static_cast<size_t>(26), static_cast<uint32_t>(rowIndex / 50));
    };

    // Measurement campaign: 500-row cycles, quiet periods during standstill
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 500;
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        static const std::array<std::string, 5> logLevels = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR"
        };
        static const std::array<std::string, 20> sourceModules = {
            "auth_service", "payment_gateway", "order_router", "billing_worker", "search_api",
            "inventory_sync", "metrics_collector", "cache_warmer", "email_sender", "scheduler",
            "profile_api", "fraud_detector", "reco_engine", "cdn_edge", "session_manager",
            "rate_limiter", "db_proxy", "alert_dispatch", "queue_worker", "audit_logger"
        };
        static const std::array<std::string, 8> eventCategories = {
            "security", "performance", "billing", "auth", "storage", "network", "api", "jobs"
        };
        static const std::array<std::string, 30> actions = {
            "login", "logout", "purchase", "refund", "api_call", "cache_miss", "cache_hit", "retry",
            "timeout", "enqueue", "dequeue", "db_query", "db_write", "sync_start", "sync_finish",
            "heartbeat", "token_refresh", "password_reset", "session_start", "session_end", "webhook",
            "batch_open", "batch_close", "upload", "download", "validate", "rate_limit", "throttle",
            "audit", "cleanup"
        };
        static const std::array<std::string, 6> resultStatuses = {
            "success", "failure", "timeout", "retrying", "cancelled", "degraded"
        };
        static const std::array<std::string, 12> clientRegions = {
            "us-east-1", "us-west-2", "eu-west-1", "eu-central-1", "ap-south-1", "ap-northeast-1",
            "sa-east-1", "ca-central-1", "af-south-1", "me-central-1", "us-gov-west-1", "ap-southeast-2"
        };
        static const std::array<std::string, 5> httpMethods = {
            "GET", "POST", "PUT", "DELETE", "PATCH"
        };
        static const std::array<std::string, 8> contentTypes = {
            "application/json", "text/html", "text/plain", "application/xml",
            "application/octet-stream", "multipart/form-data", "application/grpc", "application/x-www-form-urlencoded"
        };

        const size_t metricSegment = effRow / 50;

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));  // tick: always monotonic
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1704067200000000000ULL + rowIndex * 1000000ULL));
        // Strings: freeze during standstill (use effRow)
        row.set(static_cast<size_t>(2), logLevels[(effRow * 3 + 1) % logLevels.size()]);
        row.set(static_cast<size_t>(3), sourceModules[(effRow * 7 + 2) % sourceModules.size()]);
        row.set(static_cast<size_t>(4), eventCategories[(effRow * 5 + 3) % eventCategories.size()]);
        row.set(static_cast<size_t>(5), actions[(effRow * 11 + 4) % actions.size()]);
        row.set(static_cast<size_t>(6), resultStatuses[(effRow * 5 + 1) % resultStatuses.size()]);
        row.set(static_cast<size_t>(7), clientRegions[(effRow * 7 + 5) % clientRegions.size()]);
        row.set(static_cast<size_t>(8), httpMethods[(effRow * 3 + 2) % httpMethods.size()]);
        row.set(static_cast<size_t>(9), contentTypes[(effRow * 5 + 7) % contentTypes.size()]);
        row.set(static_cast<size_t>(10), static_cast<float>(10.0f + static_cast<float>(metricSegment % 400) * 0.5f));
        row.set(static_cast<size_t>(11), static_cast<uint32_t>(1024 + (metricSegment % 1000) * 32));
        row.set(static_cast<size_t>(12), static_cast<uint16_t>((metricSegment % 20) == 0 ? 500 : 200 + (metricSegment % 20)));
        row.set(static_cast<size_t>(13), (metricSegment % 20) == 0);
        row.set(static_cast<size_t>(14), (effRow % 9) != 0);

        for (size_t i = 0; i < 8; ++i) {
            const size_t col = 15 + i;
            const double base = 20.0 + static_cast<double>(i) * 15.0;
            row.set(col, base + static_cast<double>(metricSegment % 500) * 0.05);
        }

        // Counters: monotonic during active, frozen during standstill
        row.set(static_cast<size_t>(23), static_cast<uint32_t>(effRow));
        row.set(static_cast<size_t>(24), static_cast<uint32_t>(effRow - (effRow / 25)));
        row.set(static_cast<size_t>(25), static_cast<uint32_t>(effRow / 25));
        row.set(static_cast<size_t>(26), static_cast<uint32_t>(effRow / 64));
    };

    return p;
}

// ============================================================================
// Profile 13: iot_fleet — round-robin device telemetry with bounded vocabularies
// ============================================================================

inline DatasetProfile createIotFleetProfile() {
    DatasetProfile p;
    p.name = "iot_fleet";
    p.description = "IoT fleet telemetry: round-robin devices, bounded metadata vocabularies, mixed numeric channels";
    p.default_rows = 500000;

    p.layout.addColumn({"seq", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"timestamp_ns", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"device_id", bcsv::ColumnType::STRING});
    p.layout.addColumn({"location", bcsv::ColumnType::STRING});
    p.layout.addColumn({"sensor_type", bcsv::ColumnType::STRING});
    p.layout.addColumn({"firmware_version", bcsv::ColumnType::STRING});
    p.layout.addColumn({"unit", bcsv::ColumnType::STRING});
    p.layout.addColumn({"alert_level", bcsv::ColumnType::STRING});
    p.layout.addColumn({"reading", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"reading_min", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"reading_max", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"battery_pct", bcsv::ColumnType::UINT8});
    p.layout.addColumn({"signal_rssi", bcsv::ColumnType::INT8});
    p.layout.addColumn({"error_count", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"uptime_sec", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"is_online", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"is_calibrated", bcsv::ColumnType::BOOL});
    for (size_t i = 0; i < 8; ++i) {
        p.layout.addColumn({"aux_" + std::to_string(i), bcsv::ColumnType::FLOAT});
    }

    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        static const std::array<std::string, 10> sensorTypes = {
            "temperature", "humidity", "pressure", "co2", "vibration",
            "light", "noise", "flow", "ph", "occupancy"
        };
        static const std::array<std::string, 5> firmware = {
            "v3.2.1", "v3.2.0", "v3.1.9", "v3.1.8", "v3.0.5"
        };
        static const std::array<std::string, 8> units = {
            "C", "hPa", "%RH", "m/s", "lux", "dB", "ppm", "mg/m3"
        };
        static const std::array<std::string, 4> alerts = {
            "normal", "caution", "warning", "critical"
        };

        const size_t deviceIdx = datagen::hash64(rowIndex, 2) % 100;
        const size_t locationIdx = datagen::hash64(rowIndex, 3) % 25;

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1704067200000000000ULL + rowIndex * 5000000ULL));
        row.set(static_cast<size_t>(2), std::string("sensor_") + (deviceIdx < 9 ? "00" : (deviceIdx < 99 ? "0" : "")) + std::to_string(deviceIdx + 1));
        row.set(static_cast<size_t>(3), std::string("building_") + static_cast<char>('A' + (locationIdx / 5))
            + "/floor_" + std::to_string((locationIdx % 5) + 1)
            + "/room_" + std::to_string(100 + static_cast<int>(locationIdx) * 4));
        row.set(static_cast<size_t>(4), sensorTypes[datagen::hash64(rowIndex, 4) % sensorTypes.size()]);
        row.set(static_cast<size_t>(5), firmware[datagen::hash64(rowIndex, 5) % firmware.size()]);
        row.set(static_cast<size_t>(6), units[datagen::hash64(rowIndex, 6) % units.size()]);
        row.set(static_cast<size_t>(7), alerts[datagen::hash64(rowIndex, 7) % alerts.size()]);

        const double reading = static_cast<double>(datagen::hash64(rowIndex, 8) % 200000) / 1000.0;
        row.set(static_cast<size_t>(8), reading);
        row.set(static_cast<size_t>(9), static_cast<float>(reading - static_cast<double>(datagen::hash64(rowIndex, 9) % 400) / 100.0));
        row.set(static_cast<size_t>(10), static_cast<float>(reading + static_cast<double>(datagen::hash64(rowIndex, 10) % 400) / 100.0));
        row.set(static_cast<size_t>(11), static_cast<uint8_t>(datagen::hash64(rowIndex, 11) % 101));
        row.set(static_cast<size_t>(12), static_cast<int8_t>(-90 + static_cast<int>(datagen::hash64(rowIndex, 12) % 61)));
        row.set(static_cast<size_t>(13), static_cast<uint32_t>(datagen::hash64(rowIndex, 13) % 20000));
        row.set(static_cast<size_t>(14), static_cast<uint64_t>(datagen::hash64(rowIndex, 14) % 5000000ULL));
        row.set(static_cast<size_t>(15), (datagen::hash64(rowIndex, 15) % 100) < 97);
        row.set(static_cast<size_t>(16), (datagen::hash64(rowIndex, 16) % 100) < 90);

        for (size_t i = 0; i < 8; ++i) {
            const size_t col = 17 + i;
            row.set(col, static_cast<float>(datagen::hash64(rowIndex, col) % 10000) / 100.0f);
        }
    };

    // Measurement campaign: 2000-row cycles, devices go offline during standstill
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 2000;
        const bool active = datagen::isActive(rowIndex, cycleLen);
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        static const std::array<std::string, 10> sensorTypes = {
            "temperature", "humidity", "pressure", "co2", "vibration",
            "light", "noise", "flow", "ph", "occupancy"
        };
        static const std::array<std::string, 5> firmware = {
            "v3.2.1", "v3.2.0", "v3.1.9", "v3.1.8", "v3.0.5"
        };
        static const std::array<std::string, 8> units = {
            "C", "hPa", "%RH", "m/s", "lux", "dB", "ppm", "mg/m3"
        };
        static const std::array<std::string, 4> alerts = {
            "normal", "caution", "warning", "critical"
        };

        const size_t deviceIdx = effRow % 100;
        const size_t locationIdx = deviceIdx / 4;
        const size_t sensorIdx = deviceIdx % sensorTypes.size();
        const size_t metricSegment = effRow / 20;

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));  // seq: always monotonic
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1704067200000000000ULL + rowIndex * 5000000ULL));
        // Device metadata: frozen during standstill
        row.set(static_cast<size_t>(2), std::string("sensor_") + (deviceIdx < 9 ? "00" : (deviceIdx < 99 ? "0" : "")) + std::to_string(deviceIdx + 1));
        row.set(static_cast<size_t>(3), std::string("building_") + static_cast<char>('A' + (locationIdx / 5))
            + "/floor_" + std::to_string((locationIdx % 5) + 1)
            + "/room_" + std::to_string(100 + static_cast<int>(locationIdx) * 4));
        row.set(static_cast<size_t>(4), sensorTypes[sensorIdx]);
        row.set(static_cast<size_t>(5), firmware[(deviceIdx / 20) % firmware.size()]);
        row.set(static_cast<size_t>(6), units[sensorIdx % units.size()]);

        const uint64_t skew = datagen::hash64(effRow, 7) % 100;
        const std::string& alert = (skew < 90) ? alerts[0] : (skew < 97) ? alerts[1] : (skew < 99) ? alerts[2] : alerts[3];
        row.set(static_cast<size_t>(7), alert);

        // Sensor readings: drift during active, frozen during standstill
        const double base = 10.0 + static_cast<double>(sensorIdx) * 7.5 + static_cast<double>(deviceIdx) * 0.2;
        const double drift = static_cast<double>(effRow) * 0.001;
        const double jitter = active
            ? static_cast<double>(static_cast<int64_t>(datagen::mix(datagen::hash64(rowIndex, 8)) % 100) - 50) * 0.001
            : 0.0;
        const double reading = base + drift + jitter;
        row.set(static_cast<size_t>(8), reading);
        row.set(static_cast<size_t>(9), static_cast<float>(reading - 0.5));
        row.set(static_cast<size_t>(10), static_cast<float>(reading + 0.5));
        row.set(static_cast<size_t>(11), static_cast<uint8_t>(20 + (deviceIdx % 81)));
        row.set(static_cast<size_t>(12), static_cast<int8_t>(-80 + static_cast<int>((deviceIdx * 3) % 35)));
        // Counters: monotonic during active, frozen during standstill
        row.set(static_cast<size_t>(13), static_cast<uint32_t>(effRow / 100 + deviceIdx));
        row.set(static_cast<size_t>(14), static_cast<uint64_t>(effRow * 5 + deviceIdx * 1000));
        row.set(static_cast<size_t>(15), true);
        row.set(static_cast<size_t>(16), (deviceIdx % 10) != 0);

        for (size_t i = 0; i < 8; ++i) {
            const size_t col = 17 + i;
            const float aux = static_cast<float>(reading * 0.1 + static_cast<double>(i) * 0.5
                + static_cast<double>(metricSegment % 50) * 0.05);
            row.set(col, aux);
        }
    };

    return p;
}

// ============================================================================
// Profile 14: financial_orders — categorical event feed with derived fields
// ============================================================================

inline DatasetProfile createFinancialOrdersProfile() {
    DatasetProfile p;
    p.name = "financial_orders";
    p.description = "Financial order feed: 8 categorical strings per event + price/quantity derived metrics";
    p.default_rows = 500000;

    p.layout.addColumn({"order_id", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"timestamp_ns", bcsv::ColumnType::UINT64});
    p.layout.addColumn({"ticker", bcsv::ColumnType::STRING});
    p.layout.addColumn({"exchange", bcsv::ColumnType::STRING});
    p.layout.addColumn({"order_type", bcsv::ColumnType::STRING});
    p.layout.addColumn({"side", bcsv::ColumnType::STRING});
    p.layout.addColumn({"status", bcsv::ColumnType::STRING});
    p.layout.addColumn({"broker_id", bcsv::ColumnType::STRING});
    p.layout.addColumn({"currency", bcsv::ColumnType::STRING});
    p.layout.addColumn({"strategy_tag", bcsv::ColumnType::STRING});
    p.layout.addColumn({"price", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"quantity", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"fill_price", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"fill_quantity", bcsv::ColumnType::UINT32});
    p.layout.addColumn({"commission", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"is_margin", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"is_short", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"is_algorithmic", bcsv::ColumnType::BOOL});
    p.layout.addColumn({"notional_usd", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"pnl_realized", bcsv::ColumnType::DOUBLE});
    p.layout.addColumn({"risk_score", bcsv::ColumnType::FLOAT});
    p.layout.addColumn({"seq_num", bcsv::ColumnType::UINT64});

    p.generate = [](bcsv::Row& row, size_t rowIndex) {
        static const std::array<std::string, 50> tickers = {
            "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "META", "TSLA", "ORCL", "INTC", "AMD",
            "NFLX", "ADBE", "CRM", "PYPL", "QCOM", "AVGO", "TXN", "IBM", "CSCO", "MU",
            "UBER", "SHOP", "SNOW", "PLTR", "ABNB", "SQ", "RBLX", "COIN", "TEAM", "DDOG",
            "SAP", "SONY", "BABA", "TCEHY", "ASML", "TSM", "NVO", "SHEL", "BP", "RIO",
            "JPM", "BAC", "GS", "MS", "C", "WFC", "V", "MA", "AXP", "BLK"
        };
        static const std::array<std::string, 5> exchanges = {
            "NYSE", "NASDAQ", "LSE", "TSE", "HKEX"
        };
        static const std::array<std::string, 5> orderTypes = {
            "MARKET", "LIMIT", "STOP", "STOP_LIMIT", "TRAILING_STOP"
        };
        static const std::array<std::string, 2> sides = {
            "BUY", "SELL"
        };
        static const std::array<std::string, 6> statuses = {
            "NEW", "PARTIAL_FILL", "FILLED", "CANCELLED", "REJECTED", "EXPIRED"
        };
        static const std::array<std::string, 20> brokers = {
            "broker_01", "broker_02", "broker_03", "broker_04", "broker_05",
            "broker_06", "broker_07", "broker_08", "broker_09", "broker_10",
            "broker_11", "broker_12", "broker_13", "broker_14", "broker_15",
            "broker_16", "broker_17", "broker_18", "broker_19", "broker_20"
        };
        static const std::array<std::string, 8> currencies = {
            "USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "HKD"
        };
        static const std::array<std::string, 15> strategies = {
            "mean_reversion", "momentum", "vwap", "twap", "stat_arb",
            "pair_trade", "market_making", "breakout", "carry", "news_alpha",
            "event_driven", "liquidity_seek", "cross_venue", "risk_parity", "vol_target"
        };

        const size_t tickerIdx = datagen::hash64(rowIndex, 2) % tickers.size();
        const double basePrice = 20.0 + static_cast<double>(tickerIdx) * 4.5;
        const double price = basePrice + static_cast<double>(datagen::hash64(rowIndex, 10) % 1000) * 0.01;
        const uint32_t quantity = static_cast<uint32_t>(1 + (datagen::hash64(rowIndex, 11) % 20000));
        const uint32_t fillQty = static_cast<uint32_t>(datagen::hash64(rowIndex, 13) % (quantity + 1));
        const double fillPrice = price + static_cast<double>(static_cast<int>(datagen::hash64(rowIndex, 12) % 11) - 5) * 0.01;
        const double notional = price * static_cast<double>(quantity);
        const double pnl = (fillPrice - price) * static_cast<double>(fillQty);

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(1000000000ULL + rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1704067200000000000ULL + rowIndex * 100000ULL));
        row.set(static_cast<size_t>(2), tickers[tickerIdx]);
        row.set(static_cast<size_t>(3), exchanges[datagen::hash64(rowIndex, 3) % exchanges.size()]);
        row.set(static_cast<size_t>(4), orderTypes[datagen::hash64(rowIndex, 4) % orderTypes.size()]);
        row.set(static_cast<size_t>(5), sides[datagen::hash64(rowIndex, 5) % sides.size()]);
        row.set(static_cast<size_t>(6), statuses[datagen::hash64(rowIndex, 6) % statuses.size()]);
        row.set(static_cast<size_t>(7), brokers[datagen::hash64(rowIndex, 7) % brokers.size()]);
        row.set(static_cast<size_t>(8), currencies[datagen::hash64(rowIndex, 8) % currencies.size()]);
        row.set(static_cast<size_t>(9), strategies[datagen::hash64(rowIndex, 9) % strategies.size()]);
        row.set(static_cast<size_t>(10), price);
        row.set(static_cast<size_t>(11), quantity);
        row.set(static_cast<size_t>(12), fillPrice);
        row.set(static_cast<size_t>(13), fillQty);
        row.set(static_cast<size_t>(14), static_cast<float>(0.0002 * static_cast<double>(quantity)));
        row.set(static_cast<size_t>(15), (datagen::hash64(rowIndex, 15) % 100) < 20);
        row.set(static_cast<size_t>(16), (datagen::hash64(rowIndex, 16) % 100) < 10);
        row.set(static_cast<size_t>(17), (datagen::hash64(rowIndex, 17) % 100) < 60);
        row.set(static_cast<size_t>(18), notional);
        row.set(static_cast<size_t>(19), pnl);
        row.set(static_cast<size_t>(20), static_cast<float>(static_cast<double>(datagen::hash64(rowIndex, 20) % 1000) / 1000.0));
        row.set(static_cast<size_t>(21), static_cast<uint64_t>(rowIndex));
    };

    // Measurement campaign: 5000-row cycles (market open/close)
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 5000;
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        static const std::array<std::string, 50> tickers = {
            "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA", "META", "TSLA", "ORCL", "INTC", "AMD",
            "NFLX", "ADBE", "CRM", "PYPL", "QCOM", "AVGO", "TXN", "IBM", "CSCO", "MU",
            "UBER", "SHOP", "SNOW", "PLTR", "ABNB", "SQ", "RBLX", "COIN", "TEAM", "DDOG",
            "SAP", "SONY", "BABA", "TCEHY", "ASML", "TSM", "NVO", "SHEL", "BP", "RIO",
            "JPM", "BAC", "GS", "MS", "C", "WFC", "V", "MA", "AXP", "BLK"
        };
        static const std::array<std::string, 5> exchanges = {
            "NYSE", "NASDAQ", "LSE", "TSE", "HKEX"
        };
        static const std::array<std::string, 5> orderTypes = {
            "MARKET", "LIMIT", "STOP", "STOP_LIMIT", "TRAILING_STOP"
        };
        static const std::array<std::string, 2> sides = {
            "BUY", "SELL"
        };
        static const std::array<std::string, 6> statuses = {
            "NEW", "PARTIAL_FILL", "FILLED", "CANCELLED", "REJECTED", "EXPIRED"
        };
        static const std::array<std::string, 20> brokers = {
            "broker_01", "broker_02", "broker_03", "broker_04", "broker_05",
            "broker_06", "broker_07", "broker_08", "broker_09", "broker_10",
            "broker_11", "broker_12", "broker_13", "broker_14", "broker_15",
            "broker_16", "broker_17", "broker_18", "broker_19", "broker_20"
        };
        static const std::array<std::string, 8> currencies = {
            "USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "HKD"
        };
        static const std::array<std::string, 15> strategies = {
            "mean_reversion", "momentum", "vwap", "twap", "stat_arb",
            "pair_trade", "market_making", "breakout", "carry", "news_alpha",
            "event_driven", "liquidity_seek", "cross_venue", "risk_parity", "vol_target"
        };
        static const std::array<double, 50> basePrices = {
            190.0, 420.0, 150.0, 170.0, 900.0, 480.0, 250.0, 115.0, 38.0, 145.0,
            600.0, 540.0, 260.0, 70.0, 180.0, 1340.0, 195.0, 220.0, 54.0, 110.0,
            75.0, 68.0, 165.0, 26.0, 145.0, 78.0, 35.0, 210.0, 185.0, 120.0,
            170.0, 95.0, 82.0, 44.0, 980.0, 135.0, 120.0, 70.0, 36.0, 62.0,
            185.0, 42.0, 390.0, 102.0, 55.0, 61.0, 280.0, 445.0, 295.0, 910.0
        };

        // Use effRow for all values that should freeze during market close (standstill)
        const size_t tickerIdx = (effRow * 7 + 3) % tickers.size();
        const double drift = static_cast<double>(effRow) * 0.001;
        const double price = basePrices[tickerIdx] + drift;
        const uint32_t quantity = static_cast<uint32_t>(100 + ((effRow * 37) % 5000));
        const uint32_t fillQty = static_cast<uint32_t>(quantity * ((effRow % 4) + 1) / 4);
        const double fillPrice = price + 0.01;
        const double notional = price * static_cast<double>(quantity);
        const double pnl = (fillPrice - price) * static_cast<double>(fillQty);

        row.set(static_cast<size_t>(0), static_cast<uint64_t>(1000000000ULL + rowIndex));  // order_id: always monotonic
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1704067200000000000ULL + rowIndex * 100000ULL));
        row.set(static_cast<size_t>(2), tickers[tickerIdx]);
        row.set(static_cast<size_t>(3), exchanges[(effRow * 3 + 1) % exchanges.size()]);
        row.set(static_cast<size_t>(4), orderTypes[(effRow * 5 + 2) % orderTypes.size()]);
        row.set(static_cast<size_t>(5), sides[effRow % sides.size()]);
        row.set(static_cast<size_t>(6), statuses[(effRow * 7 + 3) % statuses.size()]);
        row.set(static_cast<size_t>(7), brokers[(effRow * 11 + 4) % brokers.size()]);
        row.set(static_cast<size_t>(8), currencies[(effRow * 3 + tickerIdx) % currencies.size()]);
        row.set(static_cast<size_t>(9), strategies[(effRow * 13 + 1) % strategies.size()]);
        row.set(static_cast<size_t>(10), price);
        row.set(static_cast<size_t>(11), quantity);
        row.set(static_cast<size_t>(12), fillPrice);
        row.set(static_cast<size_t>(13), fillQty);
        row.set(static_cast<size_t>(14), static_cast<float>(0.0002 * static_cast<double>(quantity)));
        row.set(static_cast<size_t>(15), (effRow % 5) == 0);
        row.set(static_cast<size_t>(16), (effRow % 10) == 0);
        row.set(static_cast<size_t>(17), (effRow % 5) < 3);
        row.set(static_cast<size_t>(18), notional);
        row.set(static_cast<size_t>(19), pnl);
        row.set(static_cast<size_t>(20), static_cast<float>((tickerIdx % 10) / 10.0));
        row.set(static_cast<size_t>(21), static_cast<uint64_t>(effRow));  // counter: frozen during standstill
    };

    return p;
}

// ============================================================================
// Profile 10: realistic_measurement — DAQ session with phases and mixed rates
// ============================================================================
//
// Models a real measurement session with distinct phases:
//   0–20% setup/warmup   — mostly static, metadata set once
//  20–80% measurement    — sensors active at different sampling rates
//  80–100% cooldown/idle — sensors go static again
//
// Column groups:
//   tick (UINT64)          — monotonic counter, changes every row
//   timestamp_ns (UINT64)  — monotonic clock (ns), changes every row
//   test_name (STRING)     — set once, never changes
//   dut_id (STRING)        — set once, never changes
//   operator (STRING)      — set once, never changes
//   phase (UINT8)          — changes at phase boundaries
//   8 fast sensors (FLOAT) — update every row during active phase
//   8 medium sensors (DOUBLE) — update every 10 rows during active phase
//   8 slow sensors (INT32)    — update every 100 rows during active phase
//   4 status flags (BOOL)  — sparse events throughout
//   4 counters (UINT32)    — update every 1/5/25/125 rows (powers of 5)

inline DatasetProfile createRealisticMeasurementProfile() {
    DatasetProfile p;
    p.name = "realistic_measurement";
    p.description = "DAQ session: 5 phases, mixed sensor rates, 3 static strings, clock+counter — realistic ZoH test";
    p.default_rows = 500000;

    // Metadata — always present
    p.layout.addColumn({"tick",         bcsv::ColumnType::UINT64});  // 0
    p.layout.addColumn({"timestamp_ns", bcsv::ColumnType::UINT64});  // 1
    p.layout.addColumn({"test_name",    bcsv::ColumnType::STRING});  // 2
    p.layout.addColumn({"dut_id",       bcsv::ColumnType::STRING});  // 3
    p.layout.addColumn({"operator",     bcsv::ColumnType::STRING});  // 4
    p.layout.addColumn({"phase",        bcsv::ColumnType::UINT8});   // 5
    // Fast sensors (update every row during measurement)
    for (size_t i = 0; i < 8; ++i)
        p.layout.addColumn({"fast_" + std::to_string(i), bcsv::ColumnType::FLOAT});  // 6–13
    // Medium sensors (update every ~10 rows during measurement)
    for (size_t i = 0; i < 8; ++i)
        p.layout.addColumn({"med_" + std::to_string(i), bcsv::ColumnType::DOUBLE});  // 14–21
    // Slow sensors (update every ~100 rows during measurement)
    for (size_t i = 0; i < 8; ++i)
        p.layout.addColumn({"slow_" + std::to_string(i), bcsv::ColumnType::INT32});  // 22–29
    // Status flags — sparse events
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"status_" + std::to_string(i), bcsv::ColumnType::BOOL});  // 30–33
    // Counters at different rates
    p.layout.addColumn({"cnt_fast",  bcsv::ColumnType::UINT32});  // 34  — every row
    p.layout.addColumn({"cnt_med",   bcsv::ColumnType::UINT32});  // 35  — every 5 rows
    p.layout.addColumn({"cnt_slow",  bcsv::ColumnType::UINT32});  // 36  — every 25 rows
    p.layout.addColumn({"cnt_rare",  bcsv::ColumnType::UINT32});  // 37  — every 125 rows

    // Random generator (worst case): everything changes every row
    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowRandom(row, rowIndex, layout);
        // Override tick/timestamp to be monotonic even in random mode
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(rowIndex * 1000));
    };

    // Measurement campaign: recurring 10000-row cycles with active/standstill
    // Active (70%): sensors drift, counters increment, noise present
    // Standstill (30%): all sensors frozen, counters frozen
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 10000;
        const bool active = datagen::isActive(rowIndex, cycleLen);
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);
        const size_t cycle = rowIndex / cycleLen;

        // Phase: derived from position in cycle (recurring, not one-shot)
        const size_t posInCycle = rowIndex % cycleLen;
        const size_t activeLen = cycleLen * 7 / 10;
        uint8_t phase;
        if (!active)                         { phase = 4; }  // standstill
        else if (posInCycle < activeLen / 10) { phase = 0; }  // warmup (first 10% of active)
        else if (posInCycle < activeLen * 9 / 10) { phase = 2; }  // measurement (middle 80% of active)
        else                                 { phase = 3; }  // cooldown (last 10% of active)

        // ── Always changing: tick + timestamp ──
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(1700000000000000ULL + rowIndex * 1000));

        // ── Static metadata: set once, never changes ── (ZoH ideal)
        row.set(static_cast<size_t>(2), std::string("Thermal_Cycling_Test_v3"));
        row.set(static_cast<size_t>(3), std::string("DUT-2026-0042"));
        row.set(static_cast<size_t>(4), std::string("TWeber"));

        // ── Phase ──
        row.set(static_cast<size_t>(5), phase);

        // ── Fast sensors: linear drift + small noise during active, frozen during standstill ──
        for (size_t i = 0; i < 8; ++i) {
            size_t col = 6 + i;
            float base = 20.0f + static_cast<float>(i) * 5.0f + static_cast<float>(cycle) * 2.0f;
            float drift = static_cast<float>(effRow % 10000) * 0.001f;
            float noise = active
                ? static_cast<float>(static_cast<int32_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 100) - 50) * 0.005f
                : 0.0f;
            row.set(col, base + drift + noise);
        }

        // ── Medium sensors: drift during active, frozen during standstill ──
        for (size_t i = 0; i < 8; ++i) {
            size_t col = 14 + i;
            double base = 100.0 + static_cast<double>(i) * 25.0 + static_cast<double>(cycle) * 5.0;
            double drift = static_cast<double>(effRow % 10000) * 0.0005;
            double noise = active
                ? static_cast<double>(static_cast<int64_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 40) - 20) * 0.0001
                : 0.0;
            row.set(col, base + drift + noise);
        }

        // ── Slow sensors: step-wise change during active, frozen during standstill ──
        for (size_t i = 0; i < 8; ++i) {
            size_t col = 22 + i;
            size_t interval = 100 + i * 50;
            size_t seg = effRow / interval;
            row.set(col, static_cast<int32_t>(1000 + static_cast<int32_t>(seg * 10 + i)));
        }

        // ── Status flags: sparse events, frozen during standstill ──
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 30 + i;
            size_t period = 500 * (i + 1);
            bool val = (effRow % period) < (period / 50);
            row.set(col, val);
        }

        // ── Counters: monotonic during active, frozen during standstill ── (Delta FoC ideal)
        row.set(static_cast<size_t>(34), static_cast<uint32_t>(effRow));
        row.set(static_cast<size_t>(35), static_cast<uint32_t>(effRow / 5));
        row.set(static_cast<size_t>(36), static_cast<uint32_t>(effRow / 25));
        row.set(static_cast<size_t>(37), static_cast<uint32_t>(effRow / 125));
    };

    return p;
}

// ============================================================================
// Profile 11: rtl_waveform — RTL simulation waveform capture
// ============================================================================
//
// Models a digital waveform dump from an RTL (register transfer logic) simulation.
// Contains only bools and unsigned integers — no floats, no strings.
//
// Column groups:
//   cycle (UINT64)       — monotonic clock counter, increments every row
//   sim_time_ps (UINT64) — monotonic simulation time in picoseconds (10ps step)
//   256 bool signals     — toggle at varied rates (clk dividers, FSM bits, enables)
//   16 uint8  registers  — byte-width buses, varied change rates
//   8  uint16 registers  — 16-bit address/control buses
//   4  uint32 registers  — 32-bit data buses
//   4  uint64 registers  — 64-bit wide data paths

inline DatasetProfile createRtlWaveformProfile() {
    DatasetProfile p;
    p.name = "rtl_waveform";
    p.description = "RTL waveform: 256 bools + 32 uint registers, clock+timer — digital simulation capture";
    p.default_rows = 500000;

    // Monotonic counters
    p.layout.addColumn({"cycle",       bcsv::ColumnType::UINT64});  // 0
    p.layout.addColumn({"sim_time_ps", bcsv::ColumnType::UINT64});  // 1

    // 256 digital signal lines
    for (size_t i = 0; i < 256; ++i)
        p.layout.addColumn({"sig_" + std::to_string(i), bcsv::ColumnType::BOOL});  // 2–257

    // 16 byte-width registers
    for (size_t i = 0; i < 16; ++i)
        p.layout.addColumn({"reg8_" + std::to_string(i), bcsv::ColumnType::UINT8});  // 258–273

    // 8 halfword registers
    for (size_t i = 0; i < 8; ++i)
        p.layout.addColumn({"reg16_" + std::to_string(i), bcsv::ColumnType::UINT16});  // 274–281

    // 4 word registers (data bus)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"reg32_" + std::to_string(i), bcsv::ColumnType::UINT32});  // 282–285

    // 4 doubleword registers (wide data path)
    for (size_t i = 0; i < 4; ++i)
        p.layout.addColumn({"reg64_" + std::to_string(i), bcsv::ColumnType::UINT64});  // 286–289

    // Random generator: all signals toggle randomly
    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(rowIndex * 10));  // 10ps step
        // Random fill for everything else
        for (size_t col = 2; col < layout.columnCount(); ++col) {
            switch (layout.columnType(col)) {
                case bcsv::ColumnType::BOOL:   row.set(col, datagen::genBool(rowIndex, col)); break;
                case bcsv::ColumnType::UINT8:  row.set(col, datagen::genUInt8(rowIndex, col)); break;
                case bcsv::ColumnType::UINT16: row.set(col, datagen::genUInt16(rowIndex, col)); break;
                case bcsv::ColumnType::UINT32: row.set(col, datagen::genUInt32(rowIndex, col)); break;
                case bcsv::ColumnType::UINT64: row.set(col, datagen::genUInt64(rowIndex, col)); break;
                default: break;
            }
        }
    };

    // Measurement campaign: 2000-row cycles (clock-gating during standstill)
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 2000;
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);

        // ── Monotonic counters (always change — Delta FoC ideal) ──
        row.set(static_cast<size_t>(0), static_cast<uint64_t>(rowIndex));
        row.set(static_cast<size_t>(1), static_cast<uint64_t>(rowIndex * 10));

        // ── 256 digital signals: use effRow so all signals freeze during standstill ──

        // sig_0: master clock — toggles every cycle (frozen during standstill = clock gating)
        row.set(static_cast<size_t>(2), (effRow & 1) != 0);

        // sig_1..3: clock dividers
        row.set(static_cast<size_t>(3), ((effRow / 2)  & 1) != 0);
        row.set(static_cast<size_t>(4), ((effRow / 4)  & 1) != 0);
        row.set(static_cast<size_t>(5), ((effRow / 8)  & 1) != 0);

        // sig_4..15: FSM state bits — change at varied intervals
        for (size_t i = 4; i < 16; ++i) {
            size_t period = 16 * (1 + (i % 4));
            size_t seg = effRow / period;
            row.set(2 + i, ((seg + i) & 1) != 0);
        }

        // sig_16..63: control/enable signals — slower
        for (size_t i = 16; i < 64; ++i) {
            size_t period = 50 + (i * 7) % 450;
            size_t seg = effRow / period;
            row.set(2 + i, ((seg ^ i) & 1) != 0);
        }

        // sig_64..255: data-path bits — moderate toggle rates
        for (size_t i = 64; i < 256; ++i) {
            size_t period = 2 + (i * 3) % 30;
            size_t seg = effRow / period;
            row.set(2 + i, (datagen::hash32(seg, i) & 1) != 0);
        }

        // ── 16 byte registers: monotonic increment during active, frozen during standstill ──
        for (size_t i = 0; i < 16; ++i) {
            size_t col = 258 + i;
            size_t period = 4 + i * 8;
            size_t seg = effRow / period;
            row.set(col, static_cast<uint8_t>(seg + i));
        }

        // ── 8 halfword registers ──
        for (size_t i = 0; i < 8; ++i) {
            size_t col = 274 + i;
            size_t period = 8 + i * 16;
            size_t seg = effRow / period;
            row.set(col, static_cast<uint16_t>(seg * 17 + i));
        }

        // ── 4 word registers ──
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 282 + i;
            size_t period = 16 + i * 32;
            size_t seg = effRow / period;
            row.set(col, static_cast<uint32_t>(datagen::hash32(seg, i)));
        }

        // ── 4 doubleword registers ──
        for (size_t i = 0; i < 4; ++i) {
            size_t col = 286 + i;
            size_t period = 32 + i * 64;
            size_t seg = effRow / period;
            row.set(col, static_cast<uint64_t>(datagen::hash64(seg, i)));
        }
    };

    return p;
}

// ============================================================================
// Profile 15: measurement_campaign — SIMD-grouped float/double, time-series
// ============================================================================
//
// Designed for Static vs Flexible parity analysis and SIMD vectorization
// investigation.  Float and double columns are arranged in groups of
// 1, 2, 3, 4 adjacent columns so that the compiler's auto-vectorizer
// can (or cannot) employ SSE/AVX lanes depending on group width.
//
// 84 columns total (within the 320-col Static layout limit):
//   6 × bool, int8, int16, int32, int64, uint8, uint16, uint32, uint64, string
//   12 × float  (groups: 1 + 2 + 3 + 4 + 2)
//   12 × double (groups: 1 + 2 + 3 + 4 + 2)

inline DatasetProfile createMeasurementCampaignProfile() {
    DatasetProfile p;
    p.name = "measurement_campaign";
    p.description = "84 columns, SIMD-grouped float/double, measurement campaign time-series for Static vs Flexible codec analysis";
    p.default_rows = 500000;

    // --- Bool: 6 status/alarm flags ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"alarm_" + std::to_string(i), bcsv::ColumnType::BOOL});           // 0-5

    // --- Int8: 6 small enums ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"state_i8_" + std::to_string(i), bcsv::ColumnType::INT8});        // 6-11

    // --- Int16: 6 medium counters ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"counter_i16_" + std::to_string(i), bcsv::ColumnType::INT16});    // 12-17

    // --- Int32: 6 step counters ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"sensor_i32_" + std::to_string(i), bcsv::ColumnType::INT32});     // 18-23

    // --- Int64: 6 tick/timestamp/large counters ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"tick_i64_" + std::to_string(i), bcsv::ColumnType::INT64});       // 24-29

    // --- UInt8: 6 phase/state ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"phase_u8_" + std::to_string(i), bcsv::ColumnType::UINT8});       // 30-35

    // --- UInt16: 6 analog readings ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"adc_u16_" + std::to_string(i), bcsv::ColumnType::UINT16});       // 36-41

    // --- UInt32: 6 monotonic counters ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"cnt_u32_" + std::to_string(i), bcsv::ColumnType::UINT32});       // 42-47

    // --- UInt64: 6 nanosecond timers ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"timer_u64_" + std::to_string(i), bcsv::ColumnType::UINT64});     // 48-53

    // --- Float: 12 sensor channels in SIMD-diagnostic groups ---
    //     Group 1: 1 solo  (scalar fallback)
    p.layout.addColumn({"f_solo_0", bcsv::ColumnType::FLOAT});                                 // 54
    //     Group 2: 2 pair  (SSE 64-bit / half-register)
    p.layout.addColumn({"f_pair_0", bcsv::ColumnType::FLOAT});                                 // 55
    p.layout.addColumn({"f_pair_1", bcsv::ColumnType::FLOAT});                                 // 56
    //     Group 3: 3 triple (partial SIMD)
    p.layout.addColumn({"f_triple_0", bcsv::ColumnType::FLOAT});                               // 57
    p.layout.addColumn({"f_triple_1", bcsv::ColumnType::FLOAT});                               // 58
    p.layout.addColumn({"f_triple_2", bcsv::ColumnType::FLOAT});                               // 59
    //     Group 4: 4 quad  (full SSE / half AVX)
    p.layout.addColumn({"f_quad_0", bcsv::ColumnType::FLOAT});                                 // 60
    p.layout.addColumn({"f_quad_1", bcsv::ColumnType::FLOAT});                                 // 61
    p.layout.addColumn({"f_quad_2", bcsv::ColumnType::FLOAT});                                 // 62
    p.layout.addColumn({"f_quad_3", bcsv::ColumnType::FLOAT});                                 // 63
    //     Group 5: 2 extra pair
    p.layout.addColumn({"f_extra_0", bcsv::ColumnType::FLOAT});                                // 64
    p.layout.addColumn({"f_extra_1", bcsv::ColumnType::FLOAT});                                // 65

    // --- Double: 12 precision sensors in SIMD-diagnostic groups ---
    //     Group 1: 1 solo
    p.layout.addColumn({"d_solo_0", bcsv::ColumnType::DOUBLE});                                // 66
    //     Group 2: 2 pair (SSE 128-bit)
    p.layout.addColumn({"d_pair_0", bcsv::ColumnType::DOUBLE});                                // 67
    p.layout.addColumn({"d_pair_1", bcsv::ColumnType::DOUBLE});                                // 68
    //     Group 3: 3 triple
    p.layout.addColumn({"d_triple_0", bcsv::ColumnType::DOUBLE});                              // 69
    p.layout.addColumn({"d_triple_1", bcsv::ColumnType::DOUBLE});                              // 70
    p.layout.addColumn({"d_triple_2", bcsv::ColumnType::DOUBLE});                              // 71
    //     Group 4: 4 quad (full AVX 256-bit)
    p.layout.addColumn({"d_quad_0", bcsv::ColumnType::DOUBLE});                                // 72
    p.layout.addColumn({"d_quad_1", bcsv::ColumnType::DOUBLE});                                // 73
    p.layout.addColumn({"d_quad_2", bcsv::ColumnType::DOUBLE});                                // 74
    p.layout.addColumn({"d_quad_3", bcsv::ColumnType::DOUBLE});                                // 75
    //     Group 5: 2 extra pair
    p.layout.addColumn({"d_extra_0", bcsv::ColumnType::DOUBLE});                               // 76
    p.layout.addColumn({"d_extra_1", bcsv::ColumnType::DOUBLE});                               // 77

    // --- String: 6 metadata/labels ---
    for (size_t i = 0; i < 6; ++i)
        p.layout.addColumn({"label_" + std::to_string(i), bcsv::ColumnType::STRING});          // 78-83

    // ── Random generator (volatile worst-case) ──
    p.generate = [layout = p.layout](bcsv::Row& row, size_t rowIndex) {
        datagen::fillRowRandom(row, rowIndex, layout);
    };

    // ── Measurement campaign generator: 2000-row cycles ──
    // Active phase (70% = 1400 rows): monotonic counters, drifting sensors,
    //   noise, occasional signal loss (flat segments).
    // Standstill phase (30% = 600 rows): all values frozen.
    p.generateTimeSeries = [](bcsv::Row& row, size_t rowIndex) {
        constexpr size_t cycleLen = 2000;
        const bool active = datagen::isActive(rowIndex, cycleLen);
        const size_t effRow = datagen::effectiveRow(rowIndex, cycleLen);
        const size_t cycle = rowIndex / cycleLen;

        // --- Bool alarms: toggle per segment, frozen in standstill ---
        for (size_t i = 0; i < 6; ++i) {
            size_t seg = effRow / 200;
            row.set(i, ((seg + i + cycle) % 3) == 0);
        }

        // --- Int8 state enums: step-wise, frozen in standstill ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 6 + i;
            size_t seg = effRow / (100 + i * 50);
            row.set(col, static_cast<int8_t>((seg + i) % 127));
        }

        // --- Int16 medium counters: monotonic, frozen in standstill ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 12 + i;
            row.set(col, static_cast<int16_t>(static_cast<int16_t>(effRow / (10 + i * 5)) + static_cast<int16_t>(i * 100)));
        }

        // --- Int32 step counters: slow sensors ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 18 + i;
            size_t interval = 50 + i * 25;
            size_t seg = effRow / interval;
            row.set(col, static_cast<int32_t>(1000 + static_cast<int32_t>(seg * 7 + i)));
        }

        // --- Int64 tick/timestamp: always monotonic (tick), frozen (others) ---
        row.set(static_cast<size_t>(24), static_cast<int64_t>(rowIndex));                      // tick: always advances
        row.set(static_cast<size_t>(25), static_cast<int64_t>(1700000000LL + static_cast<int64_t>(rowIndex) * 1000)); // timestamp
        for (size_t i = 2; i < 6; ++i) {
            size_t col = 24 + i;
            row.set(col, static_cast<int64_t>(static_cast<int64_t>(effRow) * static_cast<int64_t>(100 + i * 50)));
        }

        // --- UInt8 phase/state: step-wise ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 30 + i;
            size_t seg = effRow / (200 + i * 100);
            row.set(col, static_cast<uint8_t>((seg + i) % 255));
        }

        // --- UInt16 ADC readings: monotonic + noise ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 36 + i;
            uint16_t base = static_cast<uint16_t>(effRow / (5 + i * 3) + i * 1000);
            uint16_t noise = active
                ? static_cast<uint16_t>(datagen::mix(datagen::hash64(rowIndex, col)) % 10)
                : static_cast<uint16_t>(0);
            row.set(col, static_cast<uint16_t>(base + noise));
        }

        // --- UInt32 monotonic counters ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 42 + i;
            row.set(col, static_cast<uint32_t>(effRow / (1 + i) + cycle * 10000));
        }

        // --- UInt64 nanosecond timers ---
        for (size_t i = 0; i < 6; ++i) {
            size_t col = 48 + i;
            row.set(col, static_cast<uint64_t>(effRow) * static_cast<uint64_t>(1000 + i * 500)
                       + static_cast<uint64_t>(cycle) * uint64_t{1000000});
        }

        // --- Float sensors (SIMD groups): drift + jitter, signal loss ---
        for (size_t i = 0; i < 12; ++i) {
            size_t col = 54 + i;
            float base = 20.0f + static_cast<float>(i) * 5.0f + static_cast<float>(cycle) * 2.0f;
            float drift = static_cast<float>(effRow) * 0.005f;

            // Signal loss: ~2% of active rows go flat (sensor reading stuck)
            uint64_t h = datagen::mix(datagen::hash64(rowIndex, col));
            bool signalLost = active && (h % 50) == 0;

            float jitter = 0.0f;
            if (active && !signalLost) {
                jitter = static_cast<float>(static_cast<int32_t>(h % 200) - 100) * 0.002f;
            }
            row.set(col, signalLost ? base : (base + drift + jitter));
        }

        // --- Double sensors (SIMD groups): drift + jitter, signal loss ---
        for (size_t i = 0; i < 12; ++i) {
            size_t col = 66 + i;
            double base = 100.0 + static_cast<double>(i) * 15.0 + static_cast<double>(cycle) * 5.0;
            double drift = static_cast<double>(effRow) * 0.001;

            uint64_t h = datagen::mix(datagen::hash64(rowIndex, col));
            bool signalLost = active && (h % 50) == 0;

            double jitter = 0.0;
            if (active && !signalLost) {
                jitter = static_cast<double>(static_cast<int64_t>(h % 400) - 200) * 0.0005;
            }
            row.set(col, signalLost ? base : (base + drift + jitter));
        }

        // --- String metadata: static labels, change per cycle ---
        static const std::array<std::string, 6> testNames = {
            "Thermal_Cycle_v3", "Vibration_Test", "Endurance_Run",
            "Calibration_Check", "Stress_Profile_A", "Baseline_Sweep"
        };
        static const std::array<std::string, 6> dutIds = {
            "DUT-2026-0042", "DUT-2026-0043", "DUT-2026-0044",
            "DUT-2026-0101", "DUT-2026-0102", "DUT-2026-0103"
        };
        row.set(static_cast<size_t>(78), testNames[cycle % testNames.size()]);
        row.set(static_cast<size_t>(79), dutIds[cycle % dutIds.size()]);
        // Phase label: derived from position in cycle
        const size_t posInCycle = rowIndex % cycleLen;
        const size_t activeLen = cycleLen * 7 / 10;
        const char* phaseLabel = !active ? "standstill"
            : (posInCycle < activeLen / 10) ? "warmup"
            : (posInCycle < activeLen * 9 / 10) ? "active"
            : "cooldown";
        row.set(static_cast<size_t>(80), phaseLabel);
        static const std::array<const char*, 4> operatorLabels = {
            "Operator_0", "Operator_1", "Operator_2", "Operator_3"
        };
        row.set(static_cast<size_t>(81), operatorLabels[cycle % 4]);
        // Remaining strings: batch/run labels
        size_t strSeg = effRow / 400;
        row.set(static_cast<size_t>(82), std::string("batch_") + std::to_string(strSeg % 20));
        row.set(static_cast<size_t>(83), std::string("run_") + std::to_string(cycle));
    };

    return p;
}

// ============================================================================
// Registry — get all available dataset profiles
// ============================================================================

inline const std::vector<DatasetProfile>& getAllProfilesCached() {
    static const std::vector<DatasetProfile> profiles = {
        createMixedGenericProfile(),
        createSparseEventsProfile(),
        createSensorNoisyProfile(),
        createStringHeavyProfile(),
        createBoolHeavyProfile(),
        createArithmeticWideProfile(),
        createSimulationSmoothProfile(),
        createWeatherTimeseriesProfile(),
        createHighCardinalityStringProfile(),
        createEventLogProfile(),
        createIotFleetProfile(),
        createFinancialOrdersProfile(),
        createRealisticMeasurementProfile(),
        createRtlWaveformProfile(),
        createMeasurementCampaignProfile()
    };
    return profiles;
}

inline std::vector<DatasetProfile> getAllProfiles() {
    return getAllProfilesCached();
}

inline DatasetProfile getProfile(const std::string& name) {
    const auto& profiles = getAllProfilesCached();
    static const std::unordered_map<std::string, size_t> profile_index = [] {
        std::unordered_map<std::string, size_t> index;
        const auto& all = getAllProfilesCached();
        for (size_t i = 0; i < all.size(); ++i) {
            index.emplace(all[i].name, i);
        }
        return index;
    }();

    auto it = profile_index.find(name);
    if (it != profile_index.end()) {
        return profiles[it->second];
    }

    throw std::runtime_error("Unknown dataset profile: " + name);
}

inline std::vector<std::string> getProfileNames() {
    std::vector<std::string> names;
    const auto& profiles = getAllProfilesCached();
    names.reserve(profiles.size());
    for (const auto& p : profiles) names.push_back(p.name);
    return names;
}

} // namespace bench
