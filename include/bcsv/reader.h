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
        std::shared_ptr<LayoutType> layout_;
        ByteBuffer buffer_raw_;
        ByteBuffer buffer_zip_;
        std::vector<uint16_t> row_offsets_;     // Row offsets for indexing
        size_t row_index_file_   = 0;           // current row index within the file
        size_t row_index_packet_ = 0;           // current row index within the packet
        size_t packet_row_count_ = 0;           // number of rows in the current packet
        ReaderMode mode_ = ReaderMode::RESILIENT; // Default to resilient mode for backward compatibility

        std::ifstream stream_;                  // Always binary file stream
        std::filesystem::path filePath_;        // Always present
        uint8_t fileCompressionLevel_ = 1;      // Compression level from file header
        

    public:
        explicit Reader(std::shared_ptr<LayoutType> &layout);
        explicit Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath);
        explicit Reader(std::shared_ptr<LayoutType> &layout, ReaderMode mode);
        explicit Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, ReaderMode mode);
        ~Reader();

        void close();
        const std::filesystem::path& getFilePath() const { return filePath_; }
        bool is_open() const {  return stream_.is_open(); }
        bool open(const std::filesystem::path& filepath);

        // Reader mode management
        void setMode(ReaderMode mode) { mode_ = mode; }
        ReaderMode getMode() const { return mode_; }

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

        static std::shared_ptr<Reader<LayoutType>> create(std::shared_ptr<LayoutType> &layout, ReaderMode mode) {
            return std::make_shared<Reader<LayoutType>>(layout, mode);
        }

        static std::shared_ptr<Reader<LayoutType>> create(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, ReaderMode mode) {
            return std::make_shared<Reader<LayoutType>>(layout, filepath, mode);
        }
    };

} // namespace bcsv
