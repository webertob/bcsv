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
 * @brief RowCodecDispatch — runtime codec selection with managed lifetime.
 *
 * Runtime codec selection happens once (typically at file-open time) and hot
 * loops dispatch through pre-bound function pointers (serialize/deserialize/reset)
 * without per-row branching.
 *
 * Writer remains compile-time codec bound. This dispatch is intended for Reader
 * side runtime codec selection and similar runtime-driven paths.
 */

#include "row_codec_flat001.h"
#include "row_codec_zoh001.h"
#include "row_codec_delta002.h"
#include "definitions.h"

#include <cassert>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>

namespace bcsv {

enum class RowCodecId : uint8_t {
    FLAT001,
    ZOH001,
    DELTA002,
};

template<typename LayoutType>
class RowCodecDispatch {
    using RowType = typename LayoutType::RowType;
    using FlatCodec    = RowCodecFlat001<LayoutType>;
    using ZoHCodec     = RowCodecZoH001<LayoutType>;
    using DeltaCodec   = RowCodecDelta002<LayoutType>;

public:
    using SerializeFn = std::span<std::byte> (*)(void* codec, RowType& row, ByteBuffer& buffer);
    using DeserializeFn = void (*)(void* codec, std::span<const std::byte> buffer, RowType& row);
    using ResetFn = void (*)(void* codec);
    using DestroyFn = void (*)(void* codec);
    using CloneFn = void* (*)(const void* codec);

    RowCodecDispatch() = default;
    explicit RowCodecDispatch(const LayoutType& layout) : layout_(&layout) {}
    ~RowCodecDispatch() { destroy(); }

    RowCodecDispatch(const RowCodecDispatch& other) {
        copyFrom(other);
    }

    RowCodecDispatch& operator=(const RowCodecDispatch& other) {
        if (this != &other) {
            destroy();
            copyFrom(other);
        }
        return *this;
    }

    RowCodecDispatch(RowCodecDispatch&& other) noexcept {
        moveFrom(std::move(other));
    }

    RowCodecDispatch& operator=(RowCodecDispatch&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(std::move(other));
        }
        return *this;
    }

    void setLayout(const LayoutType& layout) noexcept {
        layout_ = &layout;
    }

    void setup(RowCodecId id) {
        if (!layout_) {
            throw std::logic_error("RowCodecDispatch::setup() failed: layout is not set");
        }

        destroy();
        switch (id) {
            case RowCodecId::FLAT001: {
                auto* codec = new FlatCodec();
                codec->setup(*layout_);
                ctx_ = codec;
                codec_id_ = id;
                serialize_fn_ = &serializeFlat;
                deserialize_fn_ = &deserializeFlat;
                reset_fn_ = &resetFlat;
                destroy_fn_ = &destroyFlat;
                clone_fn_ = &cloneFlat;
                break;
            }
            case RowCodecId::ZOH001: {
                auto* codec = new ZoHCodec();
                codec->setup(*layout_);
                ctx_ = codec;
                codec_id_ = id;
                serialize_fn_ = &serializeZoH;
                deserialize_fn_ = &deserializeZoH;
                reset_fn_ = &resetZoH;
                destroy_fn_ = &destroyZoH;
                clone_fn_ = &cloneZoH;
                break;
            }
            case RowCodecId::DELTA002: {
                auto* codec = new DeltaCodec();
                codec->setup(*layout_);
                ctx_ = codec;
                codec_id_ = id;
                serialize_fn_ = &serializeDelta;
                deserialize_fn_ = &deserializeDelta;
                reset_fn_ = &resetDelta;
                destroy_fn_ = &destroyDelta;
                clone_fn_ = &cloneDelta;
                break;
            }
            default:
                throw std::logic_error("RowCodecDispatch::setup() failed: unsupported codec id");
        }
    }

    void setup(RowCodecId id, const LayoutType& layout) {
        layout_ = &layout;
        setup(id);
    }

    void selectCodec(FileFlags flags, const LayoutType& layout) {
        RowCodecId id;
        if ((flags & FileFlags::DELTA_ENCODING) != FileFlags::NONE)
            id = RowCodecId::DELTA002;
        else if ((flags & FileFlags::ZERO_ORDER_HOLD) != FileFlags::NONE)
            id = RowCodecId::ZOH001;
        else
            id = RowCodecId::FLAT001;
        setup(id, layout);
    }

    void destroy() noexcept {
        if (ctx_ != nullptr && destroy_fn_ != nullptr) {
            destroy_fn_(ctx_);
        }
        ctx_ = nullptr;
        serialize_fn_ = nullptr;
        deserialize_fn_ = nullptr;
        reset_fn_ = nullptr;
        destroy_fn_ = nullptr;
        clone_fn_ = nullptr;
    }

    std::span<std::byte> serialize(RowType& row, ByteBuffer& buffer) const {
        assert(ctx_ != nullptr && serialize_fn_ != nullptr && "RowCodecDispatch::serialize() before setup()");
        return serialize_fn_(ctx_, row, buffer);
    }

