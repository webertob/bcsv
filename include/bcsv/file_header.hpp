#pragma once

/**
 * @file file_header.hpp
 * @brief Binary CSV (BCSV) Library - FileHeader implementations
 * 
 * This file contains the basic implementations for the FileHeader class.
 * Methods requiring full ColumnLayout definition are in bcsv.hpp.
 */

#include "file_header.h"
#include "layout.hpp"
#include <string>
#include <iostream>
#include <vector>
   
namespace bcsv {

    inline FileHeader::FileHeader(size_t columnCount, uint8_t compressionLevel, uint8_t major, uint8_t minor, uint8_t patch) {
        header_.magic = BCSV_MAGIC;
        header_.versionMajor = major;
        header_.versionMinor = minor;
        header_.versionPatch = patch;
        header_.compressionLevel = compressionLevel;
        header_.flags = 0;             // All flags reserved for future use
        header_.columnCount = columnCount;
    }

    inline void FileHeader::setVersion(uint8_t major, uint8_t minor, uint8_t patch) {
        header_.versionMajor = major;
        header_.versionMinor = minor;
        header_.versionPatch = patch;
    }

    inline std::string FileHeader::getVersionString() const {
        return std::to_string(header_.versionMajor) + "." + 
               std::to_string(header_.versionMinor) + "." + 
               std::to_string(header_.versionPatch);
    }

    inline uint8_t FileHeader::getVersionMajor() const {
        return header_.versionMajor;
    }

    inline uint8_t FileHeader::getVersionMinor() const {
        return header_.versionMinor;
    }

    inline uint8_t FileHeader::getVersionPatch() const {
        return header_.versionPatch;
    }

    inline void FileHeader::setCompressionLevel(uint8_t level) { 
        // In v1.0+, compression is always enabled - only the level can be changed
        header_.compressionLevel = (level > 9) ? 9 : level;
        // Ensure compression level is at least 1 since compression is mandatory
        if (header_.compressionLevel == 0) {
            header_.compressionLevel = 1;
        }
    }

    inline uint8_t FileHeader::getCompressionLevel() const {
        return header_.compressionLevel;
    }

    inline void FileHeader::setFlag(uint16_t flag, bool value) {
        if (value) {
            header_.flags |= flag;
        } else {
            header_.flags &= ~flag;
        }
    }

    inline bool FileHeader::getFlag(uint16_t flag) const {
        return (header_.flags & flag) != 0;
    }

    inline uint16_t FileHeader::getFlags() const {
        return header_.flags;
    }

    inline void FileHeader::setFlags(uint16_t flags) {
        header_.flags = flags;
    }

    inline bool FileHeader::isValidMagic() const {
        return header_.magic == BCSV_MAGIC;
    }

    inline uint32_t FileHeader::getMagic() const {
        return header_.magic;
    }

    // FileHeader method implementations that require ColumnLayout definition
    template<typename LayoutType>
    inline static size_t FileHeader::getBinarySize(const LayoutType& columnLayout) {
        size_t size = sizeof(FileHeaderStruct);                          // Fixed header
        size_t columnCount = std::min(columnLayout.getColumnCount(), MAX_COLUMN_COUNT);
        size += columnCount * sizeof(uint16_t);        // Column data types
        size += columnCount * sizeof(uint16_t);        // Column name lengths
        for (size_t i = 0; i < columnCount; ++i) {     // Column names
            size += columnLayout.getColumnName(i).length();
        }
        return size;
    }

