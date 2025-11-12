/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstddef>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

#include <lz4.h>

#include "definitions.h"
#include "layout.h"
#include "file_header.h"
#include "row.h"
#include "byte_buffer.h"
#include "packet_header_v3.h"
#include "lz4_stream.hpp"
#include "file_footer.h"
#include "vle.hpp"
#include "checksum.hpp"

namespace bcsv {

    /**
     * @brief Reader operation modes for error handling
     */
    enum class ReaderMode {
        STRICT,     ///< Strict mode: Any validation error throws exception immediately
        RESILIENT   ///< Resilient mode: Attempt to recover from errors by finding next valid packet
    };

    /**
     * @brief Class for reading BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class Reader {
        using RowType           = typename LayoutType::RowType;
        using RowViewType       = typename LayoutType::RowViewType;
        using FilePath          = std::filesystem::path;

        ReaderMode              mode_ = ReaderMode::STRICT; // Default to strict mode for backward compatibility
        FileHeader              fileHeader_;                // File header for accessing flags and metadata
        FileFooter              fileFooter_;                // Parsed from EOF, or rebuilt if missing
        FilePath                filePath_;                  // Always present
        std::ifstream           stream_;                    // Always binary file stream
        std::optional<LZ4DecompressionStream> 
                                lz4Stream_;                 // Per-packet decompression (nullopt if compressionLevel == 0)
               
        // Current packet state
        Checksum::Streaming     packetHash_;                // Validates payload checksum chain
        ByteBuffer              bufferRawRow_;              // Current decompressed row (reused for ZoH repeat)
        std::streampos          currentPacketHeaderPos_;    // File position of current packet header
        size_t                  currentPacketIndex_;        // Index into fileIndex_ for current packet

        // Global row tracking
        size_t                  currentRowIndex_;           // 0-based absolute row index in file
        RowType                 row_;                       // Current row

    public:
        /**
         * @brief Construct a Reader with a given layout
         * @param mode The reader mode for error handling (default: STRICT)
         */
        explicit                Reader(ReaderMode mode = ReaderMode::STRICT);
                                ~Reader();

        void                    close();
        size_t                  countRows() const           { return fileFooter_.totalRowCount(); }
        uint8_t                 compressionLevel() const    { return fileHeader_.compressionLevel(); }
        const FilePath&         filePath() const            { return filePath_; }
        const LayoutType&       layout() const              { return row_.layout(); }
        ReaderMode              mode() const                { return mode_; }
        size_t                  rowIndex() const            { return currentRowIndex_; }

        bool                    isOpen() const              { return stream_.is_open(); }
        bool                    open(const FilePath& filepath);

        bool                    readNext();
        const RowType&          row() const                 { return row_; }
        void                    setMode(ReaderMode mode)    { mode_ = mode; }

    private:
        bool                    readFileHeader();
        bool                    readFileFooter();            // v1.3.0: Read FileFooter or footer from EOF
        bool                    rebuildFileFooter();         // v1.3.0: Rebuild index by scanning packets (RESILIENT mode)
        bool                    openPacket();               // v1.3.0: Open next packet for sequential reading
    };

} // namespace bcsv
