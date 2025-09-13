#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

#include <lz4.h>
#include <lz4hc.h>

#include "definitions.h"
#include "byte_buffer.h"
#include "layout.h"
#include "file_header.h"
#include "row.h"

namespace bcsv {

    /**
     * @brief Class for writing BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class Writer {
        using RowType           = typename LayoutType::RowType;
        using RowViewType       = typename LayoutType::RowViewType;
        using FilePath          = std::filesystem::path;

        FileHeader              fileHeader_;                // File header for accessing flags and metadata
        FilePath                filePath_;                  // Always present
        std::ofstream           stream_;                    // Always binary file stream

        ByteBuffer              buffer_raw_;
        ByteBuffer              buffer_zip_;
        std::vector<uint16_t>   row_offsets_;               // Row offsets for indexing
        size_t                  row_cnt_ = 0;
        RowType                 row_;
 
    public:
        Writer() = delete;
        Writer(const LayoutType& layout);
        ~Writer();

        void                    close();
        void                    flush();
        uint8_t                 getCompressionLevel() const     { return fileHeader_.getCompressionLevel(); }
        const FilePath&         getFilePath() const             { return filePath_; }
        const LayoutType&       getLayout() const               { return row_.getLayout(); }
        bool                    is_open() const                 { return stream_.is_open(); }
        bool                    open(const FilePath& filepath, bool overwrite = false, uint8_t compressionLevel = 1);
        RowType&                row()                           { return row_; }
        const RowType&          row() const                     { return row_; }
        bool                    writeRow();

    private:
        void                    writePacket();
    };

} // namespace bcsv
