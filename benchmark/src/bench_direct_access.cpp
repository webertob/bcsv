/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bench_direct_access.cpp
 * @brief Google Benchmark suite for ReaderDirectAccess::read(size_t index).
 *
 * Benchmarks cover realistic access patterns:
 *   - Sequential readNext() baseline
 *   - Direct access: full sequential via read(i)
 *   - Head(N) — first N rows
 *   - Tail(N) — last N rows
 *   - Slice — range in middle
 *   - Random access — uniformly random row indices
 *   - Jump — alternating near-start / near-end
 *
 * Each pattern is tested for both compressed (LZ4, default) and uncompressed
 * codecs, across multiple file sizes.
 */

#include <benchmark/benchmark.h>
#include <bcsv/bcsv.h>
#include <bcsv/bcsv.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// Helpers
// ============================================================================

static const std::string BENCH_DIR = "bcsv_test_files/bench_direct_access";

static std::string benchFile(const std::string& name) {
    return (fs::path(BENCH_DIR) / name).string();
}

static bcsv::Layout makeLayout() {
    bcsv::Layout layout;
    layout.addColumn({"time",  bcsv::ColumnType::DOUBLE});
    layout.addColumn({"x",     bcsv::ColumnType::FLOAT});
    layout.addColumn({"y",     bcsv::ColumnType::FLOAT});
    layout.addColumn({"id",    bcsv::ColumnType::INT32});
    layout.addColumn({"flag",  bcsv::ColumnType::BOOL});
    layout.addColumn({"label", bcsv::ColumnType::STRING});
    return layout;
}

static void writeFile(const std::string& path, size_t nRows,
                      size_t compression, size_t blockSizeKB = 64) {
    if (fs::exists(path)) return;  // Reuse if already written
    fs::create_directories(fs::path(path).parent_path());

    auto layout = makeLayout();
    bcsv::Writer<bcsv::Layout> writer(layout);
    if (!writer.open(path, true, compression, blockSizeKB)) {
        throw std::runtime_error("Bench: failed to open writer for " + path);
    }
    for (size_t i = 0; i < nRows; ++i) {
        writer.row().set<double>(0, static_cast<double>(i) * 0.001);
        writer.row().set<float>(1,  static_cast<float>(i) * 1.5f);
        writer.row().set<float>(2,  static_cast<float>(i) * -0.7f);
        writer.row().set<int32_t>(3, static_cast<int32_t>(i));
        writer.row().set(4, (i % 3 == 0));
        writer.row().set(5, std::string("row_") + std::to_string(i));
        writer.writeRow();
    }
    writer.close();
}

static void ensureFile(const std::string& tag, size_t nRows, size_t compression) {
    std::string name = tag + "_" + std::to_string(nRows) + "_c" + std::to_string(compression) + ".bcsv";
    writeFile(benchFile(name), nRows, compression);
}

static std::string filePath(const std::string& tag, size_t nRows, size_t compression) {
    return benchFile(tag + "_" + std::to_string(nRows) + "_c" + std::to_string(compression) + ".bcsv");
}

// ============================================================================
// Baseline: sequential readNext() over entire file
// ============================================================================

