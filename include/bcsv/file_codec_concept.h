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
 * @file file_codec_concept.h
 * @brief FileCodecConcept — compile-time interface for file-level codecs.
 *
 * A FileCodec encapsulates the file-level I/O strategy: how serialized row data
 * is framed (packet headers, terminators, checksums) and optionally compressed
 * before being written to / read from a binary stream.
 *
 * FileCodecs are orthogonal to RowCodecs (Flat/ZoH):
 *   RowCodec:  Row ⟷ raw bytes   (serialize / deserialize per row)
 *   FileCodec: raw bytes ⟷ file  (framing, compression, checksum, packet lifecycle)
 *
 * Architecture:
 *   Writer  — selects FileCodec at open() via FileCodecDispatch (runtime, one indirect call per row)
 *   Reader  — auto-selects FileCodec from file header (runtime dispatch)
 *   Writer hot-path cost is dominated by I/O + compression; a single indirect
 *   call (~2ns) is negligible.
 *
 * Five planned codecs:
 *   FileCodecStream001        — no packets, no compression, per-row XXH32 checksums (embedded hard-RT)
 *   FileCodecStreamLZ4001     — no packets, streaming LZ4, per-row XXH32 checksums
 *   FileCodecPacket001        — packet framing + checksums, no compression
 *   FileCodecPacketLZ4001     — packet framing + streaming LZ4 (v1.3.0 default)
 *   FileCodecPacketLZ4Batch001— packet framing + batch LZ4 + async (future)
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <iosfwd>

#include "byte_buffer.h"
#include "definitions.h"
#include "file_header.h"
#include "file_footer.h"

namespace bcsv {

/**
 * @brief Concept constraining file-level codec types.
 *
 * Any type satisfying FileCodecConcept can be used with FileCodecDispatch.
 * Each method corresponds to a function pointer slot in the dispatch table.
 *
 * Lifecycle (write side):
 *   1. setupWrite(os, header)        — once after Writer::open()
 *   2. beginWrite(os, rowCnt)        — before each row; handles packet lifecycle internally
 *   3. writeRow(os, rowData)         — write one serialized row
 *   4. finalize(os, totalRows)       — in Writer::close(); closes last packet, writes footer
 *
 * Lifecycle (read side):
 *   1. setupRead(is, header)         — once after Reader::open(); opens first packet if needed
 *   2. readRow(is)                   — read one row; handles packet transitions internally
 *
 * Buffer ownership:
 *   Codecs own their own write and read buffers. writeBuffer() provides
 *   the write buffer for RowCodec serialization.
 */
template<typename T>
concept FileCodecConcept = requires(T codec,
                                     const T ccodec,
                                     std::ostream& os,
                                     std::istream& is,
                                     const FileHeader& header,
                                     std::span<const std::byte> rowData,
                                     uint64_t rowIndex)
{
    // ── Setup ───────────────────────────────────────────────────────────
    /// Configure the codec for writing.  Called once after Writer::open().
    { codec.setupWrite(os, header) };

    /// Configure the codec for reading.  Called once after Reader::open().
    /// For packet-based codecs, this also opens the first packet.
    { codec.setupRead(is, header) };

    // ── Write lifecycle ─────────────────────────────────────────────────
    /// Called before each writeRow().  Handles packet close/open internally.
    /// Returns true if a packet boundary was crossed (Writer resets RowCodec).
    /// Stream codecs always return false.
    { codec.beginWrite(os, rowIndex) } -> std::convertible_to<bool>;

    /// Write a single serialized (uncompressed) row to the output stream.
    /// Handles VLE length prefix, optional compression, optional checksum.
    { codec.writeRow(os, rowData) };

    /// Called once in Writer::close().  Closes any open packet and writes
    /// the file footer.  Stream codecs are no-ops (no footer).
    { codec.finalize(os, rowIndex) };

    /// Returns a reference to the codec's internal write buffer.
    /// Writer uses this to let RowCodec serialize into the codec's buffer.
    { codec.writeBuffer() } -> std::convertible_to<ByteBuffer&>;

    // ── Read lifecycle ──────────────────────────────────────────────────
    /// Read a single row from the input stream (codec owns the read buffer).
    /// Returns a span of the decompressed row data, or a sentinel span.
    /// Handles VLE decode, optional decompression, optional checksum,
    /// and packet boundary transitions.
    { codec.readRow(is) } -> std::convertible_to<std::span<const std::byte>>;

    // ── Boundary / state signals ────────────────────────────────────────
    /// True if the last readRow() call crossed a packet boundary.
    /// Reader uses this to reset RowCodec state at packet transitions.
    /// Stream codecs always return false.
    { ccodec.packetBoundaryCrossed() } -> std::convertible_to<bool>;

    /// Reset per-packet internal state (LZ4 context, checksum, counters).
    { codec.reset() };
};

/// Sentinel span signalling "ZoH repeat — reuse previous row".
/// Identity comparison: `result.data() == ZOH_REPEAT_SENTINEL.data()`.
inline const std::byte ZOH_REPEAT_TAG{0};
inline const std::span<const std::byte> ZOH_REPEAT_SENTINEL{&ZOH_REPEAT_TAG, 0};

/// Sentinel span signalling "end of file / no more rows".
/// Identity comparison: `result.data() == EOF_SENTINEL.data()`.
inline const std::byte EOF_TAG{0};
inline const std::span<const std::byte> EOF_SENTINEL{&EOF_TAG, 0};

} // namespace bcsv
