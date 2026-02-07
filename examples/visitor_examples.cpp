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
#include <iostream>
#include <sstream>
#include <iomanip>

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
    
    Row row(layout, true); // Enable change tracking
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
        
        std::cout << "\n✓ All examples completed successfully!\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
