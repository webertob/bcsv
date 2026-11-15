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
 * @file packet_scan.h
 * @brief scanPackets() — the complete packets of a file, found by walking its framing.
 *
 * A file whose writer never reached close() (crash, SIGKILL, power loss) has
 * no footer and usually ends inside a packet. scanPackets() walks the packet
 * chain from the first packet at the exact offsets the framing gives — no
 * searching for magic bytes, no size estimates — and stops at the first spot
 * that does not start a complete packet: the footer, the end of the file, a
 * cut or damaged packet, or a header whose first row does not continue the
 * row count.
 *
 * It is the one definition of "the rows a file holds" without a footer:
 * ReaderDirectAccess indexes its result when it rebuilds a footer
 * (open(path, true), Python Reader.count_rows(), C bcsv_reader_open_ex()),
 * and bcsvRepair writes its result into the new footer.
 *
 * Complete means every byte of the packet is present and its framing is
 * intact. The last complete packet is always verified (checksum, row count
 * from the payload), because its row count can come from nowhere else. A
 * batch-LZ4 packet followed by another packet is hopped over by its declared
 * sizes and its row count taken from the next header, unless `verifyAll` is
 * set; row-framed packets (packet, packet_lz4) are always checksummed, since
 * walking them reads every byte anyway.
 */

#include "file_codec_packet001.h"
#ifdef BCSV_HAS_BATCH_CODEC
#include "file_codec_packet_lz4_batch001.h"
#endif
#include "../definitions.h"
#include "../file_footer.h"
#include "../file_header.h"
#include "../packet_header.h"

#include <cstdint>
#include <istream>

namespace bcsv {

/// Result of scanPackets().
struct PacketScanReport {
    PacketIndex index;            ///< complete packets, in file order
    uint64_t    rows = 0;         ///< rows in the complete packets
    uint64_t    end  = 0;         ///< offset just past the last complete packet (first packet offset if none)
    bool        has_tail = false; ///< a valid packet header follows that does not start a complete packet
    uint64_t    tail_offset = 0;  ///< offset of that packet's header; its first row is `rows`
    PacketScan  tail;             ///< what the walk found in it (rows: complete rows before the cut, row-framed codecs)
};

namespace detail {
    inline bool readPacketHeaderAt(std::istream& is, uint64_t offset, uint64_t fileSize,
                                   PacketHeader& header) {
        if (offset + sizeof(PacketHeader) > fileSize) return false;
        is.clear();
        is.seekg(static_cast<std::streamoff>(offset));
        return is.good() && header.read(is);
    }
} // namespace detail

/**
 * @brief Walk the packets of a packet-mode file (see the file comment).
 * @param is                 the file, opened in binary mode; position and state are not restored
 * @param header             its file header
 * @param firstPacketOffset  FileHeader::getBinarySize(layout): where the first packet starts
 * @param verifyAll          also decompress and checksum batch packets that have a successor
 * @return the complete packets; empty for stream-mode files, which have no packets
 */
inline PacketScanReport scanPackets(std::istream& is, const FileHeader& header,
                                    uint64_t firstPacketOffset, bool verifyAll) {
    PacketScanReport report;
    report.end = firstPacketOffset;

    const FileCodecId codec = resolveFileCodecId(header.versionMinor(),
                                                 header.getCompressionLevel(), header.getFlags());
    if (codec == FileCodecId::STREAM_001 || codec == FileCodecId::STREAM_LZ4_001) {
        return report;
    }
    const bool batch = codec == FileCodecId::PACKET_LZ4_BATCH_001;
#ifndef BCSV_HAS_BATCH_CODEC
    if (batch) return report;   // this build cannot read batch packets at all
#endif

    is.clear();
    is.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(is.tellg());

    auto scanAt = [&](uint64_t offset, [[maybe_unused]] bool verify) -> PacketScan {
        const uint64_t payload = offset + sizeof(PacketHeader);
        is.clear();
        is.seekg(static_cast<std::streamoff>(payload));
#ifdef BCSV_HAS_BATCH_CODEC
        if (batch) {
            return FileCodecPacketLZ4Batch001::scanPacket(is, payload, fileSize,
                                                          header.getPacketSize(), verify);
        }
#endif
        return FileCodecPacket001::scanPacket(is, payload);
    };
    auto commit = [&](uint64_t offset, uint64_t rows, uint64_t end) {
        report.index.emplace_back(offset, report.rows);
        report.rows += rows;
        report.end   = end;
    };
    auto setTail = [&](uint64_t offset, const PacketScan& scan) {
        report.has_tail    = true;
        report.tail_offset = offset;
        report.tail        = scan;
    };

    // A batch packet hopped over without counting its rows waits here until
    // the next header gives its row count — or turns out to be the last one.
    bool     pending = false;
    uint64_t pendingOffset = 0, pendingFirstRow = 0, pendingEnd = 0;

    uint64_t offset = firstPacketOffset;
    while (true) {
        PacketHeader hdr;
        const bool hdrOk = detail::readPacketHeaderAt(is, offset, fileSize, hdr);

        if (pending) {
            pending = false;
            if (hdrOk && hdr.first_row_index >= pendingFirstRow) {
                commit(pendingOffset, hdr.first_row_index - pendingFirstRow, pendingEnd);
            } else {
                // The pending packet is the last one: count and verify it.
                const PacketScan last = scanAt(pendingOffset, true);
                if (last.complete) commit(pendingOffset, last.rows, last.end);
                else               setTail(pendingOffset, last);
                break;
            }
        }

        // Footer, end of file, a cut header or garbage — or a header that does
        // not continue the row count (not a packet of this chain).
        if (!hdrOk || hdr.first_row_index != report.rows) break;

        const PacketScan scan = scanAt(offset, verifyAll);
        if (!scan.complete) {
            setTail(offset, scan);
            break;
        }
        if (scan.counted) {
            commit(offset, scan.rows, scan.end);
        } else {
            pending         = true;
            pendingOffset   = offset;
            pendingFirstRow = hdr.first_row_index;
            pendingEnd      = scan.end;
        }
        offset = scan.end;
    }
    return report;
}

} // namespace bcsv
