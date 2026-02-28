/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file lz4_block.hpp
 * @brief Stateless LZ4 block compression/decompression for batch codecs.
 *
 * Provides one-shot block-mode compression using externally allocated state.
 * Each instance is thread-safe (no shared mutable state between calls on
 * separate instances).  Adaptive mode: uses LZ4 fast for compression levels
 * 1-5 and LZ4 HC for levels 6-9.
 *
 * Unlike the streaming wrappers in lz4_stream.hpp, these classes have no
 * dictionary context — each compression call is independent.  This is ideal
 * for batch codecs where an entire packet payload is compressed at once.
 */

#include "lz4-1.10.0/lz4.h"
#include "lz4-1.10.0/lz4hc.h"
#include "byte_buffer.h"
#include "definitions.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>

// LZ4 APIs use int for sizes.  Ensure our maximum possible input
// (MAX_PACKET_SIZE + one worst-case row + PCKT_TERMINATOR VLE) fits.
static_assert(bcsv::MAX_PACKET_SIZE + bcsv::MAX_ROW_LENGTH + 16 < static_cast<size_t>(INT_MAX),
              "MAX_PACKET_SIZE + MAX_ROW_LENGTH must fit in int for LZ4 APIs");

namespace bcsv {

/**
 * @brief Stateless LZ4 block compressor with adaptive mode selection.
 *
 * Compression levels 1-5 use LZ4_compress_fast_extState() (fast mode).
 * Compression levels 6-9 use LZ4_compress_HC_extStateHC() (high compression).
 *
 * Owns an externally allocated state buffer sized for the chosen mode.
 * Non-copyable, movable.
 */
class LZ4BlockCompressor {
public:
    /// Construct without initialization.  Call init() before use.
    LZ4BlockCompressor() = default;

    /// Initialize with the given BCSV compression level (1-9).
    explicit LZ4BlockCompressor(int compressionLevel) {
        init(compressionLevel);
    }

    ~LZ4BlockCompressor() {
        std::free(state_);
    }

    // Non-copyable — owns raw state allocation
    LZ4BlockCompressor(const LZ4BlockCompressor&) = delete;
    LZ4BlockCompressor& operator=(const LZ4BlockCompressor&) = delete;

    LZ4BlockCompressor(LZ4BlockCompressor&& other) noexcept
        : state_(other.state_)
        , state_size_(other.state_size_)
        , use_hc_(other.use_hc_)
        , acceleration_(other.acceleration_)
        , hc_level_(other.hc_level_)
    {
        other.state_ = nullptr;
        other.state_size_ = 0;
    }

    LZ4BlockCompressor& operator=(LZ4BlockCompressor&& other) noexcept {
        if (this != &other) {
            std::free(state_);
            state_ = other.state_;
            state_size_ = other.state_size_;
            use_hc_ = other.use_hc_;
            acceleration_ = other.acceleration_;
            hc_level_ = other.hc_level_;
            other.state_ = nullptr;
            other.state_size_ = 0;
        }
        return *this;
    }

    /// Initialize (or re-initialize) for a given BCSV compression level.
    void init(int compressionLevel) {
        std::free(state_);

        if (compressionLevel >= 6) {
            // HC mode: levels 6-9 map to LZ4HC levels 6-12
            use_hc_ = true;
            hc_level_ = compressionLevel + 3;  // BCSV 6→9, 7→10, 8→11, 9→12
            state_size_ = static_cast<size_t>(LZ4_sizeofStateHC());
        } else {
            // Fast mode: levels 1-5
            use_hc_ = false;
            acceleration_ = 10 - compressionLevel;  // level 1→accel 9, level 5→accel 5
            state_size_ = static_cast<size_t>(LZ4_sizeofState());
        }

        state_ = std::malloc(state_size_);
        if (!state_) {
            throw std::runtime_error("LZ4BlockCompressor: failed to allocate state");
        }
    }

    bool isInitialized() const noexcept { return state_ != nullptr; }
    bool isHC() const noexcept { return use_hc_; }

    /**
     * @brief Compress src into dst (appending).
     * @param src  Uncompressed input data.
     * @param dst  Output buffer — compressed data is appended.
     * @return     Span of the appended compressed bytes within dst.
     */
    std::span<const std::byte> compress(std::span<const std::byte> src, ByteBuffer& dst) {
        if (src.empty()) return {};

        int srcSize = static_cast<int>(src.size());
        int maxDstSize = LZ4_compressBound(srcSize);

        size_t offset = dst.size();
        dst.resize(offset + static_cast<size_t>(maxDstSize));

        const char* srcPtr = reinterpret_cast<const char*>(src.data());
        char* dstPtr = reinterpret_cast<char*>(dst.data() + offset);

        int compressedSize = 0;
        if (use_hc_) {
            compressedSize = LZ4_compress_HC_extStateHC(
                state_, srcPtr, dstPtr, srcSize, maxDstSize, hc_level_);
        } else {
            compressedSize = LZ4_compress_fast_extState(
                state_, srcPtr, dstPtr, srcSize, maxDstSize, acceleration_);
        }

        if (compressedSize <= 0) {
            throw std::runtime_error("LZ4BlockCompressor: compression failed");
        }

        dst.resize(offset + static_cast<size_t>(compressedSize));
        return std::span<const std::byte>(dst.data() + offset,
                                          static_cast<size_t>(compressedSize));
    }

private:
    void*  state_{nullptr};
    size_t state_size_{0};
    bool   use_hc_{false};
    int    acceleration_{1};
    int    hc_level_{9};
};

/**
 * @brief Stateless LZ4 block decompressor.
 *
 * Uses LZ4_decompress_safe() — no streaming context, each call is independent.
 * The caller must know the maximum uncompressed size (stored in the wire format).
 */
class LZ4BlockDecompressor {
public:
    LZ4BlockDecompressor() = default;

    /**
     * @brief Decompress a single LZ4 block.
     * @param src             Compressed data.
     * @param dst             Output buffer — must be pre-sized to hold decompressed data.
     * @param maxDecompressed Maximum expected decompressed size.
     * @return Span of decompressed bytes within dst.
     */
    std::span<const std::byte> decompress(
            std::span<const std::byte> src,
            ByteBuffer& dst,
            size_t maxDecompressed) {
        if (src.empty()) return {};

        dst.resize(maxDecompressed);

        int result = LZ4_decompress_safe(
            reinterpret_cast<const char*>(src.data()),
            reinterpret_cast<char*>(dst.data()),
            static_cast<int>(src.size()),
            static_cast<int>(maxDecompressed));

        if (result < 0) {
            throw std::runtime_error("LZ4BlockDecompressor: decompression failed");
        }

        return std::span<const std::byte>(dst.data(), static_cast<size_t>(result));
    }
};

} // namespace bcsv
