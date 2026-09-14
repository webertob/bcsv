# Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
#
# This file is part of the BCSV library.
#
# Licensed under the MIT License. See LICENSE file in the project root
# for full license information.

# Export-surface guard for libbcsv_c_api (docs/adr/0007): the dynamic symbol
# table must define exactly the bcsv_* C API. A re-exported C++ runtime —
# std::, __cxa_, vtables, operator new/delete — is interposed by the first
# libstdc++ copy in the process, so one runtime allocates what another frees.
# The fix is cmake/bcsv_c_api_exports.lds; this is the tripwire that it stays
# applied.
#
# Usage: cmake -DBCSV_LIBRARY=<libbcsv_c_api.so> -P bcsv_c_api_export_test.cmake

if(NOT DEFINED BCSV_LIBRARY)
    message(FATAL_ERROR "Set BCSV_LIBRARY to the shared library to check")
endif()

if(WIN32 OR APPLE)
    # COFF has no interposition model matching ELF; Mach-O uses a two-level
    # namespace, where a plugin's libstdc++ symbols cannot replace the host's.
    # The version script is not applied there, so this guard is Linux/ELF-only.
    return()
endif()

find_program(NM_EXECUTABLE nm REQUIRED)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env LC_ALL=C ${NM_EXECUTABLE} --defined-only --extern-only ${BCSV_LIBRARY}
    OUTPUT_VARIABLE nm_out
    RESULT_VARIABLE nm_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT nm_rc EQUAL 0)
    message(FATAL_ERROR "nm failed on ${BCSV_LIBRARY}")
endif()

# Defined dynamic symbols are the whole external ABI. Expected: strictly the
# bcsv_* C API. Anything else (std::, __cxa_, _ZT, op new/delete, ...) means
# the version script stopped doing its job.
set(stray "")
string(REPLACE "\n" ";" nm_lines "${nm_out}")
foreach(line IN LISTS nm_lines)
    # Symbol name is the last whitespace-separated field of the nm output.
    string(REGEX REPLACE "^[^\n]*[ \t]" "" symbol "${line}")
    if(NOT "${symbol}" MATCHES "^bcsv_")
        list(APPEND stray "${symbol}")
    endif()
endforeach()

if(stray)
    list(LENGTH stray stray_count)
    list(SUBLIST stray 0 25 stray_head)
    string(REPLACE ";" "\n  " stray_head "${stray_head}")
    message(FATAL_ERROR
        "libbcsv_c_api exports ${stray_count} symbol(s) outside the bcsv_* C API.\n"
        "Non-C-API symbols must stay local: the host process's own C++ runtime\n"
        "would interpose them and corrupt the heap (docs/adr/0007). Check that\n"
        "cmake/bcsv_c_api_exports.lds is still applied to target bcsv_c_api.\n"
        "Stray symbols (first 25):\n  ${stray_head}")
endif()

message(STATUS "export-surface guard: OK (${BCSV_LIBRARY} exports only bcsv_*)")
