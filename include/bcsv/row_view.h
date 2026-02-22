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
 * @file row_view.h
 * @brief ARCHIVED — RowView and RowViewStatic class declarations.
 *
 * These classes are NOT part of the active bcsv API. They are preserved here
 * for potential future sparse-access work. RowView did not deliver the intended
 * speed gains for sparse read/write access while adding significant complexity.
 *
 * To use: include this file directly (it is NOT included from bcsv.h).
 * Dependencies: row.h, layout.h, row_codec_flat001.h
 *
 * Last commit containing the active codec sparse-path: 8575319 (git log -1 8575319)
 */

#include "definitions.h"
#include "layout.h"
#include "row.h"
#include "row_codec_flat001.h"

namespace bcsv {

    /* RowView provides a direct view into a serialized buffer, partially supporting the row interface.
       Intended for sparse data access, avoiding costly full deserialization.
       Currently only supports the basic get/set interface for individual columns, into a flat serialized buffer.
       Does not support ZoH format or more complex encoding schemes.

       ARCHIVED: Not part of the active bcsv API.
    */
    class RowView {
        // Immutable after construction
        Layout                  layout_;        // Shared layout data (no callbacks needed for views)
        RowCodecFlat001<Layout, TrackingPolicy::Disabled> codec_; // Wire-format metadata + per-column offsets

    public:
        RowView() = delete;
        RowView(const Layout& layout, std::span<std::byte> buffer = {});
        RowView(const RowView& other);
        RowView(RowView&& other) noexcept;
        ~RowView() = default;

        [[nodiscard]] const std::span<std::byte>& buffer() const noexcept                   { return codec_.buffer(); }
        const Layout&               layout() const noexcept                                 { return layout_; }
        void                        setBuffer(const std::span<std::byte> &buffer) noexcept  { codec_.setBuffer(buffer); }
        [[nodiscard]] Row           toRow() const;
        [[nodiscard]] bool          validate(bool deepValidation = true) const;

        [[nodiscard]] std::span<const std::byte>  get(size_t index) const;
                                    template<typename T>
        [[nodiscard]] T             get(size_t index) const;
                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, std::span<T> &dst) const;
                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T &dst) const;

                                    template<typename T>
        [[nodiscard]] bool          set(size_t index, const T& value);
                                    template<typename T>
        [[nodiscard]] bool          set(size_t index, std::span<const T> src);

                                    template<RowVisitorConst Visitor>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

                                    template<RowVisitorConst Visitor>
        void                        visitConst(Visitor&& visitor) const;

                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

                                    template<typename T, typename Visitor>
                                        requires TypedRowVisitor<Visitor, T>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

                                    template<typename T, typename Visitor>
                                        requires TypedRowVisitorConst<Visitor, T>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

        RowView& operator=(const RowView& other);
        RowView& operator=(RowView&& other) noexcept;
    };


    /* Provides a zero-copy view into a buffer with compile-time layout.
       Intended for sparse data access, avoiding costly full deserialization.
       Currently only supports the basic get/set interface into a flat serialized buffer.
       Does not support ZoH format or more complex encoding schemes. Change tracking is not supported.

       ARCHIVED: Not part of the active bcsv API.
    */
    template<typename... ColumnTypes>
    class RowViewStatic {
    public:
        using LayoutType = LayoutStatic<ColumnTypes...>;
        using Codec = RowCodecFlat001<LayoutType, TrackingPolicy::Disabled>;
        static constexpr size_t COLUMN_COUNT = LayoutType::columnCount();

        template<size_t Index>
        using column_type = std::tuple_element_t<Index, typename LayoutType::ColTypes>;

        // ── Flat wire-format constants (sourced from codec — single source of truth) ──
        static constexpr size_t BOOL_COUNT      = Codec::BOOL_COUNT;
        static constexpr size_t STRING_COUNT    = Codec::STRING_COUNT;
        static constexpr size_t WIRE_BITS_SIZE  = Codec::WIRE_BITS_SIZE;
        static constexpr size_t WIRE_DATA_SIZE  = Codec::WIRE_DATA_SIZE;
        static constexpr size_t WIRE_STRG_COUNT = Codec::WIRE_STRG_COUNT;
        static constexpr size_t WIRE_FIXED_SIZE = Codec::WIRE_FIXED_SIZE;
        static constexpr auto   WIRE_OFFSETS    = Codec::WIRE_OFFSETS;


    private:
        LayoutType              layout_;
        Codec                   codec_;

    public:

        RowViewStatic() = delete;
        RowViewStatic(const LayoutType& layout, std::span<std::byte> buffer = {})
            : layout_(layout), codec_() {
            codec_.setup(layout_);
            codec_.setBuffer(buffer);
        }
        RowViewStatic(const RowViewStatic& other);
        RowViewStatic(RowViewStatic&& other) noexcept;
        ~RowViewStatic() = default;

        const std::span<std::byte>& buffer() const noexcept                                     { return codec_.buffer(); }
        const LayoutType&           layout() const noexcept                                     { return layout_; }
        void                        setBuffer(const std::span<std::byte> &buffer) noexcept      { codec_.setBuffer(buffer); }

        // =========================================================================
        // 1. Static Access (Compile-Time) - Zero Copy where possible
        // =========================================================================
                                    template<size_t Index>
        [[nodiscard]] auto          get() const;

                                    template<size_t StartIndex, typename T, size_t Extent>
        void                        get(std::span<T, Extent> &dst) const;

                                    template<size_t Index, typename T>
        void                        set(const T& value) noexcept;

                                    template<size_t StartIndex, typename T, size_t Extent>
        void                        set(std::span<const T, Extent> values) noexcept;

        // =========================================================================
        // 2. Dynamic Access (Runtime Index)
        // =========================================================================
        [[nodiscard]] std::span<const std::byte>  get(size_t index) const noexcept;

                                    template<typename T, size_t Extent>
        [[nodiscard]] bool          get(size_t index, std::span<T, Extent>& dst) const noexcept;

                                    template<typename T>
        [[nodiscard]] bool          get(size_t index, T& dst) const noexcept;

                                    template<typename T>
        void                        set(size_t index, const T& value) noexcept;

                                    template<typename T, size_t Extent>
        void                        set(size_t index, std::span<const T, Extent> values) noexcept;

        // =========================================================================
        // 3. Conversion / Validation
        // =========================================================================
        [[nodiscard]] RowStatic<ColumnTypes...>   toRow() const;
        [[nodiscard]] bool          validate() const noexcept;

                                    template<RowVisitorConst Visitor>
        void                        visitConst(size_t startIndex, Visitor&& visitor, size_t count = 1) const;

                                    template<RowVisitorConst Visitor>
        void                        visitConst(Visitor&& visitor) const;

                                    template<RowVisitor Visitor>
        void                        visit(size_t startIndex, Visitor&& visitor, size_t count = 1);

                                    template<RowVisitor Visitor>
        void                        visit(Visitor&& visitor);

        RowViewStatic& operator=(const RowViewStatic& other) noexcept;
        RowViewStatic& operator=(RowViewStatic&& other) noexcept;

    private:
        template<size_t Index = 0>
        bool validateStringPayloads() const;
    };

} // namespace bcsv

// Include implementations
#include "row_view.hpp"
