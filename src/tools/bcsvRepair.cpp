/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file bcsvRepair.cpp
 * @brief CLI tool to repair BCSV files from interrupted write processes
 *
 * Recovers data from BCSV files where the writer was interrupted before
 * completing the write. Supports all five file codecs:
 *
 *   Packet-based (packet, packet_lz4, packet_lz4_batch):
 *     Walks packet-by-packet, rebuilds the packet index, attempts partial
 *     recovery of the last incomplete packet, and writes a valid footer.
 *
 *   Stream-based (stream, stream_lz4):
 *     Walks row-by-row using per-row XXH32 checksums, truncates at the
 *     last valid row. Stream files have no footer by design.
 *
 * Two output modes:
 *   Copy mode     (-o FILE)    Write repaired file to a new location
 *   In-place mode (--in-place) Truncate and append footer to the original file
 *
 * Note on LZ4 batch codec: The packet_lz4_batch codec wraps the entire packet
 * payload in a single LZ4 frame. If the frame is truncated, decompression fails
 * and zero rows can be recovered from that packet.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <bcsv/bcsv.h>
#include <bcsv/vle.hpp>
#include <bcsv/file_footer.h>
#include <bcsv/checksum.hpp>
#include "cli_common.h"

#ifdef __has_include
#  if __has_include(<unistd.h>)
#    include <unistd.h>
#    define BCSV_HAS_FTRUNCATE 1
#  endif
#endif

// ── Configuration ───────────────────────────────────────────────────

struct Config {
    std::string input_file;
    std::string output_file;                  // empty → no copy mode

    bool in_place   = false;
    bool backup     = false;
    bool deep       = false;                  // validate payload checksums
    bool dry_run    = false;
    bool json       = false;
    bool verbose    = false;
    bool help       = false;
};

// ── Repair result ───────────────────────────────────────────────────

struct RepairResult {
    size_t   packets_found      = 0;          // total valid packets discovered
    size_t   packets_discarded  = 0;          // packets with bad payload checksums or truncated
    uint64_t rows_recovered     = 0;          // total rows in valid packets (or valid stream rows)
    uint64_t rows_recovered_partial = 0;      // rows recovered from broken last packet
    uint64_t rows_discarded     = 0;          // rows lost (stream: from cut point; packet: in discarded packets)
    uint64_t file_size_original = 0;
    uint64_t bytes_trimmed      = 0;          // bytes removed (broken tail)
    uint64_t footer_size        = 0;          // bytes of new footer written
    bool     had_damage         = false;      // any damage detected
    bool     had_footer         = false;      // original file had a valid footer
    bool     is_stream_mode     = false;      // file uses stream codec
    std::string error;                        // fatal error message, empty on success
    std::vector<std::string> warnings;        // non-fatal issues
};

// ── Usage ───────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog
        << " -i INPUT [-o OUTPUT | --in-place] [OPTIONS]\n\n"

        << "Repair BCSV files from interrupted write processes.\n\n"
        << "Packet-mode files (packet, packet_lz4, packet_lz4_batch):\n"
        << "  Walks packet-by-packet, rebuilds the packet index, attempts partial\n"
        << "  recovery of the last incomplete packet, and writes a valid footer.\n\n"
        << "Stream-mode files (stream, stream_lz4):\n"
        << "  Walks row-by-row using per-row XXH32 checksums, truncates at the\n"
        << "  last valid row. Stream files have no footer by design.\n\n"

        << "Output (one required unless --dry-run):\n"
        << "  -o, --output FILE      Write repaired file to OUTPUT (copy mode)\n"
        << "  --in-place             Modify input file directly (truncate + append footer)\n\n"

        << "Options:\n"
        << "  -i, --input FILE       Input BCSV file (required)\n"
        << "  --backup               With --in-place: copy original to FILE.bak first\n"
        << "  --deep                 Validate packet payload checksums (slower, more thorough)\n"
        << "                         Auto-enabled for --dry-run\n"
        << "  --dry-run              Analyze only — report damage without modifying anything\n"
        << "  --json                 Machine-readable JSON output to stdout\n"
        << "  -v, --verbose          Print progress (packets scanned, rows counted)\n"
        << "  -h, --help             Show this help message\n\n"

        << "Exit codes:\n"
        << "  0  Repair successful (or file was already valid)\n"
        << "  1  Repair failed or file is not repairable\n"
        << "  2  Argument error\n\n"

        << "Notes:\n"
        << "  The packet_lz4_batch codec wraps entire packets in a single LZ4 frame;\n"
        << "  if the frame is truncated, zero rows can be recovered from that packet.\n"
        << "  Non-batch packet codecs attempt partial recovery of rows from the\n"
        << "  last broken packet (walks rows until first invalid VLE/data boundary).\n\n"

        << "Examples:\n"
        << "  " << prog << " -i broken.bcsv --dry-run\n"
        << "  " << prog << " -i broken.bcsv -o repaired.bcsv\n"
        << "  " << prog << " -i broken.bcsv --in-place --backup\n"
        << "  " << prog << " -i broken.bcsv -o repaired.bcsv --deep --verbose\n";
}

// ── Argument parsing ────────────────────────────────────────────────

