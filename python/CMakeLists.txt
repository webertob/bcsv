cmake_minimum_required(VERSION 3.15...3.30)
project(pybcsv LANGUAGES C CXX)

# ── Version from Git ────────────────────────────────────────────────────
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --abbrev=0
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        OUTPUT_VARIABLE BCSV_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_RESULT
    )
    if(GIT_RESULT EQUAL 0)
        string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ ${BCSV_GIT_TAG})
        set(BCSV_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(BCSV_VERSION_MINOR ${CMAKE_MATCH_2})
        set(BCSV_VERSION_PATCH ${CMAKE_MATCH_3})
        set(BCSV_VERSION_STRING "${BCSV_VERSION_MAJOR}.${BCSV_VERSION_MINOR}.${BCSV_VERSION_PATCH}")
    endif()
endif()
if(NOT DEFINED BCSV_VERSION_STRING)
    set(BCSV_VERSION_MAJOR 0)
    set(BCSV_VERSION_MINOR 0)
    set(BCSV_VERSION_PATCH 0)
    set(BCSV_VERSION_STRING "0.0.0.dev0")
endif()
message(STATUS "[pybcsv] Version: ${BCSV_VERSION_STRING}")

# Set variables expected by cmake/version.h.in template
set(VERSION_MAJOR ${BCSV_VERSION_MAJOR})
set(VERSION_MINOR ${BCSV_VERSION_MINOR})
set(VERSION_PATCH ${BCSV_VERSION_PATCH})
set(VERSION_STRING ${BCSV_VERSION_STRING})

# Generate version_generated.h into build dir
set(VERSION_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/version.h.in")
if(EXISTS "${VERSION_TEMPLATE}")
    configure_file("${VERSION_TEMPLATE}"
        "${CMAKE_CURRENT_BINARY_DIR}/include/bcsv/version_generated.h" @ONLY)
else()
    # Fallback for standalone / sdist builds where ../cmake/ doesn't exist
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/include/bcsv/version_generated.h"
        "// Auto-generated version header\n"
        "#ifndef BCSV_VERSION_GENERATED_H\n"
        "#define BCSV_VERSION_GENERATED_H\n"
        "namespace bcsv { namespace version {\n"
        "constexpr int MAJOR = ${BCSV_VERSION_MAJOR};\n"
        "constexpr int MINOR = ${BCSV_VERSION_MINOR};\n"
        "constexpr int PATCH = ${BCSV_VERSION_PATCH};\n"
        "constexpr const char* STRING = \"${BCSV_VERSION_STRING}\";\n"
        "} }\n"
        "#endif\n"
    )
endif()

# ── Find nanobind ───────────────────────────────────────────────────────
find_package(Python 3.11 COMPONENTS Interpreter Development.Module REQUIRED)

# Detect nanobind CMake dir via Python
execute_process(
    COMMAND ${Python_EXECUTABLE} -m nanobind --cmake_dir
    OUTPUT_VARIABLE NB_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
list(APPEND CMAKE_PREFIX_PATH "${NB_DIR}")
find_package(nanobind CONFIG REQUIRED)

# ── Source files ────────────────────────────────────────────────────────
# Prefer parent project's include/ (always available in full checkout / CI).
# Fall back to local python/include/ (populated by sync_headers.py for standalone builds).
set(PARENT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")
set(LOCAL_INCLUDE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(EXISTS "${PARENT_INCLUDE_DIR}/bcsv/bcsv.h")
    set(BCSV_INCLUDE_DIR "${PARENT_INCLUDE_DIR}")
    message(STATUS "[pybcsv] Using parent include: ${BCSV_INCLUDE_DIR}")
elseif(EXISTS "${LOCAL_INCLUDE_DIR}/bcsv/bcsv.h")
    set(BCSV_INCLUDE_DIR "${LOCAL_INCLUDE_DIR}")
    message(STATUS "[pybcsv] Using local include: ${BCSV_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "[pybcsv] Cannot find bcsv headers. Run sync_headers.py or build from the project root.")
endif()

set(LZ4_DIR "${BCSV_INCLUDE_DIR}/lz4-1.10.0")

set(BCSV_SOURCES
    pybcsv/bindings.cpp
)

set(LZ4_SOURCES
    ${LZ4_DIR}/lz4.c
    ${LZ4_DIR}/lz4file.c
    ${LZ4_DIR}/lz4frame.c
    ${LZ4_DIR}/lz4hc.c
    ${LZ4_DIR}/xxhash.c
)

# ── Build the extension module ──────────────────────────────────────────
nanobind_add_module(_bcsv STABLE_ABI ${BCSV_SOURCES} ${LZ4_SOURCES})

target_include_directories(_bcsv PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${BCSV_INCLUDE_DIR}
    ${LZ4_DIR}
)

set_target_properties(_bcsv PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 11
)

# ── Platform-specific flags ─────────────────────────────────────────────
if(MSVC)
    target_compile_options(_bcsv PRIVATE
        /O2
        /std:c++20
    )
    target_compile_definitions(_bcsv PRIVATE BCSV_HAS_BATCH_CODEC=1)
elseif(APPLE)
    # Apple Clang lacks std::jthread/stop_token — disable batch codec
    target_compile_options(_bcsv PRIVATE
        -O3 -Wall -Wextra
        -mmacosx-version-min=13.3
    )
else()
    # Linux: full C++20 with batch codec + pthreads
    target_compile_options(_bcsv PRIVATE
        -O3 -Wall -Wextra
    )
    target_compile_definitions(_bcsv PRIVATE BCSV_HAS_BATCH_CODEC=1)
    target_link_libraries(_bcsv PRIVATE pthread)
endif()

# ── Install ─────────────────────────────────────────────────────────────
install(TARGETS _bcsv LIBRARY DESTINATION pybcsv)
