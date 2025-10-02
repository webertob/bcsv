#ifndef BCSV_C_API_H
#define BCSV_C_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles for C++ objects
typedef void* bcsv_reader_t;
typedef void* bcsv_writer_t;
typedef void* bcsv_row_t;
typedef void* bcsv_layout_t;
typedef const void* const_bcsv_reader_t;
typedef const void* const_bcsv_writer_t;
typedef const void* const_bcsv_row_t;
typedef const void* const_bcsv_layout_t;

// Enum for column types must match bcsv::ColumnType!
typedef enum {
    BCSV_TYPE_BOOL   = 0,
    BCSV_TYPE_UINT8  = 1,
    BCSV_TYPE_UINT16 = 2,
    BCSV_TYPE_UINT32 = 3,
    BCSV_TYPE_UINT64 = 4,
    BCSV_TYPE_INT8   = 5,
    BCSV_TYPE_INT16  = 6,
    BCSV_TYPE_INT32  = 7,
    BCSV_TYPE_INT64  = 8,
    BCSV_TYPE_FLOAT  = 9,
    BCSV_TYPE_DOUBLE = 10,
    BCSV_TYPE_STRING = 11
} bcsv_type_t;

typedef enum {
    BCSV_FLAG_NONE   = 0,
    BCSV_FLAG_ZOH    = 1 << 0, // Write in ZoH format
} bcsv_file_flags_t;

// Layout API - Start
// These functions operate on layout objects, which define the schema of BCSV files
// Layouts can be shared between readers and writers
// Layouts can be modified dynamically (add/remove columns) or cloned
// Layouts can be compared for compatibility (same columns and types)
// Layouts can be queried for column count, names, and types
bcsv_layout_t   bcsv_layout_create          ();
bcsv_layout_t   bcsv_layout_clone           (const_bcsv_layout_t layout);
void            bcsv_layout_destroy         (bcsv_layout_t layout);

bool            bcsv_layout_has_column      (const_bcsv_layout_t layout, const char* name);
size_t          bcsv_layout_column_count    (const_bcsv_layout_t layout);
size_t          bcsv_layout_column_index    (const_bcsv_layout_t layout, const char* name);
const char*     bcsv_layout_column_name     (const_bcsv_layout_t layout, size_t index);
bcsv_type_t     bcsv_layout_column_type     (const_bcsv_layout_t layout, size_t index);

bool            bcsv_layout_set_column_name (bcsv_layout_t layout, size_t index, const char* name);
void            bcsv_layout_set_column_type (bcsv_layout_t layout, size_t index, bcsv_type_t type);
bool            bcsv_layout_add_column      (bcsv_layout_t layout, size_t index, const char* name, bcsv_type_t type);
void            bcsv_layout_remove_column   (bcsv_layout_t layout, size_t index);
void            bcsv_layout_clear           (bcsv_layout_t layout);
bool            bcsv_layout_isCompatible    (const_bcsv_layout_t layout1, const_bcsv_layout_t layout2);
void            bcsv_layout_assign          (bcsv_layout_t dest, const_bcsv_layout_t src);
// Layout API - End



// Reader API - Start
// These functions operate on reader objects, which read BCSV files row by row
// Readers can be opened in strict or resilient mode (handle schema mismatches)
// Readers provide access to the current row and its index
typedef enum {
    BCSV_READ_MODE_STRICT    = 0,
    BCSV_READ_MODE_RESILIENT = 1
} bcsv_read_mode_t;

bcsv_reader_t       bcsv_reader_create  (bcsv_read_mode_t mode);
void                bcsv_reader_destroy (bcsv_reader_t reader);

void                bcsv_reader_close   (bcsv_reader_t reader);
bool                bcsv_reader_open    (bcsv_reader_t reader, const char* filename);
bool                bcsv_reader_is_open (const_bcsv_reader_t reader);
const char*         bcsv_reader_filename(const_bcsv_reader_t reader);
const_bcsv_layout_t bcsv_reader_layout  (const_bcsv_reader_t reader);    // returns layout of opened file, or NULL if not open

bool                bcsv_reader_next    (bcsv_reader_t reader);          // returns 1 if row available, 0 if EOF
const_bcsv_row_t    bcsv_reader_row     (const_bcsv_reader_t reader);    // returns reference to internal row (no copy)
size_t              bcsv_reader_index   (const_bcsv_reader_t reader);    // returns current row index (0-based), number of rows read so far
// Reader API - End



// Writer API - Start
// These functions operate on writer objects, which write BCSV files row by row
// Writers can be opened in strict or resilient mode (handle schema mismatches)
// Writers provide access to the current row and its index
bcsv_writer_t       bcsv_writer_create  (bcsv_layout_t layout);
void                bcsv_writer_destroy (bcsv_writer_t writer);

void                bcsv_writer_close   (bcsv_writer_t writer);
void                bcsv_writer_flush   (bcsv_writer_t writer);
bool                bcsv_writer_open    (bcsv_writer_t writer, const char* filename, bool overwrite, int compress, int block_size_kb, bcsv_file_flags_t flags); // defaults: overwrite=false, compress=1, block_size_kb=64, flags=0
bool                bcsv_writer_is_open (const_bcsv_writer_t writer);
const char*         bcsv_writer_filename(const_bcsv_writer_t writer);
const_bcsv_layout_t bcsv_writer_layout  (const_bcsv_writer_t writer);      // returns layout

