/*
 * Comprehensive test suite for the BCSV C API
 * Tests all C API functions including layout, reader, writer, and row operations
 */

#include "../include/bcsv/bcsv_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifdef _WIN32
#include <wchar.h>
// Platform-specific filename checking functions
static bool filename_contains_expected(const wchar_t* full_path, const char* expected) {
    if (!full_path || !expected) return false;
    // Convert expected filename to wide char for comparison
    wchar_t expected_wide[256];
    size_t converted = 0;
    if (mbstowcs_s(&converted, expected_wide, sizeof(expected_wide)/sizeof(wchar_t), expected, _TRUNCATE) != 0) {
        return false;
    }
    return wcsstr(full_path, expected_wide) != NULL;
}
#else
// Platform-specific filename checking functions
static bool filename_contains_expected(const char* full_path, const char* expected) {
    if (!full_path || !expected) return false;
    return strstr(full_path, expected) != NULL;
}
#endif

// Test counters
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("  ✓ %s\n", message); \
    } else { \
        printf("  ✗ %s\n", message); \
    } \
} while(0)

#define TEST_START(name) printf("\n--- %s ---\n", name)
#define TEST_END() do { /* Test section complete */ } while(0)

void test_layout_api() {
    TEST_START("Layout API Tests");
    
    // Test layout creation
    bcsv_layout_t layout = bcsv_layout_create();
    TEST_ASSERT(layout != NULL, "Layout creation");
    
    // Test adding columns
    bool result1 = bcsv_layout_add_column(layout, 0, "name", BCSV_TYPE_STRING);
    TEST_ASSERT(result1, "Add string column");
    
    bool result2 = bcsv_layout_add_column(layout, 1, "age", BCSV_TYPE_INT32);
    TEST_ASSERT(result2, "Add int32 column");
    
    bool result3 = bcsv_layout_add_column(layout, 2, "score", BCSV_TYPE_DOUBLE);
    TEST_ASSERT(result3, "Add double column");
    
    // Test column count
    size_t count = bcsv_layout_column_count(layout);
    TEST_ASSERT(count == 3, "Column count is 3");
    
    // Test column names and types
    const char* name0 = bcsv_layout_column_name(layout, 0);
    TEST_ASSERT(strcmp(name0, "name") == 0, "Column 0 name is 'name'");
    
    bcsv_type_t type1 = bcsv_layout_column_type(layout, 1);
    TEST_ASSERT(type1 == BCSV_TYPE_INT32, "Column 1 type is INT32");
    
    // Test has column
    bool has_name = bcsv_layout_has_column(layout, "name");
    TEST_ASSERT(has_name, "Has column 'name'");
    
    bool has_missing = bcsv_layout_has_column(layout, "missing");
    TEST_ASSERT(!has_missing, "Does not have column 'missing'");
    
    // Test column index
    size_t idx = bcsv_layout_column_index(layout, "age");
    TEST_ASSERT(idx == 1, "Column 'age' index is 1");
    
    // Test layout cloning
    bcsv_layout_t cloned_layout = bcsv_layout_clone(layout);
    TEST_ASSERT(cloned_layout != NULL, "Layout cloning");
    TEST_ASSERT(bcsv_layout_column_count(cloned_layout) == 3, "Cloned layout has 3 columns");
    
    // Test layout compatibility
    bool compatible = bcsv_layout_is_compatible(layout, cloned_layout);
    TEST_ASSERT(compatible, "Layout compatibility check");
    
    // Test layout assignment
    bcsv_layout_t assigned_layout = bcsv_layout_create();
    bcsv_layout_assign(assigned_layout, layout);
    TEST_ASSERT(bcsv_layout_column_count(assigned_layout) == 3, "Layout assignment");
    
    // Cleanup
    bcsv_layout_destroy(layout);
    bcsv_layout_destroy(cloned_layout);
    bcsv_layout_destroy(assigned_layout);
    
    TEST_END();
}

