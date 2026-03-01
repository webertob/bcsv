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

# CSV to BCSV converter
add_executable(csv2bcsv csv2bcsv.cpp)
target_link_libraries(csv2bcsv PRIVATE bcsv)

# BCSV to CSV converter
add_executable(bcsv2csv bcsv2csv.cpp)
target_link_libraries(bcsv2csv PRIVATE bcsv)

# Display first N rows
add_executable(bcsvHead bcsvHead.cpp)
target_link_libraries(bcsvHead PRIVATE bcsv)

# Display last N rows
add_executable(bcsvTail bcsvTail.cpp)
target_link_libraries(bcsvTail PRIVATE bcsv)

# Display file structure / schema
add_executable(bcsvHeader bcsvHeader.cpp)
target_link_libraries(bcsvHeader PRIVATE bcsv)

# Streaming filter & project
add_executable(bcsvSampler bcsvSampler.cpp)
target_link_libraries(bcsvSampler PRIVATE bcsv)

# Synthetic dataset generator
add_executable(bcsvGenerator bcsvGenerator.cpp)
target_link_libraries(bcsvGenerator PRIVATE bcsv)
target_include_directories(bcsvGenerator PRIVATE ${CMAKE_SOURCE_DIR}/benchmark/src)

# Set output directory for all tools
set_target_properties(csv2bcsv bcsv2csv bcsvHead bcsvTail bcsvHeader bcsvSampler bcsvGenerator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
