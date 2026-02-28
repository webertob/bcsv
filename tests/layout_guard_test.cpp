/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file layout_guard_test.cpp
 * @brief Tests for LayoutGuard — RAII structural lock on Layout::Data.
 *
 * Verifies:
 *   - Guard prevents structural mutations (addColumn, removeColumn, setColumnType,
 *     setColumns, clear) while held.
 *   - setColumnName is allowed while guard is held (benign to codecs).
 *   - Guard release re-enables mutations.
 *   - Multiple guards can coexist on the same Layout::Data.
 *   - RAII: guard destructor releases the lock.
 *   - Codec setup acquires the guard; destruction/move-assign releases it.
 *   - Writer/Reader close releases the guard.
 *   - Row observer pattern still works independently of the guard.
 */

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <filesystem>

#include <bcsv/bcsv.h>

namespace {

using namespace bcsv;

// ── Helpers ──────────────────────────────────────────────────────────────────

Layout makeTestLayout() {
    Layout layout;
    layout.addColumn({"b1",  ColumnType::BOOL});
    layout.addColumn({"i32", ColumnType::INT32});
    layout.addColumn({"d",   ColumnType::DOUBLE});
    layout.addColumn({"s",   ColumnType::STRING});
    return layout;
}

// ═════════════════════════════════════════════════════════════════════════════
// LayoutGuard basics
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, DefaultConstructedIsNotLocked) {
    LayoutGuard guard;
    EXPECT_FALSE(guard.isLocked());
    EXPECT_FALSE(static_cast<bool>(guard));
}

TEST(LayoutGuard, AcquiredGuardIsLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_TRUE(guard.isLocked());
    EXPECT_TRUE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, ReleaseUnlocksLayout) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_TRUE(layout.isStructurallyLocked());
    guard.release();
    EXPECT_FALSE(guard.isLocked());
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, ReleaseIsIdempotent) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    guard.release();
    guard.release();  // Should not crash or underflow
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, DestructorReleasesLock) {
    Layout layout = makeTestLayout();
    {
        LayoutGuard guard(layout.data());
        EXPECT_TRUE(layout.isStructurallyLocked());
    } // guard destroyed here
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, MoveConstructTransfersLock) {
    Layout layout = makeTestLayout();
    LayoutGuard guard1(layout.data());
    LayoutGuard guard2(std::move(guard1));
    EXPECT_FALSE(guard1.isLocked());
    EXPECT_TRUE(guard2.isLocked());
    EXPECT_TRUE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, MoveAssignTransfersLock) {
    Layout layout = makeTestLayout();
    LayoutGuard guard1(layout.data());
    LayoutGuard guard2;
    guard2 = std::move(guard1);
    EXPECT_FALSE(guard1.isLocked());
    EXPECT_TRUE(guard2.isLocked());
    EXPECT_TRUE(layout.isStructurallyLocked());
}

// ═════════════════════════════════════════════════════════════════════════════
// Multiple guards
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, MultipleGuardsCoexist) {
    Layout layout = makeTestLayout();
    LayoutGuard guard1(layout.data());
    LayoutGuard guard2(layout.data());
    EXPECT_TRUE(layout.isStructurallyLocked());

    guard1.release();
    EXPECT_TRUE(layout.isStructurallyLocked());  // guard2 still holds

    guard2.release();
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, ThreeGuardsCoexist) {
    Layout layout = makeTestLayout();
    LayoutGuard g1(layout.data());
    LayoutGuard g2(layout.data());
    LayoutGuard g3(layout.data());
    EXPECT_TRUE(layout.isStructurallyLocked());

    g1.release();
    g2.release();
    EXPECT_TRUE(layout.isStructurallyLocked());

    g3.release();
    EXPECT_FALSE(layout.isStructurallyLocked());
}

// ═════════════════════════════════════════════════════════════════════════════
// Mutation blocking
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, AddColumnThrowsWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_THROW(layout.addColumn({"new_col", ColumnType::INT32}), std::logic_error);
}

