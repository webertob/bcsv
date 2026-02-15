/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file visitor_examples.cpp
 * @brief Comprehensive examples of BCSV visitor pattern usage
 * 
 * Demonstrates:
 * - Basic visitor patterns
 * - Fine-grained change tracking
 * - Helper types from row_visitors.h
 * - Type-specific processing
 * - Compile-time optimization with RowStatic
 */

#include <bcsv/bcsv.h>
#include <bcsv/row_visitors.h>
#include <bcsv/row_codec_flat001.h>
#include <bcsv/row_codec_flat001.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>

using namespace bcsv;

// ============================================================================
// Example 1: Basic Read-Only Visitor - CSV Output
// ============================================================================

void example_csv_output() {
    std::cout << "\n=== Example 1: CSV Output ===\n";
    
    Layout layout({
        {"name", ColumnType::STRING},
        {"age", ColumnType::INT32},
        {"salary", ColumnType::DOUBLE},
        {"active", ColumnType::BOOL}
    });
    
    Row row(layout);
    row.set(0, std::string("Alice"));
    row.set(1, int32_t(30));
    row.set(2, 75000.0);
    row.set(3, true);
    
    // Simple lambda visitor
    std::ostringstream csv;
    row.visit([&](size_t index, const auto& value) {
        if (index > 0) csv << ",";
        
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
            csv << (value ? "true" : "false");
        } else {
            csv << value;
        }
    });
    
    std::cout << "CSV: " << csv.str() << "\n";
    
    // Using helper type
    std::ostringstream csv2;
    row.visit(visitors::csv_visitor{csv2});
    std::cout << "CSV (helper): " << csv2.str() << "\n";
}

// ============================================================================
// Example 2: Statistics Computation
// ============================================================================

void example_statistics() {
    std::cout << "\n=== Example 2: Statistics ===\n";
    
    Layout layout({
        {"temp1", ColumnType::DOUBLE},
        {"temp2", ColumnType::FLOAT},
        {"pressure", ColumnType::INT32},
        {"timestamp", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, 23.5);
    row.set(1, 24.2f);
    row.set(2, int32_t(1013));
    row.set(3, std::string("2025-02-07T10:30:00Z"));
    
    // Manual statistics
    visitors::stats_visitor stats;
    row.visit(stats);
    
    std::cout << "Statistics:\n"
              << "  Count: " << stats.count << "\n"
              << "  Min:   " << stats.min << "\n"
              << "  Max:   " << stats.max << "\n"
              << "  Mean:  " << stats.mean() << "\n"
              << "  Sum:   " << stats.sum << "\n";
}

// ============================================================================
// Example 3: Fine-Grained Change Tracking
// ============================================================================

void example_change_tracking() {
    std::cout << "\n=== Example 3: Fine-Grained Change Tracking ===\n";
    
    Layout layout({
        {"value1", ColumnType::DOUBLE},
        {"value2", ColumnType::INT32},
        {"name", ColumnType::STRING}
    });
    
    RowTracked<TrackingPolicy::Enabled> row(layout);
    row.set(0, 10.0);
    row.set(1, int32_t(20));
    row.set(2, std::string("test"));
    
    row.resetChanges();  // Clear change tracking
    
    // Visitor with fine-grained tracking
    row.visit([](size_t index, auto& value, bool& changed) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            auto old = value;
            value *= 2;  // Double all numeric values
            changed = (value != old);
            
            std::cout << "Column " << index << ": " << old << " -> " << value 
                      << " (changed: " << (changed ? "yes" : "no") << ")\n";
        } else {
            // Don't modify strings
            changed = false;
            std::cout << "Column " << index << ": skipped (string)\n";
        }
    });
    
    std::cout << "Has changes: " << (row.hasAnyChanges() ? "yes" : "no") << "\n";
}

// ============================================================================
// Example 4: Type-Specific Processing with Overload
// ============================================================================

void example_type_specific() {
    std::cout << "\n=== Example 4: Type-Specific Processing ===\n";
    
    Layout layout({
        {"id", ColumnType::INT32},
        {"name", ColumnType::STRING},
        {"value", ColumnType::DOUBLE},
        {"enabled", ColumnType::BOOL}
    });
    
    Row row(layout);
    row.set(0, int32_t(12345));
    row.set(1, std::string("Item-A"));
    row.set(2, 99.99);
    row.set(3, true);
    
    // Type-specific processing using if constexpr
    row.visit([](size_t index, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        std::cout << "  [" << index << "] ";
        
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            std::cout << "Integer: " << value << " (hex: 0x" 
                      << std::hex << value << std::dec << ")\n";
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::cout << "String: \"" << value << "\" (length: " 
                      << value.length() << ")\n";
        } else if constexpr (std::is_floating_point_v<T>) {
            std::cout << "Double: " << std::fixed 
                      << std::setprecision(2) << value << "\n";
        } else if constexpr (std::is_same_v<T, bool>) {
            std::cout << "Bool: " << (value ? "true" : "false") << "\n";
        }
    });
}