static Config parseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            cfg.help = true;
            return cfg;
        } else if (arg == "-v" || arg == "--verbose") {
            cfg.verbose = true;
        } else if (arg == "--json") {
            cfg.json = true;
        } else if (arg == "--deep") {
            cfg.deep = true;
        } else if (arg == "--dry-run") {
            cfg.dry_run = true;
        } else if (arg == "--in-place") {
            cfg.in_place = true;
        } else if (arg == "--backup") {
            cfg.backup = true;
        } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            cfg.input_file = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            cfg.output_file = argv[++i];
        } else if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + arg);
        } else {
            if (cfg.input_file.empty())
                cfg.input_file = arg;
            else
                throw std::runtime_error("Too many positional arguments.");
        }
    }

    if (cfg.help) return cfg;

    if (cfg.input_file.empty()) {
        throw std::runtime_error("Input file is required (-i FILE).");
    }

    if (!cfg.dry_run && cfg.output_file.empty() && !cfg.in_place) {
        throw std::runtime_error(
            "Output mode required: use -o FILE (copy) or --in-place, or --dry-run to analyze only.");
    }

    if (!cfg.output_file.empty() && cfg.in_place) {
        throw std::runtime_error("-o and --in-place are mutually exclusive.");
    }

    if (cfg.backup && !cfg.in_place) {
        throw std::runtime_error("--backup is only valid with --in-place.");
    }

    // Auto-enable deep validation for dry-run
    if (cfg.dry_run) {
        cfg.deep = true;
    }

    return cfg;
}

// ── JSON helpers ────────────────────────────────────────────────────

static std::string jsonStr(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    out += '"';
    return out;
}

// ── Packet payload validation ───────────────────────────────────────

/// Information about a validated packet
struct PacketInfo {
    uint64_t byte_offset;       // file offset of packet header
    uint64_t first_row;         // first row index
    uint64_t end_offset;        // file offset just past the payload checksum (end of packet)
    uint64_t row_count;         // rows in this packet (only set for last packet or deep mode)
    bool     is_valid;          // payload checksum validated OK
    bool     is_last_scanned;   // was this the last packet we could fully scan?
    std::string error_detail;   // error info if invalid
};

/**
 * @brief Count rows in a packet by walking VLE-encoded row lengths.
 *
 * Reads the entire payload into memory in one bulk read, then walks
 * VLE-encoded row lengths in-memory until the PCKT_TERMINATOR is found
 * or the buffer is exhausted (truncated packet).
 *
 * @param is         Input stream positioned after the PacketHeader
 * @param isBatch    true if this is a batch-LZ4 packet (different payload structure)
 * @param[out] rowCount  Number of complete rows found
 * @param[out] endOffset File offset just past the packet's trailing checksum
 * @param[out] payloadValid true if the terminator was found (packet is complete)
 * @return true if at least the terminator was found (complete packet)
 */
static bool walkPacketPayload(std::ifstream& is, bool isBatch,
                               size_t& rowCount, uint64_t& endOffset,
                               bool& payloadValid) {
    rowCount = 0;
    payloadValid = false;

    if (isBatch) {
        // Batch LZ4: [uint32 uncompressed_size][uint32 compressed_size][compressed_data][uint64 checksum]
        uint32_t uncompressed_size = 0;
        uint32_t compressed_size = 0;
        is.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));
        if (!is || is.gcount() != sizeof(uncompressed_size)) return false;
        is.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
        if (!is || is.gcount() != sizeof(compressed_size)) return false;

        if (uncompressed_size > bcsv::MAX_PACKET_SIZE || compressed_size > bcsv::MAX_PACKET_SIZE) {
            return false;
        }

        // Skip compressed data + checksum
        is.seekg(static_cast<std::streamoff>(compressed_size), std::ios::cur);
        if (!is) return false;
        uint64_t checksum = 0;
        is.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
        if (!is || is.gcount() != sizeof(checksum)) return false;

        endOffset = static_cast<uint64_t>(is.tellg());
        payloadValid = true;
        return true;
    }

    // Non-batch packet: bulk-read payload, walk VLE in memory.
    // Read up to MAX_PACKET_SIZE (1 GB) — actual allocation bounded by remaining file data.
    uint64_t start_pos = static_cast<uint64_t>(is.tellg());
    constexpr size_t MAX_READ = bcsv::MAX_PACKET_SIZE;

    // Determine how much data is left in the file
    is.seekg(0, std::ios::end);
    uint64_t file_end = static_cast<uint64_t>(is.tellg());
    is.seekg(static_cast<std::streamoff>(start_pos));

    size_t to_read = static_cast<size_t>(std::min(static_cast<uint64_t>(MAX_READ), file_end - start_pos));
    std::vector<char> buf(to_read);
    is.read(buf.data(), static_cast<std::streamsize>(to_read));
    size_t got = static_cast<size_t>(is.gcount());
    if (got == 0) return false;

    // Walk VLE rows in the buffer
    size_t pos = 0;
    while (pos < got) {
        uint64_t rowLen = 0;
        size_t vle_bytes = 0;
        try {
            vle_bytes = bcsv::vleDecode<uint64_t, true>(
                rowLen, reinterpret_cast<const std::byte*>(buf.data() + pos), got - pos);
        } catch (...) {
            return false;  // truncated VLE
        }

        if (rowLen == bcsv::PCKT_TERMINATOR) {
            pos += vle_bytes;
            // Trailing checksum (xxHash64)
            if (pos + sizeof(uint64_t) > got) return false;
            pos += sizeof(uint64_t);
            endOffset = start_pos + pos;
            payloadValid = true;
            return true;
        }

        if (rowLen == 0) {
            pos += vle_bytes;
            rowCount++;
        } else {
            if (rowLen > bcsv::MAX_ROW_LENGTH) return false;
            pos += vle_bytes + static_cast<size_t>(rowLen);
            if (pos > got) return false;  // truncated row data
            rowCount++;
        }
    }
    return false;  // EOF before terminator
}

/**
 * @brief Deep-validate a packet's payload checksum (xxHash64).
 *
 * For non-batch packets: reads all VLE lengths and row data, feeds them
 * to a streaming xxHash64, then compares against the stored checksum.
 *
 * For batch packets: reads the compressed block, decompresses it, and
 * validates the stored checksum against xxHash64 of the uncompressed data.
 * Also counts rows by walking the decompressed payload.
 *
 * @param is        Input stream positioned just after the PacketHeader
 * @param isBatch   true if batch-LZ4 codec
 * @param[out] rowCount  Number of rows found (set on success)
 * @param[out] endOffset File offset after the packet's trailing checksum
 * @return true if checksum matches
 */
