/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>
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
            , buffer_()
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
            , buffer_(std::move(other.buffer_))
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
                buffer_ = std::move(other.buffer_);
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
        const std::vector<std::byte>& compress(std::span<const std::byte> input) {
            if (input.empty()) {
                buffer_.clear();
                return buffer_;
            }
            buffer_.resize(LZ4_COMPRESSBOUND(input.size()));
            
            int compressedSize = LZ4_compress_fast_continue(
                stream_,
                reinterpret_cast<const char*>(input.data()),
                reinterpret_cast<char*>(buffer_.data()),
                static_cast<int>(input.size()),
                static_cast<int>(buffer_.size()),
                acceleration_
            );
            buffer_.resize(compressedSize);
            return buffer_;
        }
        
        const std::vector<std::byte>& compress(std::span<const char> input) {
            return compress({reinterpret_cast<const std::byte*>(input.data()), input.size()});
        }

        const std::vector<std::byte>& compress(std::span<const uint8_t> input) {
            return compress({reinterpret_cast<const std::byte*>(input.data()), input.size()});
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
            buffer_.clear();
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
        
        /**
         * @brief Get the internal compressed buffer
         * @return Reference to the internal compressed buffer
         */
        const std::vector<std::byte>& getCompressedBuffer() const {
            return buffer_;
        }

    private:
        LZ4_stream_t* stream_;
        int acceleration_;
        std::vector<std::byte> buffer_;
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
    template<size_t MaxPayloadSize = 16 * 1024 * 1024> // 16 MB default max payload size
    class LZ4DecompressionStream {
    public:
        /**
         * @brief Construct with ring buffer sized for packet
         * @param maxBlockSize Maximum size of any decompressed row in the packet
         */
        explicit LZ4DecompressionStream(size_t PayloadSize = 64 * 1024) // 64 KB default payload size
            : stream_(LZ4_createStreamDecode())
            , buffer_()
            , page_(0)
        {
            if (!stream_) {
                throw std::runtime_error("Failed to create LZ4 decompression stream");
            }

            // Allocate double buffer for decompressed data
            buffer_[0].resize(PayloadSize);
            buffer_[1].resize(PayloadSize);
        }
        
        ~LZ4DecompressionStream() {
            if (stream_) {
                LZ4_freeStreamDecode(stream_);
            }
        }
        
        // Delete copy, allow move
        LZ4DecompressionStream(const LZ4DecompressionStream&) = delete;
        LZ4DecompressionStream& operator=(const LZ4DecompressionStream&) = delete;
        
        LZ4DecompressionStream(LZ4DecompressionStream&& other) noexcept
            : stream_(other.stream_)
            , buffer_(std::move(other.buffer_))
            , page_(other.page_)
        {
            other.stream_ = nullptr;
        }
        
        void resizeBuffer(size_t newSize) {
            if(newSize > MaxPayloadSize) {
                throw std::runtime_error("LZ4DecompressionStream: Requested buffer size exceeds maximum allowed payload size");
            }
            buffer_[0].resize(newSize);
            buffer_[1].resize(newSize);
        }

        /**
         * @brief Decompress into ring buffer
         * @param input Compressed data
         * @return Span view into payload buffer containing decompressed data
         */
        std::span<const std::byte> decompress(std::span<const std::byte> input) {
            if (input.empty()) {
                return {};
            }
            
            if(input.size() > buffer_[0].size()) {
                resizeBuffer(input.size()*2);
            }

            // swap page [0 and 1]
            page_ = (page_+ 1) & 1; 

            int decompressedSize = -1;
            while(decompressedSize < 0) {
                // Decompress - we don't know exact size, just provide max capacity
                decompressedSize = LZ4_decompress_safe_continue(
                    stream_,
                    reinterpret_cast<const char*>(input.data()),
                    reinterpret_cast<char*>(buffer_[page_].data()),
                    static_cast<int>(input.size()),             // Exact compressed size (known)
                    static_cast<int>(buffer_[page_].size()) // Maximum available space (upper bound)
                );
            
                if (decompressedSize < 0) {
                    // Decompression failed - likely buffer too small
                    resizeBuffer(buffer_[page_].size() * 2);
                }
            };
            
            // decompressedSize tells us the ACTUAL size
            std::span<const std::byte> result(
                buffer_[page_].data(), static_cast<size_t>(decompressedSize)
            );           
            return result;
        }
        
        std::span<const std::byte> decompress(std::span<const uint8_t> input) {
            return decompress({reinterpret_cast<const std::byte*>(input.data()), input.size()});
        }

        std::span<const std::byte> decompress(std::span<const char> input) {
            return decompress({reinterpret_cast<const std::byte*>(input.data()), input.size()});
        }

        /**
         * @brief Reset for new packet
         */
        void reset() {
            if (stream_) {
                LZ4_freeStreamDecode(stream_);
                stream_ = LZ4_createStreamDecode();
                if (!stream_) {
                    throw std::runtime_error("Failed to recreate LZ4 decompression stream");
                }
            }
            page_ = 1;
        }
        
    private:
        LZ4_streamDecode_t* stream_;
        std::array< std::vector<std::byte>, 2> buffer_;  // Double buffer for decompression
        int page_ = 1;
    };
    
} // namespace bcsv