void test_row_api() {
    TEST_START("Row API Tests");
    
    // Create layout for testing
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "name", BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 1, "age", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 2, "score", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 3, "active", BCSV_TYPE_BOOL);
    
    // Test row creation
    bcsv_row_t row = bcsv_row_create(layout);
    TEST_ASSERT(row != NULL, "Row creation");
    
    // Test row layout
    const_bcsv_layout_t row_layout = bcsv_row_layout(row);
    TEST_ASSERT(row_layout != NULL, "Row layout access");
    TEST_ASSERT(bcsv_layout_column_count(row_layout) == 4, "Row layout has 4 columns");
    
    // Test change tracking (compile-time only)
    TEST_ASSERT(!bcsv_row_changes_enabled(row), "Change tracking initially disabled");
    TEST_ASSERT(bcsv_row_changes_any(row), "Without change tracking, we need to conservativly assume changes are present.");
    
    // Test setting values
    bcsv_row_set_string(row, 0, "Alice");
    bcsv_row_set_int32(row, 1, 30);
    bcsv_row_set_double(row, 2, 95.5);
    bcsv_row_set_bool(row, 3, true);
    
    TEST_ASSERT(bcsv_row_changes_any(row), "Has changes after setting values");
    
    // Test getting values
    const char* name = bcsv_row_get_string(row, 0);
    TEST_ASSERT(strcmp(name, "Alice") == 0, "Get string value");
    
    int32_t age = bcsv_row_get_int32(row, 1);
    TEST_ASSERT(age == 30, "Get int32 value");
    
    double score = bcsv_row_get_double(row, 2);
    TEST_ASSERT(score == 95.5, "Get double value");
    
    bool active = bcsv_row_get_bool(row, 3);
    TEST_ASSERT(active == true, "Get bool value");
    
    // Test change tracking functions (no effect when tracking is disabled)
    bcsv_row_changes_reset(row);
    TEST_ASSERT(bcsv_row_changes_any(row), "Reset changes is a no-op without tracking");
    
    bcsv_row_changes_set(row);
    TEST_ASSERT(bcsv_row_changes_any(row), "Set changes is a no-op without tracking");
    
    // Test row cloning
    bcsv_row_t cloned_row = bcsv_row_clone(row);
    TEST_ASSERT(cloned_row != NULL, "Row cloning");
    
    const char* cloned_name = bcsv_row_get_string(cloned_row, 0);
    TEST_ASSERT(strcmp(cloned_name, "Alice") == 0, "Cloned row has correct string value");
    
    int32_t cloned_age = bcsv_row_get_int32(cloned_row, 1);
    TEST_ASSERT(cloned_age == 30, "Cloned row has correct int32 value");
    
    // Test row assignment
    bcsv_row_t another_row = bcsv_row_create(layout);
    bcsv_row_set_string(row, 0, "Bob");
    bcsv_row_set_int32(row, 1, 25);
    
    bcsv_row_assign(another_row, row);
    
    const char* assigned_name = bcsv_row_get_string(another_row, 0);
    TEST_ASSERT(strcmp(assigned_name, "Bob") == 0, "Row assignment - string value");
    
    int32_t assigned_age = bcsv_row_get_int32(another_row, 1);
    TEST_ASSERT(assigned_age == 25, "Row assignment - int32 value");
    
    // Test row clear
    bcsv_row_clear(row);
    const char* cleared_name = bcsv_row_get_string(row, 0);
    TEST_ASSERT(strlen(cleared_name) == 0, "Row clear - string is empty");
    
    int32_t cleared_age = bcsv_row_get_int32(row, 1);
    TEST_ASSERT(cleared_age == 0, "Row clear - int32 is zero");
    
    // Test vectorized access
    int32_t test_values[3] = {10, 20, 30};
    bcsv_layout_t vector_layout = bcsv_layout_create();
    for (int i = 0; i < 3; i++) {
        char col_name[10];
        snprintf(col_name, sizeof(col_name), "col%d", i);
        bcsv_layout_add_column(vector_layout, i, col_name, BCSV_TYPE_INT32);
    }
    
    bcsv_row_t vector_row = bcsv_row_create(vector_layout);
    bcsv_row_set_int32_array(vector_row, 0, test_values, 3);
    
    int32_t retrieved_values[3];
    bcsv_row_get_int32_array(vector_row, 0, retrieved_values, 3);
    
    bool vectorized_ok = (retrieved_values[0] == 10 && retrieved_values[1] == 20 && retrieved_values[2] == 30);
    TEST_ASSERT(vectorized_ok, "Vectorized access");
    
    // Cleanup
    bcsv_row_destroy(row);
    bcsv_row_destroy(cloned_row);
    bcsv_row_destroy(another_row);
    bcsv_row_destroy(vector_row);
    bcsv_layout_destroy(layout);
    bcsv_layout_destroy(vector_layout);
    
    TEST_END();
}

