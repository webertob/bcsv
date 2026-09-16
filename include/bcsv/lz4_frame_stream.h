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
 * @file lz4_frame_stream.h
 * @brief Standard LZ4 frame format I/O — the container behind `.csv.lz4`
 *        (ADR-0008).
 *
 * Public surface: the content sniff lz4_frame_sniff() plus the streaming
 * adapters Lz4FrameIStream / Lz4FrameOStream. The chunk codecs they build on
 * are internal (bcsv::detail). Output is a spec-valid LZ4 frame: the official
 * `lz4` CLI decodes and validates what bcsv writes; any conformant frame
 * (independent blocks, checksums, HC, concatenated or skippable frames)
 * decodes here. Decoding is strict — corruption never looks like a clean EOF.
 * Implementations: lz4_frame_stream.hpp (pulled in at the bottom).
 */

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>

namespace bcsv {

    namespace detail {
        class Lz4FrameInputBuffer;    // streambuf: LZ4 frames -> plain bytes
        class Lz4FrameOutputBuffer;   // streambuf: plain bytes -> LZ4 frame
    }

    /// True when `head` (first >= 4 raw bytes of a stream) starts with an LZ4
    /// frame magic or a skippable-frame magic — the content check that picks
    /// decoded vs plain reading. Never trust the file name (ADR-0008).
    bool lz4_frame_sniff(std::string_view head);

    // ── Stream adapters — borrowed source/sink streambufs (caller keeps them
    //    alive), forward-only, non-copyable/movable. ────────────────────────

    /// istream over plain bytes decoded from LZ4 frame data on `source`
    /// (e.g. a binary std::ifstream's rdbuf()).
    class Lz4FrameIStream : public std::istream {
    public:
        explicit Lz4FrameIStream(std::streambuf* source);
        ~Lz4FrameIStream() override;
        Lz4FrameIStream(const Lz4FrameIStream&) = delete;
        Lz4FrameIStream& operator=(const Lz4FrameIStream&) = delete;

        bool               hasError() const;        // decode error latched below
        const std::string& errorMessage() const;    // LZ4F reason ("" if none)

    private:
        // Owned decoder buffer, bound as this stream's streambuf at ctor.
        // Decoder errors are latched, never thrown through the istream: they
        // end getline() and set hasError() (which CsvReader checks) instead
        // of a silent-clean EOF or a mid-read exception.
        std::unique_ptr<detail::Lz4FrameInputBuffer> buf_;
    };

    /// ostream compressing everything written into ONE valid LZ4 frame on
    /// `sink` (e.g. a binary std::ofstream's rdbuf()). Frame parameters are
    /// fixed at construction (frame headers are immutable). Unwritable sinks
    /// throw on write.
    class Lz4FrameOStream : public std::ostream {
    public:
        Lz4FrameOStream(std::streambuf* sink, int block_max_size_id = 4, int level = 0);
        ~Lz4FrameOStream() override;                // finish() (exceptions swallowed)
        Lz4FrameOStream(const Lz4FrameOStream&) = delete;
        Lz4FrameOStream& operator=(const Lz4FrameOStream&) = delete;

        /// Flush pending bytes, write the end-of-frame marker, sync the sink.
        /// Idempotent; retried by the destructor if a failed attempt left the
        /// trailer unwritten. `block_max_size_id`: 4=64KB/5=256KB/6=1MB/7=4MB;
        /// `level`: 0=fast (default), 2..12 = LZ4HC (level 1 is fast too).
        void finish();

    private:
        std::unique_ptr<detail::Lz4FrameOutputBuffer> buf_;
    };

} // namespace bcsv

#include "lz4_frame_stream.hpp"   // inline implementations (incl. detail codecs)
