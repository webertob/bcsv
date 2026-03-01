/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include "layout.h"
#include "row.h"
#include "row_codec_dispatch.h"
#include "file_header.h"
#include "file_codec_dispatch.h"
#include "file_footer.h"

namespace bcsv {

    /**
     * @brief Class for reading BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class Reader {
    public:
        using RowType           = typename LayoutType::RowType;

    protected:
        using FilePath          = std::filesystem::path;
        using RowCodeDisptch    = RowCodecDispatch<LayoutType>;

        std::string             err_msg_;                // last error message description

        FileHeader              file_header_;            // file header for accessing flags and metadata
        FilePath                file_path_;              // points to the input file
        std::ifstream           stream_;                // input file binary stream

        // File-level codec (framing, decompression, checksums, packet lifecycle)
        FileCodecDispatch       file_codec_;             // Runtime-selected file codec

        // Global row tracking
        RowCodeDisptch          row_codec_;      // Runtime codec dispatch (Item 11 Phase 7)
        size_t                  row_pos_;                // postion of current row in file (0-based row counter)
        RowType                 row_;                   // current row, decoded data

        
    public:
        /**
         * @brief Construct a Reader with a given layout
         * @param None
         */
                                Reader();
                                ~Reader();

        void                    close();
        uint8_t                 compressionLevel() const    { return file_header_.getCompressionLevel(); }
        const FilePath&         filePath() const            { return file_path_; }
        const LayoutType&       layout() const              { return row_.layout(); }
        const std::string&      getErrorMsg() const         { return err_msg_; }
        
        bool                    isOpen() const              { return stream_.is_open(); }
        bool                    open(const FilePath& filepath);
        bool                    readNext();
        const RowType&          row() const                 { return row_; }
        size_t                  rowPos() const              { return row_pos_; } // 0-based row index in file
        
    protected:
        bool                    readFileHeader();
    };

    /**
     * @brief Class for direct access reading of BCSV binary files
     *
     * Provides O(log P) random access to any row by index (P = number of packets).
     * Both compressed (LZ4) and uncompressed codecs cache the entire target
     * packet in memory.  Subsequent reads within the same packet are O(1)
     * vector-index lookups.  Cross-packet seeks load only the target packet.
     *
     * Optimized for piecewise-sequential access patterns (head, tail, slice).
     */
    template<LayoutConcept LayoutType>
    class ReaderDirectAccess : public Reader<LayoutType> {
    protected:
        using Base              = Reader<LayoutType>;
        using RowType           = typename LayoutType::RowType;
        using FilePath          = std::filesystem::path;
        using RowCodeDisptch    = RowCodecDispatch<LayoutType>;

        FileFooter  file_footer_;

        // ── Packet cache ────────────────────────────────────────────────
        // When a new packet is needed, the entire packet is read via the
        // file codec (which handles decompression transparently) into
        // cached_rows_.  Subsequent reads within the same packet are
        // O(1) vector-index lookups.

        size_t                          cached_packet_idx_{SIZE_MAX};  ///< Index into PacketIndex of cached packet (SIZE_MAX = none)
        size_t                          cached_first_row_{0};          ///< first_row of the cached packet
        size_t                          cached_row_count_{0};          ///< Number of rows in the cached packet
        std::vector<std::vector<std::byte>> cached_rows_;              ///< Raw row data per row

        // Row codec for direct-access deserialization (separate from sequential)
        RowCodeDisptch                  da_row_codec_;

    public:
        void    close();
        bool    open(const FilePath& filepath, bool rebuildFooter = false);
        bool    read(size_t index);
        size_t  rowCount()const             { return file_footer_.rowCount(); }
        const FileFooter& fileFooter()const { return file_footer_; }

    protected:
        void    buildFileFooter();
        bool    loadPacket(size_t packetIdx);
        bool    deserializeCachedRow(size_t rowInPacket, size_t index);
    };

    

} // namespace bcsv

    
