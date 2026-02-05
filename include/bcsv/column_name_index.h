#pragma once

/**
 * @file column_name_index.h
 * @brief Binary CSV (BCSV) Library - Column Name Index utilities
 * 
 * This file contains utilities for managing column name indices, providing
 * a unified interface for both dynamic (std::vector) and fixed-size (std::array) storage.
 */


#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "definitions.h"

namespace bcsv {

    /**
     * @brief Index for column names implementing a flat_map strategy.
     * 
     * This class functions effectively as a "flat map". Instead of using node-based storage 
     * like std::map, it stores key-value pairs (Column Name -> Index) in a sorted sequential 
     * container (std::vector or std::array).
     * 
     * This approach optimizes for cache locality and memory compactness, which is efficient 
     * for the typical number of columns in a CSV file. Lookups utilize binary search O(log n)
     * over contiguous memory.
     * 
     * @tparam Capacity 0 for dynamic (std::vector), >0 for static (std::array)
     */
    template<size_t Capacity = 0>
    class ColumnNameIndex {
    public:
        // PREFERRED: static constexpr bool allows cleaner syntax: if constexpr (IS_FIXED_SIZE)
        static constexpr bool IS_FIXED_SIZE = (Capacity > 0);

        /// Key-Value pair type: {Column Name, Column Index}
        using Entry = std::pair<std::string, size_t>;
        
        /// Underlying storage type, switched based on Capacity.
        template<typename T>
        using ContainerType = std::conditional_t<
            IS_FIXED_SIZE,
            std::array<T, Capacity>,
            std::vector<T>
        >;

        using Container = ContainerType<Entry>;
        using Iterator = typename Container::iterator;
        using ConstIterator = typename Container::const_iterator;
        
        /**
         * @brief Generates an Excel-style default column name from an index (e.g., 0->A, 26->AA).
         * @param index The zero-based column index.
         * @param[out] name The string ref where the generated name will be stored.
         */
        static void defaultName(size_t index, std::string& name) {
            size_t len = 1;
            for (size_t range = 26; index >= range; range *= 26) {
                index -= range;
                len++;
            }
            name.resize(len);
            for (size_t i = len; i > 0; --i) {
                name[i - 1] = static_cast<char>('A' + (index % 26));
                index /= 26;
            }
        };

        /**
         * @brief Normalizes a name and handles empty strings by generating a default name.
         * 
         * Trims whitespace and ensures the name does not exceed MAX_STRING_LENGTH.
         * If the resulting name is empty, generates a default name based on the index.
         * 
         * @param index The column index associated with this name (used for default generation).
         * @param[in,out] name The name to normalize.
         */
        static void normalizeName(size_t index, std::string& name) {
            const char* WhiteSpace = " \t\v\r\n";
            std::size_t start = name.find_first_not_of(WhiteSpace);
            std::size_t end   = name.find_last_not_of(WhiteSpace);
            name = (start == std::string::npos) ? std::string() : name.substr(start, end - start + 1);

            // replace control characters with underscore
            // std::replace_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r' || c == '\t'; }, '_');

            if (name.empty()) {
                // apply default name
                defaultName(index, name);
            }

            if (name.length() > MAX_STRING_LENGTH) {
                name.resize(MAX_STRING_LENGTH);
            }
        };

    private:
        Container data_;

        /**
         * @brief Internal comparator for transparent lookups.
         * Allows comparing Entry objects with std::string keys directly.
         * When names are equal, uses column index as secondary sort key
         * to keep auto-generated suffixes aligned with original column order.
         */
        struct Comparator {
            using is_transparent = void; 
            bool operator()(const Entry& e, const std::string& k) const { return e.first < k; }
            bool operator()(const std::string& k, const Entry& e) const { return k < e.first; }
            bool operator()(const Entry& a, const Entry& b) const {
                // Primary sort by name, secondary sort by column index
                if (a.first != b.first) return a.first < b.first;
                return a.second < b.second;
            }
        };

    public:
        ColumnNameIndex() = default;
        ~ColumnNameIndex() = default;

        Iterator begin()                { return data_.begin(); }
        ConstIterator begin() const     { return data_.begin(); }
        
        /**
         * @brief Builds the column name index with automatic conflict resolution.
         * 
         * Normalizes names, sorts by name, and resolves conflicts using numeric suffixes.
         * Conflict resolution: "col", "col" -> "col", "col.1"
         * 
         * IMPORTANT: This method modifies the input columnNames container in place to ensure
         * synchronization between the original column names and the index. After calling build(),
         * columnNames will contain the conflict-resolved names.
         * 
         * The algorithm maintains intuitive suffix numbering: when duplicate names occur at
         * positions [0, 1, 2], they become "name", "name.1", "name.2" (suffixes match position).
         * 
         * Algorithm:
         * - Parse names into (basename, suffix) pairs
         * - Sort by (name, column_index) to preserve original order for duplicates
         * - Single-pass conflict resolution: increment suffix for duplicates
         * - Maintain sorted order throughout (no final re-sort needed)
         * 
         * Complexity: O(n log n) typical case, handles pathological cases efficiently.
         * 
         * @param columnNames The source column names to index (modified in place).
         */
        void build(ContainerType<std::string>& columnNames);

        /**
         * @brief Clears the index.
         * For dynamic vectors, this empties the container. 
         * For fixed arrays, this resets names to defaults (A, B, C...).
         */
        void clear();

        /**
         * @brief Checks if a column name exists in the index.
         * @param name The column name to check.
         * @return true if found, false otherwise.
         */
        bool contains(const std::string& name) const;
        
        Iterator end()                  { return data_.end(); }
        ConstIterator end()   const     { return data_.end(); }
        


        /**
         * @brief Inserts a new column into the index.
         * Only available for dynamic layouts. Resolves naming conflicts automatically.
         * @param name The name of the new column (may be modified if conflict occurs).
         * @param column The position index of the new column.
         */
        void insert(std::string& name, size_t column);

        /**
         * @brief Removes a column from the index by name.
         * Only available for dynamic layouts. Also updates indices of subsequent columns.
         * @param name Name of the column to remove.
         */
        void remove(const std::string& name);

        /**
         * @brief Renames an existing column.
         * Handles re-sorting the index and resolving new name conflicts.
         * @param oldName The current name of the column.
         * @param newName The desired new name (may be modified).
         * @return true if oldName was found and renamed, false otherwise.
         */
        bool rename(const std::string& oldName, std::string& newName);

        /**
         * @brief Requests that the vector capacity be at least enough to contain n elements.
         * No-op for fixed-size arrays.
         */
        void reserve(size_t n) {
            if constexpr (!IS_FIXED_SIZE) { data_.reserve(n); } 
        }
        
        /**
         * @brief Resizes the container.
         * Only available for dynamic layouts.
         */
        void resize(size_t n) {
            static_assert(!IS_FIXED_SIZE, "Cannot resize a fixed-size ColumnIndex"); if constexpr (!IS_FIXED_SIZE) { data_.resize(n);  } 
        }
        
        /**
         * @brief Returns the number of elements in the index.
         */
        auto size()  const {
            if constexpr (IS_FIXED_SIZE) { return Capacity; } else { return data_.size(); } 
        }

        /**
         * @brief Retrieves the column index for a given name.
         * @param name The column name.
         * @return The column index, or MAX_COLUMN_COUNT (SIZE_MAX) if not found.
         */
        size_t operator[](const std::string& name) const;

    };
} // namespace bcsv