#pragma once

/**
 * @file writer.hpp
 * @brief Binary CSV (BCSV) Library - Writer implementations
 * 
 * This file contains the template implementations for the Writer class.
 */

#include "writer.h"
#include "definitions.h"
#include "file_header.h"
#include "layout.h"
#include "row.h"
#include <fstream>
#include <type_traits>

namespace bcsv {

    template<typename LayoutType>
    Writer<LayoutType>::Writer(std::shared_ptr<LayoutType> &layout) : layout_(layout) {
        buffer_raw_.reserve(LZ4_BLOCK_SIZE_KB * 1024);
        buffer_zip_.reserve(LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE_KB * 1024));

        static_assert(std::is_base_of_v<LayoutInterface, LayoutType>, "LayoutType must derive from LayoutInterface");
        if (!layout_) {
            throw std::runtime_error("Error: Layout is not initialized");
        }
    }

    template<typename LayoutType>
    Writer<LayoutType>::Writer(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, bool overwrite) 
        : Writer(layout) {
        open(filepath, overwrite);
    }


    template<typename LayoutType>
    Writer<LayoutType>::~Writer() {
        if (is_open()) {
            close();
        }
        layout_->unlock(this);
    }

     /**
     * @brief Close the binary file
     */
    template<typename LayoutType>
    void Writer<LayoutType>::close() {
        if (stream_.is_open()) {
            flush();
            stream_.close();
            filePath_.clear();
            layout_->unlock(this);
        }
    }

    template<typename LayoutType>
    void Writer<LayoutType>::flush() {
        if (stream_.is_open()) {
            stream_.flush();
        }
    }

     /**
     * @brief Open a binary file for writing with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @param overwrite Whether to overwrite existing files (default: false)
     * @return true if file was successfully opened, false otherwise
     */
    template<typename LayoutType>
    bool Writer<LayoutType>::open(const std::filesystem::path& filepath, bool overwrite = false) {
        if(is_open()) {
            std::cerr << "Warning: File is already open: " << filePath_ << std::endl;
            return false;
        }

        try {
            // Convert to absolute path for consistent handling
            std::filesystem::path absolutePath = std::filesystem::absolute(filepath);
            
            // Check if parent directory exists, create if needed
            std::filesystem::path parentDir = absolutePath.parent_path();
            if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
                std::error_code ec;
                if (!std::filesystem::create_directories(parentDir, ec)) {
                    throw std::runtime_error("Error: Cannot create directory: " + parentDir.string() +
                                               " (Error: " + ec.message() + ")");
                }
            }

            // Check if file already exists
            if (std::filesystem::exists(absolutePath)) {
                if (!overwrite) {
                    throw std::runtime_error("Warning: File already exists: " + absolutePath.string() +
                                               ". Use overwrite=true to replace it.");
                }
            }

            // Check parent directory write permissions
            std::error_code ec;
            auto perms = std::filesystem::status(parentDir, ec).permissions();
            if (ec || (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
                throw std::runtime_error("Error: No write permission for directory: " + parentDir.string());
            }

            // Open the binary file
            stream_.open(absolutePath, std::ios::binary);
            if (!stream_.is_open()) {
                throw std::runtime_error("Error: Cannot open file for writing: " + absolutePath.string() +
                                           " (Check permissions and disk space)");
            }

            // Store file path
            filePath_ = absolutePath;
            writeHeader();
            return true;

        } catch (const std::filesystem::filesystem_error& ex) {
            std::cerr << "Filesystem error: " << ex.what() << std::endl;
            return false;
        } catch (const std::exception& ex) {
            std::cerr << "Error opening file: " << ex.what() << std::endl;
            return false;
        }

        return false;
    }
        
    template<typename LayoutType>
    void Writer<LayoutType>::writeHeader() {
        // Write the header information to the stream
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }
        layout_->lock(this);
        FileHeader fileHeader;
        fileHeader.writeToBinary(stream_, *layout_);
        rowCounter_ = 0;
    }

    template<typename LayoutType>
    void Writer<LayoutType>::writePacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if (buffer_raw_.empty()) {
            return; // Nothing to write
        }

        // Compress the raw buffer using LZ4
        int compressedSize = LZ4_compress_default(buffer_raw_.data(), buffer_zip_.data(),
                                                  static_cast<int>(buffer_raw_.size()),
                                                  static_cast<int>(buffer_zip_.size()));
        if (compressedSize <= 0) {
            throw std::runtime_error("Error: LZ4 compression failed");
        }

        PacketHeader packetHeader;
        packetHeader.magic = PCKT_MAGIC;
        packetHeader.payloadSizeRaw = static_cast<uint32_t>(buffer_raw_.size());
        packetHeader.payloadSizeZip = static_cast<uint32_t>(compressedSize);
        packetHeader.rowFirst = rowCounterOld_;
        packetHeader.rowCount = rowCounter_ - rowCounterOld_;
        packetHeader.crc32 = 0; // Placeholder for CRC32
        




        
        rowCounterOld_ = rowCounter_;


        // Write the compressed data size and uncompressed size
        uint32_t uncompressedSize = static_cast<uint32_t>(buffer_raw_.size());
        uint32_t compSize = static_cast<uint32_t>(compressedSize);
        stream_.write(reinterpret_cast<const char*>(&uncompressedSize), sizeof(uncompressedSize));
        stream_.write(reinterpret_cast<const char*>(&compSize), sizeof(compSize));

        // Write the compressed data
        stream_.write(buffer_zip_.data(), compSize);

        // Clear the raw buffer for next packet
        buffer_raw_.clear();
    }

    template<typename LayoutType>
    void Writer<LayoutType>::writeRow(const Row& row) {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        //compare types
        auto colTypes = layout_->getColumnTypes();
        auto rowTypes = row.getTypes();
        if (colTypes.size() != rowTypes.size()) {
            throw std::invalid_argument("Row type count does not match layout");
        }
        for (size_t i = 0; i < colTypes.size(); ++i) {
            if (colTypes[i] != rowTypes[i]) {
                throw std::invalid_argument("Row type does not match layout");
            }
        }

        size_t rowSize = row.serializedSize();
        if(buffer_raw_.size() + rowSize > buffer_raw_.capacity()) {
            writePacket();
        }

        size_t bufSize = buffer_raw_.size();
        buffer_raw_.resize(bufSize + rowSize);
        char* dstBuffer = buffer_raw_.data() + bufSize;
        row.serializeTo(dstBuffer, rowSize);
        rowIndex_.push_back(static_cast<uint16_t>(buffer_raw_.size()));
        rowCounter_++;
    }

} // namespace bcsv