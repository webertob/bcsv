#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>
#include "lz4-1.10.0/lz4.h"
#include "byte_buffer.h"

namespace bcsv {

    /**
     * @brief Streaming LZ4 compressor.
     * 
     * Maintains a dictionary context to improve compression ratio for sequential data.
     * 
     * Design Decisions:
     * - Headerless: Does NOT write the 4-byte uncompressed size header. This saves 4 bytes per row.
     *   The uncompressed size is not required for decompression; the decompressor will output 
     *   whatever amount of data was originally compressed in the block.
     * - Block-Based: One call to compress() corresponds to exactly one call to decompress(). 
     *   The caller must preserve the boundaries of the compressed blocks (e.g., by storing the compressed size).
     * - Ring Buffer: Uses a ring buffer to keep the last 64KB as a dictionary for the next compression.
     * - Zero-Copy: For inputs larger than the dictionary, compresses directly from the source to avoid 
     *   unnecessary copies, then saves the dictionary.
     * 
     * @tparam MAX_USABLE_BUFFER_SIZE Maximum size of the uncompressed data buffer (excluding dictionary/margin).
     */
    template<size_t MAX_USABLE_BUFFER_SIZE = 16 * 1024 * 1024> 
    class LZ4CompressionStream {
        private:
            static constexpr size_t LZ4_DICT_SIZE = 64 * 1024;
            static constexpr size_t LZ4_MARGIN = 14;
            static constexpr size_t MAX_BUFFER_SIZE = MAX_USABLE_BUFFER_SIZE + LZ4_DICT_SIZE + LZ4_MARGIN;
            
            LZ4_stream_t stream_;
            ByteBuffer buffer_;
            int acceleration_;
            int pos_ = 0;
            
        public:
            explicit LZ4CompressionStream(size_t initial_capacity = 64 * 1024, int acceleration = 1)
                : acceleration_(acceleration)
                , pos_(0)
            {
                size_t buffer_size = LZ4_DICT_SIZE + LZ4_MARGIN + initial_capacity;
                buffer_.resize(buffer_size);
                LZ4_initStream(&stream_, sizeof(stream_));
            }
            
            ~LZ4CompressionStream() = default;
            
            LZ4CompressionStream(const LZ4CompressionStream&) = delete;
            LZ4CompressionStream& operator=(const LZ4CompressionStream&) = delete;
            
            // Move operations deleted: LZ4_stream_t contains internal pointers into
            // the ring buffer. After std::move(buffer_), those pointers dangle.
            // LZ4_initStream would reset all dictionary context, corrupting any
            // subsequent LZ4_compress_fast_continue calls within the same packet.
            // std::optional::emplace() constructs in-place and does not require
            // movability. Writer/Reader never move mid-stream.
            LZ4CompressionStream(LZ4CompressionStream&&) = delete;
            LZ4CompressionStream& operator=(LZ4CompressionStream&&) = delete;
            
            void reset() {
                LZ4_resetStream_fast(&stream_);
                pos_ = 0;
            }
            
            int getAcceleration() const { return acceleration_; }
            void setAcceleration(int acc) { acceleration_ = acc; }

