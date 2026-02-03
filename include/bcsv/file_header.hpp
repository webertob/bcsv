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
 * @file file_header.hpp
 * @brief Binary CSV (BCSV) Library - FileHeader implementations
 * 
 * This file contains the basic implementations for the FileHeader class.
 * Methods requiring full ColumnLayout definition are in bcsv.hpp.
 */

#include <algorithm>
#include "file_header.h"

namespace bcsv {

    inline FileHeader::FileHeader(size_t columnCount, size_t compressionLevel, uint8_t major, uint8_t minor, uint8_t patch) {
        constSection_.magic = BCSV_MAGIC;
        constSection_.versionMajor = major;
        constSection_.versionMinor = minor;
        constSection_.versionPatch = patch;
        constSection_.compressionLevel = static_cast<uint8_t>( std::min(compressionLevel, size_t(9)) ); // Clamp to 0-9
        constSection_.flags = 0;             // All flags reserved for future use
        constSection_.columnCount = static_cast<uint16_t>( std::min(columnCount, MAX_COLUMN_COUNT));
        constSection_.packetSize  = 8 * 1024 * 1024;  // Default 8MB packet size
        if (constSection_.columnCount == MAX_COLUMN_COUNT) {
            std::cerr << "Warning: Maximum column count exceeded. Some columns may be ignored." << std::endl;
        }
    }          

    inline void FileHeader::setFlag(FileFlags flag, bool value) {
        if (value) {
            constSection_.flags |= static_cast<uint16_t>(flag);
        } else {
            constSection_.flags &= ~static_cast<uint16_t>(flag);
        }
    }

    // FileHeader method implementations that require ColumnLayout definition
    template<LayoutConcept LayoutType>
    inline size_t FileHeader::getBinarySize(const LayoutType& layout) {
        size_t size = sizeof(ConstSection);                    // Fixed header
        size += layout.columnCount() * sizeof(uint16_t);        // Column data types
        size += layout.columnCount() * sizeof(uint16_t);        // Column name lengths
        for (size_t i = 0; i < layout.columnCount(); ++i) {     // Column names
            size += layout.columnName(i).length();
        }
        return size;
    }

    template<LayoutConcept LayoutType>
    inline bool FileHeader::writeToBinary(std::ostream& stream, const LayoutType& layout) {
        // Update header with current column count
        constSection_.columnCount = static_cast<uint16_t>(layout.columnCount());

        // Write fixed header
        stream.write(reinterpret_cast<const char*>(&constSection_), sizeof(constSection_));
        if (!stream.good()) return false;

        // Write column data types
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            const uint16_t val = static_cast<uint16_t>(layout.columnType(i));
            stream.write(reinterpret_cast<const char*>(&val), sizeof(val));
            if (!stream.good()) return false;
        }

