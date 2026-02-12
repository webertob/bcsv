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
#include <vector>
#include "bcsv/definitions.h"
#include "file_header.h"

namespace bcsv {

    inline FileHeader::FileHeader(size_t columnCount, size_t compressionLevel, uint8_t major, uint8_t minor, uint8_t patch) {
        const_section_.magic = BCSV_MAGIC;
        const_section_.version_major = major;
        const_section_.version_minor = minor;
        const_section_.version_patch = patch;
        const_section_.compression_level = static_cast<uint8_t>( std::min(compressionLevel, size_t(9)) ); // Clamp to 0-9
        const_section_.flags = 0;             // All flags reserved for future use
        const_section_.column_count = static_cast<uint16_t>( std::min(columnCount, MAX_COLUMN_COUNT));
        const_section_.packet_size  = 8 * 1024 * 1024;  // Default 8MB packet size
        if (const_section_.column_count == MAX_COLUMN_COUNT) {
            std::cerr << "Warning: Maximum column count exceeded. Some columns may be ignored." << std::endl;
        }
    }          

    inline void FileHeader::setFlag(FileFlags flag, bool value) {
        if (value) {
            const_section_.flags |= static_cast<uint16_t>(flag);
        } else {
            const_section_.flags &= ~static_cast<uint16_t>(flag);
        }
    }

    // FileHeader method implementations that require ColumnLayout definition
    template<LayoutConcept LayoutType>
    inline size_t FileHeader::getBinarySize(const LayoutType& layout) {
        size_t size = sizeof(ConstSection);                    // Fixed header
        size += layout.columnCount() * sizeof(ColumnType);      // Column data types
        size += layout.columnCount() * sizeof(uint16_t);        // Column name lengths
        for (size_t i = 0; i < layout.columnCount(); ++i) {     // Column names
            size += layout.columnName(i).length();
        }
        return size;
    }

    template<LayoutConcept LayoutType>
    inline void FileHeader::writeToBinary(std::ostream& stream, const LayoutType& layout) {
        // Update header with current column count
        const_section_.column_count = static_cast<uint16_t>(layout.columnCount());

        // Write fixed header
        stream.write(reinterpret_cast<const char*>(&const_section_), sizeof(const_section_));
        if (!stream.good()) {
            throw std::runtime_error("Failed to write BCSV header to stream");
        }
        
        // Write column data types
        const auto& columnTypes = layout.columnTypes();
        stream.write(reinterpret_cast<const char*>(columnTypes.data()), columnTypes.size() * sizeof(ColumnType));
        if (!stream.good()) {
            throw std::runtime_error("Failed to write column data types to stream");
        }
        
        // Write column name lengths
        std::vector<uint16_t> strLength(layout.columnCount());
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            strLength[i] = static_cast<uint16_t>(layout.columnName(i).length());
        }
        stream.write(reinterpret_cast<const char*>(strLength.data()), strLength.size() * sizeof(uint16_t));
        if (!stream.good()) {
            throw std::runtime_error("Failed to write column name lengths to stream");
        }

