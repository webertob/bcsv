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
 * @file file_codec_packet001.h
 * @brief FileCodecPacket001 — packet-raw file codec.
 *
 * Packet-structured file codec without compression.
 * Writes rows with BLE length prefix inside packets that carry a checksum
 * (xxHash64) and terminator.  Provides crash recovery (read up to last
 * fully written packet) and random access (via PacketIndex / FileFooter).
 *
 * Wire format per packet:
 *   PacketHeader (16 bytes)
 *   BLE(row_len) | row_bytes      ← repeated
 *   BLE(PCKT_TERMINATOR)
 *   uint64_t payload_checksum     ← xxHash64 of (VLE lengths + row payloads)
 *
 * Intended for embedded platforms that need packet framing (crash recovery,
 * random access) but cannot afford LZ4 compression CPU cost.
 */

#include "file_codec_concept.h"
#include "byte_buffer.h"
#include "checksum.hpp"
#include "definitions.h"
#include "file_header.h"
#include "file_footer.h"
#include "packet_header.h"
#include "vle.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <istream>
#include <span>
#include <stdexcept>
#include <string>

namespace bcsv {

class FileCodecPacket001 {
public:
    FileCodecPacket001() = default;

    // ── Setup ────────────────────────────────────────────────────────────

    void setupWrite(std::ostream& /*os*/, const FileHeader& header) {
        packet_size_limit_ = header.getPacketSize();
        build_index_ = !header.hasFlag(FileFlags::NO_FILE_INDEX);
        packet_index_.clear();
    }

    void setupRead(std::istream& is, const FileHeader& header) {
        packet_size_limit_ = header.getPacketSize();
        // Open first packet for reading (absorbed from former openFirstPacketForRead).
        // May be false if the file is empty (footer immediately after file header).
        packet_open_ = openPacketRead(is);
    }

    // ── Write lifecycle ─────────────────────────────────────────────────

    /// Called before each writeRow().  Handles close-if-full → open-if-needed.
    /// Returns true if a packet boundary was crossed (caller resets RowCodec).
    bool beginWrite(std::ostream& os, uint64_t rowCnt) {
        // Close current packet if full
        if (packet_open_ && packet_size_ >= packet_size_limit_) {
            closePacket(os);
        }

        // Open new packet if needed
        if (!packet_open_) {
            openPacket(os, rowCnt);
            // Signal boundary crossing except for the very first packet
            return rowCnt > 0;
        }

        return false;
    }

    void writeRow(std::ostream& os, std::span<const std::byte> rowData) {
        if (rowData.empty()) {
            // ZoH repeat: length = 0
            writeRowLengthChecksummed(os, 0);
            return;
        }

        writeRowLengthChecksummed(os, rowData.size());
        os.write(reinterpret_cast<const char*>(rowData.data()),
                 static_cast<std::streamsize>(rowData.size()));
        packet_hash_.update(rowData);
        packet_size_ += rowData.size();
    }

    /// Finalize: close last packet and write the file footer.
    void finalize(std::ostream& os, uint64_t totalRows) {
        if (packet_open_) {
            closePacket(os);
        }
        FileFooter footer(packet_index_, totalRows);
        footer.write(os);
    }

    ByteBuffer& writeBuffer() { return write_buffer_; }

    // ── Read lifecycle ──────────────────────────────────────────────────

    std::span<const std::byte> readRow(std::istream& is) {
        packet_boundary_crossed_ = false;

        if (!packet_open_) {
            return EOF_SENTINEL;
        }

        // Read row length (VLE with checksum update)
        size_t rowLen = 0;
        try {
            vleDecode<uint64_t, true>(is, rowLen, &packet_hash_);
        } catch (...) {
            return EOF_SENTINEL;
        }

        // Handle packet terminator → try to open next packet
        while (rowLen == PCKT_TERMINATOR) {
            closePacketRead(is);
            packet_open_ = openPacketRead(is);
            if (!packet_open_) {
                return EOF_SENTINEL;
            }
            packet_boundary_crossed_ = true;
            try {
                vleDecode<uint64_t, true>(is, rowLen, &packet_hash_);
            } catch (...) {
                return EOF_SENTINEL;
            }
        }

        if (rowLen == 0) {
            // ZoH repeat — Reader validates the ZERO_ORDER_HOLD flag.
            return ZOH_REPEAT_SENTINEL;
        }

        if (rowLen > MAX_ROW_LENGTH) [[unlikely]] {
            throw std::runtime_error(
                "FileCodecPacket001::readRow: row length exceeds MAX_ROW_LENGTH ("
                + std::to_string(rowLen) + " > " + std::to_string(MAX_ROW_LENGTH) + ")");
        }

        read_buffer_.resize(rowLen);
        is.read(reinterpret_cast<char*>(read_buffer_.data()),
                static_cast<std::streamsize>(rowLen));
        if (!is || is.gcount() != static_cast<std::streamsize>(rowLen)) {
            throw std::runtime_error("FileCodecPacket001::readRow: failed to read row data");
        }
        packet_hash_.update(read_buffer_);

        return std::span<const std::byte>(read_buffer_.data(), rowLen);
    }

