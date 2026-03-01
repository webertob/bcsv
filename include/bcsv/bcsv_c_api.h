/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#ifndef BCSV_C_API_H
#define BCSV_C_API_H

#include <stddef.h>   // for size_t, NULL
#include <stdbool.h>  // for bool, true, false
#include <stdint.h>   // for uint8_t, int32_t, etc.

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque handles
// ============================================================================
typedef void* bcsv_reader_t;
typedef void* bcsv_writer_t;
typedef void* bcsv_row_t;
typedef void* bcsv_layout_t;
typedef void* bcsv_csv_reader_t;
typedef void* bcsv_csv_writer_t;
typedef void* bcsv_sampler_t;
typedef const void* const_bcsv_reader_t;
typedef const void* const_bcsv_writer_t;
typedef const void* const_bcsv_row_t;
typedef const void* const_bcsv_layout_t;
typedef const void* const_bcsv_csv_reader_t;
typedef const void* const_bcsv_csv_writer_t;
typedef const void* const_bcsv_sampler_t;

// ============================================================================
// Enums
// ============================================================================

// Column type enum — must match bcsv::ColumnType!
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

// File flags — must match bcsv::FileFlags!
typedef enum {
    BCSV_FLAG_NONE            = 0,
    BCSV_FLAG_ZOH             = 1 << 0,  // Zero-order hold row codec
    BCSV_FLAG_NO_FILE_INDEX   = 1 << 1,  // No file index (sequential scan only)
    BCSV_FLAG_STREAM_MODE     = 1 << 2,  // Stream mode (no packets/checksums/footer)
    BCSV_FLAG_BATCH_COMPRESS  = 1 << 3,  // Batch-compressed LZ4 packets
    BCSV_FLAG_DELTA_ENCODING  = 1 << 4,  // Delta + VLE row encoding
} bcsv_file_flags_t;

// Sampler mode — must match bcsv::SamplerMode!
typedef enum {
    BCSV_SAMPLER_TRUNCATE = 0,  // Skip rows where window is incomplete at boundaries
    BCSV_SAMPLER_EXPAND   = 1,  // Clamp out-of-bounds references to edge row
} bcsv_sampler_mode_t;

// ============================================================================
// Visitor callback type
// ============================================================================
// Called once per column during bcsv_row_visit_const.
// col_index: column index (0-based)
// col_type:  bcsv_type_t of this column
// value:     pointer to the column value (bool*, uint8_t*, ..., const char* for STRING)
// user_data: opaque pointer passed through from bcsv_row_visit_const
typedef void (*bcsv_visit_callback_t)(size_t col_index, bcsv_type_t col_type,
                                       const void* value, void* user_data);

// ============================================================================
// Version API
// ============================================================================
const char*         bcsv_version        (void);                         // library version string, e.g. "1.2.0"
int                 bcsv_version_major  (void);                         // library major version
int                 bcsv_version_minor  (void);                         // library minor version
int                 bcsv_version_patch  (void);                         // library patch version
const char*         bcsv_format_version (void);                         // file format version string, e.g. "1.3.0"

// ============================================================================
// Error API
// ============================================================================
const char*         bcsv_last_error     (void);                         // thread-local last error string (empty if no error)
void                bcsv_clear_last_error(void);                        // explicitly reset error state

// ============================================================================
// Layout API
// ============================================================================
// Layouts define the schema (column names and types) of BCSV files.
// They can be shared between readers and writers, modified dynamically,
// cloned, compared for compatibility, and queried.
bcsv_layout_t   bcsv_layout_create          (void);
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
bool            bcsv_layout_is_compatible   (const_bcsv_layout_t layout1, const_bcsv_layout_t layout2);
void            bcsv_layout_assign          (bcsv_layout_t dest, const_bcsv_layout_t src);
size_t          bcsv_layout_column_count_by_type(const_bcsv_layout_t layout, bcsv_type_t type); // count columns of given type
const char*     bcsv_layout_to_string       (const_bcsv_layout_t layout);                       // debug string (thread-local buffer, valid until next call)

// ============================================================================
// Reader API (BCSV binary files)
// ============================================================================
bcsv_reader_t       bcsv_reader_create  (void);
void                bcsv_reader_destroy (bcsv_reader_t reader);

void                bcsv_reader_close   (bcsv_reader_t reader);
bool                bcsv_reader_open    (bcsv_reader_t reader, const char* filename);
bool                bcsv_reader_open_ex (bcsv_reader_t reader, const char* filename, bool rebuild_footer);  // open with optional footer rebuild
bool                bcsv_reader_is_open (const_bcsv_reader_t reader);
#ifdef _WIN32
const wchar_t*      bcsv_reader_filename(const_bcsv_reader_t reader);
#else
const char*         bcsv_reader_filename(const_bcsv_reader_t reader);
#endif
const_bcsv_layout_t bcsv_reader_layout  (const_bcsv_reader_t reader);    // returns layout of opened file, or NULL if not open

