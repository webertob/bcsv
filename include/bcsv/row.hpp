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

    Row::Row(std::shared_ptr<Layout> layout) : layout_(layout), data_(layout->getColumnCount()) {
        for(size_t i = 0; i < layout->getColumnCount(); ++i) {
            data_[i] = defaultValue(layout->getColumnType(i));
        }
    }

    template<typename T>
    T Row::getAs(size_t index) const {
        if constexpr (std::is_same_v<T, ValueType>) {
            // Return the ValueType directly
            return this->get(index);
        } else {
            // For specific types, validate and extract
            if (!std::holds_alternative<T>(data_[index])) {
                throw std::runtime_error("ValueType does not contain requested type");
            }        
            return std::get<T>(this->get(index));
        }
    }

    const ValueType& Row::get(size_t index) const {
        if (RANGE_CHECKING && index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }
        return data_[index];
    }

    void Row::set(size_t index, const auto& value) {
        if (RANGE_CHECKING && index >= data_.size()) {
            throw std::out_of_range("Index out of range");
        }

        using ProvidedType = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<ProvidedType, ValueType>) {
            // Handle ValueType (variant) - validate against layout
            data_[index] = value;
        } else {
            // Smart assignment based on current variant state
            std::visit([this, index, &value](auto&& currentValue) {
                using CurrentType = std::decay_t<decltype(currentValue)>;
                
                if constexpr (std::is_same_v<ProvidedType, CurrentType>) {
                    // Exact type match - direct assignment
                    data_[index] = ValueType{value};
                } else if constexpr (std::is_convertible_v<ProvidedType, CurrentType>) {
                    // Safe conversion to existing type
                    data_[index] = ValueType{static_cast<CurrentType>(value)};
                } else {
                    throw std::runtime_error("Cannot convert provided type to variant at index " + std::to_string(index));
                }
            }, data_[index]);
        }
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

    void Row::serializeTo(std::byte* dstBuffer, size_t dstBufferSize) const  {
        if (!dstBuffer || dstBufferSize == 0) {
            throw std::invalid_argument("Invalid destination buffer");
        }

        size_t totalSize = 0;
        size_t fixedSize = 0;
        serializedSize(fixedSize, totalSize);
        if (dstBufferSize < totalSize) {
            throw std::runtime_error("Destination buffer too small");
        }

        std::byte*  ptrFix = dstBuffer;             // Pointer to the start of fixed-size data
        std::byte*  ptrStr = dstBuffer + fixedSize; // Pointer to the start of string data
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

    void Row::setLayout(std::shared_ptr<Layout> layout) {
        if(layout_ == layout) return; //no change
        if(layout_) {
            layout_->removeRow(shared_from_this());
        }
        layout_ = layout;
        if(layout_) {
            layout_->addRow(shared_from_this());
        }
    }

    // Assignment validates layout compatibility
    Row& Row::operator=(const Row& other) {
        if (!layout_->isCompatibleWith(other.getLayout())) {
            throw std::runtime_error("Incompatible layouts");
        }
        data_ = other.data_;
        return *this;
    }

    // ========================================================================
    // RowView Implementation
    // ========================================================================

    template<typename T>
    T RowView::get(size_t index) const
    {
        if constexpr (std::is_same_v<T, ValueType>) {
            switch (layout_->getColumnType(index)) {
                case ColumnDataType::BOOL:
                    return ValueType{this->get<bool>(index)};
                case ColumnDataType::UINT8:
                    return ValueType{this->get<uint8_t>(index)};
                case ColumnDataType::UINT16:
                    return ValueType{this->get<uint16_t>(index)};
                case ColumnDataType::UINT32:
                    return ValueType{this->get<uint32_t>(index)};
                case ColumnDataType::UINT64:
                    return ValueType{this->get<uint64_t>(index)};
                case ColumnDataType::INT8:
                    return ValueType{this->get<int8_t>(index)};
                case ColumnDataType::INT16:
                    return ValueType{this->get<int16_t>(index)};
                case ColumnDataType::INT32:
                    return ValueType{this->get<int32_t>(index)};
                case ColumnDataType::INT64:
                    return ValueType{this->get<int64_t>(index)};
                case ColumnDataType::FLOAT:
                    return ValueType{this->get<float>(index)};
                case ColumnDataType::DOUBLE:
                    return ValueType{this->get<double>(index)};
                case ColumnDataType::STRING:
                    return ValueType{this->get<std::string>(index)};
                default:
                    throw std::runtime_error("Unsupported column type");
            }
        }

        if (buffer_ == nullptr) {
            throw std::runtime_error("Buffer is null");
        }

        if (toColumnDataType<T>() != layout_->getColumnType(index)) {
            throw std::runtime_error("Type mismatch");
        }

        size_t len = layout_->getColumnLength(index);
        size_t off = layout_->getColumnOffset(index);
        if (off + len > bufferSize_) {
            throw std::runtime_error("Field end exceeds buffer size");
        }

        const std::byte* ptr = buffer_ + off;
        if constexpr (std::is_same_v<T, std::string>) {
            // Unpack string address
            uint64_t packedAddr;
            std::memcpy(&packedAddr, ptr, sizeof(uint64_t));
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);
            // Validate string payload is within buffer
            if (strOff + strLen > bufferSize_) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            return std::string(reinterpret_cast<const char*>(buffer_) + strOff, strLen);
        } else {
            T value;
            std::memcpy(&value, ptr, sizeof(T));
            return value;
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
        if (RANGE_CHECKING && index >= layout_->getColumnCount()) {
            throw std::out_of_range("Index out of range");
        }

        if (buffer_ == nullptr) {
            throw std::runtime_error("Buffer is null");
        }

        if (toColumnDataType<T>() != layout_->getColumnType(index)) {
            throw std::runtime_error("Type mismatch with layout");
        }

        size_t len = layout_->getColumnLength(index);
        size_t off = layout_->getColumnOffset(index);
        if (off + len > bufferSize_) {
            throw std::runtime_error("Field end exceeds buffer size");
        }

        std::byte* ptr = buffer_ + off;
        if constexpr (std::is_same_v<T, std::string>) {
            // Handle string case - read existing address to get allocation
            uint64_t packedAddr;
            std::memcpy(&packedAddr, ptr, sizeof(uint64_t));
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);

            // Validate string payload is within buffer
            if (strOff + strLen > bufferSize_) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            std::memcpy(buffer_ + strOff, value.data(), std::min(value.size(), strLen));

            // Pad with null bytes if shorter
            if(value.size() < strLen) {
                std::memset(buffer_ + strOff + value.size(), 0, strLen - value.size()); 
            }
        } else {
            // Handle primitive types
            std::memcpy(ptr, &value, sizeof(T));
        }
    }

    bool RowView::validate() const 
    {
        if(buffer_ == nullptr) {
            return false;
        }

        // Check last fixed offset (this also covers all previous fields)
        size_t col_count = layout_->getColumnCount();
        if (layout_->getColumnOffset(col_count-1) + layout_->getColumnLength(col_count-1) > bufferSize_) {
            return false;
        }

        // special check for strings
        for(size_t i = 0; i < col_count; ++i) {
            if (layout_->getColumnType(i) == ColumnDataType::STRING) {
                uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + layout_->getColumnOffset(i));
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
    // RowStatic Implementation
    // ========================================================================


    template<typename LayoutType>
    template<size_t Index>
    auto& RowStatic<LayoutType>::get() {
        static_assert(Index < LayoutType::getColumnCount(), "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename LayoutType>
    template<size_t Index>
    const auto& RowStatic<LayoutType>::get() const {
        static_assert(Index < LayoutType::getColumnCount(), "Index out of bounds");
        return std::get<Index>(data_);
    }

    template<typename LayoutType>
    template<size_t Index>
    void RowStatic<LayoutType>::set(const auto& value) {
        static_assert(Index < LayoutType::getColumnCount(), "Index out of bounds");
    
        using ProvidedType = std::decay_t<decltype(value)>;
        using ExpectedType = std::tuple_element_t<Index, typename LayoutType::column_types>;

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

    template<typename LayoutType>
    template<size_t Index, typename T>
    void RowStatic<LayoutType>::setExplicit(const T& value) {
        static_assert(Index < LayoutType::getColumnCount(), "Index out of bounds");

        using ExpectedType = std::tuple_element_t<Index, typename LayoutType::column_types>;

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

    template<typename LayoutType>
    template<size_t I>
    ValueType RowStatic<LayoutType>::get(size_t index) const {
        if constexpr (I < LayoutType::getColumnCount()) {
            if(index == I) {
                return ValueType{std::get<I>(data_)}; 
            } else {
                return this->get<T, I + 1>(index);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename LayoutType>
    template<size_t I>
    void RowStatic<LayoutType>::set(size_t index, const auto& value) {
        if constexpr (I < LayoutType::getColumnCount()) {
            if(index == I) {
                this->set<I>(value);
            } else {
                this->set<I + 1>(index, value);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename LayoutType>
    void RowStatic<LayoutType>::serializedSize(size_t& fixedSize, size_t& totalSize) const {
        // Use compile-time constant
        fixedSize = LayoutType::FIXED_SIZE;
        totalSize = LayoutType::FIXED_SIZE;

        // Calculate variable size for strings only
        for (size_t i = 0; i < LayoutType::getColumnCount(); ++i) {
            if constexpr (std::is_same_v<column_type<i>, std::string>) {
                totalSize += std::min(std::get<i>(data_).size(), MAX_STRING_LENGTH);
            }
        }        
        // Calculate padding needed for 4-byte alignment
        totalSize += (4 - (totalSize % 4)) % 4;
    }

    template<typename LayoutType>
    void RowStatic<LayoutType>::serializeTo(std::byte* dstBuffer, size_t dstBufferSize) const {
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
        for (size_t i = 0; i < LayoutType::getColumnCount(); ++i) {
            constexpr size_t len = fieldLengths_[i];
            constexpr size_t off = fieldOffsets_[i];
            std::byte* ptr = dstBuffer + off;
            if constexpr (std::is_same_v<column_type<i>, std::string>) {
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
                std::memcpy(ptr, &std::get<i>(data_), len);
            }
        }
        // Zero out padding bytes
        std::memset(dstBuffer + strOffset, 0, totalSize - strOffset);
    }

    template<typename LayoutType>
    void RowStatic<LayoutType>::deserializeFrom(const std::byte* srcBuffer, size_t srcBufferSize)
    {
        if (!srcBuffer || srcBufferSize == 0) {
            throw std::invalid_argument("Invalid source buffer");
        }

        for (size_t i = 0; i < LayoutType::getColumnCount(); ++i) {
            constexpr size_t len = LayoutType::column_lengths[i];
            constexpr size_t off = LayoutType::column_offsets[i];
            const std::byte* ptr = srcBuffer + off;
            if(off+len > srcBufferSize) {
                throw std::runtime_error("Buffer overflow reading element at index " + std::to_string(i) +
                                           " (offset=" + std::to_string(off) + ", length=" + std::to_string(len) +
                                           ", bufferSize=" + std::to_string(srcBufferSize) + ")");
            }

            if constexpr (std::is_same_v<column_type<i>, std::string>) {
                size_t strAddress, strOffset, strLength;
                std::memcpy(&strAddress, ptr, len);
                StringAddress::unpack(strAddress, strOffset, strLength);

                if (strOffset + strLength > srcBufferSize) {
                    throw std::runtime_error("String data at index " + std::to_string(i) +
                                              " extends beyond buffer (offset=" + std::to_string(strOffset) +
                                              ", length=" + std::to_string(strLength) +
                                              ", bufferSize=" + std::to_string(srcBufferSize) + ")");
                }
                std::get<i>(data_) = std::string(srcBuffer + strOffset, strLength);
                // Read packed string address
            } else {
                std::memcpy(&std::get<i>(data_), ptr, len);
            }
        }
    }



    // ========================================================================
    // RowStaticView Implementation
    // ========================================================================

    template<typename LayoutType>
    template<size_t Index>
    auto RowViewStatic<LayoutType>::get() const 
    {
        static_assert(Index < LayoutType::getColumnCount(), "Index out of bounds");

        //validate buffer length
        if (LayoutType::column_offsets[Index] + LayoutType::column_lengths[Index] > bufferSize_) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            // Handle string case
            uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + LayoutType::column_offsets[Index]);
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);

            if (strOff + strLen > bufferSize_) {
                throw std::runtime_error("String payload extends beyond buffer");
            }
            return std::string_view(buffer_ + strOff, strLen);
        } else {
            // Handle primitive case
            return *reinterpret_cast<const column_type<Index>()*>(buffer_ + LayoutType::column_offsets[Index]);
        }
    }

    template<typename LayoutType>
    template<size_t Index>
    void RowViewStatic<LayoutType>::set(const auto& value) {
        // handle ValueType
        if constexpr (std::is_same_v<decltype(value), ValueType>) {
            this->setExplicit<Index, column_type<Index>>(std::get<column_type<Index>>(value));
        } else {
            this->setExplicit<Index, column_type<Index>>(value);
        }
    }

    template<typename LayoutType>
    template<size_t Index, typename T>
    void RowViewStatic<LayoutType>::setExplicit(const T& value) {
        static_assert(Index < LayoutType::getColumnCount(), "Index out of bounds");
        static_assert(!std::is_same_v<T, column_type<Index>>, "setExplicit type mismatch");

        //validate buffer length
        if (LayoutType::column_offsets[Index] + LayoutType::column_lengths[Index] > bufferSize_) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            // Handle string case
            uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_ + LayoutType::column_offsets[Index]);
            size_t strOff, strLen;
            StringAddress::unpack(packedAddr, strOff, strLen);

            if (strOff + strLen > bufferSize_) {
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
            *reinterpret_cast<column_type<Index>*>(buffer_ + LayoutType::column_offsets[Index]) = value;
        }
    }


    template<typename LayoutType>
    template<size_t I>
    ValueType RowViewStatic<LayoutType>::get(size_t index) const {
        if constexpr (I < LayoutType::getColumnCount()) {
            if(index == I) {
                return ValueType{this->get<I>()}; 
            } else {
                return this->get<I + 1>(index);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename LayoutType>
    template<size_t I>
    void RowViewStatic<LayoutType>::set(size_t index, const auto& value)
    {
        if constexpr (I < LayoutType::getColumnCount()) {
            if(index == I) {
                this->set<I>(value);
            } else {
                this->set<I + 1>(index, value);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }

    }


    template<typename LayoutType>
    bool RowViewStatic<LayoutType>::validate() const 
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

} // namespace bcsv