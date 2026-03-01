/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bench_c_api.cpp
 * @brief Google Benchmark suite for the BCSV C API.
 *
 * Benchmarks cover:
 *   - Writer throughput (Flat / ZoH / Delta)
 *   - Reader throughput (sequential + random access)
 *   - Vectorized double-array access (3D coordinates)
 *   - C API vs C++ API overhead comparison
 *   - CSV round-trip throughput
 *
 * Files are written to a temporary directory and cleaned up after each run.
 */

#include <benchmark/benchmark.h>

// C API header (extern "C" is inside the header)
#include <bcsv/bcsv_c_api.h>

// C++ API for comparison benchmarks
#include <bcsv/bcsv.h>
#include <bcsv/bcsv.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <random>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// Helpers
// ============================================================================

static const std::string BENCH_DIR = "/tmp/bcsv_bench_c_api";

static void ensureBenchDir() {
    fs::create_directories(BENCH_DIR);
}

static std::string benchFile(const std::string& name) {
    return (fs::path(BENCH_DIR) / name).string();
}

// Standard layout: id(i32), x(f64), y(f64), z(f64), label(str), flag(bool)
static bcsv_layout_t makeCLayout() {
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",    BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "x",     BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "y",     BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 3, "z",     BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 4, "label", BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 5, "flag",  BCSV_TYPE_BOOL);
    return layout;
}

static bcsv::Layout makeCppLayout() {
    bcsv::Layout layout;
    layout.addColumn({"id",    bcsv::ColumnType::INT32});
    layout.addColumn({"x",     bcsv::ColumnType::DOUBLE});
    layout.addColumn({"y",     bcsv::ColumnType::DOUBLE});
    layout.addColumn({"z",     bcsv::ColumnType::DOUBLE});
    layout.addColumn({"label", bcsv::ColumnType::STRING});
    layout.addColumn({"flag",  bcsv::ColumnType::BOOL});
    return layout;
}

static void fillCRow(bcsv_row_t row, int i) {
    bcsv_row_set_int32 (row, 0, i);
    bcsv_row_set_double(row, 1, i * 0.1);
    bcsv_row_set_double(row, 2, i * 0.2);
    bcsv_row_set_double(row, 3, i * 0.3);
    bcsv_row_set_string(row, 4, "label");
    bcsv_row_set_bool  (row, 5, (i & 1) == 0);
}

// ============================================================================
// C API Writer Throughput
// ============================================================================

enum CWriterKind { CW_FLAT, CW_ZOH, CW_DELTA };

static void BM_CApi_Writer(benchmark::State& state, CWriterKind kind) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();

    const char* tag = (kind == CW_FLAT) ? "flat" : (kind == CW_ZOH) ? "zoh" : "delta";
    std::string path = benchFile(std::string("bench_capi_") + tag + ".bcsv");

    for (auto _ : state) {
        bcsv_layout_t layout = makeCLayout();
        bcsv_writer_t writer = nullptr;
        switch (kind) {
            case CW_FLAT:  writer = bcsv_writer_create(layout);       break;
            case CW_ZOH:   writer = bcsv_writer_create_zoh(layout);   break;
            case CW_DELTA: writer = bcsv_writer_create_delta(layout); break;
        }
        bcsv_writer_open(writer, path.c_str(), true, 1, 64, BCSV_FLAG_NONE);

        bcsv_row_t row = bcsv_writer_row(writer);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_writer_next(writer);
        }
        bcsv_writer_close(writer);
        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
    }

    state.SetItemsProcessed(state.iterations() * nRows);
    state.SetLabel(tag);
}

BENCHMARK_CAPTURE(BM_CApi_Writer, Flat,  CW_FLAT) ->Range(1000, 100000)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_CApi_Writer, ZoH,   CW_ZOH)  ->Range(1000, 100000)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(BM_CApi_Writer, Delta, CW_DELTA)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
// C API Reader Throughput (Sequential)
// ============================================================================

static void BM_CApi_Reader_Sequential(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_capi_read_seq.bcsv");

    // Write fixture
    {
        bcsv_layout_t layout = makeCLayout();
        bcsv_writer_t writer = bcsv_writer_create(layout);
        bcsv_writer_open(writer, path.c_str(), true, 1, 64, BCSV_FLAG_NONE);
        bcsv_row_t row = bcsv_writer_row(writer);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_writer_next(writer);
        }
        bcsv_writer_close(writer);
        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
    }

    for (auto _ : state) {
        bcsv_reader_t reader = bcsv_reader_create();
        bcsv_reader_open(reader, path.c_str());
        int64_t count = 0;
        while (bcsv_reader_next(reader)) {
            const_bcsv_row_t row = bcsv_reader_row(reader);
            benchmark::DoNotOptimize(bcsv_row_get_int32(row, 0));
            ++count;
        }
        benchmark::ClobberMemory();
        bcsv_reader_close(reader);
        bcsv_reader_destroy(reader);
    }
    state.SetItemsProcessed(state.iterations() * nRows);
}
BENCHMARK(BM_CApi_Reader_Sequential)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
// C API Reader: Random Access
// ============================================================================

