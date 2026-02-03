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

    template<LayoutConcept LayoutType>
    Writer<LayoutType>::Writer(const LayoutType& layout) 
    : fileHeader_(layout.columnCount(), 1), row_(layout)
    {
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
        
        if (packetOpen_) {
            closePacket();
        }
        
        FileFooter footer(packetIndex_, rowCnt_);
        footer.write(stream_);
        
        stream_.close();
        filePath_.clear();
        lz4Stream_.reset();
        rowBufferRaw_.clear();
        rowBufferRaw_.shrink_to_fit();
        rowBufferPrev_.clear();
        rowBufferPrev_.shrink_to_fit();
        packetIndex_.clear();
        packetIndex_.shrink_to_fit();
        rowCnt_ = 0;       
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::flush() {
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
    template<LayoutConcept LayoutType>
    bool Writer<LayoutType>::open(const FilePath& filepath, bool overwrite, size_t compressionLevel, size_t blockSizeKB, FileFlags flags) {
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
            fileHeader_ = FileHeader(layout().columnCount(), compressionLevel);
            fileHeader_.setFlags(flags);
            fileHeader_.setPacketSize(std::clamp(blockSizeKB*1024, size_t(MIN_PACKET_SIZE), size_t(MAX_PACKET_SIZE)));  // limit packet size to 64KB-1GB
            fileHeader_.writeToBinary(stream_, layout());
            rowCnt_ = 0;
                        
            // Initialize LZ4 streaming compression if enabled
            if (compressionLevel > 0) {
                int acceleration = 10 - compressionLevel;  // Maps 1-9 to 9-1
                lz4Stream_.emplace(acceleration);
            }
            
            // Initialize payload hasher for checksum chaining
            packetHash_.reset();
            packetIndex_.clear();            
            row_.clear();
            row_.trackChanges(fileHeader_.hasFlag(FileFlags::ZERO_ORDER_HOLD));     // Enable change tracking by default
            rowBufferRaw_.clear();
            rowBufferPrev_.clear(); // Start with empty previous row
            return true;

        } catch (const std::filesystem::filesystem_error& ex) {
            std::cerr << "Filesystem error: " << ex.what() << std::endl;
            return false;
        } catch (const std::exception& ex) {
            std::cerr << "Error opening file: " << ex.what() << std::endl;
            return false;
        }

        return true;
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::closePacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }
        
        if(!packetOpen_) {
            throw std::runtime_error("Error: No open packet to close");
        }

        // 1. Write packet terminator (this effectivly limits the maximum length of a row)
        // We use writeRowLength to ensure consistent VLE encoding and checksum updates
        writeRowLength(PCKT_TERMINATOR);

        // 2. Finalize packet: write checksum
        uint64_t hash = packetHash_.finalize();
        stream_.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        packetOpen_ = false;
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::openPacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if(packetOpen_) {
            throw std::runtime_error("Error: Packet is already open");
        }
        
        // Record packet start position in index if needed
        if(fileHeader_.hasFlag(FileFlags::NO_FILE_INDEX) == false) {
            size_t packetOffset_ = stream_.tellp();
            packetIndex_.emplace_back(packetOffset_, rowCnt_);
        }
        
        // Write packet header
        PacketHeader::write(stream_, rowCnt_);

        // Reset packet to initial state
        packetSize_ = 0;
        packetHash_.reset();
        if (lz4Stream_.has_value()) {
            lz4Stream_->reset();
        }
        rowBufferPrev_.clear();
        rowBufferRaw_.clear();
        row_.setChanges();      // mark all as changed for first row
        packetOpen_ = true;
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::writeRow() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if(!packetOpen_) {
            openPacket();
        }

        // 1. Serialize row to buffer
        rowBufferRaw_.clear();
        std::span<std::byte> actRow;
        if(fileHeader_.hasFlag(FileFlags::ZERO_ORDER_HOLD)) {
            actRow = row_.serializeToZoH(rowBufferRaw_);
            row_.resetChanges();
        } else {
            actRow = row_.serializeTo(rowBufferRaw_);
        }

        // 2. write row data to file
        if  (
                fileHeader_.hasFlag(FileFlags::ZERO_ORDER_HOLD) 
            &&  (
                    actRow.size() == 0
                ||  (
                        actRow.size() == rowBufferPrev_.size() 
                    &&  std::equal(actRow.begin(), actRow.end(), rowBufferPrev_.begin())
                    )
                )
            )
        {
            //identical to previous row -> ZoH repeat
            writeRowLength(0);
            rowBufferRaw_.resize(rowBufferRaw_.size()-actRow.size()); // clear actRow from buffer
            //std::swap(rowBufferPrev_, rowBufferRaw_); --> ZoH repeat, keep previous row inplace
        }
        else if(lz4Stream_.has_value()) 
        {
            // compress data using LZ4 before writing
            const auto compressedData= lz4Stream_->compressUseInternalBuffer(rowBufferRaw_);
            writeRowLength(compressedData.size());
            stream_.write(reinterpret_cast<const char*>(compressedData.data()), compressedData.size());
            packetHash_.update(compressedData);
            packetSize_ += compressedData.size();
            std::swap(rowBufferPrev_, rowBufferRaw_);
        } 
        else 
        {
            // Non-ZoH row, no compression, simply write raw data to stream
            writeRowLength(rowBufferRaw_.size());
            stream_.write(reinterpret_cast<const char*>(rowBufferRaw_.data()), rowBufferRaw_.size());
            packetHash_.update(rowBufferRaw_);
            packetSize_ += rowBufferRaw_.size();
            std::swap(rowBufferPrev_, rowBufferRaw_);
        }

        rowCnt_++;
        if(packetSize_ >= fileHeader_.getPacketSize()) {
            closePacket();
        }
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::writeRowLength(size_t length)
    {
        assert(stream_.is_open());
        assert(packetOpen_);

        // Use unified VLE encoding (Block Length Encoding)
        uint64_t tempBuf;
        size_t numBytes = vle_encode<uint64_t, true>(length, &tempBuf, sizeof(tempBuf));
        
        // Write encoded bytes directly to stream
        stream_.write(reinterpret_cast<const char*>(&tempBuf), numBytes);
        
        // Update packet checksum and size
        packetHash_.update(reinterpret_cast<const char*>(&tempBuf), numBytes);
        packetSize_ += numBytes;
    }

} // namespace bcsv