// ============================================================================
// Example 5: Compile-Time Optimization with RowStatic
// ============================================================================

void example_static_visitor() {
    std::cout << "\n=== Example 5: Compile-Time Optimization (RowStatic) ===\n";
    
    using LayoutType = LayoutStatic<int32_t, std::string, double>;
    LayoutType layout({"id", "name", "value"});
    
    RowStatic<int32_t, std::string, double> row(layout);
    row.set<0>(42);
    row.set<1>(std::string("static-row"));
    row.set<2>(3.14159);
    
    // Note: index is runtime value (even though known at compile-time internally)
    row.visit([](size_t index, const auto& value) {
        std::cout << "  Column[" << index << "]: ";
        
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int>) {
            std::cout << "ID = " << value;
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::cout << "Name = \"" << value << "\"";
        } else if constexpr (std::is_same_v<T, double>) {
            std::cout << "Value = " << value;
        }
        
        std::cout << " (runtime index = " << index << ")\n";
    });
}

// ============================================================================
// Example 6: Conditional Processing
// ============================================================================

void example_conditional() {
    std::cout << "\n=== Example 6: Conditional Processing ===\n";
    
    Layout layout({
        {"temperature", ColumnType::DOUBLE},
        {"humidity", ColumnType::DOUBLE},
        {"status", ColumnType::STRING}
    });
    
    Row row(layout);
    row.set(0, 35.5);   // High temperature
    row.set(1, 20.0);   // Low humidity
    row.set(2, std::string("warning"));
    
    // Check for anomalies
    std::vector<std::string> warnings;
    
    row.visit([&](size_t index, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, double>) {
            if (index == 0 && value > 30.0) {
                warnings.push_back("High temperature: " + std::to_string(value));
            } else if (index == 1 && value < 30.0) {
                warnings.push_back("Low humidity: " + std::to_string(value));
            }
        }
    });
    
    std::cout << "Warnings found: " << warnings.size() << "\n";
    for (const auto& w : warnings) {
        std::cout << "  - " << w << "\n";
    }
}

// ============================================================================
// Example 7: Data Validation
// ============================================================================

