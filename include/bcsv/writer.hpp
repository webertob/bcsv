#pragma once

/**
 * @file writer.hpp
 * @brief Binary CSV (BCSV) Library - Writer implementations
 * 
 * This file contains the template implementations for the Writer class.
 */

#include "writer.h"
#include "file_header.h"
#include "column_layout.h"
#include "row.h"
#include <fstream>
#include <type_traits>

namespace bcsv {

    // Writer template implementations

    template<typename StreamType>
    Writer<StreamType>::Writer(const std::string& filename) {
        open(filename);
    }

    template<typename StreamType>
    Writer<StreamType>::Writer(StreamType&& stream) : stream_(std::move(stream)) {
    }

    template<typename StreamType>
    bool Writer<StreamType>::open(const std::string& filename) {
        if constexpr (std::is_same_v<StreamType, std::ofstream>) {
            stream_.open(filename, std::ios::binary);
            return stream_.is_open();
        }
        return false;
    }

    template<typename StreamType>
    void Writer<StreamType>::close() {
        if constexpr (std::is_same_v<StreamType, std::ofstream>) {
            if (stream_.is_open()) {
                stream_.close();
            }
        }
    }

    template<typename StreamType>
    void Writer<StreamType>::setCompression(bool enabled) {
        compressionEnabled_ = enabled;
    }

    template<typename StreamType>
    bool Writer<StreamType>::isCompressionEnabled() const {
        return compressionEnabled_;
    }

    template<typename StreamType>
    bool Writer<StreamType>::writeFileHeader(const FileHeader& fileHeader) {
        if (!fileHeader.writeToBinary(stream_, columnLayout_)) {
            return false;
        }
        fileHeaderWritten_ = true;
        return true;
    }

    template<typename StreamType>
    bool Writer<StreamType>::writeColumnLayout(const ColumnLayout& columnLayout) {
        columnLayout_ = columnLayout;
        headerWritten_ = true;
        return true;
    }

    template<typename StreamType>
    bool Writer<StreamType>::writeRow(const Row& row) {
        // Placeholder implementation - serialize the row data
        // In a full implementation, this would serialize the row values
        rowCount_++;
        return true;
    }

    template<typename StreamType>
    void Writer<StreamType>::flush() {
        if constexpr (std::is_same_v<StreamType, std::ofstream>) {
            stream_.flush();
        }
    }

    template<typename StreamType>
    bool Writer<StreamType>::compressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& compressed) {
        // LZ4 compression implementation placeholder
        compressed = data; // For now, just copy
        return true;
    }

} // namespace bcsv