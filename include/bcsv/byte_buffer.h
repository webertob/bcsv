#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <limits>
#include <new>
#include <cstdlib>

namespace bcsv {

    /* An allocator that does not initialize elements, for use with STL containers */
    template<typename T>
    class LazyAllocator {
    public:
        // Required type aliases for C++17 allocators
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        
        // Rebind mechanism for different types
        template<typename U>
        struct rebind {
            using other = LazyAllocator<U>;
        };
        
        // Default constructor
        LazyAllocator() = default;
        
        // Copy constructor
        LazyAllocator(const LazyAllocator&) = default;
        
        // Copy constructor for different types (rebind)
        template<typename U>
        LazyAllocator(const LazyAllocator<U>&) {}
        
        // Allocate memory without initialization
        pointer allocate(size_type n) {
            if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
                throw std::bad_alloc();
            }
            return static_cast<pointer>(std::malloc(n * sizeof(T)));
        }
        
        // Deallocate memory
        void deallocate(pointer p, size_type) noexcept {
            std::free(p);
        }
        
        // Construct elements properly for insert operations
        template<typename U, typename... Args>
        void construct(U* p, Args&&... args) {
            new(p) U(std::forward<Args>(args)...);
        }
        
        // Don't destroy elements  
        template<typename U>
        void destroy(U*) noexcept {}
        
        // Comparison operators (all instances are equal)
        bool operator==(const LazyAllocator&) const noexcept { return true; }
        bool operator!=(const LazyAllocator&) const noexcept { return false; }
        
        // Maximum allocation size
        size_type max_size() const noexcept {
            return std::numeric_limits<size_type>::max() / sizeof(T);
        }
    };

    using ByteBuffer = std::vector<std::byte, LazyAllocator<std::byte>>;

} // namespace bcsv
