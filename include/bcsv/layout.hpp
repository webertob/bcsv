#pragma once

/**
 * @file layout.hpp
 * @brief Binary CSV (BCSV) Library - Layout implementations
 * 
 * This file contains the implementations for the Layout class.
 */

#include "layout.h"
#include <algorithm>
#include <stdexcept>

namespace bcsv {

    inline void LayoutInterface::updateIndexMap() {
        column_name_index_.clear();
        for (size_t i = 0; i < column_names_.size(); ++i) {
            column_name_index_[column_names_[i]] = i;
        }
    }

    inline size_t LayoutInterface::getColumnIndex(const std::string& columnName) const {
        auto it = column_name_index_.find(columnName);
        if (it != column_name_index_.end()) {
            return it->second;
        }
        return SIZE_MAX; // Return maximum value to indicate not found
    }
    
    inline const std::string& LayoutInterface::getColumnName(size_t index) const {
        return column_names_[index];
    }

    inline std::vector<std::string> LayoutInterface::getColumnTypesAsString() const {
        std::vector<std::string> types(getColumnCount(), "");
        for (size_t i = 0; i < getColumnCount(); ++i) {
            types[i] = getColumnTypeAsString(i);
        }
        return types;
    }
    
    inline std::string LayoutInterface::getColumnTypeAsString(size_t index) const {
        return dataTypeToString(getColumnType(index));
    }

    /**
     * @brief Check if this layout is compatible with another for data transfer
     * @param other The other layout to check compatibility with
     * @return true if data can be safely transferred between layouts
     */
    inline bool LayoutInterface::isCompatibleWith(const LayoutInterface& other) const {
        // Quick check: different column counts means not equivalent
        if (getColumnCount() != other.getColumnCount()) {
            return false;
        }
            
        const size_t columnCount = getColumnCount();
        for (size_t i = 0; i < columnCount; ++i) {
            if (getColumnType(i) != other.getColumnType(i)) {
                return false;
            }
        }
        return true;
    }

    inline void LayoutInterface::setColumnName(size_t index, const std::string& name) {
        column_name_index_.erase(column_names_[index]); //remove old name from index
        column_names_[index] = name; //update name
        column_name_index_[name] = index; //add new name to index
    }

        // Helper functions for type conversion
    inline ColumnDataType LayoutInterface::stringToDataType(const std::string& typeString) {
        if (typeString == "bool") return ColumnDataType::BOOL;
        if (typeString == "uint8") return ColumnDataType::UINT8;
        if (typeString == "uint16") return ColumnDataType::UINT16;
        if (typeString == "uint32") return ColumnDataType::UINT32;
        if (typeString == "uint64") return ColumnDataType::UINT64;
        if (typeString == "int8") return ColumnDataType::INT8;
        if (typeString == "int16") return ColumnDataType::INT16;
        if (typeString == "int32") return ColumnDataType::INT32;
        if (typeString == "int64") return ColumnDataType::INT64;
        if (typeString == "float") return ColumnDataType::FLOAT;
        if (typeString == "double") return ColumnDataType::DOUBLE;
        return ColumnDataType::STRING; // default
    }

    inline std::string LayoutInterface::dataTypeToString(ColumnDataType type) {
        switch (type) {
            case ColumnDataType::BOOL: return "bool";
            case ColumnDataType::UINT8: return "uint8";
            case ColumnDataType::UINT16: return "uint16";
            case ColumnDataType::UINT32: return "uint32";
            case ColumnDataType::UINT64: return "uint64";
            case ColumnDataType::INT8: return "int8";
            case ColumnDataType::INT16: return "int16";
            case ColumnDataType::INT32: return "int32";
            case ColumnDataType::INT64: return "int64";
            case ColumnDataType::FLOAT: return "float";
            case ColumnDataType::DOUBLE: return "double";
            case ColumnDataType::STRING: return "string";
            default: return "string";
        }
    }

