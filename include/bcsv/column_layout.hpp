#pragma once

/**
 * @file column_layout.hpp
 * @brief Binary CSV (BCSV) Library - ColumnLayout implementations
 * 
 * This file contains the implementations for the ColumnLayout class.
 */

#include "column_layout.h"
#include <algorithm>

namespace bcsv {

    // Implementation included inline for header-only library
    
    inline void ColumnLayout::addColumn(const std::string& name, ColumnDataType type) {
        if (column_names_.size() >= MAX_COLUMN_COUNT) {
            return; // Cannot exceed maximum column count
        }
        column_names_.push_back(name);
        column_types_.push_back(type);
        column_name_index_[name] = column_names_.size() - 1;
    }
    
    inline void ColumnLayout::addColumn(const std::string& name, const std::string& typeString) {
        ColumnDataType type = stringToDataType(typeString);
        addColumn(name, type);
    }
    
    inline void ColumnLayout::setColumns(const std::vector<std::pair<std::string, ColumnDataType>>& columns) {
        column_names_.clear();
        column_types_.clear();
        column_name_index_.clear();
        for (const auto& column : columns) {
            addColumn(column.first, column.second);
        }
    }
    
    inline std::vector<std::pair<std::string, ColumnDataType>> ColumnLayout::getColumns() const {
        std::vector<std::pair<std::string, ColumnDataType>> columns;
        for (size_t i = 0; i < column_names_.size(); ++i) {
            columns.emplace_back(column_names_[i], column_types_[i]);
        }
        return columns;
    }
    
    inline std::vector<std::string> ColumnLayout::getNames() const {
        return column_names_;
    }
    
    inline std::vector<ColumnDataType> ColumnLayout::getDataTypes() const {
        return column_types_;
    }
    
    inline std::vector<std::string> ColumnLayout::getDataTypesAsString() const {
        std::vector<std::string> types;
        for (const auto& type : column_types_) {
            types.push_back(dataTypeToString(type));
        }
        return types;
    }
    
    inline ColumnDataType ColumnLayout::getDataType(size_t index) const {
        if (index < column_types_.size()) {
            return column_types_[index];
        }
        return ColumnDataType::STRING;
    }
    
    inline std::string ColumnLayout::getDataTypeAsString(size_t index) const {
        if (index < column_types_.size()) {
            return dataTypeToString(column_types_[index]);
        }
        return "INVALID";
    }
    
    inline size_t ColumnLayout::getIndex(const std::string& columnName) const {
        auto it = column_name_index_.find(columnName);
        if (it != column_name_index_.end()) {
            return it->second;
        }
        return SIZE_MAX; // Return maximum value to indicate not found
    }
    
    inline std::string ColumnLayout::getName(size_t index) const {
        if (index < column_names_.size()) {
            return column_names_[index];
        }
        return "";
    }
    
    inline void ColumnLayout::setName(size_t index, const std::string& name) {
        if (index < column_names_.size()) {
            // Remove old mapping
            column_name_index_.erase(column_names_[index]);
            // Update name
            column_names_[index] = name;
            // Add new mapping
            column_name_index_[name] = index;
        }
    }
    
    inline void ColumnLayout::setDataType(size_t index, ColumnDataType type) {
        if (index < column_types_.size()) {
            column_types_[index] = type;
        }
    }
    
    inline void ColumnLayout::removeColumn(size_t index) {
        if (index < column_names_.size()) {
            // Remove from index map
            column_name_index_.erase(column_names_[index]);
            // Remove from vectors
            column_names_.erase(column_names_.begin() + index);
            column_types_.erase(column_types_.begin() + index);
            // Update index map since indices have shifted
            updateIndexMap();
        }
    }
    
    inline void ColumnLayout::clear() {
        column_names_.clear();
        column_types_.clear();
        column_name_index_.clear();
    }
    
    inline bool ColumnLayout::hasColumn(const std::string& columnName) const {
        return column_name_index_.find(columnName) != column_name_index_.end();
    }
    
    inline void ColumnLayout::updateIndexMap() {
        column_name_index_.clear();
        for (size_t i = 0; i < column_names_.size(); ++i) {
            column_name_index_[column_names_[i]] = i;
        }
    }

    // Helper functions for type conversion
    inline ColumnDataType ColumnLayout::stringToDataType(const std::string& typeString) {
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
    
    inline std::string ColumnLayout::dataTypeToString(ColumnDataType type) {
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

} // namespace bcsv
