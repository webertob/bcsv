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

    // Helper to convert C++ types to ColumnDataType at compile time
    template<typename T>
    static constexpr ColumnDataType getColumnDataType() {
        if constexpr (std::is_same_v<T, std::string>) {
            return ColumnDataType::STRING;
        } else if constexpr (std::is_same_v<T, int8_t>) {
            return ColumnDataType::INT8;
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return ColumnDataType::INT16;
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return ColumnDataType::INT32;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return ColumnDataType::INT64;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            return ColumnDataType::UINT8;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            return ColumnDataType::UINT16;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            return ColumnDataType::UINT32;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return ColumnDataType::UINT64;
        } else if constexpr (std::is_same_v<T, float>) {
            return ColumnDataType::FLOAT;
        } else if constexpr (std::is_same_v<T, double>) {
            return ColumnDataType::DOUBLE;
        } else if constexpr (std::is_same_v<T, bool>) {
            return ColumnDataType::BOOL;
        } else {
            static_assert(sizeof(T) == 0, "Unsupported column type for LayoutStatic");
        }
    }
    
     /**
     * @brief Represents the column layout containing column names and types. This defines the common layout for BCSV files.
     * It is also the common interface for Layout (run-time flexible layout definition) and LayoutStatic (compile-time fixed layout definition).
     */
    class LayoutInterface {
    protected:
        std::vector<std::string> column_names_;
        std::unordered_map<std::string, size_t> column_name_index_;

    public:
        LayoutInterface() = default;
        virtual ~LayoutInterface() = default;

        virtual size_t getColumnCount() const { return column_names_.size(); }
        const std::unordered_map<std::string, size_t>& getColumnIndex() const { return column_name_index_; }
        size_t getColumnIndex(const std::string& columnName) const;
        std::string getColumnName(size_t index) const;
        const std::vector<std::string>& getColumnNames() const { return column_names_; };
        virtual ColumnDataType getColumnType(size_t index) const = 0;
        std::string getColumnTypeAsString(size_t index) const;
        std::vector<std::string> getColumnTypesAsString() const;
        bool hasColumn(const std::string& columnName) const { return column_name_index_.find(columnName) != column_name_index_.end(); }
        bool isCompatibleWith(const LayoutInterface& other) const;
        void setColumnName(size_t index, const std::string& name);
        
        /* Locking mechanism (need to ensure layout does not change during operations)*/
        virtual bool isLocked() const = 0;
        virtual void lock(void* owner) = 0;
        virtual void unlock(void* owner) = 0;

    protected:
        // Helper functions
        static ColumnDataType stringToDataType(const std::string& typeString);
        static std::string dataTypeToString(ColumnDataType type);
        void updateIndexMap();
        
    };

    /* Flexible layout definition for BCSV files. Defined at run-time to allow for dynamic changes to the layout. */
    class Layout : public LayoutInterface {
        std::vector<ColumnDataType> column_types_;
        std::set<void*> lock_owners_;

    public:
        typedef LayoutInterface Base;

        Layout() = default;
        explicit Layout(const std::vector<ColumnDefinition>& columns);

        void clear();
        ColumnDataType getColumnType(size_t index) const override;
        const std::vector<ColumnDataType>& getColumnTypes() const { return column_types_; }
        void insertColumn(const std::string& name, ColumnDataType type = ColumnDataType::STRING, size_t position = SIZE_MAX);
        void insertColumn(const std::string& name, const std::string& typeString, size_t position = SIZE_MAX);
        void setColumnDataType(size_t index, ColumnDataType type);
        void setColumns(const std::vector<ColumnDefinition>& columns);
        void setColumns(const std::vector<std::string>& names, const std::vector<ColumnDataType>& types);
        void removeColumn(size_t index);

        /* Locking mechanism (need to ensure layout does not change during operations)*/
        bool isLocked() const override { return !lock_owners_.empty(); }
        void lock(void* owner) override { lock_owners_.insert(owner); }
        void unlock(void* owner) override { lock_owners_.erase(owner); }

    public:
        // Factory functions that return shared pointers
        static std::shared_ptr<Layout> create() {
            return std::make_shared<Layout>();
        }
        
        static std::shared_ptr<Layout> create(const std::vector<ColumnDefinition>& columns) {
            return std::make_shared<Layout>(columns);
        }
    };

    /*
    * Static layout definition for BCSV files. Defined at compile-time to improve performance and reduce runtime overhead.
    */
    template<typename... ColumnTypes>
    class LayoutStatic : public LayoutInterface {
        std::tuple<ColumnTypes...> column_types_;

    public:
        LayoutStatic() = default;
        explicit LayoutStatic(const std::vector<std::string>& columnNames);

        constexpr size_t getColumnCount() const override { return sizeof...(ColumnTypes); }
        ColumnDataType getColumnType(size_t index) const override;

        /* Locking mechanism (static version is always locked)*/
        bool isLocked() const override { return true; }
        void lock(void* owner) override { /* stub - static layouts cannot be modified */ }
        void unlock(void* owner) override { /* stub - static layouts cannot be modified */ }

    private:
        template<size_t Index = 0>
        constexpr ColumnDataType getTypeAtIndex(size_t targetIndex) const;
    
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