static bool deepValidatePacket(std::ifstream& is, bool isBatch,
                                size_t& rowCount, uint64_t& endOffset) {
    rowCount = 0;

    if (isBatch) {
        // Read sizes
        uint32_t uncompressed_size = 0;
        uint32_t compressed_size = 0;
        is.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));
        if (!is || is.gcount() != sizeof(uncompressed_size)) return false;
        is.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
        if (!is || is.gcount() != sizeof(compressed_size)) return false;

        if (uncompressed_size > bcsv::MAX_PACKET_SIZE || compressed_size > bcsv::MAX_PACKET_SIZE) {
            return false;
        }

        // Read compressed block
        std::vector<char> compressed(compressed_size);
        is.read(compressed.data(), compressed_size);
        if (!is || is.gcount() != static_cast<std::streamsize>(compressed_size)) return false;

        // Read expected checksum
        uint64_t expected_checksum = 0;
        is.read(reinterpret_cast<char*>(&expected_checksum), sizeof(expected_checksum));
        if (!is || is.gcount() != sizeof(expected_checksum)) return false;
        endOffset = static_cast<uint64_t>(is.tellg());

        // Decompress
        std::vector<char> decompressed(uncompressed_size);
        int decompBytes = LZ4_decompress_safe(compressed.data(), decompressed.data(),
                                               static_cast<int>(compressed_size),
                                               static_cast<int>(uncompressed_size));
        if (decompBytes < 0 || static_cast<uint32_t>(decompBytes) != uncompressed_size) {
            return false;  // LZ4 decompression failed
        }

        // Validate checksum on uncompressed data
        uint64_t actual_checksum = XXH64(decompressed.data(), uncompressed_size, 0);
        if (actual_checksum != expected_checksum) return false;

        // Count rows in decompressed payload by walking VLE
        std::span<std::byte> buf(reinterpret_cast<std::byte*>(decompressed.data()), uncompressed_size);
        while (!buf.empty()) {
            uint64_t rowLen = 0;
            try {
                size_t consumed = bcsv::vleDecode<uint64_t, true>(rowLen, buf.data(), buf.size());
                buf = buf.subspan(consumed);
            } catch (...) {
                break;
            }

            if (rowLen == bcsv::PCKT_TERMINATOR) break;
            if (rowLen == 0) {
                rowCount++;
            } else {
                if (rowLen > buf.size()) break;
                buf = buf.subspan(static_cast<size_t>(rowLen));
                rowCount++;
            }
        }
        return true;
    }

    // Non-batch: bulk-read payload into memory, walk VLE rows and compute hash
    uint64_t start_pos = static_cast<uint64_t>(is.tellg());

    // Determine remaining file data
    is.seekg(0, std::ios::end);
    uint64_t file_end = static_cast<uint64_t>(is.tellg());
    is.seekg(static_cast<std::streamoff>(start_pos));

    size_t to_read = static_cast<size_t>(
        std::min(static_cast<uint64_t>(bcsv::MAX_PACKET_SIZE), file_end - start_pos));
    std::vector<char> buf(to_read);
    is.read(buf.data(), static_cast<std::streamsize>(to_read));
    size_t got = static_cast<size_t>(is.gcount());
    if (got == 0) return false;

    // Walk VLE rows in buffer, computing streaming xxHash64
    bcsv::Checksum::Streaming hash;
    size_t pos = 0;
    while (pos < got) {
        const auto* ptr = reinterpret_cast<const std::byte*>(buf.data() + pos);
        size_t avail = got - pos;

        uint64_t rowLen = 0;
        size_t vle_bytes = 0;
        try {
            vle_bytes = bcsv::vleDecode<uint64_t, true>(rowLen, ptr, avail);
        } catch (...) {
            return false;
        }

        // Feed VLE bytes to hash
        hash.update(buf.data() + pos, vle_bytes);

        if (rowLen == bcsv::PCKT_TERMINATOR) {
            pos += vle_bytes;
            // Trailing checksum (xxHash64)
            if (pos + sizeof(uint64_t) > got) return false;
            uint64_t expected_checksum = 0;
            std::memcpy(&expected_checksum, buf.data() + pos, sizeof(expected_checksum));
            pos += sizeof(uint64_t);
            endOffset = start_pos + pos;
            return hash.finalize() == expected_checksum;
        }

        if (rowLen == 0) {
            pos += vle_bytes;
            rowCount++;
        } else {
            if (rowLen > bcsv::MAX_ROW_LENGTH) return false;
            size_t row_end = pos + vle_bytes + static_cast<size_t>(rowLen);
            if (row_end > got) return false;  // truncated
            // Feed row data to hash
            hash.update(buf.data() + pos + vle_bytes, static_cast<size_t>(rowLen));
            pos = row_end;
            rowCount++;
        }
    }
    return false;
}

// ── Stream row walking ──────────────────────────────────────────────

/**
 * @brief Walk a stream file row by row, verifying per-row XXH32 checksums.
 *
 * Wire format per row:  VLE(row_len) | row_bytes | uint32_t(XXH32)
 * ZoH repeat:           VLE(0)       (no payload, no checksum)
 *
 * Uses bulk I/O with a sliding buffer. Rows are validated in-memory;
 * individual read() calls only happen at buffer refill boundaries.
 *
 * @param is           Input stream positioned at the first row (right after FileHeader)
 * @param[out] validRows   Number of rows that passed validation
 * @param[out] cutOffset   File offset just past the last valid row
 * @param verbose      Print per-row progress
 * @return true always (cutOffset marks the last valid position)
 */
