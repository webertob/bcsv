# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# GetGitVersion.cmake - Extract version information from Git tags
# Sets VERSION_STRING, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH variables

# Find Git executable
find_package(Git QUIET)

# Initialize defaults (fallback values when Git is not available)
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0") 
set(VERSION_PATCH "3")
set(VERSION_STRING "1.0.3")

if(GIT_FOUND)
    # Get the latest version tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_LATEST_TAG
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    # Get current commit description (includes commits since tag)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    if(GIT_LATEST_TAG)
        # Extract version numbers from tag (format: v1.2.3)
        string(REGEX MATCH "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)" VERSION_MATCH "${GIT_LATEST_TAG}")
        if(VERSION_MATCH)
            set(VERSION_MAJOR "${CMAKE_MATCH_1}")
            set(VERSION_MINOR "${CMAKE_MATCH_2}")
            set(VERSION_PATCH "${CMAKE_MATCH_3}")
            set(VERSION_STRING_BASE "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
            set(VERSION_STRING "${VERSION_STRING_BASE}")
            
            # If we have commits since the tag, add development suffix for display only
            if(GIT_DESCRIBE AND NOT GIT_DESCRIBE STREQUAL GIT_LATEST_TAG)
                string(REGEX MATCH "^v[0-9]+\\.[0-9]+\\.[0-9]+-([0-9]+)-g[0-9a-f]+(.*)" DEV_MATCH "${GIT_DESCRIBE}")
                if(DEV_MATCH)
                    set(COMMITS_SINCE_TAG "${CMAKE_MATCH_1}")
                    set(DIRTY_SUFFIX "${CMAKE_MATCH_2}")
                    set(VERSION_STRING_FULL "${VERSION_STRING_BASE}-dev.${COMMITS_SINCE_TAG}${DIRTY_SUFFIX}")
                    message(STATUS "Git version: ${VERSION_STRING_FULL} (from tag ${GIT_LATEST_TAG})")
                else()
                    message(STATUS "Git version: ${VERSION_STRING} (from tag ${GIT_LATEST_TAG})")
                endif()
            else()
                message(STATUS "Git version: ${VERSION_STRING} (from tag ${GIT_LATEST_TAG})")
            endif()
        else()
            message(WARNING "Invalid version tag format: ${GIT_LATEST_TAG} (expected vX.Y.Z)")
        endif()
    else()
        message(STATUS "No version tags found, using default version: ${VERSION_STRING}")
    endif()
else()
    message(STATUS "Git not found, using default version: ${VERSION_STRING}")
endif()

# Generate version header file into the build tree (not the source tree)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_BINARY_DIR}/include/bcsv/version_generated.h"
    @ONLY
)