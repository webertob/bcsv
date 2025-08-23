#pragma once

/**
 * @file reader.hpp
 * @brief Binary CSV (BCSV) Library - Reader implementations
 * 
 * This file contains the template implementations for the Reader class.
 */

#include "reader.h"
#include "file_header.h"
#include "column_layout.h"
#include "row.h"
#include <fstream>
#include <type_traits>

namespace bcsv {

    // Template implementations

    template<typename StreamType>
    Reader<StreamType>::Reader(const std::string& filename) {
        open(filename);
    }

    template<typename StreamType>
    Reader<StreamType>::Reader(StreamType&& stream) : stream_(std::move(stream)) {
    }

    template<typename StreamType>
    bool Reader<StreamType>::open(const std::string& filename) {
        if constexpr (std::is_same_v<StreamType, std::ifstream>) {
            stream_.open(filename, std::ios::binary);
            return stream_.is_open();
        }
        return false;
    }

    template<typename StreamType>
    void Reader<StreamType>::close() {
        if constexpr (std::is_same_v<StreamType, std::ifstream>) {
            if (stream_.is_open()) {
                stream_.close();
            }
        }
    }

    template<typename StreamType>
    bool Reader<StreamType>::readFileHeader() {
        if (!fileHeader_.readFromBinary(stream_, columnLayout_)) {
            return false;
        }
        fileHeaderRead_ = true;
        headerRead_ = true; // Column layout is read as part of file header
        return true;
    }

    template<typename StreamType>
    bool Reader<StreamType>::readColumnLayout() {
        // Column layout is read as part of file header
        if (!fileHeaderRead_) {
            return readFileHeader();
        }
        return headerRead_;
    }

    template<typename StreamType>
    bool Reader<StreamType>::readRow(Row& row) {
        // Placeholder implementation - read a simple row
        // In a full implementation, this would deserialize the row data
        currentRowIndex_++;
        return false; // No more rows for now
    }

    template<typename StreamType>
    const FileHeader& Reader<StreamType>::getFileHeader() const {
        return fileHeader_;
    }

    template<typename StreamType>
    const ColumnLayout& Reader<StreamType>::getColumnLayout() const {
        return columnLayout_;
    }

    template<typename StreamType>
    bool Reader<StreamType>::hasMoreRows() const {
        return stream_.good() && !stream_.eof();
    }

    template<typename StreamType>
    size_t Reader<StreamType>::getCurrentRowIndex() const {
        return currentRowIndex_;
    }

    template<typename StreamType>
    bool Reader<StreamType>::decompressData(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed) {
        // LZ4 decompression implementation placeholder
        decompressed = compressed; // For now, just copy
        return true;
    }

    // Iterator implementation
    template<typename StreamType>
    Reader<StreamType>::iterator::iterator(Reader* reader, bool end) : reader_(reader), end_(end) {
        if (!end_ && reader_) {
            end_ = !reader_->readRow(currentRow_);
        }
    }

    template<typename StreamType>
    Row Reader<StreamType>::iterator::operator*() {
        return currentRow_;
    }

    template<typename StreamType>
    typename Reader<StreamType>::iterator& Reader<StreamType>::iterator::operator++() {
        if (reader_) {
            end_ = !reader_->readRow(currentRow_);
        }
        return *this;
    }

    template<typename StreamType>
    bool Reader<StreamType>::iterator::operator!=(const iterator& other) const {
        return end_ != other.end_;
    }

    template<typename StreamType>
    typename Reader<StreamType>::iterator Reader<StreamType>::begin() {
        return iterator(this);
    }

    template<typename StreamType>
    typename Reader<StreamType>::iterator Reader<StreamType>::end() {
        return iterator(this, true);
    }

} // namespace bcsv