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
 * @file csv_writer.h
 * @brief CsvWriter — write CSV text files using the BCSV Layout/Row data model.
 *
 * CsvWriter<LayoutType> satisfies WriterConcept and provides a text-based
 * counterpart to the binary Writer<LayoutType, RowCodec>.  It shares the same
 * Layout and Row types but uses RFC 4180 CSV serialization instead of the
 * binary codec pipeline.
 *
 * Design:
 *   - Uses Row::visitConst() + std::to_chars() for zero-overhead numeric formatting
 *   - Single os.write() per row (buffered in a reusable char vector)
 *   - Configurable delimiter (default ',') and decimal separator (default '.')
 *   - RFC 4180 quoting for strings containing delimiter, quotes, or newlines
 *   - Header-only implementation (csv_writer.hpp)
 *
 * Usage:
 *     bcsv::Layout layout;
 *     layout.addColumn("time", bcsv::ColumnType::DOUBLE);
 *     layout.addColumn("value", bcsv::ColumnType::FLOAT);
 *
 *     bcsv::CsvWriter<bcsv::Layout> writer(layout);
 *     writer.open("output.csv", true);
 *     writer.row().set(0, 1.0);
 *     writer.row().set(1, 3.14f);
 *     writer.writeRow();
 *     writer.close();
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
     * @brief Class for writing CSV text files using the BCSV data model
     */
    template<LayoutConcept LayoutType>
    class CsvWriter {
    public:
        using RowType           = typename LayoutType::RowType;
        using FilePath          = std::filesystem::path;

    private:
        std::string             err_msg_;           // last error message description
        FilePath                file_path_;          // path to the output file
        std::ofstream           stream_;             // text output stream

        RowType                 row_;                // current row (user fills, writeRow() serializes)
        uint64_t                row_cnt_ = 0;        // total rows written

        char                    delimiter_ = ',';    // field delimiter
        char                    decimal_sep_ = '.';  // decimal separator for float/double

        std::vector<char>       buf_;                // reusable per-row serialization buffer

    public:
        CsvWriter() = delete;
        explicit CsvWriter(const LayoutType& layout, char delimiter = ',', char decimalSep = '.');
        ~CsvWriter();

        void                    close();
        const std::string&      getErrorMsg() const             { return err_msg_; }
        const FilePath&         filePath() const                { return file_path_; }
        const LayoutType&       layout() const                  { return row_.layout(); }
        bool                    isOpen() const                  { return stream_.is_open(); }
        bool                    open(const FilePath& filepath, bool overwrite = false, bool includeHeader = true);
        RowType&                row()                           { return row_; }
        const RowType&          row() const                     { return row_; }
        size_t                  rowCount() const                { return row_cnt_; }
        void                    write(const RowType& row);
        void                    writeRow();

        // CSV-specific accessors
        char                    delimiter() const               { return delimiter_; }
        char                    decimalSeparator() const        { return decimal_sep_; }

    private:
        void                    writeHeader();

        template<typename T>
        void                    appendToChars(T value);

        template<typename T>
        void                    appendFloat(T value);

        void                    appendString(const std::string& value);
    };

} // namespace bcsv
