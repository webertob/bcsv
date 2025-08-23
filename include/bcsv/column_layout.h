#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include "definitions.h"

namespace bcsv {
    // Column data type enumeration (stored as uint16_t in file)
    enum class ColumnDataType : uint16_t {
        BOOL = 0x0001,
        UINT8 = 0x0002,
        UINT16 = 0x0003,
        UINT32 = 0x0004,
        UINT64 = 0x0005,
        INT8 = 0x0006,
        INT16 = 0x0007,
        INT32 = 0x0008,
        INT64 = 0x0009,
        FLOAT = 0x000A,
        DOUBLE = 0x000B,
        STRING = 0x000C
    };

    /**
     * @brief Represents the column layout containing column names and types
     */
    class ColumnLayout {
    public:
        ColumnLayout() = default;
        
        void addColumn(const std::string& name, ColumnDataType type = ColumnDataType::STRING);
        void addColumn(const std::string& name, const std::string& typeString);
        
        void setColumns(const std::vector<std::pair<std::string, ColumnDataType>>& columns);
        std::vector<std::pair<std::string, ColumnDataType>> getColumns() const;
        
        std::vector<std::string> getNames() const;
        std::vector<ColumnDataType> getDataTypes() const;
        std::vector<std::string> getDataTypesAsString() const;
        
        size_t getColumnCount() const { return column_names_.size(); }
        
        ColumnDataType getDataType(size_t index) const;
        std::string getDataTypeAsString(size_t index) const;
        
        size_t getIndex(const std::string& columnName) const;
        std::string getName(size_t index) const;
        
        // Column management
        void setName(size_t index, const std::string& name);
        void setDataType(size_t index, ColumnDataType type);
        
        void removeColumn(size_t index);
        void clear();
        
        bool hasColumn(const std::string& columnName) const;

    private:
        std::vector<std::string> column_names_;
        std::vector<ColumnDataType> column_types_;
        std::unordered_map<std::string, size_t> column_name_index_;
        
        // Helper functions for type conversion
        static ColumnDataType stringToDataType(const std::string& typeString);
        static std::string dataTypeToString(ColumnDataType type);
        
        // Helper function to update the index map
        void updateIndexMap();
    };

} // namespace bcsv