bool                bcsv_reader_next    (bcsv_reader_t reader);          // read next row sequentially, returns true if row available
bool                bcsv_reader_read    (bcsv_reader_t reader, size_t index); // random-access read by row index, returns true on success
const_bcsv_row_t    bcsv_reader_row     (const_bcsv_reader_t reader);    // returns reference to internal row (no copy)
size_t              bcsv_reader_index   (const_bcsv_reader_t reader);    // returns current row index (0-based)
size_t              bcsv_reader_count_rows(const_bcsv_reader_t reader);  // total row count from file footer

const char*         bcsv_reader_error_msg       (const_bcsv_reader_t reader);    // per-handle error message
uint8_t             bcsv_reader_compression_level(const_bcsv_reader_t reader);   // compression level of opened file

// ============================================================================
// Writer API (BCSV binary files)
// ============================================================================
bcsv_writer_t       bcsv_writer_create      (bcsv_layout_t layout);           // flat (no row codec) writer
bcsv_writer_t       bcsv_writer_create_zoh  (bcsv_layout_t layout);           // zero-order hold writer
bcsv_writer_t       bcsv_writer_create_delta(bcsv_layout_t layout);           // delta + VLE writer
void                bcsv_writer_destroy (bcsv_writer_t writer);

void                bcsv_writer_close   (bcsv_writer_t writer);
void                bcsv_writer_flush   (bcsv_writer_t writer);
bool                bcsv_writer_open    (bcsv_writer_t writer, const char* filename, bool overwrite, int compress, int block_size_kb, bcsv_file_flags_t flags);
bool                bcsv_writer_is_open (const_bcsv_writer_t writer);
#ifdef _WIN32
const wchar_t*      bcsv_writer_filename(const_bcsv_writer_t writer);
#else
const char*         bcsv_writer_filename(const_bcsv_writer_t writer);
#endif
const_bcsv_layout_t bcsv_writer_layout  (const_bcsv_writer_t writer);             // returns layout

bool                bcsv_writer_next    (bcsv_writer_t writer);                   // writes internal row, returns false on error
bool                bcsv_writer_write   (bcsv_writer_t writer, const_bcsv_row_t row); // writes external row, returns false on error
bcsv_row_t          bcsv_writer_row     (bcsv_writer_t writer);                   // returns reference to internal row (no copy)
size_t              bcsv_writer_index   (const_bcsv_writer_t writer);             // returns rows written so far

const char*         bcsv_writer_error_msg       (const_bcsv_writer_t writer);    // per-handle error message
uint8_t             bcsv_writer_compression_level(const_bcsv_writer_t writer);   // compression level

// ============================================================================
// CSV Reader API
// ============================================================================
bcsv_csv_reader_t       bcsv_csv_reader_create  (bcsv_layout_t layout, char delimiter, char decimal_sep);
void                    bcsv_csv_reader_destroy  (bcsv_csv_reader_t reader);

bool                    bcsv_csv_reader_open     (bcsv_csv_reader_t reader, const char* filename, bool has_header);
void                    bcsv_csv_reader_close    (bcsv_csv_reader_t reader);
bool                    bcsv_csv_reader_is_open  (const_bcsv_csv_reader_t reader);
#ifdef _WIN32
const wchar_t*          bcsv_csv_reader_filename (const_bcsv_csv_reader_t reader);
#else
const char*             bcsv_csv_reader_filename (const_bcsv_csv_reader_t reader);
#endif
const_bcsv_layout_t     bcsv_csv_reader_layout   (const_bcsv_csv_reader_t reader);
bool                    bcsv_csv_reader_next     (bcsv_csv_reader_t reader);       // read next CSV row
const_bcsv_row_t        bcsv_csv_reader_row      (const_bcsv_csv_reader_t reader); // current row
size_t                  bcsv_csv_reader_index    (const_bcsv_csv_reader_t reader);  // rows read so far
size_t                  bcsv_csv_reader_file_line(const_bcsv_csv_reader_t reader);  // current file line number (1-based)
const char*             bcsv_csv_reader_error_msg(const_bcsv_csv_reader_t reader);  // per-handle error message

// ============================================================================
// CSV Writer API
// ============================================================================
bcsv_csv_writer_t       bcsv_csv_writer_create   (bcsv_layout_t layout, char delimiter, char decimal_sep);
void                    bcsv_csv_writer_destroy   (bcsv_csv_writer_t writer);

