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
 * @file lz4_frame_stream.hpp
 * @brief Implementations for the LZ4 frame container behind `.csv.lz4`
 *        (interface: lz4_frame_stream.h, rationale: docs/adr/0008.md).
 *
 * Frames written by the compressor carry block AND content checksums. The
 * `lz4` CLI leaves both off by default, but a checksumless frame cannot tell
 * a corrupt stream from a good one: a flipped byte inside a block decodes to
 * plausible-looking garbage with zero errors reported. The flags cost ~8
 * bytes per block, every spec reader (including `lz4 -t`) validates them,
 * and with them the loud-error guarantee of ADR-0008 covers corruption, not
 * just truncation. Reading accepts every spec-conformant variant as is.
 */

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "lz4-1.10.0/lz4frame.h"
#include "lz4_frame_stream.h"   // interface (forward-decls the buffers this implements)

namespace bcsv {

    namespace detail {

        // ── Frame-spec constants (single source of truth) ─────────────────
        // Skippable frames own 0x184D2A50..0x184D2A5F: mask off the low nibble.
        inline constexpr unsigned kFrameMagic         = LZ4F_MAGICNUMBER;   // 0x184D2204
        inline constexpr unsigned kSkippableMagicBase = 0x184D2A50U;
        inline constexpr unsigned kSkippableMagicMask = 0xFFFFFFF0U;

        inline bool is_skippable_magic(unsigned magic) {
            return (magic & kSkippableMagicMask) == kSkippableMagicBase;
        }

        /// Frame fields (magic, skippable payload length) are little-endian
        /// on the wire; decode explicitly so big-endian targets do not
        /// reject valid frames or misread skip lengths.
        inline unsigned read_u32_le(const char* p) {
            return static_cast<unsigned>(static_cast<unsigned char>(p[0]))
                 | static_cast<unsigned>(static_cast<unsigned char>(p[1])) << 8
                 | static_cast<unsigned>(static_cast<unsigned char>(p[2])) << 16
                 | static_cast<unsigned>(static_cast<unsigned char>(p[3])) << 24;
        }

        inline std::string to_hex(unsigned v) {   // "184D2204" (no 0x prefix)
            static const char* d = "0123456789ABCDEF";
            std::string s;
            for (int i = 28; i >= 0; i -= 4) s.push_back(d[(v >> i) & 0xF]);
            return s;
        }

        /// Check an LZ4F return code; throw with the official error name.
        inline size_t lz4f_check(size_t code, const char* what) {
            if (LZ4F_isError(code)) {
                throw std::runtime_error(std::string("LZ4 frame: ") + what + ": " + LZ4F_getErrorName(code));
            }
            return code;
        }

        // ── Push-side codec: plain bytes in, frame bytes out ──────────────
        // Streaming contract: header() first, then push() per chunk, then
        // finish() exactly once. Do NOT write `header() + push(x) + finish()`
        // as one expression — operator+ evaluates its operands in unspecified
        // order and the one-shot finish() throws when reordered. Sequence the
        // statements (the output streambuf below does).
        class Lz4FrameCompressor {
        public:
            // id: block-max-size FField, 4=64KB/5=256KB/6=1MB/7=4MB — the same
            // numbering as the CLI's -B flags (== LZ4F_blockSizeID_t).
            // level: 0 = fast (default), 2..12 = LZ4HC. Level 1 is still fast
            // mode in lz4 itself (LZ4HC_CLEVEL_MIN == 2), so the accepted
            // domain is 0..12, validated here rather than silently clamped.
            explicit Lz4FrameCompressor(int block_max_size_id = 4, int compression_level = 0)
                : prefs_(LZ4F_INIT_PREFERENCES)
            {
                if (block_max_size_id < 4 || block_max_size_id > 7) {
                    throw std::runtime_error("LZ4 frame: invalid block max size id " +
                                             std::to_string(block_max_size_id) +
                                             " (4=64KB, 5=256KB, 6=1MB, 7=4MB)");
                }
                if (compression_level < 0 || compression_level > 12) {
                    throw std::runtime_error("LZ4 frame: invalid compression level " +
                                             std::to_string(compression_level) +
                                             " (0=fast, 2..12=LZ4HC)");
                }
                prefs_.frameInfo.blockSizeID         = static_cast<LZ4F_blockSizeID_t>(block_max_size_id);
                prefs_.frameInfo.blockMode           = LZ4F_blockLinked;
                prefs_.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
                prefs_.frameInfo.blockChecksumFlag   = LZ4F_blockChecksumEnabled;
                prefs_.compressionLevel              = compression_level;

                lz4f_check(LZ4F_createCompressionContext(&cctx_, LZ4F_VERSION),
                           "createCompressionContext");
                created_ = true;

                std::string hdr(LZ4F_HEADER_SIZE_MAX, '\0');
                size_t n = lz4f_check(LZ4F_compressBegin(cctx_, hdr.data(), LZ4F_HEADER_SIZE_MAX, &prefs_),
                                      "compressBegin");
                hdr.resize(n);
                header_ = std::move(hdr);
            }

