# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

# ============================================================================
# Legacy Standalone Benchmarks
# ============================================================================
# These two executables are kept as standalone programs in the benchmark/
# folder for quick manual inspection.  They are NOT part of the Python
# orchestration layer (build_and_run.py / report.py).
#
# They are superseded by the modular benchmark suite (bench_macro_datasets,
# bench_micro_types) but retained here for one release cycle behind the
# BCSV_ENABLE_LEGACY_BENCHMARKS option (default ON).

option(BCSV_ENABLE_LEGACY_BENCHMARKS
    "Build deprecated benchmark_performance and benchmark_large executables"
    ON)

if(BCSV_ENABLE_LEGACY_BENCHMARKS)
    add_executable(benchmark_performance benchmark_performance.cpp)
    target_link_libraries(benchmark_performance PRIVATE bcsv)
    target_compile_features(benchmark_performance PRIVATE cxx_std_20)
    set_target_properties(benchmark_performance PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_executable(benchmark_large benchmark_large.cpp)
    target_link_libraries(benchmark_large PRIVATE bcsv)
    target_compile_features(benchmark_large PRIVATE cxx_std_20)
    set_target_properties(benchmark_large PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    message(STATUS "Legacy benchmarks enabled: benchmark_performance, benchmark_large")
endif()