void example_validation() {
    std::cout << "\n=== Example 7: Data Validation ===\n";
    
    Layout layout({
        {"age", ColumnType::INT32},
        {"email", ColumnType::STRING},
        {"score", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, int32_t(-5));     // Invalid: negative age
    row.set(1, std::string(""));  // Invalid: empty email
    row.set(2, 150.0);            // Invalid: score > 100
    
    // Validate all columns
    std::vector<std::string> errors;
    
    row.visit([&](size_t index, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        
        if constexpr (std::is_same_v<T, int32_t>) {
            if (index == 0 && value < 0) {
                errors.push_back("Invalid age: " + std::to_string(value));
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (index == 1 && value.empty()) {
                errors.push_back("Email cannot be empty");
            }
        } else if constexpr (std::is_same_v<T, double>) {
            if (index == 2 && (value < 0.0 || value > 100.0)) {
                errors.push_back("Score out of range: " + std::to_string(value));
            }
        }
    });
    
    std::cout << "Validation errors: " << errors.size() << "\n";
    for (const auto& e : errors) {
        std::cout << "  - " << e << "\n";
    }
}

// ============================================================================
// Example 8: JSON-like Output
// ============================================================================

void example_json_output() {
    std::cout << "\n=== Example 8: JSON-like Output ===\n";
    
    Layout layout({
        {"id", ColumnType::INT32},
        {"name", ColumnType::STRING},
        {"active", ColumnType::BOOL},
        {"score", ColumnType::DOUBLE}
    });
    
    Row row(layout);
    row.set(0, int32_t(1001));
    row.set(1, std::string("Alice"));
    row.set(2, true);
    row.set(3, 95.5);
    
    std::ostringstream json;
    json << "{";
    
    row.visit([&, first = true](size_t index, const auto& value) mutable {
        if (!first) json << ", ";
        first = false;
        
        // Get column name
        const auto& colName = layout.columnName(index);
        json << "\"" << colName << "\": ";
        
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) {
            json << "\"" << value << "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
            json << (value ? "true" : "false");
        } else {
            json << value;
        }
    });
    
    json << "}";
    std::cout << json.str() << "\n";
}

// ============================================================================
// Example 9: Typed visit<T>() — Compile-Time Dispatch
// ============================================================================

void example_typed_visit() {
    std::cout << "\n=== Example 9: Typed visit<T>() — Compile-Time Dispatch ===\n";

    // Layout with 5 consecutive double columns (homogeneous block)
    Layout layout({
        {"temp_1", ColumnType::DOUBLE},
        {"temp_2", ColumnType::DOUBLE},
        {"temp_3", ColumnType::DOUBLE},
        {"temp_4", ColumnType::DOUBLE},
        {"temp_5", ColumnType::DOUBLE}
    });

    Row row(layout);
    // Set initial temperatures
    for (size_t i = 0; i < 5; ++i)
        row.set<double>(i, 20.0 + i * 0.5);

    // Read all temperatures with visitConst<T>() — no runtime type switch
    double sum = 0;
    double minVal = std::numeric_limits<double>::max();
    double maxVal = std::numeric_limits<double>::lowest();

    row.visitConst<double>(0, [&](size_t col, const double& temp) {
        sum += temp;
        minVal = std::min(minVal, temp);
        maxVal = std::max(maxVal, temp);
        std::cout << "  " << layout.columnName(col) << " = " << temp << " °C\n";
    }, 5);

    std::cout << "  Mean: " << (sum / 5.0) << " °C, Range: [" << minVal << ", " << maxVal << "]\n";

    // Scale all temperatures with visit<T>() and change tracking
    row.visit<double>(0, [](size_t, double& temp, bool& changed) {
        temp = temp * 1.8 + 32.0;  // Convert to Fahrenheit
        changed = true;
    }, 5);

    std::cout << "  After C→F conversion:\n";
    row.visitConst<double>(0, [&](size_t col, const double& temp) {
        std::cout << "    " << layout.columnName(col) << " = " << temp << " °F\n";
    }, 5);
}

// ============================================================================
// Example 10: Typed visit<T>() — 2-param Visitor (auto-tracks changes)
// ============================================================================

void example_typed_visit_2param() {
    std::cout << "\n=== Example 10: Typed visit<T>() — 2-Param Visitor ===\n";

    Layout layout({
        {"x", ColumnType::INT32},
        {"y", ColumnType::INT32},
        {"z", ColumnType::INT32}
    });

    RowTracking row(layout);
    row.set<int32_t>(0, 10);
    row.set<int32_t>(1, 20);
    row.set<int32_t>(2, 30);
    row.changes().reset();  // Clear change flags

    // 2-param visitor: all visited columns automatically marked changed
    row.visit<int32_t>(0, [](size_t, int32_t& val) {
        val += 100;
    }, 3);

    std::cout << "  After adding 100 to all columns:\n";
    row.visitConst<int32_t>(0, [&](size_t col, const int32_t& val) {
        std::cout << "    " << layout.columnName(col) << " = " << val
                  << " (changed: " << row.changes()[col] << ")\n";
    }, 3);
}

// ============================================================================
// Example 11: RowView visit<T>() — Zero-Copy Buffer Access
// ============================================================================

void example_rowview_typed_visit() {
    std::cout << "\n=== Example 11: RowView visit<T>() — Zero-Copy Buffer Access ===\n";

    Layout layout({
        {"ch0", ColumnType::DOUBLE},
        {"ch1", ColumnType::DOUBLE},
        {"ch2", ColumnType::DOUBLE},
        {"name", ColumnType::STRING}
    });

    // Create and populate a Row, then serialize
    Row row(layout);
    row.set(0, 100.0);
    row.set(1, 200.0);
    row.set(2, 300.0);
    row.set(3, std::string("sensor_A"));

    ByteBuffer buf;
    RowCodecFlat001<Layout> codec;
    codec.setup(layout);
    auto serialized = codec.serialize(row, buf);

    // Create a zero-copy RowView into the serialized buffer
    RowView rv(layout, std::span<std::byte>(serialized));

    // Read doubles with visitConst<T>() — no runtime switch, zero-copy
    double sum = 0;
    rv.visitConst<double>(0, [&](size_t col, const double& val) {
        std::cout << "  " << layout.columnName(col) << " = " << val << "\n";
        sum += val;
    }, 3);
    std::cout << "  Sum: " << sum << "\n";

    // Read string with visitConst<string_view>() — zero-copy into buffer
    rv.visitConst<std::string_view>(3, [&](size_t col, const std::string_view& sv) {
        std::cout << "  " << layout.columnName(col) << " = \"" << sv << "\"\n";
    }, 1);

    // Modify doubles in-place (directly in the serialized buffer)
    rv.visit<double>(0, [](size_t, double& val) {
        val *= 2.0;
    }, 3);

    std::cout << "  After 2x scaling:\n";
    rv.visitConst<double>(0, [&](size_t col, const double& val) {
        std::cout << "    " << layout.columnName(col) << " = " << val << "\n";
    }, 3);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "BCSV Visitor Pattern Examples\n";
    std::cout << "==============================\n";
    
    try {
        example_csv_output();
        example_statistics();
        example_change_tracking();
        example_type_specific();
        example_static_visitor();
        example_conditional();
        example_validation();
        example_json_output();
        example_typed_visit();
        example_typed_visit_2param();
        example_rowview_typed_visit();
        
        std::cout << "\n✓ All examples completed successfully!\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
