/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "row.h"
#include "sampler_vm.h"

#include <cstdint>
#include <vector>
#include <stdexcept>

namespace bcsv {

    // ── Boundary handling ───────────────────────────────────────────

    enum class BoundaryMode : uint8_t {
        TRUNCATE,   // Skip rows where window is incomplete at boundaries
        EXPAND,     // Clamp out-of-bounds references to edge row
    };

    // ── RowWindow ───────────────────────────────────────────────────
    //
    // Circular buffer of Row snapshots for sliding-window expressions.
    // The window size is determined by the expression's row-offset range
    // (min_offset..max_offset), computed by the TypeResolver.
    //
    // Memory: rows are allocated once and reused via Row::operator= to
    // avoid heap allocations in steady state.

    class RowWindow {
    public:
        /// Construct a window for the given Layout and row-offset range.
        /// min_offset ≤ 0 (lookbehind), max_offset ≥ 0 (lookahead).
        RowWindow(const Layout& layout, int16_t min_offset, int16_t max_offset,
                  BoundaryMode mode = BoundaryMode::TRUNCATE)
            : min_offset_(min_offset)
            , max_offset_(max_offset)
            , mode_(mode)
            , lookbehind_(static_cast<size_t>(min_offset < 0 ? -min_offset : 0))
            , lookahead_(static_cast<size_t>(max_offset > 0 ? max_offset : 0))
            , capacity_(lookbehind_ + 1 + lookahead_)
            , cursor_(0)
            , count_(0)
            , total_pushed_(0)
        {
            // Pre-allocate all Row objects with the same layout
            slots_.reserve(capacity_);
            for (size_t i = 0; i < capacity_; ++i) {
                slots_.emplace_back(layout);
            }
        }

        /// Push a new row into the window (copy).
        /// The window advances: the new row becomes the "newest" slot.
        void push(const Row& row) {
            size_t slot = (cursor_ + count_) % capacity_;
            if (count_ < capacity_) {
                slots_[slot] = row;
                ++count_;
            } else {
                // Circular overwrite — oldest slot becomes newest
                slots_[cursor_] = row;
                cursor_ = (cursor_ + 1) % capacity_;
            }
            ++total_pushed_;
        }

        /// Is the window full enough to evaluate the current row?
        /// During the filling phase, we need at least (lookbehind + 1 + lookahead)
        /// rows before the first evaluation can happen.
        bool ready() const {
            if (mode_ == BoundaryMode::EXPAND) {
                return count_ >= 1;  // EXPAND mode always has something to clamp to
            }
            return count_ >= capacity_;
        }

        // ── Draining phase (B3) ─────────────────────────────────────
        //
        // After EOF, the last `lookahead_` rows have been pushed but
        // never became "current".  advanceDrain() advances the virtual
        // current pointer forward by one position without pushing new
        // data.  `resolve()` clamps out-of-range offsets (EXPAND mode).

        /// Are there remaining rows to drain after EOF?
        bool hasDrains() const { return drain_step_ < lookahead_; }

        /// Number of drain steps remaining
        size_t drainsRemaining() const { return lookahead_ - drain_step_; }

        /// Advance one drain step (call after EOF, before evaluating)
        void advanceDrain() { ++drain_step_; }

        /// Get the current row (row_offset = 0).
        /// The "current" row is at lookbehind positions from the start.
        const Row& current() const {
            return resolve(0);
        }

        /// Resolve a row reference by offset.
        /// offset 0 = current, -1 = previous, +1 = next, etc.
        const Row& resolve(int16_t offset) const {
            // Current row index in the circular buffer (linear coordinates)
            int64_t newest_linear = static_cast<int64_t>(total_pushed_) - 1;
            int64_t oldest_linear;
            if (count_ < capacity_) {
                oldest_linear = 0;
            } else {
                oldest_linear = static_cast<int64_t>(total_pushed_) -
                                static_cast<int64_t>(capacity_);
            }

            // Current row = newest row minus remaining lookahead
            int64_t current_linear = newest_linear -
                static_cast<int64_t>(lookahead_) +
                static_cast<int64_t>(drain_step_);
            int64_t target_linear = current_linear + offset;

            // Clamp to valid range
            if (mode_ == BoundaryMode::EXPAND) {
                if (target_linear < oldest_linear) target_linear = oldest_linear;
                if (target_linear > newest_linear) target_linear = newest_linear;
            }

            // Map to circular buffer slot
            size_t slot = static_cast<size_t>(
                ((target_linear % static_cast<int64_t>(capacity_)) +
                 static_cast<int64_t>(capacity_)) % static_cast<int64_t>(capacity_));
            return slots_[slot];
        }

        /// Create a RowAccessor function for the VM
        RowAccessor accessor() const {
            return [this](int16_t offset) -> const Row& {
                return this->resolve(offset);
            };
        }

        /// Number of rows currently in the window
        size_t count() const { return count_; }

        /// Total capacity
        size_t capacity() const { return capacity_; }

        /// Total rows pushed so far
        size_t totalPushed() const { return total_pushed_; }

        /// Reset the window for reuse
        void reset() {
            cursor_ = 0;
            count_ = 0;
            total_pushed_ = 0;
            drain_step_ = 0;
        }

        /// Lookbehind depth
        size_t lookbehind() const { return lookbehind_; }

        /// Lookahead depth
        size_t lookahead() const { return lookahead_; }

        /// Min offset used
        int16_t minOffset() const { return min_offset_; }

        /// Max offset used
        int16_t maxOffset() const { return max_offset_; }

    private:
        int16_t      min_offset_;
        int16_t      max_offset_;
        BoundaryMode mode_;
        size_t       lookbehind_;
        size_t       lookahead_;
        size_t       capacity_;
        size_t       cursor_;      // index of oldest slot in circular buffer
        size_t       count_;       // number of valid rows in the buffer
        size_t       total_pushed_;
        size_t       drain_step_ = 0;  // draining steps after EOF (B3)

        std::vector<Row> slots_;   // circular buffer of Row objects
    };

} // namespace bcsv
