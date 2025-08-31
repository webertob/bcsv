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
        header_.columnCount = static_cast<uint16_t>( std::min(columnCount, MAX_COLUMN_COUNT));
        if (header_.columnCount == MAX_COLUMN_COUNT) {
            std::cerr << "Warning: Maximum column count exceeded. Some columns may be ignored." << std::endl;
        }
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
    template<LayoutConcept LayoutType>
    inline static size_t FileHeader::getBinarySize(const LayoutType& layout) {
        size_t size = sizeof(FileHeaderStruct);                    // Fixed header
        size += layout.getColumnCount() * sizeof(uint16_t);        // Column data types
        size += layout.getColumnCount() * sizeof(uint16_t);        // Column name lengths
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {     // Column names
            size += layout.getColumnName(i).length();
        }
        return size;
    }

    template<LayoutConcept LayoutType>
    inline bool FileHeader::writeToBinary(std::ostream& stream, const LayoutType& layout) {
        // Update header with current column count
        header_.columnCount = static_cast<uint16_t>(layout.getColumnCount());

        // Write fixed header
        stream.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
        if (!stream.good()) return false;

        // Write column data types
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {
            const uint16_t val = static_cast<uint16_t>(layout.getColumnType(i));
            stream.write(reinterpret_cast<const char*>(&val), sizeof(val));
            if (!stream.good()) return false;
        }

        // Write column name lengths
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {
            uint16_t val = static_cast<uint16_t>(layout.getColumnName(i).length());
            stream.write(reinterpret_cast<const char*>(&val), sizeof(val));
            if (!stream.good()) return false;
        }

        // Write column names (without null terminator)
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {
            const std::string& name = layout.getColumnName(i);
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
            std::vector<ColumnDefinition> columnDefinitions(header_.columnCount);
            for (uint16_t i = 0; i < header_.columnCount; ++i) {
                uint16_t typeValue;
                stream.read(reinterpret_cast<char*>(&typeValue), sizeof(typeValue));
                if (!stream.good()) {
                    throw std::runtime_error("Failed to read column data type at index " + std::to_string(i));
                }
                columnDefinitions[i].type = static_cast<ColumnDataType>(typeValue);
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
            for (uint16_t i = 0; i < header_.columnCount; ++i) {
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
    bool FileHeader::readFromBinary(std::istream& stream, LayoutStatic<ColumnTypes...>& layout) {
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
            if (header_.columnCount != layout.getColumnCount()) {
                throw std::runtime_error("Column count mismatch. Static layout expects " + 
                                       std::to_string(layout.getColumnCount()) + " columns, but binary has " + 
                                       std::to_string(header_.columnCount) + " columns");
            }

            // Read column data types and validate against static definition
            for (uint16_t i = 0; i < layout.getColumnCount(); ++i) {
                ColumnDataType type;
                stream.read(reinterpret_cast<char*>(&type), sizeof(type));
                if (!stream.good()) {
                    throw std::runtime_error("Failed to read column data type at index " + std::to_string(i));
                }
                if (type != layout.getColumnType(i)) {
                    throw std::runtime_error("Column type mismatch at index " + std::to_string(i) + 
                                           ". Static layout expects " + dataTypeToString(layout.getColumnType(i)) + 
                                           ", but binary has " + dataTypeToString(type));
                }
            }

            // Read column name lengths (we still need to read them to advance the stream)
            std::vector<uint16_t> nameLengths(layout.getColumnCount());
            for (uint16_t i = 0; i < layout.getColumnCount(); ++i) {
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
            for (uint16_t i = 0; i < layout.getColumnCount(); ++i) {
                if (nameLengths[i] > 0) {
                    std::vector<char> nameBuffer(nameLengths[i]);
                    stream.read(nameBuffer.data(), nameLengths[i]);
                    if (!stream.good()) {
                        throw std::runtime_error("Failed to read column name at index " + std::to_string(i));
                    }
                    
                    auto name = std::string_view(nameBuffer.begin(), nameBuffer.end());
                    // Optional: Validate column names match static definition (if names were set)
                    if (!layout.getColumnName(i).empty()) {
                        if (name != layout.getColumnName(i)) {
                            throw std::runtime_error("Column name mismatch at index " + std::to_string(i) + 
                                                   ". Static layout expects '" + layout.getColumnName(i) + 
                                                   "', but binary has '" + name + "'");
                        }
                    } else {
                        layout.setColumnName(i, name);
                    }
                } else {
                    layout.setColumnName(i, "Column_" + std::to_string(i));
                }
            }
            return true;
        } catch (const std::exception& e) {
            // For LayoutStatic, we don't clear/modify the layout on failure
            // since it's compile-time defined and cannot be changed
            throw; // Re-throw the exception
        }
    }

    template<LayoutConcept LayoutType>
    inline void FileHeader::printBinaryLayout(const LayoutType& layout) const {
        std::cout << "FileHeader Binary Layout (" << getBinarySize(layout) << " bytes):\n";
        std::cout << "  Magic:       0x" << std::hex << header_.magic << std::dec << " (" << sizeof(header_.magic) << " bytes)\n";
        std::cout << "  Version:     " << static_cast<int>(header_.versionMajor) << "." 
                  << static_cast<int>(header_.versionMinor) << "." 
                  << static_cast<int>(header_.versionPatch) << " (3 bytes)\n";
        std::cout << "  Compression: " << static_cast<int>(header_.compressionLevel) << " (1 byte)\n";
        std::cout << "  Flags:       0x" << std::hex << header_.flags << std::dec << " (2 bytes)\n";
        std::cout << "  Columns:     " << static_cast<uint16_t>(layout.getColumnCount()) << " (2 bytes)\n";
        std::cout << "  Column Data Types: " << layout.getColumnCount() * sizeof(uint16_t) << " bytes\n";
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: " << static_cast<uint16_t>(layout.getColumnType(i)) << "\n";
        }
        std::cout << "  Column Name Lengths: " << layout.getColumnCount() * sizeof(uint16_t) << " bytes\n";
        size_t totalNameBytes = 0;
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: " << layout.getColumnName(i).length() << " bytes\n";
            totalNameBytes += layout.getColumnName(i).length();
        }
        std::cout << "  Column Names: " << totalNameBytes << " bytes\n";
        for (size_t i = 0; i < layout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: \"" << layout.getColumnName(i) << "\"\n";
        }
    }

} // namespace bcsv
