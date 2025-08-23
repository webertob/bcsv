#pragma once

#include <string>
#include <vector>
#include <variant>
#include <map>

#include "definitions.h"
#include "column_layout.h"

namespace bcsv {

    // Type aliases for common data types
    using FieldValue = std::variant<std::string, int64_t, double, bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, float>;
    using FieldMap = std::map<std::string, FieldValue>;

    /**
     * @brief Represents a single row of data
     */
    class Row {
    public:
        Row() = default;
        explicit Row(const ColumnLayout& columnLayout);
        
        void setValue(const std::string& fieldName, const FieldValue& value);
        void setValue(size_t index, const FieldValue& value);
        
        FieldValue getValue(const std::string& fieldName) const;
        FieldValue getValue(size_t index) const;
        
        void setValues(const std::vector<FieldValue>& values) { values_ = values; }
        const std::vector<FieldValue>& getValues() const { return values_; }
        
        size_t getColumnCount() const { return values_.size(); }
        
        bool hasColumn(const std::string& columnName) const;

    private:
        std::vector<FieldValue> values_;
        const ColumnLayout* columnLayout_ = nullptr;
    };

} // namespace bcsv