static bool walkStreamRows(std::ifstream& is, uint64_t& validRows,
                            uint64_t& cutOffset, bool verbose) {
    validRows = 0;
    cutOffset = static_cast<uint64_t>(is.tellg());

    // Initial 1 MB buffer; grows dynamically if a row exceeds capacity.
    // MAX_ROW_LENGTH is ~16 MB, so worst case we resize once to ~17 MB.
    size_t buf_cap = 1024 * 1024;
    std::vector<char> buffer(buf_cap);
    size_t buf_len = 0;
    size_t buf_pos = 0;
    uint64_t buf_file_offset = cutOffset;
    bool eof_reached = false;

    // Shift remaining data to front and read more from disk
    auto refill = [&]() {
        if (eof_reached) return;
        size_t remaining = buf_len - buf_pos;
        if (buf_pos > 0 && remaining > 0) {
            std::memmove(buffer.data(), buffer.data() + buf_pos, remaining);
        }
        buf_file_offset += buf_pos;
        buf_len = remaining;
        buf_pos = 0;
        size_t to_read = buf_cap - buf_len;
        if (to_read > 0) {
            is.read(buffer.data() + buf_len, static_cast<std::streamsize>(to_read));
            auto got = static_cast<size_t>(is.gcount());
            buf_len += got;
            if (got < to_read) eof_reached = true;
        }
    };

    // Grow buffer to fit at least `needed` bytes from buf_pos, then refill
    auto growAndRefill = [&](size_t needed) {
        if (needed <= buf_cap) return;
        buf_cap = needed + 1024 * 1024;  // add 1 MB headroom
        buffer.resize(buf_cap);
        refill();
    };

    refill();

    while (buf_pos < buf_len) {
        size_t avail = buf_len - buf_pos;
        const auto* ptr = reinterpret_cast<const std::byte*>(buffer.data() + buf_pos);

        // Decode VLE row length
        uint64_t rowLen = 0;
        size_t vle_bytes = 0;
        try {
            vle_bytes = bcsv::vleDecode<uint64_t, true>(rowLen, ptr, avail);
        } catch (...) {
            // Possibly truncated VLE at buffer boundary — try refill
            if (!eof_reached && avail < 10) {
                refill();
                avail = buf_len - buf_pos;
                ptr = reinterpret_cast<const std::byte*>(buffer.data() + buf_pos);
                try {
                    vle_bytes = bcsv::vleDecode<uint64_t, true>(rowLen, ptr, avail);
                } catch (...) {
                    break;
                }
            } else {
                break;
            }
        }

        if (rowLen == 0) {
            // ZoH repeat — no payload, no checksum
            buf_pos += vle_bytes;
            cutOffset = buf_file_offset + buf_pos;
            validRows++;
            continue;
        }

        if (rowLen > bcsv::MAX_ROW_LENGTH) break;

        // Total bytes for this row: VLE + payload + XXH32
        size_t row_total = vle_bytes + static_cast<size_t>(rowLen) + sizeof(uint32_t);

        // Ensure the full row is in the buffer
        if (buf_pos + row_total > buf_len) {
            if (!eof_reached) {
                growAndRefill(row_total);
                if (buf_pos + row_total > buf_len) break;  // still truncated
            } else {
                break;  // truncated
            }
        }

        // Validate XXH32 checksum
        const char* row_data = buffer.data() + buf_pos + vle_bytes;
        uint32_t expected_hash = 0;
        std::memcpy(&expected_hash, row_data + rowLen, sizeof(expected_hash));
        uint32_t actual_hash = bcsv::Checksum::compute32(row_data, static_cast<size_t>(rowLen));

        if (actual_hash != expected_hash) {
            if (verbose) {
                std::cerr << "    Row " << validRows
                          << " @ offset " << (buf_file_offset + buf_pos)
                          << ": checksum mismatch\n";
            }
            break;
        }

        buf_pos += row_total;
        cutOffset = buf_file_offset + buf_pos;
        validRows++;

        if (verbose && (validRows % 10000 == 0)) {
            std::cerr << "    " << validRows << " rows validated ...\r";
        }

        // Proactive refill when buffer is running low (but only if buffer is large enough)
        if (buf_len - buf_pos < std::min<size_t>(65536, buf_cap / 16) && !eof_reached) {
            refill();
        }
    }

    if (verbose && validRows >= 10000) {
        std::cerr << "\n";
    }
    return true;
}

// walkStreamLZ4Rows is identical to walkStreamRows — the XXH32 checksum
// covers the compressed block in both cases, so no separate function needed.

// ── Partial packet recovery ─────────────────────────────────────────

/**
 * @brief Attempt to recover valid rows from a broken (last) packet.
 *
 * For non-batch packets only. Reads the remaining file into memory and
 * walks VLE-encoded rows in-buffer, checking plausibility and data
 * boundaries. Stops at the first incomplete row.
 *
 * @param is           Input stream positioned just after the PacketHeader
 * @param[out] rowCount    Number of complete rows recovered
 * @param[out] endOffset   File offset just past the last valid row
 * @return true if at least one row was recovered
 */