            /**
             * @brief Compresses the source span and appends to the destination vector.
             * 
             * @param src Input data to compress.
             * @param dst Destination vector to append compressed data to.
             * @throws std::runtime_error if compression fails.
             */
            void compress(std::span<const std::byte> src, ByteBuffer &dst) {
                if (src.empty()) return;

                size_t originalSize = dst.size();
                int srcSize = static_cast<int>(src.size());
                int maxDestSize = LZ4_compressBound(srcSize);
                
                dst.resize(originalSize + maxDestSize);
                
                char* dstPtr = reinterpret_cast<char*>(dst.data() + originalSize);
                int compressedSize = 0;

                // --- Case 1: Append ---
                if (static_cast<size_t>(pos_ + srcSize) <= buffer_.size()) {
                    std::memcpy(buffer_.data() + pos_, src.data(), srcSize);
                    compressedSize = LZ4_compress_fast_continue(
                        &stream_,
                        reinterpret_cast<const char*>(buffer_.data() + pos_),
                        dstPtr,
                        srcSize,
                        maxDestSize,
                        acceleration_
                    );
                    pos_ += srcSize;
                }
                // --- Case 3: Zero-Copy (Large Input) ---
                // Optimization: If input is larger than dictionary, it's cheaper to compress directly
                // and then save the dictionary, rather than copying to ring buffer.
                else if (static_cast<size_t>(srcSize) > LZ4_DICT_SIZE) {
                    compressedSize = LZ4_compress_fast_continue(
                        &stream_,
                        reinterpret_cast<const char*>(src.data()),
                        dstPtr,
                        srcSize,
                        maxDestSize,
                        acceleration_
                    );
                    int dictBytes = LZ4_saveDict(&stream_, reinterpret_cast<char*>(buffer_.data()), LZ4_DICT_SIZE);
                    pos_ = dictBytes;
                }
                // --- Case 2: Wrap (Ring Buffer) ---
                // Only wrap if we have enough history (idx_ > DICT_SIZE) AND new data fits before history starts
                else if (pos_ > static_cast<int>(LZ4_DICT_SIZE) && static_cast<size_t>(srcSize) <= static_cast<size_t>(pos_) - LZ4_DICT_SIZE) {
                    std::memcpy(buffer_.data(), src.data(), srcSize);
                    compressedSize = LZ4_compress_fast_continue(
                        &stream_,
                        reinterpret_cast<const char*>(buffer_.data()),
                        dstPtr,
                        srcSize,
                        maxDestSize,
                        acceleration_
                    );
                    pos_ = srcSize;
                }
                // --- Case 3b: Fallback (Zero-Copy + SaveDict) ---
                // This handles cases where srcSize <= DICT_SIZE but doesn't fit in ring buffer (and cannot wrap)
                else {
                    compressedSize = LZ4_compress_fast_continue(
                        &stream_,
                        reinterpret_cast<const char*>(src.data()),
                        dstPtr,
                        srcSize,
                        maxDestSize,
                        acceleration_
                    );
                    int dictBytes = LZ4_saveDict(&stream_, reinterpret_cast<char*>(buffer_.data()), LZ4_DICT_SIZE);
                    pos_ = dictBytes;
                }

                if (compressedSize <= 0) throw std::runtime_error("LZ4 compression failed");
                
                dst.resize(originalSize + compressedSize);
            }
            
            ByteBuffer compress(std::span<const std::byte> src) {
                ByteBuffer dst;
                compress(src, dst);
                return dst;
            }
    };

    template<size_t MAX_USABLE_BUFFER_SIZE = 16 * 1024 * 1024>
    class LZ4CompressionStreamInternalBuffer : public LZ4CompressionStream<MAX_USABLE_BUFFER_SIZE> {
        ByteBuffer compressedBuffer_;

        public:
            explicit LZ4CompressionStreamInternalBuffer(size_t initial_capacity = 64 * 1024, int acceleration = 1)
                : LZ4CompressionStream<MAX_USABLE_BUFFER_SIZE>(initial_capacity, acceleration)
                , compressedBuffer_(initial_capacity)
            {
            }

            std::span<std::byte> compressUseInternalBuffer(std::span<const std::byte> src) {
                compressedBuffer_.clear();
                this->compress(src, compressedBuffer_);
                return std::span<std::byte>(compressedBuffer_.data(), compressedBuffer_.size());
            }
    };

    /**
     * @brief Streaming LZ4 decompressor.
     * 
     * Decompresses data produced by LZ4CompressionStream.
     * 
     * Design Decisions:
     * - Dynamic Growth: Starts with a small buffer (64KB) to save memory for small rows.
     *   If decompression fails (likely due to insufficient space), it doubles the buffer size 
     *   and retries, up to MAX_USABLE_BUFFER_SIZE.
     * - Headerless: Does not expect a 4-byte size header. Relies on LZ4_decompress_safe_continue 
     *   failing if the buffer is too small.
     * - Block-Based: Expects input to be exactly one block produced by LZ4CompressionStream::compress().
     *   The caller must ensure the input span contains the full compressed block.
     * 
     * @tparam MAX_USABLE_BUFFER_SIZE Maximum size the buffer can grow to. Should match the compressor's max size.
     */
    template<size_t MAX_USABLE_BUFFER_SIZE = 16 * 1024 * 1024>
    class LZ4DecompressionStream {
        private:
            static constexpr size_t LZ4_DICT_SIZE = 64 * 1024;
            static constexpr size_t LZ4_MARGIN = 14;
            static constexpr size_t MAX_BUFFER_SIZE = MAX_USABLE_BUFFER_SIZE + LZ4_DICT_SIZE + LZ4_MARGIN;

