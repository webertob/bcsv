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
    // Layout Implementation
    // ========================================================================

    inline Layout::Layout(const Layout& other) {
        column_types_ = other.column_types_;
        column_names_ = other.column_names_;
        column_index_ = other.column_index_;
        column_lengths_ = other.column_lengths_;
        column_offsets_ = other.column_offsets_;

        //don't copy row pointers
    }

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) {
        setColumns(columns);
    }

    inline size_t Layout::getColumnIndex(const std::string& columnName) const {
        auto it = column_index_.find(columnName);
        if (it != column_index_.end()) {
            return it->second;
        } else if constexpr (RANGE_CHECKING) {
            throw std::out_of_range("Column name not found: " + columnName);

        }
        return SIZE_MAX; // Return maximum value to indicate not found
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
        updateIndex();

        //update rows
        for (auto& row : rows_) {
            if (auto sp = row.lock()) {
                sp->data_.insert(sp->data_.begin() + position, defaultValue(column.type));
            }
        }
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

        //update rows
        for (auto& row : rows_) {
            if (auto sp = row.lock()) {
                sp->data_.clear();
            }
        }
    }
    
    inline void Layout::setColumns(const std::vector<ColumnDefinition>& columns) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
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

        // update rows (try to convert if convertible)
        for (auto& row : rows_) {
            if (auto sp = row.lock()) {
                sp->data_.resize(getColumnCount());
                for (size_t i = 0; i < sp->data_.size(); ++i) {
                    sp->data_[i] = convertValueType(sp->data_[i], column_types_[i]);
                }
            }
        }
    }

    inline void Layout::setColumnName(size_t index, const std::string& name) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }

        if(RANGE_CHECKING && index >= column_names_.size() ) {
            throw std::out_of_range("Column index out of range");
        }

        //no empty names
        if (name.empty()) {
            std::cerr << "Column name cannot be empty" << std::endl;
            return;
        }

        // Check for duplicate names
        if (column_index_.find(name) != column_index_.end() && column_names_[index] != name) {
            std::cerr << "Duplicate column name: " << name << " skip" << std::endl;
            return;
        }

        column_index_.erase(column_names_[index]); //remove old name from index
        column_names_[index] = name; //update name
        column_index_[name] = index; //add new name to index
    }


    inline void Layout::setColumnType(size_t index, ColumnDataType type) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if(column_types_[index] == type) {
            return; //no change
        }

        column_types_[index] = type;

        //need to update lengths and offsets
        column_lengths_[index] = binaryFieldLength(type);
        for (size_t i = index + 1; i < column_offsets_.size(); ++i) {
            column_offsets_[i] = column_offsets_[i - 1] + column_lengths_[i - 1];
        }

        // update rows (try to convert if convertible)
        for (auto& row : rows_) {
            if (auto sp = row.lock()) {
                sp->data_[index] = convertValueType(sp->data_[index], type);
            }
        }

    }

    inline void Layout::removeColumn(size_t index) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if (index >= column_names_.size()) {
            throw std::out_of_range("Column index out of range");
        }
        size_t removedLength = column_lengths_[index];

        // Remove from index map
        column_index_.erase(column_names_[index]);
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);
        column_offsets_.erase(column_offsets_.begin() + index);
        column_lengths_.erase(column_lengths_.begin() + index);

        // update subsequent offsets
        for (size_t i = index + 1; i < column_offsets_.size(); ++i) {
            column_offsets_[i] -= removedLength;
        }
        updateIndex();

        //update rows
        for (auto& row : rows_) {
            if (auto sp = row.lock()) {
                sp->data_.erase(sp->data_.begin() + index);
            }
        }
    }

    inline void Layout::addRow(std::weak_ptr<Row> row) {
        rows_.insert(row);
        if (auto sp = row.lock()) {
            sp->setLayout(shared_from_this());
            // ToDo: try to insert or remove into data_ using defaultValues to create minimum change to row (preserve as much information as possible)
            // for now we simply replace data_ with a version that matches layout
            sp->data_ = std::vector<ValueType>(column_types_.size());
            for (size_t i = 0; i < column_types_.size(); ++i) {
                sp->data_[i] = defaultValue(column_types_[i]);
            }
        }
    }

    inline void Layout::removeRow(std::weak_ptr<Row> row) {
        rows_.erase(row);
        if (auto sp = row.lock()) {
            sp->setLayout(nullptr);
        }
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

    template<LayoutConcept OtherLayout>
    inline Layout& Layout::operator=(const OtherLayout& other) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
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
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(const std::vector<std::string>& columnNames) 
    {
        if (columnNames.size() != sizeof...(ColumnTypes)) {
            throw std::invalid_argument("Number of column names must match number of column types");
        }
        for (size_t i = 0; i < columnNames_.size(); ++i) {
            column_names_[i] = columnNames[i];
        }
        updateIndex();
    }

    template<typename... ColumnTypes>
    inline size_t LayoutStatic<ColumnTypes...>::getColumnIndex(const std::string& columnName) const {
        auto it = column_index_.find(columnName);
        if (it != column_index_.end()) {
            return it->second;
        } else if constexpr (RANGE_CHECKING) {
            throw std::out_of_range("Column name not found: " + columnName);
        }
        return SIZE_MAX; // Return maximum value to indicate not found
    }

    template<typename... ColumnTypes>
    inline void LayoutStatic<ColumnTypes...>::setColumnName(size_t index, const std::string& name) {
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
        }
        if (RANGE_CHECKING && index >= sizeof...(ColumnTypes)) {
            throw std::out_of_range("Column index out of range");
        }

        //no empty names
        if (name.empty()) {
            std::cerr << "Column name cannot be empty" << std::endl;
            return;
        }

        // Check for duplicate names
        if (column_index_.find(name) != column_index_.end() && column_names_[index] != name) {
            std::cerr << "Duplicate column name: " << name << " skip" << std::endl;
            return;
        }

        column_index_.erase(column_names_[index]); //remove old name from index
        column_names_[index] = name; //update name
        column_index_[name] = index; //add new name to index
    }

    // Recursive helper to get type at runtime index
    template<typename... ColumnTypes>
    template<size_t Index>
    inline ColumnDataType LayoutStatic<ColumnTypes...>::getColumnTypeT(size_t index) const {
        if constexpr (Index < sizeof...(ColumnTypes)) {
            if (Index == index) {
                return toColumnDataType< column_type<Index> >;
            } else {
                return this->getColumnTypeT<Index + 1>(index);
            }
        } else {
            return ColumnDataType::STRING; // Should never reach here with valid index
        }
    }

    template<typename... ColumnTypes>
    template<LayoutConcept OtherLayout>
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
    template<LayoutConcept OtherLayout>
    inline LayoutStatic<ColumnTypes...>& LayoutStatic<ColumnTypes...>::operator=(const OtherLayout& other) {
        if (!this->isCompatibleWith(other)) {
            throw std::runtime_error("Incompatible layout");
        }
        if (isLocked()) {
            throw std::runtime_error("Cannot modify locked layout");
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
        column_index_.clear();
        for (size_t i = 0; i < column_names_.size(); ++i) {
            column_index_[column_names_[i]] = i;
        }
    }

} // namespace bcsv