static void BM_Sequential_ReadNext(benchmark::State& state) {
    size_t nRows = static_cast<size_t>(state.range(0));
    size_t comp  = static_cast<size_t>(state.range(1));
    ensureFile("seq", nRows, comp);

    for (auto _ : state) {
        bcsv::Reader<bcsv::Layout> reader;
        reader.open(filePath("seq", nRows, comp));
        while (reader.readNext()) {
            auto v = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(nRows) * state.iterations());
}

// ============================================================================
// Direct access: full sequential via read(i)
// ============================================================================

static void BM_DirectAccess_FullSequential(benchmark::State& state) {
    size_t nRows = static_cast<size_t>(state.range(0));
    size_t comp  = static_cast<size_t>(state.range(1));
    ensureFile("daseq", nRows, comp);

    for (auto _ : state) {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        reader.open(filePath("daseq", nRows, comp));
        for (size_t i = 0; i < nRows; ++i) {
            reader.read(i);
            auto v = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(nRows) * state.iterations());
}

// ============================================================================
// Head: first N rows
// ============================================================================

static void BM_DirectAccess_Head(benchmark::State& state) {
    size_t nRows = static_cast<size_t>(state.range(0));
    size_t head  = static_cast<size_t>(state.range(1));
    size_t comp  = static_cast<size_t>(state.range(2));
    ensureFile("head", nRows, comp);

    for (auto _ : state) {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        reader.open(filePath("head", nRows, comp));
        for (size_t i = 0; i < head; ++i) {
            reader.read(i);
            auto v = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(head) * state.iterations());
}

// ============================================================================
// Tail: last N rows
// ============================================================================

static void BM_DirectAccess_Tail(benchmark::State& state) {
    size_t nRows = static_cast<size_t>(state.range(0));
    size_t tail  = static_cast<size_t>(state.range(1));
    size_t comp  = static_cast<size_t>(state.range(2));
    ensureFile("tail", nRows, comp);

    for (auto _ : state) {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        reader.open(filePath("tail", nRows, comp));
        for (size_t i = nRows - tail; i < nRows; ++i) {
            reader.read(i);
            auto v = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(tail) * state.iterations());
}

// ============================================================================
// Slice: range in middle
// ============================================================================

static void BM_DirectAccess_Slice(benchmark::State& state) {
    size_t nRows = static_cast<size_t>(state.range(0));
    size_t sliceLen = static_cast<size_t>(state.range(1));
    size_t comp = static_cast<size_t>(state.range(2));
    ensureFile("slice", nRows, comp);

    size_t start = nRows / 2 - sliceLen / 2;

    for (auto _ : state) {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        reader.open(filePath("slice", nRows, comp));
        for (size_t i = start; i < start + sliceLen; ++i) {
            reader.read(i);
            auto v = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(sliceLen) * state.iterations());
}

// ============================================================================
// Random access: uniformly random row indices
// ============================================================================

static void BM_DirectAccess_Random(benchmark::State& state) {
    size_t nRows = static_cast<size_t>(state.range(0));
    size_t nAccess = static_cast<size_t>(state.range(1));
    size_t comp = static_cast<size_t>(state.range(2));
    ensureFile("rand", nRows, comp);

    // Pre-generate random indices
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, nRows - 1);
    std::vector<size_t> indices(nAccess);
    for (auto& idx : indices) idx = dist(rng);

    for (auto _ : state) {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        reader.open(filePath("rand", nRows, comp));
        for (size_t idx : indices) {
            reader.read(idx);
            auto v = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(nAccess) * state.iterations());
}

// ============================================================================
// Jump: head↔tail alternation
// ============================================================================

static void BM_DirectAccess_Jump(benchmark::State& state) {
    size_t nRows  = static_cast<size_t>(state.range(0));
    size_t nJumps = static_cast<size_t>(state.range(1));
    size_t comp   = static_cast<size_t>(state.range(2));
    ensureFile("jump", nRows, comp);

    for (auto _ : state) {
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        reader.open(filePath("jump", nRows, comp));
        for (size_t j = 0; j < nJumps; ++j) {
            reader.read(j * 10);                      // near start
            auto v1 = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v1);
            reader.read(nRows - 1 - j * 10);          // near end
            auto v2 = reader.row().get<double>(0);
            benchmark::DoNotOptimize(v2);
        }
        reader.close();
    }
    state.SetItemsProcessed(static_cast<int64_t>(nJumps * 2) * state.iterations());
}

// ============================================================================
// Registration
// ============================================================================

// Baseline: sequential readNext   (rows, compression)
BENCHMARK(BM_Sequential_ReadNext)
    ->Args({10000, 1})->Args({100000, 1})
    ->Args({10000, 0})->Args({100000, 0})
    ->Unit(benchmark::kMicrosecond);

// Full sequential via read(i)     (rows, compression)
BENCHMARK(BM_DirectAccess_FullSequential)
    ->Args({10000, 1})->Args({100000, 1})
    ->Args({10000, 0})->Args({100000, 0})
    ->Unit(benchmark::kMicrosecond);

// Head                            (rows, head_count, compression)
BENCHMARK(BM_DirectAccess_Head)
    ->Args({100000, 100, 1})->Args({100000, 100, 0})
    ->Args({100000, 1000, 1})->Args({100000, 1000, 0})
    ->Unit(benchmark::kMicrosecond);

// Tail
BENCHMARK(BM_DirectAccess_Tail)
    ->Args({100000, 100, 1})->Args({100000, 100, 0})
    ->Args({100000, 1000, 1})->Args({100000, 1000, 0})
    ->Unit(benchmark::kMicrosecond);

// Slice
BENCHMARK(BM_DirectAccess_Slice)
    ->Args({100000, 100, 1})->Args({100000, 100, 0})
    ->Args({100000, 1000, 1})->Args({100000, 1000, 0})
    ->Unit(benchmark::kMicrosecond);

// Random
BENCHMARK(BM_DirectAccess_Random)
    ->Args({100000, 100, 1})->Args({100000, 100, 0})
    ->Args({100000, 1000, 1})->Args({100000, 1000, 0})
    ->Unit(benchmark::kMicrosecond);

// Jump
BENCHMARK(BM_DirectAccess_Jump)
    ->Args({100000, 50, 1})->Args({100000, 50, 0})
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Cleanup hook
// ============================================================================

static void cleanupBenchFiles() {
    if (fs::exists(BENCH_DIR)) {
        fs::remove_all(BENCH_DIR);
    }
}

// Use atexit for cleanup
static bool registered = (std::atexit(cleanupBenchFiles), true);

BENCHMARK_MAIN();