    template<typename LayoutType>
    inline bool FileHeader::writeToBinary(std::ostream& stream, const LayoutType& columnLayout) const {
        // Update header with current column count
        FileHeaderStruct tempHeader = header_;
        tempHeader.columnCount = static_cast<uint16_t>(std::min(columnLayout.getColumnCount(), MAX_COLUMN_COUNT)); // ensure no overflow takes place
        //warn if column count was truncated
        if (tempHeader.columnCount < columnLayout.getColumnCount()) {
            throw std::runtime_error("Warning: Column count truncated from " + std::to_string(columnLayout.getColumnCount()) + " to " + std::to_string(tempHeader.columnCount));
        }

        // Write fixed header
        stream.write(reinterpret_cast<const char*>(&tempHeader), sizeof(tempHeader));
        if (!stream.good()) return false;

        // Write column data types
        for (size_t i = 0; i < tempHeader.columnCount; ++i) {
            uint16_t typeValue = static_cast<uint16_t>(columnLayout.getColumnType(i));
            stream.write(reinterpret_cast<const char*>(&typeValue), sizeof(typeValue));
            if (!stream.good()) return false;
        }

        // Write column name lengths
        for (size_t i = 0; i < tempHeader.columnCount; ++i) {
            uint16_t nameLength = static_cast<uint16_t>(columnLayout.getColumnName(i).length());
            stream.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
            if (!stream.good()) return false;
        }

        // Write column names (without null terminator)
        for (size_t i = 0; i < tempHeader.columnCount; ++i) {
            const std::string& name = columnLayout.getColumnName(i);
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
            stream.read(reinterpret_cast<char*>(&header_), sizeof(header_));
            if (!stream.good()) {
                throw std::runtime_error("Failed to read BCSV header from stream");
            }
            
            if (!isValidMagic()) {
                throw std::runtime_error("Invalid magic number in BCSV header. Expected: 0x" + 
                                       std::to_string(BCSV_MAGIC) + ", Got: 0x" + std::to_string(header_.magic));
            }
            
            // Validate column count
            if (header_.columnCount > MAX_COLUMN_COUNT) {
                throw std::runtime_error("Column count (" + std::to_string(header_.columnCount) + 
                                       ") exceeds maximum limit (" + std::to_string(MAX_COLUMN_COUNT) + ")");
            }

            // Read column data types
            std::vector<ColumnDataType> columnDataTypes(header_.columnCount);
            for (uint16_t i = 0; i < header_.columnCount; ++i) {
                uint16_t typeValue;
                stream.read(reinterpret_cast<char*>(&typeValue), sizeof(typeValue));
                if (!stream.good()) {
                    throw std::runtime_error("Failed to read column data type at index " + std::to_string(i));
                }
                columnDataTypes[i] = static_cast<ColumnDataType>(typeValue);
            }

            // Read column name lengths
            std::vector<uint16_t> nameLengths(header_.columnCount);
            for (uint16_t i = 0; i < header_.columnCount; ++i) {
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
            std::vector<std::string> columnNames(header_.columnCount);
            for (uint16_t i = 0; i < header_.columnCount; ++i) {
                if (nameLengths[i] > 0) {
                    std::vector<char> nameBuffer(nameLengths[i]);
                    stream.read(nameBuffer.data(), nameLengths[i]);
                    if (!stream.good()) {
                        throw std::runtime_error("Failed to read column name at index " + std::to_string(i));
                    }
                    columnNames[i] = std::string(nameBuffer.begin(), nameBuffer.end());
                } else {
                    columnNames[i] = "";
                }
            }

            // Populate the layout with the deserialized data
            columnLayout.setColumns(columnNames, columnDataTypes);
            return true;
            
        } catch (const std::exception& e) {
            // Ensure layout is cleared on any failure
            columnLayout.clear();
            throw; // Re-throw the exception
        }
    }

    // Specialized version for LayoutStatic - validates that binary matches static definition
    template<typename... ColumnTypes>
    bool FileHeader::readFromBinary(std::istream& stream, const LayoutStatic<ColumnTypes...>& columnLayout) {
        try {
            // Read fixed header
            stream.read(reinterpret_cast<char*>(&header_), sizeof(header_));
            if (!stream.good()) {
                throw std::runtime_error("Failed to read BCSV header from stream");
            }
            
            if (!isValidMagic()) {
                throw std::runtime_error("Invalid magic number in BCSV header. Expected: 0x" + 
                                       std::to_string(BCSV_MAGIC) + ", Got: 0x" + std::to_string(header_.magic));
            }
            
            // Validate column count matches static definition
            constexpr size_t expectedColumnCount = sizeof...(ColumnTypes);
            if (header_.columnCount != expectedColumnCount) {
                throw std::runtime_error("Column count mismatch. Static layout expects " + 
                                       std::to_string(expectedColumnCount) + " columns, but binary has " + 
                                       std::to_string(header_.columnCount) + " columns");
            }

            // Read column data types and validate against static definition
            for (uint16_t i = 0; i < expectedColumnCount; ++i) {
                uint16_t typeValue;
                stream.read(reinterpret_cast<char*>(&typeValue), sizeof(typeValue));
                if (!stream.good()) {
                    throw std::runtime_error("Failed to read column data type at index " + std::to_string(i));
                }
                
                ColumnDataType binaryType = static_cast<ColumnDataType>(typeValue);
                ColumnDataType expectedType = columnLayout.getColumnType(i);
                
                if (binaryType != expectedType) {
                    throw std::runtime_error("Column type mismatch at index " + std::to_string(i) + 
                                           ". Static layout expects " + std::to_string(static_cast<int>(expectedType)) + 
                                           ", but binary has " + std::to_string(static_cast<int>(binaryType)));
                }
            }

            // Read column name lengths (we still need to read them to advance the stream)
            std::vector<uint16_t> nameLengths(expectedColumnCount);
            for (uint16_t i = 0; i < expectedColumnCount; ++i) {
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

            // Read column names and optionally validate against static definition
            for (uint16_t i = 0; i < expectedColumnCount; ++i) {
                if (nameLengths[i] > 0) {
                    std::vector<char> nameBuffer(nameLengths[i]);
                    stream.read(nameBuffer.data(), nameLengths[i]);
                    if (!stream.good()) {
                        throw std::runtime_error("Failed to read column name at index " + std::to_string(i));
                    }
                    
                    std::string columnName = std::string(nameBuffer.begin(), nameBuffer.end());
                    
                    // Optional: Validate column names match static definition (if names were set)
                    if (!columnLayout.getColumnName(i).empty()) {
                        if (columnName != columnLayout.getColumnName(i)) {
                            throw std::runtime_error("Column name mismatch at index " + std::to_string(i) + 
                                                   ". Static layout expects '" + columnLayout.getColumnName(i) + 
                                                   "', but binary has '" + columnName + "'");
                        }
                    }
                } else {
                    // Empty name - skip validation
                }
            }

            return true;
            
        } catch (const std::exception& e) {
            // For LayoutStatic, we don't clear/modify the layout on failure
            // since it's compile-time defined and cannot be changed
            throw; // Re-throw the exception
        }
    }

    template<typename LayoutType>
    inline void FileHeader::printBinaryLayout(const LayoutType& columnLayout) const {
        std::cout << "FileHeader Binary Layout (" << getBinarySize(columnLayout) << " bytes):\n";
        std::cout << "  Magic:       0x" << std::hex << header_.magic << std::dec << " (" << sizeof(header_.magic) << " bytes)\n";
        std::cout << "  Version:     " << static_cast<int>(header_.versionMajor) << "." 
                  << static_cast<int>(header_.versionMinor) << "." 
                  << static_cast<int>(header_.versionPatch) << " (3 bytes)\n";
        std::cout << "  Compression: " << static_cast<int>(header_.compressionLevel) << " (1 byte)\n";
        std::cout << "  Flags:       0x" << std::hex << header_.flags << std::dec << " (2 bytes)\n";
        std::cout << "  Columns:     " << static_cast<uint16_t>(columnLayout.getColumnCount()) << " (2 bytes)\n";
        std::cout << "  Column Data Types: " << columnLayout.getColumnCount() * sizeof(uint16_t) << " bytes\n";
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: " << static_cast<uint16_t>(columnLayout.getColumnType(i)) << "\n";
        }
        std::cout << "  Column Name Lengths: " << columnLayout.getColumnCount() * sizeof(uint16_t) << " bytes\n";
        size_t totalNameBytes = 0;
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: " << columnLayout.getColumnName(i).length() << " bytes\n";
            totalNameBytes += columnLayout.getColumnName(i).length();
        }
        std::cout << "  Column Names: " << totalNameBytes << " bytes\n";
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: \"" << columnLayout.getColumnName(i) << "\"\n";
        }
    }

} // namespace bcsv