            ~Lz4FrameCompressor() {
                if (created_) LZ4F_freeCompressionContext(cctx_);
            }

            Lz4FrameCompressor(const Lz4FrameCompressor&) = delete;
            Lz4FrameCompressor& operator=(const Lz4FrameCompressor&) = delete;

            const std::string& header() const { return header_; }

            std::string push(std::string_view chunk) {
                if (finished_) throw std::runtime_error("LZ4 frame: compressor already finished");
                if (chunk.empty()) return {};
                work_.resize(LZ4F_compressBound(chunk.size(), &prefs_));
                size_t n = lz4f_check(LZ4F_compressUpdate(
                    cctx_, work_.data(), work_.size(), chunk.data(), chunk.size(), nullptr),
                    "compressUpdate");
                return std::string(work_.data(), n);
            }

            // Emits the pending block, the end-block marker and (with the
            // content checksum on) the digest. The LZ4F call itself cannot
            // touch the sink, so the trailer is remembered: emit() of it may
            // fail on a full disk and a later retry replays it (see the
            // output buffer's finalize()). Valid to call repeatedly.
            const std::string& finish() {
                if (!finished_) {
                    finished_ = true;
                    work_.resize(LZ4F_compressBound(0, &prefs_));
                    size_t n = lz4f_check(LZ4F_compressEnd(cctx_, work_.data(), work_.size(), nullptr),
                                          "compressEnd");
                    trailer_.assign(work_.data(), n);
                }
                return trailer_;
            }

        private:
            LZ4F_cctx*         cctx_ = nullptr;
            bool               created_ = false;
            bool               finished_ = false;
            LZ4F_preferences_t prefs_;
            std::string        header_;
            std::string        trailer_;    // finish() output, replayable
            std::vector<char>  work_;       // reused compress output buffer
        };

        // ── Pull-side codec: frame bytes in, plain bytes out ──────────────
        // Strict decoder over a pull-based byte source. fill(std::string&)
        // appends as much raw compressed input as it can cheaply provide and
        // returns false when it is permanently exhausted. Decoding never
        // slurps a file: output comes out in chunks of at most kChunkBytes.
        // Guarantees (checked by tests): corrupt / truncated / trailing-
        // garbage streams throw — the CLI's lenient partial-output policy is
        // intentionally NOT replicated; multi-frame concatenation and
        // skippable frames decode transparently.
        class Lz4FrameReader {
        public:
            Lz4FrameReader() {
                lz4f_check(LZ4F_createDecompressionContext(&dctx_, LZ4F_VERSION),
                           "createDecompressionContext");
                created_ = true;
            }

            ~Lz4FrameReader() {
                if (created_) LZ4F_freeDecompressionContext(dctx_);
            }

            Lz4FrameReader(const Lz4FrameReader&) = delete;
            Lz4FrameReader& operator=(const Lz4FrameReader&) = delete;

