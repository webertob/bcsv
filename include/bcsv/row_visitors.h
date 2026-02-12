/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file row_visitors.h
 * @brief Concepts and helpers for BCSV row visitor pattern
 * 
 * This header provides:
 * - C++20 concepts defining row visitor requirements
 * - Helper types and examples for common row visitor patterns
 * 
 * @section visitor_examples Visitor Pattern Examples
 * 
 * @subsection basic_visitors Basic Visitors
 * 
 * Read-only visitor (2 parameters):
 * @code
 * row.visit([](size_t index, const auto& value) {
 *     std::cout << "Column " << index << " = " << value << "\n";
 * });
 * @endcode
 * 
 * Mutable visitor with fine-grained change tracking (3 parameters):
 * @code
 * row.visit([](size_t index, auto& value, bool& changed) {
 *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
 *         value *= 2;
 *         changed = true;  // Mark this column as modified
 *     } else {
 *         changed = false; // Don't mark strings as changed
 *     }
 * });
 * @endcode
 * 
 * Legacy mutable visitor (2 parameters, all columns marked changed):
 * @code
 * row.visit([](size_t index, auto& value) {
 *     if constexpr (std::is_arithmetic_v<decltype(value)>) {
 *         value *= 2;
 *     }
 * }); // All visited columns automatically marked as changed
 * @endcode
 * 
 * @subsection typed_visitors Type-Specific Visitors
 * 
 * Handle only specific types:
 * @code
 * row.visit([](size_t index, const auto& value) {
 *     using T = std::decay_t<decltype(value)>;
 *     if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
 *         double d = static_cast<double>(value);
 *         // Process numeric value...
 *     } else if constexpr (std::is_same_v<T, std::string>) {
 *         // Process string...
 *     }
 *     // Ignore bool or other types
 * });
 * @endcode
 * 
 * @subsection compile_time_typed_visitors Compile-Time Typed Visitors (visit<T>)
 * 
 * When columns share a known type, visit<T>() eliminates the runtime type switch:
 * @code
 * // 2x faster than visit() for homogeneous column ranges
 * row.visit<double>(0, [](size_t col, double& v, bool& changed) {
 *     v *= 2.0;
 *     changed = true;
 * }, 100);  // Process 100 consecutive double columns
 * 
 * // Read-only variant
 * double sum = 0;
 * row.visitConst<int32_t>(0, [&](size_t, const int32_t& v) {
 *     sum += v;
 * }, 50);
 * @endcode
 *
 * visit<T>() and visitConst<T>() are available on both RowImpl and RowView.
 * On RowView, scalars are accessed directly in the serialized buffer
 * and strings are returned as zero-copy std::string_view references:
 * @code
 * bcsv::RowView view(buffer.data(), buffer.size(), layout);
 * view.visitConst<std::string_view>(stringCol, [](size_t, std::string_view sv) {
 *     std::cout << sv << "\n";  // zero-copy into buffer
 * });
 * @endcode
 * 
 * @subsection helper_visitors Using Helper Types
 * 
 * Use overload helper for different types:
 * @code
 * using bcsv::visitors::overload;
 * 
 * row.visit(overload{
 *     [](size_t, const std::string& s) { std::cout << "String: " << s << "\n"; },
 *     [](size_t, int32_t i) { std::cout << "Int: " << i << "\n"; },
 *     [](size_t, auto) { } // Catch-all for other types
 * });
 * @endcode
 */

#pragma once

#include <concepts>
#include <type_traits>

