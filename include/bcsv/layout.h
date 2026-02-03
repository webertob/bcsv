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
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <cstdint>
#include <iostream>

#include "column_name_index.h"
#include "definitions.h"

// Forward declarations to avoid circular dependencies
namespace bcsv {
    class Row;
    class RowView;
    template<typename... ColumnTypes> class RowStatic;
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
        typename T::RowType;  // Each layout must define its row type
        typename T::RowViewType;  // Each layout must define its row view type
    };

    /**
     * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
     * This layout is flexible and can be modified at runtime.
     */
    class Layout {
    private:
        std::vector<std::string>       column_names_;  // column --> name
        ColumnNameIndex<0>             column_index_;  // name --> column
        std::vector<ColumnType>        column_types_;  // column --> type
    
    protected:
        void checkRange(size_t index) const;

    public:
        using RowType     = Row;
        using RowViewType = RowView;

        Layout() = default;
        Layout(const Layout& other) = default;
        explicit Layout(const std::vector<ColumnDefinition>& columns);
        ~Layout() = default;

                                        // Basic Layout information
        void                            addColumn(ColumnDefinition column, size_t position = SIZE_MAX);
        void                            clear();   
        size_t                          columnCount() const noexcept                    { return column_names_.size(); }
        size_t                          columnIndex(const std::string& name) const      { return column_index_[name]; }
        const std::string&              columnName(size_t index) const                  { checkRange(index); return column_names_[index]; }
        ColumnType                      columnType(size_t index) const                  { checkRange(index); return column_types_[index]; }
        bool                            hasColumn(const std::string& name) const        { return column_index_.contains(name); }
                                        
                                        template<typename OtherLayout>
        bool                            isCompatible(const OtherLayout& other) const;
        void                            removeColumn(size_t index);
        void                            setColumnName(size_t index, std::string name);
        void                            setColumnType(size_t index, ColumnType type);
        void                            setColumns(const std::vector<ColumnDefinition>& columns);
                                        
                                        template<typename OtherLayout>
        Layout&                         operator=(const OtherLayout& other);
    };

    /**
     * @brief Static layout definition for BCSV files.
     * 
     * This layout is defined at compile-time to improve performance and reduce runtime overhead.
     */
    template<typename... ColumnTypes>
    class LayoutStatic {        
        std::array<std::string, sizeof...(ColumnTypes)> column_names_;    // column --> name
        ColumnNameIndex< sizeof...(ColumnTypes) >       column_index_;    // name --> column
        

    protected: 
        constexpr void checkRange(size_t index) const;

    public:
        using RowType           = RowStatic<ColumnTypes...>;
        using RowViewType       = RowViewStatic<ColumnTypes...>;
        using column_types      = std::tuple<ColumnTypes...>;
                                  template<size_t Index>
        using column_type       = std::tuple_element_t<Index, column_types>;
        static constexpr std::array<ColumnType, sizeof...(ColumnTypes)> types = { toColumnType<ColumnTypes>()...  }; // column --> type

        LayoutStatic();
        LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames);
        LayoutStatic(const LayoutStatic&) = default;
        LayoutStatic(LayoutStatic&&) = default;
        ~LayoutStatic() = default;

                                        // Basic Layout information
        void                            clear();                                        // clear names to default values
        static constexpr size_t         columnCount() noexcept                          { return sizeof...(ColumnTypes); }
        size_t                          columnIndex(const std::string& name) const      { return column_index_[name]; }
        const std::string&              columnName(size_t index) const                  { checkRange(index); return column_names_[index]; }
                                        template<size_t Index>
        static constexpr ColumnType     columnType()                                    { return types[Index]; }          // compile-time version

        static ColumnType               columnType(size_t index)                        { if (index >= sizeof...(ColumnTypes)) { throw std::out_of_range("LayoutStatic::columnType: Index out of range"); } return types[index]; } // runtime version
        bool                            hasColumn(const std::string& name) const        { return column_index_.contains(name); }
                                        
                                        template<typename OtherLayout>
        bool                            isCompatible(const OtherLayout& other) const;
        void                            setColumnName(size_t index, std::string name);
                                        template<typename Container>
        bool                            setColumnNames(const Container& names);
                                        
        LayoutStatic&                   operator=(const LayoutStatic&) = default;      // Copies column names (types are compile-time fixed)
        LayoutStatic&                   operator=(LayoutStatic&&) = default;           // Moves column names (types are compile-time fixed)
                                        template<typename OtherLayout>
        LayoutStatic&                   operator=(const OtherLayout& other);           // Copies column names if layouts are compatible (types must match)
    };

    // Stream operator for Layout - provides human-readable column information
    template<LayoutConcept LayoutType>
    std::ostream& operator<<(std::ostream& os, const LayoutType& layout);

} // namespace bcsv