            /// Appends the next decoded chunk to `plain` (caller passes it
            /// empty); true == content produced. False only on a clean end of
            /// stream (EOF exactly at a frame boundary). Any other stop —
            /// truncated input, mid-stream bad magic, corrupt block, failed
            /// checksum — throws. Once thrown, the reader is poisoned and all
            /// further calls throw (the frame stream cannot resync).
            template<typename FillFn>
            bool next_chunk(FillFn&& fill, std::string& plain) {
                plain.clear();
                if (poisoned_) throw std::runtime_error("LZ4 frame: reader unusable after a decode error");
                if (end_of_stream_) return false;

                while (plain.size() < kChunkBytes) {
                    if (!in_frame_) {
                        if (!decode_frame_header(fill)) {   // false == clean EOF between frames
                            end_of_stream_ = true;
                            return !plain.empty();
                        }
                        continue;
                    }

                    if (pending_.empty() && !pull(fill)) {
                        throw std::runtime_error("LZ4 frame: truncated input mid-frame");
                    }

                    size_t srcSize = pending_.size();
                    size_t dstSize = dst_.size();
                    size_t ret = LZ4F_decompress(dctx_, dst_.data(), &dstSize,
                                                 pending_.data(), &srcSize, nullptr);
                    pending_.erase(0, srcSize);
                    if (LZ4F_isError(ret)) {
                        poisoned_ = true;
                        throw std::runtime_error(std::string("LZ4 frame: decompress: ") +
                                                 LZ4F_getErrorName(ret));
                    }
                    plain.append(dst_.data(), dstSize);   // only verified output
                    if (ret == 0) in_frame_ = false;      // frame complete → header/EOF next
                    // Stall guard: LZ4F accepted the bytes but neither consumed
                    // nor produced any (needs more input than the source has).
                    // Top up once; an exhausted source means a truncated frame.
                    if (srcSize == 0 && dstSize == 0 && ret != 0) {
                        if (!pull(fill)) {
                            throw std::runtime_error("LZ4 frame: truncated input mid-frame");
                        }
                    }
                }
                return true;
            }

        private:
            /// Append fresh source bytes into pending_ (stable storage);
            /// false == exhausted (or the source broke its own contract).
            template<typename FillFn>
            bool pull(FillFn&& fill) {
                std::string chunk;
                if (!fill(chunk) || chunk.empty()) return false;
                pending_.append(chunk);
                return true;
            }

            /// Consume the next frame's header (skipping any skippable frames
            /// in front of it) from pending_, filling via `fill` as needed.
            /// False == *clean* end of stream before a new frame starts.
            /// Throws on bad magic, corrupt/truncated header, or trailing
            /// garbage after the last frame.
            template<typename FillFn>
            bool decode_frame_header(FillFn&& fill) {
                while (true) {
                    while (pending_.size() < 4) {
                        if (!pull(fill)) {
                            if (pending_.empty()) return false;   // clean EOF between frames
                            throw std::runtime_error("LZ4 frame: trailing garbage after frame");
                        }
                    }
                    unsigned magic = read_u32_le(pending_.data());

                    // Skippable frame [magic][u32 len][payload]. Its payload
                    // is skippable BY DEFINITION, so it is discarded in
                    // bounded windows: a hostile header claiming a 4 GB
                    // payload must not make the reader buffer 4 GB to skip
                    // them. (size_t arithmetic: the u32 length can't wrap.)
                    if (is_skippable_magic(magic)) {
                        while (pending_.size() < 8) {
                            if (!pull(fill)) throw std::runtime_error("LZ4 frame: truncated skippable frame");
                        }
                        uint32_t payload = read_u32_le(pending_.data() + 4);
                        pending_.erase(0, 8);
                        size_t remaining = payload;
                        while (remaining > 0) {
                            size_t want = std::min(remaining, kChunkBytes);
                            while (pending_.size() < want) {
                                if (!pull(fill)) throw std::runtime_error("LZ4 frame: truncated skippable frame");
                            }
                            size_t take = std::min(want, pending_.size());
                            pending_.erase(0, take);
                            remaining -= take;
                        }
                        continue;   // look for the real frame behind it
                    }

                    if (magic != kFrameMagic) {
                        throw std::runtime_error("LZ4 frame: bad magic 0x" + to_hex(magic) +
                                                 " — stream is not a valid LZ4 frame");
                    }

                    // Header length is encoded in bytes 4..5 (FLG/BD):
                    // LZ4F_headerSize gives the full variable length before
                    // LZ4F_getFrameInfo is handed anything, so it never sees
                    // a short buffer.
                    while (pending_.size() < LZ4F_MIN_SIZE_TO_KNOW_HEADER_LENGTH) {
                        if (!pull(fill)) throw std::runtime_error("LZ4 frame: truncated frame header");
                    }
                    size_t hdr = LZ4F_headerSize(pending_.data(), pending_.size());
                    if (LZ4F_isError(hdr)) {
                        throw std::runtime_error(std::string("LZ4 frame: invalid frame header: ") +
                                                 LZ4F_getErrorName(hdr));
                    }
                    while (pending_.size() < hdr) {
                        if (!pull(fill)) throw std::runtime_error("LZ4 frame: truncated frame header");
                    }

                    size_t srcSize = pending_.size();
                    LZ4F_frameInfo_t info = LZ4F_INIT_FRAMEINFO;
                    size_t ret = LZ4F_getFrameInfo(dctx_, &info, pending_.data(), &srcSize);
                    if (LZ4F_isError(ret)) {
                        throw std::runtime_error(std::string("LZ4 frame: frame header: ") +
                                                 LZ4F_getErrorName(ret));
                    }
                    pending_.erase(0, srcSize);
                    in_frame_ = true;
                    return true;
                }
            }

