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
    }

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) {
        setColumns(columns);
    }

    inline size_t Layout::getColumnIndex(const std::string& columnName) const {
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
        if (column_index_.find(column.name) != column_index_.end()) {
            std::cerr << "Duplicate column name: " + column.name << std::endl;
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
        updateIndex();
        return true;
    }
    
    inline void Layout::clear() {
        column_names_.clear();
        column_types_.clear();
        column_lengths_.clear();
        column_offsets_.clear();
        column_index_.clear();
    }
    
    inline void Layout::setColumns(const std::vector<ColumnDefinition>& columns) {
        clear();
        column_names_.resize(columns.size());
        column_types_.resize(columns.size());
        column_lengths_.resize(columns.size());
        column_offsets_.resize(columns.size());

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


    inline void Layout::setColumnType(size_t index, ColumnDataType type) {
        if(column_types_[index] == type) {
            return; //no change
        }

        column_types_[index] = type;
        //need to update lengths and offsets
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

        // update subsequent offsets
        for (size_t i = index + 1; i < column_offsets_.size(); ++i) {
            column_offsets_[i] -= col_length;
        }
        updateIndex();
    }

    /**
     * @brief Check if this layout is compatible with another for data transfer
     * @param other The other layout to check compatibility with
     * @return true if data can be safely transferred between layouts
     */
    inline bool Layout::isCompatibleWith(const Layout& other) const {
        // different column counts means not equivalent
        if (getColumnCount() != other.getColumnCount()) {
            return false;
        }

        // different column types means not equivalent
        const size_t columnCount = getColumnCount();
        for (size_t i = 0; i < columnCount; ++i) {
            if (getColumnType(i) != other.getColumnType(i)) {
                return false;
            }
        }
        return true;
    }

    template<typename OtherLayout>
    requires requires(const OtherLayout& other) {
        { other.getColumnCount() } -> std::convertible_to<size_t>;
        { other.getColumnType(size_t{}) } -> std::convertible_to<ColumnDataType>;
    }
    inline Layout& Layout::operator=(const OtherLayout& other) {
        clear();
        size_t size = other.getColumnCount();
        column_names_.resize(size);
        column_types_.resize(size);
        column_lengths_.resize(size);
        column_offsets_.resize(size);

        for (size_t i = 0; i < size; ++i) {
            column_types_[i] = other.getColumnType(i);
            column_names_[i] = other.getColumnName(i);
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
    inline size_t LayoutStatic<ColumnTypes...>::getColumnIndex(const std::string& columnName) const {
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
    inline ColumnDataType LayoutStatic<ColumnTypes...>::getColumnTypeT(size_t index) const {
        if constexpr (Index < sizeof...(ColumnTypes)) {
            if (Index == index) {
                return toColumnDataType< column_type<Index> >();
            } else {
                return this->getColumnTypeT<Index + 1>(index);
            }
        } else {
            return ColumnDataType::STRING; // Should never reach here with valid index
        }
    }

    template<typename... ColumnTypes>
    template<typename OtherLayout>
    requires requires(const OtherLayout& other) {
        { other.getColumnCount() } -> std::convertible_to<size_t>;
        { other.getColumnType(size_t{}) } -> std::convertible_to<ColumnDataType>;
    }
    inline bool LayoutStatic<ColumnTypes...>::isCompatibleWith(const OtherLayout& other) const {
        // Check if the number of columns is the same
        if (sizeof...(ColumnTypes) != other.getColumnCount()) {
            return false;
        }

        // Check if the column types are the same
        for (size_t i = 0; i < sizeof...(ColumnTypes); ++i) {
            if (getColumnType(i) != other.getColumnType(i)) {
                return false;
            }
        }

        return true;
    }

    template<typename... ColumnTypes>
    template<typename OtherLayout>
    requires requires(const OtherLayout& other) {
        { other.getColumnCount() } -> std::convertible_to<size_t>;
        { other.getColumnType(size_t{}) } -> std::convertible_to<ColumnDataType>;
    }
    inline LayoutStatic<ColumnTypes...>& LayoutStatic<ColumnTypes...>::operator=(const OtherLayout& other) {
        if (!this->isCompatibleWith(other)) {
            throw std::runtime_error("Incompatible layout");
        }
        if (this != &other) {
            for (size_t i = 0; i < column_names_.size(); ++i) {
                column_names_[i] = other.getColumnName(i);
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

} // namespace bcsv