        // Write column names (without null terminator)
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            const std::string& name = layout.columnName(i);
            stream.write(name.c_str(), name.length());
            if (!stream.good()) {
                throw std::runtime_error("Failed to write column name at index " + std::to_string(i));
            }
        }
    }

    // Specialized version for Layout - modifies the layout to match binary data
    inline void FileHeader::readFromBinary(std::istream& stream, Layout& columnLayout) {
        try {
            // Clear the layout first to ensure clean state on failure
            columnLayout.clear();
            
            // Read fixed header
            stream.read(reinterpret_cast<char*>(&const_section_), sizeof(const_section_));
            if (!stream.good()) {
                throw std::runtime_error("Failed to read BCSV header from stream");
            }
            
            if (!isValidMagic()) {
                throw std::runtime_error("Invalid magic number in BCSV header. Expected: 0x" + 
                                       std::to_string(BCSV_MAGIC) + ", Got: 0x" + std::to_string(const_section_.magic));
            }
            
            // Validate column count
            if (const_section_.column_count > MAX_COLUMN_COUNT) {
                throw std::runtime_error("Column count (" + std::to_string(const_section_.column_count) + 
                                       ") exceeds maximum limit (" + std::to_string(MAX_COLUMN_COUNT) + ")");
            }

            // Read column data types
            std::vector<ColumnType> columnTypes(const_section_.column_count);
            stream.read(reinterpret_cast<char*>(columnTypes.data()), const_section_.column_count * sizeof(ColumnType));
            if (stream.gcount() != static_cast<std::streamsize>(const_section_.column_count * sizeof(ColumnType))) {
                throw std::runtime_error("Failed to read column data types");
            }


            // Read column name lengths
            std::vector<uint16_t> nameLengths(const_section_.column_count);
            stream.read(reinterpret_cast<char*>(nameLengths.data()), const_section_.column_count * sizeof(uint16_t));
            if (stream.gcount() != static_cast<std::streamsize>(const_section_.column_count * sizeof(uint16_t))) {
                throw std::runtime_error("Failed to read column name lengths");
            }
            
            // Validate name lengths
            for (uint16_t i = 0; i < const_section_.column_count; ++i) {
                if (nameLengths[i] > MAX_STRING_LENGTH) [[unlikely]] {
                    throw std::runtime_error("Column name length (" + std::to_string(nameLengths[i]) + 
                                           ") exceeds maximum (" + std::to_string(MAX_STRING_LENGTH) + 
                                           ") at index " + std::to_string(i));
                }
            }

            // Read column names
            std::vector<std::string> columnNames(const_section_.column_count);
            for (uint16_t i = 0; i < const_section_.column_count; ++i) {
                auto &name = columnNames[i];
                if (nameLengths[i] > 0) [[likely]] {
                    name.resize(nameLengths[i]);
                    stream.read(name.data(), nameLengths[i]);
                    if (stream.gcount() != static_cast<std::streamsize>(nameLengths[i])) {
                        throw std::runtime_error("Failed to read column name at index " + std::to_string(i));
                    }
                } else {
                    name = "Column_" + std::to_string(i);
                }
            }

            // Populate the layout with the deserialized data
            columnLayout.setColumns(columnNames, columnTypes);
            
        } catch (const std::exception&) {
            // Ensure layout is cleared on any failure
            columnLayout.clear();
            throw; // Re-throw the exception
        }
    }

    // Specialized version for LayoutStatic - validates that binary matches static definition
    template<typename... ColumnTypes>
    void FileHeader::readFromBinary(std::istream& stream, LayoutStatic<ColumnTypes...>& layout) {
        // Read fixed header
        stream.read(reinterpret_cast<char*>(&const_section_), sizeof(const_section_));
        if (!stream.good()) {
            throw std::runtime_error("Failed to read BCSV header from stream");
        }
        
        if (!isValidMagic()) {
            throw std::runtime_error("Invalid magic number in BCSV header. Expected: 0x" + 
                                   std::to_string(BCSV_MAGIC) + ", Got: 0x" + std::to_string(const_section_.magic));
        }
        
        // Validate column count matches static definition (this also covers MAX_COLUMN_COUNT)
        if (const_section_.column_count != layout.columnCount()) {
            throw std::runtime_error("Column count mismatch. Static layout expects " + 
                                   std::to_string(layout.columnCount()) + " columns, but binary has " + 
                                   std::to_string(const_section_.column_count) + " columns");
        }

        // Read column data types and validate against static definition
        std::vector<ColumnType> columnTypes(layout.columnCount());
        stream.read(reinterpret_cast<char*>(columnTypes.data()), layout.columnCount() * sizeof(ColumnType));
        if (stream.gcount() != static_cast<std::streamsize>(layout.columnCount() * sizeof(ColumnType))) {
            throw std::runtime_error("Failed to read column data types");
        }
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            if (columnTypes[i] != layout.columnType(i)) [[unlikely]] {
                throw std::runtime_error("Column type mismatch at index " + std::to_string(i) + 
                                       ". Static layout expects " + std::string(toString(layout.columnType(i))) + 
                                       ", but binary has " + std::string(toString(columnTypes[i])));
            }
        }

        // Read column name lengths (we must read them to advance the stream)
        std::vector<uint16_t> nameLengths(layout.columnCount());
        stream.read(reinterpret_cast<char*>(nameLengths.data()), layout.columnCount() * sizeof(uint16_t));
        if (stream.gcount() != static_cast<std::streamsize>(layout.columnCount() * sizeof(uint16_t))) {
            throw std::runtime_error("Failed to read column name lengths");
        }

        // Read column names
        std::vector<std::string> columnNames(layout.columnCount());
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            auto &name = columnNames[i];
            if (nameLengths[i] > MAX_STRING_LENGTH) [[unlikely]] {
                throw std::runtime_error("Column name length (" + std::to_string(nameLengths[i]) + 
                                       ") exceeds maximum (" + std::to_string(MAX_STRING_LENGTH) + 
                                       ") at index " + std::to_string(i));
            } else if (nameLengths[i] > 0) [[likely]] {
                name.resize(nameLengths[i]);
                stream.read(name.data(), nameLengths[i]);
                if (stream.gcount() != static_cast<std::streamsize>(nameLengths[i])) {
                    throw std::runtime_error("Failed to read column name at index " + std::to_string(i));
                }
            } else {
                columnNames[i] = "Column_" + std::to_string(i);
            }
        }
        layout.setColumnNames(columnNames);
    }

    template<LayoutConcept LayoutType>
    inline void FileHeader::printBinaryLayout(const LayoutType& layout) const {
        std::cout << "FileHeader Binary Layout (" << getBinarySize(layout) << " bytes):\n";
        std::cout << "  Magic:       0x" << std::hex << const_section_.magic << std::dec << " (" << sizeof(const_section_.magic) << " bytes)\n";
        std::cout << "  Version:     " << static_cast<int>(const_section_.version_major) << "." 
                  << static_cast<int>(const_section_.version_minor) << "." 
                  << static_cast<int>(const_section_.version_patch) << " (3 bytes)\n";
        std::cout << "  Compression: " << static_cast<int>(const_section_.compression_level) << " (1 byte)\n";
        std::cout << "  Flags:       0x" << std::hex << const_section_.flags << std::dec << " (2 bytes)\n";
        std::cout << "  Columns:     " << static_cast<uint16_t>(layout.columnCount()) << " (2 bytes)\n";
        std::cout << "  Column Data Types: " << layout.columnCount() * sizeof(ColumnType) << " bytes\n";
        for (size_t i = 0; i < layout.columnCount(); ++i) {
            std::cout << "    [" << i << "]: " << static_cast<int>(layout.columnType(i)) << "\n";
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
