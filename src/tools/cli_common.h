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
 *   - resolveCodecFlags()       — codec strings → FileFlags + compression level
 *   - VALID_FILE_CODECS / VALID_ROW_CODECS — canonical string lists
 *
 * Tools opt-in to specific helpers via ordinary #include.
 */

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <bcsv/bcsv.h>

#ifdef __has_include
#  if __has_include(<unistd.h>)
#    include <unistd.h>
#    define BCSV_CLI_HAS_MKSTEMP 1
#  endif
#endif

#ifndef BCSV_CLI_HAS_MKSTEMP
#  include <io.h>  // Windows/MSVC: _mktemp_s
#endif

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

// ── Version / program identity ─────────────────────────────────────

/// Derive a clean tool name from argv[0]: strips the directory prefix
/// and a trailing ".exe" (Windows), e.g. "C:\bin\bcsvHead.exe" → "bcsvHead".
inline std::string programName(const char* argv0) {
    std::string name = (argv0 && *argv0) ? argv0 : "bcsv";
    // Strip directory (handle both '/' and '\\').
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    // Strip a trailing ".exe" (case-insensitive).
    if (name.size() > 4) {
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".exe")
            name = name.substr(0, name.size() - 4);
    }
    return name;
}

/// One-line identity string, e.g. "bcsvHead (BCSV 1.5.1)".
inline std::string versionTag(const std::string& tool) {
    return tool + " (BCSV " + bcsv::getVersion() + ")";
}

/// Print the full --version block to `os`.
inline void printVersion(const std::string& tool, std::ostream& os = std::cout) {
    os << versionTag(tool) << "\n"
       << "Binary-CSV command-line tools\n"
       << "Copyright (c) 2025-2026 Tobias Weber\n"
       << "License: MIT\n";
}

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
        os << cnt << "x" << tname;
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

// ── Codec mapping ─────────────────────────────────────────────────

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

/// Reverse-map FileFlags + compression level back to human-readable codec names.
struct CodecNames {
    std::string row_codec;
    std::string file_codec;
};

inline CodecNames codecNamesFromFlags(bcsv::FileFlags flags, uint8_t comp_level) {
    CodecNames names;

    // Row codec
    const bool has_delta = (flags & bcsv::FileFlags::DELTA_ENCODING) != bcsv::FileFlags::NONE;
    const bool has_zoh   = (flags & bcsv::FileFlags::ZERO_ORDER_HOLD) != bcsv::FileFlags::NONE;
    if (has_delta)
        names.row_codec = "delta";
    else if (has_zoh)
        names.row_codec = "zoh";
    else
        names.row_codec = "flat";

    // File codec — mirrors resolveFileCodecId() logic
    const bool stream    = (flags & bcsv::FileFlags::STREAM_MODE) != bcsv::FileFlags::NONE;
    const bool batch     = (flags & bcsv::FileFlags::BATCH_COMPRESS) != bcsv::FileFlags::NONE;
    const bool compressed = comp_level > 0;

    if (stream) {
        names.file_codec = compressed ? "stream_lz4" : "stream";
    } else if (batch && compressed) {
        names.file_codec = "packet_lz4_batch";
    } else {
        names.file_codec = compressed ? "packet_lz4" : "packet";
    }

    return names;
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
        bcsv::WriterFlat<LayoutType> writer(layout);
        action(writer);
    }
}

// ── Index range parsing ───────────────────────────────────────────

/// A set of sorted, merged, inclusive index ranges (e.g. columns or rows).
struct IndexRangeSet {
    std::vector<std::pair<size_t, size_t>> ranges; // sorted, non-overlapping

    /// Returns true if `idx` falls within at least one range.
    /// Empty ranges → match-all (no user scope requested).
    bool contains(size_t idx) const noexcept {
        if (ranges.empty())
            return true;
        for (const auto& [lo, hi] : ranges)
            if (idx >= lo && idx <= hi)
                return true;
        return false;
    }

    /// Returns true if a non-empty user scope was provided.
    bool active() const noexcept { return !ranges.empty(); }

