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
 * @file csv_writer.hpp
 * @brief CsvWriter template implementations.
 */

#include "csv_writer.h"
#include "row.hpp"
#include <cassert>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace bcsv {

    // ── Constructor / Destructor ────────────────────────────────────────

    template<LayoutConcept LayoutType>
    CsvWriter<LayoutType>::CsvWriter(const LayoutType& layout, char delimiter, char decimalSep)
        : row_(layout)
        , delimiter_(delimiter)
        , decimal_sep_(decimalSep)
    {
        buf_.reserve(4096);
    }

    template<LayoutConcept LayoutType>
    CsvWriter<LayoutType>::~CsvWriter() {
        if (isOpen()) {
            try {
                // Only close the owned stream; don't flush external ostreams in dtor
                if (stream_.is_open()) {
                    stream_.flush();
                    stream_.close();
                }
                os_ = nullptr;
            } catch (...) {
                // Suppress exceptions during destruction
            }
        }
    }

    // ── Lifecycle ───────────────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    void CsvWriter<LayoutType>::close() {
        if (os_ == nullptr) {
            return;
        }
        os_->flush();
        if (stream_.is_open()) {
            stream_.close();
        }
        os_ = nullptr;
        file_path_.clear();
        row_cnt_ = 0;
    }

    template<LayoutConcept LayoutType>
    bool CsvWriter<LayoutType>::open(const FilePath& filepath, bool overwrite, bool includeHeader) {
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

            // Create parent directory if needed
            FilePath parentDir = absolutePath.parent_path();
            if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
                std::error_code ec;
                if (!std::filesystem::create_directories(parentDir, ec)) {
                    err_msg_ = "Error: Cannot create directory: " + parentDir.string() +
                              " (Error: " + ec.message() + ")";
                    throw std::runtime_error(err_msg_);
                }
            }

            // Check if file already exists
            if (std::filesystem::exists(absolutePath)) {
                if (!overwrite) {
                    err_msg_ = "Warning: File already exists: " + absolutePath.string() +
                              ". Use overwrite=true to replace it.";
                    throw std::runtime_error(err_msg_);
                }
            }

            // Check write permissions
            std::error_code ec;
            auto perms = std::filesystem::status(parentDir, ec).permissions();
            if (ec || (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
                err_msg_ = "Error: No write permission for directory: " + parentDir.string();
                throw std::runtime_error(err_msg_);
            }

            // Open text file (not binary — let OS handle line endings)
            stream_.open(absolutePath, std::ios::out | std::ios::trunc);
            if (!stream_.good()) {
                err_msg_ = "Error: Cannot open file for writing: " + absolutePath.string();
                throw std::runtime_error(err_msg_);
            }

            os_ = &stream_;
            file_path_ = absolutePath;
            row_cnt_ = 0;
            row_.clear();

            // Write CSV header line (column names)
            if (includeHeader) {
                writeHeader();
            }

            return true;

        } catch (const std::filesystem::filesystem_error& ex) {
            if (err_msg_.empty()) {
                err_msg_ = std::string("Filesystem error: ") + ex.what();
            }
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        } catch (const std::exception& ex) {
            if (err_msg_.empty()) {
                err_msg_ = std::string("Error opening file: ") + ex.what();
            }
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        }
    }

    // ── Stream open (stdout / external ostream) ────────────────────────

    template<LayoutConcept LayoutType>
    bool CsvWriter<LayoutType>::open(std::ostream& os, bool includeHeader) {
        err_msg_.clear();

        if (isOpen()) {
            err_msg_ = "Warning: Writer is already open";
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        }

        os_ = &os;
        file_path_.clear();
        row_cnt_ = 0;
        row_.clear();

        if (includeHeader) {
            writeHeader();
        }

        return true;
    }

    // ── Writing ─────────────────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    void CsvWriter<LayoutType>::write(const RowType& row) {
        row_ = row;
        writeRow();
    }

    template<LayoutConcept LayoutType>
    void CsvWriter<LayoutType>::writeRow() {
        if (os_ == nullptr) {
            throw std::runtime_error("Error: Writer is not open");
        }

        buf_.clear();

        row_.visitConst([this](size_t index, const auto& value) {
            if (index > 0) buf_.push_back(delimiter_);

            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, bool>) {
                if (value) {
                    buf_.insert(buf_.end(), {'t','r','u','e'});
                } else {
                    buf_.insert(buf_.end(), {'f','a','l','s','e'});
                }
            } else if constexpr (std::is_same_v<T, int8_t>) {
                appendToChars(static_cast<int>(value));
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                appendToChars(static_cast<unsigned>(value));
            } else if constexpr (std::is_same_v<T, std::string>) {
                appendString(value);
            } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                appendFloat(value);
            } else {
                // int16..uint64: numeric types with no decimal separator concern
                appendToChars(value);
            }
        });

        buf_.push_back('\n');
        os_->write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
        row_cnt_++;
    }

    // ── Private helpers ─────────────────────────────────────────────────

    /// Write the CSV header line (column names, RFC 4180 quoting as needed)
    template<LayoutConcept LayoutType>
    void CsvWriter<LayoutType>::writeHeader() {
        buf_.clear();
        const auto& lay = layout();
        for (size_t i = 0; i < lay.columnCount(); ++i) {
            if (i > 0) buf_.push_back(delimiter_);
            appendString(std::string(lay.columnName(i)));
        }
        buf_.push_back('\n');
        os_->write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
    }

    /// Append any numeric type via std::to_chars (no locale, no virtual dispatch)
    template<LayoutConcept LayoutType>
    template<typename T>
    void CsvWriter<LayoutType>::appendToChars(T value) {
        constexpr size_t kMaxDigits = 32;
        size_t oldSize = buf_.size();
        buf_.resize(oldSize + kMaxDigits);
        auto [ptr, ec] = std::to_chars(buf_.data() + oldSize,
                                        buf_.data() + oldSize + kMaxDigits, value);
        buf_.resize(static_cast<size_t>(ptr - buf_.data()));
    }

    /// Append float/double with decimal separator replacement if configured
    template<LayoutConcept LayoutType>
    template<typename T>
    void CsvWriter<LayoutType>::appendFloat(T value) {
        constexpr size_t kMaxDigits = 32;
        size_t oldSize = buf_.size();
        buf_.resize(oldSize + kMaxDigits);
        auto [ptr, ec] = std::to_chars(buf_.data() + oldSize,
                                        buf_.data() + oldSize + kMaxDigits, value);
        size_t newSize = static_cast<size_t>(ptr - buf_.data());

        // Replace '.' with configured decimal separator if needed
        if (decimal_sep_ != '.') {
            for (size_t i = oldSize; i < newSize; ++i) {
                if (buf_[i] == '.') {
                    buf_[i] = decimal_sep_;
                    break; // only one decimal point per number
                }
            }
        }

        buf_.resize(newSize);
    }

    /// Append a string with RFC 4180 quoting if needed
    template<LayoutConcept LayoutType>
    void CsvWriter<LayoutType>::appendString(const std::string& value) {
        // Always quote strings to protect whitespace and special characters.
        // This ensures " string payload " is not truncated (Item 12.b requirement).
        buf_.push_back('"');
        for (char c : value) {
            if (c == '"') buf_.push_back('"'); // escape quotes by doubling
            buf_.push_back(c);
        }
        buf_.push_back('"');
    }

} // namespace bcsv
