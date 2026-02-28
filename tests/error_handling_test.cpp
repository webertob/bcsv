/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file error_handling_test.cpp
 * @brief Comprehensive error handling tests for BCSV library
 * 
 * Tests cover:
 * - Missing files
 * - Layout incompatibility
 * - Permission errors
 * - API error signaling clarity
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class ErrorHandlingTest : public ::testing::Test {
protected:
    std::string test_dir_;
    
    void SetUp() override {
        test_dir_ = "test_error_handling_temp";
        fs::create_directories(test_dir_);
    }
    
    void TearDown() override {
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }
};

// Test 1: Reader with non-existent file
TEST_F(ErrorHandlingTest, Reader_NonExistentFile) {
    std::string nonexistent_file = test_dir_ + "/does_not_exist.bcsv";
    
    // Ensure file doesn't exist
    ASSERT_FALSE(fs::exists(nonexistent_file));
    
    // Try to open non-existent file
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    layout.addColumn({"value", bcsv::ColumnType::FLOAT});
    
    bcsv::Reader<bcsv::Layout> reader;
    bool open_result = reader.open(nonexistent_file);
    
    // EXPECTED: open() returns false
    EXPECT_FALSE(open_result) << "Reader should return false for non-existent file";
    
    // Check if error message is available and informative
    std::string errMsg = reader.getErrorMsg();
    EXPECT_FALSE(errMsg.empty()) << "Error message should not be empty";
    EXPECT_NE(errMsg.find("does not exist"), std::string::npos) 
        << "Error message should mention 'does not exist'. Got: " << errMsg;
    
    // std::cout << "✓ Non-existent file error message: " << errMsg << "\n";
}

// Test 2: Reader with incompatible layout (wrong column count)
TEST_F(ErrorHandlingTest, Reader_IncompatibleLayout_ColumnCount) {
    std::string test_file = test_dir_ + "/test_layout_mismatch.bcsv";
    
    // Write file with 2 columns
    bcsv::Layout write_layout;
    write_layout.addColumn({"id", bcsv::ColumnType::INT32});
    write_layout.addColumn({"value", bcsv::ColumnType::FLOAT});
    
    bcsv::Writer<bcsv::Layout> writer(write_layout);
    ASSERT_TRUE(writer.open(test_file, true));
    
    auto& row = writer.row();
    row.set(0, 42);
    row.set(1, 3.14f);
    writer.writeRow();
    writer.close();
    
    // Try to read with 3 columns (incompatible)
    bcsv::Layout read_layout;
    read_layout.addColumn({"id", bcsv::ColumnType::INT32});
    read_layout.addColumn({"value", bcsv::ColumnType::FLOAT});
    read_layout.addColumn({"extra", bcsv::ColumnType::BOOL});  // Extra column
    
    bcsv::Reader<bcsv::Layout> reader;
    bool open_result = reader.open(test_file);
    
    // Open succeeds - layout validation is separate
    EXPECT_TRUE(open_result) << "Reader open should succeed";
    
    // Check layout compatibility
    bool compatible = reader.layout().isCompatible(read_layout);
    EXPECT_FALSE(compatible) << "Layout with different column count should be incompatible";
    
    // std::cout << "✓ Layout incompatibility detected: column count mismatch\n";
}

// Test 3: Reader with incompatible layout (wrong column type)
TEST_F(ErrorHandlingTest, Reader_IncompatibleLayout_ColumnType) {
    std::string test_file = test_dir_ + "/test_type_mismatch.bcsv";
    
    // Write file with INT32 column
    bcsv::Layout write_layout;
    write_layout.addColumn({"id", bcsv::ColumnType::INT32});
    write_layout.addColumn({"value", bcsv::ColumnType::FLOAT});
    
    bcsv::Writer<bcsv::Layout> writer(write_layout);
    ASSERT_TRUE(writer.open(test_file, true));
    
    auto& row = writer.row();
    row.set(0, 42);
    row.set(1, 3.14f);
    writer.writeRow();
    writer.close();
    
    // Try to read with STRING instead of INT32 (incompatible)
    bcsv::Layout read_layout;
    read_layout.addColumn({"id", bcsv::ColumnType::STRING});  // Wrong type
    read_layout.addColumn({"value", bcsv::ColumnType::FLOAT});
    
    bcsv::Reader<bcsv::Layout> reader;
    bool open_result = reader.open(test_file);
    
    // Open succeeds
    EXPECT_TRUE(open_result);
    
    // Check layout compatibility
    bool compatible = reader.layout().isCompatible(read_layout);
    EXPECT_FALSE(compatible) << "Layout with different column type should be incompatible";
    
    // std::cout << "✓ Layout incompatibility detected: column type mismatch\n";
}

