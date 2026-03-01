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
 * @file file_codec_packet_lz4_batch001.h
 * @brief FileCodecPacketLZ4Batch001 — async double-buffered batch-LZ4 file codec.
 *
 * Packet-structured file codec with batch LZ4 compression and asynchronous
 * double-buffered I/O.  A dedicated background thread owns the stream and
 * performs all compression/decompression and I/O, yielding a flat call-time
 * profile for writeRow() and readRow() on the main thread.
 *
 * Main-thread cost:
 *   writeRow()  = O(memcpy)     — append BLE(len) + data to active raw buffer
 *   readRow()   = O(VLE decode) — decode from pre-decompressed buffer
 *
 * The only stall point is when the background thread has not yet finished
 * processing the previous buffer (back-pressure).
 *
 * Wire format per packet:
 *   PacketHeader (16 bytes)
 *   uint32_t uncompressed_size
 *   uint32_t compressed_size
 *   LZ4_block (compressed_size bytes)
 *   uint64_t payload_checksum        ← xxHash64 of uncompressed payload
 *
 * Inner uncompressed payload (before compression):
 *   BLE(row_len) | row_bytes         ← repeated for each row
 *   BLE(PCKT_TERMINATOR)
 *
 * Exceptions that occur on the background thread are captured via
 * std::exception_ptr and re-thrown on the next main-thread call.
 */

#include "file_codec_concept.h"
#include "byte_buffer.h"
#include "checksum.hpp"
#include "definitions.h"
#include "file_header.h"
#include "file_footer.h"
#include "lz4_block.hpp"
#include "packet_header.h"
#include "vle.hpp"

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <ostream>
#include <istream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

namespace bcsv {

class FileCodecPacketLZ4Batch001 {
public:
    FileCodecPacketLZ4Batch001() = default;

    ~FileCodecPacketLZ4Batch001() {
        shutdownBgThread();
    }

    // Non-copyable, non-movable — owns a thread + mutex/CV.
    FileCodecPacketLZ4Batch001(const FileCodecPacketLZ4Batch001&) = delete;
    FileCodecPacketLZ4Batch001& operator=(const FileCodecPacketLZ4Batch001&) = delete;
    FileCodecPacketLZ4Batch001(FileCodecPacketLZ4Batch001&&) = delete;
    FileCodecPacketLZ4Batch001& operator=(FileCodecPacketLZ4Batch001&&) = delete;

    // ── Setup ────────────────────────────────────────────────────────────

    void setupWrite(std::ostream& os, const FileHeader& header) {
        shutdownBgThread();  // Defensive: clean up if reused without destroy

        os_ptr_ = &os;
        packet_size_limit_ = header.getPacketSize();
        build_index_ = !header.hasFlag(FileFlags::NO_FILE_INDEX);
        packet_index_.clear();

        compressor_.init(static_cast<int>(header.getCompressionLevel()));

        raw_a_.clear();
        raw_b_.clear();
        raw_active_ = &raw_a_;
        raw_bg_     = &raw_b_;
        compressed_buf_.clear();
        current_packet_first_row_ = 0;

        startBgThread();
    }

    void setupRead(std::istream& is, const FileHeader& header) {
        shutdownBgThread();  // Defensive: clean up if reused without destroy

        is_ptr_ = &is;
        packet_size_limit_ = header.getPacketSize();

        read_a_.clear();
        read_b_.clear();
        read_current_ = &read_a_;
        read_next_    = &read_b_;
        read_cursor_  = 0;

        // Read and decompress first packet synchronously on main thread.
        packet_open_ = readAndDecompressPacket(*is_ptr_, *read_current_);

        if (packet_open_) {
            // Start BG thread and pre-read next packet.
            startBgThread();
            {
                std::lock_guard<std::mutex> lk(mutex_);
                bg_task_ = BgTask::READ_DECOMPRESS;
            }
            cv_.notify_one();
        }
    }

    // ── Write lifecycle ─────────────────────────────────────────────────

