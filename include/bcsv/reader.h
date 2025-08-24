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

namespace bcsv {

    /**
     * @brief Class for reading BCSV binary files
     */
    template<typename LayoutType>
    class Reader {
        std::shared_ptr<LayoutType> layout_;
        std::vector<char> buffer_raw_;
        std::vector<char> buffer_zip_;

        std::ifstream stream_;                  // Always binary file stream
        std::filesystem::path filePath_;        // Always present
        size_t currentRowIndex_ = 0;

    public:
        explicit Reader(std::shared_ptr<LayoutType> &layout);
        explicit Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath);
        ~Reader();

        void close();
        const std::filesystem::path& getFilePath() const { return filePath_; }
        bool is_open() const {  return stream_.is_open(); }
        bool open(const std::filesystem::path& filepath);
        
        bool readRow(Row& row);
        bool seekToRow(size_t rowIndex);
        size_t getRowCount() const;
        
    private:
        bool readHeader();

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
