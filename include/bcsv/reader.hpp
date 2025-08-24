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

    template<typename LayoutType>
    Reader<LayoutType>::Reader(std::shared_ptr<LayoutType> &layout) : layout_(layout) {
        buffer_raw_.reserve(LZ4_BLOCK_SIZE_KB * 1024);
        buffer_zip_.reserve(LZ4_COMPRESSBOUND(LZ4_BLOCK_SIZE_KB * 1024));

        static_assert(std::is_base_of_v<LayoutInterface, LayoutType>, "LayoutType must derive from LayoutInterface");
        if (!layout_) {
            throw std::runtime_error("Error: Layout is not initialized");
        }
    }

    template<typename LayoutType>
    Reader<LayoutType>::Reader(std::shared_ptr<LayoutType> &layout, const std::filesystem::path& filepath) 
        : Reader(layout) {
        open(filepath);
    }

    template<typename LayoutType>
    Reader<LayoutType>::~Reader() {
        if (is_open()) {
            close();
        }
        layout_->unlock(this);
    }

    /**
     * @brief Close the binary file
     */
    template<typename LayoutType>
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
    template<typename LayoutType>
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
            return false;
        } catch (const std::exception& ex) {
            std::cerr << "Error opening file: " << ex.what() << std::endl;
            return false;
        }

        return false;
    }

    template<typename LayoutType>
    bool Reader<LayoutType>::readHeader() {
        // Write the header information to the stream
        if (!stream_.is_open()) {
            return false;
        }
        layout_->unlock(this);
        FileHeader fileHeader;
        if(fileHeader.readFromBinary(stream_, *layout_)) {
            // need to ensure layout does not change during reading the rest of the file.
            layout_->lock(this);
        } else {
            return false;
        }
        currentRowIndex_ = 0;
        return true;
    }

    template<typename LayoutType>
    bool Reader<LayoutType>::readRow(Row& row) {
        if (!stream_.is_open() || stream_.eof()) {
            return false;
        }
        
        std::vector<FieldValue> values;
        values.reserve(layout_->getColumnCount());
        
        for (size_t i = 0; i < layout_->getColumnCount(); ++i) {
            ColumnDataType colType = layout_->getColumnType(i);
            
            switch (colType) {
                case ColumnDataType::STRING: {
                    uint32_t len;
                    if (!stream_.read(reinterpret_cast<char*>(&len), sizeof(len))) {
                        return false;
                    }
                    std::string str(len, '\0');
                    if (!stream_.read(str.data(), len)) {
                        return false;
                    }
                    values.emplace_back(std::move(str));
                    break;
                }
                case ColumnDataType::INT8: {
                    int8_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::INT16: {
                    int16_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::INT32: {
                    int32_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::INT64: {
                    int64_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::UINT8: {
                    uint8_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::UINT16: {
                    uint16_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::UINT32: {
                    uint32_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::UINT64: {
                    uint64_t val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::FLOAT: {
                    float val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::DOUBLE: {
                    double val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                case ColumnDataType::BOOL: {
                    bool val;
                    if (!stream_.read(reinterpret_cast<char*>(&val), sizeof(val))) {
                        return false;
                    }
                    values.emplace_back(val);
                    break;
                }
                default:
                    return false;
            }
        }
        
        row.setValues(values);
        return stream_.good();
    }

} // namespace bcsv