    void deserialize(std::span<const std::byte> buffer, RowType& row) const {
        assert(ctx_ != nullptr && deserialize_fn_ != nullptr && "RowCodecDispatch::deserialize() before setup()");
        deserialize_fn_(ctx_, buffer, row);
    }

    void reset() const {
        assert(ctx_ != nullptr && reset_fn_ != nullptr && "RowCodecDispatch::reset() before setup()");
        reset_fn_(ctx_);
    }

    bool isSetup() const noexcept { return ctx_ != nullptr; }
    bool isZoH() const noexcept { return isSetup() && codec_id_ == RowCodecId::ZOH001; }
    bool isFlat() const noexcept { return isSetup() && codec_id_ == RowCodecId::FLAT001; }
    bool isDelta() const noexcept { return isSetup() && codec_id_ == RowCodecId::DELTA002; }
    RowCodecId codecId() const noexcept { return codec_id_; }

private:
    const LayoutType* layout_{nullptr};
    RowCodecId codec_id_{RowCodecId::FLAT001};
    void* ctx_{nullptr};
    SerializeFn serialize_fn_{nullptr};
    DeserializeFn deserialize_fn_{nullptr};
    ResetFn reset_fn_{nullptr};
    DestroyFn destroy_fn_{nullptr};
    CloneFn clone_fn_{nullptr};

    void copyFrom(const RowCodecDispatch& other) {
        layout_ = other.layout_;
        codec_id_ = other.codec_id_;
        serialize_fn_ = other.serialize_fn_;
        deserialize_fn_ = other.deserialize_fn_;
        reset_fn_ = other.reset_fn_;
        destroy_fn_ = other.destroy_fn_;
        clone_fn_ = other.clone_fn_;

        if (other.ctx_ != nullptr && clone_fn_ != nullptr) {
            ctx_ = clone_fn_(other.ctx_);
        } else {
            ctx_ = nullptr;
        }
    }

    void moveFrom(RowCodecDispatch&& other) noexcept {
        layout_ = other.layout_;
        codec_id_ = other.codec_id_;
        ctx_ = other.ctx_;
        serialize_fn_ = other.serialize_fn_;
        deserialize_fn_ = other.deserialize_fn_;
        reset_fn_ = other.reset_fn_;
        destroy_fn_ = other.destroy_fn_;
        clone_fn_ = other.clone_fn_;

        other.ctx_ = nullptr;
        other.serialize_fn_ = nullptr;
        other.deserialize_fn_ = nullptr;
        other.reset_fn_ = nullptr;
        other.destroy_fn_ = nullptr;
        other.clone_fn_ = nullptr;
    }

    static std::span<std::byte> serializeFlat(void* codec, RowType& row, ByteBuffer& buffer) {
        return static_cast<FlatCodec*>(codec)->serialize(row, buffer);
    }

    static std::span<std::byte> serializeZoH(void* codec, RowType& row, ByteBuffer& buffer) {
        return static_cast<ZoHCodec*>(codec)->serialize(row, buffer);
    }

    static void deserializeFlat(void* codec, std::span<const std::byte> buffer, RowType& row) {
        static_cast<FlatCodec*>(codec)->deserialize(buffer, row);
    }

    static void deserializeZoH(void* codec, std::span<const std::byte> buffer, RowType& row) {
        static_cast<ZoHCodec*>(codec)->deserialize(buffer, row);
    }

    static void resetFlat(void* codec) {
        static_cast<FlatCodec*>(codec)->reset();
    }

    static void resetZoH(void* codec) {
        static_cast<ZoHCodec*>(codec)->reset();
    }

    static void destroyFlat(void* codec) {
        delete static_cast<FlatCodec*>(codec);
    }

    static void destroyZoH(void* codec) {
        delete static_cast<ZoHCodec*>(codec);
    }

    static void* cloneFlat(const void* codec) {
        return new FlatCodec(*static_cast<const FlatCodec*>(codec));
    }

    static void* cloneZoH(const void* codec) {
        return new ZoHCodec(*static_cast<const ZoHCodec*>(codec));
    }

    static std::span<std::byte> serializeDelta(void* codec, RowType& row, ByteBuffer& buffer) {
        return static_cast<DeltaCodec*>(codec)->serialize(row, buffer);
    }

    static void deserializeDelta(void* codec, std::span<const std::byte> buffer, RowType& row) {
        static_cast<DeltaCodec*>(codec)->deserialize(buffer, row);
    }

    static void resetDelta(void* codec) {
        static_cast<DeltaCodec*>(codec)->reset();
    }

    static void destroyDelta(void* codec) {
        delete static_cast<DeltaCodec*>(codec);
    }

    static void* cloneDelta(const void* codec) {
        return new DeltaCodec(*static_cast<const DeltaCodec*>(codec));
    }
};

} // namespace bcsv