static void BM_CApi_Reader_RandomAccess(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    const int64_t nReads = 1000; // number of random reads per iteration
    ensureBenchDir();
    std::string path = benchFile("bench_capi_read_rand.bcsv");

    // Write fixture
    {
        bcsv_layout_t layout = makeCLayout();
        bcsv_writer_t writer = bcsv_writer_create(layout);
        bcsv_writer_open(writer, path.c_str(), true, 1, 64, BCSV_FLAG_NONE);
        bcsv_row_t row = bcsv_writer_row(writer);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_writer_next(writer);
        }
        bcsv_writer_close(writer);
        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
    }

    // Pre-generate random indices
    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> dist(0, nRows - 1);
    std::vector<size_t> indices(static_cast<size_t>(nReads));
    for (auto& idx : indices) idx = static_cast<size_t>(dist(rng));

    for (auto _ : state) {
        bcsv_reader_t reader = bcsv_reader_create();
        bcsv_reader_open(reader, path.c_str());
        for (auto idx : indices) {
            bcsv_reader_read(reader, idx);
            benchmark::DoNotOptimize(bcsv_row_get_int32(bcsv_reader_row(reader), 0));
        }
        bcsv_reader_close(reader);
        bcsv_reader_destroy(reader);
    }
    state.SetItemsProcessed(state.iterations() * nReads);
}
BENCHMARK(BM_CApi_Reader_RandomAccess)->Arg(10000)->Arg(100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
// Vectorized 3D coordinate access (double arrays)
// ============================================================================

static void BM_CApi_VectorizedXYZ(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_capi_xyz.bcsv");

    // Write fixture with 3D doubles
    {
        bcsv_layout_t layout = makeCLayout();
        bcsv_writer_t writer = bcsv_writer_create(layout);
        bcsv_writer_open(writer, path.c_str(), true, 1, 64, BCSV_FLAG_NONE);
        bcsv_row_t row = bcsv_writer_row(writer);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_writer_next(writer);
        }
        bcsv_writer_close(writer);
        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
    }

    for (auto _ : state) {
        bcsv_reader_t reader = bcsv_reader_create();
        bcsv_reader_open(reader, path.c_str());
        double xyz[3];
        while (bcsv_reader_next(reader)) {
            const_bcsv_row_t row = bcsv_reader_row(reader);
            // Vectorized read: x,y,z at columns 1,2,3
            bcsv_row_get_double_array(row, 1, xyz, 3);
            benchmark::DoNotOptimize(xyz[0] + xyz[1] + xyz[2]);
        }
        benchmark::ClobberMemory();
        bcsv_reader_close(reader);
        bcsv_reader_destroy(reader);
    }
    state.SetItemsProcessed(state.iterations() * nRows);
    state.SetLabel("3D double vector");
}
BENCHMARK(BM_CApi_VectorizedXYZ)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
// C API vs C++ API overhead comparison — writer
// ============================================================================

static void BM_CppApi_Writer(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_cpp_writer.bcsv");

    for (auto _ : state) {
        auto layout = makeCppLayout();
        bcsv::WriterFlat<bcsv::Layout> writer(layout);
        writer.open(path, true, 1, 64);

        auto& row = writer.row();
        for (int64_t i = 0; i < nRows; ++i) {
            row.set<int32_t>(0, static_cast<int32_t>(i));
            row.set<double>(1, i * 0.1);
            row.set<double>(2, i * 0.2);
            row.set<double>(3, i * 0.3);
            row.set<std::string>(4, "label");
            row.set<bool>(5, (i & 1) == 0);
            writer.writeRow();
        }
        writer.close();
    }
    state.SetItemsProcessed(state.iterations() * nRows);
    state.SetLabel("C++ baseline");
}
BENCHMARK(BM_CppApi_Writer)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

