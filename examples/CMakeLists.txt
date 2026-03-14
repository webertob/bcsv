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
# 1. quickstart.cpp           - Minimal write/read (Quick Start)
# 2. example.cpp              - Flexible interface usage (runtime-defined schemas)
# 3. example_static.cpp       - Static interface usage (compile-time schemas)
# 4. example_zoh.cpp          - Flexible + Zero-Order Hold compression
# 5. example_zoh_static.cpp   - Static + Zero-Order Hold compression
# 6. example_delta.cpp        - Delta encoding for time-series data
# 7. example_direct_access.cpp - Random-access reader (ReaderDirectAccess)
# 8. example_error_handling.cpp - Error handling patterns
# 9. example_sampler.cpp      - Sampler API demonstration
#
# CLI tools have been moved to src/tools/

# Quick start example
add_executable(quickstart quickstart.cpp)
target_link_libraries(quickstart PRIVATE bcsv)

# Basic flexible interface example
add_executable(example example.cpp)
target_link_libraries(example PRIVATE bcsv)

# Static interface example
add_executable(example_static example_static.cpp)
target_link_libraries(example_static PRIVATE bcsv)

# Zero-Order Hold flexible interface example
add_executable(example_zoh example_zoh.cpp)
target_link_libraries(example_zoh PRIVATE bcsv)

# Zero-Order Hold static interface example
add_executable(example_zoh_static example_zoh_static.cpp)
target_link_libraries(example_zoh_static PRIVATE bcsv)

# Delta encoding example
add_executable(example_delta example_delta.cpp)
target_link_libraries(example_delta PRIVATE bcsv)

# Direct access (random-access reader) example
add_executable(example_direct_access example_direct_access.cpp)
target_link_libraries(example_direct_access PRIVATE bcsv)

# Error handling example
add_executable(example_error_handling example_error_handling.cpp)
target_link_libraries(example_error_handling PRIVATE bcsv)

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
set(ALL_EXAMPLES
    quickstart example example_static example_zoh example_zoh_static
    example_delta example_direct_access example_error_handling
    c_api_vectorized_example visitor_examples example_sampler
)
set_target_properties(${ALL_EXAMPLES} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
