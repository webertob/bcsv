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
            if(!readHeader()) {
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

    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readHeader() {
        // Write the header information to the stream
        if (!stream_.is_open()) {
            return false;
        }
        layout_->unlock(this);
        FileHeader fileHeader;
        if(fileHeader.readFromBinary(stream_, *layout_)) {
            layout_->lock(this);
        } else {
            return false;
        }
        row_cnt_ = 0;
        return true;
    }

    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readPacket() {
        if (!stream_.is_open()) {
            return false;
        }

        // Read packet header (compressed and uncompressed sizes)
        PacketHeader header;
        stream_.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!stream_.good()) {
            return false;
        }

        if(!header.validate()) {
            return false;
        }

        //read row offsets (by definition 1st offset is always 0)
        row_offsets_.resize(header.rowCount);
        row_offsets_[0] = 0;
        //read the remaining offsets
        constexpr size_t offset_size = sizeof(row_offsets_.value_type);
        stream_.read(reinterpret_cast<char*>(row_offsets_.data() + offset_size), (row_offsets_.size() - 1) * offset_size);

        // Read the compressed packet data
        buffer_zip_.resize(header.payloadSizeZip);
        stream_.read(buffer_zip_.data(), buffer_zip_.size());
        if (!stream_.good()) {
            return false;
        }

        // validate CRC
        if(!header.validateCRC32(row_offsets_, buffer_zip_)) {
            throw std::runtime_error("Error: Packet CRC32 validation failed");
        }

        // Decompress the packet data
        buffer_raw_.resize(header.payloadSizeRaw);
        int decompressedSize = LZ4_decompress_safe(buffer_zip_.data(), buffer_raw_.data(), buffer_zip_.size(), buffer_raw_.size());
        if (decompressedSize < 0 || static_cast<size_t>(decompressedSize) != header.payloadSizeRaw) {
            throw std::runtime_error("Error: LZ4 decompression failed");
        }

        // Update row count
        row_cnt_old_ = row_cnt_;
        row_cnt_ += header.rowCount;
        return true;
    }

    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readRow(LayoutType::RowViewType& row) {
        size_t rows_remaining = row_cnt_old_ - row_cnt_;
        if(rows_remaining == 0) {
            if(!readPacket()) {
                return false;
            }
            rows_remaining = row_cnt_old_ - row_cnt_;
            if(rows_remaining == 0) {
                return false; // No more data
            }
        }

        size_t row_index  = row_offsets_.size() - rows_remaining - 1;
        size_t row_offset = row_offsets_[row_index];
        size_t row_length = rows_remaining > 1 ? row_offsets_[row_index + 1] - row_offset : buffer_raw_.size() - row_offset;
        --rows_remaining;
        row.setBuffer(buffer_raw_.data() + row_offset, row_length);
        return row.validate();

    }

} // namespace bcsv