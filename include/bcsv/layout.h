/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <cstdint>
#include <iostream>
#include <memory>
#include "bitset.h"
#include "column_name_index.h"
#include "definitions.h"

// Forward declarations to avoid circular dependencies
namespace bcsv {
    template<TrackingPolicy Policy> class RowImpl;
    class RowView;
    template<TrackingPolicy Policy, typename... ColumnTypes> class RowStaticImpl;
    template<typename... ColumnTypes> class RowViewStatic;
}

namespace bcsv {

    struct ColumnDefinition {
        std::string name;
        ColumnType  type;

        ColumnDefinition() 
            : name(""), type(ColumnType::STRING) {}
        ColumnDefinition(std::string_view n, ColumnType t)
            : name(n), type(t) {}
    };
    
    // ========================================================================
    // Layout Concept - Replaces LayoutInterface
    // ========================================================================
    template<typename T>
    concept LayoutConcept = requires(T layout, const T& const_layout, size_t index, const std::string& name, void* owner) {
        // Basic layout information
        { const_layout.columnCount()                    } -> std::convertible_to<size_t>;
        { const_layout.columnIndex(name)                } -> std::convertible_to<size_t>;
        { const_layout.columnName(index)                } -> std::convertible_to<std::string>;
        { const_layout.columnType(index)                } -> std::convertible_to<ColumnType>;
        { const_layout.hasColumn(name)                  } -> std::convertible_to<bool>;    
        { layout.setColumnName(index, name)             };  // No return type constraint (may return void or bool, may throw on error)
        { const_layout.isCompatible(const_layout)       } -> std::convertible_to<bool>;

        // Type information (for static layouts)
        typename T::template RowType<TrackingPolicy::Disabled>;  // Each layout must define its row type
        typename T::RowViewType;  // Each layout must define its row view type
    };

    /**
     * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
     * This layout is flexible and can be modified at runtime.
     * 
     * Note: This class is NOT thread-safe. External synchronization required for concurrent access.
     */
    class Layout {
    public:
        /**
         * @brief Shared layout data with all layout logic and observer management.
         * Contains column metadata and maintains consistency between names, indices, and types.
         * 
         * Note: NOT thread-safe. External synchronization required for concurrent access.
         */
        class Data {
        public:
            // Describes a single column transformation during layout update
            struct Change {
                uint16_t   index;      // Column index (post-transformation position for adds)
                ColumnType old_type;   // Type before change (nullopt if column is being added)
                ColumnType new_type;   // Type after change (nullopt if column is being removed)
            };

            struct Callbacks {
                std::function<void(const std::vector<Change>&)> update;  // Called BEFORE layout updates
            };

        private:
            std::vector<std::pair<void*, Callbacks>> callbacks_;     // Registered callbacks for layout changes
            std::vector<std::string>                 column_names_;  // column --> name
            ColumnNameIndex<0>                       column_index_;  // name --> column
            std::vector<ColumnType>                  column_types_;  // column --> type
            std::vector<uint32_t>                    offsets_;       // Unified per-column offsets into RowImpl's storage containers (bits_, data_, strg_). Meaning depends on columnType: BOOL→bit index in bits_, STRING→index in strg_, scalar→byte offset in data_ (aligned).
            Bitset<>                                 tracked_mask_;  // Bit i set iff column i is NOT BOOL (scalar or string). Size == columnCount.

            // Internal helpers
            void rebuildColumnIndex();
            void rebuildOffsets();  // Recomputes offsets_ from column_types_
            void checkRange(size_t index) const;

        public:
            Data();
            Data(const Data& other);  // Copy layout data, not observers
            Data(Data&&) = default;
            Data& operator=(const Data&) = delete;
            Data& operator=(Data&&) = delete;
            ~Data() = default;

            // ============================================================
            // Read-only accessors
            // ============================================================           
            size_t columnCount() const noexcept { return column_types_.size(); }
            size_t columnIndex(const std::string& name) const { return column_index_[name]; }
            const std::string& columnName(size_t index) const;
            ColumnType columnType(size_t index) const;
            const std::vector<ColumnType>& columnTypes() const noexcept { return column_types_; }
            bool hasColumn(const std::string& name) const { return column_index_.contains(name); }
            bool isCompatible(const Data& other) const;
            uint32_t columnOffset(size_t index) const;
            const std::vector<uint32_t>& columnOffsets() const noexcept { return offsets_; }
            Bitset<> boolMask() const noexcept { return ~tracked_mask_; }  // Derived: inverse of tracked_mask_
            const Bitset<>& trackedMask() const noexcept { return tracked_mask_; }