    /// Materialise the selected indices in ascending order.
    /// Empty ranges (no scope) → all indices [0, total).
    /// Ranges are pre-sorted and merged, so the result is ascending and unique.
    std::vector<size_t> toIndices(size_t total) const {
        std::vector<size_t> out;
        if (ranges.empty()) {
            out.reserve(total);
            for (size_t i = 0; i < total; ++i)
                out.push_back(i);
            return out;
        }
        for (const auto& [lo, hi] : ranges)
            for (size_t i = lo; i <= hi; ++i)
                out.push_back(i);
        return out;
    }
};

/** Parse "0:99,200:-10,3" → sorted, merged, inclusive ranges.
 * Single values ("3") expand to a one-element range. Negative indices
 * count from the end (`idx + total`). Open ends (":5", "2:") default to
 * 0 / total-1. Empty spec or total == 0 → empty ranges (match-all).
 * Throws std::runtime_error if a spec is given but no ranges are valid
 * or an index is out of bounds. */
inline IndexRangeSet parseIndexRanges(const std::string& spec, size_t total) {
    IndexRangeSet r;
    bool          specified = !spec.empty();
    if (!specified || total == 0)
        return r;

    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : spec) {
            if (c == ',') {
                parts.push_back(cur);
                cur.clear();
                continue;
            }
            cur += c;
        }
        parts.push_back(cur);
    }

    for (auto& p : parts) {
        auto p0 = p;
        // inline trim
        size_t a = 0, b = p0.size();
        while (a < b && std::isspace(static_cast<unsigned char>(p0[a])))
            ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(p0[b - 1])))
            --b;
        p0 = p0.substr(a, b - a);
        if (p0.empty())
            continue;

        int64_t lo, hi;
        auto    colon = p0.find(':');
        if (colon == std::string::npos) {
            lo = hi = std::stoll(p0);
        } else {
            std::string ls = p0.substr(0, colon);
            std::string hs = p0.substr(colon + 1);
            lo             = ls.empty() ? 0 : std::stoll(ls);
            hi             = hs.empty() ? static_cast<int64_t>(total) - 1 : std::stoll(hs);
        }

        if (lo < 0)
            lo += static_cast<int64_t>(total);
        if (hi < 0)
            hi += static_cast<int64_t>(total);

        if (lo < 0 || lo > hi || hi >= static_cast<int64_t>(total)) {
            throw std::runtime_error("Range [" + std::to_string(lo) + ":" + std::to_string(hi) +
                                     "] is out of bounds (0-" + std::to_string(total - 1) + ")");
        }
        r.ranges.emplace_back(static_cast<size_t>(lo), static_cast<size_t>(hi));
    }

    if (r.ranges.empty()) {
        throw std::runtime_error("No valid " + std::string(specified && parts.size() > 1 ? "range elements" : "ranges") +
                                 " found for spec '" + spec + "'");
    }

    std::sort(r.ranges.begin(), r.ranges.end());
    {
        std::vector<std::pair<size_t, size_t>> merged;
        for (auto& el : r.ranges) {
            if (merged.empty())
                merged.push_back(el);
            else if (el.first <= merged.back().second + 1)
                merged.back().second = std::max(merged.back().second, el.second);
            else
                merged.push_back(el);
        }
        r.ranges = std::move(merged);
    }
    return r;
}

/** Streaming variant of parseIndexRanges for inputs whose total count is not
 * known up front (e.g. CSV data rows). Negative indices are rejected with a
 * clear message; an open upper end ("100:") extends to SIZE_MAX. */
inline IndexRangeSet parseIndexRangesUnbounded(const std::string& spec) {
    // Negatives count from the end, which is meaningless while streaming —
    // reject them up front by spec text (the range grammar has no other
    // legitimate use for '-').
    if (spec.find('-') != std::string::npos)
        throw std::runtime_error("Negative indices are not supported here (the total "
                                 "count is unknown while streaming): '" + spec + "'");
    constexpr size_t UNBOUNDED = std::numeric_limits<int64_t>::max();
    IndexRangeSet    r = parseIndexRanges(spec, UNBOUNDED);
    // Open upper ends were folded to UNBOUNDED-1; widen them to SIZE_MAX.
    for (auto& [lo, hi] : r.ranges)
        if (hi >= UNBOUNDED - 1)
            hi = std::numeric_limits<size_t>::max();
    return r;
}

