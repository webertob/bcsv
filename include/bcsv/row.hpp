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
        : layout_(layout), data_(layout.columnCount()) 
    {
        for(size_t i = 0; i < layout.columnCount(); ++i) {
            data_[i] = defaultValue(layout.columnType(i));
        }
    }

    /** Clear the row to its default state (default values) */
    void Row::clear()
    {
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            data_[i] = defaultValue(layout_.columnType(i));
        }
        if(tracksChanges()) {
            changes_.set(); // mark everything as changed
        }
    }

    /** Enable or disable change tracking (i.e. used for Zero-Order-Hold compression)*/
    inline
    void Row::trackChanges(bool enable)
    {
        if(enable == tracksChanges()) {
            return; // no-op if already in desired state
        }
        if (enable) {            
            changes_.resize(layout_.columnCount(), true);   // we start with all bit set true to indicate everything has "changed"
        } else {
            changes_.clear();                               // disable tracking
        }
    }

    /** Check if change tracking is enabled */ 
    inline
    bool Row::tracksChanges() const
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
            if (RANGE_CHECKING && toColumnType<CurrentType>() != layout_.columnType(index)) {
                throw std::runtime_error("Internal error: variant type doesn't match layout");
            }
            
            if constexpr (std::is_same_v<ProvidedType, CurrentType>) {
                if(tracksChanges() && (std::get<CurrentType>(data_[index]) != value)) {
                    changes_[index] = true; // mark this column as changed
                }
                data_[index] = value;  // Direct assignment
            } else if constexpr (std::is_convertible_v<ProvidedType, CurrentType>) {
                if(tracksChanges() && (std::get<CurrentType>(data_[index]) != static_cast<CurrentType>(value))) {
                    changes_[index] = true; // mark this column as changed
                }
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
    }

    void Row::serializeTo(ByteBuffer& buffer) const  {
        size_t fixedSize, totalSize;
        serializedSize(fixedSize, totalSize);

        // we append to the end of the buffer
        std::byte*  ptrFix = buffer.data() + buffer.size(); // Pointer to the start of fixed-size data
        std::byte*  ptrStr = ptrFix + fixedSize;            // Pointer to the start of string data
        buffer.resize(buffer.size() + totalSize);           // Ensure buffer is large enough
        
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
    }

    bool Row::deserializeFrom(const std::span<const std::byte> buffer)
    {
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ColumnType type = layout_.columnType(i);
            size_t len = layout_.columnLength(i);
            size_t off = layout_.columnOffset(i);

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

    void Row::serializeToZoH(ByteBuffer& buffer) const {
        assert(tracksChanges() && "Change tracking must be enabled for ZoH serialization");
        if(!hasAnyChanges()) {
            return; // nothing to serialize
        }

        // reserve space to store change bitset
        size_t ptrChanges = buffer.size();
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
        std::memcpy(buffer.data() + ptrChanges, changes_.data(), changes_.sizeBytes());
    }

    bool Row::deserializeFromZoH(const std::span<const std::byte> buffer) {
        trackChanges(true); // ensure change tracking is enabled
        // We expect the buffer to start with the change bitset, followed by the actual row data
        if (buffer.size() < changes_.sizeBytes()) {
            std::cerr << "Row::deserializeFromZoH() failed! Buffer too small to contain change bitset." << std::endl;
            return false;
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
                        std::cerr << "Row::deserializeFromZoH() failed! Buffer too small for string length." << std::endl;
                        return false;
                    }
                    uint16_t strLength;
                    std::memcpy(&strLength, dataBuffer.data(), sizeof(uint16_t));
                    if (dataBuffer.size() < sizeof(uint16_t) + strLength) {
                        std::cerr << "Row::deserializeFromZoH() failed! Buffer too small for string payload." << std::endl;
                        return false;
                    }
                    std::get<std::string>(data_[i]).assign(reinterpret_cast<const char*>(dataBuffer.data() + sizeof(uint16_t)), strLength);
                    dataBuffer = dataBuffer.subspan(sizeof(uint16_t) + strLength);
                    continue;
                } else {
                    if (dataBuffer.size() < layout_.columnLength(i)) {
                        std::cerr << "Row::deserializeFromZoH() failed! Buffer too small for column." << std::endl;
                        return false;
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
        return true;
    }

    // ========================================================================
    // RowView Implementation
    // ========================================================================

    template<typename T>
    T RowView::get(size_t index) const
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
        for(size_t i = 0; i < layout_.columnCount(); ++i) {
            ValueType value = get<ValueType>(i);
            row.set(i, value);
        }
        return row;
    }

    bool RowView::validate() const 
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
                uint64_t packedAddr = *reinterpret_cast<const uint64_t*>(buffer_.data() + layout_.columnOffset(i));
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
        if(tracks_changes_ == enable) {
            return; // no-op if already in desired state
        }
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
    void RowStatic<ColumnTypes...>::set(const auto& value) {
        static_assert(Index < column_count, "Index out of bounds");
        using ExpectedType = column_type<Index>;

        if constexpr (std::is_same_v<decltype(value), ValueType>) {
            std::visit([this](auto&& actualValue) {
                using ActualType = std::decay_t<decltype(actualValue)>;
                if constexpr (std::is_same_v<ActualType, ExpectedType>) {
                    markChangedAndSet<Index>(actualValue);
                } else if constexpr (std::is_convertible_v<ActualType, ExpectedType>) {
                    markChangedAndSet<Index>(static_cast<ExpectedType>(actualValue));
                } else {
                    static_assert(std::is_convertible_v<ActualType, ExpectedType>,
                                "Cannot convert provided type to column type");
                }
            }, value);
        } else {
            const auto& actualValue = value;  // Now this can be a reference!
            using ActualType = std::decay_t<decltype(actualValue)>;
            if constexpr (std::is_same_v<ActualType, ExpectedType>) {
                markChangedAndSet<Index>(actualValue);
            } else if constexpr (std::is_convertible_v<ActualType, ExpectedType>) {
                markChangedAndSet<Index>(static_cast<ExpectedType>(actualValue));
            } else {
                static_assert(std::is_convertible_v<ActualType, ExpectedType>,
                            "Cannot convert provided type to column type");
            }
        }        
    }

    template<typename... ColumnTypes>
    template<size_t Index, typename T>
    void RowStatic<ColumnTypes...>::markChangedAndSet(const T& value) {
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
    template<size_t Index>
    void RowStatic<ColumnTypes...>::set(size_t index, const auto& value) {
        if constexpr (Index < column_count) {
            if(index == Index) {
                this->set<Index>(value);
            } else {
                this->set<Index + 1>(index, value);
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
    void RowStatic<ColumnTypes...>::serializeTo(ByteBuffer& buffer) const {
        size_t fixedSize, totalSize;
        serializedSize(fixedSize, totalSize);

        std::span<std::byte> rowBuffer(buffer.data()+buffer.size(), totalSize);
        buffer.resize(buffer.size() + totalSize); // Ensure buffer is large enough

        // Serialize each tuple element using compile-time recursion
        serializeElements<0>(rowBuffer, fixedSize);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    void RowStatic<ColumnTypes...>::serializeElements(std::span<std::byte> &buffer, size_t& strPtr) const {
        if constexpr (Index < column_count) {
            constexpr size_t len = LayoutType::column_lengths[Index];
            constexpr size_t off = LayoutType::column_offsets[Index];
            if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                size_t strLength = std::min(std::get<Index>(data_).size(), MAX_STRING_LENGTH);
                size_t strAddress = StringAddress::pack(strPtr, strLength);
                std::memcpy(buffer.data() + off, &strAddress, len);                                 //write string address
                std::memcpy(buffer.data() + strPtr, std::get<Index>(data_).c_str(), strLength);     //write string payload
                strPtr += strLength;
            } else {
                std::memcpy(buffer.data() + off, &std::get<Index>(data_), len);
            }
            // Recursively process next element
            serializeElements<Index + 1>(buffer, strPtr);
        }
    }

    template<typename... ColumnTypes>
    void RowStatic<ColumnTypes...>::serializeToZoH(ByteBuffer& buffer) const {
        if(!hasAnyChanges()) {
            return; // nothing to serialize
        }

        // reserve space to store change bitset
        size_t ptrChanges = buffer.size();
        buffer.resize(buffer.size() + changes_.sizeBytes());
        
        // Serialize each tuple element using compile-time recursion
        serializeElementsZoH<0>(buffer);

        // write change bitset to reserved location (we do it at the end as we encode bools in to the bitset, during element serialization)
        std::memcpy(buffer.data() + ptrChanges, changes_.data(), changes_.sizeBytes());
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
                if constexpr (std::is_same_v<column_type<Index>, std::string>) {
                    // special handling for strings, as we need to determine string length
                    // encoding in ZoH mode happens in place. Therefore no string offset is required!
                    const auto& value = std::get<Index>(data_);
                    uint16_t strLength = static_cast<uint16_t>(std::min(value.size(), MAX_STRING_LENGTH));
                    
                    const std::byte* lengthPtr = reinterpret_cast<const std::byte*>(&strLength);
                    buffer.insert(buffer.end(), lengthPtr, lengthPtr + sizeof(uint16_t));
                    const std::byte* dataPtr = reinterpret_cast<const std::byte*>(value.c_str());
                    buffer.insert(buffer.end(), dataPtr, dataPtr + strLength);
                } else {
                    // for all other types, we append directly to the end of the buffer
                    const auto& value = std::get<Index>(data_);
                    const std::byte* dataPtr = reinterpret_cast<const std::byte*>(&value);
                    buffer.insert(buffer.end(), dataPtr, dataPtr + sizeof(column_type<Index>));
                }
            }
            // Recursively process next element
            return serializeElementsZoH<Index + 1>(buffer); 
        }
    }


    template<typename... ColumnTypes>
    bool RowStatic<ColumnTypes...>::deserializeFrom(const std::span<const std::byte> buffer)  {
        //we expect the buffer, starts with the first byte of the row and ends with the last byte of the row (no change bitset)
        return deserializeElements<0>(buffer);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowStatic<ColumnTypes...>::deserializeElements(const std::span<const std::byte> &buffer) 
    {
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

    template<typename... ColumnTypes>
    bool RowStatic<ColumnTypes...>::deserializeFromZoH(const std::span<const std::byte> buffer)  
    {
        // we expect the buffer to start with the change bitset, followed by the actual row data
        if (buffer.size() < changes_.sizeBytes()) {
            std::cerr << "RowStatic::deserializeFromZoH() failed! Buffer too small to contain change bitset." << std::endl;
            return false;
        } else {
            // read change bitset from beginning of buffer
            std::memcpy(changes_.data(), buffer.data(), changes_.sizeBytes());
        }
        auto dataBuffer = buffer.subspan(changes_.sizeBytes());
        return deserializeElementsZoH<0>(dataBuffer);
    }

    template<typename... ColumnTypes>
    template<size_t Index>
    bool RowStatic<ColumnTypes...>::deserializeElementsZoH(std::span<const std::byte> &buffer) {
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
                        std::cerr << "RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string length." << std::endl;
                        return false;
                    }
                    uint16_t strLength;
                    std::memcpy(&strLength, buffer.data(), sizeof(uint16_t));
                    
                    if (buffer.size() < sizeof(uint16_t) + strLength) {
                        std::cerr << "RowStatic::deserializeElementsZoH() failed! Buffer too small to contain string payload." << std::endl;
                        return false;
                    }
                    std::string value;
                    value.assign(reinterpret_cast<const char*>(buffer.data() + sizeof(uint16_t)), strLength);
                    std::get<Index>(data_) = value;
                    buffer = buffer.subspan(sizeof(uint16_t) + strLength);
                } else {
                    // for all other types, we read directly from the start of the buffer
                    if (buffer.size() < sizeof(column_type<Index>)) {
                        std::cerr << "RowStatic::deserializeElementsZoH() failed! Buffer too small to contain element." << std::endl;
                        return false;
                    }
                    std::memcpy(&std::get<Index>(data_), buffer.data(), sizeof(column_type<Index>));
                    buffer = buffer.subspan(sizeof(column_type<Index>));
                }
            }
            // Column not changed - keeping previous value (no action needed)
            return deserializeElementsZoH<Index + 1>(buffer);
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