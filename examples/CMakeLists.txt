# BCSV Examples
# =============
# This directory contains examples demonstrating different aspects of BCSV:
#
# 1. example.cpp - Basic flexible interface usage (runtime-defined schemas)
# 2. example_static.cpp - Static interface usage (compile-time schemas)  
# 3. performance_benchmark.cpp - Performance comparison between interfaces
# 4. csv2bcsv.cpp - CLI tool to convert CSV files to BCSV format
# 5. bcsv2csv.cpp - CLI tool to convert BCSV files to CSV format

# Basic flexible interface example
add_executable(example example.cpp)
target_link_libraries(example PRIVATE bcsv)

# Static interface example
add_executable(example_static example_static.cpp)
target_link_libraries(example_static PRIVATE bcsv)

# Performance benchmark
add_executable(performance_benchmark performance_benchmark.cpp)
target_link_libraries(performance_benchmark PRIVATE bcsv)

# CSV to BCSV converter CLI tool
add_executable(csv2bcsv csv2bcsv.cpp)
target_link_libraries(csv2bcsv PRIVATE bcsv)

# BCSV to CSV converter CLI tool
add_executable(bcsv2csv bcsv2csv.cpp)
target_link_libraries(bcsv2csv PRIVATE bcsv)

# Set output directory
set_target_properties(example example_static performance_benchmark csv2bcsv bcsv2csv PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
