#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

#include <lz4.h>

#include "definitions.h"
#include "layout.h"
#include "file_header.h"
#include "row.h"
#include "byte_buffer.h"

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
        using RowType = typename LayoutType::RowType;
        using RowViewType = typename LayoutType::RowViewType;

        ReaderMode              mode_ = ReaderMode::STRICT; // Default to strict mode for backward compatibility
        FileHeader              fileHeader_;                // File header for accessing flags and metadata
        std::filesystem::path   filePath_;                  // Always present
        std::ifstream           stream_;                    // Always binary file stream

        ByteBuffer              buffer_raw_;
        ByteBuffer              buffer_zip_;
        std::vector<uint16_t>   row_offsets_;                // Row offsets for indexing
        size_t                  row_index_file_   = 0;       // current row index within the file
        size_t                  row_index_packet_ = 0;       // current row index within the packet
        RowType                 row_;                        // current row
        size_t                  packet_row_count_ = 0;       // number of rows in the current packet

    public:
        /**
         * @brief Construct a Reader with a given layout
         * @param layout The layout defining the structure of rows
         * @param mode The reader mode for error handling (default: STRICT)
         */
        explicit                            Reader(ReaderMode mode = ReaderMode::STRICT);
                                            ~Reader();

        void                                close();
        uint8_t                             getCompressionLevel() const { return fileHeader_.getCompressionLevel(); }
        const std::filesystem::path&        getFilePath() const { return filePath_; }
        const LayoutType&                   getLayout() const { return row_.getLayout(); }
        ReaderMode                          getMode() const { return mode_; }
        size_t                              getCurrentRowIndex() const { return row_index_file_; }
        
        bool                                isOpen() const {  return stream_.is_open(); }
        bool                                open(const std::filesystem::path& filepath);

        bool                                readNext();
        const RowType&                      row() const { return row_; }
        void                                setMode(ReaderMode mode) { mode_ = mode; }

    private:
        bool                                readFileHeader();
        bool                                readPacket();        
    };

} // namespace bcsv
