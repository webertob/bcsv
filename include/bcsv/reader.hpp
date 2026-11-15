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
 * @file reader.hpp
 * @brief Binary CSV (BCSV) Library - Reader implementations for v1.3.0
 * 
 * This file contains the template implementations for the Reader class.
 * Supports v1.3.0 streaming LZ4 compression with VLE-encoded payloads.
 */

#include "bcsv/file_footer.h"
#include "reader.h"
#include "file_header.h"
#include "codec_file/file_codec_concept.h"
#include "codec_file/packet_scan.h"
#include "layout.h"
#include "packet_header.h"
#include "definitions.h"
#include "vle.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <sstream>
#include <span>

namespace bcsv {

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::Reader() 
    : err_msg_()
    , file_header_()
    , file_path_()
    , stream_()
    , file_codec_()
    , row_pos_(0)
    , row_(LayoutType())

    {
    }

    template<LayoutConcept LayoutType>
    Reader<LayoutType>::~Reader() {
        if (isOpen()) {
            close();
        }
    }

    /**
     * @brief Close the binary file
     */
    template<LayoutConcept LayoutType>
    void Reader<LayoutType>::close() {
        if(!isOpen()) {
            return;
        }

        file_path_.clear();
        // Shut down codecs BEFORE closing the stream.  The batch file codec
        // owns a background thread that may still be reading from stream_;
        // closing the stream first would cause a use-after-close SIGSEGV.
        // (Writer::close() already has the correct ordering.)   [LIB-4 fix]
        file_codec_.destroy();
        row_codec_.destroy();  // Deletes inner codec; inner codec's destructor releases the structural lock
        stream_.close();
        row_pos_ = 0;
        row_.clear();
    }

