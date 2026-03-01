#pragma once

/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
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
#include "row_codec_delta002.h"
#include "file_header.h"
#include "file_codec_dispatch.h"

namespace bcsv {

    // ── Codec → FileFlags mapping ────────────────────────────────────────
    // Writer owns the contract between row-codec type and required file
    // header flags.  Codecs themselves are wire-format only and have no
    // knowledge of FileFlags — this keeps the layers cleanly separated.
    // Reader auto-detects the codec from the file header flags it reads.

    /// Default: no extra flags required (covers RowCodecFlat001 and any
    /// future codec that does not need a dedicated flag).
    template<typename CodecType>
    struct RowCodecFileFlags {
        static constexpr FileFlags value = FileFlags::NONE;
    };

    /// ZoH codec requires the ZERO_ORDER_HOLD flag in the file header.
    template<typename LayoutType>
    struct RowCodecFileFlags<RowCodecZoH001<LayoutType>> {
        static constexpr FileFlags value = FileFlags::ZERO_ORDER_HOLD;
    };

    /// Delta codec requires the DELTA_ENCODING flag in the file header.
    template<typename LayoutType>
    struct RowCodecFileFlags<RowCodecDelta002<LayoutType>> {
        static constexpr FileFlags value = FileFlags::DELTA_ENCODING;
    };

    /**
     * @brief Class for writing BCSV binary files
     */
    template<
        LayoutConcept LayoutType,
        typename RowCodec = RowCodecFlat001<LayoutType>
    >
    class Writer {
    public:
        using RowType           = typename LayoutType::RowType;

    private:
        using FilePath          = std::filesystem::path;

        std::string             err_msg_;                    // last error message description
        FileHeader              file_header_;                // File header for accessing flags and metadata
        FilePath                file_path_;                  // Always present
        std::ofstream           stream_;                     // Always binary file stream

        // File-level codec (framing, compression, checksums, packet lifecycle)
        FileCodecDispatch       file_codec_;                 // Runtime-selected file codec

        RowCodec                row_codec_;                  // Compile-time selected row codec
        uint64_t                row_cnt_ = 0;                // Total rows written across all packets
        RowType                 row_;
        

    public:
        Writer() = delete;
        Writer(const LayoutType& layout);
        ~Writer();

        void                    close();

        /// @brief Flush all buffered data to disk in a crash-recoverable state.
        /// @note For packet-based codecs, this closes the current packet
        ///       (writes terminator + checksum), flushes the OS stream, then
        ///       opens a new packet for subsequent writes.  The row codec is
        ///       reset at the packet boundary (ZoH/Delta restart cleanly).
        ///       For stream codecs, this flushes the OS stream buffer only.
        ///       After flush(), all previously written rows are recoverable
        ///       by a Reader even if the process crashes.
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

    template<LayoutConcept LayoutType>
    using WriterDelta = Writer<LayoutType, RowCodecDelta002<LayoutType>>;

} // namespace bcsv