bool                bcsv_writer_next    (bcsv_writer_t writer);            // writes current row, returns false on error
bcsv_row_t          bcsv_writer_row     (bcsv_writer_t writer);            // returns reference to internal row (no copy)
size_t              bcsv_writer_index   (const_bcsv_writer_t writer);      // returns current row index (0-based), number of rows written so far
// Writer API - End


// Row API - Start
// These operate on the row reference, no deep copy

// Single value access
const_bcsv_layout_t bcsv_row_layout     (const_bcsv_row_t row);                // returns layout of the row
bool                bcsv_row_get_bool   (const_bcsv_row_t row, int col);
uint8_t             bcsv_row_get_uint8  (const_bcsv_row_t row, int col);
uint16_t            bcsv_row_get_uint16 (const_bcsv_row_t row, int col);
uint32_t            bcsv_row_get_uint32 (const_bcsv_row_t row, int col);
uint64_t            bcsv_row_get_uint64 (const_bcsv_row_t row, int col);
int8_t              bcsv_row_get_int8   (const_bcsv_row_t row, int col);
int16_t             bcsv_row_get_int16  (const_bcsv_row_t row, int col);
int32_t             bcsv_row_get_int32  (const_bcsv_row_t row, int col);
int64_t             bcsv_row_get_int64  (const_bcsv_row_t row, int col);
float               bcsv_row_get_float  (const_bcsv_row_t row, int col);
double              bcsv_row_get_double (const_bcsv_row_t row, int col);
const char*         bcsv_row_get_string (const_bcsv_row_t row, int col);
void                bcsv_row_set_bool   (bcsv_row_t row, int col, bool        value);
void                bcsv_row_set_uint8  (bcsv_row_t row, int col, uint8_t     value);
void                bcsv_row_set_uint16 (bcsv_row_t row, int col, uint16_t    value);
void                bcsv_row_set_uint32 (bcsv_row_t row, int col, uint32_t    value);
void                bcsv_row_set_uint64 (bcsv_row_t row, int col, uint64_t    value);
void                bcsv_row_set_int8   (bcsv_row_t row, int col, int8_t      value);
void                bcsv_row_set_int16  (bcsv_row_t row, int col, int16_t     value);
void                bcsv_row_set_int32  (bcsv_row_t row, int col, int32_t     value);
void                bcsv_row_set_int64  (bcsv_row_t row, int col, int64_t     value);
void                bcsv_row_set_float  (bcsv_row_t row, int col, float       value);
void                bcsv_row_set_double (bcsv_row_t row, int col, double      value);
void                bcsv_row_set_string (bcsv_row_t row, int col, const char* value);

// Vectorized access - bulk get/set multiple consecutive columns
// These functions provide efficient bulk access to multiple consecutive columns of the same type
// dst/src: pointer to array buffer
// start_col: starting column index (0-based)
// count: number of consecutive columns to read/write
void                bcsv_row_get_bool_array   (const_bcsv_row_t row, int start_col, bool*     dst, size_t count);
void                bcsv_row_get_uint8_array  (const_bcsv_row_t row, int start_col, uint8_t*  dst, size_t count);
void                bcsv_row_get_uint16_array (const_bcsv_row_t row, int start_col, uint16_t* dst, size_t count);
void                bcsv_row_get_uint32_array (const_bcsv_row_t row, int start_col, uint32_t* dst, size_t count);
void                bcsv_row_get_uint64_array (const_bcsv_row_t row, int start_col, uint64_t* dst, size_t count);
void                bcsv_row_get_int8_array   (const_bcsv_row_t row, int start_col, int8_t*   dst, size_t count);
void                bcsv_row_get_int16_array  (const_bcsv_row_t row, int start_col, int16_t*  dst, size_t count);
void                bcsv_row_get_int32_array  (const_bcsv_row_t row, int start_col, int32_t*  dst, size_t count);
void                bcsv_row_get_int64_array  (const_bcsv_row_t row, int start_col, int64_t*  dst, size_t count);
void                bcsv_row_get_float_array  (const_bcsv_row_t row, int start_col, float*    dst, size_t count);
void                bcsv_row_get_double_array (const_bcsv_row_t row, int start_col, double*   dst, size_t count);
void                bcsv_row_set_bool_array   (bcsv_row_t row, int start_col, const bool*     src, size_t count);
void                bcsv_row_set_uint8_array  (bcsv_row_t row, int start_col, const uint8_t*  src, size_t count);
void                bcsv_row_set_uint16_array (bcsv_row_t row, int start_col, const uint16_t* src, size_t count);
void                bcsv_row_set_uint32_array (bcsv_row_t row, int start_col, const uint32_t* src, size_t count);
void                bcsv_row_set_uint64_array (bcsv_row_t row, int start_col, const uint64_t* src, size_t count);
void                bcsv_row_set_int8_array   (bcsv_row_t row, int start_col, const int8_t*   src, size_t count);
void                bcsv_row_set_int16_array  (bcsv_row_t row, int start_col, const int16_t*  src, size_t count);
void                bcsv_row_set_int32_array  (bcsv_row_t row, int start_col, const int32_t*  src, size_t count);
void                bcsv_row_set_int64_array  (bcsv_row_t row, int start_col, const int64_t*  src, size_t count);
void                bcsv_row_set_float_array  (bcsv_row_t row, int start_col, const float*    src, size_t count);
void                bcsv_row_set_double_array (bcsv_row_t row, int start_col, const double*   src, size_t count);
// Row API - End

const char*         bcsv_last_error();

#ifdef __cplusplus
}
#endif

#endif // BCSV_C_API_H