    /**
     * @brief Open a binary file for reading with comprehensive validation
     * @param filepath Path to the file (relative or absolute)
     * @return true if file was successfully opened, false otherwise
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::open(const FilePath& filepath) {
        err_msg_.clear();
        if(isOpen()) {
            err_msg_ = "Warning: File is already open: " + file_path_.string();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << std::endl;
            }
            return false;
        }

        try {
            // Convert to absolute path for consistent handling
            FilePath absolutePath = std::filesystem::absolute(filepath);
            
            // Check if file exists
            if (!std::filesystem::exists(absolutePath)) {
                throw std::runtime_error("Error: File does not exist: " + absolutePath.string());
            }

            // Check if it's a regular file
            if (!std::filesystem::is_regular_file(absolutePath)) {
                throw std::runtime_error("Error: Path is not a regular file: " + absolutePath.string());
            }

            // Check read permissions
            std::error_code ec;
            auto perms = std::filesystem::status(absolutePath, ec).permissions();
            if (ec || (perms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) {
                throw std::runtime_error("Error: No read permission for file: " + absolutePath.string());
            }

            // Open the binary file
            stream_.open(absolutePath, std::ios::binary);
            if (!stream_.is_open()) {
                throw std::runtime_error("Error: Cannot open file for reading: " + absolutePath.string());
            }
            
            file_path_ = absolutePath;
            
            // Read file header
            if(!readFileHeader()) {
                // readFileHeader() has already recorded *why* it failed --
                // incompatible version, bad magic, column-type mismatch.  A
                // generic message here would replace it in the catch below,
                // and those specific strings are documented as observable in
                // docs/ERROR_HANDLING.md.
                throw std::runtime_error(err_msg_.empty()
                                             ? std::string("Error: Failed to read file header")
                                             : err_msg_);
            }

            // Initialize file-level codec (framing, decompression, checksums)
            // setupRead also opens the first packet for packet-based codecs.
            file_codec_.select(file_header_.versionMinor(),
                               compressionLevel(), file_header_.getFlags());
            file_codec_.setupRead(stream_, file_header_);

            row_pos_ = 0;

        } catch (const std::exception& ex) {
            err_msg_ = ex.what();
            file_codec_.destroy();
            row_codec_.destroy();
            if (stream_.is_open()) {
                stream_.close();
            }
            file_path_.clear();
            return false;
        }
        return true;
    }

    /**
     * @brief Read file header
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readFileHeader() {
        if (!stream_) {
            err_msg_ = "Error: Stream is not open";
            return false;
        }
        
        try {
            LayoutType layout;
            file_header_.readFromBinary(stream_, layout);
            
            // Check version compatibility: same major, minor <= ours
            if (file_header_.versionMajor() != static_cast<uint8_t>(version::MAJOR) || 
                file_header_.versionMinor() > static_cast<uint8_t>(version::MINOR)) {
                std::ostringstream oss;
                oss << "Error: Incompatible file version: "
                    << static_cast<int>(file_header_.versionMajor()) << "."
                    << static_cast<int>(file_header_.versionMinor())
                    << " (Expected: " << version::MAJOR << "." 
                    << version::MINOR << " or earlier)";
                err_msg_ = oss.str();
                if constexpr (DEBUG_OUTPUTS) {
                    std::cerr << err_msg_ << "\n";
                }
                return false;
            }
            
            row_ = RowType(layout);

            // Initialize codec dispatch — version-gated codec selection.
            // Uses file minor version + flags to select the correct codec
            // for backward compatibility.  See resolveRowCodecId().
            row_codec_.selectCodec(file_header_.versionMinor(),
                                   file_header_.getFlags(), row_.layout());

            return true;
            
        } catch (const std::exception& ex) {
            err_msg_ = std::string("Error: Failed to read file header: ") + ex.what();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << err_msg_ << "\n";
            }
            return false;
        }
    }

    /**
     * @brief Read next row from file via file codec
     */
    template<LayoutConcept LayoutType>
    bool Reader<LayoutType>::readNext() {
        if (!isOpen()) {
            return false;
        }

        // Liveness is queried through the codec, not by polling stream_
        // directly: the batch codec's background thread may be touching the
        // stream concurrently, so its state is not ours to inspect here.
        if (!file_codec_.readGood(stream_)) {
            return false;
        }

        // Read row via file codec (handles VLE, decompression, checksums, packet transitions)
        auto rowRawData = file_codec_.readRow(stream_);

        // Check for EOF sentinel
        if (rowRawData.data() == EOF_SENTINEL.data()) {
            return false;
        }

        // Reset RowCodec state if a packet boundary was crossed
        if (file_codec_.packetBoundaryCrossed()) {
            row_codec_.reset();
        }

        // Check for ZoH repeat sentinel — reuse previous row
        if (rowRawData.data() == ZOH_REPEAT_SENTINEL.data()) {
            if (!file_header_.hasFlag(FileFlags::ZERO_ORDER_HOLD) && row_.layout().columnCount() > 0) {
                throw std::runtime_error("Error: ZERO_ORDER_HOLD flag not set, but repeat row encountered");
            }
            if (row_pos_ == 0 && row_.layout().columnCount() > 0) {
                throw std::runtime_error("Error: Cannot repeat previous row, no previous row data available");
            }
            row_pos_++;
            return true;
        }

        // Deserialize row via row codec (Item 11)
        row_codec_.deserialize(rowRawData, row_);
        row_pos_++;
        return true;        
    }

    template<LayoutConcept LayoutType>
    void ReaderDirectAccess<LayoutType>::close() {
        Base::close();
        da_row_codec_.destroy();
        file_footer_.clear();
        cached_rows_.clear();
        cached_packet_idx_ = SIZE_MAX;
        cached_first_row_ = 0;
        cached_row_count_ = 0;
        cached_decode_pos_ = SIZE_MAX;
        da_row_pos_ = SIZE_MAX;
    }