/// Monotone membership cursor over an IndexRangeSet: contains(idx) must be
/// called with non-decreasing idx and is O(1) amortised (plain
/// IndexRangeSet::contains is O(#ranges) per call). exhausted(idx) tells a
/// streaming caller it is past the last selected index and may stop reading.
struct IndexRangeCursor {
    const IndexRangeSet& set;
    size_t               pos = 0;   // current range index

    explicit IndexRangeCursor(const IndexRangeSet& s) : set(s) {}

    bool contains(size_t idx) noexcept {
        if (set.ranges.empty())
            return true;   // no scope → match-all
        while (pos < set.ranges.size() && idx > set.ranges[pos].second)
            ++pos;
        return pos < set.ranges.size() &&
               idx >= set.ranges[pos].first && idx <= set.ranges[pos].second;
    }

    bool exhausted(size_t idx) const noexcept {
        if (set.ranges.empty())
            return false;
        return idx > set.ranges.back().second;
    }
};

// ── JSON string escaping ──────────────────────────────────────────

/// Escape a string as a JSON string literal (including the surrounding quotes).
inline std::string jsonStr(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    out += '"';
    return out;
}

// ── Column type classification ────────────────────────────────────

/// True for BOOL and every fixed-width integer type.
inline bool isIntegerType(bcsv::ColumnType t) noexcept {
    switch (t) {
        case bcsv::ColumnType::BOOL:
        case bcsv::ColumnType::INT8:   case bcsv::ColumnType::INT16:
        case bcsv::ColumnType::INT32:  case bcsv::ColumnType::INT64:
        case bcsv::ColumnType::UINT8:  case bcsv::ColumnType::UINT16:
        case bcsv::ColumnType::UINT32: case bcsv::ColumnType::UINT64:
            return true;
        default:
            return false;
    }
}

inline bool isSignedIntType(bcsv::ColumnType t) noexcept {
    switch (t) {
        case bcsv::ColumnType::INT8:  case bcsv::ColumnType::INT16:
        case bcsv::ColumnType::INT32: case bcsv::ColumnType::INT64:
            return true;
        default:
            return false;
    }
}

inline bool isUnsignedIntType(bcsv::ColumnType t) noexcept {
    switch (t) {
        case bcsv::ColumnType::UINT8:  case bcsv::ColumnType::UINT16:
        case bcsv::ColumnType::UINT32: case bcsv::ColumnType::UINT64:
            return true;
        default:
            return false;
    }
}

inline bool isFloatType(bcsv::ColumnType t) noexcept {
    return t == bcsv::ColumnType::FLOAT || t == bcsv::ColumnType::DOUBLE;
}

/// Every type except VOID is a legal cast source/target.
inline bool isCastableType(bcsv::ColumnType t) noexcept {
    return t != bcsv::ColumnType::VOID;
}

// ── Type-name parsing (canonical names + aliases) ─────────────────

