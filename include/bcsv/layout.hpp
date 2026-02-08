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
#include <cstring>   // for std::memcmp
#include <iomanip>
#include <stdexcept>
#include <string>
#include <type_traits>
    
namespace bcsv {

    // ========================================================================
    // Layout::Data Implementation
    // ========================================================================

    inline Layout::Data::Data() {
        callbacks_.reserve(64);  // Reserve typical capacity to avoid early reallocations
    }

    inline Layout::Data::Data(const Data& other)
        : callbacks_()          // Don't copy callbacks - new Data has no observers
        , column_names_(other.column_names_)
        , column_index_(other.column_index_)
        , column_types_(other.column_types_)
    {
        callbacks_.reserve(64);
    }

    inline void Layout::Data::rebuildColumnIndex() {
        column_index_.clear();
        column_index_.build(column_names_);
    }

    inline void Layout::Data::checkRange(size_t index) const {
        if constexpr (RANGE_CHECKING) {
            if (index >= column_types_.size()) {
                throw std::out_of_range("Layout::Data::Column index out of range");
            }
        }
    }

    inline const std::string& Layout::Data::columnName(size_t index) const {
        checkRange(index);
        return column_names_[index];
    }

    inline ColumnType Layout::Data::columnType(size_t index) const {
        checkRange(index);
        return column_types_[index];
    }

    inline void Layout::Data::addColumn(ColumnDefinition column, size_t position) {
        if (column_types_.size() >= MAX_COLUMN_COUNT) [[unlikely]] {
            throw std::runtime_error("Cannot exceed maximum column count");
        }

        // if position is past the end or at the end of the current layout we simply append to the end.
        position = std::min(position, column_types_.size());

        // Build change notification (post-insert semantics: index is where column will be)
        std::vector<Change> changes = {{static_cast<uint16_t>(position), ColumnType::VOID, column.type}};
        
        // Notify observers BEFORE making changes
        notifyUpdate(changes);
        
        // Now update the layout
        column_index_.insert(column.name, position);
        column_names_.insert(column_names_.begin() + position, column.name);
        column_types_.insert(column_types_.begin() + position, column.type);
    }