        // Write column name lengths
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            uint16_t val = static_cast<uint16_t>(layout.columnName(i).length());
            stream.write(reinterpret_cast<const char*>(&val), sizeof(val));
            if (!stream.good()) return false;
        }

        // Write column names (without null terminator)
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            const std::string& name = layout.columnName(i);
            if (!name.empty()) {
                stream.write(name.c_str(), name.length());
                if (!stream.good()) return false;
            }
        }

        return true;
    }

    // Specialized version for Layout - modifies the layout to match binary data
    inline bool FileHeader::readFromBinary(std::istream& stream, Layout& columnLayout) {
        try {
            // Clear the layout first to ensure clean state on failure
            columnLayout.clear();
            
            // Read fixed header
            stream.read(reinterpret_cast<char*>(&constSection_), sizeof(constSection_));
            if (!stream.good()) {
                throw std::runtime_error("Failed to read BCSV header from stream");
            }
            
            if (!isValidMagic()) {
                throw std::runtime_error("Invalid magic number in BCSV header. Expected: 0x" + 
                                       std::to_string(BCSV_MAGIC) + ", Got: 0x" + std::to_string(constSection_.magic));
            }
            
            // Validate column count
            if (constSection_.columnCount > MAX_COLUMN_COUNT) {
                throw std::runtime_error("Column count (" + std::to_string(constSection_.columnCount) + 
                                       ") exceeds maximum limit (" + std::to_string(MAX_COLUMN_COUNT) + ")");
            }

            // Read column data types
            std::vector<ColumnDefinition> columnDefinitions(constSection_.columnCount);
            for (uint16_t i = 0; i < constSection_.columnCount; ++i) {
                uint16_t typeValue;
                stream.read(reinterpret_cast<char*>(&typeValue), sizeof(typeValue));
                if (!stream.good()) {
                    throw std::runtime_error("Failed to read column data type at index " + std::to_string(i));
                }
                columnDefinitions[i].type = static_cast<ColumnType>(typeValue);
            }

            // Read column name lengths
            std::vector<uint16_t> nameLengths(constSection_.columnCount);
            for (uint16_t i = 0; i < constSection_.columnCount; ++i) {
                stream.read(reinterpret_cast<char*>(&nameLengths[i]), sizeof(uint16_t));
                if (!stream.good()) {
                    throw std::runtime_error("Failed to read column name length at index " + std::to_string(i));
                }
                
                // Validate name length
                if (nameLengths[i] > MAX_STRING_LENGTH) {
                    throw std::runtime_error("Column name length (" + std::to_string(nameLengths[i]) + 
                                           ") exceeds maximum (" + std::to_string(MAX_STRING_LENGTH) + 
                                           ") at index " + std::to_string(i));
                }
            }

            // Read column names
            for (uint16_t i = 0; i < constSection_.columnCount; ++i) {
                if (nameLengths[i] > 0) {
                    std::vector<char> nameBuffer(nameLengths[i]);
                    stream.read(nameBuffer.data(), nameLengths[i]);
                    if (!stream.good()) {
                        throw std::runtime_error("Failed to read column name at index " + std::to_string(i));
                    }
                    columnDefinitions[i].name = std::string(nameBuffer.begin(), nameBuffer.end());
                } else {
                    columnDefinitions[i].name = "";
                }
            }

            // Populate the layout with the deserialized data
            columnLayout.setColumns(columnDefinitions);
            return true;
            
        } catch (const std::exception&) {
            // Ensure layout is cleared on any failure
            columnLayout.clear();
            throw; // Re-throw the exception
        }
    }

    // Specialized version for LayoutStatic - validates that binary matches static definition
    template<typename... ColumnTypes>
    bool FileHeader::readFromBinary(std::istream& stream, LayoutStaticBase<ColumnTypes...>& layout) {
        // Read fixed header
        stream.read(reinterpret_cast<char*>(&constSection_), sizeof(constSection_));
        if (!stream.good()) {
            std::cerr << "error: Failed to read BCSV header from stream" << std::endl;
            return false;
        }
        
        if (!isValidMagic()) {
            std::cerr << "error: Invalid magic number in BCSV header. Expected: 0x" << std::hex << BCSV_MAGIC << ", Got: 0x" << std::hex << constSection_.magic << std::endl;
            return false;
        }
        
        // Validate column count matches static definition
        if (constSection_.columnCount != layout.columnCount()) {
            std::cerr << "error: Column count mismatch. Static layout expects " << layout.columnCount() << " columns, but binary has " << constSection_.columnCount << " columns" << std::endl;
            return false;
        }
        if (constSection_.columnCount > MAX_COLUMN_COUNT) {
            std::cerr << "error: Column count (" << constSection_.columnCount << ") exceeds maximum limit (" << MAX_COLUMN_COUNT << ")" << std::endl;
            return false;
        }   

        // Read column data types and validate against static definition
        for (uint16_t i = 0; i < layout.columnCount(); ++i) {
            ColumnType type;
            stream.read(reinterpret_cast<char*>(&type), sizeof(type));
            if (!stream.good()) {
                std::cerr << "error: Failed to read column data type at index " << std::to_string(i) << std::endl;
                return false;
            }
            if (type != layout.columnType(i)) {
                std::cerr << "error: Column type mismatch at index " << std::to_string(i) << 
                            ". Static layout expects " << toString(layout.columnType(i)) << 
                            ", but binary has " << toString(type) << std::endl;
                return false;
            }
        }

        // Read column name lengths (we must read them to advance the stream)
        std::vector<uint16_t> nameLengths(layout.columnCount());
        for (uint16_t i = 0; i < layout.columnCount(); ++i) {
            stream.read(reinterpret_cast<char*>(&nameLengths[i]), sizeof(uint16_t));
            if (!stream.good()) {
                std::cerr << "error: Failed to read column name length at index " << std::to_string(i) << std::endl;
                return false;
            }
            // Validate name length
            if (nameLengths[i] > MAX_STRING_LENGTH) {
                std::cerr << "error: Column name length (" << std::to_string(nameLengths[i]) << 
                            ") exceeds maximum (" << std::to_string(MAX_STRING_LENGTH) << 
                            ") at index " << std::to_string(i) << std::endl;
                return false;
            }
        }

        // Read column names and optionally validate against static definition
        std::vector<char> nameBuffer;
        for (uint16_t i = 0; i < layout.columnCount(); ++i) {
            if (nameLengths[i] > 0) {
                nameBuffer.resize(nameLengths[i]);
                stream.read(nameBuffer.data(), nameLengths[i]);
                if (!stream.good()) {
                    std::cerr << "error: Failed to read column name at index " << std::to_string(i) << std::endl;
                    return false;
                }
                layout.setColumnName(i, std::string(nameBuffer.begin(), nameBuffer.end()));
            } else {
                layout.setColumnName(i, "Column_" + std::to_string(i));
            }
        }
        return true;
    }

    template<LayoutConcept LayoutType>
    inline void FileHeader::printBinaryLayout(const LayoutType& layout) const {
        std::cout << "FileHeader Binary Layout (" << getBinarySize(layout) << " bytes):\n";
        std::cout << "  Magic:       0x" << std::hex << constSection_.magic << std::dec << " (" << sizeof(constSection_.magic) << " bytes)\n";
        std::cout << "  Version:     " << static_cast<int>(constSection_.versionMajor) << "." 
                  << static_cast<int>(constSection_.versionMinor) << "." 
                  << static_cast<int>(constSection_.versionPatch) << " (3 bytes)\n";
        std::cout << "  Compression: " << static_cast<int>(constSection_.compressionLevel) << " (1 byte)\n";
        std::cout << "  Flags:       0x" << std::hex << constSection_.flags << std::dec << " (2 bytes)\n";
        std::cout << "  Columns:     " << static_cast<uint16_t>(layout.columnCount()) << " (2 bytes)\n";
        std::cout << "  Column Data Types: " << layout.columnCount() * sizeof(uint16_t) << " bytes\n";
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            std::cout << "    [" << i << "]: " << static_cast<uint16_t>(layout.columnType(i)) << "\n";
        }
        std::cout << "  Column Name Lengths: " << layout.columnCount() * sizeof(uint16_t) << " bytes\n";
        size_t totalNameBytes = 0;
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            std::cout << "    [" << i << "]: " << layout.columnName(i).length() << " bytes\n";
            totalNameBytes += layout.columnName(i).length();
        }
        std::cout << "  Column Names: " << totalNameBytes << " bytes\n";
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            std::cout << "    [" << i << "]: \"" << layout.columnName(i) << "\"\n";
        }
    }

} // namespace bcsv
