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
#include <limits>
#include <type_traits>

namespace bcsv {

    template<LayoutConcept LayoutType>
    Writer<LayoutType>::Writer(std::shared_ptr<LayoutType> layout) : layout_(std::move(layout)) {
        buffer_raw_.reserve(LZ4_BLOCK_SIZE_KB * 1024);
        buffer_zip_.reserve(LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE_KB * 1024));

        // Using concepts instead of static_assert for type checking
        if (!layout_) {
            throw std::runtime_error("Error: Layout is not initialized");
        }
    }

    template<LayoutConcept LayoutType>
    Writer<LayoutType>::Writer(std::shared_ptr<LayoutType> layout, const std::filesystem::path& filepath, bool overwrite) 
        : Writer(std::move(layout)) {
        open(filepath, overwrite);
    }


    template<LayoutConcept LayoutType>
    Writer<LayoutType>::~Writer() {
        if (is_open()) {
            close();
        }
        layout_->unlock(this);
    }

     /**
     * @brief Close the binary file
     */
    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::close() {
        if (stream_.is_open()) {
            flush();
            stream_.close();
            filePath_.clear();
            layout_->unlock(this);
        }
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::flush() {
        if (stream_.is_open()) {
            // Write any remaining data in the buffer before flushing the stream
            if (!buffer_raw_.empty()) {
                writePacket();
            }
            stream_.flush();
        }
    }

     /**
     * @brief Open a binary file for writing with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @param overwrite Whether to overwrite existing files (default: false)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType>
    bool Writer<LayoutType>::open(const std::filesystem::path& filepath, bool overwrite) {
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
            if (!stream_.good()) {
                throw std::runtime_error("Error: Cannot open file for writing: " + absolutePath.string() +
                                           " (Check permissions and disk space)");
            }

            // Store file path
            filePath_ = absolutePath;
            writeFileHeader();
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

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::writeFileHeader() {
        // Write the header information to the stream
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }
        layout_->lock(this);
        FileHeader fileHeader;
        fileHeader.writeToBinary(stream_, *layout_);
        row_cnt_ = 0;
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::writePacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if (buffer_raw_.empty()) {
            return; // Nothing to write
        }

        // Compress the raw buffer using LZ4
        buffer_zip_.resize(buffer_zip_.capacity());
        int compressedSize = LZ4_compress_default(reinterpret_cast<const char*>(buffer_raw_.data()), 
                                                  reinterpret_cast<char*>(buffer_zip_.data()),
                                                  static_cast<int>(buffer_raw_.size()),
                                                  static_cast<int>(buffer_zip_.size()));
        if (compressedSize <= 0) {
            buffer_zip_.clear();
            throw std::runtime_error("Error: LZ4 compression failed");
        } else {
            buffer_zip_.resize(compressedSize);
        }
        
        // Check for potential overflow when converting size_t to uint32_t
        if (RANGE_CHECKING && buffer_raw_.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Raw buffer size exceeds uint32_t maximum");
        }
        if (RANGE_CHECKING && buffer_zip_.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Compressed buffer size exceeds uint32_t maximum");
        }
        if (RANGE_CHECKING && row_offsets_.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Row count difference exceeds uint32_t maximum");
        }
        
        PacketHeader packetHeader;
        packetHeader.payloadSizeZip = static_cast<uint32_t>(buffer_zip_.size());
        packetHeader.rowFirst = row_cnt_; 
        packetHeader.rowCount = static_cast<uint32_t>(row_offsets_.size()); // Number of rows
        row_offsets_.pop_back();    // remove last offset (end of last row) based on the format specification
        packetHeader.updateCRC32(row_offsets_, buffer_zip_);

        // Write the packet (header + row offsets + compressed data)
        stream_.write(reinterpret_cast<const char*>(&packetHeader), sizeof(packetHeader));
        
        // Safe conversion for row offsets size
        size_t row_offsets_bytes = row_offsets_.size() * sizeof(uint16_t);
        stream_.write(reinterpret_cast<const char*>(row_offsets_.data()), static_cast<std::streamsize>(row_offsets_bytes));
        stream_.write(reinterpret_cast<const char*>(buffer_zip_.data()), static_cast<std::streamsize>(buffer_zip_.size()));

        // Clear buffers for next packet
        buffer_raw_.clear();
        buffer_zip_.clear();
        row_offsets_.clear();
        row_cnt_ += packetHeader.rowCount; // Update total row count
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::writeRow(const typename LayoutType::RowType& row) {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        //check row belongs to layout_
        if (row.getLayoutPtr() != layout_) {
            throw std::invalid_argument("Row does not belong to layout");
        }

        // check if the new row fits into the current packet, if not write the current packet
        size_t fixedSize, totalSize;
        row.serializedSize(fixedSize, totalSize);
        size_t row_raw_size = totalSize;
        if(buffer_raw_.size() + row_raw_size > buffer_raw_.capacity()) {
            writePacket();
        }

        // Append serialized row to raw buffer
        std::byte* ptr_raw = buffer_raw_.data() + buffer_raw_.size();
        buffer_raw_.resize(buffer_raw_.size() + row_raw_size);
        row.serializeTo(ptr_raw, row_raw_size);
        row_offsets_.push_back(static_cast<uint16_t>(buffer_raw_.size()));
    }

} // namespace bcsv