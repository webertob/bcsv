/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

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
#include "packet_header.h"
#include <fstream>

namespace bcsv {

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader(ReaderMode mode) 
    : mode_(mode), fileHeader_(), row_(LayoutType())
    {
        row_index_file_ = 0;
        row_index_packet_ = 0;
        row_lengths_.clear();
    }

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::~Reader() {
        if (isOpen()) {
            close();
        }
    }

    /**
     * @brief Close the binary file
     */
    template<LayoutConcept LayoutType>
    void Reader<LayoutType>::close() {
        if(!isOpen()) {
            return;
        }
        fileHeader_ = FileHeader();
        //row_ = typename LayoutType::RowType(LayoutType()); // lets keep the layout and row data available
        stream_.close();
        filePath_.clear();
        buffer_raw_.clear();
        buffer_zip_.clear();
        buffer_raw_.shrink_to_fit();
        buffer_zip_.shrink_to_fit();
        row_index_file_ = 0;
        row_index_packet_ = 0;
        packet_row_count_ = 0;
        row_lengths_.clear();
        row_offsets_.clear();
    }

    /**
     * @brief Open a binary file for reading with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::open(const FilePath& filepath) {
        if(isOpen()) {
            std::cerr << "Warning: File is already open: " << filePath_ << std::endl;
            return false;
        }

        try {
            // Convert to absolute path for consistent handling
            FilePath absolutePath = std::filesystem::absolute(filepath);
            
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
        row_lengths_.clear();
        row_offsets_.clear();

        if (!stream_) {
            return false;
        }
        
        LayoutType layout;
        if(!fileHeader_.readFromBinary(stream_, layout)) {
            fileHeader_ = FileHeader();
            row_ = typename LayoutType::RowType(LayoutType()); // reset layout and row
            std::cerr << "Error: Failed to read or validate file header" << std::endl;
            return false;
        } else if (fileHeader_.versionMajor() != bcsv::VERSION_MAJOR || fileHeader_.versionMinor() != bcsv::VERSION_MINOR) {
            fileHeader_ = FileHeader();
            row_ = typename LayoutType::RowType(LayoutType()); // reset layout and row
            std::cerr << "Error: Incompatible file version: "
                        << static_cast<int>(fileHeader_.versionMajor()) << "."
                        << static_cast<int>(fileHeader_.versionMinor())
                        << " (Expected: " << static_cast<int>(bcsv::VERSION_MAJOR) << "." 
                        << static_cast<int>(bcsv::VERSION_MINOR) << ")\n";
            return false;
        } else {    
            row_ = typename LayoutType::RowType(layout);
            row_.trackChanges(fileHeader_.hasFlag(FileFlags::ZERO_ORDER_HOLD));
        }
        buffer_raw_.reserve(fileHeader_.blockSize()*2); // reserve double block size for safety
        buffer_zip_.reserve(fileHeader_.blockSize()*2); // reserve double block size for safety
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

                //read row lengths (by definition 1st offset is always 0)
                row_lengths_.resize(header.rowCount-1);
                stream_.read(reinterpret_cast<char*>(row_lengths_.data()), row_lengths_.size() * sizeof(uint16_t));
                if (stream_.gcount() != static_cast<std::streamsize>(row_lengths_.size() * sizeof(uint16_t))) {
                    throw std::runtime_error("Error: Failed to read row lengths");
                }

                // Read the compressed packet data
                buffer_zip_.resize(header.payloadSizeZip);
                stream_.read(reinterpret_cast<char*>(buffer_zip_.data()), buffer_zip_.size());
                if (stream_.gcount() != static_cast<std::streamsize>(buffer_zip_.size())) {
                    throw std::runtime_error("Error: Failed to read packet data");
                }

                // validate CRC
                if(!header.validateCRC32(row_lengths_, buffer_zip_)) {
                    throw std::runtime_error("Error: Packet CRC32 validation failed");
                }

                // Handle decompression based on file compression level
                if (compressionLevel() == 0) {
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

                // rebuild row offsets
                row_offsets_.resize(header.rowCount);
                row_offsets_[0] = 0;
                for (size_t i = 1; i < row_offsets_.size(); ++i) {
                    row_offsets_[i] = row_offsets_[i - 1] + row_lengths_[i - 1];
                }

                // rebuild last row length based on total size
                if(row_offsets_.back() > buffer_raw_.size()) {
                    throw std::runtime_error("Error: Row lengths exceed packet data size");
                } 
                // last row length is implicit from total size
                auto last_length = buffer_raw_.size() - row_offsets_.back();
                if(last_length > std::numeric_limits<uint16_t>::max()) {
                    throw std::runtime_error("Error: Last row length exceeds uint16_t maximum");
                }
                row_lengths_.push_back(static_cast<uint16_t>(last_length));

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
        row_lengths_.clear();
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
    bool Reader<LayoutType>::readNext() {
        if(row_index_packet_ >= packet_row_count_ ) {
            if(!readPacket()) {
                return false;
            }
            if(packet_row_count_ == 0) {
                return false;
            }
        }

        size_t row_offset = row_offsets_[row_index_packet_];
        size_t row_length = row_lengths_[row_index_packet_];
        if(fileHeader_.hasFlag(FileFlags::ZERO_ORDER_HOLD)) {
            if(!row_length) {
                // special case: empty row in ZoH means "repeat/keep previous row"
                // if this is the first row in the file, we cannot repeat anything
                if(row_index_file_ == 0) {
                    std::cerr << "Error: First row in file cannot be empty in Zero-Order Hold mode" << std::endl;
                    return false;
                }
            } else {
                if (!row_.deserializeFromZoH({buffer_raw_.data() + row_offset, row_length})) {
                    return false;
                }
            }
        } else {
            if (!row_.deserializeFrom({buffer_raw_.data() + row_offset, row_length})) {
                return false;
            }
        }
        row_index_packet_++;
        row_index_file_++;
        return true;
    }

} // namespace bcsv