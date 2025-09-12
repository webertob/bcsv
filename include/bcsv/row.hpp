#pragma once

/**
 * @file row.hpp
 * @brief Binary CSV (BCSV) Library - Row implementations
 * 
 * This file contains the implementations for the Row and RowStatic classes.
 */

#include "row.h"
#include "layout.h"
#include "byte_buffer.h"
#include <cstring>
#include <variant>

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

    Row::Row(const Layout &layout)
        : layout_(layout), data_(layout.getColumnCount()) 
    {
        for(size_t i = 0; i < layout.getColumnCount(); ++i) {
            data_[i] = defaultValue(layout.getColumnType(i));
        }
    }

    template<typename T>
    const T& Row::get(size_t index) const {
        if (RANGE_CHECKING) {
            return std::get<T>(data_.at(index)); // Will throw
        }
        return std::get<T>(data_[index]);
    }

    template<>
    const ValueType& Row::get(size_t index) const {
        if (RANGE_CHECKING) {
            return data_.at(index); // Will throw
        }
        return data_[index];
    }

    void Row::set(size_t index, const auto& value) {

        // Range check
        if (RANGE_CHECKING && index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }

        using ProvidedType = std::decay_t<decltype(value)>;

        // Visitor to handle assignment with type checking and conversion
        std::visit([this, index, &value](auto&& currentValue) {
            using CurrentType = std::decay_t<decltype(currentValue)>;
            
            // Ensure current variant matches layout (should always be true)
            if (RANGE_CHECKING && toColumnType<CurrentType>() != layout_.getColumnType(index)) {
                throw std::runtime_error("Internal error: variant type doesn't match layout");
            }
            
            if constexpr (std::is_same_v<ProvidedType, CurrentType>) {
                data_[index] = value;  // Direct assignment
            } else if constexpr (std::is_convertible_v<ProvidedType, CurrentType>) {
                data_[index] = static_cast<CurrentType>(value);  // Safe conversion
            } else {
                throw std::runtime_error("Cannot convert " + std::string(typeid(ProvidedType).name()) + 
                                        " to " + std::string(typeid(CurrentType).name()));
            }
        }, data_[index]);
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

    void Row::serializeTo(std::span<std::byte> buffer) const  {
        size_t totalSize = 0;
        size_t fixedSize = 0;
        serializedSize(fixedSize, totalSize);

        if (RANGE_CHECKING && buffer.size() < totalSize) {
            throw std::runtime_error("Buffer too small for serialization");
        }

        std::byte*  ptrFix = buffer.data();             // Pointer to the start of fixed-size data
        std::byte*  ptrStr = buffer.data() + fixedSize; // Pointer to the start of string data
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
                    std::memcpy(ptrFix, &v, sizeof(T));
                    ptrFix += sizeof(T);
                }
            }, value);
        }
        // Zero out any padding bytes to maintain clean data
        std::memset(ptrStr, 0, totalSize - (ptrStr - buffer.data()));
    }

    bool Row::deserializeFrom(const std::span<const std::byte> buffer)
    {
        for(size_t i = 0; i < layout_.getColumnCount(); ++i) {
            ColumnType type = layout_.getColumnType(i);
            size_t len = layout_.getColumnLength(i);
            size_t off = layout_.getColumnOffset(i);

            if (RANGE_CHECKING && (off + len > buffer.size())) {
                std::cerr << "Field end exceeds buffer size" << std::endl;
                return false;
            }

            const std::byte* ptr = buffer.data() + off;
            switch (type) {
                case ColumnType::BOOL: {
                    std::memcpy(&std::get<bool>(data_[i]), ptr, sizeof(bool));
                    break;
                }
                case ColumnType::UINT8: {
                    std::memcpy(&std::get<uint8_t>(data_[i]), ptr, sizeof(uint8_t));
                    break;
                }
                case ColumnType::UINT16: {
                    std::memcpy(&std::get<uint16_t>(data_[i]), ptr, sizeof(uint16_t));
                    break;
                }
                case ColumnType::UINT32: {
                    std::memcpy(&std::get<uint32_t>(data_[i]), ptr, sizeof(uint32_t));
                    break;
                }
                case ColumnType::UINT64: {
                    std::memcpy(&std::get<uint64_t>(data_[i]), ptr, sizeof(uint64_t));
                    break;
                }
                case ColumnType::INT8: {
                    std::memcpy(&std::get<int8_t>(data_[i]), ptr, sizeof(int8_t));
                    break;
                }
                case ColumnType::INT16: {
                    std::memcpy(&std::get<int16_t>(data_[i]), ptr, sizeof(int16_t));
                    break;
                }
                case ColumnType::INT32: {
                    std::memcpy(&std::get<int32_t>(data_[i]), ptr, sizeof(int32_t));
                    break;
                }
                case ColumnType::INT64: {
                    std::memcpy(&std::get<int64_t>(data_[i]), ptr, sizeof(int64_t));
                    break;
                }
                case ColumnType::FLOAT: {
                    std::memcpy(&std::get<float>(data_[i]), ptr, sizeof(float));
                    break;
                }
                case ColumnType::DOUBLE: {
                    std::memcpy(&std::get<double>(data_[i]), ptr, sizeof(double));
                    break;
                }
                case ColumnType::STRING: {
                    size_t strOff, strLen;
                    uint64_t packedAddr;
                    std::memcpy(&packedAddr, ptr, sizeof(uint64_t));
                    StringAddress::unpack(packedAddr, strOff, strLen);
                    if (RANGE_CHECKING && (strOff + strLen > buffer.size())) {
                        std::cerr << "String payload extends beyond buffer" << std::endl;
                        return false;
                    }
                    std::get<std::string>(data_[i]).assign(reinterpret_cast<const char*>(buffer.data() + strOff), strLen);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported column type");
            }
        }
        return true;
    }


    // ========================================================================
    // RowView Implementation
    // ========================================================================

    template<typename T>
    T RowView::get(size_t index) const
    {
        if (toColumnType<T>() != layout_.getColumnType(index)) {
            throw std::runtime_error("Type mismatch");
        }

        size_t len = layout_.getColumnLength(index);
        size_t off = layout_.getColumnOffset(index);
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Field end exceeds buffer size");
        }

        const std::byte* ptr = buffer_.data() + off;
        if constexpr (std::is_same_v<T, std::string>) {
            // Unpack string address
            uint64_t packedAddr;
            std::memcpy(&packedAddr, ptr, sizeof(uint64_t));
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);
            // Validate string payload is within buffer
            if (strOff + strLen > buffer_.size()) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            return std::string(reinterpret_cast<const char*>(buffer_.data()) + strOff, strLen);
        } else {
            T value;
            std::memcpy(&value, ptr, sizeof(T));
            return value;
        }
    }

    template<>
    ValueType RowView::get<ValueType>(size_t index) const
    {
        switch (layout_.getColumnType(index)) {
            case ColumnType::BOOL:
                return ValueType{get<bool>(index)};
            case ColumnType::UINT8:
                return ValueType{get<uint8_t>(index)};
            case ColumnType::UINT16:
                return ValueType{get<uint16_t>(index)};
            case ColumnType::UINT32:
                return ValueType{get<uint32_t>(index)};
            case ColumnType::UINT64:
                return ValueType{get<uint64_t>(index)};
            case ColumnType::INT8:
                return ValueType{get<int8_t>(index)};
            case ColumnType::INT16:
                return ValueType{get<int16_t>(index)};
            case ColumnType::INT32:
                return ValueType{get<int32_t>(index)};
            case ColumnType::INT64:
                return ValueType{get<int64_t>(index)};
            case ColumnType::FLOAT:
                return ValueType{get<float>(index)};
            case ColumnType::DOUBLE:
                return ValueType{get<double>(index)};
            case ColumnType::STRING:
                return ValueType{get<std::string>(index)};
            default:
                throw std::runtime_error("Unsupported column type");
        }
    }
    
    void RowView::set(size_t index, const auto& value) 
    {
        //handle valuetype
        if constexpr (std::is_same_v<decltype(value), ValueType>) {
            // Handle ValueType (variant) - need to visit and extract actual type
            std::visit([this, index](auto&& actualValue) {
                using Type = std::decay_t<decltype(actualValue)>;
                this->setExplicit<Type>(index, actualValue);
            }, value);
            return; // Important: return after handling variant
        }

        // derive type and call setExplicit
        using Type = std::decay_t<decltype(value)>;
        this->setExplicit<Type>(index, value);
    }

    template<typename T>
    void RowView::setExplicit(size_t index, const T& value) 
    {
        // does not support ValueType
        static_assert(!std::is_same_v<T, ValueType>, "setExplicit does not support ValueType");
        if (RANGE_CHECKING && index >= layout_.getColumnCount()) {
            throw std::out_of_range("Index out of range");
        }

        if (toColumnDataType<T>() != layout_.getColumnType(index)) {
            throw std::runtime_error("Type mismatch with layout");
        }

        size_t len = layout_.getColumnLength(index);
        size_t off = layout_.getColumnOffset(index);
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Field end exceeds buffer size");
        }

        std::byte* ptr = buffer_.data() + off;
        if constexpr (std::is_same_v<T, std::string>) {
            // Handle string case - read existing address to get allocation
            uint64_t packedAddr;
            std::memcpy(&packedAddr, ptr, sizeof(uint64_t));
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);

            // Validate string payload is within buffer
            if (strOff + strLen > buffer_.size()) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            std::memcpy(buffer_.data() + strOff, value.data(), std::min(value.size(), strLen));

            // Pad with null bytes if shorter
            if(value.size() < strLen) {
                std::memset(buffer_.data() + strOff + value.size(), 0, strLen - value.size());
            }
        } else {
            // Handle primitive types
            std::memcpy(ptr, &value, sizeof(T));
        }
    }

    Row RowView::toRow() const
    {
        Row row(layout_);
        for(size_t i = 0; i < layout_.getColumnCount(); ++i) {
            ValueType value = get<ValueType>(i);
            row.set(i, value);
        }
        return row;
    }

    bool RowView::validate() const 
    {
        if (layout_.getColumnCount() == 0) {
            return true; // Nothing to validate
        }
        
        // Check last fixed offset (this also covers all previous fields)
        size_t col_count = layout_.getColumnCount();
        if (layout_.getColumnOffset(col_count-1) + layout_.getColumnLength(col_count-1) > buffer_.size()) {
            return false;
        }

        // special check for strings // variant length
        for(size_t i = 0; i < col_count; ++i) {
            if (layout_.getColumnType(i) == ColumnType::STRING) {
                uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_.data() + layout_.getColumnOffset(i));
                size_t strOff, strLen;
                StringAddress::unpack(packedAddr, strOff, strLen);
                if (strOff + strLen > buffer_.size()) {
                    return false;
                }
            }
        }
        return true;
    }




    // ========================================================================
    // RowStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    template<size_t Index>
    auto& RowStatic<ColumnTypes...>::get() {
        static_assert(Index < column_count, "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    const auto& RowStatic<ColumnTypes...>::get() const {
        static_assert(Index < column_count, "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::set(const auto& value) {
        static_assert(Index < column_count, "Index out of bounds");

        using ProvidedType = std::decay_t<decltype(value)>;
        using ExpectedType = column_type<Index>;

        if constexpr (std::is_same_v<ProvidedType, ValueType>) {
            // Handle ValueType (variant) - extract actual type
            std::visit([this](auto&& actualValue) {
                using ActualType = std::decay_t<decltype(actualValue)>;
                if constexpr (std::is_same_v<ActualType, ExpectedType>) {
                    std::get<Index>(data_) = actualValue;
                } else {
                    throw std::runtime_error("ValueType contains wrong type for column " + std::to_string(Index));
                }
            }, value);
        } else if constexpr (std::is_same_v<ProvidedType, ExpectedType>) {
            // Direct assignment for matching types
            std::get<Index>(data_) = value;
        } else if constexpr (std::is_convertible_v<ProvidedType, ExpectedType>) {
            // Safe conversion
            std::get<Index>(data_) = static_cast<ExpectedType>(value);
        } else {
            static_assert(std::is_convertible_v<ProvidedType, ExpectedType>,
                        "Cannot convert provided type to column type");
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::setExplicit(const T& value) {
        static_assert(Index < column_count, "Index out of bounds");

        using ExpectedType = column_type<Index>;

        if constexpr (std::is_same_v<T, ExpectedType>) {
            // Direct assignment for matching types
            std::get<Index>(data_) = value;
        } else if constexpr (std::is_convertible_v<T, ExpectedType>) {
            // Safe conversion
            std::get<Index>(data_) = static_cast<ExpectedType>(value);
        } else {
            static_assert(std::is_convertible_v<T, ExpectedType>,
                        "Cannot convert provided type to column type");
        }
    }

    template<typename... ColumnTypes>
    template<size_t I>
    ValueType RowStatic<ColumnTypes...>::get(size_t index) const {
        if constexpr (I < column_count) {
            if(index == I) {
                return ValueType{std::get<I>(data_)}; 
            } else {
                return this->get<I + 1>(index);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename... ColumnTypes>
    template<size_t I>
    void RowStatic<ColumnTypes...>::set(size_t index, const auto& value) {
        if constexpr (I < column_count) {
            if(index == I) {
                this->set<I>(value);
            } else {
                this->set<I + 1>(index, value);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::serializedSize(size_t& fixedSize, size_t& totalSize) const {
        // Use compile-time constant
        fixedSize = LayoutType::fixed_size;
        totalSize = LayoutType::fixed_size;

        // Calculate variable size for strings only using template recursion
        calculateStringSizes<0>(totalSize);
        
        // Calculate padding needed for 4-byte alignment
        totalSize += (4 - (totalSize % 4)) % 4;
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::calculateStringSizes(size_t& totalSize) const {
        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                totalSize += std::min(std::get<Index>(data_).size(), MAX_STRING_LENGTH);
            }
            // Recursively process next element
            calculateStringSizes<Index + 1>(totalSize);
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::serializeTo(std::span<std::byte> buffer) const {
        size_t totalSize = 0;
        size_t fixedSize = 0;
        serializedSize(fixedSize, totalSize);

        if (RANGE_CHECKING && buffer.size() < totalSize) {
            throw std::runtime_error("Buffer is too small for serialization");
        }

        // Serialize each tuple element using compile-time recursion
        size_t str_offset = fixedSize;
        
        serializeElements<0>(buffer, str_offset);
        // Zero out any remaining padding bytes to maintain clean data
        std::memset(buffer.data() + str_offset, 0, buffer.size() - str_offset);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::serializeElements(std::span<std::byte> &dstBuffer, size_t& strOffset) const {
        if constexpr (Index < column_count) {
            constexpr size_t len = LayoutType::column_lengths[Index];
            constexpr size_t off = LayoutType::column_offsets[Index];

            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t strLength = std::min(std::get<Index>(data_).size(), MAX_STRING_LENGTH);
                size_t strAddress = StringAddress::pack(strOffset, strLength);
                std::memcpy(dstBuffer.data() + off, &strAddress, len);     //write string address field
                if (RANGE_CHECKING && (strOffset + strLength > dstBuffer.size())) {
                    throw std::runtime_error("String data at index " + std::to_string(Index) +
                                               " extends beyond buffer (offset=" + std::to_string(strOffset) +
                                               ", length=" + std::to_string(strLength) +
                                               ", bufferSize=" + std::to_string(dstBuffer.size()) + ")");
                }
                std::memcpy(dstBuffer.data() + strOffset, std::get<Index>(data_).c_str(), strLength); //write string payload
                strOffset += strLength;
            } else {
                std::memcpy(dstBuffer.data() + off, &std::get<Index>(data_), len);
            }
            
            // Recursively process next element
            serializeElements<Index + 1>(dstBuffer, strOffset);
        }
    }

    template<typename... ColumnTypes>
    bool RowStatic<ColumnTypes...>::deserializeFrom(const std::span<const std::byte> buffer)  {
        return deserializeElements<0>(buffer);
    }
    
    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowStatic<ColumnTypes...>::deserializeElements(const std::span<const std::byte> &buffer) {
        if constexpr (Index < column_count) {
            constexpr size_t len = column_lengths[Index];
            constexpr size_t off = column_offsets[Index];

            if (off + len > buffer.size()) {
                std::cerr << "RowStatic::deserializeElements() failed! Buffer overflow while reading." << std::endl;
                return false;
            }

            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t strAddress, strOffset, strLength;
                std::memcpy(&strAddress, buffer.data() + off, len);
                StringAddress::unpack(strAddress, strOffset, strLength);
                if (strOffset + strLength > buffer.size()) {
                    std::cerr << "RowStatic::deserializeElements() failed! Buffer overflow while reading." << std::endl;
                    return false;
                }
                std::get<Index>(data_).assign(reinterpret_cast<const char*>(buffer.data() + strOffset), strLength);
            } else {
                std::memcpy(&std::get<Index>(data_), buffer.data() + off, len);
            }
            
            // Recursively process next element
            return deserializeElements<Index + 1>(buffer);
        } else {
            return true; // All elements processed successfully
        }
    }



    // ========================================================================
    // RowStaticView Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    template<size_t Index>
    auto RowViewStatic<ColumnTypes...>::get() const 
    {
        static_assert(Index < column_count, "Index out of bounds");
        constexpr size_t off = LayoutType::column_offsets[Index];
        constexpr size_t len = LayoutType::column_lengths[Index];

        //validate buffer length
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            // Handle string case
            size_t strAddr, strOff, strLen;
            std::memcpy(&strAddr, buffer_.data() + off, len);
            StringAddress::unpack(strAddr, strOff, strLen);
            if (strOff + strLen > buffer_.size()) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            return std::string_view(reinterpret_cast<const char*>(buffer_.data() + strOff), strLen);
        } else {
            // Handle primitive case
            column_type<Index> val;
            std::memcpy(&val, buffer_.data() + off, len);
            return val;
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowViewStatic<ColumnTypes...>::set(const auto& value) {
        // handle ValueType
        if constexpr (std::is_same_v<decltype(value), ValueType>) {
            this->setExplicit<Index, column_type<Index> >(std::get< column_type<Index> >(value));
        } else {
            this->setExplicit<Index, column_type<Index> >(value);
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowViewStatic<ColumnTypes...>::setExplicit(const T& value) {
        static_assert(Index < column_count, "Index out of bounds");
        static_assert(!std::is_same_v<T, column_type<Index> >, "setExplicit type mismatch");

        constexpr size_t off = LayoutType::column_offsets[Index];
        constexpr size_t len = LayoutType::column_lengths[Index];

        //validate buffer length
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            // Handle string case
            size_t strAdr, strOff, strLen;
            std::memcpy(&strAdr, buffer_.data() + off, len);
            StringAddress::unpack(strAdr, strOff, strLen);
            if (strOff + strLen > buffer_.size()) {
                throw std::runtime_error("String payload extends beyond buffer");
            }

            // Copy string data into buffer (truncate to strLen)
            std::memcpy(buffer_ + strOff, value.data(), std::min(value.size(), strLen));
            if(value.size() < strLen) {
                // Pad with null bytes if shorter
                std::memset(buffer_ + strOff + value.size(), 0, strLen - value.size());
            }
        
        } else {
            // Handle primitive case
            std::memcpy(buffer_ + off, &value, len);
        }
    }


    template<typename... ColumnTypes>
    template<size_t I>
    ValueType RowViewStatic<ColumnTypes...>::get(size_t index) const {
        if constexpr (I < column_count) {
            if(index == I) {
                return ValueType{this->get<I>()}; 
            } else {
                return this->get<I + 1>(index);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename... ColumnTypes>
    template<size_t I>
    void RowViewStatic<ColumnTypes...>::set(size_t index, const auto& value)
    {
        if constexpr (I < column_count) {
            if(index == I) {
                this->set<I>(value);
            } else {
                this->set<I + 1>(index, value);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }

    }

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...> RowViewStatic<ColumnTypes...>::toRow() const
    {
        RowStatic<ColumnTypes...> row;
        for(size_t i = 0; i < column_count; ++i) {
            row.set<i>(this->get<i>());
        }
        return row;
    }

    template<typename... ColumnTypes>
    bool RowViewStatic<ColumnTypes...>::validate() const 
    {
        if(buffer_.empty()) {
            return false;
        }

        if constexpr (column_count == 0) {
            return true; // Nothing to validate
        }

        // Check if buffer is large enough for all fixed fields
        if (LayoutType::column_offsets[column_count-1] + LayoutType::column_lengths[column_count-1] > buffer_.size()) {
            return false;
        }

        // Validate string payloads using template recursion
        return validateStringPayloads<0>();
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowViewStatic<ColumnTypes...>::validateStringPayloads() const {
        if constexpr (Index >= column_count) {
            return true;
        } else {
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t strAdr, strOff, strLen;
                std::memcpy(&strAdr, buffer_.data() + LayoutType::column_offsets[Index], LayoutType::column_lengths[Index]);
                StringAddress::unpack(strAdr, strOff, strLen);
                if (strOff + strLen > buffer_.size()) {
                    return false;
                }
            }
            return validateStringPayloads<Index + 1>();
        }
    }

} // namespace bcsv