    inline void Layout::Data::removeColumn(size_t index) {
        if (index >= column_names_.size()) {
            throw std::out_of_range("Layout::Data::removeColumn: index " + std::to_string(index) + " out of range");
        }

        // Build change notification (removing column at index)
        std::vector<Change> changes = {{static_cast<uint16_t>(index), column_types_[index], ColumnType::VOID}};
        
        // Notify observers BEFORE making changes
        notifyUpdate(changes);
        
        // Now update the layout
        column_index_.remove(column_names_[index]);
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);
    }

    inline void Layout::Data::setColumnName(size_t index, std::string name) {
        checkRange(index);
        if (column_names_[index] == name) {
            // NOP if name is unchanged
            return;
        }
        if(!column_index_.rename(column_names_[index], name)) [[unlikely]] {
            throw std::runtime_error("Column name '" + name + "' already exists or rename failed");
        }
        column_names_[index] = std::move(name);
        // Note: Name changes don't trigger notifications (as per design doc)
    }

    inline void Layout::Data::setColumnType(size_t index, ColumnType type) {
        checkRange(index);
        ColumnType oldType = column_types_[index];
        if (oldType == type) {
            return;  // No change
        }
        
        // Build change notification (type change at index)
        std::vector<Change> changes = {{static_cast<uint16_t>(index), oldType, type}};
        
        // Notify observers BEFORE changing type (so they can query current state)
        notifyUpdate(changes);
        
        // Now change the type
        column_types_[index] = type;
    }

    inline void Layout::Data::setColumns(const std::vector<ColumnDefinition>& columns) {
        // Build full replacement change list (compare old vs new at each index)
        const size_t oldSize = column_types_.size();
        const size_t newSize = columns.size();
        const size_t maxSize = std::max(oldSize, newSize);
        
        std::vector<Change> changes;
        changes.reserve(maxSize);
        
        for (uint16_t i = 0; i < maxSize; ++i) {
            ColumnType oldType = (i < oldSize) ? column_types_[i] : ColumnType::VOID;
            ColumnType newType = (i < newSize) ? columns[i].type : ColumnType::VOID;
            changes.push_back({i, oldType, newType});
        }
        
        // Notify observers BEFORE making changes (they can query old layout)
        notifyUpdate(changes);
        
        // Now update the layout
        clear();

        if (columns.size() == 0) {
            return;
        }

        // Prepare storage
        column_index_.reserve(columns.size());
        column_names_.resize(columns.size());
        column_types_.resize(columns.size());
        
        for(size_t i = 0; i < columns.size(); ++i) {
            column_names_[i] = columns[i].name;
            column_types_[i] = columns[i].type;
        }
        column_index_.build(column_names_);
    }

    inline void Layout::Data::setColumns(const std::vector<std::string>& columnNames, 
                                         const std::vector<ColumnType>& columnTypes) {
        if (columnNames.size() != columnTypes.size()) {
            throw std::invalid_argument("Column names and types size mismatch");
        }
        clear();

        if (columnNames.size() == 0) {
            return;
        }

        // Prepare storage
        column_index_.reserve(columnNames.size());
        column_names_ = columnNames;
        column_types_ = columnTypes;
        column_index_.build(column_names_);
        
        // Note: Bulk setColumns doesn't trigger individual notifications
    }

    
    /**
     * @brief Check if this layout is compatible with another for data transfer
     * @param other The other layout to check compatibility with
     * @return true if data can be safely transferred between layouts
     * 
     * Supports: Layout, LayoutStatic, Layout::Data, shared_ptr<Layout::Data>
     */
    inline bool Layout::Data::isCompatible(const Data& other) const {
        if (this == &other) {
            return true;
        }
        if (columnCount() != other.columnCount()) {
            return false;
        }
        return std::memcmp(column_types_.data(), other.column_types_.data(), columnCount() * sizeof(ColumnType)) == 0;
    }
    

    inline void Layout::Data::clear() {
        column_names_.clear();
        column_index_.clear();
        column_types_.clear();
        // Note: clear() doesn't trigger notifications
    }

    inline void Layout::Data::registerCallback(void* owner, Callbacks callbacks) {
        callbacks_.emplace_back(owner, std::move(callbacks));
    }

    inline void Layout::Data::unregisterCallback(void* owner) {
        auto it = std::find_if(callbacks_.begin(), callbacks_.end(),
            [owner](const auto& p) { return p.first == owner; });
        if (it != callbacks_.end()) {
            std::swap(*it, callbacks_.back());
            callbacks_.pop_back();  // O(1) removal
        }
    }

    inline void Layout::Data::notifyUpdate(const std::vector<Change>& changes) {
        for (auto& [owner, cb] : callbacks_) {
            if (cb.update) {
                cb.update(changes);
            }
        }
    }

    // ========================================================================
    // Layout Implementation
    // ========================================================================

    // Implementation included inline for header-only library
    inline Layout::Layout(const std::vector<ColumnDefinition>& columns) 
        : data_(std::make_shared<Data>()) {
        data_->setColumns(columns);
    }
    
    inline Layout::Layout(const std::vector<std::string>& columnNames, 
                         const std::vector<ColumnType>& columnTypes)
        : data_(std::make_shared<Data>()) {
        data_->setColumns(columnNames, columnTypes);
    }

    inline Layout Layout::clone() const {
        auto new_data = std::make_shared<Data>(*data_);
        return Layout(new_data);
    }

    template<typename OtherLayout>
    inline bool Layout::isCompatible(const OtherLayout& other) const {
        // Fast path: if other is same Layout type, compare shared_ptr pointers
        using DecayedType = std::decay_t<OtherLayout>;
        if constexpr (std::is_same_v<DecayedType, Layout>) {
            // Pointer equality check - O(1) when sharing same data
            if (data_ == other.data_) 
                return true;
            else
                return data_->isCompatible(*other.data_);
        } else if constexpr (requires { other.columnTypes(); }) {
            if(columnTypes().size() != other.columnTypes().size()) {
                return false;
            } 
            return memcmp(columnTypes().data(), other.columnTypes().data(), columnTypes().size() * sizeof(ColumnType)) == 0;   
        }
        return false;
    }

    template<typename OtherLayout>
    inline Layout& Layout::operator=(const OtherLayout& other) {
        data_->clear();
        size_t size = other.columnCount();
        
        // We can reuse setColumns for a bulk import which handles 
        // all normalization and index building efficiently.
        std::vector<ColumnDefinition> cols;
        cols.reserve(size);
        
        for (size_t i = 0; i < size; ++i) {
            cols.push_back({other.columnName(i), other.columnType(i)});
        }
        
        data_->setColumns(cols);
        return *this;
    }




    // ========================================================================
    // LayoutStatic::Data Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::Data::Data() {
        column_index_.clear(); // Initialize with default column names
        for(auto it = column_index_.begin(); it != column_index_.end(); ++it) {
            column_names_[it->second] = it->first;
        }
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::Data::Data(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames) 
        : column_names_(columnNames)
    {
        column_index_.build(column_names_);
    }

    template<typename... ColumnTypes>
    inline void LayoutStatic<ColumnTypes...>::Data::clear() {
        column_index_.clear(); // resets all column names to their default names
        for(auto it = column_index_.begin(); it != column_index_.end(); ++it) {
            column_names_[it->second] = it->first;
        }
    }

    template<typename... ColumnTypes>
    inline const std::string& LayoutStatic<ColumnTypes...>::Data::columnName(size_t index) const {
        if constexpr (RANGE_CHECKING) {
            if (index >= sizeof...(ColumnTypes)) {
                throw std::out_of_range("LayoutStatic::Data::columnName: index out of range");
            }
        }
        return column_names_[index];
    }

    template<typename... ColumnTypes>
    inline void LayoutStatic<ColumnTypes...>::Data::setColumnName(size_t index, std::string name) {
        if constexpr (RANGE_CHECKING) {
            if (index >= sizeof...(ColumnTypes)) {
                throw std::out_of_range("LayoutStatic::Data::setColumnName: index out of range");
            }
        }
        
        if(!column_index_.rename(column_names_[index], name)) [[unlikely]] {
            throw std::runtime_error("Column name '" + name + "' already exists or rename failed");
        }
        column_names_[index] = std::move(name);
    }

    template<typename... ColumnTypes>
    template<typename Container>
    inline void LayoutStatic<ColumnTypes...>::Data::setColumnNames(const Container& names, size_t offset) {
        if (names.size() + offset != sizeof...(ColumnTypes)) {
            throw std::out_of_range("LayoutStatic::Data::setColumnNames() size mismatch");
        }
        for(size_t i = 0; i < names.size(); ++i) {
            column_names_[i + offset] = names[i];
        }
        column_index_.build(column_names_); // build index after bulk insertion
    }

    // ========================================================================
    // LayoutStatic Implementation
    // ========================================================================

    template<typename... ColumnTypes>
    inline void LayoutStatic<ColumnTypes...>::checkRange(size_t index) const {
        if constexpr (RANGE_CHECKING) {
            if (index >= sizeof...(ColumnTypes)) {
                throw std::out_of_range("LayoutStatic::Column index out of range");
            }
        }
    }
    
    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic() 
        : data_(std::make_shared<Data>())
    {
        // we still require that ColumnTypes are part of ValueType
        static_assert((detail::is_in_variant_v<ColumnTypes, ValueType> && ...), "ColumnTypes must be present in bcsv::ValueType.");
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(DataPtr data)
        : data_(std::move(data))
    {
        static_assert((detail::is_in_variant_v<ColumnTypes, ValueType> && ...), "ColumnTypes must be present in bcsv::ValueType.");
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...>::LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames) 
        : data_(std::make_shared<Data>(columnNames))
    {
        // we still require that ColumnTypes are part of ValueType
        static_assert((detail::is_in_variant_v<ColumnTypes, ValueType> && ...), "ColumnTypes must be present in bcsv::ValueType.");
    }

    template<typename... ColumnTypes>
    inline LayoutStatic<ColumnTypes...> LayoutStatic<ColumnTypes...>::clone() const {
        auto new_data = std::make_shared<Data>(*data_);
        return LayoutStatic(new_data);
    }

    template<typename... ColumnTypes>
    template<typename OtherLayout>
    inline bool LayoutStatic<ColumnTypes...>::isCompatible(const OtherLayout& other) const {
        constexpr size_t N = sizeof...(ColumnTypes);
        
        // Runtime check: different column count means incompatible
        if (N != other.columnCount()) {
            return false;
        }

        // Compile-time dispatch based on OtherLayout type
        if constexpr (requires { typename OtherLayout::ColTypes; }) {            
            // Compile-time optimization: if types are identical, always compatible
            if constexpr (std::is_same_v<std::tuple<ColumnTypes...>, typename OtherLayout::ColTypes>) {
                return true;
            } else {
                return false;
            }
        } else {
            // Types differ: use memcmp for fast comparison
            const auto& other_types = other.columnTypes();
            return std::memcmp(column_types_.data(), other_types.data(), N * sizeof(ColumnType)) == 0;
        }
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
            data_->setColumnNames(new_names);
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
