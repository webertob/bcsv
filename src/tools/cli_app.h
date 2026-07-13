/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

#pragma once

/**
 * @file cli_app.h
 * @brief Shared CLI11 integration layer for BCSV command-line tools.
 *
 * Builds on cli_common.h (value/format helpers) and the vendored CLI11 parser
 * to standardise argument handling across all tools:
 *   - setupVersionFlag()  — consistent "-V,--version" output (matches printVersion)
 *   - parseHandled()      — uniform --help/--version + parse-error handling with a
 *                           tool-specific error exit code
 *   - addCodecOptions()   — the shared --row-codec/--file-codec/--compression-level/
 *                           --block-size block used by the writer tools
 *
 * Tools include this instead of wiring CLI11 directly. On Windows it also undoes
 * <windows.h> macro pollution pulled in transitively by CLI11.
 */

#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// CLI11 transitively includes <windows.h> on Windows. Suppress its min/max
// macros before that happens so std::min/std::max in project headers compile.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include <CLI11.hpp>

// <windows.h> (transitively included by CLI11 on Windows) defines a number of
// all-caps macros that collide with ordinary C++ identifiers used in project
// headers (e.g. bcsv::ColumnType::VOID, comparison.h's `enum class CompareMode
// { STRICT, ... }`). CLI11 itself is already fully included above, and the CLI
// tools use no Win32 API, so it is safe to undo these macros before including
// any project headers or tool code.
#ifdef _WIN32
#  undef VOID
#  undef STRICT
#  undef IN
#  undef OUT
#  undef OPTIONAL
#  undef ERROR
#  undef DELETE
#  undef DIFFERENCE
#  undef ABSOLUTE
#  undef RELATIVE
#  undef CONST
#  undef SAME
#endif

#include "cli_common.h"

namespace bcsv_cli {

/// Attach the standard "-V,--version" flag whose output matches printVersion(),
/// and apply shared app conventions (treat '/path' tokens as positionals rather
/// than Windows-style options, so Unix-style paths work on Windows too).
inline void setupVersionFlag(CLI::App& app, const std::string& tool) {
    app.allow_windows_style_options(false);
    app.set_version_flag(
        "-V,--version",
        [tool]() {
            std::ostringstream os;
            printVersion(tool, os);
            std::string s = os.str();
            // printVersion emits a trailing newline; CLI11 adds its own, so trim one.
            if (!s.empty() && s.back() == '\n')
                s.pop_back();
            return s;
        },
        "Show version information and exit");
}

/// Run app.parse(); handle --help/--version and parse errors uniformly.
/// On --help/--version this prints the message and returns exit code 0.
/// On a genuine parse error it prints the message and returns `error_code`
/// (tools with a custom error exit code, e.g. bcsvValidate = 2, pass it here).
/// Returns std::nullopt when parsing succeeded and main should continue. Usage:
///     if (auto rc = bcsv_cli::parseHandled(app, argc, argv, 2)) return *rc;
inline std::optional<int> parseHandled(CLI::App& app, int argc, char** argv,
                                       int error_code) {
    try {
        app.parse(argc, argv);
        return std::nullopt;
    } catch (const CLI::ParseError& e) {
        const int code = app.exit(e);   // help/version -> 0, errors -> non-zero
        return (code == 0) ? 0 : error_code;
    }
}

/// The valid --row-codec values as a vector (for CLI::IsMember).
inline std::vector<std::string> rowCodecChoices() {
    return {std::begin(VALID_ROW_CODECS), std::end(VALID_ROW_CODECS)};
}

/// The valid --file-codec values as a vector (for CLI::IsMember).
inline std::vector<std::string> fileCodecChoices() {
    return {std::begin(VALID_FILE_CODECS), std::end(VALID_FILE_CODECS)};
}

/// Install the standard encoding options shared by the writer tools
/// (csv2bcsv, bcsvGenerator, bcsvSampler): --row-codec, --file-codec,
/// --compression-level, --block-size. Values are validated and defaults
/// are shown in --help. Binds directly to the caller's Config fields.
inline void addCodecOptions(CLI::App&    app,
                            std::string& row_codec,
                            std::string& file_codec,
                            size_t&      compression_level,
                            size_t&      block_size_kb) {
    // take_last(): the legacy linear parser let a repeated codec flag override
    // earlier occurrences (last wins) rather than erroring; preserve that.
    app.add_option("--row-codec", row_codec, "Row codec: delta, zoh, flat")
        ->check(CLI::IsMember(rowCodecChoices()))
        ->take_last()
        ->capture_default_str();
    app.add_option("--file-codec", file_codec,
                   "File codec: packet_lz4_batch, packet_lz4, packet, stream_lz4, stream")
        ->check(CLI::IsMember(fileCodecChoices()))
        ->take_last()
        ->capture_default_str();
    app.add_option("--compression-level", compression_level, "LZ4 compression level")
        ->check(CLI::NonNegativeNumber)
        ->take_last()
        ->capture_default_str();
    app.add_option("--block-size", block_size_kb, "Block size in KB")
        ->check(CLI::PositiveNumber)
        ->take_last()
        ->capture_default_str();
}

} // namespace bcsv_cli