            LZ4_streamDecode_t stream_;
            ByteBuffer buffer_;
            int pos_ = 0;

        public:
            explicit LZ4DecompressionStream(size_t initial_capacity = 64 * 1024) 
                : pos_(0) 
            {
                size_t buffer_size = LZ4_DICT_SIZE + LZ4_MARGIN + initial_capacity;
                buffer_.resize(buffer_size);
                LZ4_setStreamDecode(&stream_, nullptr, 0);
            }
            
            ~LZ4DecompressionStream() = default;

            LZ4DecompressionStream(const LZ4DecompressionStream&) = delete;
            LZ4DecompressionStream& operator=(const LZ4DecompressionStream&) = delete;

            // Move operations deleted: LZ4_streamDecode_t contains internal pointers
            // (externalDict, prefixEnd) into the ring buffer. After std::move(buffer_),
            // those pointers dangle. See LZ4CompressionStream rationale.
            LZ4DecompressionStream(LZ4DecompressionStream&&) = delete;
            LZ4DecompressionStream& operator=(LZ4DecompressionStream&&) = delete;

            void reset() {
                LZ4_setStreamDecode(&stream_, nullptr, 0);
                pos_ = 0;
            }

            /**
             * @brief Decompresses data.
             * 
             * @param src Compressed data.
             * @return std::span<const std::byte> View of the decompressed data. Valid until next call.
             * @throws std::runtime_error if decompression fails or buffer limit exceeded.
             */
            std::span<const std::byte> decompress(std::span<const std::byte> src) {
                if (src.empty()) return {};

                const char* srcPtr = reinterpret_cast<const char*>(src.data());
                int srcSize = static_cast<int>(src.size());
                
                while (true) {
                    int dictLen = std::min(static_cast<size_t>(pos_), LZ4_DICT_SIZE);
                    int decompressedBytes = 0;

                    // Attempt 1: Append to Ring Buffer
                    if (static_cast<size_t>(pos_) < buffer_.size()) {
                        decompressedBytes = LZ4_decompress_safe_continue(
                            &stream_,
                            srcPtr,
                            reinterpret_cast<char*>(buffer_.data() + pos_),
                            srcSize,
                            static_cast<int>(buffer_.size() - pos_)
                        );
                        
                        if (decompressedBytes > 0) {
                            std::span<const std::byte> result(buffer_.data() + pos_, decompressedBytes);
                            pos_ += decompressedBytes;
                            return result;
                        }
                    }

                    // Attempt 2: Wrap (Move dict to beginning)
                    if (pos_ > static_cast<int>(dictLen)) {
                        if (dictLen > 0) {
                            std::memmove(buffer_.data(), buffer_.data() + pos_ - dictLen, dictLen);
                        }
                        pos_ = dictLen;
                        LZ4_setStreamDecode(&stream_, reinterpret_cast<char*>(buffer_.data()), dictLen);
                        
                        decompressedBytes = LZ4_decompress_safe_continue(
                            &stream_,
                            srcPtr,
                            reinterpret_cast<char*>(buffer_.data() + pos_),
                            srcSize,
                            static_cast<int>(buffer_.size() - pos_)
                        );
                        
                        if (decompressedBytes > 0) {
                            std::span<const std::byte> result(buffer_.data() + pos_, decompressedBytes);
                            pos_ += decompressedBytes;
                            return result;
                        }
                    }

                    // Attempt 3: Grow
                    if (buffer_.size() >= MAX_BUFFER_SIZE) {
                        throw std::runtime_error("LZ4 decompression failed: insufficient buffer or corrupt data");
                    }
                    
                    size_t newCapacity = std::min(buffer_.size() * 2, MAX_BUFFER_SIZE);
                    if (newCapacity <= buffer_.size()) newCapacity = MAX_BUFFER_SIZE;
                    
                    std::vector<std::byte, LazyAllocator<std::byte>> newBuffer(newCapacity);
                    
                    if (dictLen > 0) {
                         std::memcpy(newBuffer.data(), buffer_.data() + pos_ - dictLen, dictLen);
                    }
                    
                    buffer_ = std::move(newBuffer);
                    pos_ = dictLen;
                    
                    LZ4_setStreamDecode(&stream_, reinterpret_cast<char*>(buffer_.data()), dictLen);
                }
            }
    };

} // namespace bcsv