static void BM_CApi_Writer_Flat_Compare(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_capi_flat_cmp.bcsv");

    for (auto _ : state) {
        bcsv_layout_t layout = makeCLayout();
        bcsv_writer_t writer = bcsv_writer_create(layout);
        bcsv_writer_open(writer, path.c_str(), true, 1, 64, BCSV_FLAG_NONE);

        bcsv_row_t row = bcsv_writer_row(writer);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_writer_next(writer);
        }
        bcsv_writer_close(writer);
        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
    }
    state.SetItemsProcessed(state.iterations() * nRows);
    state.SetLabel("C API");
}
BENCHMARK(BM_CApi_Writer_Flat_Compare)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
// C API vs C++ API overhead comparison — reader
// ============================================================================

static void BM_CppApi_Reader(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_cpp_reader.bcsv");

    // Write fixture with C++ API
    {
        auto layout = makeCppLayout();
        bcsv::WriterFlat<bcsv::Layout> writer(layout);
        writer.open(path, true, 1, 64);
        auto& row = writer.row();
        for (int64_t i = 0; i < nRows; ++i) {
            row.set<int32_t>(0, static_cast<int32_t>(i));
            row.set<double>(1, i * 0.1);
            row.set<double>(2, i * 0.2);
            row.set<double>(3, i * 0.3);
            row.set<std::string>(4, "label");
            row.set<bool>(5, (i & 1) == 0);
            writer.writeRow();
        }
        writer.close();
    }

    for (auto _ : state) {
        bcsv::Reader<bcsv::Layout> reader;
        reader.open(path);
        while (reader.readNext()) {
            benchmark::DoNotOptimize(reader.row().get<int32_t>(0));
        }
        benchmark::ClobberMemory();
        reader.close();
    }
    state.SetItemsProcessed(state.iterations() * nRows);
    state.SetLabel("C++ baseline");
}
BENCHMARK(BM_CppApi_Reader)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

static void BM_CApi_Reader_Compare(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_capi_read_cmp.bcsv");

    // Write fixture with C API
    {
        bcsv_layout_t layout = makeCLayout();
        bcsv_writer_t writer = bcsv_writer_create(layout);
        bcsv_writer_open(writer, path.c_str(), true, 1, 64, BCSV_FLAG_NONE);
        bcsv_row_t row = bcsv_writer_row(writer);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_writer_next(writer);
        }
        bcsv_writer_close(writer);
        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
    }

    for (auto _ : state) {
        bcsv_reader_t reader = bcsv_reader_create();
        bcsv_reader_open(reader, path.c_str());
        while (bcsv_reader_next(reader)) {
            benchmark::DoNotOptimize(bcsv_row_get_int32(bcsv_reader_row(reader), 0));
        }
        benchmark::ClobberMemory();
        bcsv_reader_close(reader);
        bcsv_reader_destroy(reader);
    }
    state.SetItemsProcessed(state.iterations() * nRows);
    state.SetLabel("C API");
}
BENCHMARK(BM_CApi_Reader_Compare)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
// CSV round-trip through C API
// ============================================================================

static void BM_CApi_CSV_Roundtrip(benchmark::State& state) {
    const int64_t nRows = state.range(0);
    ensureBenchDir();
    std::string path = benchFile("bench_capi_csv.csv");

    for (auto _ : state) {
        bcsv_layout_t layout = makeCLayout();

        // Write CSV
        bcsv_csv_writer_t cw = bcsv_csv_writer_create(layout, ',', '.');
        bcsv_csv_writer_open(cw, path.c_str(), true, true);
        bcsv_row_t row = bcsv_csv_writer_row(cw);
        for (int64_t i = 0; i < nRows; ++i) {
            fillCRow(row, static_cast<int>(i));
            bcsv_csv_writer_next(cw);
        }
        bcsv_csv_writer_close(cw);
        bcsv_csv_writer_destroy(cw);

        // Read CSV
        bcsv_csv_reader_t cr = bcsv_csv_reader_create(layout, ',', '.');
        bcsv_csv_reader_open(cr, path.c_str(), true);
        int64_t count = 0;
        while (bcsv_csv_reader_next(cr)) {
            benchmark::DoNotOptimize(bcsv_row_get_int32(bcsv_csv_reader_row(cr), 0));
            ++count;
        }
        benchmark::ClobberMemory();
        bcsv_csv_reader_close(cr);
        bcsv_csv_reader_destroy(cr);
        bcsv_layout_destroy(layout);
    }
    state.SetItemsProcessed(state.iterations() * nRows * 2); // write + read
    state.SetLabel("CSV write+read");
}
BENCHMARK(BM_CApi_CSV_Roundtrip)->Range(1000, 100000)->Unit(benchmark::kMicrosecond);

// ============================================================================
BENCHMARK_MAIN();
