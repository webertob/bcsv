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
 * @file csv_reader.h
 * @brief CsvReader — read CSV text files using the BCSV Layout/Row data model.
 *
 * CsvReader<LayoutType> satisfies ReaderConcept and provides a text-based
 * counterpart to the binary Reader<LayoutType>.  It shares the same Layout
 * and Row types but uses CSV text parsing instead of binary codec pipelines.
 *
 * Design:
 *   - State-machine CSV line splitter (handles quoted fields, embedded delimiters)
 *   - std::from_chars() for all numeric types (no locale, no virtual dispatch)
 *   - Configurable delimiter (default ',') and decimal separator (default '.')
 *   - Auto-reads the header line on open() and validates against the Layout
 *   - Preserves leading/trailing whitespace in string fields (Item 12.b)
 *   - Header-only implementation (csv_reader.hpp)
 *
 * Usage:
 *     bcsv::Layout layout;
 *     layout.addColumn("time", bcsv::ColumnType::DOUBLE);
 *     layout.addColumn("value", bcsv::ColumnType::FLOAT);
 *
 *     bcsv::CsvReader<bcsv::Layout> reader(layout);
 *     reader.open("input.csv");
 *     while (reader.readNext()) {
 *         double t = reader.row().get<double>(0);
 *         float  v = reader.row().get<float>(1);
 *     }
 *     reader.close();
 */

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "definitions.h"
#include "layout.h"
#include "row.h"

namespace bcsv {

    /**
     * @brief Class for reading CSV text files using the BCSV data model
     */
    template<LayoutConcept LayoutType>
    class CsvReader {
    public:
        using RowType           = typename LayoutType::RowType;
        using FilePath          = std::filesystem::path;

    private:
        std::string             err_msg_;            // last error message description
        FilePath                file_path_;           // path to the input file
        std::ifstream           stream_;              // text input stream

        RowType                 row_;                 // current row, parsed data
        size_t                  row_pos_ = 0;         // 0-based row index (data rows, not header)
        size_t                  file_line_ = 0;       // 1-based raw line counter (including header, blanks)

        char                    delimiter_ = ',';     // field delimiter
        char                    decimal_sep_ = '.';   // decimal separator for float/double

        // Reusable buffers to minimize allocations
        std::string             line_buf_;            // current line being parsed
        std::vector<std::string_view> cells_;         // split cells (views into line_buf_)
        std::string             str_buf_;             // temporary string buffer for unquoting

    public:
        CsvReader() = delete;
        explicit CsvReader(const LayoutType& layout, char delimiter = ',', char decimalSep = '.');
        ~CsvReader();

        void                    close();
        const std::string&      getErrorMsg() const             { return err_msg_; }
        const FilePath&         filePath() const                { return file_path_; }
        const LayoutType&       layout() const                  { return row_.layout(); }
        bool                    isOpen() const                  { return stream_.is_open(); }
        bool                    open(const FilePath& filepath, bool hasHeader = true);
        bool                    readNext();
        const RowType&          row() const                     { return row_; }
        size_t                  rowPos() const                  { return row_pos_; }
        size_t                  fileLine() const                { return file_line_; }

        // CSV-specific accessors
        char                    delimiter() const               { return delimiter_; }
        char                    decimalSeparator() const        { return decimal_sep_; }

    private:
        bool                    readHeader();
        void                    splitLine(const std::string& line);
        bool                    parseCells();
        std::string             unquote(std::string_view cell);
    };

} // namespace bcsv
