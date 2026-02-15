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
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <optional>
#include <string>
#include "definitions.h"
#include "layout.h"
#include "row.h"
#include "row_codec_dispatch.h"
#include "file_header.h"
#include "byte_buffer.h"
#include "lz4_stream.hpp"
#include "file_footer.h"
#include "checksum.hpp"

namespace bcsv {

    /**
     * @brief Class for reading BCSV binary files
     */
    template<LayoutConcept LayoutType, TrackingPolicy Policy = TrackingPolicy::Disabled>
    class Reader {
    protected:
        using RowType           = typename LayoutType::template RowType<Policy>;
        using FilePath          = std::filesystem::path;

        std::string             err_msg_;                // last error message description

        FileHeader              file_header_;            // file header for accessing flags and metadata
        FilePath                file_path_;              // points to the input file
        std::ifstream           stream_;                // input file binary stream
        std::optional< LZ4DecompressionStream< MAX_ROW_LENGTH> >
                                lz4_stream_;             // packet level (de)-compression facility (nullopt if compressionLevel == 0)

        // Current packet state
        Checksum::Streaming     packet_hash_;            // stream to validate payload using a checksum chain
        bool                    packet_open_{false};     // indicates if a packet is currently open for reading
        std::streampos          packet_pos_;             // position of the first byte of the current packet header in the file (PacketHeader MAGIC)

        // Global row tracking
        ByteBuffer              row_buffer_;             // current row, encoded data (decompressed)
        size_t                  row_pos_;                // postion of current row in file (0-based row counter)
        RowType                 row_;                   // current row, decoded data
        CodecDispatch<LayoutType, Policy> codec_;      // Runtime codec dispatch (Item 11 Phase 7)
        
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
        void                    closePacket();
        bool                    openPacket();
        bool                    readFileHeader();
    };

    template<LayoutConcept LayoutType>
    using ReaderZoH = Reader<LayoutType, TrackingPolicy::Enabled>;

    /**
     * @brief Class for direct access reading of BCSV binary files
     */
    template<LayoutConcept LayoutType, TrackingPolicy Policy = TrackingPolicy::Disabled>
    class ReaderDirectAccess : public Reader<LayoutType, Policy> {
    protected:
        using Base              = Reader<LayoutType, Policy>;
        using RowType           = typename LayoutType::template RowType<Policy>;
        using FilePath          = std::filesystem::path;

        FileFooter  file_footer_;

        //ToDo: Develop a caching strategy to improve performance in Direct Access Mode (balance with memory requirement)
        //For now consider: piece wise sequential read as the targeted option. Still a file access. Load to RAW for fully random access, using your own use-case optimizes structures. 
        //pkt cache --> keep a set of packets open
        //row cache --> keep a set of rows open

    public:
        void    close();
        bool    open(const FilePath& filepath, bool rebuildFooter = false);
        bool    read(size_t index);
        size_t  rowCount()const             { return file_footer_.rowCount(); }
        const FileFooter& fileFooter()const { return file_footer_; }

    protected:
        void    buildFileFooter();
    };

    

} // namespace bcsv

    
