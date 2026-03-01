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
        
        // Finalize file codec: close last packet, write footer (if packet-based)
        if (file_codec_.isSetup()) {
            file_codec_.finalize(stream_, row_cnt_);
        }
        
        stream_.close();
        row_codec_ = CodecType();  // Move-assign default; old codec's destructor releases the structural lock
        file_codec_.destroy();
        file_path_.clear();
        row_cnt_ = 0;       
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::flush() {
        if (!stream_.is_open()) {
            return;
        }
        // Close the current packet (terminator + checksum), flush the OS stream,
        // then open a new packet for subsequent writes.  Row codec is reset at
        // the packet boundary so ZoH/Delta encoders restart cleanly.
        if (file_codec_.isSetup()) {
            if (file_codec_.flushPacket(stream_, row_cnt_)) {
                row_codec_.reset();
            }
        } else {
            stream_.flush();
        }
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
            file_header_.setFlags(flags | RowCodecFileFlags<CodecType>::value);
            file_header_.setPacketSize(std::clamp(blockSizeKB*1024, size_t(MIN_PACKET_SIZE), size_t(MAX_PACKET_SIZE)));  // limit packet size to 64KB-1GB
            file_header_.writeToBinary(stream_, layout());
            row_cnt_ = 0;

            // Initialize file-level codec (framing, compression, checksums)
            file_codec_.select(static_cast<uint8_t>(compressionLevel), flags);
            file_codec_.setupWrite(stream_, file_header_);

            row_.clear();

            // Initialize row codec (Item 11)
            row_codec_.setup(layout());
            row_codec_.reset();

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
    void Writer<LayoutType, CodecType>::write(const RowType& row) {
        row_ = row;
        writeRow();
    }

    template<LayoutConcept LayoutType, typename CodecType>
    void Writer<LayoutType, CodecType>::writeRow() {
        if (!stream_.is_open()) {
            throw std::runtime_error("Error: File is not open");
        }

        // Packet lifecycle: beginWrite handles close-if-full → open-if-needed.
        // Returns true when a packet boundary was crossed → reset RowCodec.
        if (file_codec_.beginWrite(stream_, row_cnt_)) {
            row_codec_.reset();
        }

        // 1. Serialize row to codec's internal buffer via row codec
        auto& buf = file_codec_.writeBuffer();
        buf.clear();
        std::span<std::byte> actRow = row_codec_.serialize(row_, buf);

        // 2. Write row via file codec (handles VLE, compression, checksum, I/O)
        file_codec_.writeRow(stream_, actRow);

        row_cnt_++;
    }

} // namespace bcsv