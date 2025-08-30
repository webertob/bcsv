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

    // ========================================================================
    // LayoutInterface Implementation
    // ========================================================================

    inline void LayoutInterface::updateIndexMap() {
        column_index_.clear();
        for (size_t i = 0; i < column_names_.size(); ++i) {
            column_index_[column_names_[i]] = i;
        }
    }

    inline size_t LayoutInterface::getColumnIndex(const std::string& columnName) const {
        auto it = column_index_.find(columnName);
        if (it != column_index_.end()) {
            return it->second;
        }
        return SIZE_MAX; // Return maximum value to indicate not found
    }
    
    inline const std::string& LayoutInterface::getColumnName(size_t index) const {
        return column_names_[index];
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
        column_index_.erase(column_names_[index]); //remove old name from index
        column_names_[index] = name; //update name
        column_index_[name] = index; //add new name to index
    }



    // ========================================================================
    // Layout Implementation
    // ========================================================================

    inline Layout::Layout(const Layout& other) {
        column_types_ = other.column_types_;
        column_names_ = other.column_names_;
        column_index_ = other.column_index_;
        column_lengths_ = other.column_lengths_;
        column_offsets_ = other.column_offsets_;
    }

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) {
        setColumns(columns);
    }

    inline void Layout::insertColumn(const ColumnDefinition& column, size_t position) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if (column_names_.size() >= MAX_COLUMN_COUNT) {
            throw std::runtime_error("Cannot exceed maximum column count");
        }
        
        // If position is past the end or SIZE_MAX, append to the end
        if (position >= column_names_.size()) {
            column_names_.push_back(column.name);
            column_types_.push_back(column.type);
            if (column_offsets_.empty()) {
                column_offsets_.push_back(0);
            } else {
                column_offsets_.push_back(column_offsets_.back() + column_lengths_.back());
            }
            column_lengths_.push_back(binaryFieldLength(column.type));
        } else {
            // Insert at the specified position
            column_names_.insert(column_names_.begin() + position, column.name);
            column_types_.insert(column_types_.begin() + position, column.type);
            column_lengths_.insert(column_lengths_.begin() + position, binaryFieldLength(column.type));
            if (position == 0) {
                column_offsets_.insert(column_offsets_.begin(), 0);
            } else {
                column_offsets_.insert(column_offsets_.begin() + position, column_offsets_[position - 1] + column_lengths_[position - 1]);
            }
            //update all offsets past position
            for (size_t i = position + 1; i < column_offsets_.size(); ++i) {
                column_offsets_[i] += column_lengths_[position];
            }
        }
        updateIndexMap();
    }
    
    inline void Layout::clear() {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        column_names_.clear();
        column_types_.clear();
        column_lengths_.clear();
        column_offsets_.clear();
        column_index_.clear();
    }
    
    inline void Layout::setColumns(const std::vector<ColumnDefinition>& columns) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        clear();
        column_names_.reserve(columns.size());
        column_types_.reserve(columns.size());
        column_lengths_.reserve(columns.size());
        column_offsets_.reserve(columns.size());
        for (const auto& column : columns) {
            insertColumn(column);
        }
    }

    inline size_t Layout::getColumnOffset(size_t index) const {
        if (RANGE_CHECKING && index >= column_offsets_.size()) {
            throw std::out_of_range("Column index out of range");
        }
        return column_offsets_[index];
    }

    inline size_t Layout::getColumnLength(size_t index) const {
        if (RANGE_CHECKING && index >= column_lengths_.size()) {
            throw std::out_of_range("Column index out of range");
        }
        return column_lengths_[index];
    }

    inline ColumnDataType Layout::getColumnType(size_t index) const {
        if (RANGE_CHECKING && index >= column_types_.size()) {
            throw std::out_of_range("Column index out of range");
        }
        return column_types_[index];
    }

    inline void Layout::setColumnType(size_t index, ColumnDataType type) {
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
        column_index_.erase(column_names_[index]);
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);
        column_offsets_.erase(column_offsets_.begin() + index);
        // Update all offsets past the removed column
        for (size_t i = index; i < column_offsets_.size(); ++i) {
            column_offsets_[i] -= column_lengths_[index];
        }
        column_lengths_.erase(column_lengths_.begin() + index);
        // Update index map since indices have shifted
        updateIndexMap();
    }

    inline Layout& Layout::operator=(const Layout& other) {
        if (this != &other) {
            column_types_ = other.column_types_;
            column_names_ = other.column_names_;
            column_index_ = other.column_index_;
            column_lengths_ = other.column_lengths_;
            column_offsets_ = other.column_offsets_;
            //the assignment is unlocked!
        }
        return *this;
    }


    // ========================================================================
    // LayoutStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic() 
    {
        std::vector<std::string> names(sizeof...(ColumnTypes));
        // Default column names as "Column0", "Column1", etc.
        for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            names[i] = "Column" + std::to_string(i);
        }
        column_names_ = std::move(names);
        updateIndexMap();
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(const std::vector<std::string>& columnNames) 
    {
        if (columnNames.size() != sizeof...(ColumnTypes)) {
            throw std::invalid_argument("Number of column names must match number of column types");
        }
        column_names_ = columnNames;
        updateIndexMap();
    }

    // Recursive helper to get type at runtime index
    template<typename... ColumnTypes>
    template<size_t Index>
    constexpr ColumnDataType LayoutStatic<ColumnTypes...>::getColumnType(size_t index) const {
        if constexpr (Index < sizeof...(ColumnTypes)) {
            if (Index == index) {
                return toColumnDataType< column_type<Index> >;
            } else {
                return getColumnType<Index + 1>(index);
            }
        } else {
            return ColumnDataType::STRING; // Should never reach here with valid index
        }
    }

} // namespace bcsv
