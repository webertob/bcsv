# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# Examples CMakeLists.txt
# =============
# This directory contains examples demonstrating different aspects of BCSV:
#
# 1. example.cpp - Flexible interface usage (runtime-defined schemas)
# 2. example_static.cpp - Static interface usage (compile-time schemas)  
# 3. example_zoh.cpp - Flexible interface usage (runtime-defined schemas with Zero-Order Hold compression)  
# 4. example_zoh_static.cpp - Static interface usage (compile-time schemas with Zero-Order Hold compression)  
# 5. performance_benchmark.cpp - Performance comparison between interfaces
# 6. large_scale_benchmark.cpp - Large-scale performance test (500K rows, 72 columns)
# 7. csv2bcsv.cpp - CLI tool to convert CSV files to BCSV format
# 8. bcsv2csv.cpp - CLI tool to convert BCSV files to CSV format

# Basic flexible interface example
add_executable(example example.cpp)
target_link_libraries(example PRIVATE bcsv)

# Static interface example
add_executable(example_static example_static.cpp)
target_link_libraries(example_static PRIVATE bcsv)

# Zero-copy static interface example
add_executable(example_zoh example_zoh.cpp)
target_link_libraries(example_zoh PRIVATE bcsv)

# Zero-copy static interface example
add_executable(example_zoh_static example_zoh_static.cpp)
target_link_libraries(example_zoh_static PRIVATE bcsv)

# Performance benchmark
add_executable(performance_benchmark performance_benchmark.cpp)
target_link_libraries(performance_benchmark PRIVATE bcsv)

# Large scale performance benchmark
add_executable(large_scale_benchmark large_scale_benchmark.cpp)
target_link_libraries(large_scale_benchmark PRIVATE bcsv)

# CSV to BCSV converter CLI tool
add_executable(csv2bcsv csv2bcsv.cpp)
target_link_libraries(csv2bcsv PRIVATE bcsv)

# BCSV to CSV converter CLI tool
add_executable(bcsv2csv bcsv2csv.cpp)
target_link_libraries(bcsv2csv PRIVATE bcsv)


# Set output directory
set_target_properties(example example_static example_zoh example_zoh_static performance_benchmark large_scale_benchmark csv2bcsv bcsv2csv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
