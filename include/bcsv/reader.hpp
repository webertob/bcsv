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
 * @brief Binary CSV (BCSV) Library - Reader implementations for v1.3.0
 * 
 * This file contains the template implementations for the Reader class.
 * Supports v1.3.0 streaming LZ4 compression with VLE-encoded payloads.
 */

#include "reader.h"
#include "file_header.h"
#include "layout.h"
#include "packet_header_v3.h"
#include "vle.hpp"
#include <fstream>
#include <cstring>

namespace bcsv {

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader(ReaderMode mode) 
    : mode_(mode)
    , fileHeader_()
    , currentPacketIndex_(0)
    , currentRowIndex_(0)
    , row_(LayoutType())
    {
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
        stream_.close();
        filePath_.clear();
        fileHeader_ = FileHeader();
        fileFooter_.clear();
        lz4Stream_.reset();
        bufferRawRow_.clear();
        currentPacketIndex_ = 0;
        currentRowIndex_ = 0;
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
                throw std::runtime_error("Error: Cannot open file for reading: " + absolutePath.string());
            }
            
            filePath_ = absolutePath;
            
            // Read file header
            if(!readFileHeader()) {
                stream_.close();
                filePath_.clear();
                return false;
            }
            
            // Try to read FileFooter from EOF
            if (!readFileFooter()) {
                stream_.close();
                filePath_.clear();
                return false;
            } 
            return true;

        } catch (const std::exception& ex) {
            std::cerr << "Error opening file: " << ex.what() << std::endl;
            if (stream_.is_open()) {
                stream_.close();
            }
            filePath_.clear();
            return false;
        }
    }

    /**
     * @brief Read file header
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readFileHeader() {
        if (!stream_) {
            return false;
        }
        
        LayoutType layout;
        if(!fileHeader_.readFromBinary(stream_, layout)) {
            std::cerr << "Error: Failed to read file header\n";
            return false;
        }
        
        // Check version compatibility (v1.3.0 only)
        if (fileHeader_.versionMajor() != BCSV_FORMAT_VERSION_MAJOR || 
            fileHeader_.versionMinor() != BCSV_FORMAT_VERSION_MINOR) {
            std::cerr << "Error: Incompatible file version: "
                      << static_cast<int>(fileHeader_.versionMajor()) << "."
                      << static_cast<int>(fileHeader_.versionMinor())
                      << " (Expected: " << static_cast<int>(BCSV_FORMAT_VERSION_MAJOR) << "." 
                      << static_cast<int>(BCSV_FORMAT_VERSION_MINOR) << ")\n";
            return false;
        }
        // Pre-allocate buffers
        row_ = typename LayoutType::RowType(layout);
        bufferRawRow_.reserve(row_.maxByteSize());
        return true;
    }

    /**
     * @brief Read FileFooter or footer from EOF
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readFileFooter() {
        if(!stream_) {
            return false;
        }

        // Save current position
        std::streampos currentPos = stream_.tellg();

        // Try to read FileFooter
        if (!fileFooter_.read(stream_)) {
            if( mode_ == ReaderMode::RESILIENT ) {
                std::cerr << "Warning: FileFooter missing or invalid, attempting to rebuild index\n";
                if (!rebuildFileFooter()) {
                    std::cerr << "Error: Failed to rebuild FileFooter\n";
                    stream_.seekg(currentPos);
                    return false;
                }
            } else {
                std::cerr << "Error: Failed to read FileFooter\n";
                stream_.seekg(currentPos);
                return false;
            }
        }
        stream_.seekg(currentPos); // Restore position
        return true;
    }

    /**
     * @brief Rebuild FileFooter by scanning packets (RESILIENT mode only)
     */
    template<LayoutConcept LayoutType>
    !! This is Broken! bool Reader<LayoutType>::rebuildFileFooter() {
        if(!stream_) {
            return false;
        }
        fileFooter_.clear();
        
        // Get file size
        stream_.seekg(0, std::ios::end);
        std::streampos fileSize = stream_.tellg();

        // Jump to first packet (after file header + layout)
        stream_.seekg(fileHeader_.getBinarySize(layout()), std::ios::beg);
        std::streampos firstPacketPos = stream_.tellg();    // Save position after file header + layout
        
        size_t   packetCount = 0;
        uint32_t blockSize = fileHeader_.blockSize();
        
        while (stream_.tellg() < fileSize - static_cast<std::streamoff>(sizeof(PacketHeaderV3))) {
            std::streampos packetStart = stream_.tellg();
            
            //ToDO This is completly broken.
            We must seek packets. 
            we know packets are ATLEAST blockSize appart. 
            But they are not aligned to block size!, thus we cannot directly jump

            We search for PacketMagic, but need to validate the whole header. using header checksum
            If we have found 2 subsequent valid packets, we can put that information into the packetIndex_

            // Try to read PacketHeaderV3
            PacketHeaderV3 header;
            if (!header.read(stream_)) {
                // Invalid header, try to skip ahead by block size
                stream_.clear();
                stream_.seekg(packetStart + static_cast<std::streamoff>(blockSize));
                continue;
            }
            
            if (!header.validate()) {
                // Invalid header, skip ahead
                stream_.clear();
                stream_.seekg(packetStart + static_cast<std::streamoff>(blockSize));
                continue;
            }
            
            // Valid packet found
            fileIndex_.addPacket(
                static_cast<uint64_t>(packetStart),
                header.firstRowIndex
            );
            
            packetCount++;
            
            // Skip packet payload (estimate using block size)
            stream_.seekg(packetStart + static_cast<std::streamoff>(blockSize));
        }
        
        if (packetCount == 0) {
            std::cerr << "Error: No valid packets found during rebuild\n";
            return false;
        }
        
        // Count total rows by scanning last packet payload
        if (fileIndex_.packetCount() == 0) {
            fileIndex_.setTotalRowCount(0);
        } else {
            const auto& lastPacket = fileIndex_.getPackets().back();
            
            // We already validated the header during the scan above, so it must be valid
            // Seek to last packet payload (skip header) and count rows by jumping through VLE sizes
            stream_.clear();
            stream_.seekg(lastPacket.headerOffset + sizeof(PacketHeaderV3));
            
            size_t rowsInLastPacket = 0;
            
            // Count rows by reading VLE sizes and skipping payload
            while (stream_) {
                try {
                    size_t vleValue = vle_decode<size_t>(stream_);
                    
                    if (vleValue == 0) {
                        // ZoH repeat - no payload, just count
                        rowsInLastPacket++;
                    } else if (vleValue == 1) {
                        // Terminator - end of packet
                        break;
                    } else {
                        // Regular row: vleValue = dataSize + 1
                        size_t dataSize = vleValue - 1;
                        
                        // Skip over the payload data
                        stream_.seekg(dataSize, std::ios::cur);
                        
                        if (stream_) {
                            rowsInLastPacket++;
                        } else {
                            // Incomplete row at end of packet (corrupted file)
                            break;
                        }
                    }
                } catch (const std::exception&) {
                    // Error reading VLE or EOF reached (corrupted file)
                    break;
                }
            }
            fileIndex_.setTotalRowCount(lastPacket.firstRowIndex + rowsInLastPacket);
        }        
        std::cerr << "Rebuilt index: " << packetCount << " packets, ~" << totalRowCount_ << " rows\n";
        return true;
    }

    /**
     * @brief Open next packet for sequential reading
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::openPacket() {
        if (currentPacketIndex_ >= fileIndex_.packetCount()) {
            return false; // No more packets
        }
        
        // Get packet info from index
        const auto& packetEntry = fileIndex_.getPacket(currentPacketIndex_);
        currentPacketHeaderPos_ = packetEntry.byteOffset_;
        
        // Seek to packet header
        stream_.seekg(currentPacketHeaderPos_);
        
        // Read PacketHeaderV3
        PacketHeaderV3 header;
        if (!header.read(stream_)) {
            if (mode_ == ReaderMode::STRICT) {
                throw std::runtime_error("Error: Failed to read PacketHeaderV3");
            }
            std::cerr << "Warning: Failed to read packet header at offset " << currentPacketHeaderPos_ << "\n";
            return false;
        }
        
        if (!header.validate()) {
            if (mode_ == ReaderMode::STRICT) {
                throw std::runtime_error("Error: Invalid PacketHeaderV3 checksum");
            }
            std::cerr << "Warning: Invalid packet header checksum\n";
            return false;
        }
        
        // Verify firstRowIndex matches expected
        if (header.firstRowIndex != currentRowIndex_) {
            if (mode_ == ReaderMode::STRICT) {
                throw std::runtime_error("Error: Packet firstRowIndex mismatch");
            }
            std::cerr << "Warning: Packet firstRowIndex mismatch, expected " 
                      << currentRowIndex_ << " got " << header.firstRowIndex << "\n";
        }
        
        // Initialize LZ4 decompression if needed
        if (compressionLevel() > 0) {
            lz4Stream_.emplace();
        } else {
            lz4Stream_.reset();
        }
        
        // Reset payload hasher for checksum validation
        packetHash_.reset();
        
        return true;
    }

    /**
     * @brief Read next row from current packet
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readNext() {
        // Check if we're at end of file
        if (currentRowIndex_ >= totalRowCount_) {
            return false;
        }
        
        // Determine which packet this row belongs to
        if (currentPacketIndex_ >= fileIndex_.packetCount()) {
            return false;
        }
        
        const auto& currentPacket = fileIndex_.getPacket(currentPacketIndex_);
        
        // Check if current row is in next packet
        uint64_t nextPacketFirstRow = (currentPacketIndex_ + 1 < fileIndex_.packetCount()) 
            ? fileIndex_.getPacket(currentPacketIndex_ + 1).firstRow_
            : totalRowCount_;
        
        if (currentRowIndex_ >= nextPacketFirstRow) {
            // Move to next packet
            currentPacketIndex_++;
            if (!openPacket()) {
                return false;
            }
        } else if (currentRowIndex_ == currentPacket.firstRow_) {
            // First row of current packet - need to open it
            if (!openPacket()) {
                return false;
            }
        }
        
        // Read VLE size
        size_t vleValue;
        try {
            vleValue = vle_decode<size_t>(stream_);
        } catch (const std::exception& ex) {
            if (mode_ == ReaderMode::STRICT) {
                throw std::runtime_error(std::string("Error reading VLE: ") + ex.what());
            }
            std::cerr << "Warning: Failed to read VLE: " << ex.what() << "\n";
            return false;
        }
        
        // Update payload checksum with VLE
        uint8_t vleBytes[10];
        std::span<uint8_t> vleSpan(vleBytes, 10);
        size_t vleSize = vle_encode(vleValue, vleSpan);
        packetHash_.update(vleBytes, vleSize);
        
        if (vleValue == 0) {
            // ZoH repeat: reuse bufferCurrentRow_ (no read/decompress needed)
            // Just deserialize from existing buffer
            if (!row_.deserializeFrom({bufferRawRow_.data(), bufferRawRow_.size()})) {
                if (mode_ == ReaderMode::STRICT) {
                    throw std::runtime_error("Error: Failed to deserialize ZoH row");
                }
                return false;
            }
            currentRowIndex_++;
            return true;
            
        } else if (vleValue == 1) {
            // Terminator: move to next packet
            currentPacketIndex_++;
            if (currentPacketIndex_ >= fileIndex_.packetCount()) {
                return false; // No more packets
            }
            if (!openPacket()) {
                return false;
            }
            // Recursively read first row of next packet
            return readNext();
            
        } else {
            // Regular row: vleValue = dataSize + 1
            size_t dataSize = vleValue - 1;
            
            // Read compressed data
            ByteBuffer compressedData(dataSize);
            stream_.read(reinterpret_cast<char*>(compressedData.data()), dataSize);
            
            if (!stream_ || stream_.gcount() != static_cast<std::streamsize>(dataSize)) {
                if (mode_ == ReaderMode::STRICT) {
                    throw std::runtime_error("Error: Failed to read row data");
                }
                return false;
            }
            
            // Update payload checksum
            packetHash_.update(compressedData.data(), dataSize);
            
            // Decompress if needed
            if (lz4Stream_.has_value()) {
                // Estimate expected size (conservative: 2x compressed size)
                size_t estimatedSize = dataSize * 2;
                if (estimatedSize > bufferRawRow_.capacity()) {
                    bufferRawRow_.reserve(estimatedSize);
                }
                bufferRawRow_.resize(bufferRawRow_.capacity());
                
                std::span<const uint8_t> input(
                    reinterpret_cast<const uint8_t*>(compressedData.data()),
                    compressedData.size()
                );
                std::span<uint8_t> output(
                    reinterpret_cast<uint8_t*>(bufferRawRow_.data()),
                    bufferRawRow_.size()
                );
                
                int decompressedSize = lz4Stream_->decompress(input, output, static_cast<int>(output.size()));
                
                if (decompressedSize < 0) {
                    if (mode_ == ReaderMode::STRICT) {
                        throw std::runtime_error("Error: LZ4 decompression failed");
                    }
                    std::cerr << "Warning: LZ4 decompression failed, trying uncompressed\n";
                    // Fall back to uncompressed
                    bufferRawRow_ = compressedData;
                } else {
                    bufferRawRow_.resize(decompressedSize);
                }
            } else {
                // No compression
                bufferRawRow_ = compressedData;
            }
            
            // Deserialize row
            if (!row_.deserializeFrom({bufferRawRow_.data(), bufferRawRow_.size()})) {
                if (mode_ == ReaderMode::STRICT) {
                    throw std::runtime_error("Error: Failed to deserialize row");
                }
                return false;
            }
            
            currentRowIndex_++;
            return true;
        }
    }

} // namespace bcsv