            /// Compute unified offsets from a type vector. Used by Row's onLayoutUpdate to pre-compute new offsets
            /// before the layout has been updated. Returns total data_ byte size via out parameter.
            static void computeOffsets(const std::vector<ColumnType>& types, std::vector<uint32_t>& offsets, uint32_t& dataSize);

            // ============================================================
            // Layout modification operations (with observer notifications)
            // ============================================================
            void addColumn(ColumnDefinition column, size_t position = SIZE_MAX);
            void clear();
            void removeColumn(size_t index);
            void setColumnName(size_t index, std::string name);
            void setColumnType(size_t index, ColumnType type);
            void setColumns(const std::vector<ColumnDefinition>& columns);
            void setColumns(const std::vector<std::string>& columnNames, const std::vector<ColumnType>& columnTypes);

            // ============================================================
            // Observer management
            // ============================================================
            void registerCallback(void* owner, Callbacks callbacks);
            void unregisterCallback(void* owner);

        private:
            // Notification method (called internally before modifications)
            void notifyUpdate(const std::vector<Change>& changes);
        };

        using DataPtr = std::shared_ptr<Data>;

    private:
        DataPtr data_;

    public:
        template<TrackingPolicy Policy = TrackingPolicy::Disabled>
        using RowType     = RowImpl<Policy>;
        using RowViewType = RowView;

        // ============================================================
        // Constructors
        // ============================================================
        Layout() : data_(std::make_shared<Data>()) {}
        explicit Layout(DataPtr data) : data_(std::move(data)) {}
        Layout(const Layout& other) = default;  // Shallow copy (shares data)
        explicit Layout(const std::vector<ColumnDefinition>& columns);
        explicit Layout(const std::vector<std::string>& columnNames, 
                       const std::vector<ColumnType>& columnTypes);
        ~Layout() = default;

        // ============================================================
        // Data access and sharing
        // ============================================================
        Layout clone() const;
        const DataPtr& data() const { return data_; }

        // ============================================================
        // Observer management (convenience for Row)
        // ============================================================
        void registerCallback(void* owner, Data::Callbacks callbacks) const {
            data_->registerCallback(owner, std::move(callbacks));
        }

        void unregisterCallback(void* owner) const {
            data_->unregisterCallback(owner);
        }

        // ============================================================
        // Layout information (facade - delegates to Data)
        // ============================================================        
        size_t columnCount() const noexcept { 
            return data_->columnCount(); 
        }
        
        size_t columnIndex(const std::string& name) const { 
            return data_->columnIndex(name); 
        }
        
        const std::string& columnName(size_t index) const { 
            return data_->columnName(index); 
        }
        
        ColumnType columnType(size_t index) const { 
            return data_->columnType(index); 
        }
        
        uint32_t columnOffset(size_t index) const {
            return data_->columnOffset(index);
        }
        
        const std::vector<uint32_t>& columnOffsets() const noexcept {
            return data_->columnOffsets();
        }
        
        Bitset<> boolMask() const noexcept {
            return data_->boolMask();
        }
        
        const Bitset<>& trackedMask() const noexcept {
            return data_->trackedMask();
        }

        const std::vector<ColumnType>& columnTypes() const noexcept { 
            return data_->columnTypes(); 
        }
        
        bool hasColumn(const std::string& name) const { 
            return data_->hasColumn(name); 
        }

        // ============================================================
        // Layout modification (facade - delegates to Data)
        // ============================================================
        void addColumn(ColumnDefinition column, size_t position = SIZE_MAX) {
            data_->addColumn(std::move(column), position);
        }
        
        void removeColumn(size_t index) {
            data_->removeColumn(index);
        }
        
        void setColumnName(size_t index, std::string name) {
            data_->setColumnName(index, std::move(name));
        }
        
        void setColumnType(size_t index, ColumnType type) {
            data_->setColumnType(index, type);
        }
        
        void setColumns(const std::vector<ColumnDefinition>& columns) {
            data_->setColumns(columns);
        }
        
        void setColumns(const std::vector<std::string>& columnNames, 
                       const std::vector<ColumnType>& columnTypes) {
            data_->setColumns(columnNames, columnTypes);
        }
        
        void clear() {
            data_->clear();
        }

        // ============================================================
        // Compatibility and assignment
        // ============================================================
        template<typename OtherLayout>
        bool isCompatible(const OtherLayout& other) const;
                
        template<typename OtherLayout>
        Layout& operator=(const OtherLayout& other);

