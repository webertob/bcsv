#pragma once

/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */


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
#include "packet_header.h"
#include "vle.hpp"
#include <cassert>
#include <cstddef>
#include <fstream>
#include <cstring>


namespace bcsv {

    template<LayoutConcept LayoutType, typename CodecType>
    Writer<LayoutType, CodecType>::Writer(const LayoutType& layout)
        : file_header_(layout.columnCount(), 1)
        , row_(layout)
    {
    }

    template<LayoutConcept LayoutType, typename CodecType>
    Writer<LayoutType, CodecType>::~Writer() {
        if (isOpen()) {
            try {
                close();
            } catch (...) {
                // Suppress exceptions during destruction to prevent std::terminate
                // during stack unwinding. Data may be lost if close() fails here.
            }
        }
    }

     /**
     * @brief Close the binary file
     */
    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::close() {
        if (!stream_.is_open()) {
            return;
        }
        
        if (packet_open_) {
            closePacket();
        }
        
        FileFooter footer(packet_index_, row_cnt_);
        footer.write(stream_);
        
        stream_.close();
        codec_ = CodecType();  // Move-assign default; old codec's destructor releases the structural lock
        file_path_.clear();
        lz4_stream_.reset();
        row_buffer_raw_.clear();
        row_buffer_raw_.shrink_to_fit();
        row_buffer_prev_.clear();
        row_buffer_prev_.shrink_to_fit();
        packet_index_.clear();
        packet_index_.shrink_to_fit();
        row_cnt_ = 0;       
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::flush() {
        if (!stream_.is_open()) {
            return;
        }
        stream_.flush();
    }

     /**
     * @brief Open a binary file for writing with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @param overwrite Whether to overwrite existing files (default: false)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType, typename CodecType>
    bool Writer<LayoutType, CodecType>::open(const FilePath& filepath, bool overwrite, size_t compressionLevel, size_t blockSizeKB, FileFlags flags) {
        err_msg_.clear();
        
        if(isOpen()) {
            err_msg_ = "Warning: File is already open: " + file_path_.string();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
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
                    err_msg_ = "Error: Cannot create directory: " + parentDir.string() +
                              " (Error: " + ec.message() + ")";
                    throw std::runtime_error(err_msg_);
                }
            }

            // Check if file already exists
            if (std::filesystem::exists(absolutePath)) {
                if (!overwrite) {
                    err_msg_ = "Warning: File already exists: " + absolutePath.string() +
                              ". Use overwrite=true to replace it.";
                    throw std::runtime_error(err_msg_);
                }
            }

            // Check parent directory write permissions
            std::error_code ec;
            auto perms = std::filesystem::status(parentDir, ec).permissions();
            if (ec || (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
                err_msg_ = "Error: No write permission for directory: " + parentDir.string();
                throw std::runtime_error(err_msg_);
            }

            // Open the binary file
            stream_.open(absolutePath, std::ios::binary);
            if (!stream_.good()) {
                err_msg_ = "Error: Cannot open file for writing: " + absolutePath.string() +
                          " (Check permissions and disk space)";
                throw std::runtime_error(err_msg_);
            }

            // Store file path
            file_path_ = absolutePath;
            file_header_ = FileHeader(layout().columnCount(), compressionLevel);
            file_header_.setFlags(flags);
            file_header_.setPacketSize(std::clamp(blockSizeKB*1024, size_t(MIN_PACKET_SIZE), size_t(MAX_PACKET_SIZE)));  // limit packet size to 64KB-1GB
            file_header_.writeToBinary(stream_, layout());
            row_cnt_ = 0;
                        
            // Initialize LZ4 streaming compression if enabled
            if (compressionLevel > 0) {
                int acceleration = 10 - compressionLevel;  // Maps 1-9 to 9-1
                lz4_stream_.emplace(64 * 1024, acceleration);
            }
            
            // Initialize payload hasher for checksum chaining
            packet_hash_.reset();
            packet_index_.clear();            
            row_.clear();
            row_buffer_raw_.clear();
            row_buffer_prev_.clear(); // Start with empty previous row

            // Initialize codec (Item 11)
            codec_.setup(layout());
            codec_.reset();

            return true;

        } catch (const std::filesystem::filesystem_error& ex) {
            if (err_msg_.empty()) {
                err_msg_ = std::string("Filesystem error: ") + ex.what();
            }
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        } catch (const std::exception& ex) {
            if (err_msg_.empty()) {
                err_msg_ = std::string("Error opening file: ") + ex.what();
            }
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        }
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::closePacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }
        
        if(!packet_open_) {
            throw std::runtime_error("Error: No open packet to close");
        }

        // 1. Write packet terminator (this effectivly limits the maximum length of a row)
        // We use writeRowLength to ensure consistent VLE encoding and checksum updates
        writeRowLength(PCKT_TERMINATOR);

        // 2. Finalize packet: write checksum
        uint64_t hash = packet_hash_.finalize();
        stream_.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        packet_open_ = false;
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::openPacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if(packet_open_) {
            throw std::runtime_error("Error: Packet is already open");
        }
        
        // Record packet start position in index if needed
        if(file_header_.hasFlag(FileFlags::NO_FILE_INDEX) == false) {
            size_t packetOffset = stream_.tellp();
            packet_index_.emplace_back(packetOffset, row_cnt_);
        }
        
        // Write packet header
        PacketHeader::write(stream_, row_cnt_);

        // Reset packet to initial state
        packet_size_ = 0;
        packet_hash_.reset();
        if (lz4_stream_.has_value()) {
            lz4_stream_->reset();
        }
        row_buffer_prev_.clear();
        row_buffer_raw_.clear();
        codec_.reset();  // Item 11: reset codec state at packet boundary
        packet_open_ = true;
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::write(const RowType& row) {
        row_ = row;
        writeRow();
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::writeRow() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if(!packet_open_) {
            openPacket();
        }

        // 1. Serialize row to buffer via codec (Item 11)
        row_buffer_raw_.clear();
        std::span<std::byte> actRow = codec_.serialize(row_, row_buffer_raw_);

        // 2. write row data to file
        // TODO: This ZoH repeat check should be codec-agnostic. Currently we gate on
        //       ZERO_ORDER_HOLD flag, but ideally actRow.size() == 0 should be the
        //       universal "no change" signal from any codec (Flat always returns non-empty,
        //       ZoH returns empty when row is unchanged). Rework to unify once Flat codec
        //       adopts the same convention.
        if (actRow.size() == 0)
        {
            // No change detected by codec → ZoH repeat
            writeRowLength(0);
        }
        else if(lz4_stream_.has_value()) 
        {
            // compress data using LZ4 before writing
            // Note: actRow is a span into row_buffer_raw_. LZ4CompressionStream copies
            // source data into its own ring buffer (or uses LZ4_saveDict for large inputs),
            // so the source pointer is not retained after compress() returns.
            const auto compressedData= lz4_stream_->compressUseInternalBuffer(actRow);
            writeRowLength(compressedData.size());
            stream_.write(reinterpret_cast<const char*>(compressedData.data()), compressedData.size());
            packet_hash_.update(compressedData);
            packet_size_ += compressedData.size();
            std::swap(row_buffer_prev_, row_buffer_raw_);
        } 
        else 
        {
            // No compression, write raw data to stream
            writeRowLength(actRow.size());
            stream_.write(reinterpret_cast<const char*>(actRow.data()), actRow.size());
            packet_hash_.update(actRow);
            packet_size_ += actRow.size();
            std::swap(row_buffer_prev_, row_buffer_raw_);
        }

        row_cnt_++;
        if(packet_size_ >= file_header_.getPacketSize()) {
            closePacket();
        }
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::writeRowLength(size_t length)
    {
        assert(stream_.is_open());
        assert(packet_open_);

        // Use unified VLE encoding (Block Length Encoding)
        uint64_t tempBuf;
        size_t numBytes = vleEncode<uint64_t, true>(length, &tempBuf, sizeof(tempBuf));
        
        // Write encoded bytes directly to stream
        stream_.write(reinterpret_cast<const char*>(&tempBuf), numBytes);
        
        // Update packet checksum and size
        packet_hash_.update(reinterpret_cast<const char*>(&tempBuf), numBytes);
        packet_size_ += numBytes;
    }

} // namespace bcsv