TEST(LayoutGuard, RemoveColumnThrowsWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_THROW(layout.removeColumn(0), std::logic_error);
}

TEST(LayoutGuard, SetColumnTypeThrowsWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_THROW(layout.setColumnType(0, ColumnType::INT64), std::logic_error);
}

TEST(LayoutGuard, SetColumnsDefThrowsWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    std::vector<ColumnDefinition> cols = {{"a", ColumnType::BOOL}};
    EXPECT_THROW(layout.setColumns(cols), std::logic_error);
}

TEST(LayoutGuard, SetColumnsPairThrowsWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    std::vector<std::string> names = {"a"};
    std::vector<ColumnType> types = {ColumnType::BOOL};
    EXPECT_THROW(layout.setColumns(names, types), std::logic_error);
}

TEST(LayoutGuard, ClearThrowsWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_THROW(layout.clear(), std::logic_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// setColumnName is allowed while locked
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, SetColumnNameAllowedWhileLocked) {
    Layout layout = makeTestLayout();
    LayoutGuard guard(layout.data());
    EXPECT_NO_THROW(layout.setColumnName(0, "renamed_bool"));
    EXPECT_EQ(layout.columnName(0), "renamed_bool");
}

// ═════════════════════════════════════════════════════════════════════════════
// Mutations work after guard is released
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, MutationsAllowedAfterRelease) {
    Layout layout = makeTestLayout();
    {
        LayoutGuard guard(layout.data());
        EXPECT_THROW(layout.addColumn({"x", ColumnType::BOOL}), std::logic_error);
    }
    // Guard released — mutations should work
    EXPECT_NO_THROW(layout.addColumn({"x", ColumnType::BOOL}));
    EXPECT_EQ(layout.columnCount(), 5u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Row observer still works (Row is not a lock holder)
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, RowObserverWorksWithoutGuard) {
    Layout layout = makeTestLayout();
    Row row(layout);
    // No guard — Row can track layout mutations normally
    layout.addColumn({"extra", ColumnType::INT64});
    EXPECT_EQ(layout.columnCount(), 5u);
    EXPECT_EQ(row.layout().columnCount(), 5u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Codec integration — Flat001
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, Flat001SetupLocksLayout) {
    Layout layout = makeTestLayout();
    RowCodecFlat001<Layout> codec;
    EXPECT_FALSE(layout.isStructurallyLocked());

    codec.setup(layout);
    EXPECT_TRUE(layout.isStructurallyLocked());
    EXPECT_THROW(layout.addColumn({"x", ColumnType::BOOL}), std::logic_error);

    codec = RowCodecFlat001<Layout>();  // Move-assign default releases guard
    EXPECT_FALSE(layout.isStructurallyLocked());
    EXPECT_NO_THROW(layout.addColumn({"x", ColumnType::BOOL}));
}

TEST(LayoutGuard, Flat001DestructorReleasesLock) {
    Layout layout = makeTestLayout();
    {
        RowCodecFlat001<Layout> codec;
        codec.setup(layout);
        EXPECT_TRUE(layout.isStructurallyLocked());
    }
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, DoubleSetupReleasesOldGuard) {
    Layout layout1 = makeTestLayout();
    Layout layout2 = makeTestLayout();
    RowCodecFlat001<Layout> codec;

    codec.setup(layout1);
    EXPECT_TRUE(layout1.isStructurallyLocked());
    EXPECT_FALSE(layout2.isStructurallyLocked());

    // Second setup on a different layout must release the old guard.
    codec.setup(layout2);
    EXPECT_FALSE(layout1.isStructurallyLocked());
    EXPECT_TRUE(layout2.isStructurallyLocked());

    // Same-layout double setup: lock count must stay at 1, not grow to 2.
    codec.setup(layout2);
    EXPECT_TRUE(layout2.isStructurallyLocked());

    codec = RowCodecFlat001<Layout>();
    EXPECT_FALSE(layout2.isStructurallyLocked());
}

// ═════════════════════════════════════════════════════════════════════════════
// Codec integration — ZoH001
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, ZoH001SetupLocksLayout) {
    Layout layout = makeTestLayout();
    RowCodecZoH001<Layout> codec;
    EXPECT_FALSE(layout.isStructurallyLocked());

    codec.setup(layout);
    EXPECT_TRUE(layout.isStructurallyLocked());
    EXPECT_THROW(layout.removeColumn(0), std::logic_error);

    codec = RowCodecZoH001<Layout>();  // Move-assign default releases guard
    EXPECT_FALSE(layout.isStructurallyLocked());
}

// ═════════════════════════════════════════════════════════════════════════════
// Codec integration — CodecDispatch
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, DispatchSetupLocksLayout) {
    Layout layout = makeTestLayout();
    RowCodecDispatch<Layout> dispatch;
    dispatch.setLayout(layout);
    EXPECT_FALSE(layout.isStructurallyLocked());

    dispatch.setup(RowCodecId::FLAT001);
    EXPECT_TRUE(layout.isStructurallyLocked());

    dispatch.destroy();
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, DispatchSelectCodecLocksLayout) {
    Layout layout = makeTestLayout();
    RowCodecDispatch<Layout> dispatch;
    dispatch.selectCodec(FileFlags::ZERO_ORDER_HOLD, layout);
    EXPECT_TRUE(layout.isStructurallyLocked());

    dispatch.destroy();
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, DispatchDestroyReleasesLock) {
    Layout layout = makeTestLayout();
    RowCodecDispatch<Layout> dispatch;
    dispatch.selectCodec(FileFlags::NONE, layout);
    EXPECT_TRUE(layout.isStructurallyLocked());

    dispatch.destroy();
    EXPECT_FALSE(layout.isStructurallyLocked());
}

