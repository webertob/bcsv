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
 * @file row_codec_dispatch.h
 * @brief CodecDispatch — runtime codec selection for the Reader.
 *
 * Resolves the concrete codec (Flat001 or ZoH001) at file-open time based on
 * file flags. Hot-path methods (deserialize, reset) use function pointers —
 * one indirect call per row, zero per-row branching.
 *
 * TrackingPolicy and file codec are orthogonal axes. The programmer chooses
 * the Policy (Enabled/Disabled) when constructing the Reader. The file's codec
 * (Flat/ZoH) is determined by its header flags. CodecDispatch bridges the two:
 *   - selectCodec() reads the file flags, constructs the correct codec in
 *     union storage, and wires function pointers.
 *   - deserialize()/reset() forward through function pointers.
 *
 * The Writer retains compile-time RowCodecType (std::conditional_t) — it knows
 * what codec it writes. Runtime flexibility is a Reader concern.
 *
 * @see row_codec_flat001.h, row_codec_zoh001.h for the concrete codecs.
 * @see ITEM_11_PLAN.md §7.3 for design rationale.
 */

#include "row_codec_flat001.h"
#include "row_codec_zoh001.h"
#include "definitions.h"

#include <cassert>
#include <cstddef>
#include <span>

namespace bcsv {

// ────────────────────────────────────────────────────────────────────────────
// Primary template: CodecDispatch for dynamic Layout
// ────────────────────────────────────────────────────────────────────────────
template<typename LayoutType, TrackingPolicy Policy = TrackingPolicy::Disabled>
class CodecDispatch {
    using RowType = typename LayoutType::template RowType<Policy>;
    using FlatCodec = RowCodecFlat001<LayoutType, Policy>;
    using ZoHCodec  = RowCodecZoH001<LayoutType, Policy>;

public:
    // ── Function-pointer types ───────────────────────────────────────────
    using DeserializeFn = void (*)(const void* codec,
                                    std::span<const std::byte> buffer,
                                    RowType& row);
    using ResetFn       = void (*)(const void* codec);

    CodecDispatch() = default;
    ~CodecDispatch() { destroy(); }

    // Non-copyable, non-movable (codec contains layout pointer — not relocatable)
    CodecDispatch(const CodecDispatch&) = delete;
    CodecDispatch& operator=(const CodecDispatch&) = delete;

    // ── Codec selection (called once at file-open time) ──────────────────

    /// Select and initialize the codec based on file flags.
    /// Reads ZERO_ORDER_HOLD to choose ZoH001 vs Flat001.
    void selectCodec(FileFlags flags, const LayoutType& layout) {
        destroy();

        if ((flags & FileFlags::ZERO_ORDER_HOLD) != FileFlags::NONE) {
            auto* codec = new (&storage_.zoh) ZoHCodec();
            codec->setup(layout);
            active_ = ActiveCodec::ZoH;
            deserialize_fn_ = &deserializeZoH;
            reset_fn_       = &resetZoH;
        } else {
            auto* codec = new (&storage_.flat) FlatCodec();
            codec->setup(layout);
            active_ = ActiveCodec::Flat;
            deserialize_fn_ = &deserializeFlat;
            reset_fn_       = &resetFlat;
        }
    }

    // ── Hot-path methods (function-pointer dispatch) ─────────────────────

    void deserialize(std::span<const std::byte> buffer, RowType& row) const {
        assert(active_ != ActiveCodec::None && "CodecDispatch::deserialize() before selectCodec()");
        deserialize_fn_(activePtr(), buffer, row);
    }

    void reset() const {
        assert(active_ != ActiveCodec::None && "CodecDispatch::reset() before selectCodec()");
        reset_fn_(activePtr());
    }

    // ── Query ────────────────────────────────────────────────────────────
    bool isSetup() const noexcept { return active_ != ActiveCodec::None; }
    bool isZoH()   const noexcept { return active_ == ActiveCodec::ZoH; }
    bool isFlat()  const noexcept { return active_ == ActiveCodec::Flat; }

    /// Access the flat codec (always available — ZoH composes flat internally).
    /// Used for wire metadata queries (wireBitsSize, wireFixedSize, etc.).
    const FlatCodec& flat() const noexcept {
        if (active_ == ActiveCodec::ZoH) {
            return storage_.zoh.flat();
        }
        return storage_.flat;
    }

private:
    enum class ActiveCodec : uint8_t { None, Flat, ZoH };

    union Storage {
        FlatCodec flat;
        ZoHCodec  zoh;
        Storage() {}   // No default construction of members
        ~Storage() {}  // Destruction handled by CodecDispatch::destroy()
    };

    Storage       storage_;
    ActiveCodec   active_{ActiveCodec::None};
    DeserializeFn deserialize_fn_{nullptr};
    ResetFn       reset_fn_{nullptr};

