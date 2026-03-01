# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# Examples CMakeLists.txt
# =============
# This directory contains examples demonstrating different aspects of BCSV:
#
# 1. example.cpp        - Flexible interface usage (runtime-defined schemas)
# 2. example_static.cpp - Static interface usage (compile-time schemas)
# 3. example_zoh.cpp    - Flexible + Zero-Order Hold compression
# 4. example_zoh_static.cpp - Static + Zero-Order Hold compression
# 5. example_sampler.cpp - Sampler API demonstration
#
# CLI tools have been moved to src/tools/

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

# C API vectorized example
add_executable(c_api_vectorized_example c_api_vectorized_example.c)
target_link_libraries(c_api_vectorized_example PRIVATE bcsv_c_api bcsv)

# Visitor pattern examples
add_executable(visitor_examples visitor_examples.cpp)
target_link_libraries(visitor_examples PRIVATE bcsv)

# Sampler example — streaming filter-and-project operator
add_executable(example_sampler example_sampler.cpp)
target_link_libraries(example_sampler PRIVATE bcsv)

# Set output directory
set_target_properties(example example_static example_zoh example_zoh_static c_api_vectorized_example visitor_examples example_sampler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