    /// Called before each writeRow().  Returns true if a packet boundary
    /// was crossed (caller resets RowCodec).
    bool beginWrite(std::ostream& /*os*/, uint64_t rowCnt) {
        rethrowBgException();

        if (raw_active_->size() >= packet_size_limit_) {
            // Close the current packet payload.
            vleEncode<uint64_t, true>(static_cast<uint64_t>(PCKT_TERMINATOR), *raw_active_);

            // Wait for BG to finish any previous work.
            waitForBgIdle();
            rethrowBgException();

            // Hand off active buffer to BG for compression + write.
            bg_first_row_ = current_packet_first_row_;
            std::swap(raw_active_, raw_bg_);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                bg_task_ = BgTask::COMPRESS_WRITE;
            }
            cv_.notify_one();

            // Next packet starts at current rowCnt.
            current_packet_first_row_ = rowCnt;
            return true;  // Boundary crossed
        }

        return false;
    }

    void writeRow(std::ostream& /*os*/, std::span<const std::byte> rowData) {
        if (rowData.empty()) {
            // ZoH repeat: length = 0
            vleEncode<uint64_t, true>(uint64_t{0}, *raw_active_);
            return;
        }

        vleEncode<uint64_t, true>(static_cast<uint64_t>(rowData.size()), *raw_active_);
        size_t offset = raw_active_->size();
        raw_active_->resize(offset + rowData.size());
        std::memcpy(raw_active_->data() + offset, rowData.data(), rowData.size());
    }

    /// Finalize: flush remaining data, shut down BG thread, write footer.
    void finalize(std::ostream& /*os*/, uint64_t totalRows) {
        if (!raw_active_->empty()) {
            // Close the last packet payload.
            vleEncode<uint64_t, true>(static_cast<uint64_t>(PCKT_TERMINATOR), *raw_active_);

            waitForBgIdle();
            rethrowBgException();

            bg_first_row_ = current_packet_first_row_;
            std::swap(raw_active_, raw_bg_);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                bg_task_ = BgTask::COMPRESS_WRITE;
            }
            cv_.notify_one();

            waitForBgIdle();
            rethrowBgException();
        }

        shutdownBgThread();

        // Write file footer on main thread (BG is stopped).
        FileFooter footer(packet_index_, totalRows);
        footer.write(*os_ptr_);
    }

    ByteBuffer& writeBuffer() { return write_buffer_; }

    /// Flush: close the current packet, compress+write synchronously, open a
    /// new packet for subsequent writes. Returns true (packet boundary crossed).
    bool flushPacket(std::ostream& /*os*/, uint64_t rowCnt) {
        if (raw_active_->empty()) {
            if (os_ptr_) os_ptr_->flush();
            return false;
        }

        // Close current packet payload
        vleEncode<uint64_t, true>(static_cast<uint64_t>(PCKT_TERMINATOR), *raw_active_);

        // Wait for any in-flight BG work, then hand off synchronously
        waitForBgIdle();
        rethrowBgException();

        bg_first_row_ = current_packet_first_row_;
        std::swap(raw_active_, raw_bg_);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            bg_task_ = BgTask::COMPRESS_WRITE;
        }
        cv_.notify_one();

        waitForBgIdle();
        rethrowBgException();

        if (os_ptr_) os_ptr_->flush();

        // Next packet starts at current rowCnt
        current_packet_first_row_ = rowCnt;
        return true;  // boundary crossed — caller must reset RowCodec
    }

    // ── Read lifecycle ──────────────────────────────────────────────────

    std::span<const std::byte> readRow(std::istream& /*is*/) {
        rethrowBgException();
        packet_boundary_crossed_ = false;

        if (!packet_open_) {
            return EOF_SENTINEL;
        }

        return decodeNextRow();
    }

    // ── Boundary / state signals ────────────────────────────────────────

    bool packetBoundaryCrossed() const noexcept {
        return packet_boundary_crossed_;
    }

    void reset() noexcept {
        // No streaming LZ4 context to reset — block mode is stateless.
        // Individual packet checksums are handled per-packet in the BG thread.
    }

