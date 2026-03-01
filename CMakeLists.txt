# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

cmake_minimum_required(VERSION 3.28)

# Extract version from Git tags before project() call
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GetGitVersion)

project(bcsv VERSION ${VERSION_STRING} LANGUAGES CXX C)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

include(FetchContent)

# ============================================================================
# xxHash Library
# ============================================================================
add_library(xxhash STATIC 
    include/xxHash-0.8.3/xxhash.c
)

target_include_directories(xxhash PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/xxHash-0.8.3>
    $<INSTALL_INTERFACE:include/xxHash-0.8.3>
)

set_target_properties(xxhash PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
)

# Optimize xxHash performance
target_compile_definitions(xxhash PUBLIC
    XXH_INLINE_ALL           # Inline functions for better performance
)

# ============================================================================
# LZ4 Library
# ============================================================================
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LZ4 QUIET liblz4>=1.9)
endif()

if(LZ4_FOUND)
    message(STATUS "Found system LZ4 ${LZ4_VERSION} - using system version")
    add_library(lz4_system INTERFACE IMPORTED)
    target_include_directories(lz4_system INTERFACE ${LZ4_INCLUDE_DIRS})
    target_link_libraries(lz4_system INTERFACE ${LZ4_LIBRARIES})
    target_compile_options(lz4_system INTERFACE ${LZ4_CFLAGS_OTHER})
    set(LZ4_TARGET lz4_system)
    set(USING_SYSTEM_LZ4 TRUE)
else()
    message(STATUS "System LZ4 not found or too old (need >= 1.9) - using local copy")
    set(LZ4_LOCAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/lz4-1.10.0)
    
    add_library(lz4_local STATIC 
        ${LZ4_LOCAL_DIR}/lz4.c
        ${LZ4_LOCAL_DIR}/lz4hc.c
        ${LZ4_LOCAL_DIR}/lz4frame.c
        ${LZ4_LOCAL_DIR}/xxhash.c  # LZ4's internal xxHash (separate from ours)
    )
    
    target_include_directories(lz4_local PUBLIC 
        $<BUILD_INTERFACE:${LZ4_LOCAL_DIR}>
        $<INSTALL_INTERFACE:include/lz4-1.10.0>
    )
    
    set_target_properties(lz4_local PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD 99
    )
    
    set(LZ4_TARGET lz4_local)
    set(USING_SYSTEM_LZ4 FALSE)
endif()

# ============================================================================
# BCSV Library (Header-only interface)
# ============================================================================
add_library(bcsv INTERFACE)

target_include_directories(bcsv INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(bcsv INTERFACE 
    xxhash        # xxHash64 for checksums
)

# Link LZ4 (conditionally - system LZ4 shouldn't be exported)
if(USING_SYSTEM_LZ4)
    # System LZ4 - add as private dependency (not exported)
    target_link_libraries(bcsv INTERFACE ${LZ4_TARGET})
else()
    # Local LZ4 - add as public dependency (will be exported)
    target_link_libraries(bcsv INTERFACE lz4_local)
endif()

# Threading (required by FileCodecPacketLZ4Batch001)
option(BCSV_ENABLE_BATCH_CODEC "Enable the batch-LZ4 file codec (requires threading)" ON)
if(BCSV_ENABLE_BATCH_CODEC)
    find_package(Threads REQUIRED)
    target_link_libraries(bcsv INTERFACE Threads::Threads)
    target_compile_definitions(bcsv INTERFACE BCSV_HAS_BATCH_CODEC=1)
endif()

# Compiler requirements
target_compile_features(bcsv INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(bcsv INTERFACE /W4)
else()
    target_compile_options(bcsv INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Enable generation of compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build Options
# ============================================================================
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(BUILD_TOOLS "Build CLI tools" ON)
if(BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()

option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(benchmark)

# ============================================================================
# Installation
# ============================================================================
install(DIRECTORY include/ DESTINATION include)

set(INSTALL_TARGETS bcsv xxhash)

if(NOT USING_SYSTEM_LZ4)
    list(APPEND INSTALL_TARGETS lz4_local)
endif()

install(TARGETS ${INSTALL_TARGETS} EXPORT bcsvConfig)
install(EXPORT bcsvConfig DESTINATION lib/cmake/bcsv)

# ============================================================================
# C API Shared Library
# ============================================================================
add_library(bcsv_c_api SHARED
    src/bcsv_c_api.cpp
)

target_include_directories(bcsv_c_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bcsv_c_api
    PUBLIC bcsv
    PRIVATE xxhash ${LZ4_TARGET}
)

# Platform-specific properties
if(WIN32)
    set_target_properties(bcsv_c_api PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        OUTPUT_NAME "bcsv_c_api"
    )
else()
    set_target_properties(bcsv_c_api PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "bcsv_c_api"
    )
endif()

install(TARGETS bcsv_c_api
    EXPORT bcsvConfig
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# ============================================================================
# Test Programs
# ============================================================================
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTS)
    add_executable(test_row_api tests/bcsv_c_api_row_test.c)
    target_link_libraries(test_row_api bcsv_c_api)
    target_include_directories(test_row_api PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME test_row_api COMMAND $<TARGET_FILE:test_row_api>)
    
    add_executable(test_c_api tests/bcsv_c_api_test.c)
    target_link_libraries(test_c_api bcsv_c_api)
    target_include_directories(test_c_api PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME test_c_api COMMAND $<TARGET_FILE:test_c_api>)

    add_executable(test_c_api_full tests/bcsv_c_api_full_test.c)
    target_link_libraries(test_c_api_full bcsv_c_api m)
    target_include_directories(test_c_api_full PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME test_c_api_full COMMAND $<TARGET_FILE:test_c_api_full>)
endif()