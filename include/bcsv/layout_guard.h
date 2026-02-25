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
 * @file layout_guard.h
 * @brief RAII guard that prevents structural mutations on a Layout while held.
 *
 * Codecs acquire a LayoutGuard during setup() and release it on destruction
 * or move-assignment.  While at least one guard is held, the six structural mutation
 * methods on Layout::Data (addColumn, removeColumn, setColumnType, setColumns,
 * clear) throw std::logic_error.  setColumnName is excluded — it is benign
 * to codecs (does not change types, offsets, or wire metadata).
 *
 * Multiple guards may reference the same Layout::Data concurrently (parallel
 * read access is safe).  The internal counter uses std::atomic for race-free
 * increment/decrement, even though the library as a whole is not thread-safe.
 *
 * Lifetime note: the guard holds a std::shared_ptr<Layout::Data>, so the Data
 * stays alive as long as any guard (or codec) references it — even if all
 * Layout facade objects have been destroyed.
 *
 * @see ARCHITECTURE.md, docs/ERROR_HANDLING.md
 */

#include "layout.h"
#include <memory>
#include <utility>

namespace bcsv {

/**
 * @brief RAII guard that locks a Layout::Data against structural mutations.
 *
 * - Movable, non-copyable.
 * - Increments Layout::Data::structural_lock_count_ on construction.
 * - Decrements on destruction (or explicit release()).
 * - A default-constructed guard holds no lock.
 */
class LayoutGuard {
public:
    // Default: no lock held.
    LayoutGuard() noexcept = default;

    /// Acquire a structural lock on the given Layout::Data.
    explicit LayoutGuard(Layout::DataPtr data) noexcept
        : data_(std::move(data))
    {
        if (data_) {
            data_->acquireStructuralLock();
        }
    }

    ~LayoutGuard() {
        release();
    }

    // Non-copyable — each guard represents one lock reference.
    LayoutGuard(const LayoutGuard&) = delete;
    LayoutGuard& operator=(const LayoutGuard&) = delete;

    // Movable — transfers lock ownership.
    LayoutGuard(LayoutGuard&& other) noexcept
        : data_(std::move(other.data_))
    {
        other.data_ = nullptr;  // ensure moved-from guard releases nothing
    }

    LayoutGuard& operator=(LayoutGuard&& other) noexcept {
        if (this != &other) {
            release();
            data_ = std::move(other.data_);
            other.data_ = nullptr;
        }
        return *this;
    }

    /// Explicitly release the lock (idempotent).
    void release() noexcept {
        if (data_) {
            data_->releaseStructuralLock();
            data_ = nullptr;
        }
    }

    /// Check whether this guard is actively holding a lock.
    bool isLocked() const noexcept { return data_ != nullptr; }

    explicit operator bool() const noexcept { return isLocked(); }

private:
    Layout::DataPtr data_{nullptr};
};

} // namespace bcsv