/// Parse a type name (canonical or alias, case-insensitive) into a ColumnType.
/// `allow_void` permits the literal "void" (for positional-list slots that must
/// cover an already-VOID column); otherwise VOID is rejected as non-castable.
/// Throws std::runtime_error on an empty or unknown name.
inline bcsv::ColumnType parseColumnType(std::string s, bool allow_void = false) {
    // trim
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    s = s.substr(a, b - a);
    // lower-case
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (s.empty())
        throw std::runtime_error("Empty type name");

    if (s == "void") {
        if (allow_void)
            return bcsv::ColumnType::VOID;
        throw std::runtime_error("'void' is not a castable type");
    }

    static const std::map<std::string, bcsv::ColumnType> ALIASES = {
        {"bool", bcsv::ColumnType::BOOL},     {"b", bcsv::ColumnType::BOOL},
        {"int8", bcsv::ColumnType::INT8},     {"i8", bcsv::ColumnType::INT8},   {"sbyte", bcsv::ColumnType::INT8},
        {"int16", bcsv::ColumnType::INT16},   {"i16", bcsv::ColumnType::INT16}, {"short", bcsv::ColumnType::INT16},
        {"int32", bcsv::ColumnType::INT32},   {"i32", bcsv::ColumnType::INT32}, {"int", bcsv::ColumnType::INT32},
        {"int64", bcsv::ColumnType::INT64},   {"i64", bcsv::ColumnType::INT64}, {"long", bcsv::ColumnType::INT64},
        {"uint8", bcsv::ColumnType::UINT8},   {"ui8", bcsv::ColumnType::UINT8}, {"u8", bcsv::ColumnType::UINT8},
        {"uchar", bcsv::ColumnType::UINT8},   {"char", bcsv::ColumnType::UINT8}, {"ch", bcsv::ColumnType::UINT8}, {"byte", bcsv::ColumnType::UINT8},
        {"uint16", bcsv::ColumnType::UINT16}, {"ui16", bcsv::ColumnType::UINT16}, {"u16", bcsv::ColumnType::UINT16}, {"ushort", bcsv::ColumnType::UINT16},
        {"uint32", bcsv::ColumnType::UINT32}, {"ui32", bcsv::ColumnType::UINT32}, {"u32", bcsv::ColumnType::UINT32}, {"uint", bcsv::ColumnType::UINT32},
        {"uint64", bcsv::ColumnType::UINT64}, {"ui64", bcsv::ColumnType::UINT64}, {"u64", bcsv::ColumnType::UINT64}, {"ulong", bcsv::ColumnType::UINT64},
        {"float", bcsv::ColumnType::FLOAT},   {"f", bcsv::ColumnType::FLOAT}, {"f32", bcsv::ColumnType::FLOAT}, {"single", bcsv::ColumnType::FLOAT},
        {"double", bcsv::ColumnType::DOUBLE}, {"d", bcsv::ColumnType::DOUBLE}, {"f64", bcsv::ColumnType::DOUBLE},
        {"string", bcsv::ColumnType::STRING}, {"str", bcsv::ColumnType::STRING}, {"s", bcsv::ColumnType::STRING},
    };
    auto it = ALIASES.find(s);
    if (it != ALIASES.end())
        return it->second;

    throw std::runtime_error(
        "Unknown type '" + s + "'. Valid: bool int8 int16 int32 int64 "
        "uint8 uint16 uint32 uint64 float double string "
        "(aliases: i8..i64, ui8..ui64/u8..u64, b, ch/char/byte, f/f32, d/f64, "
        "str/s, int=int32, long=int64, uint=uint32, short, ushort, ulong)");
}

// ── Crash-safe output: temp sibling for atomic rename ─────────────

/// Create an empty temp file next to `target` (same directory, so the final
/// rename stays on one filesystem) and return its path. Callers write into the
/// temp file, then fs::rename() it over `target`; on failure they remove it.
inline std::filesystem::path makeTempSibling(const std::filesystem::path& target,
                                             const std::string&           prefix) {
    const std::filesystem::path dir  = target.parent_path();
    std::string                 tmpl = (dir / (prefix + "_XXXXXX")).string();
#ifdef BCSV_CLI_HAS_MKSTEMP
    int fd = mkstemp(tmpl.data());   // POSIX: creates + opens atomically
    if (fd == -1)
        throw std::runtime_error("Cannot create temp file in " +
                                 (dir.empty() ? std::string(".") : dir.string()));
    ::close(fd);  // writers overwrite via writer.open(..., true)
#else
    if (_mktemp_s(tmpl.data(), tmpl.size() + 1) != 0)  // Windows: name only (minor TOCTOU)
        throw std::runtime_error("Cannot create temp file name in " +
                                 (dir.empty() ? std::string(".") : dir.string()));
#endif
    return std::filesystem::path(tmpl);
}

} // namespace bcsv_cli

