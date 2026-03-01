/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_sampler.cpp
 * @brief Micro-benchmarks for the BCSV Sampler using Google Benchmark
 *
 * Measures:
 * - Sampler throughput for pass-through (true / wildcard)
 * - Sampler throughput with a simple float conditional
 * - Sampler throughput with lookbehind + selection arithmetic (gradient)
 * - Sampler throughput with 3-point moving average (lookbehind + lookahead)
 * - Sampler compilation latency
 *
 * Usage:
 *   bench_sampler [Google Benchmark flags]
 *   bench_sampler --benchmark_format=json --benchmark_out=results.json
 */

#include <benchmark/benchmark.h>
#include <bcsv/bcsv.h>
#include <bcsv/sampler.h>
#include <bcsv/sampler.hpp>

#include <filesystem>
#include <string>
#include <cmath>

namespace fs = std::filesystem;
using namespace bcsv;

// ============================================================================
// Dataset generation — creates a temp BCSV file with N rows
// ============================================================================

namespace {

struct BenchData {
    std::string path;
    std::string dir;
};

BenchData createBenchFile(size_t rows) {
    static size_t counter = 0;
    BenchData bd;
    bd.dir = (fs::temp_directory_path() / ("bcsv_bench_sampler_" + std::to_string(++counter))).string();
    fs::create_directories(bd.dir);
    bd.path = bd.dir + "/bench.bcsv";

    Layout layout;
    layout.addColumn({"timestamp",   ColumnType::DOUBLE});
    layout.addColumn({"temperature", ColumnType::FLOAT});
    layout.addColumn({"status",      ColumnType::STRING});
    layout.addColumn({"flags",       ColumnType::UINT16});
    layout.addColumn({"counter",     ColumnType::INT32});

    Writer<Layout> writer(layout);
    writer.open(bd.path, true);

    for (size_t i = 0; i < rows; ++i) {
        double ts = static_cast<double>(i) * 0.001;
        float temp = 20.0f + 10.0f * std::sin(static_cast<float>(i) * 0.01f);
        std::string status = (i % 10 == 0) ? "alarm" : "ok";
        uint16_t flags = static_cast<uint16_t>(i & 0xFF);
        int32_t ctr = static_cast<int32_t>(i);

        writer.row().set(0, ts);
        writer.row().set(1, temp);
        writer.row().set(2, status);
        writer.row().set(3, flags);
        writer.row().set(4, ctr);
        writer.writeRow();
    }
    writer.close();
    return bd;
}

} // namespace

// ============================================================================
// BM_Sampler_Passthrough — true / X[0][*]
// ============================================================================

static void BM_Sampler_Passthrough(benchmark::State& state) {
    const size_t N = static_cast<size_t>(state.range(0));
    auto bd = createBenchFile(N);

    for (auto _ : state) {
        Reader<Layout> reader;
        reader.open(bd.path);
        Sampler<Layout> sampler(reader);
        sampler.setConditional("true");
        sampler.setSelection("X[0][*]");
        size_t count = 0;
        while (sampler.next()) { ++count; }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Sampler_Passthrough)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// BM_Sampler_FloatFilter — temperature > 25.0
// ============================================================================

static void BM_Sampler_FloatFilter(benchmark::State& state) {
    const size_t N = static_cast<size_t>(state.range(0));
    auto bd = createBenchFile(N);

    for (auto _ : state) {
        Reader<Layout> reader;
        reader.open(bd.path);
        Sampler<Layout> sampler(reader);
        sampler.setConditional("X[0][1] > 25.0");
        sampler.setSelection("X[0][0], X[0][1]");
        size_t count = 0;
        while (sampler.next()) { ++count; }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Sampler_FloatFilter)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// BM_Sampler_Gradient — dT/dt via lookbehind
// ============================================================================

static void BM_Sampler_Gradient(benchmark::State& state) {
    const size_t N = static_cast<size_t>(state.range(0));
    auto bd = createBenchFile(N);

    for (auto _ : state) {
        Reader<Layout> reader;
        reader.open(bd.path);
        Sampler<Layout> sampler(reader);
        sampler.setConditional("true");
        sampler.setSelection(
            "X[0][0], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])");
        size_t count = 0;
        while (sampler.next()) { ++count; }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Sampler_Gradient)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// BM_Sampler_MovingAvg3 — 3-point moving average (lookbehind + lookahead)
// ============================================================================

static void BM_Sampler_MovingAvg3(benchmark::State& state) {
    const size_t N = static_cast<size_t>(state.range(0));
    auto bd = createBenchFile(N);

    for (auto _ : state) {
        Reader<Layout> reader;
        reader.open(bd.path);
        Sampler<Layout> sampler(reader);
        sampler.setConditional("true");
        sampler.setSelection(
            "X[0][0], (X[-1][1] + X[0][1] + X[+1][1]) / 3.0");
        size_t count = 0;
        while (sampler.next()) { ++count; }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Sampler_MovingAvg3)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// BM_Sampler_CompileLatency — measures expression compilation time only
// ============================================================================

static void BM_Sampler_CompileLatency(benchmark::State& state) {
    const size_t N = 100;  // small file, compilation is the focus
    auto bd = createBenchFile(N);

    Reader<Layout> reader;
    reader.open(bd.path);

    for (auto _ : state) {
        Sampler<Layout> sampler(reader);
        auto r1 = sampler.setConditional(
            "(X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0]) > 1.0 || "
            "(X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0]) < -1.0");
        benchmark::DoNotOptimize(r1);
        auto r2 = sampler.setSelection(
            "X[0][0], X[0][1], (X[0][1] - X[-1][1]) / (X[0][0] - X[-1][0])");
        benchmark::DoNotOptimize(r2);
    }

    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Sampler_CompileLatency);

// ============================================================================
// BM_Sampler_StringFilter — status == "alarm"
// ============================================================================

static void BM_Sampler_StringFilter(benchmark::State& state) {
    const size_t N = static_cast<size_t>(state.range(0));
    auto bd = createBenchFile(N);

    for (auto _ : state) {
        Reader<Layout> reader;
        reader.open(bd.path);
        Sampler<Layout> sampler(reader);
        sampler.setConditional("X[0][2] == \"alarm\"");
        sampler.setSelection("X[0][0], X[0][2]");
        size_t count = 0;
        while (sampler.next()) { ++count; }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Sampler_StringFilter)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// BM_Reader_Baseline — raw Reader throughput (no sampler) for comparison
// ============================================================================

static void BM_Reader_Baseline(benchmark::State& state) {
    const size_t N = static_cast<size_t>(state.range(0));
    auto bd = createBenchFile(N);

    for (auto _ : state) {
        Reader<Layout> reader;
        reader.open(bd.path);
        size_t count = 0;
        while (reader.readNext()) { ++count; }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(N));
    fs::remove_all(bd.dir);
}
BENCHMARK(BM_Reader_Baseline)->Arg(1000)->Arg(10000)->Arg(100000);

BENCHMARK_MAIN();
