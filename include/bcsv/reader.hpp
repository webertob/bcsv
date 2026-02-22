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

#include "bcsv/file_footer.h"
#include "reader.h"
#include "file_header.h"
#include "layout.h"
#include "packet_header.h"
#include "definitions.h"
#include "vle.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <sstream>
#include <span>

namespace bcsv {

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader() 
    : err_msg_()
    , file_header_()
    , file_path_()
    , stream_()
    , lz4_stream_()
    , packet_hash_()
    , packet_open_(false)
    , packet_pos_()
    , row_pos_(0)
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

        file_path_.clear();
        stream_.close();
        lz4_stream_.reset();
        packet_hash_.reset();
        packet_pos_ = 0;
        row_pos_ = 0;
        row_buffer_.clear();
        row_buffer_.shrink_to_fit();
        row_.clear();
        packet_open_ = false;
    }

    /**
     * @brief Open a binary file for reading with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::open(const FilePath& filepath) {
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
            
            file_path_ = absolutePath;
            
            // Read file header
            if(!readFileHeader()) {
                throw std::runtime_error("Failed to read file header");
            }

            // create LZ4 decompression stream if needed
            if (compressionLevel() > 0) {
                lz4_stream_.emplace();
            }

            // Open the first packet to prepare for reading rows
            packet_open_ = openPacket();
            row_pos_ = 0;

        } catch (const std::exception& ex) {
            err_msg_ = ex.what();
            if (stream_.is_open()) {
                stream_.close();
            }
            file_path_.clear();
            return false;
        }
        return true;
    }

    /**
     * @brief Read file header
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readFileHeader() {
        if (!stream_) {
            err_msg_ = "Error: Stream is not open";
            return false;
        }
        
        try {
            LayoutType layout;
            file_header_.readFromBinary(stream_, layout);
            
            // Check version compatibility (v1.3.0 only)
            if (file_header_.versionMajor() != BCSV_FORMAT_VERSION_MAJOR || 
                file_header_.versionMinor() != BCSV_FORMAT_VERSION_MINOR) {
                std::ostringstream oss;
                oss << "Error: Incompatible file version: "
                    << static_cast<int>(file_header_.versionMajor()) << "."
                    << static_cast<int>(file_header_.versionMinor())
                    << " (Expected: " << static_cast<int>(BCSV_FORMAT_VERSION_MAJOR) << "." 
                    << static_cast<int>(BCSV_FORMAT_VERSION_MINOR) << ")";
                err_msg_ = oss.str();
                if constexpr (DEBUG_OUTPUTS) {
                    std::cerr << err_msg_ << "\n";
                }
                return false;
            }
            
            row_ = RowType(layout);

            // Initialize codec dispatch — selects Flat001 or ZoH001 based on
            // file flags. Codec is selected at file-open time (Item 11 §7).
            codec_.selectCodec(file_header_.getFlags(), row_.layout());

            return true;
            
        } catch (const std::exception& ex) {
            err_msg_ = std::string("Error: Failed to read file header: ") + ex.what();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << "\n";
            }
            return false;
        }
    }

    template<LayoutConcept LayoutType>
    void Reader<LayoutType>::closePacket() {
        assert(stream_);
         
        // Finalize and validate packet checksum
        uint64_t expectedHash = 0;
        stream_.read(reinterpret_cast<char*>(&expectedHash), sizeof(expectedHash));
        if (!stream_ || stream_.gcount() != sizeof(expectedHash)) {
            throw std::runtime_error("Error: Failed to read packet checksum");
        }
        
        uint64_t calculatedHash = packet_hash_.finalize();
        if (calculatedHash != expectedHash) {
            throw std::runtime_error("Error: Packet checksum mismatch");
        }
    }

    /**
     * @brief Open next packet for sequential reading
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::openPacket() {
        assert(stream_);

        packet_pos_ = stream_.tellg();
        PacketHeader header;
        bool success = header.read(stream_, true);

        // Initialize LZ4 decompression if needed
        if (lz4_stream_.has_value()) {
            lz4_stream_->reset();
        }
        
        // Reset payload hasher for checksum validation
        packet_hash_.reset();

        // Reset codec state at packet boundary (Item 11)
        codec_.reset();

        if (!success) {
            if(header.magic == FOOTER_BIDX_MAGIC) {
                stream_.clear();           // clear fail state
                stream_.seekg(packet_pos_); // reset to previous position
                return false;   // end of file reached, normal exit condition
            } else if(stream_.eof()) {
                return false;   // end of file reached, normal exit condition
            } else {
                stream_.clear();           // clear fail state
                stream_.seekg(packet_pos_); // reset to previous position
                throw std::runtime_error("Error: Failed to read packet header");
            }
        }
        return true;
    }

    /**
     * @brief Read next row from current packet
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readNext() {
        if (!isOpen() || !packet_open_) {
            return false;
        }

        if (!stream_ || !stream_.good()) {
            return false;
        }

        //reade row length
        size_t rowLen;
        vleDecode<uint64_t, true>(stream_, rowLen, &packet_hash_);

        // check for terminator
        while (rowLen == PCKT_TERMINATOR) {
            // End of packet reached
            closePacket();
            packet_open_ = openPacket();
            if(!packet_open_) {
                return false;
            }
            vleDecode<uint64_t, true>(stream_, rowLen, &packet_hash_);
        }

        if (rowLen == 0) {
            // repeat previous row
            if(!file_header_.hasFlag(FileFlags::ZERO_ORDER_HOLD) && row_.layout().columnCount() > 0) 
            {
                throw std::runtime_error("Error: ZERO_ORDER_HOLD flag not set, but repeat row encountered");
            }

            if( row_buffer_.empty() && row_.layout().columnCount() > 0 ) 
            {
                throw std::runtime_error("Error: Cannot repeat previous row, no previous row data available");
            }
            row_pos_++;
            return true;
        }

        // read row data
        if (rowLen > MAX_ROW_LENGTH) [[unlikely]] {
            throw std::runtime_error("Error: Row length exceeds MAX_ROW_LENGTH (" + std::to_string(rowLen) + " > " + std::to_string(MAX_ROW_LENGTH) + ")");
        }
        row_buffer_.resize(rowLen);
        stream_.read(reinterpret_cast<char*>(row_buffer_.data()), rowLen);
        if (!stream_ || stream_.gcount() != static_cast<std::streamsize>(rowLen)) {
            throw std::runtime_error("Error: Failed to read row data");
        }
        packet_hash_.update(row_buffer_);
        std::span<const std::byte> rowRawData(row_buffer_);

        // decompress if needed
        if(lz4_stream_.has_value()) {
            rowRawData = lz4_stream_->decompress(row_buffer_);
        }

        // deserialize row via codec (Item 11)
        codec_.deserialize(rowRawData, row_);
        row_pos_++;
        return true;        
    }

    template<LayoutConcept LayoutType>
    void ReaderDirectAccess<LayoutType>::close() {
        Base::close();
        file_footer_.clear();
    }

    template<LayoutConcept LayoutType>
    bool ReaderDirectAccess<LayoutType>::open(const FilePath& filepath, bool rebuildFooter ) {
        // open file as normal
        if(!Base::open(filepath))
            return false;

        std::streampos originalPos = Base::stream_.tellg();

        // additionally read file footer
        try {
            if(!file_footer_.read(Base::stream_)) {
                if(!rebuildFooter) {
                    Base::err_msg_ = "Error: FileFooter missing or invalid (use rebuildFooter=true to reconstruct)";
                    if constexpr (DEBUG_OUTPUTS) {
                        std::cerr << Base::err_msg_ << "\n";
                    }
                    Base::stream_.seekg(originalPos);
                    return false;
                } else {
                    Base::err_msg_ = "Warning: FileFooter missing or invalid, attempting to rebuild index";
                    if constexpr (DEBUG_OUTPUTS) {
                        std::cerr << Base::err_msg_ << "\n";
                    }
                    buildFileFooter();
                    Base::err_msg_.clear();  // Clear warning after successful rebuild
                }
            }
        } catch (const std::exception& ex) {
            Base::err_msg_ = std::string("Error: Exception reading FileFooter: ") + ex.what();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << Base::err_msg_ << std::endl;
            }
            Base::stream_.seekg(originalPos);
            return false;
        }

        // restore original position after reader::open(), as expected by the default reader(i.e. ::readNext()).
        Base::stream_.seekg(originalPos);
        return true;
    }

    template<LayoutConcept LayoutType>
    void ReaderDirectAccess<LayoutType>::buildFileFooter() {
        assert(Base::stream_);
        file_footer_.clear();

        // Store original position
        std::streampos originalPos = Base::stream_.tellg();

        // Jump to first packet (after file header + layout)
        Base::stream_.seekg(Base::file_header_.getBinarySize(Base::layout()), std::ios::beg);

        // Decision: We build a sequential algorithms to rebuild the file footer. 
        // ToDo: A parallel one would be perfectly possible, but put this to the future

        // Goal build the FileFooter
        // FileFooter contains
        //  1. packet index: 
        //      a. contains one entry per package in the file
        //      b. each entry contaits the number/count of the first row and the position in the file stream 
        //      c. index is ordered (position and row count follow same order)
        //  2. totalRow count in file
        //      a. first row of last package + row count in last package = number of total rows

        // Algorithm (build packet index):
        // 1. Start at first packet position
        // 2. Read packet header
        // 3. Store packet position and first row in file footer index
        // 4. Skip packet payload (using block size form file header as estimate)
        // 5. Repeat until end of file reached

        PacketHeader header;
        std::streampos pktPos = Base::stream_.tellg();
        std::streamoff stepSize = static_cast<std::streamoff>(Base::file_header_.getPacketSize() + sizeof(PacketHeader));
        while (header.readNext(Base::stream_, pktPos)) {
            file_footer_.packetIndex().emplace_back(
                static_cast<uint64_t>(pktPos),
                header.first_row_index
            );

            // Skip packet payload (estimate using block size)
            Base::stream_.seekg(pktPos + stepSize);
        }


        // Algorithm (count total rows):
        // 1. Seek to last packet position (from packet index)
        // 2. skip packet header
        // 3. Read row sizes (skip row data) until terminator reached, count rows
        // 4. total rows = first row of last packet + counted rows in last packet
        if (file_footer_.packetIndex().empty()) {
            file_footer_.rowCount() = 0;
        } else {
            const auto& lastPacket = file_footer_.packetIndex().back();

            // Seek to last packet payload (skip header) and count rows by jumping through VLE sizes
            Base::stream_.clear();
            Base::stream_.seekg(static_cast<std::streamoff>(lastPacket.byte_offset) + sizeof(PacketHeader));

            size_t rowCntLstPkt = 0;
            size_t rowLen;

            // Count rows within last packet using row lengths
            while (Base::stream_) {
                try {
                    vleDecode<uint64_t, true>(Base::stream_, rowLen, nullptr);

                    if (rowLen == 0) {
                        // ZoH repeat - no payload, just count
                        rowCntLstPkt++;
                    } else if (rowLen == PCKT_TERMINATOR) {
                        // Terminator - end of packet
                        break;
                    } else {
                        // Regular row: rowLen = dataSize
                        size_t dataSize = rowLen;

                        // Skip over the payload data
                        Base::stream_.seekg(static_cast<std::streamoff>(dataSize), std::ios::cur);

                        if (Base::stream_) {
                            rowCntLstPkt++;
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
            file_footer_.rowCount() = lastPacket.first_row + rowCntLstPkt;
        }
        
        // Restore original position
        Base::stream_.seekg(originalPos);
    }

} // namespace bcsv