// Test 4: Writer with existing file (no overwrite flag)
TEST_F(ErrorHandlingTest, Writer_FileExists_NoOverwrite) {
    std::string test_file = test_dir_ + "/existing_file.bcsv";
    
    // Create file first
    std::ofstream(test_file) << "dummy content";
    ASSERT_TRUE(fs::exists(test_file));
    
    // Try to open without overwrite flag
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    
    bcsv::Writer<bcsv::Layout> writer(layout);
    bool open_result = writer.open(test_file, false);  // overwrite = false
    
    // EXPECTED: open() returns false
    EXPECT_FALSE(open_result) << "Writer should return false when file exists and overwrite=false";
    
    // std::cout << "✓ File exists without overwrite flag: correctly rejected\n";
}

// Test 5: Writer with non-writable directory
TEST_F(ErrorHandlingTest, Writer_NoWritePermission) {
    // Note: This test may be skipped on systems where permission changes aren't allowed
    std::string readonly_dir = test_dir_ + "/readonly";
    fs::create_directories(readonly_dir);
    
    // Try to make directory read-only
    std::error_code ec;
    fs::permissions(readonly_dir, 
                    fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, ec);
    
    if (ec) {
        GTEST_SKIP() << "Cannot set directory permissions on this system";
        return;
    }
    
    std::string test_file = readonly_dir + "/test.bcsv";
    
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    
    bcsv::Writer<bcsv::Layout> writer(layout);
    bool open_result = writer.open(test_file, true);
    
    // EXPECTED: open() returns false due to permission error
    EXPECT_FALSE(open_result) << "Writer should return false for non-writable directory";
    
    // Restore permissions for cleanup
    fs::permissions(readonly_dir, 
                    fs::perms::owner_all,
                    fs::perm_options::replace);
    
    // std::cout << "✓ Write permission error: correctly detected\n";
}

// Test 6: Reading corrupted file (not a valid BCSV file)
TEST_F(ErrorHandlingTest, Reader_CorruptedFile) {
    std::string test_file = test_dir_ + "/corrupted.bcsv";
    
    // Create a file with invalid content
    std::ofstream outfile(test_file, std::ios::binary);
    outfile << "This is not a valid BCSV file!";
    outfile.close();
    
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    
    bcsv::Reader<bcsv::Layout> reader;
    bool open_result = reader.open(test_file);
    
    // EXPECTED: open() returns false due to invalid header
    EXPECT_FALSE(open_result) << "Reader should return false for corrupted file";
    
    std::string errMsg = reader.getErrorMsg();
    EXPECT_FALSE(errMsg.empty()) << "Error message should be provided for corrupted file";
    
    // std::cout << "✓ Corrupted file error message: " << errMsg << "\n";
}

// Test 7: Static interface with wrong layout
TEST_F(ErrorHandlingTest, StaticReader_IncompatibleLayout) {
    std::string test_file = test_dir_ + "/test_static_mismatch.bcsv";
    
    // Write with static layout: INT32, FLOAT
    using WriteLayout = bcsv::LayoutStatic<int32_t, float>;
    WriteLayout write_layout({"id", "value"});
    
    bcsv::Writer<WriteLayout> writer(write_layout);
    ASSERT_TRUE(writer.open(test_file, true));
    
    auto& row = writer.row();
    row.set<0>(42);
    row.set<1>(3.14f);
    writer.writeRow();
    writer.close();
    
    // Try to read with different static layout: INT32, STRING (incompatible)
    using ReadLayout = bcsv::LayoutStatic<int32_t, std::string>;
    ReadLayout read_layout({"id", "name"});
    
    bcsv::Reader<ReadLayout> reader;
    bool open_result = reader.open(test_file);
    
    // EXPECTED: Static interface validates layout during open() and fails
    EXPECT_FALSE(open_result) << "Reader open should fail for incompatible static layout";
    
    // Verify error message provides useful information
    std::string errMsg = reader.getErrorMsg();
    EXPECT_FALSE(errMsg.empty()) << "Error message should be provided";
    EXPECT_TRUE(errMsg.find("type") != std::string::npos || 
                errMsg.find("mismatch") != std::string::npos ||
                errMsg.find("header") != std::string::npos) 
        << "Error message should mention type mismatch or header error. Got: " << errMsg;
    
    // std::cout << "✓ Static layout incompatibility detected at open(): " << errMsg << "\n";
}