    const void* activePtr() const noexcept {
        switch (active_) {
            case ActiveCodec::Flat: return &storage_.flat;
            case ActiveCodec::ZoH:  return &storage_.zoh;
            default:                return nullptr;
        }
    }

    void destroy() noexcept {
        switch (active_) {
            case ActiveCodec::Flat: storage_.flat.~FlatCodec(); break;
            case ActiveCodec::ZoH:  storage_.zoh.~ZoHCodec();  break;
            case ActiveCodec::None: break;
        }
        active_ = ActiveCodec::None;
    }

    // ── Static trampolines (function-pointer targets) ────────────────────
    static void deserializeFlat(const void* codec,
                                 std::span<const std::byte> buffer,
                                 RowType& row) {
        static_cast<const FlatCodec*>(codec)->deserialize(buffer, row);
    }

    static void deserializeZoH(const void* codec,
                                std::span<const std::byte> buffer,
                                RowType& row) {
        static_cast<const ZoHCodec*>(codec)->deserialize(buffer, row);
    }

    static void resetFlat(const void* codec) {
        // Flat codec reset is a no-op, but call it for interface consistency.
        (void)codec;
    }

    static void resetZoH(const void* codec) {
        const_cast<ZoHCodec*>(static_cast<const ZoHCodec*>(codec))->reset();
    }
};


// ────────────────────────────────────────────────────────────────────────────
// Partial specialization: CodecDispatch for LayoutStatic<ColumnTypes...>
//
// Unlike the dynamic primary template, this specialization uses direct calls
// instead of function pointers. The codec types (FlatCodec, ZoHCodec) are
// fully known at compile time — only the Flat-vs-ZoH choice is runtime.
// A single predictable branch replaces the indirect call, allowing the
// compiler to inline deserialize/reset into the Reader's hot loop.
// ────────────────────────────────────────────────────────────────────────────
template<TrackingPolicy Policy, typename... ColumnTypes>
class CodecDispatch<LayoutStatic<ColumnTypes...>, Policy> {
    using LayoutType = LayoutStatic<ColumnTypes...>;
    using RowType    = typename LayoutType::template RowType<Policy>;
    using FlatCodec  = RowCodecFlat001<LayoutType, Policy>;
    using ZoHCodec   = RowCodecZoH001<LayoutType, Policy>;

public:
    CodecDispatch() = default;
    ~CodecDispatch() { destroy(); }

    CodecDispatch(const CodecDispatch&) = delete;
    CodecDispatch& operator=(const CodecDispatch&) = delete;

    void selectCodec(FileFlags flags, const LayoutType& layout) {
        destroy();

        if ((flags & FileFlags::ZERO_ORDER_HOLD) != FileFlags::NONE) {
            new (&storage_.zoh) ZoHCodec();
            storage_.zoh.setup(layout);
            active_ = ActiveCodec::ZoH;
        } else {
            new (&storage_.flat) FlatCodec();
            storage_.flat.setup(layout);
            active_ = ActiveCodec::Flat;
        }
    }

    // Direct-call dispatch — branch is 100% predictable (same codec for entire
    // file), and the compiler can inline both FlatCodec::deserialize and
    // ZoHCodec::deserialize since the call targets are statically known.
    void deserialize(std::span<const std::byte> buffer, RowType& row) const {
        assert(active_ != ActiveCodec::None && "CodecDispatch::deserialize() before selectCodec()");
        if (active_ == ActiveCodec::ZoH) {
            storage_.zoh.deserialize(buffer, row);
        } else {
            storage_.flat.deserialize(buffer, row);
        }
    }

    void reset() const {
        assert(active_ != ActiveCodec::None && "CodecDispatch::reset() before selectCodec()");
        if (active_ == ActiveCodec::ZoH) {
            const_cast<ZoHCodec&>(storage_.zoh).reset();
        }
        // Flat reset is a no-op — skip entirely.
    }

    bool isSetup() const noexcept { return active_ != ActiveCodec::None; }
    bool isZoH()   const noexcept { return active_ == ActiveCodec::ZoH; }
    bool isFlat()  const noexcept { return active_ == ActiveCodec::Flat; }

    const FlatCodec& flat() const noexcept {
        if (active_ == ActiveCodec::ZoH) {
            return storage_.zoh.flat();
        }
        return storage_.flat;
    }

private:
    enum class ActiveCodec : uint8_t { None, Flat, ZoH };

    union Storage {
        FlatCodec flat;
        ZoHCodec  zoh;
        Storage() {}
        ~Storage() {}
    };

    Storage     storage_;
    ActiveCodec active_{ActiveCodec::None};

    void destroy() noexcept {
        switch (active_) {
            case ActiveCodec::Flat: storage_.flat.~FlatCodec(); break;
            case ActiveCodec::ZoH:  storage_.zoh.~ZoHCodec();  break;
            case ActiveCodec::None: break;
        }
        active_ = ActiveCodec::None;
    }
};

} // namespace bcsv
