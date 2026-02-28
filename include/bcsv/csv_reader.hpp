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
#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace bcsv {

    // ── Constructor / Destructor ────────────────────────────────────────

    template<LayoutConcept LayoutType>
    CsvReader<LayoutType>::CsvReader(const LayoutType& layout, char delimiter, char decimalSep)
        : row_(layout)
        , delimiter_(delimiter)
        , decimal_sep_(decimalSep)
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
            // Read one complete CSV line (may span multiple raw lines if quoted fields contain newlines)
            line_buf_.clear();
            bool inQuotes = false;
            size_t rawLinesConsumed = 0;

            while (true) {
                std::string rawLine;
                if (!std::getline(stream_, rawLine)) {
                    // EOF or error
                    if (line_buf_.empty()) {
                        return false;  // clean EOF
                    }
                    // We had partial data — try to parse what we have
                    break;
                }
                rawLinesConsumed++;

                if (!line_buf_.empty()) {
                    line_buf_.push_back('\n'); // restore the newline that was inside a quoted field
                }
                line_buf_ += rawLine;

                // Count unescaped quotes to track whether we're inside a quoted field
                for (char c : rawLine) {
                    if (c == '"') inQuotes = !inQuotes;
                }

                if (!inQuotes) {
                    break; // complete line
                }
                // If still in quotes, the newline was inside a quoted field — continue reading
            }

            file_line_ += rawLinesConsumed;

            // Skip empty lines (iterative, no recursion)
            if (line_buf_.empty()) {
                continue;
            }

            // Strip trailing \r for Windows line endings
            if (!line_buf_.empty() && line_buf_.back() == '\r') {
                line_buf_.pop_back();
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

    // ── Private helpers ─────────────────────────────────────────────────

    /// Read the CSV header line and validate column count against layout
    template<LayoutConcept LayoutType>
    bool CsvReader<LayoutType>::readHeader() {
        std::string headerLine;
        if (!std::getline(stream_, headerLine)) {
            err_msg_ = "Error: CSV file is empty (no header line)";
            return false;
        }
        file_line_++;

        // Strip BOM if present (UTF-8 BOM: EF BB BF)
        if (headerLine.size() >= 3 &&
            static_cast<unsigned char>(headerLine[0]) == 0xEF &&
            static_cast<unsigned char>(headerLine[1]) == 0xBB &&
            static_cast<unsigned char>(headerLine[2]) == 0xBF) {
            headerLine.erase(0, 3);
        }

        // Strip trailing \r
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }

        // Split header into column names
        splitLine(headerLine);

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
                    int v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid INT8 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, static_cast<int8_t>(v));
                    break;
                }
                case ColumnType::INT16: {
                    int16_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid INT16 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::INT32: {
                    int32_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid INT32 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::INT64: {
                    int64_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid INT64 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT8: {
                    unsigned v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid UINT8 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, static_cast<uint8_t>(v));
                    break;
                }
                case ColumnType::UINT16: {
                    uint16_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid UINT16 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT32: {
                    uint32_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid UINT32 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::UINT64: {
                    uint64_t v = 0;
                    auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                    if (ec != std::errc{} && !trimmedCell.empty()) {
                        err_msg_ = "Warning: Invalid UINT64 value at file line " + std::to_string(file_line_) +
                                   ", column " + std::to_string(i);
                    }
                    row_.set(i, v);
                    break;
                }
                case ColumnType::FLOAT: {
                    float v = 0;
                    std::string cellStr;
                    if (decimal_sep_ != '.') {
                        // Replace configured decimal separator with '.' for from_chars
                        cellStr.assign(trimmedCell.data(), trimmedCell.size());
                        for (char& c : cellStr) {
                            if (c == decimal_sep_) { c = '.'; break; }
                        }
                        auto [ptr, ec] = std::from_chars(cellStr.data(), cellStr.data() + cellStr.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            err_msg_ = "Warning: Invalid FLOAT value at file line " + std::to_string(file_line_) +
                                       ", column " + std::to_string(i);
                        }
                    } else {
                        auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            err_msg_ = "Warning: Invalid FLOAT value at file line " + std::to_string(file_line_) +
                                       ", column " + std::to_string(i);
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
                        auto [ptr, ec] = std::from_chars(cellStr.data(), cellStr.data() + cellStr.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            err_msg_ = "Warning: Invalid DOUBLE value at file line " + std::to_string(file_line_) +
                                       ", column " + std::to_string(i);
                        }
                    } else {
                        auto [ptr, ec] = std::from_chars(trimmedCell.data(), trimmedCell.data() + trimmedCell.size(), v);
                        if (ec != std::errc{} && !trimmedCell.empty()) {
                            err_msg_ = "Warning: Invalid DOUBLE value at file line " + std::to_string(file_line_) +
                                       ", column " + std::to_string(i);
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
