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
#include <fstream>
#include <limits>
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
        
        if (packetInitialized_) {
            flushPacket();
        }
        
        FileFooter footer(packetIndex_, packetHash_.finalize(), totalRowCount_);
        footer.write(stream_);
        
        stream_.close();
        filePath_.clear();
        
        lz4Stream_.reset();
        bufferRawRow_.clear();
        bufferRawRow_.shrink_to_fit();
        bufferCompressed_.clear();
        bufferCompressed_.shrink_to_fit();
        bufferPrevRow_.clear();
        bufferPrevRow_.shrink_to_fit();
        packetIndex_.clear();
        packetIndex_.shrink_to_fit();
        totalRowCount_ = 0;       
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::flush() {
        if (!stream_.is_open()) {
            return;
        }
        if (packetInitialized_) {
            flushPacket();
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
            fileHeader_.setBlockSize(std::clamp(blockSizeKB, size_t(1), size_t(4096)) * 1024);  // limit block size to 1-4096 KB
            fileHeader_.writeToBinary(stream_, layout());
            
            // v1.3.0 streaming initialization
            totalRowCount_ = 0;
            packetSize_ = 0;
            
            // Initialize LZ4 streaming compression if enabled
            if (compressionLevel > 0) {
                lz4Stream_.emplace(compressionLevel);
            }
            
            // Initialize payload hasher for checksum chaining
            packetHash_.reset();
            
            // Pre-allocate buffers based on maximum row size
            // Note: Layout doesn't have maxRowSize(), use a conservative estimate
            size_t estimatedMaxRowSize = layout().columnCount() * 16; // Estimate: 16 bytes per column average
            bufferRawRow_.reserve(estimatedMaxRowSize);
            bufferCompressed_.reserve(LZ4_COMPRESSBOUND(estimatedMaxRowSize));
            bufferPrevRow_.reserve(estimatedMaxRowSize);
            bufferPrevRow_.clear(); // Start with empty previous row
            packetIndex_.clear();            
            row_.clear();
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
    void Writer<LayoutType>::initializePacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        if( packetInitialized_) {
            throw std::runtime_error("Error: Packet is already initialized");
        }
        
        // Record packet start position and row index
        if(fileHeader_.hasFlag(FileFlags::NO_FILE_INDEX) == false) {
            size_t packetOffset_ = stream_.tellp();
            packetIndex_.emplace_back(packetOffset_, totalRowCount_);
        }
        
        PacketHeaderV3 header;
        header.firstRowIndex = totalRowCount_;
        header.prevPayloadChecksum = packetHash_.finalize();
        header.write(stream_);

        // Reset packet to initial state
        packetInitialized_ = true;
        packetSize_ = 0;
        packetHash_.reset();
        if (lz4Stream_.has_value()) {
            lz4Stream_->reset();
        }
    }

    template<LayoutConcept LayoutType>
    bool Writer<LayoutType>::isZoHRepeat() {
        // ZoH always enabled: check if current row matches previous row
        if (bufferPrevRow_.empty()) {
            return false; // First row can't be a repeat
        }
        
        if (bufferRawRow_.size() != bufferPrevRow_.size()) {
            return false; // Different sizes mean different data
        }
        
        // Compare raw bytes using memcmp
        return std::memcmp(bufferRawRow_.data(), bufferPrevRow_.data(), bufferRawRow_.size()) == 0;
    }

    template<LayoutConcept LayoutType>
    bool Writer<LayoutType>::writeRow() {
        if (!stream_.is_open()) {
            std::cerr << "Error: File is not open";
            return false;
        }

        if(packetInitialized_ == false) {
            initializePacket();
        }

        // 1. Serialize row to buffer
        bufferRawRow_.clear();
        row_.serializeTo(bufferRawRow_);
        
        // 2. Check for ZoH repeat (always enabled via memcmp)
        bool isZoH = isZoHRepeat();
        if (isZoH) {
            // Write ZoH sentinel: VLE(0)
            auto rowLength = vle_encode<uint64_t>(0);
            stream_.write(reinterpret_cast<const char*>(rowLength.data()), rowLength.size());
            packetHash_.update(rowLength.data(), rowLength.size());
            packetSize_ += rowLength.size(); // VLE(0) is always 1 byte
            
        } else {
            // Non-ZoH row: compress and write
            std::span<const uint8_t> dataToWrite(reinterpret_cast<const uint8_t*>(bufferRawRow_.data()), bufferRawRow_.size());

            if (lz4Stream_.has_value()) {
                // Compress with LZ4 streaming
                bufferCompressed_.resize(LZ4_COMPRESSBOUND(dataToWrite.size()));
                
                // LZ4CompressionStream::compress expects std::span
                const auto& input = dataToWrite;
                std::span<uint8_t> output(reinterpret_cast<uint8_t*>(bufferCompressed_.data()), bufferCompressed_.size());
                int compressedSize = lz4Stream_->compress(input, output);
                
                if (compressedSize <= 0) {
                    std::cerr << "Warning: LZ4 compression failed, writing uncompressed\n";
                } else {
                    dataToWrite = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bufferCompressed_.data()), compressedSize);
                }
            }
            // If compressionLevel == 0, lz4Stream_ is nullopt, so write uncompressed
            
            // Write VLE(dataToWrite.size() + 1) and data
            auto rowLength = vle_encode<uint64_t>(dataToWrite.size() + 1); // Offset by 1 (VLE(0) is ZoH, VLE(1) is terminator)
            
            // Write VLE and data
            stream_.write(reinterpret_cast<const char*>(rowLength.data()), rowLength.size());
            stream_.write(reinterpret_cast<const char*>(dataToWrite.data()), dataToWrite.size());
            
            // Update payload checksum with VLE + data
            packetHash_.update(rowLength.data(), rowLength.size());
            packetHash_.update(dataToWrite.data(), dataToWrite.size());
            packetSize_ += rowLength.size() + dataToWrite.size();
            
            // Update previous row buffer for next ZoH comparison
            std::swap(bufferPrevRow_, bufferRawRow_);
        }
        totalRowCount_++;
        
        // 3. Check if packet exceeds threshold and flush if needed
        if (packetSize_ >= fileHeader_.blockSize()) {
            flushPacket();
        }
        return true;
    }

    template<LayoutConcept LayoutType>
    void Writer<LayoutType>::flushPacket() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }
        
        if (packetSize_ == 0) {
            return; // Empty packet, nothing to flush
        }
        
        // 1. Write terminator VLE(1)
        auto vleBytes = vle_encode<uint64_t>(1); // Terminator
        stream_.write(reinterpret_cast<const char*>(vleBytes.data()), vleBytes.size());
        packetHash_.update(vleBytes.data(), vleBytes.size());
        packetSize_ += vleBytes.size();
        packetInitialized_ = false;
    }

} // namespace bcsv