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
 * @file csv_reader.hpp
 * @brief CsvReader template implementations.
 */

#include "csv_reader.h"
#include "row.hpp"
#include "std_charconv_compat.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace bcsv {

    // ── Constructor / Destructor ────────────────────────────────────────

    template<LayoutConcept LayoutType>
    CsvReader<LayoutType>::CsvReader(const LayoutType& layout, char delimiter, char decimalSep,
                                     bool collapseWhitespace)
        : row_(layout)
        , delimiter_(delimiter)
        , decimal_sep_(decimalSep)
        , collapse_whitespace_(collapseWhitespace)
    {
        line_buf_.reserve(4096);
        cells_.reserve(layout.columnCount());
    }

    template<LayoutConcept LayoutType>
    CsvReader<LayoutType>::~CsvReader() {
        if (isOpen()) {
            close();
        }
    }

    // ── Lifecycle ───────────────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    void CsvReader<LayoutType>::close() {
        if (!stream_.is_open()) {
            return;
        }
        stream_.close();
        file_path_.clear();
        row_pos_ = 0;
        file_line_ = 0;
        parse_error_count_ = 0;
        row_.clear();
    }

    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::open(const FilePath& filepath, bool hasHeader) {
        err_msg_.clear();

        if (isOpen()) {
            err_msg_ = "Warning: File is already open: " + file_path_.string();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        }

        try {
            FilePath absolutePath = std::filesystem::absolute(filepath);

            // Check if file exists
            if (!std::filesystem::exists(absolutePath)) {
                throw std::runtime_error("Error: File does not exist: " + absolutePath.string());
            }

            // Check if it's a regular file
            if (!std::filesystem::is_regular_file(absolutePath)) {
                throw std::runtime_error("Error: Path is not a regular file: " + absolutePath.string());
            }

            // Check read permissions
            std::error_code ec;
            auto perms = std::filesystem::status(absolutePath, ec).permissions();
            if (ec || (perms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) {
                throw std::runtime_error("Error: No read permission for file: " + absolutePath.string());
            }

            // Open text file
            stream_.open(absolutePath, std::ios::in);
            if (!stream_.is_open()) {
                throw std::runtime_error("Error: Cannot open file for reading: " + absolutePath.string());
            }

            file_path_ = absolutePath;
            row_pos_ = 0;
            file_line_ = 0;
            parse_error_count_ = 0;
            row_.clear();

            // Read and validate header line
            if (hasHeader) {
                if (!readHeader()) {
                    throw std::runtime_error("Failed to read CSV header: " + err_msg_);
                }
            }

            return true;

        } catch (const std::exception& ex) {
            err_msg_ = ex.what();
            if (stream_.is_open()) {
                stream_.close();
            }
            file_path_.clear();
            return false;
        }
    }

    // ── Reading ─────────────────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::readNext() {
        if (!isOpen()) {
            return false;
        }

        // Iterative loop — avoids stack overflow on many empty or malformed lines
        while (true) {
            if (!fetchRecord()) {
                return false;  // clean EOF
            }

            // Split and parse
            splitLine(line_buf_);

            if (!parseCells()) {
                // Parse error — skip this line and try next (iterative, no recursion)
                err_msg_ = "Warning: Failed to parse CSV line at file line " +
                           std::to_string(file_line_) + " (data row " + std::to_string(row_pos_) + ")";
                if constexpr (DEBUG_OUTPUTS) {
                    std::cerr << err_msg_ << std::endl;
                }
                continue;
            }

            row_pos_++;
            return true;
        }
    }

    /**
     * @brief Read the next logical record and split it into raw cells, without typed parsing.
     *
     * Same record assembly as readNext() (multi-line quoted fields, empty-line skip, LF/CRLF/CR
     * line ends, BOM strip at the start of the file) but the cells are left as raw string_views
     * (see rawCells()); the layout is not consulted for typing and row() is not updated.
     * Callers handle column-count mismatches and unquoting (see unquote()) themselves.
     *
     * @note One layout-derived behavior remains: splitLine() drops a single trailing empty
     *       cell when a record has exactly layout().columnCount()+1 fields (trailing-delimiter
     *       tolerance). Construct the reader with an EMPTY layout to get fully untouched
     *       tokenization in raw mode.
     */
    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::readNextRaw() {
        if (!isOpen() || !fetchRecord()) {
            return false;
        }
        splitLine(line_buf_);
        row_pos_++;
        return true;
    }

    // ── Private helpers ─────────────────────────────────────────────────

    /// Assemble one logical CSV record into line_buf_ (multi-line quoted fields,
    /// empty-line skip, UTF-8 BOM strip at the start of the file).
    /// A line ends at "\n", "\r\n" or a lone "\r" (classic Mac OS) — the same on every OS,
    /// whatever the stream's text-mode translation did. Outside quotes a line end ends the
    /// record and is dropped; inside a quoted field it is part of the value and kept as is.
    /// Returns false on clean EOF.
    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::fetchRecord() {
        using Traits = std::char_traits<char>;
        std::streambuf& in = *stream_.rdbuf();
        while (true) {
            const bool atFileStart = (file_line_ == 0);
            line_buf_.clear();
            bool inQuotes = false;
            bool lineOpen = false;   // characters read since the last line end

            int c;
            while ((c = in.sbumpc()) != Traits::eof()) {
                // A '\r' directly before '\n' is the first half of a CRLF: stored for
                // now (stripped below when it ends the record), the '\n' ends the line.
                const bool lineEnd = c == '\n' || (c == '\r' && in.sgetc() != '\n');
                if (lineEnd) {
                    ++file_line_;
                    lineOpen = false;
                    if (!inQuotes) {
                        break;
                    }
                } else {
                    lineOpen = true;
                    if (c == '"') {
                        inQuotes = !inQuotes;
                    }
                }
                line_buf_.push_back(static_cast<char>(c));
            }
            if (lineOpen) {
                ++file_line_;   // last line without a line end
            }
            if (c == Traits::eof() && line_buf_.empty()) {
                return false;   // clean EOF (an unterminated quote hands back what it has)
            }

            if (atFileStart && line_buf_.starts_with("\xEF\xBB\xBF")) {
                line_buf_.erase(0, 3);   // UTF-8 BOM (Excel "CSV UTF-8")
            }
            if (!line_buf_.empty() && line_buf_.back() == '\r') {
                line_buf_.pop_back();    // the CR of the CRLF that ended the record
            }
            if (line_buf_.empty()) {
                continue;                // skip empty lines (iterative, no recursion)
            }
            return true;
        }
    }

    /// Record a typed-cell parse failure: count it, keep a message only for the first one
    /// (avoids a per-bad-cell string allocation on pathological input).
    template<LayoutConcept LayoutType>
    void CsvReader<LayoutType>::noteParseError(const char* type_name, size_t column) {
        ++parse_error_count_;
        if (parse_error_count_ == 1) {
            err_msg_ = std::string("Warning: Invalid ") + type_name + " value at file line " +
                       std::to_string(file_line_) + ", column " + std::to_string(column);
        }
    }

    /// Read the CSV header record and validate column count against layout
    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::readHeader() {
        // Same record assembly as the data rows (line ends, BOM, quoted names).
        if (!fetchRecord()) {
            err_msg_ = "Error: CSV file is empty (no header line)";
            return false;
        }

        // Split header into column names
        splitLine(line_buf_);

        if (cells_.size() != layout().columnCount()) {
            err_msg_ = "Error: CSV header column count (" + std::to_string(cells_.size()) +
                       ") does not match layout column count (" + std::to_string(layout().columnCount()) + ")";
            return false;
        }

        // Optionally validate column names match (warning only, not a hard failure)
        for (size_t i = 0; i < cells_.size(); ++i) {
            std::string headerName = unquote(cells_[i]);
            std::string layoutName = std::string(layout().columnName(i));
            if (headerName != layoutName) {
                // Name mismatch is a warning, not an error
                // (user may have renamed columns or the CSV uses different naming)
                if constexpr (DEBUG_OUTPUTS) {
                    std::cerr << "Warning: CSV header column " << i << " name '"
                              << headerName << "' differs from layout name '"
                              << layoutName << "'" << std::endl;
                }
            }
        }

        return true;
    }

    /// Split a CSV line into cells, handling quoted fields
    template<LayoutConcept LayoutType>
    void CsvReader<LayoutType>::splitLine(const std::string& line) {
        cells_.clear();
        const char* data = line.data();
        size_t len = line.size();

        if (collapse_whitespace_) {
            // Whitespace-collapse mode: treat any run of spaces/tabs as a single
            // separator, skipping leading and trailing whitespace runs.
            size_t i = 0;
            while (i < len) {
                while (i < len && (data[i] == ' ' || data[i] == '\t')) ++i;
                if (i >= len) break;
                size_t start = i;
                bool inQuotes = false;
                while (i < len) {
                    char c = data[i];
                    if (c == '"') {
                        inQuotes = !inQuotes;
                    } else if (!inQuotes && (c == ' ' || c == '\t')) {
                        break;
                    }
                    ++i;
                }
                cells_.emplace_back(data + start, i - start);
            }
        } else {
            size_t start = 0;
            bool inQuotes = false;
            for (size_t i = 0; i <= len; ++i) {
                if (i == len || (!inQuotes && data[i] == delimiter_)) {
                    cells_.emplace_back(data + start, i - start);
                    start = i + 1;
                } else if (data[i] == '"') {
                    inQuotes = !inQuotes;
                }
            }
        }

        // Tolerate a single spurious trailing empty field produced by a trailing
        // delimiter (e.g. lines ending in a tab). Only drop it when it makes the
        // cell count match the layout, so legitimate trailing empty values are
        // preserved.
        if (cells_.size() == layout().columnCount() + 1 && cells_.back().empty()) {
            cells_.pop_back();
        }
    }

    /// Parse all cells into the row using the layout's type information
    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::parseCells() {
        const auto& lay = layout();

        if (cells_.size() != lay.columnCount()) {
            return false; // column count mismatch
        }

        for (size_t i = 0; i < cells_.size(); ++i) {
            std::string_view cell = cells_[i];

            // Strip leading/trailing whitespace for non-string types only
            // String types preserve whitespace (Item 12.b requirement)
            std::string_view trimmedCell = cell;
            if (lay.columnType(i) != ColumnType::STRING) {
                // Trim whitespace for numeric/bool parsing
                while (!trimmedCell.empty() && trimmedCell.front() == ' ') trimmedCell.remove_prefix(1);
                while (!trimmedCell.empty() && trimmedCell.back() == ' ') trimmedCell.remove_suffix(1);
            }

            switch (lay.columnType(i)) {
                case ColumnType::BOOL: {
                    bool val = (trimmedCell == "true" || trimmedCell == "1" ||
                                trimmedCell == "TRUE" || trimmedCell == "True");
                    row_.set(i, val);
                    break;
                }
                case ColumnType::INT8: {
                    int8_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("INT8", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::INT16: {
                    int16_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("INT16", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::INT32: {
                    int32_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("INT32", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::INT64: {
                    int64_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("INT64", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT8: {
                    uint8_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("UINT8", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT16: {
                    uint16_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("UINT16", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT32: {
                    uint32_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("UINT32", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT64: {
                    uint64_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        noteParseError("UINT64", i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::FLOAT: {
                    float v = 0;
                    std::string cellStr;
                    if (decimal_sep_ != '.') {
                        cellStr.assign(trimmedCell.data(), trimmedCell.size());
                        for (char& c : cellStr) {
                            if (c == decimal_sep_) { c = '.'; break; }
                        }
                        auto [ptr, ec] = compat::from_chars(cellStr.data(), cellStr.data() + cellStr.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            noteParseError("FLOAT", i);
                        }
                    } else {
                        auto [ptr, ec] = compat::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            noteParseError("FLOAT", i);
                        }
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::DOUBLE: {
                    double v = 0;
                    std::string cellStr;
                    if (decimal_sep_ != '.') {
                        cellStr.assign(trimmedCell.data(), trimmedCell.size());
                        for (char& c : cellStr) {
                            if (c == decimal_sep_) { c = '.'; break; }
                        }
                        auto [ptr, ec] = compat::from_chars(cellStr.data(), cellStr.data() + cellStr.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            noteParseError("DOUBLE", i);
                        }
                    } else {
                        auto [ptr, ec] = compat::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            noteParseError("DOUBLE", i);
                        }
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::STRING: {
                    // Unquote but preserve all content including whitespace
                    str_buf_ = unquote(cell);
                    row_.set(i, str_buf_);
                    break;
                }
                default:
                    return false; // unknown type
            }
        }
        return true;
    }

    /// Unquote a CSV field: removes outer quotes and unescapes doubled quotes
    template<LayoutConcept LayoutType>
    std::string CsvReader<LayoutType>::unquote(std::string_view cell) {
        if (cell.size() >= 2 && cell.front() == '"' && cell.back() == '"') {
            std::string result(cell.data() + 1, cell.size() - 2);
            // Unescape doubled quotes
            size_t pos = 0;
            while ((pos = result.find("\"\"", pos)) != std::string::npos) {
                result.erase(pos, 1);
                pos += 1;
            }
            return result;
        }
        return std::string(cell);
    }

} // namespace bcsv