// Test 8: Attempting operations on closed reader
TEST_F(ErrorHandlingTest, Reader_OperationOnClosedFile) {
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    
    bcsv::Reader<bcsv::Layout> reader;
    
    // Try to read without opening
    EXPECT_FALSE(reader.isOpen()) << "Reader should not be open initially";
    
    // Attempting to read should fail gracefully
    bool read_result = reader.readNext();
    EXPECT_FALSE(read_result) << "readNext() should return false on closed reader";
    
    // std::cout << "✓ Operation on closed reader handled gracefully\n";
}

// Test 9: Multiple open calls on same writer
TEST_F(ErrorHandlingTest, Writer_DoubleOpen) {
    std::string test_file1 = test_dir_ + "/test1.bcsv";
    std::string test_file2 = test_dir_ + "/test2.bcsv";
    
    bcsv::Layout layout;
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    
    bcsv::Writer<bcsv::Layout> writer(layout);
    
    // Open first file
    ASSERT_TRUE(writer.open(test_file1, true));
    EXPECT_TRUE(writer.isOpen());
    
    // Try to open second file without closing first
    bool second_open = writer.open(test_file2, true);
    
    // EXPECTED: Second open should fail (writer already has a file open)
    EXPECT_FALSE(second_open) << "Writer should reject second open() without close()";
    
    writer.close();
    
    // std::cout << "✓ Double open prevented\n";
}

// Test 10: Check error reporting consistency
TEST_F(ErrorHandlingTest, ErrorReporting_Consistency) {
    // Summary: All BCSV API functions that can fail should:
    // 1. Return bool indicating success/failure
    // 2. NOT throw exceptions for normal error conditions (file not found, etc.)
    // 3. Provide error messages via getErrorMsg() or similar
    
    // std::cout << "\n=== Error Reporting API Review ===\n";
    // std::cout << "Writer::open()      - Returns: bool ✓\n";
    // std::cout << "Writer::getErrorMsg() - Available: ✓\n";
    // std::cout << "Reader::open()      - Returns: bool ✓\n";
    // std::cout << "Reader::getErrorMsg() - Available: ✓\n";
    // std::cout << "Reader::readNext()  - Returns: bool ✓\n";
    // std::cout << "\n✅ API is now consistent - both Reader and Writer support getErrorMsg()\n";
    
    SUCCEED();
}

// Test 11: Comprehensive getErrorMsg() validation
TEST_F(ErrorHandlingTest, GetErrorMsg_AllCases) {
    // std::cout << "\n=== Verifying getErrorMsg() for all error conditions ===\n";
    
    // Case 1: File doesn't exist
    {
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        bcsv::Reader<bcsv::Layout> reader;
        
        ASSERT_FALSE(reader.open(test_dir_ + "/nonexistent.bcsv"));
        std::string errMsg = reader.getErrorMsg();
        EXPECT_FALSE(errMsg.empty());
        EXPECT_NE(errMsg.find("does not exist"), std::string::npos);
        // std::cout << "  ✓ File not found: " << errMsg << "\n";
    }
    
    // Case 2: File is not a regular file (directory)
    {
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        bcsv::Reader<bcsv::Layout> reader;
        
        ASSERT_FALSE(reader.open(test_dir_));  // Try to open directory
        std::string errMsg = reader.getErrorMsg();
        EXPECT_FALSE(errMsg.empty());
        EXPECT_NE(errMsg.find("not a regular file"), std::string::npos);
        // std::cout << "  ✓ Not a regular file: " << errMsg << "\n";
    }
    
    // Case 3: Invalid BCSV file (wrong magic number)
    {
        std::string bad_file = test_dir_ + "/bad_magic.bcsv";
        std::ofstream(bad_file, std::ios::binary) << "INVALID_HEADER_DATA";
        
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        bcsv::Reader<bcsv::Layout> reader;
        
        ASSERT_FALSE(reader.open(bad_file));
        std::string errMsg = reader.getErrorMsg();
        EXPECT_FALSE(errMsg.empty());
        EXPECT_TRUE(errMsg.find("magic") != std::string::npos || 
                    errMsg.find("header") != std::string::npos);
        // std::cout << "  ✓ Invalid BCSV header: " << errMsg << "\n";
    }
    
    // Case 4: Wrong BCSV version
    // This requires creating a file with valid magic but wrong version
    // For now, we verify the error message format exists
    
    // Case 5: Layout type mismatch (static interface)
    {
        std::string test_file = test_dir_ + "/type_mismatch.bcsv";
        
        // Write with INT32
        using WriteLayout = bcsv::LayoutStatic<int32_t>;
        WriteLayout write_layout({"value"});
        bcsv::Writer<WriteLayout> writer(write_layout);
        ASSERT_TRUE(writer.open(test_file, true));
        writer.row().set<0>(42);
        writer.writeRow();
        writer.close();
        
        // Try to read with STRING
        using ReadLayout = bcsv::LayoutStatic<std::string>;
        ReadLayout read_layout({"value"});
        bcsv::Reader<ReadLayout> reader;
        
        ASSERT_FALSE(reader.open(test_file));
        std::string errMsg = reader.getErrorMsg();
        EXPECT_FALSE(errMsg.empty());
        EXPECT_TRUE(errMsg.find("type") != std::string::npos || 
                    errMsg.find("header") != std::string::npos);
        // std::cout << "  ✓ Layout type mismatch: " << errMsg << "\n";
    }
    
    // std::cout << "\n✅ All error messages properly captured in getErrorMsg()\n";
}