        template<typename OtherLayout>
        bool operator==(const OtherLayout& other) const { return isCompatible(*other.data_); }
    };

    /**
     * @brief Static layout definition for BCSV files.
     * 
     * This layout is defined at compile-time to improve performance and reduce runtime overhead.
     */
    template<typename... ColumnTypes>
    class LayoutStatic {
    public:
        /**
         * @brief Shared layout data containing column names and name-to-index mapping.
         * Column types are compile-time constant, so only names need to be shared.
         */
        class Data {
        private:
            std::array<std::string, sizeof...(ColumnTypes)> column_names_;    // column --> name
            ColumnNameIndex< sizeof...(ColumnTypes) >       column_index_;    // name --> column

        public:
            Data();
            Data(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames);
            Data(const Data&) = default;
            Data(Data&&) = default;
            Data& operator=(const Data&) = default;
            Data& operator=(Data&&) = default;
            ~Data() = default;

            void clear();
            const std::string& columnName(size_t index) const;
            void setColumnName(size_t index, std::string name);
            template<typename Container>
            void setColumnNames(const Container& names, size_t offset = 0);
            size_t columnIndex(const std::string& name) const { return column_index_[name]; }
            bool hasColumn(const std::string& name) const { return column_index_.contains(name); }
            
            const auto& columnNames() const noexcept { return column_names_; }
            const auto& columnIndex() const noexcept { return column_index_; }
        };

        using DataPtr = std::shared_ptr<Data>;

    private:
        DataPtr data_;
        
        void checkRange(size_t index) const;

    public:
        template<TrackingPolicy Policy = TrackingPolicy::Disabled>
        using RowType           = RowStaticImpl<Policy, ColumnTypes...>;
        using RowViewType       = RowViewStatic<ColumnTypes...>;
        using ColTypes          = std::tuple<ColumnTypes...>;
                                  template<size_t Index>
        using ColType           = std::tuple_element_t<Index, ColTypes>;
        using ColTypesArray     = std::array<ColumnType, sizeof...(ColumnTypes)>;
        static constexpr ColTypesArray COLUMN_TYPES = { toColumnType<ColumnTypes>()...  }; // column --> type

        LayoutStatic();
        explicit LayoutStatic(DataPtr data);
        LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames);
        LayoutStatic(const LayoutStatic&) = default;  // Shallow copy (shares data)
        LayoutStatic(LayoutStatic&&) = default;
        ~LayoutStatic() = default;
        
        // Data access
        LayoutStatic clone() const;
        const DataPtr& data() const { return data_; }

                                        // Basic Layout information (facade - delegates to Data or static)
        void                            clear()                                         { data_->clear(); }
        static constexpr size_t         columnCount() noexcept                          { return sizeof...(ColumnTypes); }
        size_t                          columnIndex(const std::string& name) const      { return data_->columnIndex(name); }
        const std::string&              columnName(size_t index) const                  { checkRange(index); return data_->columnName(index); }
                                        template<size_t Index>
        static constexpr ColumnType     columnType()                                    { return COLUMN_TYPES[Index]; }          // compile-time version

        static ColumnType               columnType(size_t index)                        { if (index >= sizeof...(ColumnTypes)) { throw std::out_of_range("LayoutStatic::columnType: Index out of range"); } return COLUMN_TYPES[index]; } // runtime version
        static constexpr const ColTypesArray& 
                                        columnTypes() noexcept                          { return COLUMN_TYPES; }
        bool                            hasColumn(const std::string& name) const        { return data_->hasColumn(name); }
                                        
                                        template<typename OtherLayout>
        bool                            isCompatible(const OtherLayout& other) const;
        void                            setColumnName(size_t index, std::string name)   { data_->setColumnName(index, std::move(name)); }
                                        template<typename Container>
        void                            setColumnNames(const Container& names, size_t offset = 0) { data_->setColumnNames(names, offset); }
                                        
        LayoutStatic&                   operator=(const LayoutStatic&) = default;      // Shallow copy (shares data)
        LayoutStatic&                   operator=(LayoutStatic&&) = default;           // Move
                                        template<typename OtherLayout>
        LayoutStatic&                   operator=(const OtherLayout& other);           // Copies column names if layouts are compatible (types must match)
        
                                        template<typename OtherLayout>
        bool                            operator==(const OtherLayout& other) const { return isCompatible(other); }
    };

    // Stream operator for Layout - provides human-readable column information
    template<LayoutConcept LayoutType>
    std::ostream& operator<<(std::ostream& os, const LayoutType& layout);

} // namespace bcsv
