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
        LayoutType              layout_;
        FileHeader              fileHeader_;                // File header for accessing flags and metadata
        std::filesystem::path   filePath_;                  // Always present
        std::ofstream           stream_;                    // Always binary file stream

        ByteBuffer              buffer_raw_;
        ByteBuffer              buffer_zip_;
        std::vector<uint16_t>   row_offsets_;               // Row offsets for indexing
        size_t                  row_cnt_ = 0;
    
    public:
        LayoutType::RowType     row;

    public:
        Writer() = delete;
        Writer(const LayoutType& layout);
        ~Writer();

        void                            close();
        void                            flush();
        uint8_t                         getCompressionLevel() const     { return fileHeader_.getCompressionLevel(); }
        const std::filesystem::path&    getFilePath() const             { return filePath_; }
        bool                            is_open() const                 { return stream_.is_open(); }
        bool                            open(const std::filesystem::path& filepath, bool overwrite = false, uint8_t compressionLevel = 1);
        bool                            writeRow();

    private:
        void                            writePacket();
    };

} // namespace bcsv
