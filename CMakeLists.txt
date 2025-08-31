cmake_minimum_required(VERSION 3.20)
project(bcsv VERSION 1.0.0 LANGUAGES CXX C)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent for external dependencies
include(FetchContent)

# Find Boost CRC library
find_package(Boost QUIET)
if(NOT Boost_FOUND)
    message(STATUS "Boost not found, using FetchContent to download...")
    FetchContent_Declare(
        boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG boost-1.82.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(boost)
endif()

# Integrate LZ4 compression library
find_package(lz4 QUIET)
if(NOT lz4_FOUND)
    message(STATUS "LZ4 not found, using FetchContent to download...")
    FetchContent_Declare(
        lz4
        GIT_REPOSITORY https://github.com/lz4/lz4.git
        GIT_TAG v1.9.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(lz4)

    # Create a simple interface library for LZ4
    add_library(lz4_interface INTERFACE)
    target_include_directories(lz4_interface INTERFACE ${lz4_SOURCE_DIR}/lib)

    # Manually compile LZ4 sources
    file(GLOB LZ4_SOURCES ${lz4_SOURCE_DIR}/lib/*.c)
    add_library(lz4_static STATIC ${LZ4_SOURCES})
    target_include_directories(lz4_static PUBLIC 
        $<BUILD_INTERFACE:${lz4_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:include/lz4>
    )
endif()

# Create interface library for modular header-only bcsv
add_library(bcsv INTERFACE)

# Set include directories
target_include_directories(bcsv INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Add LZ4 include directories
if(TARGET lz4_static)
    target_include_directories(bcsv INTERFACE
        $<BUILD_INTERFACE:${lz4_SOURCE_DIR}/lib>
    )
endif()

# Link Boost and LZ4 libraries
if(Boost_FOUND)
    target_link_libraries(bcsv INTERFACE Boost::boost)
else()
    target_link_libraries(bcsv INTERFACE boost)
endif()

# Link LZ4 library (privately to avoid export issues)
target_link_libraries(bcsv INTERFACE lz4_static)

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
install(DIRECTORY ${lz4_SOURCE_DIR}/lib/ DESTINATION include/lz4 FILES_MATCHING PATTERN "*.h")
install(TARGETS bcsv EXPORT bcsvConfig)
install(TARGETS lz4_static EXPORT bcsvConfig)
install(EXPORT bcsvConfig DESTINATION lib/cmake/bcsv)
