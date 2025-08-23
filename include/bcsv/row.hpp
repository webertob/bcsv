#pragma once

/**
 * @file row.hpp
 * @brief Binary CSV (BCSV) Library - Row implementations
 * 
 * This file contains the implementations for the Row class.
 */

#include "row.h"
#include "column_layout.h"

namespace bcsv {

    inline Row::Row(const ColumnLayout& columnLayout) : columnLayout_(&columnLayout) {
        values_.resize(columnLayout.getColumnCount());
    }
    
    inline void Row::setValue(const std::string& fieldName, const FieldValue& value) {
        if (columnLayout_) {
            size_t index = columnLayout_->getIndex(fieldName);
            if (index != SIZE_MAX && index < values_.size()) {
                values_[index] = value;
            }
        }
    }
    
    inline void Row::setValue(size_t index, const FieldValue& value) {
        if (index < values_.size()) {
            values_[index] = value;
        }
    }
    
    inline FieldValue Row::getValue(const std::string& fieldName) const {
        if (columnLayout_) {
            size_t index = columnLayout_->getIndex(fieldName);
            if (index != SIZE_MAX && index < values_.size()) {
                return values_[index];
            }
        }
        return std::string("");
    }
    
    inline FieldValue Row::getValue(size_t index) const {
        if (index < values_.size()) {
            return values_[index];
        }
        return std::string("");
    }
    
    inline bool Row::hasColumn(const std::string& columnName) const {
        if (columnLayout_) {
            return columnLayout_->hasColumn(columnName);
        }
        return false;
    }

} // namespace bcsv