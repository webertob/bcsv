/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_micro_types.cpp
 * @brief Micro-benchmarks for per-type operations using Google Benchmark
 * 
 * Measures:
 * - Row::get<T>() latency per type
 * - Row::set<T>() latency per type
 * - Row::visitConst() full iteration throughput
 * - CsvWriter::writeRow() via visitConst() throughput
 * 
 * Usage:
 *   bench_micro_types [Google Benchmark flags]
 *   bench_micro_types --benchmark_format=json --benchmark_out=results.json
 */

#include <benchmark/benchmark.h>
#include <bcsv/bcsv.h>
#include "bench_common.hpp"
#include "bench_datasets.hpp"

#include <sstream>
#include <string>

// ============================================================================
// Shared fixture: a warm row with mixed types
// ============================================================================

namespace {

/// Create a 12-column layout with one column per type
bcsv::Layout createMicroLayout() {
    bcsv::Layout layout;
    layout.addColumn({"c_bool",   bcsv::ColumnType::BOOL});
    layout.addColumn({"c_int8",   bcsv::ColumnType::INT8});
    layout.addColumn({"c_int16",  bcsv::ColumnType::INT16});
    layout.addColumn({"c_int32",  bcsv::ColumnType::INT32});
    layout.addColumn({"c_int64",  bcsv::ColumnType::INT64});
    layout.addColumn({"c_uint8",  bcsv::ColumnType::UINT8});
    layout.addColumn({"c_uint16", bcsv::ColumnType::UINT16});
    layout.addColumn({"c_uint32", bcsv::ColumnType::UINT32});
    layout.addColumn({"c_uint64", bcsv::ColumnType::UINT64});
    layout.addColumn({"c_float",  bcsv::ColumnType::FLOAT});
    layout.addColumn({"c_double", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"c_string", bcsv::ColumnType::STRING});
    return layout;
}

/// Fill a row with representative data
void fillMicroRow(bcsv::Row& row) {
    row.set(static_cast<size_t>(0), true);
    row.set(static_cast<size_t>(1), static_cast<int8_t>(42));
    row.set(static_cast<size_t>(2), static_cast<int16_t>(1234));
    row.set(static_cast<size_t>(3), static_cast<int32_t>(987654));
    row.set(static_cast<size_t>(4), static_cast<int64_t>(123456789012LL));
    row.set(static_cast<size_t>(5), static_cast<uint8_t>(200));
    row.set(static_cast<size_t>(6), static_cast<uint16_t>(50000));
    row.set(static_cast<size_t>(7), static_cast<uint32_t>(3000000000U));
    row.set(static_cast<size_t>(8), static_cast<uint64_t>(9999999999999ULL));
    row.set(static_cast<size_t>(9), 3.14159f);
    row.set(static_cast<size_t>(10), 2.718281828459045);
    row.set(static_cast<size_t>(11), std::string("benchmark_test_string"));
}

static bcsv::Layout g_microLayout = createMicroLayout();

} // anonymous namespace

// ============================================================================
// get<T>() micro-benchmarks — one per type
// ============================================================================

static void BM_Get_Bool(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        bool v = row.get<bool>(0);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Bool);

static void BM_Get_Int8(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        int8_t v = row.get<int8_t>(1);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Int8);

static void BM_Get_Int16(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        int16_t v = row.get<int16_t>(2);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Int16);

static void BM_Get_Int32(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        int32_t v = row.get<int32_t>(3);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Int32);

static void BM_Get_Int64(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        int64_t v = row.get<int64_t>(4);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Int64);

static void BM_Get_UInt8(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        uint8_t v = row.get<uint8_t>(5);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_UInt8);

static void BM_Get_UInt16(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        uint16_t v = row.get<uint16_t>(6);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_UInt16);

static void BM_Get_UInt32(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        uint32_t v = row.get<uint32_t>(7);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_UInt32);

static void BM_Get_UInt64(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        uint64_t v = row.get<uint64_t>(8);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_UInt64);

static void BM_Get_Float(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        float v = row.get<float>(9);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Float);

static void BM_Get_Double(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        double v = row.get<double>(10);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_Double);

static void BM_Get_String(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        const std::string& v = row.get<std::string>(11);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Get_String);

// ============================================================================
// set<T>() micro-benchmarks
// ============================================================================

