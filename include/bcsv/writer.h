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
     * @brief Class for writing BCSV binary files
     */
    template<typename LayoutType>
    class Writer {
        std::shared_ptr<LayoutType> layout_;
        std::vector<char> buffer_raw_;
        std::vector<char> buffer_zip_;

        std::ofstream stream_;                  // Always binary file stream
        std::filesystem::path filePath_;        // Always present

        size_t currentRowIndex_ = 0;
        bool headerWritten_ = false;
    
    public:
        explicit Writer(std::shared_ptr<LayoutType> &layout);
        explicit Writer(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, bool overwrite = false);
        ~Writer();

        void close();
        void flush();
        const std::filesystem::path& getFilePath() const { return filePath_; }
        bool is_open() const { return stream_.is_open(); }
        bool open(const std::filesystem::path& filepath, bool overwrite = false);
        
        bool writeRow(const Row& row);
        
    private:
        bool writeHeader();

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
