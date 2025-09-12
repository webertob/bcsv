cmake_minimum_required(VERSION 3.50)
project(bcsv VERSION 1.0.0 LANGUAGES CXX C)

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

# Include FetchContent for external dependencies
include(FetchContent)

# Find Boost CRC library (version >= 1.70 required)
find_package(Boost 1.70 QUIET COMPONENTS headers)
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
endif()

# Create interface library for modular header-only bcsv
add_library(bcsv INTERFACE)

# Set include directories
target_include_directories(bcsv INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link Boost and LZ4 libraries using our target variables
target_link_libraries(bcsv INTERFACE ${BOOST_TARGET} ${LZ4_TARGET})

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

# Install bcsv target
install(TARGETS bcsv EXPORT bcsvConfig)

# Install local Boost target if using local copy
if(NOT Boost_FOUND)
    install(TARGETS boost_local EXPORT bcsvConfig)
endif()

# Install LZ4 headers and target if using local copy
if(NOT LZ4_FOUND)
    # Install local LZ4 target (using the actual target name)
    install(TARGETS lz4_local EXPORT bcsvConfig)
endif()

install(EXPORT bcsvConfig DESTINATION lib/cmake/bcsv)
