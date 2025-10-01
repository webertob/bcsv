# GetGitVersion.cmake
# Extract version from Git tags or use a default version

# Try to get version from Git tags
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_VERSION)
    # If no Git tag is found, use a default version
    set(VERSION_STRING "0.0.1")
    message(STATUS "No Git tag found, using default version: ${VERSION_STRING}")
else()
    # Remove leading 'v' if present (e.g., v1.2.3 -> 1.2.3)
    string(REGEX REPLACE "^v" "" VERSION_STRING "${GIT_VERSION}")
    message(STATUS "Version from Git tag: ${VERSION_STRING}")
endif()

# Parse version components (MAJOR.MINOR.PATCH)
string(REPLACE "." ";" VERSION_LIST ${VERSION_STRING})
list(LENGTH VERSION_LIST VERSION_COMPONENTS)

if(VERSION_COMPONENTS GREATER_EQUAL 1)
    list(GET VERSION_LIST 0 VERSION_MAJOR)
else()
    set(VERSION_MAJOR 0)
endif()

if(VERSION_COMPONENTS GREATER_EQUAL 2)
    list(GET VERSION_LIST 1 VERSION_MINOR)
else()
    set(VERSION_MINOR 0)
endif()

if(VERSION_COMPONENTS GREATER_EQUAL 3)
    list(GET VERSION_LIST 2 VERSION_PATCH)
else()
    set(VERSION_PATCH 1)
endif()

message(STATUS "Version components - MAJOR: ${VERSION_MAJOR}, MINOR: ${VERSION_MINOR}, PATCH: ${VERSION_PATCH}")
