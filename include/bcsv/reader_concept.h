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
 * @file reader_concept.h
 * @brief ReaderConcept — C++20 concept defining the common Reader API.
 *
 * Both bcsv::Reader<LayoutType> (binary) and bcsv::CsvReader<LayoutType> (text)
 * satisfy this concept, enabling generic algorithms over any reader type:
 *
 *     template<bcsv::ReaderConcept R>
 *     size_t countRows(R& reader, const std::filesystem::path& path) {
 *         reader.open(path);
 *         size_t n = 0;
 *         while (reader.readNext()) ++n;
 *         reader.close();
 *         return n;
 *     }
 *
 * Note: open() is intentionally excluded — its parameters are format-specific
 * (binary Reader needs no extra args; CsvReader needs delimiter/decimal config).
 * The concept covers the common operational surface: readNext, row, close, etc.
 */

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <string>

namespace bcsv {

    template<typename R>
    concept ReaderConcept = requires(R reader, const R& const_reader) {
        // Row access
        { const_reader.row()        }   -> std::same_as<const typename R::RowType&>;

        // Iteration
        { reader.readNext()         }   -> std::convertible_to<bool>;

        // Lifecycle
        { reader.close()            };
        { const_reader.isOpen()     }   -> std::convertible_to<bool>;

        // Diagnostics
        { const_reader.getErrorMsg()}   -> std::convertible_to<const std::string&>;
        { const_reader.filePath()   }   -> std::convertible_to<std::filesystem::path>;
        { const_reader.rowPos()     }   -> std::convertible_to<size_t>;

        // Layout access
        { const_reader.layout()     };
    };

} // namespace bcsv
