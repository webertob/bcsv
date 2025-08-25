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
    RowStatic<ColumnTypes...>::RowStatic() : data_(extractDefaultValueT<ColumnTypes>()...) {
        // Uses default construction for each type
    }

    template<typename... ColumnTypes>
    template<typename... Args>
    RowStatic<ColumnTypes...>::RowStatic(Args&&... args) : data_(std::forward<Args>(args)...) {
        static_assert(sizeof...(Args) == sizeof...(ColumnTypes), 
                      "Number of arguments must match number of column types");
    }

    template<typename... ColumnTypes>
    RowStatic<ColumnTypes...>::RowStatic(const Row& other) {
        if (other.size() != sizeof...(ColumnTypes)) {
            throw std::invalid_argument("Column count mismatch");
        }
        copyFromRow(other, std::make_index_sequence<sizeof...(ColumnTypes)>{});
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::copyFromRowAtIndex(const Row& row) {
        using ColumnType = std::tuple_element_t<Index, std::tuple<ColumnTypes...>>;
        ValueType variant = row.getValue(Index);
        
        if (!std::holds_alternative<ColumnType>(variant)) {
            throw std::invalid_argument(
                "Type mismatch at index " + std::to_string(Index) + 
                ": expected " + typeid(ColumnType).name()
            );
        }
        
        std::get<Index>(data_) = std::get<ColumnType>(variant);
    }

    template<typename... ColumnTypes>
    template<size_t... Indices>
    void RowStatic<ColumnTypes...>::copyFromRow(const Row& row, std::index_sequence<Indices...>) {
        (copyFromRowAtIndex<Indices>(row), ...);
    }

    template<typename... ColumnTypes>
    template<size_t... Indices>
    void RowStatic<ColumnTypes...>::setValueAtIndex(size_t index, const ValueType& value, std::index_sequence<Indices...>) {
        bool success = false;
        ((index == Indices ? (setValueAtIndexHelper<Indices>(value), success = true) : false), ...);
        
        if (!success) {
            throw std::out_of_range("Index " + std::to_string(index) + " out of range");
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::setValueAtIndexHelper(const ValueType& value) {
        using ColumnType = std::tuple_element_t<Index, std::tuple<ColumnTypes...>>;
        
        if (!std::holds_alternative<ColumnType>(value)) {
            throw std::invalid_argument(
                "Type mismatch at index " + std::to_string(Index) + 
                ": expected " + typeid(ColumnType).name()
            );
        }
        
        std::get<Index>(data_) = std::get<ColumnType>(value);
    }

    template<typename... ColumnTypes>
    template<size_t... Indices>
    ValueType RowStatic<ColumnTypes...>::getValueAtIndex(size_t index, std::index_sequence<Indices...>) const {
        ValueType result;
        bool found = false;
        ((index == Indices ? (result = ValueType{std::get<Indices>(data_)}, found = true) : false), ...);
        
        if (!found) {
            throw std::out_of_range("Index " + std::to_string(index) + " out of range");
        }
        return result;
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::setValue(size_t index, const ValueType& value) {
        setValueAtIndex(index, value, std::make_index_sequence<sizeof...(ColumnTypes)>{});
    }

    template<typename... ColumnTypes>
    ValueType RowStatic<ColumnTypes...>::getValue(size_t index) const {
        return getValueAtIndex(index, std::make_index_sequence<sizeof...(ColumnTypes)>{});
    }

    template<typename... ColumnTypes>
    size_t RowStatic<ColumnTypes...>::size() const {
        return sizeof...(ColumnTypes);
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
        
        // Optimized: skip string calculation if no strings
        if constexpr (HAS_STRINGS) {
            // Calculate variable size for strings only
            std::apply([&](const auto&... elements) {
                ((std::is_same_v<std::decay_t<decltype(elements)>, std::string> ?
                (totalSize += std::min(elements.size(), MAX_STRING_LENGTH)) :
                void(0)), ...);
            }, data_);
        }
        
        // Calculate padding needed for 4-byte alignment
        size_t paddingBytes = (4 - (totalSize % 4)) % 4;
        totalSize += paddingBytes;
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::serializeTo(char* dstBuffer, size_t dstBufferSize) const {
        if (!dstBuffer || dstBufferSize == 0) {
            throw std::invalid_argument("Invalid destination buffer");
        }

        // Use compile-time constants where possible
        constexpr size_t fixedSize = FIXED_SIZE;
        size_t totalSize = fixedSize;
        
        // Calculate total size (reuse logic from serializedSize)
        if constexpr (HAS_STRINGS) {
            std::apply([&](const auto&... elements) {
                ((std::is_same_v<std::decay_t<decltype(elements)>, std::string> ?
                (totalSize += std::min(elements.size(), MAX_STRING_LENGTH)) :
                void(0)), ...);
            }, data_);
            size_t paddingBytes = (4 - (totalSize % 4)) % 4;
            totalSize += paddingBytes;
        } else {
            // Compile-time calculation for string-free rows
            constexpr size_t padding = (4 - (fixedSize % 4)) % 4;
            totalSize = fixedSize + padding;
        }

        if (dstBufferSize < totalSize) {
            throw std::runtime_error("Destination buffer too small");
        }

        char* ptrFix = dstBuffer;
        char* ptrStr = dstBuffer + fixedSize;
        size_t strOff = fixedSize;

        // Serialize each tuple element
        std::apply([&](const auto&... elements) {
            ((std::is_same_v<std::decay_t<decltype(elements)>, std::string> ?
            ([&]() {
                size_t len = std::min(elements.size(), MAX_STRING_LENGTH);
                uint64_t addr = StringAddress::pack(strOff, len);
                std::memcpy(ptrFix, &addr, sizeof(addr));
                ptrFix += sizeof(addr);
                std::memcpy(ptrStr, elements.c_str(), len);
                ptrStr += len;
                strOff += len;
            }()) :
            ([&]() {
                std::memcpy(ptrFix, &elements, sizeof(elements));
                ptrFix += sizeof(elements);
            }())), ...);
        }, data_);

        // Zero out padding bytes
        std::memset(ptrStr, 0, totalSize - (ptrStr - dstBuffer));
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::deserializeFrom(const char* srcBuffer, size_t srcBufferSize) {
        if (!srcBuffer || srcBufferSize == 0) {
            throw std::invalid_argument("Invalid source buffer");
        }

        // Use compile-time constant for fixed size
        constexpr size_t fixedSize = FIXED_SIZE;
        
        if (srcBufferSize < fixedSize) {
            throw std::runtime_error("Source buffer too small for fixed section");
        }

        // Deserialize using index sequence to handle each tuple element
        deserializeAtIndices(srcBuffer, srcBufferSize, std::make_index_sequence<sizeof...(ColumnTypes)>{});
    }

    template<typename... ColumnTypes>
    template<size_t... Indices>
    void RowStatic<ColumnTypes...>::deserializeAtIndices(const char* srcBuffer, size_t srcBufferSize, std::index_sequence<Indices...>) {
        const char* fixedPtr = srcBuffer;
        
        // Process each tuple element in order using fold expression
        (deserializeAtIndex<Indices>(srcBuffer, srcBufferSize, fixedPtr), ...);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::deserializeAtIndex(const char* srcBuffer, size_t srcBufferSize, const char*& fixedPtr) {
        using ColumnType = std::tuple_element_t<Index, std::tuple<ColumnTypes...>>;
        
        if constexpr (std::is_same_v<ColumnType, std::string>) {
            // Deserialize string using StringAddress
            if (fixedPtr + sizeof(uint64_t) > srcBuffer + srcBufferSize) {
                throw std::runtime_error("Buffer overflow reading string address at index " + std::to_string(Index));
            }
            
            // Read packed string address
            uint64_t packedAddr;
            std::memcpy(&packedAddr, fixedPtr, sizeof(packedAddr));
            fixedPtr += sizeof(packedAddr);
            
            // Unpack address information
            size_t payloadOffset, stringLength;
            StringAddress::unpack(packedAddr, payloadOffset, stringLength);
            
            // Validate offset and length are within buffer bounds
            if (payloadOffset + stringLength > srcBufferSize) {
                throw std::runtime_error("String data at index " + std::to_string(Index) + 
                                        " extends beyond buffer (offset=" + std::to_string(payloadOffset) + 
                                        ", length=" + std::to_string(stringLength) + 
                                        ", bufferSize=" + std::to_string(srcBufferSize) + ")");
            }
            
            // Read string content from payload section
            if (stringLength > 0) {
                std::get<Index>(data_) = std::string(srcBuffer + payloadOffset, stringLength);
            } else {
                std::get<Index>(data_) = std::string{};
            }
            
        } else {
            // Deserialize fixed-size primitive type
            constexpr size_t elementSize = sizeof(ColumnType);
            
            if (fixedPtr + elementSize > srcBuffer + srcBufferSize) {
                throw std::runtime_error("Buffer overflow reading element at index " + std::to_string(Index) + 
                                        " (elementSize=" + std::to_string(elementSize) + ")");
            }
            
            ColumnType value;
            std::memcpy(&value, fixedPtr, elementSize);
            fixedPtr += elementSize;
            
            std::get<Index>(data_) = value;
        }
    }

} // namespace bcsv