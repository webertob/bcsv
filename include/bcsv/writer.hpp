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
#include <type_traits>

namespace bcsv {

    template<typename LayoutType>
    Writer<LayoutType>::Writer(std::shared_ptr<LayoutType> &layout) : layout_(layout) {
        buffer_raw_.reserve(LZ4_BLOCK_SIZE_KB * 1024);
        buffer_zip_.reserve(LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE_KB * 1024));

        static_assert(std::is_base_of_v<LayoutInterface, LayoutType>, "LayoutType must derive from LayoutInterface");
        if (!layout_) {
            throw std::runtime_error("Error: Layout is not initialized");
        }
    }

    template<typename LayoutType>
    Writer<LayoutType>::Writer(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath, bool overwrite) 
        : Writer(layout) {
        open(filepath, overwrite);
    }


    template<typename LayoutType>
    Writer<LayoutType>::~Writer() {
        if (is_open()) {
            close();
        }
        layout_->unlock(this);
    }

     /**
     * @brief Close the binary file
     */
    template<typename LayoutType>
    void Writer<LayoutType>::close() {
        if (stream_.is_open()) {
            flush();
            stream_.close();
            filePath_.clear();
            layout_->unlock(this);
        }
    }

    template<typename LayoutType>
    void Writer<LayoutType>::flush() {
        if (stream_.is_open()) {
            stream_.flush();
        }
    }

     /**
     * @brief Open a binary file for writing with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @param overwrite Whether to overwrite existing files (default: false)
     * @return true if file was successfully opened, false otherwise
     */
    template<typename LayoutType>
    bool Writer<LayoutType>::open(const std::filesystem::path& filepath, bool overwrite = false) {
        if(is_open()) {
            std::cerr << "Warning: File is already open: " << filePath_ << std::endl;
            return false;
        }

        try {
            // Convert to absolute path for consistent handling
            std::filesystem::path absolutePath = std::filesystem::absolute(filepath);
            
            // Check if parent directory exists, create if needed
            std::filesystem::path parentDir = absolutePath.parent_path();
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
            if (!stream_.is_open()) {
                throw std::runtime_error("Error: Cannot open file for writing: " + absolutePath.string() +
                                           " (Check permissions and disk space)");
            }

            // Store file path
            filePath_ = absolutePath;
            writeHeader();
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
        
    template<typename LayoutType>
    bool Writer<LayoutType>::writeHeader() {
        // Write the header information to the stream
        if (!stream_.is_open()) {
            return false;
        }
        layout_->lock(this);
        FileHeader fileHeader;
        fileHeader.writeToBinary(stream_, *layout_);
        currentRowIndex_ = 0;
        return true;
    }

    template<typename LayoutType>
    bool Writer<LayoutType>::writeRow(const Row& row) {
        if (!stream_.is_open()) {
            return false;
        }
        
        if (!headerWritten_) {
            if (!writeHeader()) {
                return false;
            }
            headerWritten_ = true;
        }
        
        // Write row data
        const auto& values = row.getValues();
        if (values.size() != layout_->getColumnCount()) {
            return false;
        }
        
        for (size_t i = 0; i < values.size(); ++i) {
            const auto& value = values[i];
            ColumnDataType colType = layout_->getColumnType(i);
            
            // Write the value based on its type
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    uint32_t len = static_cast<uint32_t>(v.length());
                    stream_.write(reinterpret_cast<const char*>(&len), sizeof(len));
                    stream_.write(v.c_str(), len);
                } else {
                    stream_.write(reinterpret_cast<const char*>(&v), sizeof(v));
                }
            }, value);
        }
        
        currentRowIndex_++;
        return stream_.good();
    }

} // namespace bcsv