# Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
# 
# This file is part of the BCSV library.
# 
# Licensed under the MIT License. See LICENSE file in the project root 
# for full license information.

# GetGitVersion.cmake - Resolve the project version
# Sets VERSION_STRING, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH variables.
#
# VERSION.txt is the single source of truth. Git is used only to *verify* it,
# never to supply it. This ordering matters: the version is stamped into every
# file header and version::MINOR selects the file codec (see writer.hpp), so a
# build that guesses its own version writes mislabelled - or wrongly encoded -
# data. Deriving the version from `git describe` made that guess silently
# whenever git was unavailable, which is how the 1.5.12 release shipped Linux
# natives stamped 1.5.11: the containerised build jobs could not read the
# repository ("detected dubious ownership") and fell through to a VERSION.txt
# that release tagging never updated.
#
# Options:
#   BCSV_STRICT_VERSION - fail the configure step if the version cannot be
#                         verified against git. Enable for every release and
#                         packaging build; leave off for local development
#                         from a tarball or a git-less checkout.

option(BCSV_STRICT_VERSION "Fail configuration if the version cannot be verified against git tags" OFF)

# ── Read the authoritative version ──────────────────────────────────────────
set(_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt")
if(NOT EXISTS "${_VERSION_FILE}")
    message(FATAL_ERROR
        "VERSION.txt not found at ${_VERSION_FILE}.\n"
        "It is the single source of truth for the BCSV version and must be present.")
endif()

file(READ "${_VERSION_FILE}" _FILE_VERSION)
string(STRIP "${_FILE_VERSION}" _FILE_VERSION)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _FV_MATCH "${_FILE_VERSION}")
if(NOT _FV_MATCH)
    message(FATAL_ERROR
        "VERSION.txt contains '${_FILE_VERSION}', which is not a valid X.Y.Z version.")
endif()

set(VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VERSION_MINOR "${CMAKE_MATCH_2}")
set(VERSION_PATCH "${CMAKE_MATCH_3}")
set(VERSION_STRING "${_FILE_VERSION}")

# ── Verify it against git ───────────────────────────────────────────────────
# Nothing below may change VERSION_STRING. Git either confirms the declared
# version or reports why it could not, so a mismatch is loud instead of silent.
find_package(Git QUIET)

set(_GIT_USABLE FALSE)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --is-inside-work-tree
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _GIT_INSIDE_WORKTREE
        ERROR_VARIABLE _GIT_WORKTREE_ERROR
        RESULT_VARIABLE _GIT_WORKTREE_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_GIT_WORKTREE_RESULT EQUAL 0 AND _GIT_INSIDE_WORKTREE STREQUAL "true")
        set(_GIT_USABLE TRUE)
    endif()
endif()

if(_GIT_USABLE)
    # An exact tag on HEAD means this is a release build: the tag and
    # VERSION.txt must agree, or one of the two was forgotten.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match --match "v[0-9]*.[0-9]*.[0-9]*"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_EXACT_TAG
        ERROR_QUIET
        RESULT_VARIABLE _GIT_EXACT_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(_GIT_EXACT_RESULT EQUAL 0 AND GIT_EXACT_TAG)
        if(NOT GIT_EXACT_TAG STREQUAL "v${VERSION_STRING}")
            message(FATAL_ERROR
                "Version mismatch: HEAD is tagged ${GIT_EXACT_TAG} but VERSION.txt says "
                "${VERSION_STRING}.\n"
                "A release must bump VERSION.txt in a commit *before* the tag is created. "
                "Fix VERSION.txt (or move the tag) so the two agree - see VERSIONING.md.")
        endif()
        message(STATUS "Version ${VERSION_STRING} (VERSION.txt, confirmed by tag ${GIT_EXACT_TAG})")
    else()
        # Untagged commit: a development build between releases. VERSION.txt is
        # still authoritative; report the distance from the last tag for context.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(GIT_DESCRIBE)
            message(STATUS "Version ${VERSION_STRING} (VERSION.txt, development build at ${GIT_DESCRIBE})")
        else()
            message(STATUS "Version ${VERSION_STRING} (VERSION.txt, no version tags reachable)")
        endif()
    endif()
else()
    if(GIT_FOUND)
        string(STRIP "${_GIT_WORKTREE_ERROR}" _GIT_WORKTREE_ERROR)
        set(_VERSION_UNVERIFIED_REASON "git could not read ${CMAKE_CURRENT_SOURCE_DIR}: ${_GIT_WORKTREE_ERROR}")
    else()
        set(_VERSION_UNVERIFIED_REASON "git executable not found")
    endif()

    if(BCSV_STRICT_VERSION)
        message(FATAL_ERROR
            "Cannot verify version ${VERSION_STRING} against git tags: "
            "${_VERSION_UNVERIFIED_REASON}.\n"
            "BCSV_STRICT_VERSION is ON, so this build refuses to produce an "
            "artifact whose version could not be checked. If this is a "
            "containerised build, the workspace owner probably differs from the "
            "build user - add:\n"
            "  git config --global --add safe.directory ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    message(STATUS "Version ${VERSION_STRING} (VERSION.txt, unverified: ${_VERSION_UNVERIFIED_REASON})")
endif()

# Generate version header file into the build tree (not the source tree)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/bcsv/version_generated.h"
    @ONLY
)
