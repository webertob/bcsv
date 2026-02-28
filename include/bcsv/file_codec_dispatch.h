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
 * @file file_codec_dispatch.h
 * @brief FileCodecDispatch — runtime file-codec selection with managed lifetime.
 *
 * Runtime file-codec selection happens once (at file-open time) and subsequent
 * per-row operations dispatch through pre-bound function pointers without
 * per-row branching.
 *
 * Pattern mirrors RowCodecDispatch: manual type erasure with void* context
 * and static trampoline functions.
 *
 * Both Writer and Reader use FileCodecDispatch (unlike RowCodecs where Writer
 * uses compile-time selection).  The I/O-dominated cost of file-codec
 * operations (disk writes, LZ4 compression, xxHash checksums) makes a single
 * indirect call (~2ns) negligible.
 *
 * Lifecycle (write):  setupWrite → (beginWrite → writeRow)* → finalize
 * Lifecycle (read):   setupRead → (readRow)*
 */

#include "file_codec_stream001.h"
#include "file_codec_stream_lz4_001.h"
#include "file_codec_packet001.h"
#include "file_codec_packet_lz4_001.h"
#include "definitions.h"
#include "byte_buffer.h"

#include <cassert>
#include <cstddef>
#include <ostream>
#include <istream>
#include <span>
#include <stdexcept>
#include <utility>

namespace bcsv {

class FileCodecDispatch {
public:
    // ── Function pointer types ──────────────────────────────────────────
    using SetupWriteFn           = void (*)(void* ctx, std::ostream& os, const FileHeader& header);
    using SetupReadFn            = void (*)(void* ctx, std::istream& is, const FileHeader& header);
    using BeginWriteFn           = bool (*)(void* ctx, std::ostream& os, uint64_t rowCnt);
    using WriteRowFn             = void (*)(void* ctx, std::ostream& os, std::span<const std::byte> rowData);
    using FinalizeFn             = void (*)(void* ctx, std::ostream& os, uint64_t totalRows);
    using WriteBufferFn          = ByteBuffer& (*)(void* ctx);
    using ReadRowFn              = std::span<const std::byte> (*)(void* ctx, std::istream& is);
    using PacketBoundaryCrossedFn = bool (*)(const void* ctx);
    using ResetFn                = void (*)(void* ctx);
    using DestroyFn              = void (*)(void* ctx);

    // ── Constructors / Rule of Five ─────────────────────────────────────

    FileCodecDispatch() = default;

    ~FileCodecDispatch() { destroy(); }

    // Non-copyable (LZ4 codec variants are non-copyable).
    FileCodecDispatch(const FileCodecDispatch&) = delete;
    FileCodecDispatch& operator=(const FileCodecDispatch&) = delete;

    FileCodecDispatch(FileCodecDispatch&& other) noexcept {
        moveFrom(std::move(other));
    }

