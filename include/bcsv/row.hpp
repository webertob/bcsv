#pragma once

/**
 * @file row.hpp
 * @brief Binary CSV (BCSV) Library - Row implementations
 * 
 * This file contains the implementations for the Row and RowStatic classes.
 */

#include "row.h"
#include "layout.h"

namespace bcsv {

    // ========================================================================
    // StringAddress Implementation
    // ========================================================================
    uint64_t StringAddress::pack(size_t offset, size_t length) {
        if (offset > OFFSET_MASK) {
            throw std::overflow_error("Offset too large for 48-bit field");
        }
        if (length > LENGTH_MASK) {
            throw std::overflow_error("Length too large for 16-bit field");
        }
        return ((offset & OFFSET_MASK) << OFFSET_SHIFT) | (length & LENGTH_MASK);
    }
    
    void StringAddress::unpack(uint64_t packed, size_t& offset, size_t& length) {
        offset = (packed >> OFFSET_SHIFT) & OFFSET_MASK;
        length = static_cast<uint16_t>(packed & LENGTH_MASK);
    }

    // ========================================================================
    // Row Implementation
    // ========================================================================

    Row::Row(const Layout &layout) : data_(layout.getColumnCount()) {
        for(size_t i = 0; i < layout.getColumnCount(); ++i) {
            data_[i] = defaultValue(layout.getColumnType(i));
        }
    }

    template<typename T>
    const T& Row::get(size_t index) const {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        return std::get<T>(data_[index]);
    }
        
