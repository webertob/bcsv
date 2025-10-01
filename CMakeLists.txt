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

# Generate version header from template
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/bcsv/version_generated.h"
    @ONLY
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMake handles parallel compilation automatically with cmake --build . -j
# No additional configuration needed

# Set optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Include FetchContent for external dependencies
include(FetchContent)

# Find Boost CRC library (version >= 1.70 required)
# Only set CMP0167 if the current CMake knows about that policy (older CMake will ignore)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost 1.70 QUIET)
if(Boost_FOUND)
    message(STATUS "Found system Boost ${Boost_VERSION} - using system version")
    set(BOOST_TARGET Boost::boost)
else()
    message(STATUS "System Boost not found or too old (need >= 1.70) - using local copy")
    # Use local Boost copy in include/boost-1.89.0/
    add_library(boost_local INTERFACE)
    target_include_directories(boost_local INTERFACE 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/boost-1.89.0>
        $<INSTALL_INTERFACE:include/boost-1.89.0>
    )
    set(BOOST_TARGET boost_local)
endif()

# Find LZ4 library (version >= 1.9 required)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LZ4 QUIET liblz4>=1.9)
endif()

if(LZ4_FOUND)
    message(STATUS "Found system LZ4 ${LZ4_VERSION} - using system version")
    add_library(lz4_system INTERFACE)
    target_include_directories(lz4_system INTERFACE ${LZ4_INCLUDE_DIRS})
    target_link_libraries(lz4_system INTERFACE ${LZ4_LIBRARIES})
    target_compile_options(lz4_system INTERFACE ${LZ4_CFLAGS_OTHER})
    set(LZ4_TARGET lz4_system)
    set(USING_SYSTEM_LZ4 TRUE)
else()
    message(STATUS "System LZ4 not found or too old (need >= 1.9) - using local copy")
    # Use local LZ4 copy in include/lz4-1.10.0/
    set(LZ4_LOCAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/lz4-1.10.0)
    
    # Create LZ4 static library from local sources
    add_library(lz4_local STATIC 
        ${LZ4_LOCAL_DIR}/lz4.c
        ${LZ4_LOCAL_DIR}/lz4hc.c
        ${LZ4_LOCAL_DIR}/lz4frame.c
        ${LZ4_LOCAL_DIR}/xxhash.c
    )
    
    target_include_directories(lz4_local PUBLIC 
        $<BUILD_INTERFACE:${LZ4_LOCAL_DIR}>
        $<INSTALL_INTERFACE:include/lz4-1.10.0>
    )
    
    # Set properties for the library
    set_target_properties(lz4_local PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD 99
    )
    
    set(LZ4_TARGET lz4_local)
    set(USING_SYSTEM_LZ4 FALSE)
endif()

# Create interface library for modular header-only bcsv
add_library(bcsv INTERFACE)

# Set include directories
target_include_directories(bcsv INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies differently for build vs install
if(Boost_FOUND)
    # For system boost, add the include paths directly to bcsv
    target_include_directories(bcsv INTERFACE ${Boost_INCLUDE_DIRS})
    if(TARGET Boost::boost)
        target_link_libraries(bcsv INTERFACE Boost::boost)
    endif()
else()
    # For local boost, link the target
    target_link_libraries(bcsv INTERFACE ${BOOST_TARGET})
endif()

if(USING_SYSTEM_LZ4)
    # For system LZ4, add paths directly to bcsv
    target_include_directories(bcsv INTERFACE ${LZ4_INCLUDE_DIRS})
    target_link_libraries(bcsv INTERFACE ${LZ4_LIBRARIES})
    target_compile_options(bcsv INTERFACE ${LZ4_CFLAGS_OTHER})
else()
    # For local LZ4, link the target
    target_link_libraries(bcsv INTERFACE ${LZ4_TARGET})
endif()

# Compiler-specific options
target_compile_features(bcsv INTERFACE cxx_std_20)

# Add compile options for different compilers
if(MSVC)
    target_compile_options(bcsv INTERFACE /W4)
else()
    target_compile_options(bcsv INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Build examples
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Build tests
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Installation rules
install(DIRECTORY include/ DESTINATION include)

# Only install our own targets, not system libraries
set(INSTALL_TARGETS bcsv)

# Install local Boost target if using local copy
if(NOT Boost_FOUND)
    list(APPEND INSTALL_TARGETS boost_local)
endif()

# Install LZ4 target if using local copy
if(NOT USING_SYSTEM_LZ4)
    list(APPEND INSTALL_TARGETS lz4_local)
endif()

install(TARGETS ${INSTALL_TARGETS} EXPORT bcsvConfig)
install(EXPORT bcsvConfig DESTINATION lib/cmake/bcsv)

# Build the bcsv C API as a shared library (.dll/.so/.dylib)
add_library(bcsv_c_api SHARED
    include/bcsv/c_api.cpp
    # Add any additional C API source files here
)

target_include_directories(bcsv_c_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(bcsv_c_api
    PUBLIC
        bcsv
    PRIVATE
        ${BOOST_TARGET}
        ${LZ4_TARGET}
)

# Platform-specific properties
if(WIN32)
    set_target_properties(bcsv_c_api PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        OUTPUT_NAME "bcsv_c_api"
    )
elseif(APPLE)
    set_target_properties(bcsv_c_api PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "bcsv_c_api"
    )
elseif(UNIX)
    set_target_properties(bcsv_c_api PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "bcsv_c_api"
    )
endif()

# Install the shared library
install(TARGETS bcsv_c_api
    EXPORT bcsvConfig
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)