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
 * @file layout.hpp
 * @brief Binary CSV (BCSV) Library - Layout implementations
 * 
 * This file contains the implementations for the Layout class.
 */

#include "layout.h"
#include "row.h"
#include <algorithm>
#include <stdexcept>

namespace bcsv {

    // ========================================================================
    // Layout Implementation
    // ========================================================================

    inline Layout::Layout(const Layout& other) {
        column_types_ = other.column_types_;
        column_names_ = other.column_names_;
        column_index_ = other.column_index_;
        column_lengths_ = other.column_lengths_;
        column_offsets_ = other.column_offsets_;
        total_fixed_size_ = other.total_fixed_size_;
    }

    inline Layout& Layout::operator=(const Layout& other) {
        if (this != &other) {
            column_types_ = other.column_types_;
            column_names_ = other.column_names_;
            column_index_ = other.column_index_;
            column_lengths_ = other.column_lengths_;
            column_offsets_ = other.column_offsets_;
            total_fixed_size_ = other.total_fixed_size_;
        }
        return *this;
    }

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) {
        setColumns(columns);
    }

    inline size_t Layout::columnIndex(const std::string& columnName) const {
        return column_index_.at(columnName);
    }

    inline bool Layout::addColumn(const ColumnDefinition& column, size_t position) {
        if (RANGE_CHECKING && column_names_.size() >= MAX_COLUMN_COUNT) {
            std::cerr << "Cannot exceed maximum column count" << std::endl;
            return false;
        }
        if (column.name.empty()) {
            std::cerr << "Column name cannot be empty" << std::endl;
            return false;
        }
        if (column.name.length() > MAX_STRING_LENGTH) {
            std::cerr << "Column name too long: " + column.name << std::endl;
            return false;
        }
        if (column_index_.find(column.name) != column_index_.end()) {
            std::cerr << "Duplicate column name: " + column.name << std::endl;
            return false;
        }
        if (total_fixed_size_ + binaryFieldLength(column.type) > MAX_ROW_LENGTH) {
            std::cerr << "Adding column exceeds maximum row width! \n(sum of all column lengths must stay below: " 
                      << MAX_ROW_LENGTH << ", current: " 
                      << total_fixed_size_ + binaryFieldLength(column.type) << ")" 
                      << std::endl;
            return false;
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
            column_index_[column.name] = column_names_.size() - 1; // update index mapping
        } else {
            // Insert at the specified position
            column_names_.insert(column_names_.begin() + position, column.name);
            column_types_.insert(column_types_.begin() + position, column.type);
            column_lengths_.insert(column_lengths_.begin() + position, binaryFieldLength(column.type));
            column_index_[column.name] = position; // update index mapping
            if (position == 0) {
                column_offsets_.insert(column_offsets_.begin(), 0);
            } else {
                column_offsets_.insert(column_offsets_.begin() + position, column_offsets_[position - 1] + column_lengths_[position - 1]);
            }
            //update all offsets past position
            for (size_t i = position + 1; i < column_offsets_.size(); ++i) {
                column_offsets_[i] += column_lengths_[position];
                column_index_[column_names_[i]] = i; // update index mapping
            }
        }
        total_fixed_size_ += binaryFieldLength(column.type);
        return true;
    }
    
    inline void Layout::clear() {
        column_names_.clear();
        column_types_.clear();
        column_lengths_.clear();
        column_offsets_.clear();
        column_index_.clear();
        total_fixed_size_ = 0;
    }
    
    inline void Layout::setColumns(const std::vector<ColumnDefinition>& columns) {
        clear();
        column_names_.resize(columns.size());
        column_types_.resize(columns.size());
        column_lengths_.resize(columns.size());
        column_offsets_.resize(columns.size());
        total_fixed_size_ = 0;

        for(size_t i = 0; i < columns.size(); ++i) {
            if(columns[i].name.empty()) {
                throw std::invalid_argument("Column name cannot be empty");
            }
            // Check for duplicate names using index
            if(column_index_.find(columns[i].name) != column_index_.end()) {
                throw std::invalid_argument("Duplicate column name: " + columns[i].name);
            }
            column_names_[i] = columns[i].name;
            column_types_[i] = columns[i].type;
            column_index_[columns[i].name] = i;
            column_lengths_[i] = binaryFieldLength(columns[i].type);
            column_offsets_[i] = (i == 0) ? 0 : column_offsets_[i - 1] + column_lengths_[i - 1];
            total_fixed_size_ += column_lengths_[i];
        }
    }

    inline bool Layout::setColumnName(size_t index, const std::string& name) {
        if(RANGE_CHECKING && index >= column_names_.size() ) {
            std::cerr << "Column index out of range" << std::endl;
            return false;
        }

        //no empty names
        if (name.empty()) {
            std::cerr << "Column name cannot be empty" << std::endl;
            return false;
        }

        // Check for duplicate names
        if (column_index_.find(name) != column_index_.end() && column_names_[index] != name) {
            std::cerr << "Duplicate column name: " << name << " skip" << std::endl;
            return  false;
        }

        column_index_.erase(column_names_[index]); //remove old name from index
        column_names_[index] = name; //update name
        column_index_[name] = index; //add new name to index
        return true;
    }


    inline void Layout::setColumnType(size_t index, ColumnType type) {
        if (RANGE_CHECKING && index >= column_types_.size()) {
            throw std::out_of_range("Column index out of range");
        }

        if(column_types_[index] == type) {
            return; //no change
        }

        if(total_fixed_size_ + binaryFieldLength(type) - column_lengths_[index] > MAX_ROW_LENGTH) {
            throw std::runtime_error("Changing column type exceeds maximum row width! \n(sum of all column lengths must stay below: " + std::to_string(MAX_ROW_LENGTH) + ", current: " + std::to_string(total_fixed_size_) + ")");
        }

        column_types_[index] = type;
        //need to update lengths and offsets
        total_fixed_size_ += binaryFieldLength(type) - column_lengths_[index];
        column_lengths_[index] = binaryFieldLength(type);
        for (size_t i = index + 1; i < column_offsets_.size(); ++i) {
            column_offsets_[i] = column_offsets_[i - 1] + column_lengths_[i - 1];
        }
    }

    inline void Layout::removeColumn(size_t index) {
        if (index >= column_names_.size()) {
            return; // out of range
        }
        

        // Remove from index map
        size_t col_length = column_lengths_[index];
        column_index_.erase(column_names_[index]);
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);
        column_offsets_.erase(column_offsets_.begin() + index);
        column_lengths_.erase(column_lengths_.begin() + index);
        total_fixed_size_ -= col_length;

        // update subsequent offsets and indices
        for (size_t i = index; i < column_offsets_.size(); ++i) {
            column_offsets_[i] -= col_length;
            column_index_[column_names_[i]] = i; // update index mapping
        }
    }

    /**
     * @brief Check if this layout is compatible with another for data transfer
     * @param other The other layout to check compatibility with
     * @return true if data can be safely transferred between layouts
     */
    inline bool Layout::isCompatibleWith(const Layout& other) const {
        // different column counts means not equivalent
        if (columnCount() != other.columnCount()) {
            return false;
        }

        // different column types means not equivalent
        for (size_t i = 0; i < columnCount(); ++i) {
            if (columnType(i) != other.columnType(i)) {
                return false;
            }
        }
        return true;
    }

    template<typename OtherLayout>
    requires requires(const OtherLayout& other) {
        { other.columnCount() } -> std::convertible_to<size_t>;
        { other.columnType(size_t{}) } -> std::convertible_to<ColumnType>;
    }
    inline Layout& Layout::operator=(const OtherLayout& other) {
        clear();
        size_t size = other.columnCount();
        column_names_.resize(size);
        column_types_.resize(size);
        column_lengths_.resize(size);
        column_offsets_.resize(size);

        for (size_t i = 0; i < size; ++i) {
            column_types_[i] = other.columnType(i);
            column_names_[i] = other.columnName(i);
            column_lengths_[i] = other.getColumnLength(i);
            column_index_[column_names_[i]] = i;
        }

        for (size_t i = 0; i < size; ++i) {
            column_offsets_[i] = (i == 0) ? 0 : column_offsets_[i - 1] + column_lengths_[i - 1];
        }
        
        return *this;
    }

    inline void Layout::updateIndex() {
        column_index_.clear();
        // ToDo: Check for duplicate names? 
        // In case we find duplicates, We should append a suffix to make them unique i.e. name, name.1, name.2, etc.
        // this needs be reflected in column_names_ and column_index_ 
        // consider changing setColumnName and addColumn to handle this with user feedback
        for (size_t i = 0; i < column_names_.size(); ++i) {
            column_index_[column_names_[i]] = i;
        }
    }

    // ========================================================================
    // LayoutStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic() 
    {
        // Default column names as "Column0", "Column1", etc.
        for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            column_names_[i] = "Column" + std::to_string(i);
        }
        updateIndex();
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames) 
    {
        for (size_t i = 0; i < columnNames.size(); ++i) {
            column_names_[i] = columnNames[i];
        }
        updateIndex();
    }

    template<typename... ColumnTypes>
    inline size_t LayoutStatic<ColumnTypes...>::columnIndex(const std::string& columnName) const {
        return column_index_.at(columnName);
    }

    template<typename... ColumnTypes>
    inline bool LayoutStatic<ColumnTypes...>::setColumnName(size_t index, const std::string& name) {
        if (RANGE_CHECKING && index >= sizeof...(ColumnTypes)) {
            std::cerr << "Column index out of range" << std::endl;
            return false;
        }

        //no empty names
        if (name.empty()) {
            std::cerr << "Column name cannot be empty" << std::endl;
            return false;
        }

        // Check for duplicate names
        if (column_index_.find(name) != column_index_.end() && column_names_[index] != name) {
            std::cerr << "Duplicate column name: " << name << " skip" << std::endl;
            return false;
        }

        column_index_.erase(column_names_[index]); //remove old name from index
        column_names_[index] = name; //update name
        column_index_[name] = index; //add new name to index
        return true;
    }

    // Recursive helper to get type at runtime index
    template<typename... ColumnTypes>
    template<size_t Index>
    inline ColumnType LayoutStatic<ColumnTypes...>::columnTypeT(size_t index) const {
        if constexpr (Index < sizeof...(ColumnTypes)) {
            if (Index == index) {
                return toColumnType< column_type<Index> >();
            } else {
                return this->columnTypeT<Index + 1>(index);
            }
        } else {
            return ColumnType::STRING; // Should never reach here with valid index
        }
    }

    template<typename... ColumnTypes>
    template<typename OtherLayout>
    requires requires(const OtherLayout& other) {
        { other.columnCount() } -> std::convertible_to<size_t>;
        { other.columnType(size_t{}) } -> std::convertible_to<ColumnType>;
    }
    inline bool LayoutStatic<ColumnTypes...>::isCompatibleWith(const OtherLayout& other) const {
        // Check if the number of columns is the same
        if (sizeof...(ColumnTypes) != other.columnCount()) {
            return false;
        }

        // Check if the column types are the same
        for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            if (columnType(i) != other.columnType(i)) {
                return false;
            }
        }

        return true;
    }

    template<typename... ColumnTypes>
    template<typename OtherLayout>
    requires requires(const OtherLayout& other) {
        { other.columnCount() } -> std::convertible_to<size_t>;
        { other.columnType(size_t{}) } -> std::convertible_to<ColumnType>;
    }
    inline LayoutStatic<ColumnTypes...>& LayoutStatic<ColumnTypes...>::operator=(const OtherLayout& other) {
        if (!this->isCompatibleWith(other)) {
            throw std::runtime_error("Incompatible layout");
        }
        if (this != &other) {
            for (size_t i = 0; i < column_names_.size(); ++i) {
                column_names_[i] = other.columnName(i);
            }
            updateIndex();
        }
        return *this;
    }

    template<typename... ColumnTypes>
    void LayoutStatic<ColumnTypes...>::updateIndex() {
        // Just ensure any empty slots have default names
        for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            if (column_names_[i].empty()) {
                column_names_[i] = "Column" + std::to_string(i);
            }
        }
        // check for duplicate names? 
        // In case we find duplicates, We should append a suffix to make them unique i.e. name, name.1, name.2, etc.
        // this needs be reflected in column_names_ and column_index_
        std::map<std::string, int> name_count;
        for (const auto& name : column_names_) {
            name_count[name]++;
        }

        // Append suffixes to make names unique
        for (size_t i = 0; i < column_names_.size(); ++i) {
            const auto& name = column_names_[i];
            if (name_count[name] > 1) {
                column_names_[i] = name + "." + std::to_string(name_count[name] - 1);
                name_count[name]--;
            }
        }

        column_index_.clear();
        for (size_t i = 0; i < column_names_.size(); ++i) {
            column_index_[column_names_[i]] = i;
        }
    }

    // Stream operator for Layout - provides human-readable column information
    template<LayoutConcept LayoutType>
    std::ostream& operator<<(std::ostream& os, const LayoutType& layout) {
        const size_t column_count = layout.columnCount();
        
        if (column_count == 0) {
            return os << "Empty layout (no columns)";
        }
        
        // Calculate column widths for aligned output
        const size_t num_width = std::max(std::to_string(column_count).length(), (size_t)3); // minimum width for "Col" header
        
        // Find the longest column name for alignment
        size_t name_width = 4; // minimum width for "Name" header
        size_t type_width = 4; // minimum width for "Type" header
        for (size_t i = 0; i < column_count; ++i) {
            name_width = std::max(name_width, layout.columnName(i).length());
            type_width = std::max(type_width, toString(layout.columnType(i)).length());
        }
        
        // Header
        os << std::left << std::setw(num_width) << "Col"
           << " | " << std::setw(name_width) << "Name"
           << " | " << "Type" << std::endl;
        
        // Separator line
        os << std::string( num_width, '-') << "-+-"
           << std::string(name_width, '-') << "-+-"
           << std::string(type_width, '-') << std::endl; // "STRING" is 6 chars, longest type name
        
        // Column information
        for (size_t i = 0; i < column_count; ++i) {
            os << std::right << std::setw(num_width) << i
               << " | " << std::left << std::setw(name_width) << layout.columnName(i)
               << " | " << std::left << std::setw(type_width) << toString(layout.columnType(i));
            
            if (i < column_count - 1) {
                os << std::endl;
            }
        }
        
        // Always end with a newline and reset formatting
        os << std::endl;
        return os;
    }
} // namespace bcsv
