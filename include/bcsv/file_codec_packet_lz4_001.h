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
 * @file file_codec_packet_lz4_001.h
 * @brief FileCodecPacketLZ4001 — packet-LZ4-streaming file codec.
 *
 * Packet-structured file codec with per-row streaming LZ4 compression.
 * This is the v1.3.0 default codec — produces the identical wire format.
 *
 * Each row is individually LZ4-compressed within a continuous streaming
 * context that resets at packet boundaries.  Packet headers, checksums
 * and terminators provide crash recovery and random access.
 *
 * Wire format per packet:
 *   PacketHeader (16 bytes)
 *   BLE(compressed_len) | lz4_block     ← repeated for each row
 *   BLE(PCKT_TERMINATOR)
 *   uint64_t payload_checksum           ← xxHash64 of all (VLE + compressed data)
 *
 * Derives from FileCodecPacket001 — adds LZ4 compression/decompression
 * around the raw packet I/O path.
 */

#include "file_codec_packet001.h"
#include "lz4_stream.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <ostream>
#include <istream>
#include <span>

namespace bcsv {

class FileCodecPacketLZ4001 {
public:
    FileCodecPacketLZ4001() = default;

    // Non-copyable — LZ4 streams contain internal ring-buffer pointers.
    FileCodecPacketLZ4001(const FileCodecPacketLZ4001&) = delete;
    FileCodecPacketLZ4001& operator=(const FileCodecPacketLZ4001&) = delete;
    FileCodecPacketLZ4001(FileCodecPacketLZ4001&&) = delete;
    FileCodecPacketLZ4001& operator=(FileCodecPacketLZ4001&&) = delete;

    // ── Setup ────────────────────────────────────────────────────────────

    void setupWrite(std::ostream& os, const FileHeader& header) {
        packet_.setupWrite(os, header);
        int acceleration = 10 - static_cast<int>(header.getCompressionLevel());
        lz4_compress_.emplace(64 * 1024, acceleration);
    }

    void setupRead(std::istream& is, const FileHeader& header) {
        packet_.setupRead(is, header);  // Opens first packet
        lz4_decompress_.emplace();
    }

    // ── Write lifecycle ─────────────────────────────────────────────────

    bool beginWrite(std::ostream& os, uint64_t rowCnt) {
        bool boundary = packet_.beginWrite(os, rowCnt);
        if (boundary && lz4_compress_.has_value()) {
            lz4_compress_->reset();  // Reset LZ4 context at packet boundary
        }
        return boundary;
    }

    void writeRow(std::ostream& os, std::span<const std::byte> rowData) {
        if (rowData.empty()) {
            // ZoH repeat: delegate to packet codec (length = 0)
            packet_.writeRow(os, rowData);
            return;
        }

        // Compress the serialized row data
        assert(lz4_compress_.has_value());
        auto compressed = lz4_compress_->compressUseInternalBuffer(rowData);

        // Delegate to packet codec for VLE + checksum + I/O
        packet_.writeRow(os, compressed);
    }

    void finalize(std::ostream& os, uint64_t totalRows) {
        packet_.finalize(os, totalRows);
    }

    /// Flush: close the current packet, flush stream, open new packet.
    /// Resets LZ4 compression context at the boundary.
    /// Returns true if a packet boundary was crossed (caller resets RowCodec).
    bool flushPacket(std::ostream& os, uint64_t rowCnt) {
        bool boundary = packet_.flushPacket(os, rowCnt);
        if (boundary && lz4_compress_.has_value()) {
            lz4_compress_->reset();  // Reset LZ4 context for the new packet
        }
        return boundary;
    }

    ByteBuffer& writeBuffer() { return write_buffer_; }

    // ── Read lifecycle ──────────────────────────────────────────────────

    std::span<const std::byte> readRow(std::istream& is) {
        // Delegate to packet codec for VLE + checksum + packet boundaries
        auto result = packet_.readRow(is);

        // Check for sentinels (EOF or ZoH repeat) — nothing to decompress
        if (result.data() == EOF_SENTINEL.data() ||
            result.data() == ZOH_REPEAT_SENTINEL.data()) {
            return result;
        }

        // Reset LZ4 decompression context if a packet boundary was crossed
        if (packet_.packetBoundaryCrossed() && lz4_decompress_.has_value()) {
            lz4_decompress_->reset();
        }

        // Decompress
        assert(lz4_decompress_.has_value());
        return lz4_decompress_->decompress(result);
    }

    // ── Boundary / state signals ────────────────────────────────────────

    bool packetBoundaryCrossed() const noexcept {
        return packet_.packetBoundaryCrossed();
    }

    void reset() noexcept {
        packet_.reset();
        if (lz4_compress_.has_value()) {
            lz4_compress_->reset();
        }
        if (lz4_decompress_.has_value()) {
            lz4_decompress_->reset();
        }
    }

    /// Seek to a specific packet by absolute file offset and prepare for reading.
    /// Resets LZ4 decompression context and delegates to inner packet codec.
    bool seekToPacket(std::istream& is, std::streamoff offset) {
        // Reset LZ4 decompression context for the new packet
        if (lz4_decompress_.has_value()) {
            lz4_decompress_->reset();
        }
        return packet_.seekToPacket(is, offset);
    }

private:
    ByteBuffer write_buffer_;   // Owned write buffer for RowCodec serialization
    FileCodecPacket001 packet_;         // Handles framing, checksums, packet lifecycle
    std::optional<LZ4CompressionStreamInternalBuffer<MAX_ROW_LENGTH>> lz4_compress_;
    std::optional<LZ4DecompressionStream<MAX_ROW_LENGTH>> lz4_decompress_;
};

static_assert(FileCodecConcept<FileCodecPacketLZ4001>,
              "FileCodecPacketLZ4001 must satisfy FileCodecConcept");

} // namespace bcsv
