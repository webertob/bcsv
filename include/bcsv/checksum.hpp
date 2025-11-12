#pragma once

#include "xxhash.h"
#include <stdexcept>
#include <cstdint>
#include <cstddef>

namespace bcsv {

/**
 * @brief Checksum utility using xxHash64 algorithm
 * 
 * Provides both one-shot and streaming hash computation.
 * xxHash64 is ~3-5x faster than CRC32 and works efficiently
 * on both 32-bit and 64-bit platforms.
 */
class Checksum {
public:
    using hash_t = uint64_t;
    static constexpr hash_t DEFAULT_SEED = 0;

    /**
     * @brief Compute hash for a memory block (one-shot)
     * @param data Pointer to data
     * @param length Size of data in bytes
     * @param seed Optional seed value (default: 0)
     * @return 64-bit hash value
     */
    static hash_t compute(const void* data, size_t length, hash_t seed = DEFAULT_SEED) {
        return XXH64(data, length, seed);
    }

    /**
     * @brief Streaming hash for incremental computation
     * 
     * Use this for large data that needs to be hashed in chunks
     * or when data arrives incrementally.
     * 
     * Example:
     * @code
     * Checksum::Streaming hasher;
     * hasher.update(chunk1, size1);
     * hasher.update(chunk2, size2);
     * uint64_t hash = hasher.finalize();
     * @endcode
     */
    class Streaming {
    public:
        Streaming(hash_t seed = DEFAULT_SEED) {
            state_ = XXH64_createState();
            if (!state_) {
                throw std::runtime_error("Failed to create xxHash state");
            }
            reset(seed);
        }

        ~Streaming() {
            if (state_) {
                XXH64_freeState(state_);
            }
        }

        // Non-copyable
        Streaming(const Streaming&) = delete;
        Streaming& operator=(const Streaming&) = delete;

        // Moveable
        Streaming(Streaming&& other) noexcept : state_(other.state_) {
            other.state_ = nullptr;
        }

        Streaming& operator=(Streaming&& other) noexcept {
            if (this != &other) {
                if (state_) {
                    XXH64_freeState(state_);
                }
                state_ = other.state_;
                other.state_ = nullptr;
            }
            return *this;
        }

        /**
         * @brief Reset hash state with new seed
         */
        void reset(hash_t seed = DEFAULT_SEED) {
            XXH64_reset(state_, seed);
        }

        /**
         * @brief Add data to hash computation
         * @param data Pointer to data chunk
         * @param length Size of chunk in bytes
         */
        void update(const void* data, size_t length) {
            XXH64_update(state_, data, length);
        }

        /**
         * @brief Finalize and return hash value
         * @return 64-bit hash value
         * 
         * Note: After finalize(), you can call reset() to reuse the object
         */
        hash_t finalize() {
            return XXH64_digest(state_);
        }

    private:
        XXH64_state_t* state_;
    };
};

} // namespace bcsv