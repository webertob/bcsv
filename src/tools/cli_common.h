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
 * @file cli_common.h
 * @brief Shared utilities for BCSV CLI tools
 *
 * Provides standardised helpers used across all CLI tools:
 *   - columnTypeStr()           — ColumnType → human-readable string
 *   - formatBytes()             — byte count → "1.23 MB" / "456 KB" / "789 bytes"
 *   - encodingDescription()     — row/file codec + level → summary string
 *   - printLayoutSummary()      — tabular layout dump to any ostream
 *   - parseFileCodecFlags()     — "--file-codec" string → FileFlags + compression level
 *   - validateRowCodec()        — "--row-codec" string validation
 *   - VALID_FILE_CODECS / VALID_ROW_CODECS — canonical string lists
 *
 * Tools opt-in to specific helpers via ordinary #include.
 */

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <bcsv/bcsv.h>

namespace bcsv_cli {

// ── Canonical codec string constants ───────────────────────────────

inline constexpr const char* VALID_FILE_CODECS[] = {
    "packet_lz4_batch", "packet_lz4", "packet", "stream_lz4", "stream"
};

inline constexpr const char* VALID_ROW_CODECS[] = {
    "delta", "zoh", "flat"
};

inline constexpr const char* DEFAULT_FILE_CODEC = "packet_lz4_batch";
inline constexpr const char* DEFAULT_ROW_CODEC  = "delta";

// ── columnTypeStr ──────────────────────────────────────────────────

/// Convert ColumnType to human-readable string.
inline std::string columnTypeStr(bcsv::ColumnType type) {
    switch (type) {
        case bcsv::ColumnType::BOOL:   return "bool";
        case bcsv::ColumnType::INT8:   return "int8";
        case bcsv::ColumnType::UINT8:  return "uint8";
        case bcsv::ColumnType::INT16:  return "int16";
        case bcsv::ColumnType::UINT16: return "uint16";
        case bcsv::ColumnType::INT32:  return "int32";
        case bcsv::ColumnType::UINT32: return "uint32";
        case bcsv::ColumnType::INT64:  return "int64";
        case bcsv::ColumnType::UINT64: return "uint64";
        case bcsv::ColumnType::FLOAT:  return "float";
        case bcsv::ColumnType::DOUBLE: return "double";
        case bcsv::ColumnType::STRING: return "string";
        default:                       return "unknown";
    }
}

// ── formatBytes ────────────────────────────────────────────────────

/// Format a byte count as human-readable string.
inline std::string formatBytes(uintmax_t bytes) {
    std::ostringstream oss;
    if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        oss << bytes << " bytes";
    }
    return oss.str();
}

// ── printLayoutSummary ─────────────────────────────────────────────

/// Print vertical layout table: type histogram + full column listing.
inline void printLayoutSummary(const std::string& label,
                               const bcsv::Layout& layout,
                               std::ostream& os = std::cerr) {
    const size_t n = layout.columnCount();
    if (n == 0) {
        os << label << ": (empty)\n";
        return;
    }

    // Build type histogram
    std::map<std::string, size_t> type_counts;
    size_t max_name_len = 4;   // minimum width for "Name" header
    size_t max_type_len = 4;   // minimum width for "Type" header
    for (size_t i = 0; i < n; ++i) {
        const std::string name = layout.columnName(i);
        const std::string type = columnTypeStr(layout.columnType(i));
        type_counts[type]++;
        if (name.size() > max_name_len) max_name_len = name.size();
        if (type.size() > max_type_len) max_type_len = type.size();
    }

    // Type histogram line
    os << label << " (" << n << " columns)  [ ";
    bool first = true;
    for (const auto& [tname, cnt] : type_counts) {
        if (!first) os << ", ";
        os << cnt << "\xc3\x97" << tname;   // UTF-8 ×
        first = false;
    }
    os << " ]\n";

    // Column index width
    size_t idx_width = 1;
    for (size_t v = n - 1; v >= 10; v /= 10) ++idx_width;
    if (idx_width < 3) idx_width = 3;

    // Table header
    os << "  " << std::right << std::setw(static_cast<int>(idx_width)) << "Idx"
       << "  " << std::left  << std::setw(static_cast<int>(max_name_len)) << "Name"
       << "  " << std::left  << std::setw(static_cast<int>(max_type_len)) << "Type"
       << "\n";

    // Separator
    os << "  " << std::string(idx_width, '-')
       << "  " << std::string(max_name_len, '-')
       << "  " << std::string(max_type_len, '-')
       << "\n";

    // Column rows
    for (size_t i = 0; i < n; ++i) {
        os << "  " << std::right << std::setw(static_cast<int>(idx_width)) << i
           << "  " << std::left  << std::setw(static_cast<int>(max_name_len)) << layout.columnName(i)
           << "  " << std::left  << std::setw(static_cast<int>(max_type_len)) << columnTypeStr(layout.columnType(i))
           << "\n";
    }
}

