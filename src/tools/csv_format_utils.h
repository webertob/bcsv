/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file csv_format_utils.h
 * @brief Shared CSV formatting utilities for CLI tools (bcsvHead, bcsvTail, bcsv2csv)
 */

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <bcsv/bcsv.h>

namespace bcsv_cli {

/**
 * @brief Escapes a string value for CSV format
 * @param value The string to escape
 * @param delimiter The field delimiter character
 * @param quote_char The quote character to use
 * @param force_quote Whether to force quoting even if not needed
 * @return Properly escaped CSV field
 */
inline std::string escapeCsvField(const std::string& value, char delimiter, char quote_char, bool force_quote = false) {
    bool needs_quoting = force_quote || 
                        value.find(delimiter) != std::string::npos ||
                        value.find(quote_char) != std::string::npos ||
                        value.find('\n') != std::string::npos ||
                        value.find('\r') != std::string::npos ||
                        (!value.empty() && (value.front() == ' ' || value.back() == ' '));

    if (!needs_quoting) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.length() + 10);
    escaped += quote_char;
    
    for (char c : value) {
        if (c == quote_char) {
            escaped += quote_char;
            escaped += quote_char;
        } else {
            escaped += c;
        }
    }
    
    escaped += quote_char;
    return escaped;
}

/**
 * @brief Formats a numeric value with optimal precision
 * @param value The numeric value as string
 * @param precision Precision to use (-1 for auto)
 * @return Formatted numeric string
 */
inline std::string formatNumeric(const std::string& value, int precision) {
    if (precision == -1) {
        return value;
    }
    
    try {
        double num = std::stod(value);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << num;
        return oss.str();
    } catch (...) {
        return value;
    }
}

/**
 * @brief Gets a string representation of a cell value
 * @param row The row to read from
 * @param col_index The column index
 * @param column_type The type of the column
 * @param precision Floating point precision
 * @return String representation of the cell value
 */
inline std::string getCellValue(const bcsv::Row& row, size_t col_index, bcsv::ColumnType column_type, int precision) {
    try {
        switch (column_type) {
            case bcsv::ColumnType::BOOL:
                return row.get<bool>(col_index) ? "true" : "false";
            case bcsv::ColumnType::INT8:
                return std::to_string(row.get<int8_t>(col_index));
            case bcsv::ColumnType::UINT8:
                return std::to_string(row.get<uint8_t>(col_index));
            case bcsv::ColumnType::INT16:
                return std::to_string(row.get<int16_t>(col_index));
            case bcsv::ColumnType::UINT16:
                return std::to_string(row.get<uint16_t>(col_index));
            case bcsv::ColumnType::INT32:
                return std::to_string(row.get<int32_t>(col_index));
            case bcsv::ColumnType::UINT32:
                return std::to_string(row.get<uint32_t>(col_index));
            case bcsv::ColumnType::INT64:
                return std::to_string(row.get<int64_t>(col_index));
            case bcsv::ColumnType::UINT64:
                return std::to_string(row.get<uint64_t>(col_index));
            case bcsv::ColumnType::FLOAT:
                return formatNumeric(std::to_string(row.get<float>(col_index)), precision);
            case bcsv::ColumnType::DOUBLE:
                return formatNumeric(std::to_string(row.get<double>(col_index)), precision);
            case bcsv::ColumnType::STRING:
                return row.get<std::string>(col_index);
            default:
                return "";
        }
    } catch (const std::exception&) {
        return "";
    }
}

} // namespace bcsv_cli