    inline Layout::Layout(const Layout& other) {
        column_types_ = other.column_types_;
        column_names_ = other.column_names_;
        column_name_index_ = other.column_name_index_;
        //the copy is unlocked!
    }

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) {
        setColumns(columns);
    }

    inline void Layout::insertColumn(const std::string& name, ColumnDataType type, size_t position) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if (column_names_.size() >= MAX_COLUMN_COUNT) {
            throw std::runtime_error("Cannot exceed maximum column count");
        }
        
        // If position is past the end or SIZE_MAX, append to the end
        if (position >= column_names_.size()) {
            column_names_.push_back(name);
            column_types_.push_back(type);
        } else {
            // Insert at the specified position
            column_names_.insert(column_names_.begin() + position, name);
            column_types_.insert(column_types_.begin() + position, type);
        }
        updateIndexMap();
    }
    
    inline void Layout::insertColumn(const std::string& name, const std::string& typeString, size_t position) {
        insertColumn(name, stringToDataType(typeString), position);
    }
    
    inline void Layout::clear() {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        column_names_.clear();
        column_types_.clear();
        column_name_index_.clear();
    }
    
    inline void Layout::setColumns(const std::vector<ColumnDefinition>& columns) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        clear();
        column_names_.reserve(columns.size());
        column_types_.reserve(columns.size());
        for (const auto& column : columns) {
            insertColumn(column.name, column.type);
        }
    }

    inline void Layout::setColumns(const std::vector<std::string>& names, const std::vector<ColumnDataType>& types)
    {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if (names.size() != types.size()) {
            throw std::invalid_argument("Names and types vectors must have the same size");
        }
        clear();
        column_names_.reserve(names.size());
        column_types_.reserve(types.size());
        for (size_t i = 0; i < names.size(); ++i) {
            insertColumn(names[i], types[i]);
        }
    }
    
    inline ColumnDataType Layout::getColumnType(size_t index) const {
        return column_types_[index];
    }

    inline void Layout::setColumnDataType(size_t index, ColumnDataType type) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        column_types_[index] = type;
    }
    
    inline void Layout::removeColumn(size_t index) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if (index >= column_names_.size()) {
            throw std::out_of_range("Column index out of range");
        }
        // Remove from index map
        column_name_index_.erase(column_names_[index]);
        // Remove from vectors
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);
        // Update index map since indices have shifted
        updateIndexMap();
    }

    inline Layout& Layout::operator=(const Layout& other) {
        if (this != &other) {
            column_types_ = other.column_types_;
            column_names_ = other.column_names_;
            column_name_index_ = other.column_name_index_;
            //the assignment is unlocked!
        }
        return *this;
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(const std::vector<std::string>& columnNames) 
            : column_types_(std::make_tuple(ColumnTypes{}...))
    {
        if (columnNames.size() != sizeof...(ColumnTypes)) {
            throw std::invalid_argument("Number of column names must match number of column types");
        }
        column_names_ = columnNames;
        updateIndexMap();
    }

    template<typename... ColumnTypes>
    inline ColumnDataType LayoutStatic<ColumnTypes...>::getColumnType(size_t index) const {
        if (index >= sizeof...(ColumnTypes)) {
            throw std::out_of_range("Column index out of range");
        }
        return getTypeAtIndex<0>(index);
    }

    // Recursive helper to get type at runtime index
    template<typename... ColumnTypes>
    template<size_t Index>
    constexpr ColumnDataType LayoutStatic<ColumnTypes...>::getTypeAtIndex(size_t targetIndex) const {
        if constexpr (Index < sizeof...(ColumnTypes)) {
            if (Index == targetIndex) {
                using TypeAtIndex = std::tuple_element_t<Index, std::tuple<ColumnTypes...>>;
                return getColumnDataType<TypeAtIndex>();
            } else {
                return getTypeAtIndex<Index + 1>(targetIndex);
            }
        } else {
            return ColumnDataType::STRING; // Should never reach here with valid index
        }
    }

} // namespace bcsv
