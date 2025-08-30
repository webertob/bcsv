#pragma once

#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <memory>
#include <stdexcept>

#include "definitions.h"

namespace bcsv {

    struct ColumnDefinition {
        ColumnDefinition(const std::string& n, ColumnDataType t)
            : name(n), type(t) {}
        std::string name;
        ColumnDataType type;
    };
    
     /**
     * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
     * It is also the common interface for Layout (run-time flexible layout definition) and LayoutStatic (compile-time fixed layout definition).
     */
    class LayoutInterface {
    protected:
        std::vector<std::string> column_names_;
        std::unordered_map<std::string, size_t> column_index_;

    public:
        LayoutInterface() = default;
        virtual ~LayoutInterface() = default;

        virtual size_t getColumnCount() const { return column_names_.size(); }
        const std::unordered_map<std::string, size_t>& getColumnIndex() const { return column_index_; }
        size_t getColumnIndex(const std::string& columnName) const;
        const std::string& getColumnName(size_t index) const;
        void setColumnName(size_t index, const std::string& name);
        virtual ColumnDataType getColumnType(size_t index) const = 0;
        bool hasColumn(const std::string& columnName) const { return column_index_.find(columnName) != column_index_.end(); }
        bool isCompatibleWith(const LayoutInterface& other) const;
                
        /* Locking mechanism (need to ensure layout does not change during operations)*/
        virtual bool isLocked() const = 0;
        virtual void lock(void* owner) = 0;
        virtual void unlock(void* owner) = 0;

    protected:
        void updateIndexMap();
    };


    /**
     * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
     * This layout is flexible and can be modified at runtime.
     */
    class Layout : public LayoutInterface {
        std::vector<ColumnDataType> column_types_;
        std::vector<size_t> column_lengths_; // Lengths of each column in [bytes] --> serialized data
        std::vector<size_t> column_offsets_; // Offsets of each column in [bytes] --> serialized data
        std::set<void*> lock_owners_;        // ptrs to objects that have locked the layout, used to identify owners

    public:
        typedef LayoutInterface Base;

        Layout() = default;
        Layout(const Layout& other);
        explicit Layout(const std::vector<ColumnDefinition>& columns);
        ~Layout() = default;

        void clear();
        
        void insertColumn(const ColumnDefinition& column, size_t position = SIZE_MAX);
        void removeColumn(size_t index);

        ColumnDataType getColumnType(size_t index) const override;
        void setColumnType(size_t index, ColumnDataType type);
        void setColumns(const std::vector<ColumnDefinition>& columns);

        size_t getColumnOffset(size_t index) const;
        size_t getColumnLength(size_t index) const;

        /* Locking mechanism (need to ensure layout does not change during operations)*/
        bool isLocked() const override { return !lock_owners_.empty(); }
        void lock(void* owner) override { lock_owners_.insert(owner); }
        void unlock(void* owner) override { lock_owners_.erase(owner); }

        Layout& operator=(const Layout& other);

    public:
        // Factory functions that return shared pointers
        static std::shared_ptr<Layout> create() {
            return std::make_shared<Layout>();
        }
        
        static std::shared_ptr<Layout> create(const std::vector<ColumnDefinition>& columns) {
            return std::make_shared<Layout>(columns);
        }
    };




    /**
     * @brief Static layout definition for BCSV files.
     * 
     * This layout is defined at compile-time to improve performance and reduce runtime overhead.
     */
    template<typename... ColumnTypes>
    class LayoutStatic : public LayoutInterface {
    public:
        using column_types = std::tuple<ColumnTypes...>;

        template<size_t Index>
        using column_type = typename std::tuple_element_t<Index, std::tuple<ColumnTypes...>>;

        // Lengths of each column in [bytes] --> serialized data
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> getColumnLengths() {
            std::array<size_t, sizeof...(ColumnTypes)> lengths{};
            size_t index = 0;
            ((lengths[index++] = binaryFieldLength<ColumnTypes>()), ...);
            return lengths;
        }

        // Offsets of each column in [bytes] --> serialized data
        static constexpr std::array<size_t, sizeof...(ColumnTypes)> getColumnOffsets() {
            std::array<size_t, sizeof...(ColumnTypes)> offsets{};
            size_t offset = 0;
            size_t index = 0;
            ((offsets[index++] = offset, offset += binaryFieldLength<ColumnTypes>()), ...);
            return offsets;
        }

        static constexpr size_t FIXED_SIZE = (binaryFieldLength<ColumnTypes>() + ... + 0);
        static constexpr auto column_lengths = getColumnLengths();
        static constexpr auto column_offsets = getColumnOffsets();

        LayoutStatic();
        explicit LayoutStatic(const std::vector<std::string>& columnNames);

        static constexpr size_t getColumnCount() const override { return sizeof...(ColumnTypes); }
        ColumnDataType getColumnType(size_t index) const override { return getColumnType<0>(index);}

        template<size_t Index>
        static constexpr ColumnDataType getColumnType() const { return toColumnDataType< column_type<Index> >(); }

        /* Locking mechanism (static version is always locked)*/
        constexpr bool isLocked() const override { return true; }
        void lock(void* owner) override { /* stub - static layouts cannot be modified */ }
        void unlock(void* owner) override { /* stub - static layouts cannot be modified */ }

    private:
        template<size_t Index = 0>
        constexpr ColumnDataType getColumnType(size_t index) const;

    public:
        // Factory functions that return shared pointers
        static std::shared_ptr<LayoutStatic<ColumnTypes...>> create() {
            return std::make_shared<LayoutStatic<ColumnTypes...>>();
        }
        
        static std::shared_ptr<LayoutStatic<ColumnTypes...>> create(const std::vector<std::string>& columnNames) {
            return std::make_shared<LayoutStatic<ColumnTypes...>>(columnNames);
        }
   };

} // namespace bcsv
