# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# CLI Tools CMakeLists.txt
# ========================
# BCSV command-line tools for file conversion, inspection, and processing.
#
# Converters:
#   csv2bcsv      - Convert CSV files to BCSV format
#   bcsv2csv      - Convert BCSV files to CSV format
#
# Inspection:
#   bcsvHead      - Display first N rows in CSV format
#   bcsvTail      - Display last N rows in CSV format
#   bcsvHeader    - Display file structure / schema
#
# Processing:
#   bcsvSampler   - Filter & project BCSV files using expressions
#   bcsvGenerator - Generate synthetic BCSV test datasets

# ── Vendored third-party: CLI11 (header-only argument parser, tools-only) ──
# BSD-3-Clause. Linked only into CLI tool executables — never into libbcsv,
# the C API, the Python/C# bindings, or embedded targets. Lives under include/
# for a consistent vendored-dependency layout (like lz4/xxHash) but is excluded
# from the installed headers (it is only needed to build the CLI tools).
add_library(cli11 INTERFACE)
target_include_directories(cli11 SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/include/CLI11-2.6.2)

# CSV to BCSV converter
add_executable(csv2bcsv csv2bcsv.cpp)
target_link_libraries(csv2bcsv PRIVATE bcsv cli11)

# BCSV to CSV converter
add_executable(bcsv2csv bcsv2csv.cpp)
target_link_libraries(bcsv2csv PRIVATE bcsv cli11)

# Display first N rows
add_executable(bcsvHead bcsvHead.cpp)
target_link_libraries(bcsvHead PRIVATE bcsv cli11)

# Display last N rows
add_executable(bcsvTail bcsvTail.cpp)
target_link_libraries(bcsvTail PRIVATE bcsv cli11)

# Display file structure / schema
add_executable(bcsvHeader bcsvHeader.cpp)
target_link_libraries(bcsvHeader PRIVATE bcsv cli11)

# Streaming filter & project
add_executable(bcsvSampler bcsvSampler.cpp)
target_link_libraries(bcsvSampler PRIVATE bcsv cli11)

# Synthetic dataset generator
add_executable(bcsvGenerator bcsvGenerator.cpp)
target_link_libraries(bcsvGenerator PRIVATE bcsv cli11)
target_include_directories(bcsvGenerator PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)

# Validation tool
add_executable(bcsvValidate bcsvValidate.cpp)
target_link_libraries(bcsvValidate PRIVATE bcsv cli11)
target_include_directories(bcsvValidate PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)

# Repair tool
add_executable(bcsvRepair bcsvRepair.cpp)
target_link_libraries(bcsvRepair PRIVATE bcsv cli11)

# Comparison tool
add_executable(bcsvCompare bcsvCompare.cpp)
target_link_libraries(bcsvCompare PRIVATE bcsv cli11)
target_include_directories(bcsvCompare PRIVATE ${CMAKE_SOURCE_DIR}/src/shared)

# Type-narrowing scanner and converter
add_executable(bcsvCast bcsvCast.cpp)
target_link_libraries(bcsvCast PRIVATE bcsv cli11)

# Set output directory for all tools
set_target_properties(csv2bcsv bcsv2csv bcsvHead bcsvTail bcsvHeader bcsvSampler bcsvGenerator bcsvValidate bcsvRepair bcsvCompare bcsvCast PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