private:

    // ── Background task types ───────────────────────────────────────────

    enum class BgTask { IDLE, COMPRESS_WRITE, READ_DECOMPRESS, SHUTDOWN };

    // ── Background thread lifecycle ─────────────────────────────────────

    void startBgThread() {
        if (bg_thread_.joinable()) return;  // Already running

        bg_task_ = BgTask::IDLE;
        bg_exception_ = nullptr;
        bg_thread_ = std::thread([this] { bgLoop(); });
    }

    void shutdownBgThread() {
        if (!bg_thread_.joinable()) return;

        {
            std::lock_guard<std::mutex> lk(mutex_);
            bg_task_ = BgTask::SHUTDOWN;
        }
        cv_.notify_one();
        bg_thread_.join();
    }

    void bgLoop() {
        while (true) {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return bg_task_ != BgTask::IDLE; });

            BgTask task = bg_task_;
            lk.unlock();

            if (task == BgTask::SHUTDOWN) break;

            try {
                if (task == BgTask::COMPRESS_WRITE) {
                    bgCompressAndWrite();
                } else if (task == BgTask::READ_DECOMPRESS) {
                    bgReadAndDecompress();
                }
            } catch (...) {
                bg_exception_ = std::current_exception();
            }

            {
                std::lock_guard<std::mutex> lk2(mutex_);
                bg_task_ = BgTask::IDLE;
            }
            cv_.notify_one();
        }
    }

    void waitForBgIdle() {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return bg_task_ == BgTask::IDLE; });
    }

    void rethrowBgException() {
        if (bg_exception_) {
            std::exception_ptr ex = bg_exception_;
            bg_exception_ = nullptr;
            std::rethrow_exception(ex);
        }
    }

    // ── BG: compress + write ────────────────────────────────────────────

    void bgCompressAndWrite() {
        assert(os_ptr_);
        assert(!raw_bg_->empty());

        // 1. Record packet offset for index.
        if (build_index_) {
            size_t offset = static_cast<size_t>(os_ptr_->tellp());
            packet_index_.emplace_back(offset, bg_first_row_);
        }

        // 2. Write PacketHeader.
        PacketHeader::write(*os_ptr_, bg_first_row_);

        // 3. Compute checksum of uncompressed payload.
        Checksum::Streaming hash;
        hash.update(raw_bg_->data(), raw_bg_->size());
        uint64_t checksum = hash.finalize();

        // 4. Compress.
        compressed_buf_.clear();
        compressor_.compress({raw_bg_->data(), raw_bg_->size()}, compressed_buf_);

        // 5. Write sizes.
        uint32_t uncompressed_size = static_cast<uint32_t>(raw_bg_->size());
        uint32_t compressed_size   = static_cast<uint32_t>(compressed_buf_.size());
        os_ptr_->write(reinterpret_cast<const char*>(&uncompressed_size), sizeof(uncompressed_size));
        os_ptr_->write(reinterpret_cast<const char*>(&compressed_size),   sizeof(compressed_size));

        // 6. Write compressed data.
        os_ptr_->write(reinterpret_cast<const char*>(compressed_buf_.data()),
                       static_cast<std::streamsize>(compressed_buf_.size()));

        // 7. Write checksum.
        os_ptr_->write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));

        if (!os_ptr_->good()) {
            throw std::runtime_error("FileCodecPacketLZ4Batch001: stream write failed");
        }

        // 8. Clear the background buffer for reuse.
        raw_bg_->clear();
    }

    // ── BG: read + decompress ───────────────────────────────────────────

    void bgReadAndDecompress() {
        assert(is_ptr_);
        bg_has_next_packet_ = readAndDecompressPacket(*is_ptr_, *read_next_);
    }

    /// Read one batch-compressed packet from the stream, decompress into dst.
    /// Returns false if the footer (or EOF) is reached instead of a packet.
    bool readAndDecompressPacket(std::istream& is, ByteBuffer& dst) {
        // 1. Read PacketHeader.
        std::streampos pos = is.tellg();
        PacketHeader header;
        bool ok = header.read(is, true);

        if (!ok) {
            if (header.magic == FOOTER_BIDX_MAGIC) {
                is.clear();
                is.seekg(pos);
                return false;   // Footer reached
            } else if (is.eof()) {
                return false;
            } else {
                is.clear();
                is.seekg(pos);
                throw std::runtime_error(
                    "FileCodecPacketLZ4Batch001: failed to read packet header");
            }
        }

        // 2. Read sizes (checked individually for better diagnostics).
        uint32_t uncompressed_size = 0;
        uint32_t compressed_size   = 0;
        is.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));
        if (!is || is.gcount() != sizeof(uncompressed_size)) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001: failed to read uncompressed_size");
        }
        is.read(reinterpret_cast<char*>(&compressed_size),   sizeof(compressed_size));
        if (!is || is.gcount() != sizeof(compressed_size)) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001: failed to read compressed_size");
        }

        if (uncompressed_size > MAX_PACKET_SIZE || compressed_size > MAX_PACKET_SIZE) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001: packet size exceeds MAX_PACKET_SIZE");
        }

        // 3. Read compressed block.
        compressed_read_buf_.resize(compressed_size);
        is.read(reinterpret_cast<char*>(compressed_read_buf_.data()),
                static_cast<std::streamsize>(compressed_size));
        if (!is || is.gcount() != static_cast<std::streamsize>(compressed_size)) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001: failed to read compressed data");
        }

        // 4. Read expected checksum.
        uint64_t expected_checksum = 0;
        is.read(reinterpret_cast<char*>(&expected_checksum), sizeof(expected_checksum));
        if (!is || is.gcount() != sizeof(expected_checksum)) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001: failed to read packet checksum");
        }

        // 5. Decompress.
        decompressor_.decompress(
            {compressed_read_buf_.data(), compressed_size},
            dst,
            uncompressed_size);

        // 6. Verify checksum of uncompressed data.
        Checksum::Streaming hash;
        hash.update(dst.data(), uncompressed_size);
        uint64_t actual_checksum = hash.finalize();
        if (actual_checksum != expected_checksum) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001: packet checksum mismatch");
        }

        return true;
    }

    // ── Read-side row decoding from decompressed buffer ─────────────────

    /// Decode the next row from read_current_ at read_cursor_.
    /// Handles PCKT_TERMINATOR → swap to pre-decompressed next buffer.
    std::span<const std::byte> decodeNextRow() {
        size_t remaining = read_current_->size() - read_cursor_;
        if (remaining == 0) {
            return EOF_SENTINEL;
        }

        // VLE decode row length.
        uint64_t rowLen = 0;
        size_t consumed = vleDecode<uint64_t, true>(
            rowLen,
            read_current_->data() + read_cursor_,
            remaining);
        read_cursor_ += consumed;

        // Handle PCKT_TERMINATOR → transition to next packet.
        while (rowLen == PCKT_TERMINATOR) {
            // Wait for BG to finish pre-reading next packet.
            waitForBgIdle();
            rethrowBgException();

            if (!bg_has_next_packet_) {
                packet_open_ = false;
                return EOF_SENTINEL;
            }

            // Swap buffers.
            std::swap(read_current_, read_next_);
            read_cursor_ = 0;
            packet_boundary_crossed_ = true;

            // Signal BG to pre-read the next packet.
            {
                std::lock_guard<std::mutex> lk(mutex_);
                bg_task_ = BgTask::READ_DECOMPRESS;
            }
            cv_.notify_one();

            remaining = read_current_->size() - read_cursor_;
            if (remaining == 0) {
                packet_open_ = false;
                return EOF_SENTINEL;
            }

            consumed = vleDecode<uint64_t, true>(
                rowLen,
                read_current_->data() + read_cursor_,
                remaining);
            read_cursor_ += consumed;
        }

        if (rowLen == 0) {
            return ZOH_REPEAT_SENTINEL;
        }

        if (rowLen > MAX_ROW_LENGTH) [[unlikely]] {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001::readRow: row length exceeds MAX_ROW_LENGTH ("
                + std::to_string(rowLen) + " > " + std::to_string(MAX_ROW_LENGTH) + ")");
        }

        // Read row data from the decompressed buffer.
        remaining = read_current_->size() - read_cursor_;
        if (rowLen > remaining) {
            throw std::runtime_error(
                "FileCodecPacketLZ4Batch001::readRow: row data truncated in decompressed buffer");
        }

        const std::byte* rowPtr = read_current_->data() + read_cursor_;
        read_cursor_ += rowLen;

        return {rowPtr, static_cast<size_t>(rowLen)};
    }

    // ── State ───────────────────────────────────────────────────────────
    //
    // Synchronization protocol
    // ────────────────────────
    // The BG thread and main thread communicate through the mutex_/cv_
    // pair using bg_task_ as the sole shared control variable.
    //
    // Variables written by main thread BEFORE signaling BG (via bg_task_
    // store + cv_.notify under mutex) and read by BG AFTER waking
    // (cv_.wait under mutex provides the happens-before edge):
    //   bg_first_row_       — first row index for the buffer being handed off
    //   raw_bg_ (pointer)   — points to the buffer BG should compress
    //   read_next_ (pointer)— points to the buffer BG should decompress into
    //
    // Variables written by BG thread BEFORE setting bg_task_=IDLE (under
    // mutex + cv_.notify) and read by main AFTER waitForBgIdle():
    //   bg_has_next_packet_ — whether read_next_ contains valid data
    //   packet_index_       — BG appends entries; main reads after join()
    //   bg_exception_       — captured exception; main reads after wait
    //   compressed_buf_     — BG writes here; main never reads it directly
    //
    // All other state is exclusively owned by one thread (documented below).

    // Main-thread only: RowCodec serialization buffer.
    ByteBuffer write_buffer_;

    // Double-buffered raw payload (write side).
    // Main thread owns raw_active_; BG thread owns raw_bg_.
    // Ownership transfers via pointer swap under the sync protocol above.
    ByteBuffer raw_a_;
    ByteBuffer raw_b_;
    ByteBuffer* raw_active_{&raw_a_};
    ByteBuffer* raw_bg_{&raw_b_};

    // Compression — used exclusively by BG thread during COMPRESS_WRITE.
    LZ4BlockCompressor compressor_;
    ByteBuffer compressed_buf_;

    // Double-buffered decompressed payload (read side).
    // Main thread owns read_current_; BG thread owns read_next_.
    // Ownership transfers via pointer swap under the sync protocol above.
    ByteBuffer read_a_;
    ByteBuffer read_b_;
    ByteBuffer* read_current_{&read_a_};
    ByteBuffer* read_next_{&read_b_};
    size_t read_cursor_{0};               ///< Main-thread only: decode position.
    LZ4BlockDecompressor decompressor_;   ///< Used by BG thread (and once synchronously in setupRead).
    ByteBuffer compressed_read_buf_;      ///< Used by BG thread (and once synchronously in setupRead).
    bool bg_has_next_packet_{false};       ///< BG→main via sync protocol: true if read_next_ is valid.

    // Stream pointers — set by main in setup*(), used by BG during tasks.
    // Safe: setup completes before BG is signaled; no concurrent access.
    std::ostream* os_ptr_{nullptr};
    std::istream* is_ptr_{nullptr};

    // Packet bookkeeping.
    size_t   packet_size_limit_{MIN_PACKET_SIZE};
    bool     build_index_{true};          ///< Main-thread only (set in setupWrite).
    PacketIndex packet_index_;            ///< BG appends entries; main reads after join().
    uint64_t current_packet_first_row_{0};///< Main-thread only.
    uint64_t bg_first_row_{0};             ///< Main→BG via sync protocol.
    bool     packet_open_{false};          ///< Main-thread only.
    bool     packet_boundary_crossed_{false}; ///< Main-thread only.

    // Threading.
    std::thread              bg_thread_;
    std::mutex               mutex_;       ///< Protects bg_task_ and provides happens-before edges.
    std::condition_variable  cv_;          ///< Signals bg_task_ transitions.
    BgTask                   bg_task_{BgTask::IDLE};
    std::exception_ptr       bg_exception_;///< BG→main via sync protocol.
};

static_assert(FileCodecConcept<FileCodecPacketLZ4Batch001>,
              "FileCodecPacketLZ4Batch001 must satisfy FileCodecConcept");

} // namespace bcsv