// ═════════════════════════════════════════════════════════════════════════════
// Multiple codecs on the same layout
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, MultipleCodecsCoexist) {
    Layout layout = makeTestLayout();
    RowCodecFlat001<Layout> codec1;
    RowCodecZoH001<Layout> codec2;

    codec1.setup(layout);
    codec2.setup(layout);
    EXPECT_TRUE(layout.isStructurallyLocked());
    EXPECT_THROW(layout.addColumn({"x", ColumnType::BOOL}), std::logic_error);

    codec1 = RowCodecFlat001<Layout>();  // Release first guard
    EXPECT_TRUE(layout.isStructurallyLocked());  // codec2 still holds
    EXPECT_THROW(layout.addColumn({"x", ColumnType::BOOL}), std::logic_error);

    codec2 = RowCodecZoH001<Layout>();  // Release second guard
    EXPECT_FALSE(layout.isStructurallyLocked());
    EXPECT_NO_THROW(layout.addColumn({"x", ColumnType::BOOL}));
}

// ═════════════════════════════════════════════════════════════════════════════
// Writer integration
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, WriterOpenLocksWriterCloseUnlocks) {
    Layout layout = makeTestLayout();
    layout.setColumnName(0, "b1");
    layout.setColumnName(1, "i32");
    layout.setColumnName(2, "d");
    layout.setColumnName(3, "s");

    auto tmpPath = std::filesystem::temp_directory_path() / "layout_guard_test_writer.bcsv";

    {
        Writer<Layout> writer(layout);
        EXPECT_FALSE(layout.isStructurallyLocked());

        ASSERT_TRUE(writer.open(tmpPath, true));
        EXPECT_TRUE(layout.isStructurallyLocked());

        // Mutations should throw while writer is open
        EXPECT_THROW(layout.addColumn({"x", ColumnType::BOOL}), std::logic_error);

        // setColumnName should still be allowed
        EXPECT_NO_THROW(layout.setColumnName(0, "renamed"));

        writer.close();
        EXPECT_FALSE(layout.isStructurallyLocked());
    }

    // Clean up
    std::filesystem::remove(tmpPath);
}

