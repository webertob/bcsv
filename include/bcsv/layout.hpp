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
        , offsets_(other.offsets_)
        , tracked_mask_(other.tracked_mask_)
        , wire_bits_size_(other.wire_bits_size_)
        , wire_data_size_(other.wire_data_size_)
        , wire_strg_size_(other.wire_strg_size_)
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

    inline uint32_t Layout::Data::columnOffset(size_t index) const {
        checkRange(index);
        return offsets_[index];
    }

    /// Compute unified offsets from column types.
    /// For BOOL: sequential bool index (0, 1, 2, ...)
    /// For STRING: sequential string index (0, 1, 2, ...)
    /// For scalars: byte offset into data_ buffer (naturally aligned)
    /// dataSize receives the total byte size needed for the scalar data_ buffer.
    inline void Layout::Data::computeOffsets(const std::vector<ColumnType>& types, std::vector<uint32_t>& offsets, uint32_t& dataSize) {
        offsets.resize(types.size());
        uint32_t bitIdx  = 0;
        uint32_t dataOff = 0;
        uint32_t strgIdx = 0;
        for (size_t i = 0; i < types.size(); ++i) {
            ColumnType type = types[i];
            if (type == ColumnType::BOOL) {
                offsets[i] = bitIdx++;
            } else if (type == ColumnType::STRING) {
                offsets[i] = strgIdx++;
            } else {
                uint32_t alignment = static_cast<uint32_t>(alignOf(type));
                dataOff = (dataOff + (alignment - 1)) & ~(alignment - 1);
                offsets[i] = dataOff;
                dataOff += static_cast<uint32_t>(sizeOf(type));
            }
        }
        dataSize = dataOff;
    }

    inline void Layout::Data::rebuildOffsets() {
        const size_t n = column_types_.size();
        offsets_.resize(n);
        tracked_mask_.resize(n);
        tracked_mask_.reset();

        uint32_t bitIdx  = 0;
        uint32_t dataOff = 0;
        uint32_t strgIdx = 0;
        uint32_t wireOff = 0;  // packed wire-format data offset (no alignment)
        for (size_t i = 0; i < n; ++i) {
            ColumnType type = column_types_[i];
            if (type == ColumnType::BOOL) {
                offsets_[i] = bitIdx++;
            } else if (type == ColumnType::STRING) {
                offsets_[i] = strgIdx++;
                tracked_mask_[i] = true;
            } else {
                uint32_t alignment = static_cast<uint32_t>(alignOf(type));
                uint32_t size = static_cast<uint32_t>(sizeOf(type));
                dataOff = (dataOff + (alignment - 1)) & ~(alignment - 1);
                offsets_[i] = dataOff;
                dataOff += size;
                wireOff += size;
                tracked_mask_[i] = true;
            }
        }
        // Update wire-format metadata
        wire_bits_size_ = (bitIdx + 7) >> 3;    // ⌈bool_count / 8⌉
        wire_data_size_ = wireOff;              // packed scalars (no alignment padding)
        wire_strg_size_ = strgIdx;              // number of string columns
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

        // Incremental offset/mask update — avoid full rebuild.
        // For the common append-at-end case: O(1). For mid-insert: O(n).
        const ColumnType type = column.type;
        const bool isAppend = (position == column_types_.size() - 1); // we already inserted into column_types_
        if (type == ColumnType::BOOL) {
            if (isAppend) {
                // O(1): offset = count of existing BOOL columns = total - tracked
                offsets_.push_back(static_cast<uint32_t>(tracked_mask_.size() - tracked_mask_.count()));
            } else {
                uint32_t boolIdx = 0;
                for (size_t i = 0; i < position; ++i) {
                    if (column_types_[i] == ColumnType::BOOL) ++boolIdx;
                }
                offsets_.insert(offsets_.begin() + position, boolIdx);
                for (size_t i = position + 1; i < column_types_.size(); ++i) {
                    if (column_types_[i] == ColumnType::BOOL) offsets_[i]++;
                }
            }
            tracked_mask_.insert(position, false);
        } else if (type == ColumnType::STRING) {
            // String offset = sequential string index (count of STRING columns before us).
            // O(n) scan — acceptable since string-append is not the hot path (bool-append is).
            {
                uint32_t strgIdx = 0;
                for (size_t i = 0; i < position; ++i) {
                    if (column_types_[i] == ColumnType::STRING) ++strgIdx;
                }
                if (isAppend) {
                    offsets_.push_back(strgIdx);
                } else {
                    offsets_.insert(offsets_.begin() + position, strgIdx);
                    for (size_t i = position + 1; i < column_types_.size(); ++i) {
                        if (column_types_[i] == ColumnType::STRING) offsets_[i]++;
                    }
                }
            }
            tracked_mask_.insert(position, true);
        } else {
            // Scalar: byte offset depends on alignment
            uint32_t dataOff = 0;
            for (size_t i = 0; i < position; ++i) {
                if (column_types_[i] != ColumnType::BOOL && column_types_[i] != ColumnType::STRING) {
                    uint32_t a = static_cast<uint32_t>(alignOf(column_types_[i]));
                    dataOff = (dataOff + (a - 1)) & ~(a - 1);
                    dataOff += static_cast<uint32_t>(sizeOf(column_types_[i]));
                }
            }
            if (isAppend) {
                uint32_t a = static_cast<uint32_t>(alignOf(type));
                dataOff = (dataOff + (a - 1)) & ~(a - 1);
                offsets_.push_back(dataOff);
            } else {
                offsets_.insert(offsets_.begin() + position, 0u);
                for (size_t i = position; i < column_types_.size(); ++i) {
                    if (column_types_[i] != ColumnType::BOOL && column_types_[i] != ColumnType::STRING) {
                        uint32_t a = static_cast<uint32_t>(alignOf(column_types_[i]));
                        dataOff = (dataOff + (a - 1)) & ~(a - 1);
                        offsets_[i] = dataOff;
                        dataOff += static_cast<uint32_t>(sizeOf(column_types_[i]));
                    }
                }
            }
            tracked_mask_.insert(position, true);
        }

        // Update wire-format metadata incrementally
        const uint32_t boolCount = static_cast<uint32_t>(tracked_mask_.size() - tracked_mask_.count());
        if (type == ColumnType::BOOL) {
            wire_bits_size_ = (boolCount + 7) >> 3;
        } else if (type == ColumnType::STRING) {
            wire_strg_size_++;
        } else {
            wire_data_size_ += static_cast<uint32_t>(sizeOf(type));
        }
    }

    inline void Layout::Data::removeColumn(size_t index) {
        if (index >= column_names_.size()) {
            throw std::out_of_range("Layout::Data::removeColumn: index " + std::to_string(index) + " out of range");
        }

        // Build change notification (removing column at index)
        std::vector<Change> changes = {{static_cast<uint16_t>(index), column_types_[index], ColumnType::VOID}};
        
        // Notify observers BEFORE making changes
        notifyUpdate(changes);
        
        // Capture removed column info for incremental update
        const ColumnType removedType = column_types_[index];

        // Now update the layout
        column_index_.remove(column_names_[index]);
        column_names_.erase(column_names_.begin() + index);
        column_types_.erase(column_types_.begin() + index);

        // Incremental offset/mask update.
        tracked_mask_.erase(index);
        offsets_.erase(offsets_.begin() + index);

        if (removedType == ColumnType::BOOL) {
            // Re-number BOOL indices after the removed position
            for (size_t i = index; i < column_types_.size(); ++i) {
                if (column_types_[i] == ColumnType::BOOL) offsets_[i]--;
            }
            const uint32_t boolCount = static_cast<uint32_t>(tracked_mask_.size() - tracked_mask_.count());
            wire_bits_size_ = (boolCount + 7) >> 3;
        } else if (removedType == ColumnType::STRING) {
            // Re-number STRING indices after the removed position
            for (size_t i = index; i < column_types_.size(); ++i) {
                if (column_types_[i] == ColumnType::STRING) offsets_[i]--;
            }
            wire_strg_size_--;
        } else {
            // Scalar: recompute all scalar offsets (alignment-dependent)
            uint32_t dataOff = 0;
            uint32_t wireOff = 0;
            for (size_t i = 0; i < column_types_.size(); ++i) {
                ColumnType t = column_types_[i];
                if (t != ColumnType::BOOL && t != ColumnType::STRING) {
                    uint32_t a = static_cast<uint32_t>(alignOf(t));
                    dataOff = (dataOff + (a - 1)) & ~(a - 1);
                    offsets_[i] = dataOff;
                    dataOff += static_cast<uint32_t>(sizeOf(t));
                    wireOff += static_cast<uint32_t>(sizeOf(t));
                }
            }
            wire_data_size_ = wireOff;
        }
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
        rebuildOffsets();
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
        rebuildOffsets();
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
        rebuildOffsets();
        
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
        offsets_.clear();
        tracked_mask_.resize(0);
        wire_bits_size_ = 0;
        wire_data_size_ = 0;
        wire_strg_size_ = 0;
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
            return std::memcmp(COLUMN_TYPES.data(), other_types.data(), N * sizeof(ColumnType)) == 0;
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