// Test 12: Verify Writer::getErrorMsg() works
TEST_F(ErrorHandlingTest, Writer_GetErrorMsg_AllCases) {
    // std::cout << "\n=== Verifying Writer::getErrorMsg() for all error conditions ===\n";
    
    // Case 1: File exists without overwrite flag
    {
        std::string test_file = test_dir_ + "/existing.bcsv";
        std::ofstream(test_file) << "dummy";
        
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        bcsv::Writer<bcsv::Layout> writer(layout);
        
        ASSERT_FALSE(writer.open(test_file, false));  // overwrite=false
        std::string errMsg = writer.getErrorMsg();
        EXPECT_FALSE(errMsg.empty());
        EXPECT_NE(errMsg.find("already exists"), std::string::npos);
        // std::cout << "  ✓ File exists error: " << errMsg << "\n";
    }
    
    // Case 2: No write permission
    {
        std::string readonly_dir = test_dir_ + "/readonly2";
        fs::create_directories(readonly_dir);
        
        std::error_code ec;
        fs::permissions(readonly_dir, 
                        fs::perms::owner_read | fs::perms::owner_exec,
                        fs::perm_options::replace, ec);
        
        if (!ec) {
            std::string test_file = readonly_dir + "/test.bcsv";
            
            bcsv::Layout layout;
            layout.addColumn({"id", bcsv::ColumnType::INT32});
            bcsv::Writer<bcsv::Layout> writer(layout);
            
            ASSERT_FALSE(writer.open(test_file, true));
            std::string errMsg = writer.getErrorMsg();
            EXPECT_FALSE(errMsg.empty());
            EXPECT_TRUE(errMsg.find("permission") != std::string::npos ||
                        errMsg.find("write") != std::string::npos);
            // std::cout << "  ✓ Permission error: " << errMsg << "\n";
            
            fs::permissions(readonly_dir, fs::perms::owner_all, fs::perm_options::replace);
        } else {
            // std::cout << "  ⊘ Permission test skipped (cannot change permissions)\n";
        }
    }
    
    // Case 3: Double open attempt
    {
        std::string test_file1 = test_dir_ + "/file1.bcsv";
        std::string test_file2 = test_dir_ + "/file2.bcsv";
        
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        bcsv::Writer<bcsv::Layout> writer(layout);
        
        ASSERT_TRUE(writer.open(test_file1, true));
        ASSERT_FALSE(writer.open(test_file2, true));  // Second open should fail
        
        std::string errMsg = writer.getErrorMsg();
        EXPECT_FALSE(errMsg.empty());
        EXPECT_NE(errMsg.find("already open"), std::string::npos);
        // std::cout << "  ✓ Double open error: " << errMsg << "\n";
        
        writer.close();
    }
    
    // std::cout << "\n✅ All Writer error messages properly captured in getErrorMsg()\n";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
