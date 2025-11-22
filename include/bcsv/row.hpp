/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file row.hpp
 * @brief Binary CSV (BCSV) Library - Row implementations
 * 
 * This file contains the implementations for the Row and RowStatic classes.
 */

#include "bcsv/definitions.h"
#include "row.h"
#include "layout.h"
#include "byte_buffer.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <cassert>
#include <variant>
#include "string_addr.h"

namespace bcsv {

    // ========================================================================
    // Row Implementation
    // ========================================================================

    inline Row::Row(const Layout &layout)
        : layout_(layout), data_(layout.columnCount()) 
    {
        for(size_t i = 0; i < layout.columnCount(); ++i) {
            data_[i] = defaultValue(layout.columnType(i));
        }
    }

    /** Clear the row to its default state (default values) */
    inline void Row::clear()
    {
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            data_[i] = defaultValue(layout_.columnType(i));
        }
        if(tracksChanges()) {
            changes_.set(); // mark everything as changed
        }
    }

    /** Enable or disable change tracking (i.e. used for Zero-Order-Hold compression)*/
    inline void Row::trackChanges(bool enable)
    {
        if (enable) {            
            changes_.resize(layout_.columnCount());   // we start with all bit set true to indicate everything has "changed"
            changes_.set();
        } else {
            changes_.clear();                               // disable tracking
        }
    }

    /** Check if change tracking is enabled */ 
    inline bool Row::tracksChanges() const
    {
        return !changes_.empty();
    }
    
    /** Get the value at the specified column index */
    template<typename T>
    const T& Row::get(size_t index) const {
        if (RANGE_CHECKING) {
            return std::get<T>(data_.at(index)); // Will throw
        }
        return std::get<T>(data_[index]);
    }
   

    /** Get the value at the specified column index */
    template<>
    inline const ValueType& Row::get(size_t index) const {
        if (RANGE_CHECKING) {
            return data_.at(index); // Will throw
        }
        return data_[index];
    }

    /** Vectorized access to multiple columns of same type */
    template<typename T>
    void Row::get(size_t index, std::span<T> dst) const
    {
        if (RANGE_CHECKING) {
            // Only check highest index (implicitly checks for all others)
            if (index + dst.size() > layout_.columnCount()) {
                throw std::out_of_range("Index out of range");
            }
        }

        for (size_t i = 0; i < dst.size(); ++i) {
            dst[i] = std::get<T>(data_[index + i]);
        }
    }

    template<>
    inline void Row::set<std::string>(size_t index, const std::string& value) {
        if (RANGE_CHECKING && index >= layout_.columnCount()) {
            throw std::out_of_range("Index out of range");
        }

        // sanity checks
        assert(layout_.columnType(index) == ColumnType::STRING);
        if(tracksChanges() && (std::get<std::string>(data_[index]) != value)) {
            changes_.set(index);
        }
        std::get<std::string>(data_[index]).assign(value.c_str(), std::min(value.size(), MAX_STRING_LENGTH));
    }

    template<>
    inline void Row::set<ValueType>(size_t index, const ValueType& value) {
        // Determine actual type and use that type in a recursive call
        std::visit([this, index](auto&& v) {
            using ActualType = std::decay_t<decltype(v)>;
            this->set<ActualType>(index, v); 
        }, value);
    }

    template<typename T>
    inline void Row::set(size_t index, const T& value) {
        // we know have a concrete type T here
        if (RANGE_CHECKING && index >= layout_.columnCount()) {
            throw std::out_of_range("Index out of range");
        }
        // use visitor pattern to handle assignment with type checking and conversion
        std::visit([this, index, &value](auto&& data) {
            using DataType = std::decay_t<decltype(data)>;
            assert(toColumnType<DataType>() == layout_.columnType(index)); // sanity check
            if constexpr (std::is_convertible_v<T, DataType>) {
                if(tracksChanges() && (data != static_cast<DataType>(value))) {
                    changes_[index] = true; // mark this column as changed
                }
                data = static_cast<DataType>(value);  // Safe conversion
            } else {
                throw std::runtime_error("Cannot convert " + std::string(typeid(T).name()) + " to " + std::string(typeid(DataType).name()));
            }
        }, data_[index]);
    }

    /** Vectorized set of multiple columns of same type */
    template<typename T>
    void Row::set(size_t index, std::span<const T> src)
    {
        if (RANGE_CHECKING) {
            // Only check highest index (implicitly checks for all others)
            if (index + src.size() > layout_.columnCount()) {
                throw std::out_of_range("Index out of range");
            }
        }

        bool tracked = tracksChanges();
        for (size_t i = 0; i < src.size(); ++i) {
            size_t idx = index + i;
            std::visit([this, i, idx, src, tracked](auto&& data) {
                using DataType = std::decay_t<decltype(data)>;
                assert(toColumnType<DataType>() == layout_.columnType(idx)); // sanity check
                if constexpr (std::is_convertible_v<T, DataType>) {
                    if (tracked && (data != static_cast<DataType>(src[i]))) {
                        changes_[idx] = true; // mark this column as changed
                    }
                    data = static_cast<DataType>(src[i]);  // Safe conversion
                } else {
                    throw std::runtime_error("Cannot convert " + std::string(typeid(T).name()) + " to " + std::string(typeid(DataType).name()));
                }
            }, data_[idx]);
        }
    }

    /** Serialize the row into the provided buffer, appending data to the end of the buffer.
    * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
    * @return A span pointing to the serialized row data within the buffer.
    */
    inline std::span<std::byte> Row::serializeTo(ByteBuffer& buffer) const  {
        size_t  offRow = buffer.size(); // offset to the begin of this row (both fixed and variable data)
        size_t  offFix = offRow;        // offset to the begin of fixed-size data section (we don't use pointers as they may become invalid after resize)
        size_t  offVar = offRow + layout_.serializedSizeFixed(); //offset to the begin of variable-size data section
        buffer.resize(buffer.size() + layout_.serializedSizeFixed());

        for (const auto& value : data_) {
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    size_t strLen = std::min(v.length(), MAX_STRING_LENGTH);
                    StringAddr strAddr(offVar - offRow, strLen);  // Make address relative to row start
                    std::memcpy(buffer.data() + offFix, &strAddr, sizeof(strAddr));
                    offFix += sizeof(strAddr);
                    buffer.resize(buffer.size() + strLen);
                    std::memcpy(buffer.data() + offVar, v.c_str(), strLen);
                    offVar += strLen;
                } else {
                    std::memcpy(buffer.data() + offFix, &v, sizeof(T));
                    offFix += sizeof(T);
                }
            }, value);
        }
        return {buffer.data() + offRow, buffer.size() - offRow};
    }

    inline void Row::deserializeFrom(const std::span<const std::byte> buffer)
    {
        if (layout_.serializedSizeFixed() > buffer.size()) {
            throw std::runtime_error("Row::deserializeFrom failed as buffer is too short.");
        }
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            const std::byte* ptr = buffer.data() + layout_.columnOffset(i);
            ColumnType type = layout_.columnType(i);
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
                    StringAddr strAddr;
                    std::memcpy(&strAddr, ptr, sizeof(strAddr));
                    auto [strOff, strLen] = strAddr.unpack();
                    if (strOff + strLen > buffer.size()) {
                        throw std::runtime_error("String payload extends beyond buffer");
                    }
                    std::get<std::string>(data_[i]).assign(reinterpret_cast<const char*>(buffer.data() + strOff), strLen);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported column type");
            }
        }
    }

    /** Serialize the row into the provided buffer using Zero-Order-Hold (ZoH) compression.
     * Only the columns that have changed (as indicated by the change tracking bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change bitset.
     * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
     * @return A span pointing to the serialized row data within the buffer.
     */
    inline std::span<std::byte> Row::serializeToZoH(ByteBuffer& buffer) const {
        assert(tracksChanges() && "Change tracking must be enabled for ZoH serialization");
        if(!hasAnyChanges()) {
            return {buffer.data(), 0}; // nothing to serialize
        }

        // reserve space to store change bitset
        size_t offRow = buffer.size();      // offset to the begin of this row
        buffer.resize(buffer.size() + changes_.sizeBytes());
        
        // Serialize each element that has changed
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);           
            if (type == ColumnType::BOOL) {
                // Special handling for bools: always serialize but store as single bit in changes_
                changes_.set(i, std::get<bool>(data_[i]));
            } else if (changes_.test(i)) {
                size_t off = buffer.size();
                if(type == ColumnType::STRING) {
                    // Special handling for strings - encoding in ZoH mode happens in place
                    const auto& str = std::get<std::string>(data_[i]);
                    uint16_t strLength = static_cast<uint16_t>(std::min(str.size(), MAX_STRING_LENGTH));
                    buffer.resize(buffer.size() + sizeof(strLength) + strLength);
                    memcpy(buffer.data() + off, &strLength, sizeof(strLength));
                    memcpy(buffer.data() + off + sizeof(strLength), str.data(), strLength);
                } else if (type == ColumnType::UINT8) {
                    buffer.resize(buffer.size() + sizeof(uint8_t));
                    memcpy(buffer.data() + off, &std::get<uint8_t>(data_[i]), sizeof(uint8_t));
                } else if (type == ColumnType::UINT16) {
                    buffer.resize(buffer.size() + sizeof(uint16_t));
                    memcpy(buffer.data() + off, &std::get<uint16_t>(data_[i]), sizeof(uint16_t));
                } else if (type == ColumnType::UINT32) {
                    buffer.resize(buffer.size() + sizeof(uint32_t));
                    memcpy(buffer.data() + off, &std::get<uint32_t>(data_[i]), sizeof(uint32_t));
                } else if (type == ColumnType::UINT64) {
                    buffer.resize(buffer.size() + sizeof(uint64_t));
                    memcpy(buffer.data() + off, &std::get<uint64_t>(data_[i]), sizeof(uint64_t));
                } else if (type == ColumnType::INT8) {
                    buffer.resize(buffer.size() + sizeof(int8_t));
                    memcpy(buffer.data() + off, &std::get<int8_t>(data_[i]), sizeof(int8_t));
                } else if (type == ColumnType::INT16) {
                    buffer.resize(buffer.size() + sizeof(int16_t));
                    memcpy(buffer.data() + off, &std::get<int16_t>(data_[i]), sizeof(int16_t));
                } else if (type == ColumnType::INT32) {
                    buffer.resize(buffer.size() + sizeof(int32_t));
                    memcpy(buffer.data() + off, &std::get<int32_t>(data_[i]), sizeof(int32_t));
                } else if (type == ColumnType::INT64) {
                    buffer.resize(buffer.size() + sizeof(int64_t));
                    memcpy(buffer.data() + off, &std::get<int64_t>(data_[i]), sizeof(int64_t));
                } else if (type == ColumnType::FLOAT) {
                    buffer.resize(buffer.size() + sizeof(float));
                    memcpy(buffer.data() + off, &std::get<float>(data_[i]), sizeof(float));
                } else if (type == ColumnType::DOUBLE) {
                    buffer.resize(buffer.size() + sizeof(double));
                    memcpy(buffer.data() + off, &std::get<double>(data_[i]), sizeof(double));
                } else {
                    throw std::runtime_error("Unsupported column type in ZoH serialization");
                }
            }
        }

        // Write change bitset to reserved location (we do it at the end as we encode bools into the bitset during element serialization)
        std::memcpy(buffer.data() + offRow, changes_.data(), changes_.sizeBytes());
        return {buffer.data() + offRow, buffer.size() - offRow};
    }

    inline void Row::deserializeFromZoH(const std::span<const std::byte> buffer) {
        trackChanges(true); // ensure change tracking is enabled
        // We expect the buffer to start with the change bitset, followed by the actual row data
        if (buffer.size() < changes_.sizeBytes()) {
            throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small to contain change bitset.");
        }
        
        // Read change bitset from beginning of buffer
        std::memcpy(changes_.data(), buffer.data(), changes_.sizeBytes());
        auto dataBuffer = buffer.subspan(changes_.sizeBytes());
        
        // Deserialize each element that has changed
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            if (type == ColumnType::BOOL) {
                // Special handling for bools: always deserialize from bitset
                std::get<bool>(data_[i]) = changes_.test(i);
            } else if (changes_.test(i)) {
                // All other types: only deserialize if marked as changed
                if(type == ColumnType::STRING) {
                    // Special handling for strings
                    if (dataBuffer.size() < sizeof(uint16_t)) {
                        throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small for string length.");
                    }
                    uint16_t strLength;
                    std::memcpy(&strLength, dataBuffer.data(), sizeof(uint16_t));
                    if (dataBuffer.size() < sizeof(uint16_t) + strLength) {
                        throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small for string payload.");
                    }
                    std::get<std::string>(data_[i]).assign(reinterpret_cast<const char*>(dataBuffer.data() + sizeof(uint16_t)), strLength);
                    dataBuffer = dataBuffer.subspan(sizeof(uint16_t) + strLength);
                    continue;
                } else {
                    if (dataBuffer.size() < layout_.columnLength(i)) {
                        throw std::runtime_error("Row::deserializeFromZoH() failed! Buffer too small for column.");
                    }
                    if(type == ColumnType::UINT8) {
                        std::memcpy(&std::get<uint8_t>(data_[i]), dataBuffer.data(), sizeof(uint8_t));
                    } else if(type == ColumnType::UINT16) {
                        std::memcpy(&std::get<uint16_t>(data_[i]), dataBuffer.data(), sizeof(uint16_t));
                    } else if(type == ColumnType::UINT32) {
                        std::memcpy(&std::get<uint32_t>(data_[i]), dataBuffer.data(), sizeof(uint32_t));
                    } else if(type == ColumnType::UINT64) {
                        std::memcpy(&std::get<uint64_t>(data_[i]), dataBuffer.data(), sizeof(uint64_t));
                    } else if(type == ColumnType::INT8) {
                        std::memcpy(&std::get<int8_t>(data_[i]), dataBuffer.data(), sizeof(int8_t));
                    } else if(type == ColumnType::INT16) {
                        std::memcpy(&std::get<int16_t>(data_[i]), dataBuffer.data(), sizeof(int16_t));
                    } else if(type == ColumnType::INT32) {
                        std::memcpy(&std::get<int32_t>(data_[i]), dataBuffer.data(), sizeof(int32_t));
                    } else if(type == ColumnType::INT64) {
                        std::memcpy(&std::get<int64_t>(data_[i]), dataBuffer.data(), sizeof(int64_t));
                    } else if(type == ColumnType::FLOAT) {
                        std::memcpy(&std::get<float>(data_[i]), dataBuffer.data(), sizeof(float));
                    } else if(type == ColumnType::DOUBLE) {
                        std::memcpy(&std::get<double>(data_[i]), dataBuffer.data(), sizeof(double));
                    } else {
                        throw std::runtime_error("Unsupported column type in ZoH deserialization");
                    }
                    dataBuffer = dataBuffer.subspan(layout_.columnLength(i));
                    continue; // move to next column
                }
            }
        }
    }

    // ========================================================================
    // RowView Implementation
    // ========================================================================
    template<>
    inline ValueType RowView::get<ValueType>(size_t index) const
    {
        switch (layout_.columnType(index)) {
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

    template<typename T>
    inline T RowView::get(size_t index) const
    {
        if (toColumnType<T>() != layout_.columnType(index)) {
            throw std::runtime_error("Type mismatch");
        }

        size_t len = layout_.columnLength(index);
        size_t off = layout_.columnOffset(index);
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Field end exceeds buffer size");
        }

        const std::byte* ptr = buffer_.data() + off;
        if constexpr (std::is_same_v<T, std::string>) {
            // Unpack string address
            StringAddr strAddr;
            std::memcpy(&strAddr, ptr, sizeof(strAddr));
            auto [strOff, strLen] = strAddr.unpack();
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

    inline void RowView::set(size_t index, const auto& value) 
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
        if (RANGE_CHECKING && index >= layout_.columnCount()) {
            throw std::out_of_range("Index out of range");
        }

        if (toColumnType<T>() != layout_.columnType(index)) {
            throw std::runtime_error("Type mismatch with layout");
        }

        size_t len = layout_.columnLength(index);
        size_t off = layout_.columnOffset(index);
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Field end exceeds buffer size");
        }

        std::byte* ptr = buffer_.data() + off;
        if constexpr (std::is_same_v<T, std::string>) {
            // Handle string case - read existing address to get allocation
            StringAddr strAddr;
            std::memcpy(&strAddr, ptr, sizeof(strAddr));
            auto [strOff, strLen] = strAddr.unpack();
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

    inline Row RowView::toRow() const
    {
        Row row(layout_);
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ValueType value = get<ValueType>(i);
            row.set(i, value);
        }
        return row;
    }

    inline bool RowView::validate() const 
    {
        if (layout_.columnCount() == 0) {
            return true; // Nothing to validate
        }
        
        // Check last fixed offset (this also covers all previous fields)
        size_t col_count = layout_.columnCount();
        if (layout_.columnOffset(col_count-1) + layout_.columnLength(col_count-1) > buffer_.size()) {
            return false;
        }

        // special check for strings // variant length
        for(size_t i = 0; i < col_count; ++i) {
            if (layout_.columnType(i) == ColumnType::STRING) {
                StringAddr strAddr;
                memcpy(&strAddr, buffer_.data() + layout_.columnOffset(i), sizeof(strAddr));
                auto [strOff, strLen] = strAddr.unpack();
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

    /** Clear the row to its default state (default values) */
    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::clear()
    {
        clearHelper<0>();
        if(trackChanges()) {
            changes_.set(); // mark everything as changed initially
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::clearHelper()
    {
        if constexpr (Index < column_count) {
            std::get<Index>(data_) = defaultValueT<column_type<Index>>();
            clearHelper<Index + 1>();
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::trackChanges(bool enable)
    {
        tracks_changes_ = enable;   // enable or disable tracking
        if (enable) {
            changes_.set();         // mark everything as changed initially
        } else {
            changes_.reset();       // mark all as unchanged (to have a defined state)
        }
    }

    /** Get the value at the specified column index */
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
    void RowStatic<ColumnTypes...>::set(const ValueType& value) {
        //unpack variant and call set with actual type
        std::visit([this](auto&& v) {
            this->set<Index>(v);
        }, value);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::set(const std::string& value) {
        static_assert(Index < column_count, "Index out of bounds");
        static_assert(std::is_same_v<column_type<Index>, std::string>, "Column type is not string");
        if(trackChanges() && (std::get<Index>(data_) != value)) {
            changes_.set(Index);
        }
        std::get<Index>(data_).assign(value.c_str(), std::min(value.size(), MAX_STRING_LENGTH));
    }


    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::set(const T& value) {
        static_assert(Index < column_count, "Index out of bounds");
        static_assert(std::is_convertible_v<T, column_type<Index>>, "ValueType cannot be converted to column type");
        
        if(trackChanges() && (std::get<Index>(data_) != value)) {
            changes_.set(Index);
        }
        std::get<Index>(data_) = value;
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    ValueType RowStatic<ColumnTypes...>::get(size_t index) const {
        if constexpr (Index < column_count) {
            if(index == Index) {
                return ValueType{std::get<Index>(data_)}; 
            } else {
                return this->get<Index + 1>(index);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::set(size_t index, const T& value) {
        if constexpr (Index < column_count) {
            if(index == Index) {
                this->set<Index, T>(value);
            } else {
                this->set<Index + 1, T>(index, value);
            }
        } else {
            throw std::out_of_range("Index out of range");
        }
    }

    // ========================================================================
    // RowStatic Vectorized Access - Compile-Time Indexed
    // ========================================================================

    /** Compile-time vectorized get with span */
    template<typename... ColumnTypes>
    template<size_t Index, typename T, size_t Count, size_t Recursion>
    void RowStatic<ColumnTypes...>::get(std::span<T, Count> dst) const {
        if constexpr (Recursion < Count) {
            static_assert(Index + Recursion < column_count, "Span exceeds column count");
            static_assert(std::is_convertible_v<T, column_type<Index + Recursion>>, "Column type is not convertible to T");
            dst[Recursion] = std::get<Index + Recursion>(data_);
            get<Index, T, Count, Recursion + 1>(dst);
        }
    }


    /** Compile-time vectorized set with span */
    template<typename... ColumnTypes>
    template<size_t Index, typename T, size_t Count, size_t Recursion>
    void RowStatic<ColumnTypes...>::set(std::span<const T, Count> src) {
        if constexpr (Recursion < Count) {
            static_assert(Index + Recursion < column_count, "Span exceeds column count");
            static_assert(std::is_convertible_v<T, column_type<Index + Recursion>>, "Column type is not convertible to T");
            if constexpr (std::is_same_v<column_type<Index + Recursion>, std::string>) {
                // Special handling for strings (truncate to MAX_STRING_LENGTH)
                if (trackChanges() && (std::get<Index + Recursion>(data_) != src[Recursion])) {
                    changes_.set(Index + Recursion);
                }
                std::get<Index + Recursion>(data_).assign(src[Recursion].c_str(), std::min(src[Recursion].size(), MAX_STRING_LENGTH));
            } else {
                // Primitive types
                if (trackChanges() && (std::get<Index + Recursion>(data_) != src[Recursion])) {
                    changes_.set(Index + Recursion);
                }
                std::get<Index + Recursion>(data_) = src[Recursion];
            }
            set<Index, T, Count, Recursion + 1>(src);
        }
    }

    // ========================================================================
    // RowStatic Vectorized Access - Runtime Indexed
    // ========================================================================

    /** Runtime vectorized get with span */
    template<typename... ColumnTypes>
    template<typename T, size_t Index>
    void RowStatic<ColumnTypes...>::get(size_t index, std::span<T> dst) const {
        if constexpr (Index == 0 && RANGE_CHECKING) {
            // Only check highest index (implicit check for all others)
            if (index + dst.size() > column_count) {
                throw std::out_of_range("Span exceeds column count");
            }
        }
        // If current index is within requested range, copy value
        if(Index >= index && Index < index + dst.size()) {
            dst[Index - index] = static_cast<T>(std::get<Index>(data_));
        }         
        // Recurse to next index
        if constexpr (Index + 1 < column_count) {
            get<T, Index + 1>(index, dst);
        }
    }

    /** Runtime vectorized set with span */
    template<typename... ColumnTypes>
    template<typename T, size_t Index>
    void RowStatic<ColumnTypes...>::set(size_t index, std::span<const T> src) {
        if constexpr (Index == 0 && RANGE_CHECKING) {
            // Only check highest index (implicit check for all others)
            if (index + src.size() > column_count) {
                throw std::out_of_range("Span exceeds column count");
            }
        }
        // If current index is within requested range, copy value
        if(Index >= index && Index < index + src.size()) {
            if(trackChanges() && (std::get<Index>(data_) != src[Index - index])) {
                changes_.set(Index);
            }
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                // Special handling for strings (truncate to MAX_STRING_LENGTH)
                std::get<Index>(data_).assign(src[Index - index].c_str(), std::min(src[Index - index].size(), MAX_STRING_LENGTH));
            } else {
                std::get<Index>(data_) = src[Index - index];
            }
        }         
        // Recurse to next index
        if constexpr (Index + 1 < column_count) {
            set<T, Index + 1>(index, src);
        }
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

    /** Serialize the row into the provided buffer, appending data to the end of the buffer.
    * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
    * @return A span pointing to the serialized row data within the buffer.
    */
    template<typename... ColumnTypes>
    std::span<std::byte> RowStatic<ColumnTypes...>::serializeTo(ByteBuffer& buffer) const {
        size_t offRow = buffer.size();                          // remember where this row starts
        size_t offVar = LayoutType::fixed_size;                 // offset to the begin of variable-size data section (relative to row start)
        buffer.resize(buffer.size() + LayoutType::fixed_size);  // ensure buffer is large enough to hold fixed-size data

        // serialize each tuple element using compile-time recursion
        serializeElements<0>(buffer, offRow, offVar);
        return {buffer.data() + offRow, buffer.size() - offRow};
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::serializeElements(ByteBuffer& buffer, const size_t& offRow, size_t& offVar) const {
        if constexpr (Index < column_count) {
            constexpr size_t lenFix = LayoutType::column_lengths[Index];
            constexpr size_t offFix = LayoutType::column_offsets[Index];
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t lenVar = std::min(std::get<Index>(data_).size(), MAX_STRING_LENGTH);
                StringAddr strAddr(offVar, lenVar);                                                     // Make address relative to row start
                buffer.resize(buffer.size() + lenVar);                                                  // Ensure buffer is large enough to hold string payload
                std::memcpy(buffer.data() + offRow + offFix, &strAddr, sizeof(strAddr));                // write string address
                std::memcpy(buffer.data() + offRow + offVar, std::get<Index>(data_).c_str(), lenVar);   // write string payload
                offVar += lenVar;
            } else {
                std::memcpy(buffer.data() + offRow + offFix, &std::get<Index>(data_), lenFix);
            }
            // Recursively process next element
            serializeElements<Index + 1>(buffer, offRow, offVar);
        }
    }

    /** Serialize the row into the provided buffer using Zero-Order-Hold (ZoH) compression.
     * Only the columns that have changed (as indicated by the change tracking bitset) are serialized.
     * Bool columns are always serialized, but their values are stored in the change bitset.
     * @param buffer The byte buffer to serialize into. The buffer will be resized as needed.
     * @return A span pointing to the serialized row data within the buffer.
     */
    template<typename... ColumnTypes>
    std::span<std::byte> RowStatic<ColumnTypes...>::serializeToZoH(ByteBuffer& buffer) const {
        if(!hasAnyChanges()) {
            return {buffer.data(), 0}; // nothing to serialize
        }

        // reserve space to store change bitset
        size_t offRow = buffer.size();      // offset to the begin of this row
        buffer.resize(buffer.size() + changes_.sizeBytes());
        
        // Serialize each tuple element using compile-time recursion
        serializeElementsZoH<0>(buffer);

        // write change bitset to reserved location (we do it at the end as we encode bools in to the bitset, during element serialization)
        std::memcpy(buffer.data() + offRow, changes_.data(), changes_.sizeBytes());
        return {buffer.data() + offRow, buffer.size() - offRow};
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::serializeElementsZoH(ByteBuffer& buffer) const 
    {
        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, bool>) {
                // Special handling for bools: 
                //  - always serialize!
                //  - but store as single bit within changes_
                bool value = std::get<Index>(data_);
                if (value) {
                    changes_.set(Index);
                } else {
                    changes_.reset(Index);
                }
            } else if (changes_.test(Index)) {
                // all other types: only serialize if marked as changed)
                size_t old_size = buffer.size();
                if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                    // special handling for strings, as we need to determine string length
                    // encoding in ZoH mode happens in place. Therefore no string offset is required!
                    const auto& value = std::get<Index>(data_);
                    uint16_t strLength = static_cast<uint16_t>(std::min(value.size(), MAX_STRING_LENGTH));
                    const std::byte* strLengthPtr = reinterpret_cast<const std::byte*>(&strLength);
                    const std::byte* strDataPtr = reinterpret_cast<const std::byte*>(value.c_str());

                    buffer.resize(buffer.size() + sizeof(uint16_t) + strLength);
                    memcpy(buffer.data() + old_size, strLengthPtr, sizeof(uint16_t));
                    memcpy(buffer.data() + old_size + sizeof(uint16_t), strDataPtr, strLength);
                } else {
                    // for all other types, we append directly to the end of the buffer
                    const auto& value = std::get<Index>(data_);
                    const std::byte* dataPtr = reinterpret_cast<const std::byte*>(&value);
                
                    buffer.resize(buffer.size() + sizeof(column_type<Index>));
                    memcpy(buffer.data() + old_size, dataPtr, sizeof(column_type<Index>));
                }
            }
            // Recursively process next element
            return serializeElementsZoH<Index + 1>(buffer); 
        }
    }


    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::deserializeFrom(const std::span<const std::byte> buffer)  {
        //we expect the buffer, starts with the first byte of the row and ends with the last byte of the row (no change bitset)
        deserializeElements<0>(buffer);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::deserializeElements(const std::span<const std::byte> &buffer) 
    {
        if constexpr (Index < column_count) {
            constexpr size_t len = column_lengths[Index];
            constexpr size_t off = column_offsets[Index];

            if (off + len > buffer.size()) {
                throw std::runtime_error("RowStatic::deserializeElements() failed! Buffer overflow while reading.");
            }

            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                StringAddr strAddr;
                std::memcpy(&strAddr, buffer.data() + off, sizeof(strAddr));
                auto [strOff, strLen] = strAddr.unpack();
                if (strOff + strLen > buffer.size()) {
                    throw std::runtime_error("RowStatic::deserializeElements() failed! Buffer overflow while reading.");
                }
                std::get<Index>(data_).assign(reinterpret_cast<const char*>(buffer.data() + strOff), strLen);
            } else {
                std::memcpy(&std::get<Index>(data_), buffer.data() + off, len);
            }
            
            // Recursively process next element
            deserializeElements<Index + 1>(buffer);
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::deserializeFromZoH(const std::span<const std::byte> buffer)  
    {
        // we expect the buffer to start with the change bitset, followed by the actual row data
        if (buffer.size() < changes_.sizeBytes()) {
            throw std::runtime_error("RowStatic::deserializeFromZoH() failed! Buffer too small to contain change bitset.");
        } else {
            // read change bitset from beginning of buffer
            std::memcpy(changes_.data(), buffer.data(), changes_.sizeBytes());
        }
        auto dataBuffer = buffer.subspan(changes_.sizeBytes());
        deserializeElementsZoH<0>(dataBuffer);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::deserializeElementsZoH(std::span<const std::byte> &buffer) {
        // we expect the buffer to have the next element at the current position
        // thus the buffer needs to get shorter as we read elements
        // We also expect that the change bitset has been read already!

        if constexpr (Index < column_count) {
            if constexpr (std::is_same_v<column_type<Index>, bool>) {
                // Special handling for bools: 
                //  - always deserialize!
                //  - but stored as single bit within changes_
                bool value = changes_.test(Index);
                std::get<Index>(data_) = value;
            } else if(changes_.test(Index)) {
                // all other types: only deserialize if marked as changed
                if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                    // special handling for strings, as we need to determine string length
                    if (buffer.size() < sizeof(uint16_t)) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string length.");
                    }
                    uint16_t strLength;
                    std::memcpy(&strLength, buffer.data(), sizeof(uint16_t));
                    
                    if (buffer.size() < sizeof(uint16_t) + strLength) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string payload.");
                    }
                    std::string value;
                    value.assign(reinterpret_cast<const char*>(buffer.data() + sizeof(uint16_t)), strLength);
                    std::get<Index>(data_) = value;
                    buffer = buffer.subspan(sizeof(uint16_t) + strLength);
                } else {
                    // for all other types, we read directly from the start of the buffer
                    if (buffer.size() < sizeof(column_type<Index>)) {
                        throw std::runtime_error("RowStatic::deserializeElementsZoH() failed! Buffer too small to contain element.");
                    }
                    std::memcpy(&std::get<Index>(data_), buffer.data(), sizeof(column_type<Index>));
                    buffer = buffer.subspan(sizeof(column_type<Index>));
                }
            }
            // Column not changed - keeping previous value (no action needed)
            deserializeElementsZoH<Index + 1>(buffer);
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
            StringAddr strAddr;
            std::memcpy(&strAddr, buffer_.data() + off, sizeof(strAddr));
            auto [strOff, strLen] = strAddr.unpack();
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
        static_assert(std::is_same_v<T, column_type<Index> >, "setExplicit type mismatch");

        constexpr size_t off = LayoutType::column_offsets[Index];
        constexpr size_t len = LayoutType::column_lengths[Index];

        //validate buffer length
        if (RANGE_CHECKING && (off + len > buffer_.size())) {
            throw std::runtime_error("Buffer overflow");
        }

        if constexpr (std::is_same_v<column_type<Index>, std::string>) {
            // Handle string case
            StringAddr strAddr;
            std::memcpy(&strAddr, buffer_.data() + off, sizeof(strAddr));
            auto [strOff, strLen] = strAddr.unpack();
            if (strOff + strLen > buffer_.size()) {
                throw std::runtime_error("String payload extends beyond buffer");
            }

            // Copy string data into buffer (truncate to strLen)
            std::memcpy(buffer_.data() + strOff, value.data(), std::min(value.size(), strLen));
            if(value.size() < strLen) {
                // Pad with null bytes if shorter
                std::memset(buffer_.data() + strOff + value.size(), 0, strLen - value.size());
            }
        
        } else {
            // Handle primitive case
            std::memcpy(buffer_.data() + off, &value, len);
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
        RowStatic<ColumnTypes...> row(layout_);
        copyElements<0>(row);
        return row;
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowViewStatic<ColumnTypes...>::copyElements(RowStatic<ColumnTypes...>& row) const {
        if constexpr (Index < column_count) {
            row.template set<Index>(this->template get<Index>());
            copyElements<Index + 1>(row);
        }
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
                StringAddr strAddr;
                std::memcpy(&strAddr, buffer_.data() + LayoutType::column_offsets[Index], sizeof(strAddr));
                auto [strOff, strLen] = strAddr.unpack();
                if (strOff + strLen > buffer_.size()) {
                    return false;
                }
            }
            return validateStringPayloads<Index + 1>();
        }
    }

} // namespace bcsv