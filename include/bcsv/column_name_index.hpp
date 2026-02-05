#pragma once
/**
 * @file column_name_index.hpp
 * @brief Binary CSV (BCSV) Library - Column Name Index utilities
 * 
 * This file contains utilities for managing column name indices, providing
 * a unified interface for both dynamic (std::vector) and fixed-size (std::array) storage.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */


#include "column_name_index.h"
#include <algorithm>

namespace bcsv {

    // ============================================================================
    // Internal helper structure for parsing and managing column name suffixes
    // ============================================================================
    
    /**
     * @brief Helper structure for parsing column names into (basename, suffix) pairs.
     * 
     * Examples:
     * - "col" -> ("col", 0)
     * - "col.5" -> ("col", 5)
     * - "col.1.2" -> ("col.1", 2)  // Only last numeric suffix is parsed
     * 
     * Optimized for performance:
     * - Single backwards pass for parsing
     * - Manual integer parsing (faster than std::stoi)
     * - Minimal string allocations
     */
    template<size_t Capacity>
    struct ParsedName {
        std::string basename;
        int suffix;
        
        /**
         * @brief Parse a name into (basename, suffix) pair.
         * Scans backwards to find the last dot followed by digits.
         */
        static ParsedName parse(const std::string& name) {
            if (name.empty()) {
                return {name, 0};
            }
            
            const char* data = name.data();
            size_t len = name.length();
            size_t digitStart = len;
            
            // Find trailing digits by scanning backwards
            while (digitStart > 0 && data[digitStart - 1] >= '0' && data[digitStart - 1] <= '9') {
                --digitStart;
            }
            
            // Check if we have a dot before the digits: "basename.digits"
            if (digitStart > 0 && digitStart < len && data[digitStart - 1] == '.') {
                size_t dotPos = digitStart - 1;
                
                // Parse suffix manually (faster than stoi, no exceptions)
                int suffix = 0;
                for (size_t i = digitStart; i < len; ++i) {
                    suffix = suffix * 10 + (data[i] - '0');
                }
                
                return {name.substr(0, dotPos), suffix};
            }
            
            return {name, 0};
        }
        
        /**
         * @brief Reconstruct full name from (basename, suffix).
         * Pre-calculates size to avoid reallocation.
         */
        std::string toString() const {
            if (suffix == 0) {
                return basename;
            }
            // Estimate suffix digits to pre-allocate string
            size_t suffixDigits = (suffix < 10) ? 1 : (suffix < 100) ? 2 : 
                                  (suffix < 1000) ? 3 : (suffix < 10000) ? 4 : 
                                  std::to_string(suffix).length();
            std::string result;
            result.reserve(basename.length() + 1 + suffixDigits);
            result = basename;
            result += '.';
            result += std::to_string(suffix);
            return result;
        }
        
        bool operator==(const ParsedName& other) const {
            return basename == other.basename && suffix == other.suffix;
        }
    };

    // ============================================================================
    // ColumnNameIndex Implementation
    // ============================================================================
    
    /**
     * @brief Build the column name index with conflict resolution.
     * 
     * V7 Optimized Algorithm:
     * 1. Normalize and copy names into Entry array
     * 2. Sort by name string
     * 3. Parse names into (basename, suffix) pairs
     * 4. Single-pass conflict resolution:
     *    - When conflict found, increment suffix
     *    - Find new position via binary search
     *    - Shift elements to maintain sorted order
     * 5. No final sort needed (maintained throughout)
     * 
     * Performance: O(n log n) typical, handles 100% conflicts efficiently.
     */
    template<size_t Capacity>
    inline void ColumnNameIndex<Capacity>::build(ContainerType<std::string>& columnNames) {
        // Resize storage if dynamic
        if constexpr (!IS_FIXED_SIZE) {
            data_.resize(columnNames.size());
        }
        
        // Pass 1: Normalize names and create entries
        for (size_t i = 0; i < columnNames.size(); ++i) {
            std::string name = columnNames[i];
            normalizeName(i, name);
            data_[i] = {name, i};
        }
        
        // Pass 2: Sort by name (establishes sorted order we'll maintain)
        std::sort(data_.begin(), data_.end(), Comparator());
        
        // Pass 3: Parse names into (basename, suffix) pairs
        std::vector<ParsedName<Capacity>> parsed;
        parsed.reserve(data_.size());
        for (size_t i = 0; i < data_.size(); ++i) {
            parsed.push_back(ParsedName<Capacity>::parse(data_[i].first));
        }
        
        // Pass 4: Single-pass conflict resolution with local repositioning
        for (size_t i = 0; i < data_.size(); ) {
            // Check for conflict with next entry
            if (i + 1 < data_.size() && parsed[i] == parsed[i + 1]) {
                // Conflict found - increment suffix
                parsed[i + 1].suffix++;
                data_[i + 1].first = parsed[i + 1].toString();
                
                // Binary search for new position (from i+2 onwards)
                // Must use Comparator to maintain (name, column_index) ordering
                size_t left = i + 2;
                size_t right = data_.size();
                
                while (left < right) {
                    size_t mid = left + (right - left) / 2;
                    if (Comparator()(data_[mid], data_[i + 1])) {
                        left = mid + 1;
                    } else {
                        right = mid;
                    }
                }
                
                // Shift elements if needed to maintain sorted order
                if (left != i + 2) {
                    Entry temp = data_[i + 1];
                    ParsedName<Capacity> temp_parsed = parsed[i + 1];
                    
                    // Shift elements down
                    for (size_t j = i + 1; j < left - 1; ++j) {
                        data_[j] = data_[j + 1];
                        parsed[j] = parsed[j + 1];
                    }
                    
                    // Insert at correct position
                    data_[left - 1] = temp;
                    parsed[left - 1] = temp_parsed;
                }
                
                // Re-check current position (i+1 might have been replaced)
            } else {
                // No conflict, advance
                ++i;
            }
        }
        
        // Pass 5: Synchronize resolved names back to the input container
        // This ensures column_names_ and column_index_ stay in sync
        for (size_t i = 0; i < data_.size(); ++i) {
            const size_t col_idx = data_[i].second;
            columnNames[col_idx] = data_[i].first;
        }
    }

