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
#include <fstream>
#include <filesystem>
#include <optional>
#include "bcsv/definitions.h"
#include "layout.h"
#include "file_header.h"
#include "byte_buffer.h"
#include "lz4_stream.hpp"
#include "file_footer.h"
#include "checksum.hpp"

namespace bcsv {

    /**
     * @brief Class for reading BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class Reader {
    protected:
        using RowType           = typename LayoutType::RowType;
        using RowViewType       = typename LayoutType::RowViewType;
        using FilePath          = std::filesystem::path;

        FileHeader              fileHeader_;            // file header for accessing flags and metadata
        FilePath                filePath_;              // always present
        std::ifstream           stream_;                // always binary file stream
        std::optional< LZ4DecompressionStream< MAX_ROW_LENGTH> >
                                lz4Stream_;             // packet level (de)-compression facility (nullopt if compressionLevel == 0)

        // Current packet state
        Checksum::Streaming     packetHash_;            // stream to validate payload using a checksum chain
        std::streampos          packetPos_;             // position of the first byte of the current packet header in the file (PacketHeader MAGIC)

        // Global row tracking
        ByteBuffer              rowBuffer_;             // current row, encoded, decompressed raw data
        size_t                  rowPos_;                // postion of current row in file (0-based row counter)
        RowType                 row_;                   // current row, decoded

    public:
        /**
         * @brief Construct a Reader with a given layout
         * @param None
         */
                                Reader();
                                ~Reader();

        void                    close();
        uint8_t                 compressionLevel() const    { return fileHeader_.getCompressionLevel(); }
        const FilePath&         filePath() const            { return filePath_; }
        const LayoutType&       layout() const              { return row_.layout(); }
        
        bool                    isOpen() const              { return stream_.is_open(); }
        bool                    open(const FilePath& filepath);
        bool                    readNext();
        const RowType&          row() const                 { return row_; }
        size_t                  rowPos() const              { return rowPos_; } // 0-based row index in file
        
    protected:
        void                    closePacket();
        void                    openPacket();
        bool                    readFileHeader();
        void                    readRowLength(size_t& rowLength, std::istream& stream, Checksum::Streaming* checksum = nullptr);
    };

    /**
     * @brief Class for direct access reading of BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class ReaderDirectAccess : public Reader<LayoutType> {
    protected:
        using Base              = Reader<LayoutType>;
        using RowType           = typename LayoutType::RowType;
        using RowViewType       = typename LayoutType::RowViewType;
        using FilePath          = std::filesystem::path;

        FileFooter  fileFooter_;

        //ToDo: Develop a caching strategy to improve performance in Direct Access Mode (balance with memory requirement)
        //For now consider: piece wise sequential read as the targeted option. Still a file access. Load to RAW for fully random access, using your own use-case optimizes structures. 
        //pkt cache --> keep a set of packets open
        //row cache --> keep a set of rows open

    public:
        void    close();
        bool    open(const FilePath& filepath, bool rebuildFooter = false);
        bool    read(size_t index);
        size_t  rowCount()const             { return fileFooter_.rowCount(); }
        const FileFooter& fileFooter()const { return fileFooter_; }

    protected:
        void    buildFileFooter();
    };

    

} // namespace bcsv

    
