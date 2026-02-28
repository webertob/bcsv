/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file file_codec_stream001.h
 * @brief FileCodecStream001 — stream-raw file codec.
 *
 * Simplest file codec: writes BLE-prefixed uncompressed rows directly to
 * the output stream.  No packet structure, no footer, no crash recovery,
 * no random access.  Per-row XXH32 checksums provide data integrity.
 *
 * Intended for embedded hard-real-time recording where every CPU cycle
 * matters and crash recovery / random access are handled externally.
 *
 * Wire format:
 *   FileHeader
 *   BLE(row_len) | row_bytes | uint32_t(XXH32)    ← repeated for each row (row_len > 0)
 *   BLE(0)                                         ← ZoH repeat (no payload, no checksum)
 *   [EOF]
 *
 * Reader detects end-of-file via stream EOF or read failure.
 */

#include "file_codec_concept.h"
#include "byte_buffer.h"
#include "checksum.hpp"
#include "definitions.h"

#include "file_footer.h"
#include "vle.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <istream>
#include <span>
#include <stdexcept>

namespace bcsv {

class FileCodecStream001 {
public:
    FileCodecStream001() = default;

    // ── Setup ────────────────────────────────────────────────────────────

    void setupWrite(std::ostream& /*os*/, const FileHeader& /*header*/) {
        // Nothing to initialise — stateless.
    }

    void setupRead(std::istream& /*is*/, const FileHeader& /*header*/) {
        // Nothing to initialise — no packets to open.
    }

    // ── Write lifecycle ─────────────────────────────────────────────────

    bool beginWrite(std::ostream& /*os*/, uint64_t /*rowCnt*/) {
        // Stream codecs have no packet boundaries.
        return false;
    }

    void writeRow(std::ostream& os, std::span<const std::byte> rowData) {
        if (rowData.empty()) {
            // ZoH repeat: length = 0, no checksum
            writeRowLength(os, 0);
            return;
        }

        writeRowLength(os, rowData.size());
        os.write(reinterpret_cast<const char*>(rowData.data()),
                 static_cast<std::streamsize>(rowData.size()));

        // Per-row XXH32 checksum
        uint32_t hash = Checksum::compute32(rowData.data(), rowData.size());
        os.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
    }

    void finalize(std::ostream& /*os*/, uint64_t /*totalRows*/) {
        // Stream codecs have no footer.
    }

    ByteBuffer& writeBuffer() { return write_buffer_; }

    // ── Read lifecycle ──────────────────────────────────────────────────

    std::span<const std::byte> readRow(std::istream& is) {
        size_t rowLen = 0;
        try {
            vleDecode<uint64_t, true>(is, rowLen, nullptr);
        } catch (...) {
            // Stream ended or corrupt — signal EOF.
            return EOF_SENTINEL;
        }

        if (rowLen == 0) {
            // ZoH repeat — caller reuses previous row.
            return ZOH_REPEAT_SENTINEL;
        }

        if (rowLen > MAX_ROW_LENGTH) [[unlikely]] {
            throw std::runtime_error(
                "FileCodecStream001::readRow: row length exceeds MAX_ROW_LENGTH ("
                + std::to_string(rowLen) + " > " + std::to_string(MAX_ROW_LENGTH) + ")");
        }

        read_buffer_.resize(rowLen);
        is.read(reinterpret_cast<char*>(read_buffer_.data()),
                static_cast<std::streamsize>(rowLen));
        if (!is || is.gcount() != static_cast<std::streamsize>(rowLen)) {
            return EOF_SENTINEL;
        }

        // Read and verify per-row XXH32 checksum
        uint32_t expectedHash = 0;
        is.read(reinterpret_cast<char*>(&expectedHash), sizeof(expectedHash));
        if (!is || is.gcount() != static_cast<std::streamsize>(sizeof(expectedHash))) {
            throw std::runtime_error("FileCodecStream001::readRow: failed to read row checksum");
        }
        uint32_t actualHash = Checksum::compute32(read_buffer_.data(), rowLen);
        if (actualHash != expectedHash) {
            throw std::runtime_error("FileCodecStream001::readRow: row checksum mismatch");
        }

        return std::span<const std::byte>(read_buffer_.data(), rowLen);
    }

    // ── Boundary / state signals ────────────────────────────────────────

    bool packetBoundaryCrossed() const noexcept { return false; }
    void reset() noexcept {}

    /// Write a VLE-encoded row length to the stream.
    /// Also used by FileCodecStreamLZ4001.
    static void writeRowLength(std::ostream& os, size_t length) {
        uint64_t tempBuf;
        size_t numBytes = vleEncode<uint64_t, true>(length, &tempBuf, sizeof(tempBuf));
        os.write(reinterpret_cast<const char*>(&tempBuf),
                 static_cast<std::streamsize>(numBytes));
    }

private:
    ByteBuffer write_buffer_;   // Owned write buffer for RowCodec serialization
    ByteBuffer read_buffer_;    // Owned read buffer for decompressed row data
};

static_assert(FileCodecConcept<FileCodecStream001>,
              "FileCodecStream001 must satisfy FileCodecConcept");

} // namespace bcsv
