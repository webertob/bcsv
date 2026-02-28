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
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>

#include "definitions.h"
#include "layout.h"
#include "row.h"
#include "row_codec_dispatch.h"
#include "row_codec_flat001.h"
#include "row_codec_zoh001.h"
#include "file_header.h"
#include "file_codec_dispatch.h"

namespace bcsv {

    /**
     * @brief Class for writing BCSV binary files
     */
    template<
        LayoutConcept LayoutType,
        typename CodecType = RowCodecFlat001<LayoutType>
    >
    class Writer {
        using RowType           = typename LayoutType::RowType;
        using FilePath          = std::filesystem::path;
        using RowCodecDispatch  = RowCodecDispatch<LayoutType>;

        std::string             err_msg_;                    // last error message description
        FileHeader              file_header_;                // File header for accessing flags and metadata
        FilePath                file_path_;                  // Always present
        std::ofstream           stream_;                     // Always binary file stream

        // File-level codec (framing, compression, checksums, packet lifecycle)
        FileCodecDispatch       file_codec_;                 // Runtime-selected file codec

        CodecType               row_codec_;                      // Compile-time selected row codec
        uint64_t                row_cnt_ = 0;                // Total rows written across all packets
        RowType                 row_;
        

    public:
        Writer() = delete;
        Writer(const LayoutType& layout);
        ~Writer();

        void                    close();

        /// @brief Flush the underlying OS stream buffer to disk.
        /// @note This flushes the OS/stdio buffer only. It does NOT finalize the
        ///       current packet (no packet header, no checksum, no footer update).
        ///       Rows in an incomplete packet are not recoverable after a crash.
        ///       For crash-safe persistence, use close() which finalizes all packets
        ///       and writes the file footer / packet index.
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
        void                    write(const RowType& row);
        void                    writeRow();

    };

    template<LayoutConcept LayoutType>
    using WriterFlat = Writer<LayoutType, RowCodecFlat001<LayoutType>>;

    template<LayoutConcept LayoutType>
    using WriterZoH = Writer<LayoutType, RowCodecZoH001<LayoutType>>;

} // namespace bcsv
