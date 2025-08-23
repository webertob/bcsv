#pragma once

/**
 * @file file_header.hpp
 * @brief Binary CSV (BCSV) Library - FileHeader implementations
 * 
 * This file contains the basic implementations for the FileHeader class.
 * Methods requiring full ColumnLayout definition are in bcsv.hpp.
 */

#include "file_header.h"
#include "column_layout.h"
#include <string>
#include <iostream>
#include <vector>
   
namespace bcsv {

    inline FileHeader::FileHeader() {
        header_.magic = BCSV_MAGIC;
        header_.versionMajor = VERSION_MAJOR;
        header_.versionMinor = VERSION_MINOR;
        header_.versionPatch = VERSION_PATCH;
        header_.compressionLevel = 0;
        header_.flags = 0;
        header_.columnCount = 0;
    }

    inline FileHeader::FileHeader(uint8_t major, uint8_t minor, uint8_t patch) : FileHeader() {
        header_.versionMajor = major;
        header_.versionMinor = minor;
        header_.versionPatch = patch;
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
        header_.compressionLevel = (level > 9) ? 9 : level;
        if (level > 0) {
            header_.flags |= FileFlags::COMPRESSED;
        } else {
            header_.flags &= ~FileFlags::COMPRESSED;
        }
    }

    inline uint8_t FileHeader::getCompressionLevel() const {
        return header_.compressionLevel;
    }

    inline bool FileHeader::isCompressed() const {
        return (header_.flags & FileFlags::COMPRESSED) != 0;
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
    inline size_t FileHeader::getBinarySize(const ColumnLayout& columnLayout) const {
        size_t size = sizeof(FileHeaderStruct);                           // Fixed header
        size += columnLayout.getColumnCount() * sizeof(uint16_t);        // Column data types
        size += columnLayout.getColumnCount() * sizeof(uint16_t);        // Column name lengths
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {     // Column names
            size += columnLayout.getName(i).length();
        }
        return size;
    }

    inline bool FileHeader::writeToBinary(std::ostream& stream, const ColumnLayout& columnLayout) const {
        // Update header with current column count
        FileHeaderStruct tempHeader = header_;
        tempHeader.columnCount = static_cast<uint16_t>(std::min(columnLayout.getColumnCount(), MAX_COLUMN_COUNT)); // ensure no overflow takes place
        //warn if column count was truncated
        if (tempHeader.columnCount < columnLayout.getColumnCount()) {
            std::cerr << "Warning: Column count truncated from " << columnLayout.getColumnCount() << " to " << tempHeader.columnCount << std::endl;
        }

        // Write fixed header
        stream.write(reinterpret_cast<const char*>(&tempHeader), sizeof(tempHeader));
        if (!stream.good()) return false;

        // Write column data types
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            uint16_t typeValue = static_cast<uint16_t>(columnLayout.getDataType(i));
            stream.write(reinterpret_cast<const char*>(&typeValue), sizeof(typeValue));
            if (!stream.good()) return false;
        }

        // Write column name lengths
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            uint16_t nameLength = static_cast<uint16_t>(columnLayout.getName(i).length());
            stream.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
            if (!stream.good()) return false;
        }

        // Write column names (without null terminator)
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            const std::string& name = columnLayout.getName(i);
            if (!name.empty()) {
                stream.write(name.c_str(), name.length());
                if (!stream.good()) return false;
            }
        }

        return true;
    }

    inline bool FileHeader::readFromBinary(std::istream& stream, ColumnLayout& columnLayout) {
        // Read fixed header
        stream.read(reinterpret_cast<char*>(&header_), sizeof(header_));
        if (!stream.good() || !isValidMagic()) return false;

        // Validate column count
        if (header_.columnCount > MAX_COLUMN_COUNT) return false;

        // Read column data types
        std::vector<ColumnDataType> columnDataTypes(header_.columnCount);
        for (uint16_t i = 0; i < header_.columnCount; ++i) {
            uint16_t typeValue;
            stream.read(reinterpret_cast<char*>(&typeValue), sizeof(typeValue));
            if (!stream.good()) return false;
            columnDataTypes[i] = static_cast<ColumnDataType>(typeValue);
        }

        // Read column name lengths
        std::vector<uint16_t> nameLengths(header_.columnCount);
        for (uint16_t i = 0; i < header_.columnCount; ++i) {
            stream.read(reinterpret_cast<char*>(&nameLengths[i]), sizeof(uint16_t));
            if (!stream.good()) return false;
            
            // Validate name length
            if (nameLengths[i] > MAX_COLUMN_WIDTH) return false;
        }

        // Read column names
        std::vector<std::string> columnNames(header_.columnCount);
        for (uint16_t i = 0; i < header_.columnCount; ++i) {
            if (nameLengths[i] > 0) {
                std::vector<char> nameBuffer(nameLengths[i]);
                stream.read(nameBuffer.data(), nameLengths[i]);
                if (!stream.good()) return false;
                columnNames[i] = std::string(nameBuffer.begin(), nameBuffer.end());
            } else {
                columnNames[i] = "";
            }
        }

        // Clear the existing column layout and populate it with the deserialized data
        columnLayout = ColumnLayout(); // Reset to empty state
        for (uint16_t i = 0; i < header_.columnCount; ++i) {
            columnLayout.addColumn(columnNames[i], columnDataTypes[i]);
        }

        return true;
    }

    inline void FileHeader::printBinaryLayout(const ColumnLayout& columnLayout) const {
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
            std::cout << "    [" << i << "]: " << static_cast<uint16_t>(columnLayout.getDataType(i)) << "\n";
        }
        std::cout << "  Column Name Lengths: " << columnLayout.getColumnCount() * sizeof(uint16_t) << " bytes\n";
        size_t totalNameBytes = 0;
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: " << columnLayout.getName(i).length() << " bytes\n";
            totalNameBytes += columnLayout.getName(i).length();
        }
        std::cout << "  Column Names: " << totalNameBytes << " bytes\n";
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            std::cout << "    [" << i << "]: \"" << columnLayout.getName(i) << "\"\n";
        }
    }

} // namespace bcsv
