# Google Test Framework
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "Google Test not found, using FetchContent to download...")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
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
add_executable(bcsv_gtest 
    bcsv_comprehensive_test.cpp
    vectorized_access_test.cpp
    vle_template_test.cpp
    lz4_stream_test.cpp
    file_footer_test.cpp
)

# Link the bcsv library and Google Test
target_link_libraries(bcsv_gtest PRIVATE 
    bcsv 
    gtest 
    gtest_main
)

# Set output directory
set_target_properties(bcsv_gtest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Enable C++20 standard
target_compile_features(bcsv_gtest PRIVATE cxx_std_20)