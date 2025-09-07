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
     * @brief Class for reading BCSV binary files
     */
    template<LayoutConcept LayoutType>
    class Reader {
        std::shared_ptr<LayoutType> layout_;
        ByteBuffer buffer_raw_;
        ByteBuffer buffer_zip_;
        std::vector<uint16_t> row_offsets_;     // Row offsets for indexing
        size_t row_index_file_   = 0;           // current row index within the file
        size_t row_index_packet_ = 0;           // current row index within the packet

        std::ifstream stream_;                  // Always binary file stream
        std::filesystem::path filePath_;        // Always present
        

    public:
        explicit Reader(std::shared_ptr<LayoutType> &layout);
        explicit Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath);
        ~Reader();

        void close();
        const std::filesystem::path& getFilePath() const { return filePath_; }
        bool is_open() const {  return stream_.is_open(); }
        bool open(const std::filesystem::path& filepath);

        size_t readRow(LayoutType::RowViewType& row);

    private:
        bool readFileHeader();
        bool readPacket();

    public:
        // Factory functions
        static std::shared_ptr<Reader<LayoutType>> create(std::shared_ptr<LayoutType> &layout) {
            return std::make_shared<Reader<LayoutType>>(layout);
        }

        static std::shared_ptr<Reader<LayoutType>> create(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath) {
            return std::make_shared<Reader<LayoutType>>(layout, filepath);
        }
    };

} // namespace bcsv