    /* Clears std::vector and resets Array to default names*/
    template<size_t Capacity>
    inline void ColumnNameIndex<Capacity>::clear() {
        if constexpr (IS_FIXED_SIZE) {
            for(size_t i = 0; i < Capacity; ++i) {
                Entry& entry = data_[i];
                defaultName(i, entry.first);
                entry.second = i;
            }
        } else {
            data_.clear();
        }
    }

    template<size_t Capacity>
    inline bool ColumnNameIndex<Capacity>::contains(const std::string& name) const
    {
        auto it = std::lower_bound(data_.begin(), data_.end(), name, Comparator{});
        return it != data_.end() && it->first == name;
    }




    template<size_t Capacity>
    inline void ColumnNameIndex<Capacity>::insert(std::string& name, size_t column) {
        static_assert(!IS_FIXED_SIZE, "Cannot insert into a fixed-size ColumnIndex");
        if constexpr (!IS_FIXED_SIZE) {
            normalizeName(column, name);
            
            // find a slot to insert
            Iterator iter = std::lower_bound(data_.begin(), data_.end(), name, Comparator{});
            
            // resolve conflicts
            while(iter != data_.end() && iter->first == name) {
                name += '_';
                iter = std::lower_bound(iter, data_.end(), name, Comparator{});
            }

            // update indices of existing entries
            for(size_t i = 0; i < data_.size(); ++i) {
                if(data_[i].second >= column) {
                    data_[i].second++;
                }
            }
            data_.insert(iter, Entry{name, column});
        }
    }
    
    template<size_t Capacity>
    inline void ColumnNameIndex<Capacity>::remove(const std::string& name) {
        static_assert(!IS_FIXED_SIZE, "Cannot resize a fixed-size ColumnIndex");
        if constexpr (!IS_FIXED_SIZE) {
            Iterator iter = std::lower_bound(data_.begin(), data_.end(), name, Comparator{});
            if(iter == data_.end() || iter->first != name) {
                return; // not found
            }
            size_t column = iter->second;
            data_.erase(iter); 

            // Update indices of remaining entries
            for(size_t i = 0; i < data_.size(); ++i) {
                if(data_[i].second > column) {
                    data_[i].second--;
                }
            }
        }
    }

    template<size_t Capacity>
    inline bool ColumnNameIndex<Capacity>::rename(const std::string& oldName, std::string& newName)
    {
        // 1. Find old entry
        Iterator it_old = std::lower_bound(data_.begin(), data_.end(), oldName, Comparator{});
        if(it_old == data_.end() || it_old->first != oldName) {
            return false;
        }

        size_t column = it_old->second;
        normalizeName(column, newName);
        if(newName == oldName) return true;

        // 2. Find target position for new name
        Iterator it_new = std::lower_bound(data_.begin(), data_.end(), newName, Comparator{});
        
        // Resolve conflicts via '_'
        while(it_new != data_.end() && it_new->first == newName) {
            newName += '_';
            it_new = std::lower_bound(it_new, data_.end(), newName, Comparator{});
        }

        // 3. Rotate Logic (CRITICAL FIX)
        // std::rotate moves elements, but iterators stay pointing to the same *slot*.
        // We must update the name on the iterator that ends up holding our element.

        if(it_new < it_old) {
            // Moving Left: Element moves to 'it_new'
            std::rotate(it_new, it_old, it_old + 1);
            it_new->first = newName;
        } 
        else if(it_new > it_old + 1) {
            // Moving Right: Element moves to position *before* it_new
            std::rotate(it_old, it_old + 1, it_new);
            (it_new - 1)->first = newName;
        } 
        else {
            // In-place update (iterators are identical or adjacent)
            it_old->first = newName;
        }
        
        return true;
    }

    template<size_t Capacity>
    inline size_t ColumnNameIndex<Capacity>::operator[](const std::string& name) const {
        auto it = std::lower_bound(data_.begin(), data_.end(), name, Comparator{});
        if (it != data_.end() && it->first == name) {
            return it->second;
        }
        return MAX_COLUMN_COUNT; // Standard sentinel for "not found"
    }
};