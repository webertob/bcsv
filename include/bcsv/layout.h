#pragma once

#include <set>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <memory>
#include <stdexcept>

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
        ColumnDefinition() : name(""), type(ColumnType::STRING) {}
        ColumnDefinition(const std::string& n, ColumnType t)
            : name(n), type(t) {}
        std::string name;
        ColumnType type;
    };
    
    // ========================================================================
    // Layout Concept - Replaces LayoutInterface
    // ========================================================================
    template<typename T>
    concept LayoutConcept = requires(T layout, const T& const_layout, size_t index, const std::string& name, void* owner) {
        // Basic layout information
        { const_layout.hasColumn(name)                  } -> std::convertible_to<bool>;
        { const_layout.columnCount()                    } -> std::convertible_to<size_t>;
        { const_layout.columnIndex(name)                } -> std::convertible_to<size_t>;
        { const_layout.columnLength(index)              } -> std::convertible_to<size_t>;
        { const_layout.columnName(index)                } -> std::convertible_to<std::string>;
        { const_layout.columnOffset(index)              } -> std::convertible_to<size_t>;
        { const_layout.columnType(index)                } -> std::convertible_to<ColumnType>;
        { layout.setColumnName(index, name)             } -> std::same_as<bool>;
        { const_layout.isCompatibleWith(const_layout)   } -> std::convertible_to<bool>;
        
        // Type information (for static layouts)
        typename T::RowType;  // Each layout must define its row type
        typename T::RowViewType;  // Each layout must define its row view type
    };


    /**
     * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
     * This layout is flexible and can be modified at runtime.
     */
    class Layout {
        std::vector<std::string>                column_names_;
        std::unordered_map<std::string, size_t> column_index_;
        std::vector<ColumnType>                 column_types_;
        std::vector<size_t>                     column_lengths_; // Lengths of each column in [bytes] --> serialized data
        std::vector<size_t>                     column_offsets_; // Offsets of each column in [bytes] --> serialized data
        void updateIndex();

    public:
        using RowType = Row;
        using RowViewType = RowView;

        Layout() = default;
        Layout(const Layout& other);
        
        explicit Layout(const std::vector<ColumnDefinition>& columns);
        ~Layout() = default;

        // Basic Layout information
        bool hasColumn(const std::string& name) const               { return column_index_.find(name) != column_index_.end(); }
        size_t columnCount() const                                  { return column_names_.size(); }
        size_t columnIndex(const std::string& name) const;
        size_t columnLength(size_t index) const                     { if constexpr (RANGE_CHECKING) {return column_lengths_.at(index);} else { return column_lengths_[index]; } }
        const std::string& columnName(size_t index) const           { if constexpr (RANGE_CHECKING) {return column_names_.at(index);}   else { return column_names_[index]; } }
        size_t columnOffset(size_t index) const                     { if constexpr (RANGE_CHECKING) {return column_offsets_.at(index);} else { return column_offsets_[index]; } }
        ColumnType columnType(size_t index) const                   { if constexpr (RANGE_CHECKING) {return column_types_.at(index);}   else { return column_types_[index]; } }
        bool setColumnName(size_t index, const std::string& name);
        void setColumnType(size_t index, ColumnType type);
        void setColumns(const std::vector<ColumnDefinition>& columns);

        // Compatibility checking
        bool isCompatibleWith(const Layout& other) const;

        void clear();
        bool addColumn(const ColumnDefinition& column, size_t position = SIZE_MAX);
        void removeColumn(size_t index);
        

        template<typename OtherLayout>
        requires requires(const OtherLayout& other) {
            { other.columnCount()        } -> std::convertible_to<size_t>;
            { other.columnType(size_t{}) } -> std::convertible_to<ColumnType>;
        }
        Layout& operator=(const OtherLayout& other);
        Layout& operator=(const Layout& other);
    };




    /**
     * @brief Static layout definition for BCSV files.
     * 
     * This layout is defined at compile-time to improve performance and reduce runtime overhead.
     */
    template<typename... ColumnTypes>
    class LayoutStatic {
        std::array<std::string, sizeof...(ColumnTypes)>     column_names_;
        std::unordered_map<std::string, size_t>             column_index_;
        void updateIndex();

    public:
        using RowType = RowStatic<ColumnTypes...>;
        using RowViewType = RowViewStatic<ColumnTypes...>;

        using column_types = std::tuple<ColumnTypes...>;
        template<size_t Index>
        using column_type = typename std::tuple_element_t<Index, std::tuple<ColumnTypes...>>;

        // Lengths of each column in [bytes] --> serialized data
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> columnLengths() {
            std::array<size_t, sizeof...(ColumnTypes)> lengths{};
            size_t index = 0;
            ((lengths[index++] = binaryFieldLength<ColumnTypes>()), ...);
            return lengths;
        }

        // Offsets of each column in [bytes] --> serialized data
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> columnOffsets() {
            std::array<size_t, sizeof...(ColumnTypes)> offsets{};
            size_t offset = 0;
            size_t index = 0;
            ((offsets[index++] = offset, offset += binaryFieldLength<ColumnTypes>()), ...);
            return offsets;
        }

        static constexpr size_t fixed_size = (binaryFieldLength<ColumnTypes>() + ... + 0);
        static constexpr size_t column_count = sizeof...(ColumnTypes);
        static constexpr auto column_lengths = columnLengths();
        static constexpr auto column_offsets = columnOffsets();


        LayoutStatic();
        LayoutStatic(const std::array<std::string, sizeof...(ColumnTypes)>& columnNames);

        // Basic Layout information
        bool                        hasColumn(const std::string& name) const    { return column_index_.find(name) != column_index_.end(); }
        constexpr size_t            columnCount() const                         { return sizeof...(ColumnTypes); }
        size_t                      columnIndex(const std::string& name) const;
        constexpr size_t            columnLength(size_t index) const            { if constexpr (RANGE_CHECKING) {return column_lengths.at(index);}  else { return column_lengths[index]; } }
        const std::string&          columnName(size_t index) const              { if constexpr (RANGE_CHECKING) {return column_names_.at(index);}   else { return column_names_[index]; } }
        constexpr size_t            columnOffset(size_t index) const            { if constexpr (RANGE_CHECKING) {return column_offsets.at(index);}  else { return column_offsets[index]; } }
        ColumnType                  columnType(size_t index) const              { return columnTypeT<0>(index); }
        template<size_t Index = 0>
        ColumnType                  columnTypeT(size_t index) const;
        template<size_t Index>
        static constexpr ColumnType columnType()                                { return toColumnType< column_type<Index> >(); }
        
        bool                        setColumnName(size_t index, const std::string& name);




        // Compatibility checking
        template<typename OtherLayout>
        requires requires(const OtherLayout& other) {
            { other.columnCount() } -> std::convertible_to<size_t>;
            { other.columnType(size_t{}) } -> std::convertible_to<ColumnType>;
        }
        bool isCompatibleWith(const OtherLayout& other) const;

        template<typename OtherLayout>
        requires requires(const OtherLayout& other) {
            { other.columnCount() } -> std::convertible_to<size_t>;
            { other.columnType(size_t{}) } -> std::convertible_to<ColumnType>;
        }
        LayoutStatic& operator=(const OtherLayout& other);
   };

} // namespace bcsv