// ── Codec validation / mapping ────────────────────────────────────

/// Validate a --row-codec string.  Throws std::runtime_error on invalid value.
inline void validateRowCodec(const std::string& codec) {
    if (codec != "delta" && codec != "zoh" && codec != "flat") {
        throw std::runtime_error(
            "Unknown row codec '" + codec +
            "'. Expected: delta, zoh, flat.");
    }
}

/// Validate a --file-codec string.  Throws std::runtime_error on invalid value.
inline void validateFileCodec(const std::string& codec) {
    if (codec != "packet_lz4_batch" &&
        codec != "packet_lz4" &&
        codec != "packet" &&
        codec != "stream_lz4" &&
        codec != "stream") {
        throw std::runtime_error(
            "Unknown file codec '" + codec +
            "'. Expected: packet_lz4_batch, packet_lz4, packet, stream_lz4, stream.");
    }
}

/// Result of parsing file-codec settings into FileFlags + compression level.
struct FileCodecSettings {
    bcsv::FileFlags flags       = bcsv::FileFlags::NONE;
    size_t          comp_level  = 1;
};

/// Map (file_codec string, row_codec string, user compression_level) → FileFlags + effective comp_level.
inline FileCodecSettings resolveCodecFlags(const std::string& file_codec,
                                           const std::string& row_codec,
                                           size_t compression_level) {
    FileCodecSettings s;

    const bool has_lz4   = (file_codec == "packet_lz4_batch" ||
                            file_codec == "packet_lz4" ||
                            file_codec == "stream_lz4");
    const bool has_batch = (file_codec == "packet_lz4_batch");
    const bool is_stream = (file_codec == "stream_lz4" ||
                            file_codec == "stream");

    if (has_batch) {
#ifdef BCSV_HAS_BATCH_CODEC
        s.flags = s.flags | bcsv::FileFlags::BATCH_COMPRESS;
#else
        throw std::runtime_error(
            "Batch codec not available (BCSV_ENABLE_BATCH_CODEC=OFF). "
            "Use --file-codec packet_lz4 instead.");
#endif
    }
    if (is_stream)
        s.flags = s.flags | bcsv::FileFlags::STREAM_MODE;

    // Row codec flags — note:  Writer<…, RowCodecZoH001> and WriterDelta
    // inject their own flag via RowCodecFileFlags, so the ZoH flag set here
    // is technically redundant but harmless (ORed).
    if (row_codec == "zoh")
        s.flags = s.flags | bcsv::FileFlags::ZERO_ORDER_HOLD;

    s.comp_level = has_lz4 ? compression_level : 0;
    return s;
}

/// Build a short encoding description string for summary output.
inline std::string encodingDescription(const std::string& row_codec,
                                       const std::string& file_codec,
                                       size_t compression_level) {
    return row_codec + " + " + file_codec
         + " (level " + std::to_string(compression_level) + ")";
}

// ── Writer dispatch helper ────────────────────────────────────────

/// Open the appropriate Writer type (delta/zoh/flat) based on row_codec
/// string and call `action(writer)`.  The caller provides a generic
/// lambda that works with any Writer type (via auto&).
///
/// Usage:
///     bcsv_cli::withWriter(layout, row_codec, [&](auto& writer) {
///         writer.open(path, overwrite, comp_level, block_kb, flags);
///         // … write loop …
///         writer.close();
///     });
template<typename LayoutType, typename Action>
void withWriter(const LayoutType& layout,
                const std::string& row_codec,
                Action&& action) {
    if (row_codec == "delta") {
        bcsv::WriterDelta<LayoutType> writer(layout);
        action(writer);
    } else if (row_codec == "zoh") {
        bcsv::WriterZoH<LayoutType> writer(layout);
        action(writer);
    } else {
        bcsv::Writer<LayoutType> writer(layout);
        action(writer);
    }
}

} // namespace bcsv_cli