            static constexpr size_t kChunkBytes = 64 * 1024;   // plain bytes per next_chunk call

            LZ4F_dctx*        dctx_ = nullptr;
            bool              created_ = false;
            bool              poisoned_ = false;      // decode error → dctx unusable
            std::string       pending_;               // stable compressed-byte storage
            std::vector<char> dst_ = std::vector<char>(kChunkBytes);
            bool              in_frame_ = false;
            bool              end_of_stream_ = false;
        };

        // ── streambuf adapter: frames -> plain bytes ──────────────────────
        // Forward-only (what getline needs); no seeking. A decoder error does
        // NOT throw through the istream machinery (whose default exception
        // policy would only set badbit anyway): it is latched into
        // hasError()/errorMessage(), and any plain bytes verified before the
        // failure are still handed out so iteration stops at the last good
        // row instead of also losing them. CsvReader checks hasError() after
        // every failed read so a corrupt/truncated .lz4 is an LZ4 error,
        // never a clean-looking early EOF.
        class Lz4FrameInputBuffer : public std::streambuf {
        public:
            explicit Lz4FrameInputBuffer(std::streambuf* source) : source_(source) {}

            bool hasError() const { return has_error_; }
            const std::string& errorMessage() const { return error_msg_; }

        protected:
            int underflow() override {
                if (gptr() && gptr() < egptr()) {
                    return traits_type::to_int_type(*gptr());
                }
                if (done_) return traits_type::eof();

                std::string plain;
                try {
                    if (!reader_.next_chunk([this](std::string& out) {
                            char tmp[16 * 1024];
                            std::streamsize n = source_->sgetn(tmp, sizeof(tmp));
                            if (n > 0) out.append(tmp, static_cast<size_t>(n));
                            return n > 0;
                        }, plain)) {
                        done_ = true;                 // clean end of stream
                    }
                } catch (const std::exception& ex) {
                    has_error_ = true;
                    error_msg_ = ex.what();
                    done_ = true;                     // no refill after an error
                }
                if (!plain.empty()) {                 // serve verified bytes first
                    area_ = std::move(plain);
                    char* begin = area_.data();
                    setg(begin, begin, begin + area_.size());
                    return traits_type::to_int_type(*begin);
                }
                return traits_type::eof();            // getline sees end-of-stream
            }

        private:
            std::streambuf* source_ = nullptr;
            Lz4FrameReader  reader_;
            std::string     area_;        // decoded get-area storage (stable until refill)
            std::string     error_msg_;
            bool            has_error_ = false;
            bool            done_ = false;
        };

        // ── streambuf adapter: plain bytes -> one LZ4 frame ───────────────
        // Frame parameters are fixed at construction (frame headers are
        // immutable). finalize() flushes the pending block, replays the
        // trailer and syncs. finalize() is retryable: only a trailer that
        // actually reached the sink marks it done, so a failed close can be
        // retried by the destructor (which swallows — same contract as
        // std::fstream).
        class Lz4FrameOutputBuffer : public std::streambuf {
        public:
            explicit Lz4FrameOutputBuffer(std::streambuf* sink, int block_max_size_id = 4,
                                          int compression_level = 0)
                : sink_(sink)
                , comp_(block_max_size_id, compression_level)
            {
                area_.reserve(kAreaBytes);
                emit(comp_.header());
            }

