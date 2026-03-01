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
 * @file file_codec_stream_lz4_001.h
 * @brief FileCodecStreamLZ4001 — stream-LZ4 file codec.
 *
 * Streaming LZ4 compression without packet structure.
 * Each row is individually compressed via the streaming LZ4 API (dictionary
 * context accumulates across the entire file — never reset).
 * Per-row XXH32 checksums cover the compressed data for integrity.
 *
 * Wire format:
 *   FileHeader
 *   BLE(compressed_len) | lz4_block | uint32_t(XXH32)    ← repeated (len > 0)
 *   BLE(0)                                                ← ZoH repeat
 *   [EOF]
 *
 * No packet headers, no footer, no crash recovery, no random access.
 * Ideal for embedded systems that want compression but not the
 * overhead of packet framing.
 *
 * The LZ4 context is never reset (no packet boundaries), so the dictionary
 * context persists across the entire file for best compression ratio in
 * streaming scenarios.
 */

#include "file_codec_concept.h"
#include "file_codec_stream001.h"
#include "../byte_buffer.h"
#include "../checksum.hpp"
#include "../definitions.h"
#include "../file_header.h"
#include "../lz4_stream.hpp"
#include "../vle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <istream>
#include <span>
#include <stdexcept>

namespace bcsv {

class FileCodecStreamLZ4001 {
public:
    FileCodecStreamLZ4001() = default;

    // Non-copyable — LZ4 streams contain internal ring-buffer pointers.
    FileCodecStreamLZ4001(const FileCodecStreamLZ4001&) = delete;
    FileCodecStreamLZ4001& operator=(const FileCodecStreamLZ4001&) = delete;
    FileCodecStreamLZ4001(FileCodecStreamLZ4001&&) = delete;
    FileCodecStreamLZ4001& operator=(FileCodecStreamLZ4001&&) = delete;

    // ── Setup ────────────────────────────────────────────────────────────

    void setupWrite(std::ostream& /*os*/, const FileHeader& header) {
        int acceleration = 10 - static_cast<int>(header.getCompressionLevel());
        lz4_compress_.emplace(64 * 1024, acceleration);
    }

    void setupRead(std::istream& /*is*/, const FileHeader& /*header*/) {
        lz4_decompress_.emplace();
    }

    // ── Write lifecycle ─────────────────────────────────────────────────

    bool beginWrite(std::ostream& /*os*/, uint64_t /*rowCnt*/) {
        // Stream codecs have no packet boundaries.
        return false;
    }

    void writeRow(std::ostream& os, std::span<const std::byte> rowData) {
        if (rowData.empty()) {
            // ZoH repeat: length = 0, no checksum
            FileCodecStream001::writeRowLength(os, 0);
            return;
        }

        assert(lz4_compress_.has_value());
        auto compressed = lz4_compress_->compressUseInternalBuffer(rowData);

        FileCodecStream001::writeRowLength(os, compressed.size());
        os.write(reinterpret_cast<const char*>(compressed.data()),
                 static_cast<std::streamsize>(compressed.size()));

        // Per-row XXH32 checksum of compressed data
        uint32_t hash = Checksum::compute32(compressed.data(), compressed.size());
        os.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    }

    void finalize(std::ostream& /*os*/, uint64_t /*totalRows*/) {
        // Stream codecs have no footer.
    }

    /// Flush: stream codecs have no packets — just flush the OS buffer.
    /// Returns false (no packet boundary crossed).
    bool flushPacket(std::ostream& os, uint64_t /*rowCnt*/) {
        os.flush();
        return false;
    }

    ByteBuffer& writeBuffer() { return write_buffer_; }

    // ── Read lifecycle ──────────────────────────────────────────────────

    std::span<const std::byte> readRow(std::istream& is) {
        size_t rowLen = 0;
        try {
            vleDecode<uint64_t, true>(is, rowLen, nullptr);
        } catch (...) {
            return EOF_SENTINEL;
        }

        if (rowLen == 0) {
            return ZOH_REPEAT_SENTINEL;
        }

        if (rowLen > MAX_ROW_LENGTH) [[unlikely]] {
            throw std::runtime_error(
                "FileCodecStreamLZ4001::readRow: row length exceeds MAX_ROW_LENGTH ("
                + std::to_string(rowLen) + " > " + std::to_string(MAX_ROW_LENGTH) + ")");
        }

        read_buffer_.resize(rowLen);
        is.read(reinterpret_cast<char*>(read_buffer_.data()),
                static_cast<std::streamsize>(rowLen));
        if (!is || is.gcount() != static_cast<std::streamsize>(rowLen)) {
            return EOF_SENTINEL;
        }

        // Read and verify per-row XXH32 checksum of compressed data
        uint32_t expectedHash = 0;
        is.read(reinterpret_cast<char*>(&expectedHash), sizeof(expectedHash));
        if (!is || is.gcount() != static_cast<std::streamsize>(sizeof(expectedHash))) {
            throw std::runtime_error("FileCodecStreamLZ4001::readRow: failed to read row checksum");
        }
        uint32_t actualHash = Checksum::compute32(read_buffer_.data(), rowLen);
        if (actualHash != expectedHash) {
            throw std::runtime_error("FileCodecStreamLZ4001::readRow: row checksum mismatch");
        }

        // Decompress
        assert(lz4_decompress_.has_value());
        return lz4_decompress_->decompress(std::span<const std::byte>(read_buffer_.data(), rowLen));
    }

    // ── Boundary / state signals ────────────────────────────────────────

    bool packetBoundaryCrossed() const noexcept { return false; }
    void reset() noexcept {}  // Never reset LZ4 context in stream mode

private:
    ByteBuffer write_buffer_;   // Owned write buffer for RowCodec serialization
    ByteBuffer read_buffer_;    // Owned read buffer for compressed row data
    std::optional<LZ4CompressionStreamInternalBuffer<MAX_ROW_LENGTH>> lz4_compress_;
    std::optional<LZ4DecompressionStream<MAX_ROW_LENGTH>> lz4_decompress_;
};

static_assert(FileCodecConcept<FileCodecStreamLZ4001>,
              "FileCodecStreamLZ4001 must satisfy FileCodecConcept");

} // namespace bcsv
