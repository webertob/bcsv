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
 * @file writer_concept.h
 * @brief WriterConcept — C++20 concept defining the common Writer API.
 *
 * Both bcsv::Writer<LayoutType, RowCodec> (binary) and bcsv::CsvWriter<LayoutType>
 * (text) satisfy this concept, enabling generic algorithms over any writer type:
 *
 *     template<bcsv::WriterConcept W>
 *     void writeData(W& writer, const std::vector<typename W::RowType>& rows) {
 *         for (const auto& r : rows) writer.write(r);
 *     }
 *
 * Note: open() is intentionally excluded — its parameters are format-specific
 * (binary Writer needs compression/blockSize/flags; CsvWriter needs delimiter/decimal).
 * The concept covers the common operational surface: write, writeRow, row, close, etc.
 */

#include <concepts>
#include <cstddef>
#include <string>

namespace bcsv {

    template<typename W>
    concept WriterConcept = requires(W writer, const W& const_writer, const typename W::RowType& row) {
        // Row access
        { writer.row()              }   -> std::same_as<typename W::RowType&>;
        { const_writer.row()        }   -> std::same_as<const typename W::RowType&>;

        // Writing
        { writer.writeRow()         };
        { writer.write(row)         };

        // Lifecycle
        { writer.close()            };
        { const_writer.isOpen()     }   -> std::convertible_to<bool>;

        // Diagnostics
        { const_writer.getErrorMsg()}   -> std::convertible_to<const std::string&>;
        { const_writer.rowCount()   }   -> std::convertible_to<size_t>;

        // Layout access
        { const_writer.layout()     };
    };

} // namespace bcsv