bool                    bcsv_csv_writer_open      (bcsv_csv_writer_t writer, const char* filename, bool overwrite, bool include_header);
void                    bcsv_csv_writer_close     (bcsv_csv_writer_t writer);
bool                    bcsv_csv_writer_is_open   (const_bcsv_csv_writer_t writer);
#ifdef _WIN32
const wchar_t*          bcsv_csv_writer_filename  (const_bcsv_csv_writer_t writer);
#else
const char*             bcsv_csv_writer_filename  (const_bcsv_csv_writer_t writer);
#endif
const_bcsv_layout_t     bcsv_csv_writer_layout    (const_bcsv_csv_writer_t writer);
bool                    bcsv_csv_writer_next      (bcsv_csv_writer_t writer);                    // writes internal row
bool                    bcsv_csv_writer_write     (bcsv_csv_writer_t writer, const_bcsv_row_t row); // writes external row
bcsv_row_t              bcsv_csv_writer_row       (bcsv_csv_writer_t writer);                    // mutable row handle
size_t                  bcsv_csv_writer_index     (const_bcsv_csv_writer_t writer);               // rows written so far
const char*             bcsv_csv_writer_error_msg (const_bcsv_csv_writer_t writer);               // per-handle error message

// ============================================================================
// Row API
// ============================================================================

// Row lifecycle
bcsv_row_t          bcsv_row_create      (const_bcsv_layout_t layout);            // creates a new row with given layout
bcsv_row_t          bcsv_row_clone       (const_bcsv_row_t row);                  // creates a copy of an existing row
void                bcsv_row_destroy     (bcsv_row_t row);                        // destroys a row created with bcsv_row_create
void                bcsv_row_clear       (bcsv_row_t row);                        // clears all values in the row
void                bcsv_row_assign      (bcsv_row_t dest, const_bcsv_row_t src); // assigns src row data to dest row

// Single value access
const_bcsv_layout_t bcsv_row_layout     (const_bcsv_row_t row);                   // returns layout of the row
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

// Vectorized access - bulk get/set multiple consecutive columns of the same type
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

// Debug
const char*         bcsv_row_to_string  (const_bcsv_row_t row);                   // debug string (thread-local buffer, valid until next call)

// Column count
size_t              bcsv_row_column_count(const_bcsv_row_t row);                   // number of columns in this row's layout

// ============================================================================
// Row Visit API (read-only visitor pattern via callback)
// ============================================================================
// Visits each column in [start_col, start_col+count) calling cb once per column.
// For each column the callback receives: column index, column type, pointer to
// value (bool*, uint8_t*, ..., const char* for STRING), and user_data.
// Numeric pointers remain valid only for the duration of the callback invocation.
void                bcsv_row_visit_const(const_bcsv_row_t row, size_t start_col, size_t count,
                                          bcsv_visit_callback_t cb, void* user_data);

// ============================================================================
// Sampler API (expression-based filter + project over a Reader)
// ============================================================================
// Creates a sampler attached to an opened reader. The sampler does NOT take
// ownership of the reader — the caller must keep the reader open and destroy
// it after the sampler is destroyed.
bcsv_sampler_t      bcsv_sampler_create         (bcsv_reader_t reader);
void                bcsv_sampler_destroy        (bcsv_sampler_t sampler);

// Compile a conditional (filter) expression. Returns true on success.
// On failure, bcsv_sampler_error_msg() reports the compilation error.
bool                bcsv_sampler_set_conditional(bcsv_sampler_t sampler, const char* expr);

// Compile a selection (projection) expression. Returns true on success.
bool                bcsv_sampler_set_selection  (bcsv_sampler_t sampler, const char* expr);

// Get the compiled expressions (empty if not set)
const char*         bcsv_sampler_get_conditional(const_bcsv_sampler_t sampler);
const char*         bcsv_sampler_get_selection  (const_bcsv_sampler_t sampler);

// Set/get boundary mode (TRUNCATE or EXPAND)
void                bcsv_sampler_set_mode       (bcsv_sampler_t sampler, bcsv_sampler_mode_t mode);
bcsv_sampler_mode_t bcsv_sampler_get_mode       (const_bcsv_sampler_t sampler);

// Advance to next matching row. Returns true if a row is available.
bool                bcsv_sampler_next           (bcsv_sampler_t sampler);

// Current output row (valid after a successful bcsv_sampler_next)
const_bcsv_row_t    bcsv_sampler_row            (const_bcsv_sampler_t sampler);

// Output layout (reflects selection projection; equals source layout if no selection set)
const_bcsv_layout_t bcsv_sampler_output_layout  (const_bcsv_sampler_t sampler);

// Source-file row index of the current output row
size_t              bcsv_sampler_source_row_pos (const_bcsv_sampler_t sampler);

// Last error/compile-error message (empty if no error)
const char*         bcsv_sampler_error_msg      (const_bcsv_sampler_t sampler);

#ifdef __cplusplus
}
#endif

#endif // BCSV_C_API_H
