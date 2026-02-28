# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

# ============================================================================
# Modular Benchmark Suite
# ============================================================================
# C++ sources live in benchmark/src/ and are orchestrated by the Python
# harness in benchmark/run.py.
#
# Targets:
#   bench_macro_datasets   – full write/read/validate dataset cycles
#   bench_micro_types      – per-type latency via Google Benchmark
#   bench_generate_csv     – CSV generator for CLI-tool round-trip tests
#   bench_external_csv     – (optional) external CSV library comparison

option(BCSV_BUILD_BENCHMARKS "Build the modular benchmark suite" ON)

if(BCSV_BUILD_BENCHMARKS)
    # --- Macro benchmark: full write/read/validate cycles ---
    add_executable(bench_macro_datasets src/bench_macro_datasets.cpp)
    target_link_libraries(bench_macro_datasets PRIVATE bcsv)
    target_compile_features(bench_macro_datasets PRIVATE cxx_std_20)
    set_target_properties(bench_macro_datasets PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # --- CSV generator for CLI-tool round-trip benchmarks ---
    add_executable(bench_generate_csv src/bench_generate_csv.cpp)
    target_link_libraries(bench_generate_csv PRIVATE bcsv)
    target_compile_features(bench_generate_csv PRIVATE cxx_std_20)
    set_target_properties(bench_generate_csv PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # --- Micro benchmarks: per-type latency via Google Benchmark ---
    option(BCSV_BUILD_MICRO_BENCHMARKS "Build micro-benchmarks (requires Google Benchmark)" ON)

    if(BCSV_BUILD_MICRO_BENCHMARKS)
        find_package(benchmark QUIET)
        if(NOT benchmark_FOUND)
            message(STATUS "Google Benchmark not found, using FetchContent to download...")
            include(FetchContent)
            FetchContent_Declare(
                googlebenchmark
                GIT_REPOSITORY https://github.com/google/benchmark.git
                GIT_TAG v1.9.1
                GIT_SHALLOW TRUE
            )
            set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
            set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
            # Save/restore CMAKE_MODULE_PATH — Google Benchmark ships a
            # GetGitVersion.cmake that collides with the project's one.
            set(_saved_module_path "${CMAKE_MODULE_PATH}")
            set(CMAKE_MODULE_PATH "")
            FetchContent_MakeAvailable(googlebenchmark)
            set(CMAKE_MODULE_PATH "${_saved_module_path}")
        endif()

        add_executable(bench_micro_types src/bench_micro_types.cpp)
        target_link_libraries(bench_micro_types PRIVATE bcsv benchmark::benchmark)
        target_compile_features(bench_micro_types PRIVATE cxx_std_20)
        set_target_properties(bench_micro_types PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )

        add_executable(bench_micro_bitset src/bench_micro_bitset.cpp)
        target_link_libraries(bench_micro_bitset PRIVATE bcsv benchmark::benchmark)
        target_compile_features(bench_micro_bitset PRIVATE cxx_std_20)
        set_target_properties(bench_micro_bitset PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
    endif()

    # --- Codec comparison benchmark ---
    add_executable(bench_codec_compare src/bench_codec_compare.cpp)
    target_link_libraries(bench_codec_compare PRIVATE bcsv)
    target_compile_features(bench_codec_compare PRIVATE cxx_std_20)
    set_target_properties(bench_codec_compare PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

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

        add_executable(bench_external_csv src/bench_external_csv.cpp)
        target_link_libraries(bench_external_csv PRIVATE bcsv csv)
        target_compile_features(bench_external_csv PRIVATE cxx_std_20)
        set_target_properties(bench_external_csv PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
    endif()
endif()
