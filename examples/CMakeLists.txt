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
# 5. csv2bcsv.cpp - CLI tool to convert CSV files to BCSV format
# 6. bcsv2csv.cpp - CLI tool to convert BCSV files to CSV format
# 
# Note: Performance benchmarks have been moved to tests/

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

# CSV to BCSV converter CLI tool
add_executable(csv2bcsv csv2bcsv.cpp)
target_link_libraries(csv2bcsv PRIVATE bcsv)

# BCSV to CSV converter CLI tool
add_executable(bcsv2csv bcsv2csv.cpp)
target_link_libraries(bcsv2csv PRIVATE bcsv)

# BCSV head tool - display first few rows in CSV format
add_executable(bcsvHead bcsvHead.cpp)
target_link_libraries(bcsvHead PRIVATE bcsv)

# BCSV tail tool - display last few rows in CSV format
add_executable(bcsvTail bcsvTail.cpp)
target_link_libraries(bcsvTail PRIVATE bcsv)

# BCSV header tool - display file structure/schema
add_executable(bcsvHeader bcsvHeader.cpp)
target_link_libraries(bcsvHeader PRIVATE bcsv)

# C API vectorized example
add_executable(c_api_vectorized_example c_api_vectorized_example.c)
target_link_libraries(c_api_vectorized_example PRIVATE bcsv_c_api bcsv)

# Visitor pattern examples
add_executable(visitor_examples visitor_examples.cpp)
target_link_libraries(visitor_examples PRIVATE bcsv)

# Sampler example — streaming filter-and-project operator
add_executable(example_sampler example_sampler.cpp)
target_link_libraries(example_sampler PRIVATE bcsv)

# bcsvSampler CLI tool — filter & project BCSV files
add_executable(bcsvSampler bcsvSampler.cpp)
target_link_libraries(bcsvSampler PRIVATE bcsv)

# bcsvGenerator CLI tool — generate synthetic BCSV test datasets
add_executable(bcsvGenerator bcsvGenerator.cpp)
target_link_libraries(bcsvGenerator PRIVATE bcsv)
target_include_directories(bcsvGenerator PRIVATE ${CMAKE_SOURCE_DIR}/benchmark/src)

# Set output directory
set_target_properties(example example_static example_zoh example_zoh_static csv2bcsv bcsv2csv bcsvHead bcsvTail bcsvHeader c_api_vectorized_example visitor_examples example_sampler bcsvSampler bcsvGenerator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
