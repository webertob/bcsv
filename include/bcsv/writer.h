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
#include "row.h"
#include "file_header.h"
#include "file_footer.h"
#include "lz4_stream.hpp"
#include "checksum.hpp"

namespace bcsv {

    /**
     * @brief Class for writing BCSV binary files
     */
    template<LayoutConcept LayoutType, TrackingPolicy Policy = TrackingPolicy::Disabled>
    class Writer {
        using RowType           = typename LayoutType::template RowType<Policy>;
        using FilePath          = std::filesystem::path;

        std::string             err_msg_;                    // last error message description
        FileHeader              file_header_;                // File header for accessing flags and metadata
        FilePath                file_path_;                  // Always present
        std::ofstream           stream_;                    // Always binary file stream
        std::optional<LZ4CompressionStreamInternalBuffer<MAX_ROW_LENGTH>> 
                                lz4_stream_;                 // std::nullopt if compressionLevel == 0
        
        // Packet management
        PacketIndex             packet_index_;               // Builds index in memory (if NO_FILE_INDEX not set)
        Checksum::Streaming     packet_hash_;                // Streaming payload checksum for current packet
        bool                    packet_open_ = false;        // Whether a packet has been started
        size_t                  packet_size_;                // Bytes written in current packet payload
        
        // Buffers for streaming compression (pre-allocated, reused)
        ByteBuffer              row_buffer_raw_;              // Serialized raw row
        ByteBuffer              row_buffer_prev_;             // Previous row for ZoH comparison
        uint64_t                row_cnt_;                    // Total rows written across all packets
        RowType                 row_;
    public:
        Writer() = delete;
        Writer(const LayoutType& layout);
        ~Writer();

        void                    close();
        void                    flush();
        uint8_t                 compressionLevel() const        { return file_header_.getCompressionLevel(); }
        const std::string&      getErrorMsg() const             { return err_msg_; }
        const FilePath&         filePath() const                { return file_path_; }
        const LayoutType&       layout() const                  { return row_.layout(); }
        bool                    isOpen() const                  { return stream_.is_open(); }
        bool                    open(const FilePath& filepath, bool overwrite = false, size_t compressionLevel = 1, size_t blockSizeKB = 64, FileFlags flags = FileFlags::NONE);
        RowType&                row()                           { return row_; }
        const RowType&          row() const                     { return row_; }
        size_t                  rowCount() const                { return row_cnt_; }
        void                    writeRow();

    private:
        void                    closePacket();              
        void                    openPacket();         
        bool                    isZoHRepeat();
        void                    writeRowLength(size_t length);
    };

    template<LayoutConcept LayoutType>
    using WriterZoH = Writer<LayoutType, TrackingPolicy::Enabled>;

} // namespace bcsv
