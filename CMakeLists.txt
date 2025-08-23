cmake_minimum_required(VERSION 3.20)
project(bcsv VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent for external dependencies
include(FetchContent)

# TODO: Integrate LZ4 properly - for now build without it
# FetchContent_Declare(
#     lz4
#     GIT_REPOSITORY https://github.com/lz4/lz4.git
#     GIT_TAG v1.9.4
# )
# FetchContent_MakeAvailable(lz4)

# Create interface library for modular header-only bcsv
add_library(bcsv INTERFACE)

# Set include directories
target_include_directories(bcsv INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Compiler-specific options
target_compile_features(bcsv INTERFACE cxx_std_17)

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
install(TARGETS bcsv EXPORT bcsvConfig)
install(EXPORT bcsvConfig DESTINATION lib/cmake/bcsv)