TEST(LayoutGuard, WriterDestructorReleasesLock) {
    Layout layout = makeTestLayout();
    auto tmpPath = std::filesystem::temp_directory_path() / "layout_guard_test_writer2.bcsv";

    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(tmpPath, true));
        EXPECT_TRUE(layout.isStructurallyLocked());
    } // Writer destructor calls close() which resets the codec
    EXPECT_FALSE(layout.isStructurallyLocked());

    std::filesystem::remove(tmpPath);
}

// ═════════════════════════════════════════════════════════════════════════════
// Reader integration
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, ReaderOpenLocksReaderCloseUnlocks) {
    Layout layout = makeTestLayout();
    auto tmpPath = std::filesystem::temp_directory_path() / "layout_guard_test_reader.bcsv";

    // Write a file first
    {
        Writer<Layout> writer(layout);
        ASSERT_TRUE(writer.open(tmpPath, true));
        writer.row().set<int32_t>(1, 42);
        writer.row().set<double>(2, 3.14);
        writer.row().set<std::string>(3, "test");
        writer.writeRow();
        writer.close();
    }

    // Read and verify lock behavior
    {
        Reader<Layout> reader;
        ASSERT_TRUE(reader.open(tmpPath));

        // The reader creates its own layout from the file header.
        // That layout should be locked while the reader is open.
        EXPECT_TRUE(reader.layout().isStructurallyLocked());

        reader.close();
        // After close, layout should be unlocked — but reader's internal
        // layout is not accessible externally after close (no guaranteed 
        // test for this since close clears state). The important thing is
        // that the guard was released.
    }

    std::filesystem::remove(tmpPath);
}

// ═════════════════════════════════════════════════════════════════════════════
// Static layouts — codec lifecycle is safe (no guard needed)
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, StaticFlatSetupAndDestroyIsClean) {
    using SLayout = LayoutStatic<bool, int32_t, double, std::string>;
    SLayout layout({"b1", "i32", "d", "s"});

    RowCodecFlat001<SLayout> codec;
    codec.setup(layout);
    // Destructor cleans up — no crash
}

TEST(LayoutGuard, StaticZoHSetupAndDestroyIsClean) {
    using SLayout = LayoutStatic<bool, int32_t, double, std::string>;
    SLayout layout({"b1", "i32", "d", "s"});

    RowCodecZoH001<SLayout> codec;
    codec.setup(layout);
    // Destructor cleans up — no crash
}

// ═════════════════════════════════════════════════════════════════════════════
// Codec copy acquires new guard
// ═════════════════════════════════════════════════════════════════════════════

TEST(LayoutGuard, Flat001CopyAcquiresNewGuard) {
    Layout layout = makeTestLayout();
    RowCodecFlat001<Layout> codec1;
    codec1.setup(layout);
    EXPECT_TRUE(layout.isStructurallyLocked());

    // Copy the codec — should acquire a second guard
    RowCodecFlat001<Layout> codec2(codec1);
    EXPECT_TRUE(layout.isStructurallyLocked());

    codec1 = RowCodecFlat001<Layout>();  // Release first guard
    EXPECT_TRUE(layout.isStructurallyLocked());  // codec2 still holds

    codec2 = RowCodecFlat001<Layout>();  // Release second guard
    EXPECT_FALSE(layout.isStructurallyLocked());
}

TEST(LayoutGuard, ZoH001CopyAcquiresNewGuard) {
    Layout layout = makeTestLayout();
    RowCodecZoH001<Layout> codec1;
    codec1.setup(layout);

    RowCodecZoH001<Layout> codec2(codec1);
    codec1 = RowCodecZoH001<Layout>();  // Release first guard
    EXPECT_TRUE(layout.isStructurallyLocked());

    codec2 = RowCodecZoH001<Layout>();  // Release second guard
    EXPECT_FALSE(layout.isStructurallyLocked());
}

} // anonymous namespace
