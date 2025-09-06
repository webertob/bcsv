#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>

#include <lz4.h>

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
        std::shared_ptr<LayoutType> layout_;
        ByteBuffer buffer_raw_;
        ByteBuffer buffer_zip_;
        std::vector<uint16_t> row_offsets_;     // Row offsets for indexing
        size_t row_cnt_ = 0;
        std::ofstream stream_;                  // Always binary file stream
        std::filesystem::path filePath_;        // Always present

    public:
        Writer(std::shared_ptr<LayoutType> layout);
        Writer(std::shared_ptr<LayoutType> layout, const std::filesystem::path& filepath, bool overwrite = false);
        ~Writer();

        void close();
        void flush();
        const std::filesystem::path& getFilePath() const { return filePath_; }
        bool is_open() const { return stream_.is_open(); }
        bool open(const std::filesystem::path& filepath, bool overwrite = false);

        void writeRow(const typename LayoutType::RowType& row);
        void writeRow(const std::shared_ptr<typename LayoutType::RowType>& row) { if(auto *ptr = row.get()) { writeRow(*ptr); } }

    private:
        void writeFileHeader();
        void writePacket();

    public:
        // Factory functions
        static std::shared_ptr<Writer> create(std::shared_ptr<LayoutType> &layout) {
            return std::make_shared<Writer>(layout);
        }

        static std::shared_ptr<Writer> create(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, bool overwrite = false) {
            return std::make_shared<Writer>(layout, filepath, overwrite);
        }
    };

} // namespace bcsv
