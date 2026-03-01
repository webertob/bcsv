/**
 * @file layout_sync_test.cpp
 * @brief Tests to validate synchronization between column_names_ and column_index_
 * 
 * This test suite specifically validates that Layout operations maintain perfect
 * synchronization between column_names_ and column_index_ during:
 * - Column insertion (addColumn)
 * - Column removal (removeColumn)  
 * - Column renaming (setColumnName)
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>
#include <bcsv/codec_row/row_codec_flat001.h>
#include <bcsv/codec_row/row_codec_flat001.hpp>
#include <set>
#include <string>

class LayoutSyncTest : public ::testing::Test {
protected:
    // Comprehensive validation that column_names_ and column_index_ are synchronized
    void validateSync(const bcsv::Layout& layout, const std::string& context) {
        size_t count = layout.columnCount();
        
        // Forward validation: column_names_[i] can be found in index at position i
        for (size_t i = 0; i < count; ++i) {
            std::string name = layout.columnName(i);
            EXPECT_FALSE(name.empty()) << context << ": Column " << i << " has empty name";
            EXPECT_TRUE(layout.hasColumn(name)) 
                << context << ": Index missing entry for '" << name << "' at position " << i;
            EXPECT_EQ(i, layout.columnIndex(name))
                << context << ": Index maps '" << name << "' to wrong position (expected " << i << ")";
        }
        
        // No duplicate names
        std::set<std::string> seen;
        for (size_t i = 0; i < count; ++i) {
            std::string name = layout.columnName(i);
            EXPECT_TRUE(seen.insert(name).second)
                << context << ": Duplicate name '" << name << "' at position " << i;
        }
    }
    
    // Validate LayoutStatic synchronization
    template<typename... ColumnTypes>
    void validateSync(const bcsv::LayoutStatic<ColumnTypes...>& layout, const std::string& context) {
        size_t count = layout.columnCount();
        
        for (size_t i = 0; i < count; ++i) {
            std::string name = layout.columnName(i);
            EXPECT_FALSE(name.empty()) << context << ": Column " << i << " has empty name";
            EXPECT_TRUE(layout.hasColumn(name))
                << context << ": Index missing entry for '" << name << "' at position " << i;
            EXPECT_EQ(i, layout.columnIndex(name))
                << context << ": Index maps '" << name << "' to wrong position (expected " << i << ")";
        }
        
        std::set<std::string> seen;
        for (size_t i = 0; i < count; ++i) {
            std::string name = layout.columnName(i);
            EXPECT_TRUE(seen.insert(name).second)
                << context << ": Duplicate name '" << name << "' at position " << i;
        }
    }
};

// ============================================================================
// Layout (Dynamic) Synchronization Tests
// ============================================================================

TEST_F(LayoutSyncTest, AddColumn_End_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    validateSync(layout, "After adding 'a'");
    
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    validateSync(layout, "After adding 'b'");
    
    EXPECT_EQ(0, layout.columnIndex("a"));
    EXPECT_EQ(1, layout.columnIndex("b"));
}

TEST_F(LayoutSyncTest, AddColumn_Beginning_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    validateSync(layout, "Initial state");
    
    // Insert at position 0 should shift existing columns
    layout.addColumn({"z", bcsv::ColumnType::DOUBLE}, 0);
    validateSync(layout, "After inserting 'z' at position 0");
    
    EXPECT_EQ(0, layout.columnIndex("z"));
    EXPECT_EQ(1, layout.columnIndex("a"));
    EXPECT_EQ(2, layout.columnIndex("b"));
    EXPECT_EQ("z", layout.columnName(0));
    EXPECT_EQ("a", layout.columnName(1));
    EXPECT_EQ("b", layout.columnName(2));
}

TEST_F(LayoutSyncTest, AddColumn_Middle_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    layout.addColumn({"c", bcsv::ColumnType::DOUBLE});
    validateSync(layout, "Initial state");
    
    // Insert in middle should shift subsequent columns
    layout.addColumn({"x", bcsv::ColumnType::STRING}, 1);
    validateSync(layout, "After inserting 'x' at position 1");
    
    EXPECT_EQ(0, layout.columnIndex("a"));
    EXPECT_EQ(1, layout.columnIndex("x"));
    EXPECT_EQ(2, layout.columnIndex("b"));
    EXPECT_EQ(3, layout.columnIndex("c"));
}

TEST_F(LayoutSyncTest, AddColumn_DuplicateName_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"col", bcsv::ColumnType::INT32});
    validateSync(layout, "After first 'col'");
    
    // Adding duplicate should auto-resolve with underscore
    layout.addColumn({"col", bcsv::ColumnType::FLOAT});
    validateSync(layout, "After second 'col'");
    
    EXPECT_EQ(2, layout.columnCount());
    EXPECT_EQ("col", layout.columnName(0));
    EXPECT_EQ("col_", layout.columnName(1));
    EXPECT_EQ(0, layout.columnIndex("col"));
    EXPECT_EQ(1, layout.columnIndex("col_"));
}

TEST_F(LayoutSyncTest, AddColumn_MultipleDuplicates_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"x", bcsv::ColumnType::INT32});
    layout.addColumn({"x", bcsv::ColumnType::FLOAT});
    layout.addColumn({"x", bcsv::ColumnType::DOUBLE});
    validateSync(layout, "After three 'x' columns");
    
    EXPECT_EQ(3, layout.columnCount());
    // Should have "x", "x_", "x__"
    EXPECT_TRUE(layout.hasColumn("x"));
    EXPECT_TRUE(layout.hasColumn("x_"));
    EXPECT_TRUE(layout.hasColumn("x__"));
}

TEST_F(LayoutSyncTest, RemoveColumn_Beginning_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    layout.addColumn({"c", bcsv::ColumnType::DOUBLE});
    validateSync(layout, "Initial state");
    
    layout.removeColumn(0); // Remove 'a'
    validateSync(layout, "After removing first column");
    
    EXPECT_EQ(2, layout.columnCount());
    EXPECT_EQ("b", layout.columnName(0));
    EXPECT_EQ("c", layout.columnName(1));
    EXPECT_EQ(0, layout.columnIndex("b"));
    EXPECT_EQ(1, layout.columnIndex("c"));
    EXPECT_FALSE(layout.hasColumn("a"));
}

TEST_F(LayoutSyncTest, RemoveColumn_Middle_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    layout.addColumn({"c", bcsv::ColumnType::DOUBLE});
    validateSync(layout, "Initial state");
    
    layout.removeColumn(1); // Remove 'b'
    validateSync(layout, "After removing middle column");
    
    EXPECT_EQ(2, layout.columnCount());
    EXPECT_EQ("a", layout.columnName(0));
    EXPECT_EQ("c", layout.columnName(1));
    EXPECT_EQ(0, layout.columnIndex("a"));
    EXPECT_EQ(1, layout.columnIndex("c"));
    EXPECT_FALSE(layout.hasColumn("b"));
}

TEST_F(LayoutSyncTest, RemoveColumn_End_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    layout.addColumn({"c", bcsv::ColumnType::DOUBLE});
    validateSync(layout, "Initial state");
    
    layout.removeColumn(2); // Remove 'c'
    validateSync(layout, "After removing last column");
    
    EXPECT_EQ(2, layout.columnCount());
    EXPECT_EQ("a", layout.columnName(0));
    EXPECT_EQ("b", layout.columnName(1));
    EXPECT_FALSE(layout.hasColumn("c"));
}

TEST_F(LayoutSyncTest, SetColumnName_Simple_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    validateSync(layout, "Initial state");
    
    layout.setColumnName(0, "x");
    validateSync(layout, "After renaming 'a' to 'x'");
    
    EXPECT_EQ("x", layout.columnName(0));
    EXPECT_EQ(0, layout.columnIndex("x"));
    EXPECT_FALSE(layout.hasColumn("a"));
}

TEST_F(LayoutSyncTest, SetColumnName_DuplicateConflict_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    validateSync(layout, "Initial state");
    
    // Renaming 'b' to 'a' should auto-resolve to 'a_'
    layout.setColumnName(1, "a");
    validateSync(layout, "After renaming 'b' to 'a' (conflict)");
    
    EXPECT_EQ("a", layout.columnName(0));
    EXPECT_EQ("a_", layout.columnName(1));
    EXPECT_EQ(0, layout.columnIndex("a"));
    EXPECT_EQ(1, layout.columnIndex("a_"));
    EXPECT_FALSE(layout.hasColumn("b"));
}

TEST_F(LayoutSyncTest, SetColumnName_SameNameNOP_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    validateSync(layout, "Initial state");
    
    // Setting to same name should be NOP
    layout.setColumnName(0, "a");
    validateSync(layout, "After setting same name");
    
    EXPECT_EQ("a", layout.columnName(0));
    EXPECT_EQ(0, layout.columnIndex("a"));
}

TEST_F(LayoutSyncTest, SetColumnName_EmptyName_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    validateSync(layout, "Initial state");
    
    // Empty name should be normalized to default (e.g., "A")
    layout.setColumnName(0, "");
    validateSync(layout, "After setting empty name");
    
    std::string new_name = layout.columnName(0);
    EXPECT_FALSE(new_name.empty());
    EXPECT_EQ(0, layout.columnIndex(new_name));
    EXPECT_FALSE(layout.hasColumn("a"));
}

TEST_F(LayoutSyncTest, ComplexSequence_Sync) {
    bcsv::Layout layout;
    
    // Build complex layout
    layout.addColumn({"id", bcsv::ColumnType::INT64});
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"value", bcsv::ColumnType::DOUBLE});
    validateSync(layout, "Initial 3 columns");
    
    // Insert in middle
    layout.addColumn({"flag", bcsv::ColumnType::BOOL}, 1);
    validateSync(layout, "After insert at position 1");
    EXPECT_EQ("id", layout.columnName(0));
    EXPECT_EQ("flag", layout.columnName(1));
    EXPECT_EQ("name", layout.columnName(2));
    EXPECT_EQ("value", layout.columnName(3));
    
    // Rename middle column
    layout.setColumnName(2, "label");
    validateSync(layout, "After renaming position 2");
    EXPECT_EQ("label", layout.columnName(2));
    EXPECT_FALSE(layout.hasColumn("name"));
    
    // Remove first column
    layout.removeColumn(0);
    validateSync(layout, "After removing first column");
    EXPECT_EQ(3, layout.columnCount());
    EXPECT_EQ("flag", layout.columnName(0));
    EXPECT_EQ("label", layout.columnName(1));
    EXPECT_EQ("value", layout.columnName(2));
    
    // Add duplicate name
    layout.addColumn({"flag", bcsv::ColumnType::INT32});
    validateSync(layout, "After adding duplicate 'flag'");
    EXPECT_EQ(4, layout.columnCount());
    EXPECT_TRUE(layout.hasColumn("flag_"));
}

// ============================================================================
// LayoutStatic Synchronization Tests
// ============================================================================

TEST_F(LayoutSyncTest, LayoutStatic_SetColumnName_Sync) {
    bcsv::LayoutStatic<int32_t, float, double> layout;
    validateSync(layout, "Initial default names");
    
    layout.setColumnName(0, "first");
    validateSync(layout, "After renaming column 0");
    EXPECT_EQ("first", layout.columnName(0));
    EXPECT_EQ(0, layout.columnIndex("first"));
}

TEST_F(LayoutSyncTest, LayoutStatic_SetColumnName_Conflict_Sync) {
    bcsv::LayoutStatic<int32_t, float> layout({"a", "b"});
    validateSync(layout, "Initial state");
    
    // Rename second column to conflict with first
    layout.setColumnName(1, "a");
    validateSync(layout, "After rename with conflict");
    
    EXPECT_EQ("a", layout.columnName(0));
    EXPECT_EQ("a_", layout.columnName(1));
    EXPECT_FALSE(layout.hasColumn("b"));
}

TEST_F(LayoutSyncTest, LayoutStatic_SetColumnNames_Bulk_Sync) {
    bcsv::LayoutStatic<int32_t, float, double> layout;
    
    std::array<std::string, 3> names = {"x", "y", "z"};
    layout.setColumnNames(names);
    validateSync(layout, "After bulk setColumnNames");
    
    EXPECT_EQ("x", layout.columnName(0));
    EXPECT_EQ("y", layout.columnName(1));
    EXPECT_EQ("z", layout.columnName(2));
    EXPECT_EQ(0, layout.columnIndex("x"));
    EXPECT_EQ(1, layout.columnIndex("y"));
    EXPECT_EQ(2, layout.columnIndex("z"));
}

TEST_F(LayoutSyncTest, LayoutStatic_SetColumnNames_Duplicates_Sync) {
    bcsv::LayoutStatic<int32_t, float, double> layout;
    
    // Set column names with duplicates - build() should resolve conflicts
    std::array<std::string, 3> names = {"col", "col", "col"};
    layout.setColumnNames(names);
    validateSync(layout, "After bulk setColumnNames with duplicates");
    
    EXPECT_EQ(3, layout.columnCount());
    // Names should be "col", "col.1", "col.2" based on column index
    EXPECT_EQ("col", layout.columnName(0));
    EXPECT_EQ("col.1", layout.columnName(1));
    EXPECT_EQ("col.2", layout.columnName(2));
}

TEST_F(LayoutSyncTest, LayoutStatic_Clear_Sync) {
    bcsv::LayoutStatic<int32_t, float> layout({"a", "b"});
    validateSync(layout, "Initial state");
    
    layout.clear();
    validateSync(layout, "After clear");
    
    // After clear, names should be defaults (A, B, ...)
    EXPECT_EQ("A", layout.columnName(0));
    EXPECT_EQ("B", layout.columnName(1));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(LayoutSyncTest, Layout_AddRemoveAddSameColumn_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"temp", bcsv::ColumnType::INT32});
    validateSync(layout, "After add");
    
    layout.removeColumn(0);
    validateSync(layout, "After remove");
    EXPECT_EQ(0, layout.columnCount());
    
    // Adding same name again should work
    layout.addColumn({"temp", bcsv::ColumnType::FLOAT});
    validateSync(layout, "After re-add");
    EXPECT_EQ(1, layout.columnCount());
    EXPECT_EQ("temp", layout.columnName(0));
}

TEST_F(LayoutSyncTest, Layout_RenameBackAndForth_Sync) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});
    validateSync(layout, "Initial");
    
    layout.setColumnName(0, "x");
    validateSync(layout, "After rename a->x");
    
    layout.setColumnName(0, "a");
    validateSync(layout, "After rename x->a");
    
    EXPECT_EQ("a", layout.columnName(0));
    EXPECT_EQ("b", layout.columnName(1));
}

TEST_F(LayoutSyncTest, Layout_SetColumns_NamesTypes_UpdatesAttachedRow) {
    bcsv::Layout layout;
    layout.addColumn({"a", bcsv::ColumnType::INT32});
    layout.addColumn({"b", bcsv::ColumnType::FLOAT});

    bcsv::Row row(layout);
    row.set(0, int32_t(1));
    row.set(1, 2.0f);

    const std::vector<std::string> names = {"x", "y", "z"};
    const std::vector<bcsv::ColumnType> types = {
        bcsv::ColumnType::UINT16,
        bcsv::ColumnType::DOUBLE,
        bcsv::ColumnType::STRING
    };

    layout.setColumns(names, types);
    validateSync(layout, "After setColumns(names, types)");

    EXPECT_EQ(layout.columnCount(), row.layout().columnCount());
    EXPECT_EQ(layout.columnType(0), row.layout().columnType(0));
    EXPECT_EQ(layout.columnType(1), row.layout().columnType(1));
    EXPECT_EQ(layout.columnType(2), row.layout().columnType(2));

    EXPECT_NO_THROW(row.set(0, uint16_t(7)));
    EXPECT_NO_THROW(row.set(1, 3.5));
    EXPECT_NO_THROW(row.set(2, std::string("ok")));

    EXPECT_EQ(row.get<uint16_t>(0), uint16_t(7));
    EXPECT_DOUBLE_EQ(row.get<double>(1), 3.5);
    EXPECT_EQ(row.get<std::string>(2), "ok");
}

// ============================================================================
// Layout Wire Metadata via Codec Tests
// ============================================================================

TEST_F(LayoutSyncTest, Clone_PreservesWireMetadata) {
    // Layout with all column types: bool, scalar, string
    bcsv::Layout layout;
    layout.addColumn({"flag1", bcsv::ColumnType::BOOL});
    layout.addColumn({"flag2", bcsv::ColumnType::BOOL});
    layout.addColumn({"val_i32", bcsv::ColumnType::INT32});
    layout.addColumn({"val_f64", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"desc", bcsv::ColumnType::STRING});

    // Create codecs from original and cloned layouts
    bcsv::RowCodecFlat001<bcsv::Layout> codecOrig;
    codecOrig.setup(layout);

    bcsv::Layout cloned = layout.clone();
    bcsv::RowCodecFlat001<bcsv::Layout> codecCloned;
    codecCloned.setup(cloned);

    // Verify codec wire metadata matches between original and clone
    EXPECT_EQ(codecCloned.rowHeaderSize(), codecOrig.rowHeaderSize())
        << "Cloned rowHeaderSize must match original";
    EXPECT_EQ(codecCloned.wireDataSize(), codecOrig.wireDataSize())
        << "Cloned wireDataSize must match original";
    EXPECT_EQ(codecCloned.wireStrgCount(), codecOrig.wireStrgCount())
        << "Cloned wireStrgCount must match original";
    EXPECT_EQ(codecCloned.wireFixedSize(), codecOrig.wireFixedSize())
        << "Cloned wireFixedSize must match original";

    // Verify expected values
    EXPECT_GT(codecOrig.rowHeaderSize(), 0u) << "rowHeaderSize should be > 0 (2 bools)";
    EXPECT_GT(codecOrig.wireDataSize(), 0u) << "wireDataSize should be > 0 (int32 + double)";
    EXPECT_EQ(codecOrig.wireStrgCount(), 2u) << "wireStrgCount should be 2";

    // Also verify basic layout properties survived
    EXPECT_EQ(cloned.columnCount(), layout.columnCount());
    validateSync(cloned, "After clone");
}

TEST_F(LayoutSyncTest, Clone_WireMetadata_AfterRemoveColumn) {
    // Build layout, remove a column, clone — codec metadata must still match
    bcsv::Layout layout;
    layout.addColumn({"b1", bcsv::ColumnType::BOOL});
    layout.addColumn({"x",  bcsv::ColumnType::INT32});
    layout.addColumn({"s",  bcsv::ColumnType::STRING});
    layout.addColumn({"b2", bcsv::ColumnType::BOOL});

    layout.removeColumn(1);  // remove "x" (INT32)
    // Now: b1(BOOL), s(STRING), b2(BOOL) — no scalars, 2 bools, 1 string

    bcsv::RowCodecFlat001<bcsv::Layout> codecOrig;
    codecOrig.setup(layout);

    EXPECT_EQ(codecOrig.wireDataSize(), 0u) << "No scalars after removing INT32";
    EXPECT_EQ(codecOrig.wireStrgCount(), 1u) << "One string column remains";
    EXPECT_GT(codecOrig.rowHeaderSize(), 0u) << "Two bools remain";

    bcsv::Layout cloned = layout.clone();
    bcsv::RowCodecFlat001<bcsv::Layout> codecCloned;
    codecCloned.setup(cloned);

    EXPECT_EQ(codecCloned.rowHeaderSize(), codecOrig.rowHeaderSize());
    EXPECT_EQ(codecCloned.wireDataSize(), codecOrig.wireDataSize());
    EXPECT_EQ(codecCloned.wireStrgCount(), codecOrig.wireStrgCount());
    EXPECT_EQ(codecCloned.wireFixedSize(), codecOrig.wireFixedSize());
    validateSync(cloned, "After removeColumn + clone");
}
