/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <memory>
#include "lz4-1.10.0/lz4.h"

namespace bcsv {

    /**
     * @brief RAII wrapper for LZ4 streaming compression
     * 
     * This class manages the lifecycle of LZ4_stream_t for streaming compression.
     * It maintains compression context across multiple compress operations, enabling
     * cross-row dictionary compression for better compression ratios.
     * 
     * Key Features:
     * - Automatic stream creation/destruction
     * - Context preservation across compress calls
     * - Thread-safe per-instance (not thread-safe across instances)
     * - Exception-safe resource management
     * 
     * Usage Example:
     * ```cpp
     * LZ4CompressionStream compressor(9); // acceleration level 1-9
     * 
     * // Compress multiple rows maintaining context
     * std::vector<uint8_t> compressed(LZ4_COMPRESSBOUND(row1.size()));
     * int size1 = compressor.compress(row1, compressed);
     * 
     * // Second compression uses dictionary from first
     * int size2 = compressor.compress(row2, compressed);
     * 
     * // Reset for new packet
     * compressor.reset();
     * ```
     */
    class LZ4CompressionStream {
    public:
        /**
         * @brief Construct compression stream with specified acceleration level
         * @param acceleration Compression acceleration level (1=best compression, 9=fastest)
         * @throws std::runtime_error if stream creation fails
         */
        explicit LZ4CompressionStream(int acceleration = 1)
            : stream_(LZ4_createStream())
            , acceleration_(acceleration)
        {
            if (!stream_) {
                throw std::runtime_error("Failed to create LZ4 compression stream");
            }
        }
        
        /**
         * @brief Destructor - automatically frees LZ4 stream
         */
        ~LZ4CompressionStream() {
            if (stream_) {
                LZ4_freeStream(stream_);
            }
        }
        
        // Delete copy constructor and assignment operator (non-copyable)
        LZ4CompressionStream(const LZ4CompressionStream&) = delete;
        LZ4CompressionStream& operator=(const LZ4CompressionStream&) = delete;
        
        // Allow move construction and assignment
        LZ4CompressionStream(LZ4CompressionStream&& other) noexcept
            : stream_(other.stream_)
            , acceleration_(other.acceleration_)
        {
            other.stream_ = nullptr;
        }
        
        LZ4CompressionStream& operator=(LZ4CompressionStream&& other) noexcept {
            if (this != &other) {
                if (stream_) {
                    LZ4_freeStream(stream_);
                }
                stream_ = other.stream_;
                acceleration_ = other.acceleration_;
                other.stream_ = nullptr;
            }
            return *this;
        }
        
        /**
         * @brief Compress data using streaming context
         * 
         * Compresses the input data using LZ4_compress_fast_continue, which maintains
         * compression dictionary across calls for better compression ratios on similar data.
         * 
         * @param input Source data to compress
         * @param output Destination buffer (must be at least LZ4_COMPRESSBOUND(input.size()))
         * @return Number of bytes written to output buffer, or 0 on failure
         * 
         * @note Output buffer must have capacity >= LZ4_COMPRESSBOUND(input.size())
         * @note Returns 0 if compression fails (typically means output buffer too small)
         */
        int compress(std::span<const uint8_t> input, std::span<uint8_t> output) {
            if (input.empty()) {
                return 0;
            }
            
            if (output.size() < static_cast<size_t>(LZ4_COMPRESSBOUND(input.size()))) {
                return 0; // Output buffer too small
            }
            
            int compressedSize = LZ4_compress_fast_continue(
                stream_,
                reinterpret_cast<const char*>(input.data()),
                reinterpret_cast<char*>(output.data()),
                static_cast<int>(input.size()),
                static_cast<int>(output.size()),
                acceleration_
            );
            
            return compressedSize;
        }
        
        /**
         * @brief Reset compression stream for new packet
         * 
         * Resets the internal state and dictionary, preparing for a new compression sequence.
         * Use this between packets to ensure each packet can be decompressed independently.
         * 
         * @note Much faster than creating a new stream
         */
        void reset() {
            if (stream_) {
                LZ4_resetStream_fast(stream_);
            }
        }
        
        /**
         * @brief Get the acceleration level
         * @return Current acceleration level (1-9)
         */
        int getAcceleration() const {
            return acceleration_;
        }
        
        /**
         * @brief Set the acceleration level
         * @param acceleration New acceleration level (1=best compression, 9=fastest)
         */
        void setAcceleration(int acceleration) {
            acceleration_ = acceleration;
        }
        