    template<LayoutConcept LayoutType>
    bool ReaderDirectAccess<LayoutType>::open(const FilePath& filepath, bool rebuildFooter ) {
        // open file as normal
        if(!Base::open(filepath))
            return false;

        std::streampos originalPos = Base::stream_.tellg();

        // Initialize a separate row codec for direct-access deserialization
        da_row_codec_.selectCodec(Base::file_header_.versionMinor(),
                                  Base::file_header_.getFlags(), Base::row_.layout());

        // additionally read file footer
        try {
            if(!file_footer_.read(Base::stream_)) {
                if(!rebuildFooter) {
                    Base::err_msg_ = "Error: FileFooter missing or invalid (use rebuildFooter=true to reconstruct)";
                    if constexpr (DEBUG_OUTPUTS) {
                        std::cerr << Base::err_msg_ << "\n";
                    }
                    close();   // a failed open leaves nothing open (a retry with rebuild must work)
                    return false;
                } else {
                    Base::err_msg_ = "Warning: FileFooter missing or invalid, attempting to rebuild index";
                    if constexpr (DEBUG_OUTPUTS) {
                        std::cerr << Base::err_msg_ << "\n";
                    }
                    buildFileFooter();
                    Base::err_msg_.clear();  // Clear warning after successful rebuild
                }
            }
        } catch (const std::exception& ex) {
            Base::err_msg_ = std::string("Error: Exception reading FileFooter: ") + ex.what();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << Base::err_msg_ << std::endl;
            }
            close();
            return false;
        }

