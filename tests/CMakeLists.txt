# Google Test Framework
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, using FetchContent to download...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# Tests CMakeLists.txt

option(BCSV_ENABLE_STRESS_TESTS "Enable time-consuming stress tests and benchmarks" OFF)

set(TEST_SOURCES
    bcsv_comprehensive_test.cpp
    vectorized_access_test.cpp
    row_parameterized_test.cpp
    vle_template_test.cpp
    lz4_stream_test.cpp
    file_footer_test.cpp
    error_handling_test.cpp
    layout_sync_test.cpp
    bitset_test.cpp
    visit_test.cpp
    row_codec_flat001_test.cpp
    row_codec_zoh001_test.cpp
    row_codec_delta002_test.cpp
    codec_dispatch_test.cpp
    layout_guard_test.cpp
    review_fixes_test.cpp
    writer_reader_test.cpp
    file_codec_test.cpp
    csv_reader_writer_test.cpp
    bench_dataset_profiles_test.cpp
    file_header_byte_buffer_test.cpp
)

if(BCSV_ENABLE_STRESS_TESTS)
    list(APPEND TEST_SOURCES lz4_stress_test.cpp)
endif()

add_executable(bcsv_gtest ${TEST_SOURCES})

# Link the bcsv library and Google Test
target_link_libraries(bcsv_gtest PRIVATE 
    bcsv 
    gtest 
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(bcsv_gtest DISCOVERY_TIMEOUT 60)

# Set output directory
set_target_properties(bcsv_gtest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Enable C++20 standard
target_compile_features(bcsv_gtest PRIVATE cxx_std_20)

# bench_dataset_profiles_test.cpp includes benchmark/src/bench_datasets.hpp
target_include_directories(bcsv_gtest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../benchmark/src
)