static bool recoverPartialPacket(std::ifstream& is, size_t& rowCount,
                                  uint64_t& endOffset) {
    rowCount = 0;
    uint64_t start_pos = static_cast<uint64_t>(is.tellg());
    endOffset = start_pos;

    // Read remaining file data (capped at MAX_PACKET_SIZE)
    is.seekg(0, std::ios::end);
    uint64_t file_end = static_cast<uint64_t>(is.tellg());
    is.seekg(static_cast<std::streamoff>(start_pos));

    constexpr size_t MAX_READ = bcsv::MAX_PACKET_SIZE;
    size_t to_read = static_cast<size_t>(std::min(static_cast<uint64_t>(MAX_READ), file_end - start_pos));
    if (to_read == 0) return false;

    std::vector<char> buf(to_read);
    is.read(buf.data(), static_cast<std::streamsize>(to_read));
    size_t got = static_cast<size_t>(is.gcount());
    if (got == 0) return false;

    // Walk VLE rows in the buffer
    size_t pos = 0;
    while (pos < got) {
        uint64_t rowLen = 0;
        size_t vle_bytes = 0;
        try {
            vle_bytes = bcsv::vleDecode<uint64_t, true>(
                rowLen, reinterpret_cast<const std::byte*>(buf.data() + pos), got - pos);
        } catch (...) {
            break;  // Truncated VLE
        }

        if (rowLen == bcsv::PCKT_TERMINATOR) {
            // Packet was actually complete — read trailing checksum
            pos += vle_bytes;
            if (pos + sizeof(uint64_t) <= got) {
                pos += sizeof(uint64_t);
                endOffset = start_pos + pos;
            }
            return rowCount > 0;
        }

        if (rowLen == 0) {
            pos += vle_bytes;
            endOffset = start_pos + pos;
            rowCount++;
            continue;
        }

        if (rowLen > bcsv::MAX_ROW_LENGTH) break;

        // Check if row data fits
        size_t row_end = pos + vle_bytes + static_cast<size_t>(rowLen);
        if (row_end > got) break;  // Truncated row data

        pos = row_end;
        endOffset = start_pos + pos;
        rowCount++;
    }

    return rowCount > 0;
}

/**
 * @brief Compute the packet xxHash64 for the given byte range.
 *
 * Reads from startOffset to endOffset, feeding all bytes into a
 * streaming xxHash64. Used to produce a correct checksum when writing
 * a partial packet terminator.
 *
 * @param filepath      Path to the BCSV file
 * @param startOffset   First byte (right after PacketHeader)
 * @param endOffset     One past the last byte to hash
 * @param[out] vleTermBytes  VLE-encoded PCKT_TERMINATOR bytes (to append after hash)
 * @param[out] vleTermLen    Number of bytes in vleTermBytes
 * @return The finalized xxHash64 covering [startOffset..endOffset) + PCKT_TERMINATOR VLE
 */
static uint64_t computePartialPacketHash(const std::string& filepath,
                                          uint64_t startOffset, uint64_t endOffset,
                                          uint8_t* vleTermBytes, size_t& vleTermLen) {
    bcsv::Checksum::Streaming hash;

    // Feed all row data (VLE lengths + payloads) to the hash
    std::ifstream is(filepath, std::ios::binary);
    is.seekg(static_cast<std::streamoff>(startOffset));

    constexpr size_t BUF_SIZE = 64 * 1024;
    char buf[BUF_SIZE];
    uint64_t remaining = endOffset - startOffset;
    while (remaining > 0 && is) {
        size_t to_read = std::min(static_cast<size_t>(remaining), BUF_SIZE);
        is.read(buf, static_cast<std::streamsize>(to_read));
        auto got = is.gcount();
        if (got <= 0) break;
        hash.update(buf, static_cast<size_t>(got));
        remaining -= static_cast<uint64_t>(got);
    }

    // Encode PCKT_TERMINATOR as VLE and feed to hash
    uint64_t vle_buf;
    vleTermLen = bcsv::vleEncode<uint64_t, true>(bcsv::PCKT_TERMINATOR, &vle_buf, sizeof(vle_buf));
    std::memcpy(vleTermBytes, &vle_buf, vleTermLen);
    hash.update(vleTermBytes, vleTermLen);

    return hash.finalize();
}

// ── Output helpers ──────────────────────────────────────────────────

/// Copy exactly `count` bytes from src to dst using 1 MB chunks.
/// Returns empty string on success, error message on failure.
static std::string copyFileRange(std::ifstream& src, std::ostream& dst, uint64_t count) {
    constexpr size_t BUF_SIZE = 1024 * 1024;
    std::vector<char> buf(BUF_SIZE);
    uint64_t remaining = count;
    while (remaining > 0 && src && dst) {
        size_t to_read = std::min(static_cast<size_t>(remaining), BUF_SIZE);
        src.read(buf.data(), static_cast<std::streamsize>(to_read));
        auto got = src.gcount();
        if (got <= 0) break;
        dst.write(buf.data(), got);
        remaining -= static_cast<uint64_t>(got);
    }
    if (!dst) return "Write error during copy.";
    return {};
}