    // ── Boundary / state signals ────────────────────────────────────────

    /// True if the last readRow() call crossed a packet boundary.
    /// Reader uses this to reset RowCodec state at packet transitions.
    bool packetBoundaryCrossed() const noexcept {
        return packet_boundary_crossed_;
    }

    void reset() noexcept {
        packet_hash_.reset();
        packet_size_ = 0;
    }

protected:
    // ── Internal packet lifecycle ───────────────────────────────────────

    void openPacket(std::ostream& os, uint64_t firstRowIndex) {
        assert(!packet_open_);

        if (build_index_) {
            size_t offset = static_cast<size_t>(os.tellp());
            packet_index_.emplace_back(offset, firstRowIndex);
        }

        PacketHeader::write(os, firstRowIndex);

        packet_size_ = 0;
        packet_hash_.reset();
        packet_open_ = true;
    }

    void closePacket(std::ostream& os) {
        if (!packet_open_) return;

        // Write packet terminator
        writeRowLengthChecksummed(os, PCKT_TERMINATOR);

        // Write payload checksum
        uint64_t hash = packet_hash_.finalize();
        os.write(reinterpret_cast<const char*>(&hash), sizeof(hash));

        packet_open_ = false;
    }

    // ── Helpers ─────────────────────────────────────────────────────────

    /// Write VLE-encoded length to stream, updating checksum and packet size.
    void writeRowLengthChecksummed(std::ostream& os, size_t length) {
        uint64_t tempBuf;
        size_t numBytes = vleEncode<uint64_t, true>(length, &tempBuf, sizeof(tempBuf));
        os.write(reinterpret_cast<const char*>(&tempBuf),
                 static_cast<std::streamsize>(numBytes));
        packet_hash_.update(reinterpret_cast<const char*>(&tempBuf), numBytes);
        packet_size_ += numBytes;
    }

    /// Open next packet for sequential reading.
    bool openPacketRead(std::istream& is) {
        packet_pos_ = is.tellg();

        PacketHeader header;
        bool success = header.read(is, true);

        packet_hash_.reset();

        if (!success) {
            if (header.magic == FOOTER_BIDX_MAGIC) {
                is.clear();
                is.seekg(packet_pos_);
                return false;   // End of data — footer reached
            } else if (is.eof()) {
                return false;   // End of file
            } else {
                is.clear();
                is.seekg(packet_pos_);
                throw std::runtime_error("FileCodecPacket001: Failed to read packet header");
            }
        }
        return true;
    }

    /// Close current packet on read side: validate payload checksum.
    void closePacketRead(std::istream& is) {
        uint64_t expectedHash = 0;
        is.read(reinterpret_cast<char*>(&expectedHash), sizeof(expectedHash));
        if (!is || is.gcount() != sizeof(expectedHash)) {
            throw std::runtime_error("FileCodecPacket001: Failed to read packet checksum");
        }

        uint64_t calculatedHash = packet_hash_.finalize();
        if (calculatedHash != expectedHash) {
            throw std::runtime_error("FileCodecPacket001: Packet checksum mismatch");
        }
    }

    // ── State ───────────────────────────────────────────────────────────
    ByteBuffer              write_buffer_;      // Owned write buffer for RowCodec serialization
    ByteBuffer              read_buffer_;       // Owned read buffer for row data
    Checksum::Streaming     packet_hash_;
    bool                    packet_open_{false};
    bool                    packet_boundary_crossed_{false};
    size_t                  packet_size_{0};
    size_t                  packet_size_limit_{MIN_PACKET_SIZE};
    bool                    build_index_{true};
    PacketIndex             packet_index_;
    std::streampos          packet_pos_{0};
};

static_assert(FileCodecConcept<FileCodecPacket001>,
              "FileCodecPacket001 must satisfy FileCodecConcept");

} // namespace bcsv
