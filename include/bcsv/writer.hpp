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
    Writer<LayoutType>::Writer(const LayoutType& layout) 
    : fileHeader_(layout.getColumnCount(), 1), row_(layout)
    {
        buffer_raw_.reserve(LZ4_BLOCK_SIZE_KB * 1024);
        buffer_zip_.reserve(LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE_KB * 1024));
    }

    template<LayoutConcept LayoutType>
    Writer<LayoutType>::~Writer() {
        if (is_open()) {
            close();
        }
    }

     /**
     * @brief Close the binary file
     */
    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::close() {
        if (!stream_.is_open()) {
            return;
        }
        flush();
        stream_.close();
        filePath_.clear();        
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::flush() {
        if (!stream_.is_open()) {
            return;
        }

        // Write any remaining data in the buffer before flushing the stream
        if (!buffer_raw_.empty()) {
            writePacket();
        }
        stream_.flush();
    }

     /**
     * @brief Open a binary file for writing with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @param overwrite Whether to overwrite existing files (default: false)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType>
    bool Writer<LayoutType>::open(const FilePath& filepath, bool overwrite, uint8_t compressionLevel) {
        if(is_open()) {
            std::cerr << "Warning: File is already open: " << filePath_ << std::endl;
            return false;
        }

        try {
            // Convert to absolute path for consistent handling
            FilePath absolutePath = std::filesystem::absolute(filepath);

            // Check if parent directory exists, create if needed
            FilePath parentDir = absolutePath.parent_path();
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
            fileHeader_ = FileHeader(getLayout().getColumnCount(), static_cast<uint8_t>(compressionLevel));
            fileHeader_.writeToBinary(stream_, getLayout());
            row_cnt_ = 0;
            row_.clear();
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
    void Writer<LayoutType>::writePacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if (buffer_raw_.empty()) {
            return; // Nothing to write
        }

        // Handle compression based on level
        if (getCompressionLevel() == 0) {
            // No compression - use raw data directly (avoid LZ4 overhead)
            // Optimize by reusing the raw buffer as compressed buffer to avoid copying
            buffer_zip_.swap(buffer_raw_); // Efficient swap instead of copy
        } else {
            // Compress the raw buffer using LZ4 with specified level
            buffer_zip_.resize(buffer_zip_.capacity());
            int compressedSize;
            
            if (getCompressionLevel() == 1) {
                // Use fast default compression
                compressedSize = LZ4_compress_default(reinterpret_cast<const char*>(buffer_raw_.data()), 
                                                      reinterpret_cast<char*>(buffer_zip_.data()),
                                                      static_cast<int>(buffer_raw_.size()),
                                                      static_cast<int>(buffer_zip_.size()));
            } else {
                // Use high compression with specified level (2-9)
                compressedSize = LZ4_compress_HC(reinterpret_cast<const char*>(buffer_raw_.data()), 
                                                 reinterpret_cast<char*>(buffer_zip_.data()),
                                                 static_cast<int>(buffer_raw_.size()),
                                                 static_cast<int>(buffer_zip_.size()),
                                                 getCompressionLevel());
            }
            
            if (compressedSize <= 0) {
                buffer_zip_.clear();
                throw std::runtime_error("Error: LZ4 compression failed");
            } else {
                buffer_zip_.resize(compressedSize);
            }
        }
        
        // Check for potential overflow when converting size_t to uint32_t
        if (RANGE_CHECKING && buffer_raw_.size() >= std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Raw buffer size exceeds uint32_t maximum");
        }
        if (RANGE_CHECKING && buffer_zip_.size() >= std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Compressed buffer size exceeds uint32_t maximum");
        }
        if (RANGE_CHECKING && row_offsets_.size() >= std::numeric_limits<uint16_t>::max()) {
            throw std::runtime_error("Row count difference exceeds uint16_t maximum");
        }
        
        PacketHeader packetHeader;
        packetHeader.payloadSizeZip = static_cast<uint32_t>(buffer_zip_.size());
        packetHeader.rowFirst = row_cnt_; 
        packetHeader.rowCount = static_cast<uint32_t>(row_offsets_.size()); // number of rows
        row_offsets_.pop_back(); // last offset is end of data, not a row start
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
    bool Writer<LayoutType>::writeRow() {
        if (!stream_.is_open()) {
            std::cerr << "Error: File is not open";
            return false;
        }

        // serialize the row into the raw buffer
        size_t row_offset = buffer_raw_.size();
        row_.serializeTo(buffer_raw_);
        size_t row_length = buffer_raw_.size() - row_offset;
        
        // store the offset of the next row (for indexing)
        row_offsets_.push_back(static_cast<uint16_t>(buffer_raw_.size()));

        if(     buffer_raw_.size() + row_length >= LZ4_BLOCK_SIZE_KB*1000   // Rough estimate to avoid exceeding block size
             || buffer_raw_.size() >= std::numeric_limits<uint16_t>::max()) // Ensure we don't exceed 16-bit offset limit
        {
            // if packed gets too large, write current packet and start a new one
            writePacket();
        }
        return true;
    }

} // namespace bcsv