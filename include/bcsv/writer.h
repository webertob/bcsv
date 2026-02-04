#pragma once

/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */
#include <cstddef>
#include <fstream>
#include <filesystem>
#include <optional>

#include "definitions.h"
#include "byte_buffer.h"
#include "layout.h"
#include "file_header.h"
#include "file_footer.h"
#include "lz4_stream.hpp"
#include "checksum.hpp"

namespace bcsv {

    /**
     * @brief Class for writing BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class Writer {
        using RowType           = typename LayoutType::RowType;
        using RowViewType       = typename LayoutType::RowViewType;
        using FilePath          = std::filesystem::path;

        std::string             errMsg_;                    // last error message description
        FileHeader              fileHeader_;                // File header for accessing flags and metadata
        FilePath                filePath_;                  // Always present
        std::ofstream           stream_;                    // Always binary file stream
        std::optional<LZ4CompressionStreamInternalBuffer<MAX_ROW_LENGTH>> 
                                lz4Stream_;                 // std::nullopt if compressionLevel == 0
        
        // Packet management
        PacketIndex             packetIndex_;               // Builds index in memory (if NO_FILE_INDEX not set)
        Checksum::Streaming     packetHash_;                // Streaming payload checksum for current packet
        bool                    packetOpen_ = false;        // Whether a packet has been started
        size_t                  packetSize_;                // Bytes written in current packet payload
        
        // Buffers for streaming compression (pre-allocated, reused)
        ByteBuffer              rowBufferRaw_;              // Serialized raw row
        ByteBuffer              rowBufferPrev_;             // Previous row for ZoH comparison
        uint64_t                rowCnt_;                    // Total rows written across all packets
        RowType                 row_;
    public:
        Writer() = delete;
        Writer(const LayoutType& layout);
        ~Writer();

        void                    close();
        void                    flush();
        uint8_t                 compressionLevel() const        { return fileHeader_.getCompressionLevel(); }
        const std::string&      getErrorMsg() const             { return errMsg_; }
        const FilePath&         filePath() const                { return filePath_; }
        const LayoutType&       layout() const                  { return row_.layout(); }
        bool                    is_open() const                 { return stream_.is_open(); }
        bool                    open(const FilePath& filepath, bool overwrite = false, size_t compressionLevel = 1, size_t blockSizeKB = 64, FileFlags flags = FileFlags::NONE);
        RowType&                row()                           { return row_; }
        const RowType&          row() const                     { return row_; }
        size_t                  rowCount() const                { return rowCnt_; }
        void                    writeRow();

    private:
        void                    closePacket();              
        void                    openPacket();         
        bool                    isZoHRepeat();
        void                    writeRowLength(size_t length);
    };

} // namespace bcsv