            ~Lz4FrameOutputBuffer() override {
                try { finalize(); } catch (...) { /* dtor must not throw */ }
            }

            void finalize() {
                if (finalized_) return;
                flush_area();               // area_ consumed before its emit (no dup on retry)
                if (!trailer_emitted_) {
                    emit(comp_.finish());       // replayable trailer (see compressor)
                    trailer_emitted_ = true;
                }
                if (sink_->pubsync() != 0) {
                    throw std::runtime_error("LZ4 frame: sink sync failed");
                }
                finalized_ = true;          // only once the trailer fully landed
            }

        protected:
            int overflow(int ch) override {
                if (finalized_) return traits_type::eof();   // write after finish: fail, like xsputn
                if (ch == traits_type::eof()) return 0;
                append_byte(static_cast<char>(ch));
                return ch;
            }

            std::streamsize xsputn(const char* s, std::streamsize n) override {
                if (finalized_) return 0;    // short write -> ostream sets failbit
                if (n > 0) {
                    // Row-shaped writes go straight into the compressor.
                    // area_ is consumed BEFORE pushing: if the emit of the
                    // produced bytes fails, the aggregation is not re-pushed
                    // on a retry (a lost tail on a failed write beats a
                    // re-compressed duplicate that corrupts the frame).
                    flush_area();
                    emit(comp_.push(std::string_view(s, static_cast<size_t>(n))));
                }
                return n;
            }

            int sync() override {
                if (!finalized_) {
                    flush_area();
                    sink_->pubsync();
                }
                return 0;
            }

        private:
            void append_byte(char c) {
                area_.push_back(c);
                if (area_.size() >= kAreaBytes) flush_area();
            }

            void flush_area() {
                if (area_.empty()) return;
                std::string out = comp_.push(std::string_view(area_.data(), area_.size()));
                area_.clear();
                emit(out);
            }

            void emit(std::string_view bytes) {
                if (bytes.empty()) return;
                if (sink_->sputn(bytes.data(), static_cast<std::streamsize>(bytes.size()))
                        != static_cast<std::streamsize>(bytes.size())) {
                    throw std::runtime_error("LZ4 frame: sink write failed (disk full?)");
                }
            }

            static constexpr size_t kAreaBytes = 8 * 1024;

            std::streambuf*    sink_ = nullptr;
            Lz4FrameCompressor comp_;
            std::string        area_;           // small put-side aggregation buffer
            bool               trailer_emitted_ = false;
            bool               finalized_ = false;
        };

    } // namespace detail

    // ── Public adapters (contracts: lz4_frame_stream.h) ───────────────────

    inline bool lz4_frame_sniff(std::string_view head) {
        if (head.size() < 4) return false;
        const unsigned magic = detail::read_u32_le(head.data());
        return magic == detail::kFrameMagic || detail::is_skippable_magic(magic);
    }

    inline Lz4FrameIStream::Lz4FrameIStream(std::streambuf* source)
        : std::istream(nullptr)
        , buf_(std::make_unique<detail::Lz4FrameInputBuffer>(source))
    {
        rdbuf(buf_.get());
    }

    inline Lz4FrameIStream::~Lz4FrameIStream() = default;

    inline bool Lz4FrameIStream::hasError() const { return buf_->hasError(); }
    inline const std::string& Lz4FrameIStream::errorMessage() const { return buf_->errorMessage(); }

    inline Lz4FrameOStream::Lz4FrameOStream(std::streambuf* sink, int block_max_size_id, int level)
        : std::ostream(nullptr)
        , buf_(std::make_unique<detail::Lz4FrameOutputBuffer>(sink, block_max_size_id, level))
    {
        rdbuf(buf_.get());
    }

    inline Lz4FrameOStream::~Lz4FrameOStream() {
        try { finish(); } catch (...) { /* dtor must not throw */ }
    }

    inline void Lz4FrameOStream::finish() { buf_->finalize(); }

} // namespace bcsv
