/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bench_micro_bitset.cpp
 * @brief Micro-benchmarks for Bitset block operations (equalRange / assignRange).
 *
 * Measures word-granularity subrange compare and assign at various sizes
 * and alignments, plus a baseline per-bit loop for comparison.
 */

#include <benchmark/benchmark.h>
#include <bcsv/bitset.h>
#include <cstdint>

using namespace bcsv;

// ============================================================================
// Setup helpers
// ============================================================================

namespace {

// Fill a dynamic bitset with a pseudo-random pattern
Bitset<> makePattern(size_t bits) {
    Bitset<> bs(bits);
    for (size_t i = 0; i < bits; ++i) {
        if ((i * 7 + 3) % 5 < 2) bs.set(i);
    }
    return bs;
}

} // namespace

// ============================================================================
// equalRange benchmarks
// ============================================================================

static void BM_Bitset_EqualRange_Aligned(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    Bitset<> a(bits);
    Bitset<> b(bits);
    // Identical patterns → always equal (worst case for equality check: must scan all)
    for (size_t i = 0; i < bits; i += 3) { a.set(i); b.set(i); }

    for (auto _ : state) {
        bool eq = a.equalRange(b, 0, bits);
        benchmark::DoNotOptimize(eq);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_EqualRange_Aligned)
    ->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

static void BM_Bitset_EqualRange_Misaligned(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    const size_t offset = 3;
    Bitset<> a(bits + offset);
    Bitset<> b(bits);
    for (size_t i = 0; i < bits; i += 3) {
        a.set(offset + i);
        b.set(i);
    }

    for (auto _ : state) {
        bool eq = a.equalRange(b, offset, bits);
        benchmark::DoNotOptimize(eq);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_EqualRange_Misaligned)
    ->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

// Baseline: per-bit comparison loop
static void BM_Bitset_EqualRange_BitLoop_Baseline(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    Bitset<> a(bits);
    Bitset<> b(bits);
    for (size_t i = 0; i < bits; i += 3) { a.set(i); b.set(i); }

    for (auto _ : state) {
        bool eq = true;
        for (size_t i = 0; i < bits; ++i) {
            if (a[i] != b[i]) { eq = false; break; }
        }
        benchmark::DoNotOptimize(eq);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_EqualRange_BitLoop_Baseline)
    ->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

// ============================================================================
// assignRange benchmarks
// ============================================================================

static void BM_Bitset_AssignRange_Aligned(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    Bitset<> dst(bits);
    Bitset<> src = makePattern(bits);

    for (auto _ : state) {
        dst.assignRange(src, 0, bits);
        benchmark::DoNotOptimize(dst);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_AssignRange_Aligned)
    ->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

static void BM_Bitset_AssignRange_Misaligned(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    const size_t offset = 3;
    Bitset<> dst(bits + offset);
    Bitset<> src = makePattern(bits);

    for (auto _ : state) {
        dst.assignRange(src, offset, bits);
        benchmark::DoNotOptimize(dst);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_AssignRange_Misaligned)
    ->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

// Baseline: per-bit assignment loop
static void BM_Bitset_AssignRange_BitLoop_Baseline(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    Bitset<> dst(bits);
    Bitset<> src = makePattern(bits);

    for (auto _ : state) {
        for (size_t i = 0; i < bits; ++i) {
            dst.set(i, src[i]);
        }
        benchmark::DoNotOptimize(dst);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_AssignRange_BitLoop_Baseline)
    ->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(512)->Arg(1024);

// ============================================================================
// Free function dual-offset benchmarks
// ============================================================================

static void BM_Bitset_FreeEqualRange_DualOffset(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    Bitset<> a(bits + 7);
    Bitset<> b(bits + 13);
    for (size_t i = 0; i < bits; i += 3) {
        a.set(7 + i);
        b.set(13 + i);
    }

    for (auto _ : state) {
        bool eq = bcsv::equalRange(a, 7, b, 13, bits);
        benchmark::DoNotOptimize(eq);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_FreeEqualRange_DualOffset)
    ->Arg(8)->Arg(64)->Arg(128)->Arg(256)->Arg(1024);

static void BM_Bitset_FreeAssignRange_DualOffset(benchmark::State& state) {
    const size_t bits = static_cast<size_t>(state.range(0));
    Bitset<> dst(bits + 7);
    Bitset<> src(bits + 13);
    for (size_t i = 0; i < bits; i += 3) src.set(13 + i);

    for (auto _ : state) {
        bcsv::assignRange(dst, 7, src, 13, bits);
        benchmark::DoNotOptimize(dst);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(bits));
}
BENCHMARK(BM_Bitset_FreeAssignRange_DualOffset)
    ->Arg(8)->Arg(64)->Arg(128)->Arg(256)->Arg(1024);

// ============================================================================
// ZoH-scenario benchmark: compare+assign of bool block (typical 130 bools)
// ============================================================================

static void BM_Bitset_ZoH_CompareAndAssign(benchmark::State& state) {
    const size_t boolCount = static_cast<size_t>(state.range(0));
    Bitset<> prev_bits(boolCount);
    Bitset<> row_bits = makePattern(boolCount);
    prev_bits.assignRange(row_bits, 0, boolCount);

    // Simulate: every other iteration has a change
    bool toggle = false;
    for (auto _ : state) {
        if (toggle) row_bits.flip(boolCount / 2);
        toggle = !toggle;

        bool changed = !prev_bits.equalRange(row_bits, 0, boolCount);
        if (changed) {
            prev_bits.assignRange(row_bits, 0, boolCount);
        }
        benchmark::DoNotOptimize(changed);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(boolCount));
}
BENCHMARK(BM_Bitset_ZoH_CompareAndAssign)
    ->Arg(1)->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(130)->Arg(256)->Arg(512);

// Baseline: ZoH per-bit loop (mirrors current codec pattern)
static void BM_Bitset_ZoH_BitLoop_Baseline(benchmark::State& state) {
    const size_t boolCount = static_cast<size_t>(state.range(0));
    Bitset<> prev_bits(boolCount);
    Bitset<> row_bits = makePattern(boolCount);
    for (size_t i = 0; i < boolCount; ++i) prev_bits.set(i, row_bits[i]);

    bool toggle = false;
    for (auto _ : state) {
        if (toggle) row_bits.flip(boolCount / 2);
        toggle = !toggle;

        bool changed = false;
        for (size_t i = 0; i < boolCount; ++i) {
            if (prev_bits[i] != row_bits[i]) { changed = true; break; }
        }
        if (changed) {
            for (size_t i = 0; i < boolCount; ++i) {
                prev_bits.set(i, row_bits[i]);
            }
        }
        benchmark::DoNotOptimize(changed);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(boolCount));
}
BENCHMARK(BM_Bitset_ZoH_BitLoop_Baseline)
    ->Arg(1)->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(130)->Arg(256)->Arg(512);

BENCHMARK_MAIN();
