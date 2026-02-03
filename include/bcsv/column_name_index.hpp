#include "bcsv/definitions.h"
#include "column_name_index.h"

#include <algorithm>

namespace bcsv {

    
    /* Clears std::vector and resets Array to default names*/
    template<size_t Capacity>
    void ColumnNameIndex<Capacity>::clear() {
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
    bool ColumnNameIndex<Capacity>::contains(const std::string& name) const
    {
        auto it = std::lower_bound(data_.begin(), data_.end(), name, Comparator{});
        return it != data_.end() && it->first == name;
    }


    template<size_t Capacity>
    void ColumnNameIndex<Capacity>::erase(const std::string& name) {
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
    bool ColumnNameIndex<Capacity>::insert(std::string& name, size_t column) {
        static_assert(!IS_FIXED_SIZE, "Cannot insert into a fixed-size ColumnIndex");
        if constexpr (!IS_FIXED_SIZE) {
            if(size()+1 > MAX_COLUMN_COUNT) {
                return false; // cannot exceed max column count
            }

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
        return false;
    }
    
    template<size_t Capacity>
    bool ColumnNameIndex<Capacity>::rename(std::string& oldName, std::string& newName)
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
    size_t ColumnNameIndex<Capacity>::operator[](const std::string& name) const {
        auto it = std::lower_bound(data_.begin(), data_.end(), name, Comparator{});
        if (it != data_.end() && it->first == name) {
            return it->second;
        }
        return MAX_COLUMN_COUNT; // Standard sentinel for "not found"
    }

};