    FileCodecDispatch& operator=(FileCodecDispatch&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // ── Setup ───────────────────────────────────────────────────────────

    /// Select and construct a concrete file codec by ID.
    void setup(FileCodecId id) {
        destroy();
        codec_id_ = id;

        switch (id) {
            case FileCodecId::STREAM_001:
                constructCodec<FileCodecStream001>();
                break;
            case FileCodecId::STREAM_LZ4_001:
                constructCodec<FileCodecStreamLZ4001>();
                break;
            case FileCodecId::PACKET_001:
                constructCodec<FileCodecPacket001>();
                break;
            case FileCodecId::PACKET_LZ4_001:
                constructCodec<FileCodecPacketLZ4001>();
                break;
            case FileCodecId::PACKET_LZ4_BATCH_001:
                throw std::logic_error(
                    "FileCodecDispatch::setup: PACKET_LZ4_BATCH_001 is not yet implemented");
            default:
                throw std::logic_error(
                    "FileCodecDispatch::setup: unsupported FileCodecId");
        }
    }

    /// Convenience: resolve codec from header fields, then construct.
    void select(uint8_t compressionLevel, FileFlags flags) {
        setup(resolveFileCodecId(compressionLevel, flags));
    }

    void destroy() noexcept {
        if (ctx_ != nullptr && destroy_fn_ != nullptr) {
            destroy_fn_(ctx_);
        }
        ctx_ = nullptr;
        nullifyFnPtrs();
    }

    // ── Forwarding ──────────────────────────────────────────────────────

    void setupWrite(std::ostream& os, const FileHeader& header) {
        assert(ctx_ && setup_write_fn_);
        setup_write_fn_(ctx_, os, header);
    }

    void setupRead(std::istream& is, const FileHeader& header) {
        assert(ctx_ && setup_read_fn_);
        setup_read_fn_(ctx_, is, header);
    }

    bool beginWrite(std::ostream& os, uint64_t rowCnt) {
        assert(ctx_ && begin_write_fn_);
        return begin_write_fn_(ctx_, os, rowCnt);
    }

    void writeRow(std::ostream& os, std::span<const std::byte> rowData) {
        assert(ctx_ && write_row_fn_);
        write_row_fn_(ctx_, os, rowData);
    }

    void finalize(std::ostream& os, uint64_t totalRows) {
        assert(ctx_ && finalize_fn_);
        finalize_fn_(ctx_, os, totalRows);
    }

    ByteBuffer& writeBuffer() {
        assert(ctx_ && write_buffer_fn_);
        return write_buffer_fn_(ctx_);
    }

    std::span<const std::byte> readRow(std::istream& is) {
        assert(ctx_ && read_row_fn_);
        return read_row_fn_(ctx_, is);
    }

    bool packetBoundaryCrossed() const {
        assert(ctx_ && packet_boundary_crossed_fn_);
        return packet_boundary_crossed_fn_(ctx_);
    }

    void reset() {
        assert(ctx_ && reset_fn_);
        reset_fn_(ctx_);
    }

    // ── Queries ─────────────────────────────────────────────────────────

    bool isSetup() const noexcept { return ctx_ != nullptr; }
    FileCodecId codecId() const noexcept { return codec_id_; }

private:
    FileCodecId codec_id_{FileCodecId::PACKET_LZ4_001};
    void* ctx_{nullptr};

    SetupWriteFn            setup_write_fn_{nullptr};
    SetupReadFn             setup_read_fn_{nullptr};
    BeginWriteFn            begin_write_fn_{nullptr};
    WriteRowFn              write_row_fn_{nullptr};
    FinalizeFn              finalize_fn_{nullptr};
    WriteBufferFn           write_buffer_fn_{nullptr};
    ReadRowFn               read_row_fn_{nullptr};
    PacketBoundaryCrossedFn packet_boundary_crossed_fn_{nullptr};
    ResetFn                 reset_fn_{nullptr};
    DestroyFn               destroy_fn_{nullptr};

    void nullifyFnPtrs() noexcept {
        setup_write_fn_ = nullptr;
        setup_read_fn_ = nullptr;
        begin_write_fn_ = nullptr;
        write_row_fn_ = nullptr;
        finalize_fn_ = nullptr;
        write_buffer_fn_ = nullptr;
        read_row_fn_ = nullptr;
        packet_boundary_crossed_fn_ = nullptr;
        reset_fn_ = nullptr;
        destroy_fn_ = nullptr;
    }

    void moveFrom(FileCodecDispatch&& other) noexcept {
        codec_id_ = other.codec_id_;
        ctx_ = other.ctx_;
        setup_write_fn_ = other.setup_write_fn_;
        setup_read_fn_ = other.setup_read_fn_;
        begin_write_fn_ = other.begin_write_fn_;
        write_row_fn_ = other.write_row_fn_;
        finalize_fn_ = other.finalize_fn_;
        write_buffer_fn_ = other.write_buffer_fn_;
        read_row_fn_ = other.read_row_fn_;
        packet_boundary_crossed_fn_ = other.packet_boundary_crossed_fn_;
        reset_fn_ = other.reset_fn_;
        destroy_fn_ = other.destroy_fn_;

        other.ctx_ = nullptr;
        other.nullifyFnPtrs();
    }

    // ── Trampoline factory ──────────────────────────────────────────────

    /// Helper: constructs a concrete codec on the heap and wires all
    /// function pointers.  Works for any type satisfying FileCodecConcept.
    template<typename ConcreteCodec>
    void constructCodec() {
        ctx_ = new ConcreteCodec();

        setup_write_fn_ = [](void* ctx, std::ostream& os, const FileHeader& h) {
            static_cast<ConcreteCodec*>(ctx)->setupWrite(os, h);
        };
        setup_read_fn_ = [](void* ctx, std::istream& is, const FileHeader& h) {
            static_cast<ConcreteCodec*>(ctx)->setupRead(is, h);
        };
        begin_write_fn_ = [](void* ctx, std::ostream& os, uint64_t r) -> bool {
            return static_cast<ConcreteCodec*>(ctx)->beginWrite(os, r);
        };
        write_row_fn_ = [](void* ctx, std::ostream& os, std::span<const std::byte> d) {
            static_cast<ConcreteCodec*>(ctx)->writeRow(os, d);
        };
        finalize_fn_ = [](void* ctx, std::ostream& os, uint64_t t) {
            static_cast<ConcreteCodec*>(ctx)->finalize(os, t);
        };
        write_buffer_fn_ = [](void* ctx) -> ByteBuffer& {
            return static_cast<ConcreteCodec*>(ctx)->writeBuffer();
        };
        read_row_fn_ = [](void* ctx, std::istream& is) -> std::span<const std::byte> {
            return static_cast<ConcreteCodec*>(ctx)->readRow(is);
        };
        packet_boundary_crossed_fn_ = [](const void* ctx) -> bool {
            return static_cast<const ConcreteCodec*>(ctx)->packetBoundaryCrossed();
        };
        reset_fn_ = [](void* ctx) {
            static_cast<ConcreteCodec*>(ctx)->reset();
        };
        destroy_fn_ = [](void* ctx) {
            delete static_cast<ConcreteCodec*>(ctx);
        };
    }
};

} // namespace bcsv