    template<typename T>
    void Row::set(size_t index, const T& value) {
        if (index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        
        // Type validation
        ValueType testValue{value};
        if (!isSameType(data_[index], testValue)) {
            throw std::invalid_argument("Type mismatch");
        }
        
        data_[index] = ValueType{value};
    }

    ValueType Row::getValue(size_t index) const {
        return data_[index];
    }

    void Row::setValue(size_t index, const ValueType& value) {
        if (index >= data_.size()) {
            throw std::out_of_range("Column index " + std::to_string(index) + 
                                    " out of range (size: " + std::to_string(data_.size()) + ")");
        }
        data_[index] = value;
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

    // ========================================================================
    // RowStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...>::RowStatic() : data_(defaultValueT<ColumnTypes>()...) {
        // Uses default construction for each type
    }

    template<typename... ColumnTypes>
    template<typename... Args>
    RowStatic<ColumnTypes...>::RowStatic(Args&&... args) : data_(std::forward<Args>(args)...) {
        static_assert(sizeof...(Args) == sizeof...(ColumnTypes),
                    "Number of arguments must match number of column types");
    }

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...>::RowStatic(const LayoutStatic<ColumnTypes...>& other) : data_(defaultValueT<ColumnTypes>()...) {        
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::setValue(size_t index, const ValueType& value) {
        setTupleValue<0>(data_, index, value);
    }

    template<typename... ColumnTypes>
    ValueType RowStatic<ColumnTypes...>::getValue(size_t index) const {
        return getTupleValue<0>(data_, index);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    auto& RowStatic<ColumnTypes...>::get() {
        static_assert(Index < sizeof...(ColumnTypes), "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    const auto& RowStatic<ColumnTypes...>::get() const {
        static_assert(Index < sizeof...(ColumnTypes), "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::set(const T& value) {
        static_assert(Index < sizeof...(ColumnTypes), "Index out of bounds");
        static_assert(std::is_same_v<T, std::tuple_element_t<Index, std::tuple<ColumnTypes...>>>, 
                      "Type mismatch");
        std::get<Index>(data_) = value;
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::serializedSize(size_t& fixedSize, size_t& totalSize) const {
        // Use compile-time constant
        fixedSize = FIXED_SIZE;
        totalSize = FIXED_SIZE;
        
        // Calculate variable size for strings only
        constexpr for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            if constexpr (std::is_same_v<std::tuple_element_t<i, std::tuple<ColumnTypes...>>, std::string>) {
                totalSize += std::min(std::get<i>(data_).size(), MAX_STRING_LENGTH);
                totalSize += MAX_STRING_LENGTH;
            }
        }        
        // Calculate padding needed for 4-byte alignment
        totalSize += (4 - (totalSize % 4)) % 4;
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::serializeTo(char* dstBuffer, size_t dstBufferSize) const {
        if (!dstBuffer || dstBufferSize == 0) {
            throw std::invalid_argument("Invalid destination buffer");
        }

        size_t fixedSize;
        size_t totalSize;
        serializedSize(fixedSize, totalSize);

        if (dstBufferSize < totalSize) {
            throw std::runtime_error("Destination buffer too small");
        }
        size_t strOffset = fixedSize;

        // Serialize each tuple element
        constexpr for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            constexpr size_t len = fieldLengths_[i];
            constexpr size_t off = fieldOffsets_[i];
            char* ptr = dstBuffer + off;
            if constexpr (std::is_same_v<ColumnType<i>, std::string>) {
                size_t strLength = std::min(std::get<i>(data_).size(), MAX_STRING_LENGTH);
                size_t strAddress = StringAddress::pack(strOffset, strLength);
                std::memcpy(ptr, &strAddress, len);     //write string address field
                if (strOffset + strLength > dstBufferSize) {
                    throw std::runtime_error("String data at index " + std::to_string(i) +
                                               " extends beyond buffer (offset=" + std::to_string(strOffset) +
                                               ", length=" + std::to_string(strLength) +
                                               ", bufferSize=" + std::to_string(dstBufferSize) + ")");
                }
                std::memcpy(dstBuffer + strOffset, std::get<i>(data_).c_str(), strLength); //write string payload
                strOffset += strLength;
            } else {
                std::memcpy(&std::get<i>(data_), ptr, len);
            }
        }
        // Zero out padding bytes
        std::memset(dstBuffer + strOffset, 0, totalSize - strOffset);
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::deserializeFrom(const char* srcBuffer, size_t srcBufferSize)
    {
        if (!srcBuffer || srcBufferSize == 0) {
            throw std::invalid_argument("Invalid source buffer");
        }

        constexpr (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            constexpr size_t len = fieldLengths_[i];
            constexpr size_t off = fieldOffsets_[i];
            const char* ptr = srcBuffer + off;
            if(off+len > srcBufferSize) {
                throw std::runtime_error("Buffer overflow reading element at index " + std::to_string(i) +
                                           " (offset=" + std::to_string(off) + ", length=" + std::to_string(len) +
                                           ", bufferSize=" + std::to_string(srcBufferSize) + ")");
            }

            if constexpr (std::is_same_v<ColumnType<i>, std::string>) {
                size_t strAddress, strOffset, strLength;
                std::memcpy(&strAddress, ptr, len);
                StringAddress::unpack(strAddress, strOffset, strLength);

                if (strOffset + strLength > srcBufferSize) {
                    throw std::runtime_error("String data at index " + std::to_string(i) +
                                              " extends beyond buffer (offset=" + std::to_string(strOffset) +
                                              ", length=" + std::to_string(strLength) +
                                              ", bufferSize=" + std::to_string(srcBufferSize) + ")");
                }
                std::get<i>(data_) = std::string_view(srcBuffer + strOffset, strLength);
                // Read packed string address
            } else {
                std::memcpy(&std::get<i>(data_), ptr, len);
            }
        }
    }

    // ========================================================================
    // RowView Implementation
    // ========================================================================

    RowView::RowView() : buffer_(nullptr), bufferSize_(0), columnTypes_(), fieldLengths_(), fieldOffsets_() 
    {
    }

    RowView::RowView(char* buffer, size_t bufferSize, const Layout &layout)
            : buffer_(buffer), bufferSize_(bufferSize) 
    { 
        setLayout(layout);
    }

    void RowView::setValue(size_t index, const ValueType& value) 
    {
        if(buffer_ == nullptr) {
            throw std::runtime_error("Buffer is null");
        }

        //check time provided match type defined
        if (!isType(value, columnTypes_[index])) {
            throw std::runtime_error("Type mismatch");
        }

        size_t len = fieldLengths_[index];
        size_t off = fieldOffsets_[index];
        if (off + len > bufferSize_) {
            throw std::runtime_error("Field end exceeds buffer size");
        }
        char* ptr = buffer_ + off;
        switch(columnTypes_[index]) {
            case ColumnDataType::BOOL: {
                bool src = std::get<bool>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::UINT8: {
                uint8_t src = std::get<uint8_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;  
            }
            case ColumnDataType::UINT16: {
                uint16_t src = std::get<uint16_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::UINT32: {
                uint32_t src = std::get<uint32_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }         
            case ColumnDataType::UINT64: {
                uint64_t src = std::get<uint64_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::INT8: {
                int8_t src = std::get<int8_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::INT16: {
                int16_t src = std::get<int16_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::INT32: {
                int32_t src = std::get<int32_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::INT64: {
                int64_t src = std::get<int64_t>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::FLOAT: {
                float src = std::get<float>(value);
                std::memcpy(ptr, &src, sizeof(src));
                break;
            }
            case ColumnDataType::DOUBLE: {
                double src = std::get<double>(value);
                std::memcpy(ptr, &src, sizeof(src));    
                break;
            }
            case ColumnDataType::STRING: {
                // Unpack string address
                uint64_t packedAddr;
                std::memcpy(&packedAddr, ptr, sizeof(packedAddr));
                size_t strOff, strLen;
                StringAddress::unpack(packedAddr, strOff, strLen);

                // Validate string payload is within buffer
                if (strOff + strLen > bufferSize_) {
                    throw std::runtime_error("String payload extends beyond buffer");
                }

                const std::string& src = std::get<std::string>(value);
                // Copy string data into buffer (truncate to strLen)
                std::memcpy(buffer_ + strOff, src.data(), strLen);
                if(src.size() < strLen) {
                    std::memset(buffer_ + strOff + src.size(), 0, strLen - src.size()); // Pad with null bytes if shorter
                }
                break;
            }
        }
    }

    ValueType RowView::getValue(size_t index) const 
    {
        if(buffer_ == nullptr) {
            throw std::runtime_error("Buffer is null");
        }

        size_t len = fieldLengths_[index];
        size_t off = fieldOffsets_[index];
        if (off + len > bufferSize_) {
            throw std::runtime_error("Field end exceeds buffer size");
        }
        const char* ptr = buffer_ + off;
        switch(columnTypes_[index]) {
            case ColumnDataType::BOOL: {
                bool value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::UINT8: {
                uint8_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::UINT16: {
                uint16_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::UINT32: {
                uint32_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::UINT64: {
                uint64_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::INT8: {
                int8_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::INT16: {
                int16_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::INT32: {
                int32_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::INT64: {
                int64_t value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::FLOAT: {
                float value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::DOUBLE: {
                double value;
                std::memcpy(&value, ptr, sizeof(value));
                return ValueType{value};
            }
            case ColumnDataType::STRING: {
                // Unpack string address
                uint64_t packedAddr;
                std::memcpy(&packedAddr, ptr, sizeof(packedAddr));                
                size_t strOff, strLen;
                StringAddress::unpack(packedAddr, strOff, strLen);

                // Validate string payload is within buffer
                if (strOff + strLen > bufferSize_) {
                    throw std::runtime_error("String payload extends beyond buffer");
                }

                // Create string from payload
                if (strLen > 0) {
                    return ValueType{std::string(buffer_ + strOff, strLen)};
                } else {
                    return ValueType{std::string()};
                }
            }
        }
    }

    void RowView::setLayout(const Layout& layout) 
    {
        columnTypes_ = layout.getColumnTypes();
        fieldLengths_ = std::vector<size_t>(columnTypes_.size());
        fieldOffsets_ = std::vector<size_t>(columnTypes_.size());
        for (size_t i = 0; i < fieldLengths_.size(); ++i) {
            fieldLengths_[i] = binaryFieldLength(columnTypes_[i]);
        }
        for (size_t i = 1; i < fieldOffsets_.size(); ++i) {
            fieldOffsets_[i] = fieldOffsets_[i - 1] + fieldLengths_[i - 1];
        }
    }

    bool RowView::validate() const 
    {
        if(buffer_ == nullptr) {
            return false;
        }

        // Check last fixed offset (this also covers all previous fields)
        if (fieldOffsets_[size()-1] + fieldLengths_[size()-1] > bufferSize_) {
            return false;
        }

        // special check for strings
        for(size_t i = 0; i < size(); ++i) {
            if (columnTypes_[i] == ColumnDataType::STRING) {
                uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + fieldOffsets_[i]);
                size_t strOff, strLen;
                StringAddress::unpack(packedAddr, strOff, strLen);
                if (strOff + strLen > bufferSize_) {
                    return false;
                }
            }
        }
        return true;
    }

    // ========================================================================
    // RowStaticView Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    template<size_t Index>
    auto RowViewStatic<ColumnTypes...>::get() const 
    {
        static_assert(Index < sizeof...(ColumnTypes), "Index out of bounds");

        //validate buffer length
        if (fieldOffsets_[Index] + fieldLengths_[Index] > bufferSize_) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<ColumnType<Index>, std::string>) {
            // Handle string case
            uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + fieldOffsets_[Index]);
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);

            if (strOff + strLen > bufferSize_) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            return std::string_view(buffer_ + strOff, strLen);
        } else {
            // Handle primitive case
            return *reinterpret_cast<const ColumnType<Index>()*>(buffer_ + fieldOffsets_[Index]);
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowViewStatic<ColumnTypes...>::set(const ColumnType<Index>& value) {
        static_assert(Index < sizeof...(ColumnTypes), "Index out of bounds");

        //validate buffer length
        if (fieldOffsets_[Index] + fieldLengths_[Index] > bufferSize_) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<ColumnType<Index>, std::string>) {
            // Handle string case
            uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + fieldOffsets_[Index]);
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);

            if (strOff + strLen > bufferSize_) {
                throw std::runtime_error("String payload extends beyond buffer");
            }

            // Copy string data into buffer (truncate to strLen)
            std::memcpy(buffer_ + strOff, value.data(), strLen);
            if(value.size() < strLen) {
                // Pad with null bytes if shorter
                std::memset(buffer_ + strOff + value.size(), 0, strLen - value.size());
            }
        
        } else {
            // Handle primitive case
            *reinterpret_cast<ColumnType<Index>()*>(buffer_ + fieldOffsets_[Index]) = value;
        }
    }

    template<typename... ColumnTypes>
    bool RowViewStatic<ColumnTypes...>::validate() const 
    {
        if(buffer_ == nullptr) {
            return false;
        }

        // Check if buffer is large enough for all fixed fields
        if (fieldOffsets_[size()-1] + fieldLengths_[size()-1] > bufferSize_) {
            return false;
        }

        // Validate string payloads
        for (size_t i = 0; i < size(); ++i) {
            if constexpr (((std::is_same_v<ColumnTypes, std::string>) || ...)) {
                // Only check strings if we have any
                uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + fieldOffsets_[Index]);
                size_t strOff, strLen;
                StringAddress::unpack(packedAddr, strOff, strLen);
                if (strOff + strLen > bufferSize_) {
                    return false;
                }
            }
        }
        return true;
    }

} // namespace bcsv