        // restore original position after reader::open(), as expected by the
        // default reader (i.e. ::readNext()); a failed footer read or the
        // rebuild may have left eof/fail set.
        Base::stream_.clear();
        Base::stream_.seekg(originalPos);
        return true;
    }

    /**
     * @brief Read a specific row by absolute index (0-based).
     *
     * Uses the packet index for O(log P) packet lookup, then loads the
     * target packet into an in-memory cache (decompressing if LZ4).
     * Subsequent reads within the same packet are O(1) vector lookups.
     *
     * @param index 0-based row index in the file
     * @return true if the row was successfully read (accessible via row()),
     *         false if index is out of range or an error occurred
     */
    template<LayoutConcept LayoutType>
    bool ReaderDirectAccess<LayoutType>::read(size_t index) {
        if (!Base::isOpen()) {
            Base::err_msg_ = "Error: File is not open";
            return false;
        }

        if (index >= file_footer_.rowCount()) {
            Base::err_msg_ = "Error: Row index out of range (" + std::to_string(index) +
                             " >= " + std::to_string(file_footer_.rowCount()) + ")";
            return false;
        }

        // Fast path: row is in the currently cached packet
        if (cached_packet_idx_ != SIZE_MAX &&
            index >= cached_first_row_ &&
            index < cached_first_row_ + cached_row_count_) {
            size_t rowInPacket = index - cached_first_row_;
            return deserializeCachedRow(rowInPacket, index);
        }

        // Binary search: find the packet containing this row
        auto pktIt = file_footer_.findPacket(static_cast<uint64_t>(index));
        if (pktIt == file_footer_.packetIndex().end()) {
            Base::err_msg_ = "Error: Could not locate packet for row " + std::to_string(index);
            return false;
        }

        size_t packetIdx = static_cast<size_t>(std::distance(file_footer_.packetIndex().cbegin(), pktIt));
        size_t rowInPacket = index - static_cast<size_t>(pktIt->first_row);

        try {
            // Load packet into cache if not already cached
            if (packetIdx != cached_packet_idx_) {
                if (!loadPacket(packetIdx)) return false;
            }

            return deserializeCachedRow(rowInPacket, index);

        } catch (const std::exception& ex) {
            Base::err_msg_ = std::string("Error: Exception during direct read: ") + ex.what();
            if constexpr (DEBUG_OUTPUTS) {
                std::cerr << Base::err_msg_ << std::endl;
            }
            return false;
        }
    }

    /**
     * @brief Load an entire packet into the row cache.
     *
     * Seeks to the target packet and reads all rows via the file codec
     * (which handles VLE framing, checksums, and decompression
     * transparently).  Stores the raw/decompressed row data in
     * cached_rows_ for O(1) subsequent access.
     */
    template<LayoutConcept LayoutType>
    bool ReaderDirectAccess<LayoutType>::loadPacket(size_t packetIdx) {
        const auto& pktEntry = file_footer_.packetIndex()[packetIdx];

        // Invalidate the cache BEFORE any mutation: several steps below can
        // throw (seek failure, checksum mismatch, decode error), and stale
        // metadata pointing at freshly overwritten cached_rows_ would serve
        // rows from the wrong packet — or index past the vector.
        cached_packet_idx_ = SIZE_MAX;
        cached_first_row_ = 0;
        cached_row_count_ = 0;
        cached_decode_pos_ = SIZE_MAX;

        // Seek file codec to the target packet
        if (!Base::file_codec_.seekToPacket(Base::stream_,
                static_cast<std::streamoff>(pktEntry.byte_offset))) {
            Base::err_msg_ = "Error: Failed to seek to packet at offset " +
                             std::to_string(pktEntry.byte_offset);
            return false;
        }

        // Reset row codec for the new packet boundary
        da_row_codec_.reset();

        // Determine how many rows this packet contains
        const uint64_t nextFirstRow = (packetIdx + 1 < file_footer_.packetIndex().size())
            ? file_footer_.packetIndex()[packetIdx + 1].first_row
            : file_footer_.rowCount();
        if (nextFirstRow < pktEntry.first_row) {
            Base::err_msg_ = "Error: file index out of order at packet " + std::to_string(packetIdx);
            return false;
        }
        const size_t pktRowCount = static_cast<size_t>(nextFirstRow - pktEntry.first_row);

        // Read all rows via the file codec (handles VLE framing + checksum).
        // The packet must hold exactly the rows the index assigns to it,
        // or serving rows by that index would return the wrong ones. After
        // seekToPacket() the codec stops at the packet's end (EOF_SENTINEL),
        // so fewer rows show here and more rows in finishPacketRead() below.
        cached_rows_.clear();
        // Every row takes at least one byte of the packet: an index claiming
        // more must not size the allocation.
        cached_rows_.reserve(std::min<size_t>(pktRowCount, Base::file_header_.getPacketSize()));

        for (size_t i = 0; i < pktRowCount; ++i) {
            auto rowRawData = Base::file_codec_.readRow(Base::stream_);

            if (rowRawData.data() == EOF_SENTINEL.data()) {
                cached_rows_.clear();
                Base::err_msg_ = "Error: packet " + std::to_string(packetIdx) + " holds " +
                                 std::to_string(i) + " rows, the file index says " +
                                 std::to_string(pktRowCount) +
                                 " (footer inconsistent with the packets; repair with bcsvRepair)";
                return false;
            }

            if (rowRawData.data() == ZOH_REPEAT_SENTINEL.data()) {
                cached_rows_.emplace_back();  // Empty = ZoH sentinel
            } else {
                cached_rows_.emplace_back(rowRawData.begin(), rowRawData.end());
            }
        }

        // The packet must end here. Synchronous packet codecs verify the
        // checksum only when the terminator is consumed, which the
        // row-count-bounded loop above never does; finishPacketRead()
        // consumes terminator + checksum (batch: checks the terminator) and
        // STOPS — it never touches the next packet, so corruption there
        // cannot make this (valid) packet unreadable. Throws if the packet
        // holds more rows than the index says, or its checksum mismatches.
        try {
            Base::file_codec_.finishPacketRead(Base::stream_);
        } catch (const std::exception& ex) {
            cached_rows_.clear();
            Base::err_msg_ = "Error: packet " + std::to_string(packetIdx) +
                             " does not end after the " + std::to_string(pktRowCount) +
                             " rows the file index says (" + ex.what() +
                             "; footer inconsistent with the packets, or the packet is damaged;"
                             " repair with bcsvRepair)";
            return false;
        }

        cached_packet_idx_ = packetIdx;
        cached_first_row_ = static_cast<size_t>(pktEntry.first_row);
        cached_row_count_ = cached_rows_.size();
        cached_decode_pos_ = SIZE_MAX;  // No rows decoded yet in this packet

        return true;
    }

    /**
     * @brief Deserialize a row from the in-memory packet cache.
     *
     * For stateless codecs (Flat001): directly deserializes the target row.
     * For stateful codecs (ZoH001/Delta002): maintains a decode watermark
     * within the packet.  Forward reads continue from the watermark;
     * backward reads (or first access) reset the codec and decode from
     * the beginning of the packet.  Same-row re-reads return the cached
     * row_ without re-decoding.
     */
    template<LayoutConcept LayoutType>
    bool ReaderDirectAccess<LayoutType>::deserializeCachedRow(size_t rowInPacket, size_t index) {
        if (rowInPacket >= cached_row_count_) {
            Base::err_msg_ = "Error: Row offset within packet out of range";
            return false;
        }

        // Detect readNext() interleaving: if row_pos_ was modified by
        // readNext() since our last read(), row_ no longer matches the
        // watermark state.  Invalidate so we re-decode from packet start.
        if (da_row_pos_ != Base::row_pos_) {
            cached_decode_pos_ = SIZE_MAX;
        }

        // ── Stateless codec fast path (Flat001) ─────────────────────────
        if (da_row_codec_.isFlat()) {
            const auto& rawRow = cached_rows_[rowInPacket];
            if (rawRow.empty()) {
                // Flat codec never produces ZoH sentinels — this indicates a
                // corrupt/mismatched file.  Return error rather than silently
                // returning stale row_ data.
                Base::err_msg_ = "Error: Unexpected empty row in flat-codec packet at position " +
                                 std::to_string(rowInPacket);
                return false;
            }
            auto rowSpan = std::span<const std::byte>(rawRow.data(), rawRow.size());
            da_row_codec_.deserialize(rowSpan, Base::row_);
            Base::row_pos_ = index;
            da_row_pos_ = index;
            return true;
        }

        // ── Stateful codec path (ZoH001 / Delta002) ────────────────────
        // Same-row re-read: return cached row_ unchanged
        if (rowInPacket == cached_decode_pos_) {
            Base::row_pos_ = index;
            da_row_pos_ = index;
            return true;
        }

        // Determine decode start position
        size_t decodeFrom = 0;
        if (cached_decode_pos_ != SIZE_MAX && rowInPacket > cached_decode_pos_) {
            // Forward read: continue from watermark
            decodeFrom = cached_decode_pos_ + 1;
        } else {
            // Backward read or first access: reset and decode from packet start
            da_row_codec_.reset();
            decodeFrom = 0;
        }

        // Sequential decode from decodeFrom through rowInPacket.
        // ZoH repeats (empty rows) are skipped — row_ retains its previous
        // values, which is the correct ZoH semantic.  The row codec's
        // deserialize() is NOT called for repeats, so codec-internal state
        // (e.g. Delta002::rows_seen_) is not incremented — matching the
        // sequential Reader::readNext() path which also skips the codec.
        for (size_t r = decodeFrom; r <= rowInPacket; ++r) {
            const auto& rawRow = cached_rows_[r];
            if (rawRow.empty()) {
                if (r == 0) {
                    Base::err_msg_ = "Error: ZoH repeat on first row of packet";
                    return false;
                }
                // row_ retains previous values (ZoH hold)
            } else {
                auto rowSpan = std::span<const std::byte>(rawRow.data(), rawRow.size());
                da_row_codec_.deserialize(rowSpan, Base::row_);
            }
        }

        cached_decode_pos_ = rowInPacket;
        Base::row_pos_ = index;
        da_row_pos_ = index;
        return true;
    }

    /**
     * @brief Rebuild the file index of a file without a (valid) footer.
     *
     * Indexes the complete packets scanPackets() finds by walking the packet
     * framing (see codec_file/packet_scan.h): a packet cut by a crash or
     * damaged is not indexed, nor is anything after it, so rowCount() is the
     * number of rows in complete packets. Bounded by the file: every step
     * moves forward and nothing is allocated beyond what the file holds.
     */
    template<LayoutConcept LayoutType>
    void ReaderDirectAccess<LayoutType>::buildFileFooter() {
        // open() restores the read position afterwards; the failed footer
        // read may have left eof/fail set, which scanPackets() clears.
        PacketScanReport report = scanPackets(Base::stream_, Base::file_header_,
                                              Base::file_header_.getBinarySize(Base::layout()),
                                              /*verifyAll=*/false);
        file_footer_ = FileFooter(std::move(report.index), report.rows);
    }

} // namespace bcsv