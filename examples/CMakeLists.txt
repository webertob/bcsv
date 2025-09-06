# BCSV Examples
# =============
# This directory contains examples demonstrating different aspects of BCSV:
#
# 1. example.cpp - Basic flexible interface usage (runtime-defined schemas)
# 2. example_static.cpp - Static interface usage (compile-time schemas)  
# 3. performance_benchmark.cpp - Performance comparison between interfaces

# Basic flexible interface example
add_executable(example example.cpp)
target_link_libraries(example PRIVATE bcsv)

# Static interface example
add_executable(example_static example_static.cpp)
target_link_libraries(example_static PRIVATE bcsv)

# Performance benchmark
add_executable(performance_benchmark performance_benchmark.cpp)
target_link_libraries(performance_benchmark PRIVATE bcsv)

# Set output directory
set_target_properties(example example_static performance_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