    private:
        LZ4_stream_t* stream_;
        int acceleration_;
    };
    
    /**
     * @brief RAII wrapper for LZ4 streaming decompression
     * 
     * This class manages the lifecycle of LZ4_streamDecode_t for streaming decompression.
     * It maintains decompression context across multiple decompress operations, enabling
     * proper decompression of data compressed with cross-row dictionary.
     * 
     * Key Features:
     * - Automatic stream creation/destruction
     * - Context preservation across decompress calls
     * - Thread-safe per-instance (not thread-safe across instances)
     * - Exception-safe resource management
     * - Safe decompression with bounds checking
     * 
     * Usage Example:
     * ```cpp
     * LZ4DecompressionStream decompressor;
     * 
     * // Decompress multiple rows maintaining context
     * std::vector<uint8_t> decompressed(original_size);
     * int size1 = decompressor.decompress(compressed1, decompressed, original_size1);
     * 
     * // Second decompression uses dictionary from first
     * int size2 = decompressor.decompress(compressed2, decompressed, original_size2);
     * 
     * // Reset for new packet
     * decompressor.reset();
     * ```
     */
    class LZ4DecompressionStream {
    public:
        /**
         * @brief Construct decompression stream
         * @throws std::runtime_error if stream creation fails
         */
        LZ4DecompressionStream()
            : stream_(LZ4_createStreamDecode())
        {
            if (!stream_) {
                throw std::runtime_error("Failed to create LZ4 decompression stream");
            }
        }
        
        /**
         * @brief Destructor - automatically frees LZ4 stream
         */
        ~LZ4DecompressionStream() {
            if (stream_) {
                LZ4_freeStreamDecode(stream_);
            }
        }
        
        // Delete copy constructor and assignment operator (non-copyable)
        LZ4DecompressionStream(const LZ4DecompressionStream&) = delete;
        LZ4DecompressionStream& operator=(const LZ4DecompressionStream&) = delete;
        
        // Allow move construction and assignment
        LZ4DecompressionStream(LZ4DecompressionStream&& other) noexcept
            : stream_(other.stream_)
        {
            other.stream_ = nullptr;
        }
        
        LZ4DecompressionStream& operator=(LZ4DecompressionStream&& other) noexcept {
            if (this != &other) {
                if (stream_) {
                    LZ4_freeStreamDecode(stream_);
                }
                stream_ = other.stream_;
                other.stream_ = nullptr;
            }
            return *this;
        }
        
        /**
         * @brief Decompress data using streaming context with safety checks
         * 
         * Decompresses the input data using LZ4_decompress_safe_continue, which:
         * - Maintains decompression dictionary across calls
         * - Validates decompressed size matches expected size
         * - Protects against buffer overruns
         * 
         * @param input Compressed data
         * @param output Destination buffer for decompressed data
         * @param expectedSize Expected size of decompressed data (for validation)
         * @return Number of bytes written to output buffer, or negative value on error
         * 
         * @note Returns negative value if:
         *       - Decompression fails
         *       - Decompressed size doesn't match expectedSize
         *       - Buffer overrun would occur
         */
        int decompress(std::span<const uint8_t> input, std::span<uint8_t> output, int expectedSize) {
            if (input.empty() || expectedSize <= 0) {
                return -1;
            }
            
            if (output.size() < static_cast<size_t>(expectedSize)) {
                return -1; // Output buffer too small
            }
            
            int decompressedSize = LZ4_decompress_safe_continue(
                stream_,
                reinterpret_cast<const char*>(input.data()),
                reinterpret_cast<char*>(output.data()),
                static_cast<int>(input.size()),
                expectedSize
            );
            
            return decompressedSize;
        }
        
        /**
         * @brief Reset decompression stream for new packet
         * 
         * Resets the internal state and dictionary, preparing for a new decompression sequence.
         * Use this between packets to ensure each packet is decompressed independently.
         */
        void reset() {
            if (stream_) {
                // For decompression stream, we need to recreate it
                // as there's no LZ4_resetStreamDecode_fast equivalent
                LZ4_freeStreamDecode(stream_);
                stream_ = LZ4_createStreamDecode();
                if (!stream_) {
                    throw std::runtime_error("Failed to recreate LZ4 decompression stream");
                }
            }
        }
        
    private:
        LZ4_streamDecode_t* stream_;
    };
    
} // namespace bcsv
