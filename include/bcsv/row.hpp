#pragma once

/**
 * @file row.hpp
 * @brief Binary CSV (BCSV) Library - Row implementations
 * 
 * This file contains the implementations for the Row class.
 */

#include "row.h"
#include "layout.h"

namespace bcsv {

    Row::Row(const Layout &layout) : data_(layout.getColumnCount()) {
        for(size_t i = 0; i < layout.getColumnCount(); ++i) {
            data_[i] = getDefaultValue(layout.getColumnType(i));
        }
    }

    template<typename T>
    void Row::setValue(size_t index, const T& value) {
        if (index >= data_.size()) {
            throw std::out_of_range("Column index " + std::to_string(index) + 
                                    " out of range (size: " + std::to_string(data_.size()) + ")");
        }
        data_[index] = value;
    }

    template<typename T>
    const T& Row::getValue(size_t index) const {
        return std::get<T>(data_[index]);
    }

    void Row::serializedSize(size_t& fixedSize, size_t& totalSize) const {
        fixedSize = 0;
        totalSize = 0;
        
        for (const auto& value : data_) {
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>; 
                if constexpr (std::is_same_v<T, std::string>) {
                    fixedSize += sizeof(uint64_t);  // fixed segment of string (start and length, truncated to uint16_t)
                    totalSize += std::min(v.size(), MAX_STRING_LENGTH); // payload of string content
                } else {
                    // Fixed-size types
                    fixedSize += sizeof(T);         
                }
            }, value);  
        }
        totalSize += fixedSize;
        // Calculate padding needed for 4-byte alignment
        size_t paddingBytes = (4 - (totalSize % 4)) % 4;
        totalSize += paddingBytes;
    }

    void Row::serializeTo(char* dstBuffer, size_t dstBufferSize) const  {
        if (!dstBuffer || dstBufferSize == 0) {
            throw std::invalid_argument("Invalid destination buffer");
        }

        size_t totalSize = 0;
        size_t fixedSize = 0;
        serializedSize(fixedSize, totalSize);

        if (dstBufferSize < totalSize) {
            throw std::runtime_error("Destination buffer too small");
        }

        char*  ptrFix = dstBuffer;             // Pointer to the start of fixed-size data
        char*  ptrStr = dstBuffer + fixedSize; // Pointer to the start of string data
        size_t strOff = fixedSize;

        for (const auto& value : data_) {
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    size_t len = std::min(v.length(), MAX_STRING_LENGTH);
                    uint64_t addr = StringAddress::pack(strOff, len);
                    std::memcpy(ptrFix, &addr, sizeof(addr));
                    ptrFix += sizeof(addr);
                    std::memcpy(ptrStr, v.c_str(), len);
                    ptrStr += len;
                    strOff += len;
                } else {
                    std::memcpy(ptrFix, &v, sizeof(v));
                    ptrFix += sizeof(v);
                }
            }, value);
        }
        // Zero out any padding bytes to maintain clean data
        std::memset(ptrStr, 0, totalSize - (ptrStr - dstBuffer));
    }
} // namespace bcsv