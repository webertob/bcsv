/*
 * Example demonstrating vectorized access in the BCSV C API
 * 
 * This example shows how to use the new vectorized get/set functions
 * to efficiently read and write multiple consecutive columns at once.
 */

#include <stdio.h>
#include "../include/bcsv/bcsv_c_api.h"

int main() {
    // Create a layout with 10 consecutive int32 columns
    bcsv_layout_t layout = bcsv_layout_create();
    for (int i = 0; i < 10; i++) {
        char name[20];
        snprintf(name, sizeof(name), "col%d", i);
        bcsv_layout_add_column(layout, i, name, BCSV_TYPE_INT32);
    }
    
    // Create writer and open file
    bcsv_writer_t writer = bcsv_writer_create(layout);
    if (!bcsv_writer_open(writer, "vectorized_test.bcsv", true, 1, 64, BCSV_FLAG_NONE)) {
        fprintf(stderr, "Failed to open writer\n");
        return 1;
    }
    
    // Write 5 rows using vectorized API
    printf("Writing 5 rows using vectorized API...\n");
    for (int row_idx = 0; row_idx < 5; row_idx++) {
        bcsv_row_t row = bcsv_writer_row(writer);
        
        // Prepare data: 10 consecutive integers
        int32_t data[10];
        for (int i = 0; i < 10; i++) {
            data[i] = row_idx * 100 + i;
        }
        
        // Write all 10 columns at once using vectorized API
        bcsv_row_set_int32_array(row, 0, data, 10);
        
        if (!bcsv_writer_next(writer)) {
            fprintf(stderr, "Failed to write row %d\n", row_idx);
            return 1;
        }
    }
    
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);
    printf("Write complete: 5 rows written\n\n");
    
    // Read back using vectorized API
    bcsv_reader_t reader = bcsv_reader_create();
    if (!bcsv_reader_open(reader, "vectorized_test.bcsv")) {
        fprintf(stderr, "Failed to open reader\n");
        return 1;
    }
    
    printf("Reading rows using vectorized API...\n");
    int row_count = 0;
    while (bcsv_reader_next(reader)) {
        const_bcsv_row_t row = bcsv_reader_row(reader);
        
        // Read all 10 columns at once using vectorized API
        int32_t data[10];
        bcsv_row_get_int32_array(row, 0, data, 10);
        
        printf("Row %d: [", row_count);
        for (int i = 0; i < 10; i++) {
            printf("%d", data[i]);
            if (i < 9) printf(", ");
        }
        printf("]\n");
        
        row_count++;
    }
    
    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_layout_destroy(layout);
    
    printf("\nRead complete: %d rows read\n", row_count);
    printf("\nVectorized API Benefits:\n");
    printf("- Single function call instead of 10 individual calls\n");
    printf("- Better performance due to optimized memory access\n");
    printf("- Type-safe bulk operations with compile-time checks\n");
    
    return 0;
}
