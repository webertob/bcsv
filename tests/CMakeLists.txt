# Google Test Framework
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, using FetchContent to download...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# Tests CMakeLists.txt

option(BCSV_ENABLE_STRESS_TESTS "Enable time-consuming stress tests and benchmarks" OFF)

set(TEST_SOURCES
    bcsv_comprehensive_test.cpp
    vectorized_access_test.cpp
    row_parameterized_test.cpp
    vle_template_test.cpp
    lz4_stream_test.cpp
    file_footer_test.cpp
    error_handling_test.cpp
    layout_sync_test.cpp
    bitset_test.cpp
    visit_test.cpp
    row_codec_flat001_test.cpp
    row_codec_zoh001_test.cpp
    codec_dispatch_test.cpp
)

if(BCSV_ENABLE_STRESS_TESTS)
    list(APPEND TEST_SOURCES lz4_stress_test.cpp)
endif()

add_executable(bcsv_gtest ${TEST_SOURCES})

# Link the bcsv library and Google Test
target_link_libraries(bcsv_gtest PRIVATE 
    bcsv 
    gtest 
    gtest_main
)

# Set output directory
set_target_properties(bcsv_gtest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Enable C++20 standard
target_compile_features(bcsv_gtest PRIVATE cxx_std_20)

# Legacy benchmark executables — superseded by the modular benchmark suite.
# Kept behind a flag for one release cycle; will be removed in a future version.
option(BCSV_ENABLE_LEGACY_BENCHMARKS "Build deprecated benchmark_performance and benchmark_large" OFF)

if(BCSV_ENABLE_LEGACY_BENCHMARKS)
    add_executable(benchmark_performance benchmark_performance.cpp)
    target_link_libraries(benchmark_performance PRIVATE bcsv)

    add_executable(benchmark_large benchmark_large.cpp)
    target_link_libraries(benchmark_large PRIVATE bcsv)

    set_target_properties(benchmark_performance benchmark_large PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ============================================================================
# New Modular Benchmark Suite (Phase 1)
# ============================================================================
option(BCSV_ENABLE_BENCHMARKS "Build the modular benchmark suite" ON)

if(BCSV_ENABLE_BENCHMARKS)
    # --- Macro benchmark: full write/read/validate cycles ---
    add_executable(bench_macro_datasets bench_macro_datasets.cpp)
    target_link_libraries(bench_macro_datasets PRIVATE bcsv)
    target_compile_features(bench_macro_datasets PRIVATE cxx_std_20)
    set_target_properties(bench_macro_datasets PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # --- CSV generator for CLI-tool round-trip benchmarks ---
    add_executable(bench_generate_csv bench_generate_csv.cpp)
    target_link_libraries(bench_generate_csv PRIVATE bcsv)
    target_compile_features(bench_generate_csv PRIVATE cxx_std_20)
    set_target_properties(bench_generate_csv PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # --- Micro benchmarks: per-type latency via Google Benchmark ---
    option(BCSV_ENABLE_MICRO_BENCHMARKS "Build micro-benchmarks (requires Google Benchmark)" ON)

    if(BCSV_ENABLE_MICRO_BENCHMARKS)
        find_package(benchmark QUIET)
        if(NOT benchmark_FOUND)
            message(STATUS "Google Benchmark not found, using FetchContent to download...")
            include(FetchContent)
            FetchContent_Declare(
                googlebenchmark
                GIT_REPOSITORY https://github.com/google/benchmark.git
                GIT_TAG v1.8.3
                GIT_SHALLOW TRUE
            )
            # Prevent benchmark from building its own tests
            set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
            set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
            # Save and restore CMAKE_MODULE_PATH — Google Benchmark ships its
            # own GetGitVersion.cmake which collides with the project's one.
            set(_saved_module_path "${CMAKE_MODULE_PATH}")
            set(CMAKE_MODULE_PATH "")
            FetchContent_MakeAvailable(googlebenchmark)
            set(CMAKE_MODULE_PATH "${_saved_module_path}")
        endif()

        add_executable(bench_micro_types bench_micro_types.cpp)
        target_link_libraries(bench_micro_types PRIVATE bcsv benchmark::benchmark)
        target_compile_features(bench_micro_types PRIVATE cxx_std_20)
        set_target_properties(bench_micro_types PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
    endif()

    # --- External CSV library comparison (optional) ---
    option(BCSV_ENABLE_EXTERNAL_CSV_BENCH "Build external CSV library comparison benchmark" OFF)

    if(BCSV_ENABLE_EXTERNAL_CSV_BENCH)
        include(FetchContent)
        FetchContent_Declare(
            csv_parser
            GIT_REPOSITORY https://github.com/vincentlaucsb/csv-parser.git
            GIT_TAG 2.3.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(csv_parser)

        add_executable(bench_external_csv bench_external_csv.cpp)
        target_link_libraries(bench_external_csv PRIVATE bcsv csv)
        target_compile_features(bench_external_csv PRIVATE cxx_std_20)
        set_target_properties(bench_external_csv PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
    endif()
endif()