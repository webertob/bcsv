#pragma once

/**
 * @file reader.hpp
 * @brief Binary CSV (BCSV) Library - Reader implementations
 * 
 * This file contains the template implementations for the Reader class.
 */

#include "reader.h"
#include "file_header.h"
#include "layout.h"
#include "row.h"
#include <fstream>
#include <type_traits>

namespace bcsv {

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader(std::shared_ptr<LayoutType> &layout) : layout_(layout) {
        buffer_raw_.reserve(LZ4_BLOCK_SIZE_KB * 1024);
        buffer_zip_.reserve(LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE_KB * 1024));
        row_index_file_ = 0;
        row_index_packet_ = 0;
        row_offsets_.clear();
        mode_ = ReaderMode::RESILIENT; // Default mode
        // Using concepts instead of static_assert for type checking
        if (!layout_) {
            throw std::runtime_error("Error: Layout is not initialized");
        }
    }

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath) 
        : Reader(layout) {
        open(filepath);
    }

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader(std::shared_ptr<LayoutType> &layout, ReaderMode mode) 
        : Reader(layout) {
        mode_ = mode;
    }

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, ReaderMode mode) 
        : Reader(layout, mode) {
        open(filepath);
    }

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::~Reader() {
        if (is_open()) {
            close();
        }
        layout_->unlock(this);
    }

    /**
     * @brief Close the binary file
     */
    template<LayoutConcept LayoutType>
    void Reader<LayoutType>::close() {
        if (stream_.is_open()) {
            stream_.close();
            if (!filePath_.empty()) {
                std::cout << "Info: Closed file: " << filePath_ << std::endl;
            }
        }
        filePath_.clear();
        layout_->unlock(this);
        buffer_raw_.clear();
        buffer_zip_.clear();
        row_index_file_ = 0;
        row_index_packet_ = 0;
        packet_row_count_ = 0;
        row_offsets_.clear();
    }

    /**
     * @brief Open a binary file for reading with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::open(const std::filesystem::path& filepath) {
        if(is_open()) {
            std::cerr << "Warning: File is already open: " << filePath_ << std::endl;
            return false;
        }

        try {
            // Convert to absolute path for consistent handling
            std::filesystem::path absolutePath = std::filesystem::absolute(filepath);
            
            // Check if file exists
            if (!std::filesystem::exists(absolutePath)) {
                throw std::runtime_error("Error: File does not exist: " + absolutePath.string());
            }

            // Check if it's a regular file
            if (!std::filesystem::is_regular_file(absolutePath)) {
                throw std::runtime_error("Error: Path is not a regular file: " + absolutePath.string());
            }

            // Check read permissions
            std::error_code ec;
            auto perms = std::filesystem::status(absolutePath, ec).permissions();
            if (ec || (perms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) {
                throw std::runtime_error("Error: No read permission for file: " + absolutePath.string());
            }

            // Open the binary file
            stream_.open(absolutePath, std::ios::binary);
            if (!stream_.is_open()) {
                throw std::runtime_error("Error: Cannot open file for reading: " + absolutePath.string() + " (Check permissions)");
            }
            
            // Store file path
            filePath_ = absolutePath;
            if(!readFileHeader()) {
                stream_.close();
                return false;
            } else {
                return true;
            }

        } catch (const std::filesystem::filesystem_error& ex) {
            std::cerr << "Filesystem error: " << ex.what() << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << "Error opening file: " << ex.what() << std::endl;
        }
        stream_.close();
        return false;
    }

    /* Read the header information from the binary file */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readFileHeader() {
        buffer_raw_.clear();
        buffer_zip_.clear();
        row_index_file_ = 0;
        row_index_packet_ = 0;
        packet_row_count_ = 0;
        row_offsets_.clear();
        if (!stream_) {
            return false;
        }
        layout_->unlock(this);
        FileHeader fileHeader;
        if(fileHeader.readFromBinary(stream_, *layout_)) {
            layout_->lock(this);
            fileCompressionLevel_ = fileHeader.getCompressionLevel();
        } else {
            return false;
        }
        return true;
    }

    /* Read a packet of rows from the binary file */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readPacket() {
        bool normal = true;
        while (stream_)
        {
            try {
                // Check if we're at EOF before trying to read
                if (stream_.peek() == EOF) {
                    break;
                }
                
                // Read packet header (compressed and uncompressed sizes)
                PacketHeader header;
                // in normal mode we expect the header to be at the current position
                // in recovery mode we search for the header magic number
                if(normal) {
                    if (!header.read(stream_)) {
                        throw std::runtime_error("Error: Failed to read packet header in normal mode");
                    }
                } else {
                    if (!header.findAndRead(stream_)) {
                        throw std::runtime_error("Error: Failed to find and read packet header in recovery mode");
                    }
                }

                if(header.rowFirst < row_index_file_) {
                    throw std::runtime_error("Error: Packet rowFirst index is less than current file row index");
                }

                if(header.rowCount == 0) {
                    throw std::runtime_error("Error: Packet rowCount is zero");
                }

                if(header.payloadSizeZip == 0) {
                    throw std::runtime_error("Error: Invalid payload sizes in packet header");
                }

                //read row offsets (by definition 1st offset is always 0)
                row_offsets_.resize(header.rowCount-1);
                stream_.read(reinterpret_cast<char*>(row_offsets_.data()), row_offsets_.size() * sizeof(uint16_t));
                if (stream_.gcount() != static_cast<std::streamsize>(row_offsets_.size() * sizeof(uint16_t))) {
                    throw std::runtime_error("Error: Failed to read row offsets");
                }

                // Read the compressed packet data
                buffer_zip_.resize(header.payloadSizeZip);
                stream_.read(reinterpret_cast<char*>(buffer_zip_.data()), buffer_zip_.size());
                if (stream_.gcount() != static_cast<std::streamsize>(buffer_zip_.size())) {
                    throw std::runtime_error("Error: Failed to read packet data");
                }

                // validate CRC
                if(!header.validateCRC32(row_offsets_, buffer_zip_)) {
                    throw std::runtime_error("Error: Packet CRC32 validation failed");
                }

                // Handle decompression based on file compression level
                if (fileCompressionLevel_ == 0) {
                    // No compression - use compressed buffer directly (avoid LZ4 overhead)
                    buffer_raw_.swap(buffer_zip_); // Efficient swap instead of copy
                } else {
                    // Decompress the packet data using LZ4
                    buffer_raw_.resize(buffer_raw_.capacity());
                    int decompressedSize = LZ4_decompress_safe(
                        reinterpret_cast<const char*>(buffer_zip_.data()), 
                        reinterpret_cast<char*>(buffer_raw_.data()), 
                        static_cast<int>(buffer_zip_.size()), 
                        static_cast<int>(buffer_raw_.size())
                    );
                    if (decompressedSize < 0) {
                        throw std::runtime_error("Error: LZ4 decompression failed");
                    } else {
                        buffer_raw_.resize(decompressedSize);
                    }
                }

                if(!normal) { 
                    std::cerr << "Info: Recovered from error, resynchronized at row index " << header.rowFirst << std::endl;
                    std::cerr << "Info: We have lost range [" << row_index_file_ + 1 << " to " << header.rowFirst-1 << "]" << std::endl;
                    normal = true; // back to normal mode
                }

                // Update row count
                row_index_file_ = header.rowFirst;
                row_index_packet_ = 0;
                packet_row_count_ = header.rowCount;
                return true;
            } catch (const std::exception& ex) {
                // In STRICT mode, immediately rethrow any exceptions
                if (mode_ == ReaderMode::STRICT) {
                    throw;  // Rethrow the original exception
                }
                
                // In RESILIENT mode, log error and try to recover
                std::cerr << "Error reading packet: " << ex.what() << std::endl;
                normal = false; // switch to recovery mode
            }
        }
        buffer_raw_.clear();
        buffer_zip_.clear();
        row_offsets_.clear();
        row_index_packet_ = 0;
        packet_row_count_ = 0;
        return false;
    }

    /*
    * @brief Read a single row from the buffer
    * @param row The row to read into
    * @return The index of the row read. Note: count starts at 1! Returns 0 if no row could be read
    */
    template<LayoutConcept LayoutType>
    size_t Reader<LayoutType>::readRow(LayoutType::RowViewType& row) {
        if(row_index_packet_ >= packet_row_count_ ) {
            if(!readPacket()) {
                return 0;
            }
            if(packet_row_count_ == 0) {
                return 0;
            }
        }

        size_t row_offset = row_index_packet_ == 0 ? 
            0 : 
            row_offsets_[row_index_packet_ - 1];
        size_t row_length = row_index_packet_ < row_offsets_.size() ? 
            row_offsets_[row_index_packet_] - row_offset : 
            buffer_raw_.size() - row_offset;
        
        // Sanity checks
        if(row_offset + row_length > buffer_raw_.size()) {
            throw std::runtime_error("Error: Row offset and length exceed buffer size");
        }

        row.setBuffer(buffer_raw_.data() + row_offset, row_length);
        if (!row.validate()) {
            throw std::runtime_error("Error: Row validation failed");
        }
        row_index_packet_++;
        row_index_file_++;
        return row_index_file_;
    }

} // namespace bcsv