static void BM_Set_Bool(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    bool val = true;
    for (auto _ : state) {
        row.set(static_cast<size_t>(0), val);
        val = !val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Bool);

static void BM_Set_Int8(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    int8_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(1), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Int8);

static void BM_Set_Int16(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    int16_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(2), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Int16);

static void BM_Set_Int32(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    int32_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(3), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Int32);

static void BM_Set_Int64(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    int64_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(4), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Int64);

static void BM_Set_UInt8(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    uint8_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(5), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_UInt8);

static void BM_Set_UInt16(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    uint16_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(6), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_UInt16);

static void BM_Set_UInt32(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    uint32_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(7), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_UInt32);

static void BM_Set_UInt64(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    uint64_t val = 0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(8), val);
        ++val;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_UInt64);

static void BM_Set_Float(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    float val = 0.0f;
    for (auto _ : state) {
        row.set(static_cast<size_t>(9), val);
        val += 0.1f;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Float);

static void BM_Set_Double(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    double val = 0.0;
    for (auto _ : state) {
        row.set(static_cast<size_t>(10), val);
        val += 0.1;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_Double);

static void BM_Set_String(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    std::string val = "benchmark_string_value";
    for (auto _ : state) {
        row.set(static_cast<size_t>(11), val);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Set_String);

// ============================================================================
// visitConst() — full row iteration throughput
// ============================================================================

static void BM_VisitConst_12col(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    
    for (auto _ : state) {
        size_t checksum = 0;
        row.visitConst([&checksum](size_t index, const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_arithmetic_v<T>) {
                checksum += static_cast<size_t>(value);
            } else {
                checksum += value.size();
            }
        });
        benchmark::DoNotOptimize(checksum);
    }
}
BENCHMARK(BM_VisitConst_12col);

// visitConst on a larger layout (72 columns, mixed_generic)
static void BM_VisitConst_72col(benchmark::State& state) {
    auto profile = bench::createMixedGenericProfile();
    bcsv::Row row(profile.layout);
    bench::datagen::fillRowRandom(row, 42, profile.layout);
    
    for (auto _ : state) {
        size_t checksum = 0;
        row.visitConst([&checksum](size_t index, const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_arithmetic_v<T>) {
                checksum += static_cast<size_t>(value);
            } else {
                checksum += value.size();
            }
        });
        benchmark::DoNotOptimize(checksum);
    }
}
BENCHMARK(BM_VisitConst_72col);

// ============================================================================
// CsvWriter via visitConst() — measures the CSV serialization path
// ============================================================================

static void BM_CsvWriteRow_12col(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    
    std::ostringstream oss;
    bench::CsvWriter csvWriter(oss);
    
    for (auto _ : state) {
        oss.str("");
        oss.clear();
        csvWriter.writeRow(row);
        benchmark::DoNotOptimize(oss.str());
    }
}
BENCHMARK(BM_CsvWriteRow_12col);

static void BM_CsvWriteRow_72col(benchmark::State& state) {
    auto profile = bench::createMixedGenericProfile();
    bcsv::Row row(profile.layout);
    bench::datagen::fillRowRandom(row, 42, profile.layout);
    
    std::ostringstream oss;
    bench::CsvWriter csvWriter(oss);
    
    for (auto _ : state) {
        oss.str("");
        oss.clear();
        csvWriter.writeRow(row);
        benchmark::DoNotOptimize(oss.str());
    }
}
BENCHMARK(BM_CsvWriteRow_72col);

// ============================================================================
// Serialize/Deserialize row (binary wire format)
// ============================================================================

static void BM_SerializeTo_12col(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    bcsv::ByteBuffer buffer;
    buffer.reserve(4096);
    
    for (auto _ : state) {
        buffer.clear();
        auto span = row.serializeTo(buffer);
        benchmark::DoNotOptimize(span);
    }
}
BENCHMARK(BM_SerializeTo_12col);

static void BM_DeserializeFrom_12col(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    bcsv::ByteBuffer buffer;
    row.serializeTo(buffer);
    
    bcsv::Row target(g_microLayout);
    std::span<const std::byte> data(buffer.data(), buffer.size());
    
    for (auto _ : state) {
        target.deserializeFrom(data);
        benchmark::DoNotOptimize(target);
    }
}
BENCHMARK(BM_DeserializeFrom_12col);

// ============================================================================
// Row construction and copy
// ============================================================================

static void BM_RowConstruct_12col(benchmark::State& state) {
    for (auto _ : state) {
        bcsv::Row row(g_microLayout);
        benchmark::DoNotOptimize(row);
    }
}
BENCHMARK(BM_RowConstruct_12col);

static void BM_RowClear_12col(benchmark::State& state) {
    bcsv::Row row(g_microLayout);
    fillMicroRow(row);
    for (auto _ : state) {
        row.clear();
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_RowClear_12col);

BENCHMARK_MAIN();
