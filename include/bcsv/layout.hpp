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

#include "bcsv/definitions.h"
#include "layout.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <stdexcept>
#include <string>
    
namespace bcsv {

    // ========================================================================
    // Layout Implementation
    // ========================================================================

    inline void Layout::checkRange(size_t index) const {
        assert(column_names_.size() == column_types_.size());
        assert(column_names_.size() == column_index_.size());

        if constexpr (RANGE_CHECKING) {
            if (index >= columnCount()) {
                throw std::out_of_range("Layout::Column index out of range");
            }
        }
    }

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) {
        setColumns(columns);
    }
    
    inline void Layout::addColumn(ColumnDefinition column, size_t position) {
        if (columnCount() >= MAX_COLUMN_COUNT) [[unlikely]] {
            throw std::runtime_error("Cannot exceed maximum column count");
        }

        // if position is past the end or at the end of the current layout we simply append to the end.
        position = std::min(position, columnCount());

        column_index_.applyNameConventionAndInsert(column.name, position);
        column_names_.insert(column_names_.begin() + position, std::move(column.name));
        column_types_.insert(column_types_.begin() + position, column.type);
    }
    
    inline void Layout::clear() {
        column_names_.clear();
        column_index_.clear();
        column_types_.clear();
    }

    /**
     * @brief Check if this layout is compatible with another for data transfer
     * @param other The other layout to check compatibility with
     * @return true if data can be safely transferred between layouts
     */
    template<typename OtherLayout>
    inline bool Layout::isCompatible(const OtherLayout& other) const {
        if (columnCount() != other.columnCount()) {
            return false;
        }

        for (size_t i = 0; i < columnCount(); ++i) {
            if (columnType(i) != other.columnType(i)) {
                return false;
            }
        }
        return true;
    }

    inline void Layout::removeColumn(size_t index) {
        if (index >= column_names_.size()) {
            throw std::out_of_range("Layout::removeColumn: index " + std::to_string(index) + " out of range");
        }

        column_index_.erase(column_names_[index]);
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);
    }

    inline void Layout::setColumnName(size_t index, std::string name) {
        checkRange(index);
        if (column_names_[index] == name) {
            // NOP if name is unchanged
            return;
        }
        if(!column_index_.rename(column_names_[index], name)) [[unlikely]] {
            throw std::runtime_error("Column name '" + name + "' already exists or rename failed");
        }
        column_names_[index] = name;
    }

    inline void Layout::setColumns(const std::vector<ColumnDefinition>& columns)  {
        clear();

        if (columns.size() == 0) {
            // nothing to do
            return;
        }

        // Prepare storage
        column_index_.reserve(columns.size());
        column_names_.reserve(columns.size());
        column_types_.reserve(columns.size());
        
        for(size_t i = 0; i < columns.size(); ++i) {
            this->addColumn(columns[i]);
        }
    }

    inline void Layout::setColumnType(size_t index, ColumnType type) {
        checkRange(index);
        column_types_[index] = type;
    }

    template<typename OtherLayout>
    inline Layout& Layout::operator=(const OtherLayout& other) {
        clear();
        size_t size = other.columnCount();
        
        // We can reuse setColumns for a bulk import which handles 
        // all normalization and index building efficiently.
        std::vector<ColumnDefinition> cols;
        cols.reserve(size);
        
        for (size_t i = 0; i < size; ++i) {
            cols.push_back({other.columnName(i), other.columnType(i)});
        }
        
        setColumns(cols);
        return *this;
    }

    // ========================================================================
    // LayoutStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    inline constexpr void LayoutStatic<ColumnTypes...>::checkRange(size_t index) const {
        if constexpr (RANGE_CHECKING) {
            if (index >= sizeof...(ColumnTypes)) {
                throw std::out_of_range("LayoutStatic::Column index out of range");
            }
        }
    }
    
    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic() 
    {
        // we still require that ColumnTypes are part of ValueType
        static_assert((detail::is_in_variant_v<ColumnTypes, ValueType> && ...), "ColumnTypes must be present in bcsv::ValueType.");
        clear();
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames) 
    {
        // we still require that ColumnTypes are part of ValueType
        static_assert((detail::is_in_variant_v<ColumnTypes, ValueType> && ...), "ColumnTypes must be present in bcsv::ValueType.");
        setColumnNames(columnNames);
    }

    /* column count and types are fixed, simply reset names to default */
    template<typename... ColumnTypes>
    inline void LayoutStatic<ColumnTypes...>::clear() {
        column_index_.clear(); // resets all column names to their default names
        for(auto it = column_index_.begin(); it != column_index_.end(); ++it) {
            column_names_[it->second] = it->first;
        }
    }
                  
    template<typename... ColumnTypes>
    template<typename OtherLayout>
    inline bool LayoutStatic<ColumnTypes...>::isCompatible(const OtherLayout& other) const {
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
    inline void LayoutStatic<ColumnTypes...>::setColumnName(size_t index, std::string name) {
        checkRange(index);
        
        if(!column_index_.rename(column_names_[index], name)) [[unlikely]] {
            throw std::runtime_error("Column name '" + name + "' already exists or rename failed");
        }
        column_names_[index] = name;
    }

    template<typename... ColumnTypes>
    template<typename Container>
    inline bool LayoutStatic<ColumnTypes...>::setColumnNames(const Container& names) {
        constexpr size_t N = sizeof...(ColumnTypes);        
        if (names.size() != N) {
            return false; // size mismatch
        }

        for (size_t i = 0; i < N; ++i) {
            const std::string& name = names[i];
            setColumnName(i, name); // throws on conflict
        }
        return true;
    }


    template<typename... ColumnTypes>
    template<typename OtherLayout>
    inline LayoutStatic<ColumnTypes...>& LayoutStatic<ColumnTypes...>::operator=(const OtherLayout& other) {
        if (!this->isCompatible(other)) {
            throw std::runtime_error("Incompatible layout");
        }

        constexpr size_t N = sizeof...(ColumnTypes);
        if constexpr (N == 0) return *this;

        const void* otherPtr = static_cast<const void*>(&other);
        const void* thisPtr = static_cast<const void*>(this);

        if (thisPtr != otherPtr) {
            std::array<std::string, N> new_names;
            for(size_t i = 0; i < N; ++i) {
                new_names[i] = other.columnName(i);
            }
            this->setColumnNames(new_names);
        }
        return *this;
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