namespace bcsv {

// ============================================================================
// Row Visitor Concepts (C++20)
// ============================================================================

/**
 * @brief Concept for read-only row visitors that accept (index, const value&)
 * 
 * A RowReadOnlyVisitor must be invocable with:
 * - size_t index (column index)
 * - const T& value (column value of any supported type)
 * 
 * @tparam Visitor The visitor callable type
 * @tparam T The column value type
 */
template<typename Visitor, typename T>
concept RowReadOnlyVisitor = std::invocable<Visitor, size_t, const T&>;

/**
 * @brief Concept for mutable row visitors that accept (index, value&)
 * 
 * A RowMutableVisitor must be invocable with:
 * - size_t index (column index)
 * - T& value (mutable reference to column value)
 * 
 * @tparam Visitor The visitor callable type
 * @tparam T The column value type
 */
template<typename Visitor, typename T>
concept RowMutableVisitor = std::invocable<Visitor, size_t, T&>;

/**
 * @brief Concept for mutable row visitors with fine-grained change tracking
 * 
 * A RowMutableVisitorWithTracking must be invocable with:
 * - size_t index (column index)
 * - T& value (mutable reference to column value)
 * - bool& changed (output parameter: set to true if column was modified)
 * 
 * This allows the visitor to opt-out of marking columns as changed when
 * no actual modification occurred, optimizing ZoH compression.
 * 
 * @tparam Visitor The visitor callable type
 * @tparam T The column value type
 */
template<typename Visitor, typename T>
concept RowMutableVisitorWithTracking = std::invocable<Visitor, size_t, T&, bool&>;

/**
 * @brief Concept for compile-time visitors used with RowStatic
 * 
 * A RowStaticReadOnlyVisitor must be invocable with:
 * - std::integral_constant<size_t, Index> (compile-time index) OR size_t
 * - const T& or T& value (depending on const-ness)
 * 
 * @tparam Visitor The visitor callable type
 * @tparam Index The column index as a compile-time constant
 * @tparam T The column value type
 */
template<typename Visitor, size_t Index, typename T>
concept RowStaticReadOnlyVisitor = 
    std::invocable<Visitor, std::integral_constant<size_t, Index>, const T&> ||
    std::invocable<Visitor, size_t, const T&>;

// ============================================================================
// Typed Row Visitor Concepts  (C++20)
// ============================================================================

/**
 * @brief Concept for typed mutable visitors used with visit<T>()
 * 
 * A TypedRowVisitor must be invocable with a SPECIFIC type T:
 * - (size_t index, T& value, bool& changed)   — with change tracking control
 * - (size_t index, T& value)                   — all visited columns marked changed
 * 
 * Unlike the generic visit() which dispatches to the visitor with auto& (each
 * column's actual type), visit<T>() calls the visitor with a concrete T& for
 * all columns in the range. This enables:
 * - Compile-time dispatch (no runtime ColumnType switch) → ~2x faster
 * - Concrete lambda signatures (no if-constexpr chains needed)
 * - Type-safe bulk operations on homogeneous column ranges
 * 
 * @note Defined in row.h since it is required by RowImpl's visit<T>() declaration.
 *       Documented here for reference.
 * 
 * @code
 * // Scale 100 consecutive double columns by 2x
 * row.visit<double>(0, [](size_t, double& v, bool& changed) {
 *     v *= 2.0;
 *     changed = true;
 * }, 100);
 * @endcode
 * 
 * @tparam V The visitor callable type
 * @tparam T The expected column type (int32_t, double, std::string, bool, etc.)
 * 
 * @see row.h for concept definition
 * @see TypedRowVisitorConst for read-only variant
 */
// concept TypedRowVisitor — defined in row.h

/**
 * @brief Concept for typed read-only visitors used with visitConst<T>()
 * 
 * A TypedRowVisitorConst must be invocable with:
 * - (size_t index, const T& value)
 * 
 * Read-only counterpart of TypedRowVisitor. Zero-copy for scalars and strings.
 * 
 * @code
 * double sum = 0;
 * row.visitConst<double>(10, [&](size_t, const double& v) {
 *     sum += v;
 * }, 50);  // Sum columns 10..59
 * @endcode
 * 
 * @tparam V The visitor callable type
 * @tparam T The expected column type
 * 
 * @see row.h for concept definition
 * @see TypedRowVisitor for mutable variant
 */
// concept TypedRowVisitorConst — defined in row.h

// ============================================================================
// Visitor Helper Types
// ============================================================================

namespace visitors {

/**
 * @brief Overload pattern helper for variant-like visitation
 * 
 * Allows combining multiple lambdas to handle different types:
 * 
 * @code
 * row.visit(overload{
 *     [](size_t, const std::string& s) { std::cout << s; },
 *     [](size_t, int32_t i) { std::cout << i; },
 *     [](size_t, auto) { } // Catch-all for other types
 * });
 * @endcode
 * 
 * This is the same pattern as std::visit helper from cppreference.
 */
template<class... Ts>
struct overload : Ts... { 
    using Ts::operator()...; 
};

// Deduction guide for C++17 compatibility
template<class... Ts>
overload(Ts...) -> overload<Ts...>;

/**
 * @brief CSV serialization visitor example
 * 
 * Usage:
 * @code
 * std::ostringstream csv;
 * row.visit(csv_visitor{csv});
 * std::cout << csv.str(); // "value1,value2,value3"
 * @endcode
 */
struct csv_visitor {
    std::ostream& out;
    bool first = true;
    
    void operator()(size_t, const auto& value) {
        if (!first) out << ",";
        first = false;
        
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
            out << (value ? "true" : "false");
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Escape quotes for CSV format
            out << '"';
            for (char c : value) {
                if (c == '"') out << "\"\"";
                else out << c;
            }
            out << '"';
        } else {
            out << value;
        }
    }
};

/**
 * @brief Statistics accumulator visitor example
 * 
 * Computes min/max/sum/count for numeric columns:
 * 
 * @code
 * stats_visitor stats;
 * row.visit(stats);
 * std::cout << "Min: " << stats.min << ", Max: " << stats.max << "\n";
 * @endcode
 */
struct stats_visitor {
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    size_t count = 0;
    
    void operator()(size_t, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
            double d = static_cast<double>(value);
            min = std::min(min, d);
            max = std::max(max, d);
            sum += d;
            count++;
        }
    }
    
    double mean() const { return count > 0 ? sum / count : 0.0; }
};

/**
 * @brief Type filter visitor that only processes specific types
 * 
 * @code
 * // Only process strings
 * row.visit(type_filter<std::string>([](size_t index, const std::string& s) {
 *     std::cout << "String at " << index << ": " << s << "\n";
 * }));
 * @endcode
 */
template<typename TargetType, typename Func>
struct type_filter {
    Func func;
    
    void operator()(size_t index, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, TargetType>) {
            func(index, value);
        }
    }
};

/**
 * @brief Conditional visitor that applies predicate before processing
 * 
 * @code
 * // Only process non-empty strings
 * row.visit(conditional(
 *     [](const auto& v) { return !v.empty(); },
 *     [](size_t, const std::string& s) { std::cout << s << "\n"; }
 * ));
 * @endcode
 */
template<typename Predicate, typename Action>
struct conditional {
    Predicate pred;
    Action action;
    
    void operator()(size_t index, const auto& value) {
        if (pred(value)) {
            action(index, value);
        }
    }
};

} // namespace visitors
} // namespace bcsv