void test_writer_reader_api() {
    TEST_START("Writer/Reader API Tests");
    
    const char* test_filename = "c_api_test.bcsv";
    
    // Create layout
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "name", BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 2, "value", BCSV_TYPE_DOUBLE);
    
    // Test writer creation
    bcsv_writer_t writer = bcsv_writer_create(layout);
    TEST_ASSERT(writer != NULL, "Writer creation");
    
    // Test writer open
    bool writer_opened = bcsv_writer_open(writer, test_filename, true, 1, 64, BCSV_FLAG_NONE);
    TEST_ASSERT(writer_opened, "Writer open");
    TEST_ASSERT(bcsv_writer_is_open(writer), "Writer is open");
    
#ifdef _WIN32
    const wchar_t* writer_filename = bcsv_writer_filename(writer);
#else
    const char* writer_filename = bcsv_writer_filename(writer);
#endif
    // Writer returns absolute path, so check if it ends with our test filename
    bool filename_ok = filename_contains_expected(writer_filename, test_filename);
    TEST_ASSERT(filename_ok, "Writer filename contains expected name");
    
    const_bcsv_layout_t writer_layout = bcsv_writer_layout(writer);
    TEST_ASSERT(bcsv_layout_column_count(writer_layout) == 3, "Writer layout");
    
    // Write test data
    for (int i = 0; i < 5; i++) {
        bcsv_row_t row = bcsv_writer_row(writer);
        
        bcsv_row_set_int32(row, 0, i + 1);
        
        char name_buf[20];
        snprintf(name_buf, sizeof(name_buf), "Item%d", i + 1);
        bcsv_row_set_string(row, 1, name_buf);
        
        bcsv_row_set_double(row, 2, (i + 1) * 10.5);
        
        bool write_success = bcsv_writer_next(writer);
        TEST_ASSERT(write_success, "Write row");
        
        // Note: Writer index only increments when packets are flushed, not on each writeRow()
        // So we can't reliably test the index after each write
    }
    
    // Flush to ensure all data is written and index is updated
    bcsv_writer_flush(writer);
    size_t final_writer_index = bcsv_writer_index(writer);
    TEST_ASSERT(final_writer_index == 5, "Final writer index after flush");
    
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);
    
    // Test reader
    bcsv_reader_t reader = bcsv_reader_create(BCSV_READ_MODE_STRICT);
    TEST_ASSERT(reader != NULL, "Reader creation");
    
    bool reader_opened = bcsv_reader_open(reader, test_filename);
    TEST_ASSERT(reader_opened, "Reader open");
    TEST_ASSERT(bcsv_reader_is_open(reader), "Reader is open");
    
#ifdef _WIN32
    const wchar_t* reader_filename = bcsv_reader_filename(reader);
#else
    const char* reader_filename = bcsv_reader_filename(reader);
#endif
    // Reader returns absolute path, so check if it ends with our test filename
    bool reader_filename_ok = filename_contains_expected(reader_filename, test_filename);
    TEST_ASSERT(reader_filename_ok, "Reader filename contains expected name");
    
    const_bcsv_layout_t reader_layout = bcsv_reader_layout(reader);
    TEST_ASSERT(bcsv_layout_column_count(reader_layout) == 3, "Reader layout");
    
    // Read and verify data
    int row_count = 0;
    while (bcsv_reader_next(reader)) {
        const_bcsv_row_t row = bcsv_reader_row(reader);
        TEST_ASSERT(row != NULL, "Reader row access");
        
        int32_t id = bcsv_row_get_int32(row, 0);
        TEST_ASSERT(id == row_count + 1, "Read ID value");
        
        const char* name = bcsv_row_get_string(row, 1);
        char expected_name[20];
        snprintf(expected_name, sizeof(expected_name), "Item%d", row_count + 1);
        TEST_ASSERT(strcmp(name, expected_name) == 0, "Read name value");
        
        double value = bcsv_row_get_double(row, 2);
        double expected_value = (row_count + 1) * 10.5;
        TEST_ASSERT(value == expected_value, "Read double value");
        
        size_t reader_index = bcsv_reader_index(reader);
        TEST_ASSERT(reader_index == (size_t)(row_count + 1), "Reader index");
        
        row_count++;
    }
    
    TEST_ASSERT(row_count == 5, "Read 5 rows");
    
    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    
    // Cleanup
    bcsv_layout_destroy(layout);
    
    // Remove test file
    remove(test_filename);
    
    TEST_END();
}

int main() {
    printf("BCSV C API Comprehensive Test Suite\n");
    printf("====================================\n");
    
    test_layout_api();
    test_row_api();
    test_writer_reader_api();
    
    printf("\n====================================\n");
    printf("Test Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    if (tests_passed == tests_run) {
        printf("🎉 All C API tests passed!\n");
        return 0;
    } else {
        printf("❌ Some tests failed!\n");
        return 1;
    }
}