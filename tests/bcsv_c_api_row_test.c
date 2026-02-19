/*
 * Test program for the new Row C API functions
 */

#include "../include/bcsv/bcsv_c_api.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main() {
    printf("Testing Row C API functions...\n");
    
    // Create a layout for testing
    bcsv_layout_t layout = bcsv_layout_create();
    
    // Add some columns
    bcsv_layout_add_column(layout, 0, "name", BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 1, "age", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 2, "score", BCSV_TYPE_DOUBLE);
    
    printf("Layout created with %zu columns\n", bcsv_layout_column_count(layout));
    
    // Test 1: Create a row
    printf("\n1. Testing bcsv_row_create...\n");
    bcsv_row_t row = bcsv_row_create(layout);
    assert(row != NULL);
    printf("   Row created successfully\n");
    
    // Test 2: Check layout
    printf("\n2. Testing bcsv_row_layout...\n");
    const_bcsv_layout_t row_layout = bcsv_row_layout(row);
    assert(row_layout != NULL);
    assert(bcsv_layout_column_count(row_layout) == 3);
    printf("   Row layout has %zu columns\n", bcsv_layout_column_count(row_layout));
    
    // Test 3: Set/get values
    printf("\n3. Testing value access...\n");
    
    // Set some values
    bcsv_row_set_string(row, 0, "John");
    bcsv_row_set_int32(row, 1, 30);
    bcsv_row_set_double(row, 2, 95.5);
    
    // Test 4: Clear row
    printf("\n4. Testing bcsv_row_clear...\n");
    bcsv_row_clear(row);
    printf("   Row cleared successfully\n");
    
    // Verify values are cleared (strings should be empty, numbers should be 0)
    const char* name = bcsv_row_get_string(row, 0);
    int32_t age = bcsv_row_get_int32(row, 1);
    double score = bcsv_row_get_double(row, 2);
    
    printf("   After clear - name: '%s', age: %d, score: %.1f\n", name, age, score);
    
    // Test 5: Clone row
    printf("\n5. Testing bcsv_row_clone...\n");
    
    // First, set some values in the original row
    bcsv_row_set_string(row, 0, "Alice");
    bcsv_row_set_int32(row, 1, 25);
    bcsv_row_set_double(row, 2, 87.5);
    
    // Clone the row
    bcsv_row_t cloned_row = bcsv_row_clone(row);
    assert(cloned_row != NULL);
    printf("   Row cloned successfully\n");
    
    // Verify cloned values
    const char* cloned_name = bcsv_row_get_string(cloned_row, 0);
    int32_t cloned_age = bcsv_row_get_int32(cloned_row, 1);
    double cloned_score = bcsv_row_get_double(cloned_row, 2);
    
    printf("   Cloned values - name: '%s', age: %d, score: %.1f\n", cloned_name, cloned_age, cloned_score);
    assert(strcmp(cloned_name, "Alice") == 0);
    assert(cloned_age == 25);
    assert(cloned_score == 87.5);
    
    // Test 6: Assign row
    printf("\n6. Testing bcsv_row_assign...\n");
    
    // Create another row and modify the original
    bcsv_row_t another_row = bcsv_row_create(layout);
    bcsv_row_set_string(row, 0, "Bob");
    bcsv_row_set_int32(row, 1, 35);
    bcsv_row_set_double(row, 2, 92.3);
    
    // Assign original to another
    bcsv_row_assign(another_row, row);
    printf("   Row assignment completed\n");
    
    // Verify assigned values
    const char* assigned_name = bcsv_row_get_string(another_row, 0);
    int32_t assigned_age = bcsv_row_get_int32(another_row, 1);
    double assigned_score = bcsv_row_get_double(another_row, 2);
    
    printf("   Assigned values - name: '%s', age: %d, score: %.1f\n", assigned_name, assigned_age, assigned_score);
    assert(strcmp(assigned_name, "Bob") == 0);
    assert(assigned_age == 35);
    assert(assigned_score == 92.3);
    
    // Test 7: Destroy rows
    printf("\n7. Testing bcsv_row_destroy for multiple rows...\n");
    bcsv_row_destroy(cloned_row);
    bcsv_row_destroy(another_row);
    bcsv_row_destroy(row);
    printf("   All rows destroyed successfully\n");
    
    // Clean up layout
    bcsv_layout_destroy(layout);
    printf("\nLayout destroyed successfully\n");
    
    printf("\nAll Row C API tests passed!\n");
    return 0;
}