/// Create a backup and truncate the file to `offset` bytes.
/// Returns empty string on success, error message on failure.
static std::string backupAndTruncate(const std::string& path, uint64_t offset,
                                      bool do_backup, bool verbose) {
    if (do_backup) {
        std::string backup_path = path + ".bak";
        try {
            std::filesystem::copy_file(
                path, backup_path,
                std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& ex) {
            return std::string("Failed to create backup: ") + ex.what();
        }
        if (verbose) {
            std::cerr << "  Backup written to: " << backup_path << "\n";
        }
    } else {
        std::cerr << "Warning: --in-place modifies the file directly. "
                     "Consider --backup to keep a copy.\n";
    }

#ifdef BCSV_HAS_FTRUNCATE
    if (::truncate(path.c_str(), static_cast<off_t>(offset)) != 0) {
        return "ftruncate failed on: " + path;
    }
#else
    std::vector<char> valid_data(static_cast<size_t>(offset));
    {
        std::ifstream src(path, std::ios::binary);
        src.read(valid_data.data(), static_cast<std::streamsize>(offset));
        if (src.gcount() != static_cast<std::streamsize>(offset)) {
            return "Failed to read valid portion for in-place repair.";
        }
    }
    {
        std::ofstream dst(path, std::ios::binary | std::ios::trunc);
        dst.write(valid_data.data(), static_cast<std::streamsize>(offset));
        if (!dst) return "Failed to rewrite file during in-place repair.";
    }
#endif
    return {};
}

// ── Core repair analysis ────────────────────────────────────────────

/**
 * @brief Try to read a PacketHeader at the current stream position.
 *
 * Reads exactly at the given offset and validates magic + checksum.
 * Preferred over PacketHeader::readNext() for repair because sequential
 * walking is more predictable than heuristic scanning.
 */
static bool readPacketHeaderAt(std::ifstream& is, uint64_t offset, bcsv::PacketHeader& hdr) {
    is.clear();
    is.seekg(static_cast<std::streamoff>(offset));
    return is.good() && hdr.read(is);
}

/**
 * @brief Analyze a BCSV file and optionally repair it.
 *
 * Walks the file packet-by-packet from the start, validates packet
 * headers (and optionally payloads), builds a new packet index,
 * and reports the results.
 *
 * @param cfg     Parsed CLI configuration
 * @return RepairResult with analysis/repair outcome
 */
static RepairResult analyzeAndRepair(const Config& cfg) {
    RepairResult result;

    // Check file exists
    if (!std::filesystem::exists(cfg.input_file)) {
        result.error = "File does not exist: " + cfg.input_file;
        return result;
    }

    result.file_size_original = std::filesystem::file_size(cfg.input_file);

    // Open input file
    std::ifstream is(cfg.input_file, std::ios::binary);
    if (!is) {
        result.error = "Cannot open file: " + cfg.input_file;
        return result;
    }

    // Read and validate file header
    bcsv::FileHeader file_header;
    bcsv::Layout layout;
    try {
        file_header.readFromBinary(is, layout);
    } catch (const std::exception& ex) {
        result.error = std::string("Invalid BCSV header: ") + ex.what();
        return result;
    }

    if (!file_header.isValidMagic()) {
        result.error = "Not a BCSV file (bad magic number).";
        return result;
    }

    // Detect codec type from header flags
    const bool isStreamMode = file_header.hasFlag(bcsv::FileFlags::STREAM_MODE);
    const bool isCompressed = file_header.getCompressionLevel() > 0;
    const bool isBatch = file_header.hasFlag(bcsv::FileFlags::BATCH_COMPRESS);
    const size_t header_size = bcsv::FileHeader::getBinarySize(layout);

    result.is_stream_mode = isStreamMode;

    // Check if existing footer is already valid (packet-mode only)
    if (!isStreamMode) {
        bcsv::FileFooter existing_footer;
        std::ifstream check_stream(cfg.input_file, std::ios::binary);
        if (check_stream && existing_footer.read(check_stream)) {
            result.had_footer = true;
        }
    }

    // ─── Stream-mode repair path ────────────────────────────────────
    //
    // Variables shared with packet path for unified output:
    //   cut_offset            — truncation point (valid data ends here)
    //   need_partial_write    — write packet terminator before footer?
    //   partial_data_start    — payload start offset (for hash computation)
    //   partial_cut           — end of recovered rows in partial packet
    //   new_footer            — footer to append (empty for stream)
    //
    uint64_t cut_offset = header_size;
    bool need_partial_write = false;
    uint64_t partial_data_start = 0;
    uint64_t partial_cut = 0;
    bcsv::FileFooter new_footer;

    if (isStreamMode) {
        // Stream files: walk row-by-row using per-row XXH32 checksums
        uint64_t valid_rows = 0;

        is.clear();
        is.seekg(static_cast<std::streamoff>(header_size));

        // Both raw and LZ4 stream formats use the same wire layout for validation
        // (XXH32 covers the on-disk bytes in both cases)
        walkStreamRows(is, valid_rows, cut_offset, cfg.verbose);
        is.close();

        result.rows_recovered = valid_rows;

        // Detect damage: any trailing bytes beyond the last valid row?
        if (cut_offset < result.file_size_original) {
            result.had_damage = true;
            result.bytes_trimmed = result.file_size_original - cut_offset;
            result.warnings.push_back(
                "Trailing " + std::to_string(result.bytes_trimmed)
                + " bytes after last valid row (truncated/corrupt)");
        }

        if (cfg.verbose) {
            std::cerr << "  Stream mode (" << (isCompressed ? "LZ4" : "raw")
                      << "): " << valid_rows << " valid rows, cut @ offset " << cut_offset << "\n";
        }

        // Stream has no footer — new_footer stays empty.
        // Fall through to unified output phase.

    } else {

    // ─── Packet-mode repair path ────────────────────────────────────

    // --- Phase 1: Scan for packets ---
    std::vector<PacketInfo> packet_infos;

    uint64_t next_offset = header_size;

    while (true) {
        bcsv::PacketHeader pkt_header;
        if (!readPacketHeaderAt(is, next_offset, pkt_header)) {
            break;
        }

        PacketInfo info;
        info.byte_offset = next_offset;
        info.first_row   = pkt_header.first_row_index;
        info.is_valid    = true;
        info.row_count   = 0;
        info.end_offset  = 0;
        info.is_last_scanned = false;

        if (cfg.verbose) {
            std::cerr << "  Packet " << packet_infos.size()
                      << " @ offset " << info.byte_offset
                      << ", first_row=" << info.first_row << "\n";
        }

        // Position stream after packet header for payload inspection
        is.clear();
        is.seekg(static_cast<std::streamoff>(info.byte_offset) + sizeof(bcsv::PacketHeader));

        if (cfg.deep) {
            size_t rows = 0;
            uint64_t end = 0;
            bool ok = deepValidatePacket(is, isBatch, rows, end);
            info.row_count = rows;
            info.end_offset = end;
            if (!ok) {
                info.is_valid = false;
                info.error_detail = isBatch
                    ? "LZ4 batch decompression/checksum failed — entire packet discarded (0 rows recoverable)"
                    : "Packet payload checksum mismatch or truncated";
                if (cfg.verbose) {
                    std::cerr << "    INVALID: " << info.error_detail << "\n";
                }
                result.warnings.push_back(
                    "Packet " + std::to_string(packet_infos.size()) + ": " + info.error_detail);
                result.had_damage = true;
                result.packets_discarded++;
                packet_infos.push_back(info);
                break;
            }
        } else {
            // Shallow: just walk the payload to find end_offset
            size_t rows = 0;
            uint64_t end = 0;
            bool complete = false;
            bool ok = walkPacketPayload(is, isBatch, rows, end, complete);
            info.row_count = rows;
            info.end_offset = end;
            if (!ok || !complete) {
                info.is_valid = false;
                info.error_detail = "Incomplete packet (truncated before terminator/checksum)";
                if (cfg.verbose) {
                    std::cerr << "    INCOMPLETE: " << info.error_detail << "\n";
                }
                result.warnings.push_back(
                    "Packet " + std::to_string(packet_infos.size()) + ": " + info.error_detail);
                result.had_damage = true;
                result.packets_discarded++;
                packet_infos.push_back(info);
                break;
            }
        }

        packet_infos.push_back(info);

        // Next packet starts right after this one's payload checksum
        if (info.end_offset > 0) {
            next_offset = info.end_offset;
        } else {
            // Should not happen for valid packets, but fallback
            break;
        }
    }
    is.close();

    // --- Phase 2: Build new footer from valid packets ---
    uint64_t total_rows = 0;

    for (size_t i = 0; i < packet_infos.size(); ++i) {
        const auto& pi = packet_infos[i];
        if (!pi.is_valid) {
            // Partial packet recovery (non-batch only)
            if (!isBatch) {
                std::ifstream recover_is(cfg.input_file, std::ios::binary);
                uint64_t payload_start = pi.byte_offset + sizeof(bcsv::PacketHeader);
                recover_is.seekg(static_cast<std::streamoff>(payload_start));

                size_t recovered_rows = 0;
                uint64_t recovered_end = 0;

                if (recoverPartialPacket(recover_is, recovered_rows, recovered_end)) {
                    if (cfg.verbose) {
                        std::cerr << "  Partial recovery: " << recovered_rows
                                  << " rows salvaged from broken packet " << i << "\n";
                    }
                    result.warnings.push_back(
                        "Packet " + std::to_string(i) + ": " + std::to_string(recovered_rows)
                        + " rows recovered from broken packet (partial recovery)");
                    result.rows_recovered_partial = recovered_rows;

                    // Rows lost = rows in broken packet that couldn't be recovered
                    if (pi.row_count > recovered_rows) {
                        result.rows_discarded += pi.row_count - recovered_rows;
                    }

                    need_partial_write = true;
                    new_footer.addPacketEntry(pi.byte_offset, pi.first_row);
                    partial_data_start = payload_start;
                    partial_cut = recovered_end;
                    total_rows += recovered_rows;
                } else {
                    // No rows recovered from partial packet
                    result.rows_discarded += pi.row_count;
                }
            } else {
                // Batch codec: entire packet lost, no partial recovery possible
                result.rows_discarded += pi.row_count;
            }
            break;
        }

        new_footer.addPacketEntry(pi.byte_offset, pi.first_row);
        if (pi.end_offset > 0) cut_offset = pi.end_offset;

        // Track row count
        if (i + 1 < packet_infos.size() && packet_infos[i + 1].is_valid) {
            total_rows = packet_infos[i + 1].first_row;
        } else {
            if (pi.row_count > 0) {
                total_rows = pi.first_row + pi.row_count;
            } else {
                std::ifstream count_is(cfg.input_file, std::ios::binary);
                count_is.seekg(static_cast<std::streamoff>(pi.byte_offset) + sizeof(bcsv::PacketHeader));
                size_t rows = 0;
                uint64_t end = 0;
                bool complete = false;
                walkPacketPayload(count_is, isBatch, rows, end, complete);
                total_rows = pi.first_row + rows;
                if (end > 0) cut_offset = end;
            }
        }
    }

    new_footer.rowCount() = total_rows;
    result.packets_found = new_footer.packetIndex().size();
    result.rows_recovered = total_rows;

    if (!result.had_footer && result.packets_found > 0) {
        result.had_damage = true;
    }

    uint64_t effective_cut = need_partial_write ? partial_cut : cut_offset;
    if (effective_cut < result.file_size_original) {
        result.bytes_trimmed = result.file_size_original - effective_cut;
    }
    result.footer_size = new_footer.encodedSize();

    } // end of stream/packet analysis branches

    // ─── Unified output phase ───────────────────────────────────────

    if (cfg.dry_run) {
        return result;
    }

    // Prepare packet terminator writer (only used for partial packet recovery)
    auto writePacketTerminator = [&](std::ostream& os) -> bool {
        uint8_t vle_term[8];
        size_t vle_len = 0;
        uint64_t checksum = computePartialPacketHash(
            cfg.input_file, partial_data_start, partial_cut,
            vle_term, vle_len);
        os.write(reinterpret_cast<const char*>(vle_term),
                 static_cast<std::streamsize>(vle_len));
        os.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
        return os.good();
    };

    uint64_t trunc_point = need_partial_write ? partial_cut : cut_offset;
    bool has_footer = !isStreamMode && new_footer.packetIndex().size() > 0;

    if (!cfg.output_file.empty()) {
        // Copy mode
        std::ifstream src(cfg.input_file, std::ios::binary);
        if (!src) { result.error = "Cannot reopen input file for reading."; return result; }
        std::ofstream dst(cfg.output_file, std::ios::binary | std::ios::trunc);
        if (!dst) { result.error = "Cannot open output file: " + cfg.output_file; return result; }

        auto err = copyFileRange(src, dst, trunc_point);
        if (!err.empty()) { result.error = err; return result; }

        if (need_partial_write && !writePacketTerminator(dst)) {
            result.error = "Failed to write partial packet terminator.";
            return result;
        }
        if (has_footer && !new_footer.write(dst)) {
            result.error = "Failed to write footer to output file.";
            return result;
        }
        return result;
    }

    if (cfg.in_place) {
        auto err = backupAndTruncate(cfg.input_file, trunc_point, cfg.backup, cfg.verbose);
        if (!err.empty()) { result.error = err; return result; }

        // Append terminator + footer if needed
        if (need_partial_write || has_footer) {
            std::ofstream append_stream(cfg.input_file, std::ios::binary | std::ios::app);
            if (!append_stream) { result.error = "Cannot reopen file for appending."; return result; }

            if (need_partial_write && !writePacketTerminator(append_stream)) {
                result.error = "Failed to write partial packet terminator.";
                return result;
            }
            if (has_footer && !new_footer.write(append_stream)) {
                result.error = "Failed to write footer during in-place repair.";
                return result;
            }
        }
        return result;
    }

    return result;
}

// ── Output formatting ───────────────────────────────────────────────

static void printResultText(const RepairResult& r, const Config& cfg) {
    std::ostream& os = std::cerr;

    os << "bcsvRepair: " << cfg.input_file << "\n";
    os << "  File size:          " << bcsv_cli::formatBytes(r.file_size_original) << "\n";
    os << "  Codec:              " << (r.is_stream_mode ? "stream" : "packet") << "\n";

    if (!r.is_stream_mode) {
        os << "  Original footer:    " << (r.had_footer ? "present" : "MISSING") << "\n";
        os << "  Packets found:      " << r.packets_found << "\n";
        os << "  Packets discarded:  " << r.packets_discarded << "\n";
    }

    os << "  Rows recovered:     " << r.rows_recovered << "\n";

    if (r.rows_recovered_partial > 0) {
        os << "  Rows from partial:  " << r.rows_recovered_partial << "\n";
    }
    if (r.rows_discarded > 0) {
        os << "  Rows discarded:     " << r.rows_discarded << "\n";
    }

    os << "  Damage detected:    " << (r.had_damage ? "YES" : "no") << "\n";

    if (r.rows_recovered + r.rows_discarded > 0) {
        double pct = static_cast<double>(r.rows_recovered)
                   / static_cast<double>(r.rows_recovered + r.rows_discarded) * 100.0;
        os << std::fixed << std::setprecision(1);
        os << "  Recovery:           " << pct << "%\n";
    }

    if (r.bytes_trimmed > 0) {
        os << "  Bytes trimmed:      " << bcsv_cli::formatBytes(r.bytes_trimmed) << "\n";
    }
    if (r.footer_size > 0) {
        os << "  New footer size:    " << bcsv_cli::formatBytes(r.footer_size) << "\n";
    }

    for (const auto& w : r.warnings) {
        os << "  Warning: " << w << "\n";
    }

    if (!r.error.empty()) {
        os << "  ERROR: " << r.error << "\n";
    }

    if (cfg.dry_run) {
        os << "  Mode: dry-run (no files modified)\n";
    } else if (cfg.in_place) {
        os << "  Mode: in-place repair\n";
    } else {
        os << "  Mode: copy to " << cfg.output_file << "\n";
    }
}

static void printResultJson(const RepairResult& r, const Config& cfg) {
    std::cout << "{\n"
        << "  \"input\": " << jsonStr(cfg.input_file) << ",\n"
        << "  \"file_size\": " << r.file_size_original << ",\n"
        << "  \"is_stream_mode\": " << (r.is_stream_mode ? "true" : "false") << ",\n"
        << "  \"had_footer\": " << (r.had_footer ? "true" : "false") << ",\n"
        << "  \"packets_found\": " << r.packets_found << ",\n"
        << "  \"packets_discarded\": " << r.packets_discarded << ",\n"
        << "  \"rows_recovered\": " << r.rows_recovered << ",\n"
        << "  \"rows_recovered_partial\": " << r.rows_recovered_partial << ",\n"
        << "  \"rows_discarded\": " << r.rows_discarded << ",\n";

    // Recovery percentage: 100% if no damage or no rows at all
    uint64_t total_known = r.rows_recovered + r.rows_discarded;
    double recovery_pct = (total_known > 0)
        ? (static_cast<double>(r.rows_recovered) / static_cast<double>(total_known) * 100.0)
        : (r.had_damage ? 0.0 : 100.0);
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  \"recovery_pct\": " << recovery_pct << ",\n"
        << "  \"had_damage\": " << (r.had_damage ? "true" : "false") << ",\n"
        << "  \"bytes_trimmed\": " << r.bytes_trimmed << ",\n"
        << "  \"footer_size\": " << r.footer_size << ",\n"
        << "  \"dry_run\": " << (cfg.dry_run ? "true" : "false") << ",\n"
        << "  \"deep\": " << (cfg.deep ? "true" : "false") << ",\n";

    if (!r.error.empty()) {
        std::cout << "  \"error\": " << jsonStr(r.error) << ",\n";
    }

    std::cout << "  \"warnings\": [";
    for (size_t i = 0; i < r.warnings.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << jsonStr(r.warnings[i]);
    }
    std::cout << "],\n"
        << "  \"success\": " << (r.error.empty() ? "true" : "false") << "\n"
        << "}\n";
}

// ── main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    try {
        Config cfg = parseArgs(argc, argv);

        if (cfg.help) {
            printUsage(argv[0]);
            return 0;
        }

        if (cfg.verbose) {
            std::cerr << "bcsvRepair: analyzing " << cfg.input_file << " ...\n";
            if (cfg.deep) std::cerr << "  Deep validation enabled\n";
        }

        RepairResult result = analyzeAndRepair(cfg);

        if (cfg.json) {
            printResultJson(result, cfg);
        } else {
            printResultText(result, cfg);
        }

        if (!result.error.empty()) {
            return 1;
        }

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
