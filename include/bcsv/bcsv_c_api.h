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
typedef void* bcsv_sampler_t;
typedef const void* const_bcsv_reader_t;
typedef const void* const_bcsv_writer_t;
typedef const void* const_bcsv_row_t;
typedef const void* const_bcsv_layout_t;
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

// Format for the unified open (ADR-0009). AUTO: readers by content magic,
// writers by extension. A pinned format never falls back — mismatch is loud.
typedef enum {
    BCSV_FORMAT_AUTO    = 0,
    BCSV_FORMAT_BCSV    = 1,
    BCSV_FORMAT_CSV     = 2,
    BCSV_FORMAT_CSV_LZ4 = 3
} bcsv_format_t;

// Row codec of a unified writer's .bcsv member (inert for CSV output).
typedef enum {
    BCSV_CODEC_FLAT  = 0,   // dense flat columns — equals bcsv_writer_create
    BCSV_CODEC_ZOH   = 1,   // zero-order hold   — equals bcsv_writer_create_zoh
    BCSV_CODEC_DELTA = 2    // delta + VLE       — equals bcsv_writer_create_delta
} bcsv_row_codec_t;

// Open options for the *_open_u calls (ADR-0009). NULL or BCSV_OPEN_OPTS_INIT
// = all defaults (CSV reads still REQUIRE csv_layout). Tri-state ints:
// -1 = unset. [R]/[W] = reader/writer opens only; rest inert per kind/format.
// struct_size forward-extension rule: see load_open_opts() in bcsv_c_api.cpp.
typedef struct {
    size_t          struct_size;      // sizeof(bcsv_open_opts) at your compile time
    // BCSV reader [R]
    int             rebuild_footer;   // -1 = false; like bcsv_reader_open_ex
    // CSV member
    bcsv_layout_t   csv_layout;       // [R] CSV modes: REQUIRED (text carries no schema); [W] inert
    char            delimiter;        // 0 = ','
    char            decimal_sep;      // 0 = '.'
    int             has_header;       // [R] CSV: -1 = true
    int             include_header;   // [W] CSV: -1 = true
    // BCSV member writer [W]
    int             compression;      // -1 = library default (6)
    int             block_size_kb;    // -1 = library default (8MB packets)
    int             file_flags;       // -1 = auto: BATCH_COMPRESS | row-codec bit
    int             lz4_block_id;     // [W] CSV_LZ4: -1 = 4 (64KB blocks)
    int             lz4_level;        // [W] CSV_LZ4: -1 = 0 fast, 2..12 LZ4HC
} bcsv_open_opts;

#define BCSV_OPEN_OPTS_INIT \
    { sizeof(bcsv_open_opts), -1, NULL, 0, 0, -1, -1, -1, -1, -1, -1, -1 }

// ============================================================================
// Visitor callback type
// ============================================================================
typedef void (*bcsv_visit_callback_t)(size_t col_index, bcsv_type_t col_type,
                                       const void* value, void* user_data); // once per column; value matches col_type (const char* for STRING), valid during the call only

// ============================================================================
// Version API
// ============================================================================
const char*         bcsv_version        (void);                         // library version string, e.g. "1.5.0"
int                 bcsv_version_major  (void);                         // library major version
int                 bcsv_version_minor  (void);                         // library minor version
int                 bcsv_version_patch  (void);                         // library patch version

// ============================================================================
// Error API
// ============================================================================
const char*         bcsv_last_error     (void);                         // thread-local last error string (empty if no error)
void                bcsv_clear_last_error(void);                        // explicitly reset error state

// ============================================================================
// Lifecycle API
// ============================================================================
void                bcsv_shutdown       (void);                         // bulk teardown at process exit — contract: docs/adr/0006

// ============================================================================
// Layout API — schema (column names/types), shared reader/writer, mutable
// ============================================================================
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
bool                bcsv_writer_is_poisoned(const_bcsv_writer_t writer);  // MAX_ROW_LENGTH poison: writes fail until flush/close (see impl)
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
int                 bcsv_writer_file_flags      (const_bcsv_writer_t writer);  // flags actually written (row-codec bits come from the writer)

// ============================================================================
// Unified File API (ADR-0009) — one reader/writer for .bcsv, .csv, .csv.lz4
// ============================================================================
// A _u handle is a format-agnostic container; its member is built at open.
// Rule: _u handles pair with _u functions only (cross-family destroy is a
// kind-checked no-op). Migration from the removed bcsv_csv_* families:
// docs/adr/0009. Row/Layout APIs apply unchanged.
bool                    bcsv_probe_format     (const char* filename, bcsv_format_t* out_format); // classify by magic, no data open

// ── Unified reader ──────────────────────────────────────────────────────────
bcsv_reader_t           bcsv_reader_create_u  (void);                        // inert container
void                    bcsv_reader_destroy_u (bcsv_reader_t reader);
bool                    bcsv_reader_open_u    (bcsv_reader_t reader, const char* filename,
                                               bcsv_format_t format, const bcsv_open_opts* opts); // CSV opens REQUIRE opts->csv_layout
void                    bcsv_reader_close_u   (bcsv_reader_t reader);        // safe when not open
bool                    bcsv_reader_is_open_u (const_bcsv_reader_t reader);
bool                    bcsv_reader_next_u    (bcsv_reader_t reader);        // +1 row, both members
const_bcsv_row_t        bcsv_reader_row_u     (const_bcsv_reader_t reader);  // borrowed until next read
size_t                  bcsv_reader_index_u   (const_bcsv_reader_t reader);  // rows read so far
size_t                  bcsv_reader_file_line_u(const_bcsv_reader_t reader); // 1-based line (CSV/LZ4), 0 in BCSV mode
const_bcsv_layout_t     bcsv_reader_layout_u  (const_bcsv_reader_t reader);  // NULL before open
bcsv_format_t           bcsv_reader_format_u  (const_bcsv_reader_t reader);  // what AUTO resolved to
#ifdef _WIN32
const wchar_t*          bcsv_reader_filename_u(const_bcsv_reader_t reader);
#else
const char*             bcsv_reader_filename_u(const_bcsv_reader_t reader);
#endif
const char*             bcsv_reader_error_msg_u(const_bcsv_reader_t reader);
void*                   bcsv_reader_native_u  (const_bcsv_reader_t reader);  // borrowed member for deep APIs; never destroy, never keep across close/reopen (see impl)

// ── Unified writer ──────────────────────────────────────────────────────────
bcsv_writer_t           bcsv_writer_create_u  (bcsv_layout_t layout, bcsv_row_codec_t row_codec); // row_codec names the .bcsv member codec only
void                    bcsv_writer_destroy_u (bcsv_writer_t writer);
bool                    bcsv_writer_open_u    (bcsv_writer_t writer, const char* filename, bool overwrite,
                                               bcsv_format_t format, const bcsv_open_opts* opts); // member (and its row) exists after open
void                    bcsv_writer_close_u   (bcsv_writer_t writer);        // safe when not open
bool                    bcsv_writer_is_open_u (const_bcsv_writer_t writer);
bool                    bcsv_writer_next_u    (bcsv_writer_t writer);        // write internal row
bool                    bcsv_writer_write_u   (bcsv_writer_t writer, const_bcsv_row_t row); // write external row
bcsv_row_t              bcsv_writer_row_u     (bcsv_writer_t writer);        // mutable internal row (NULL before open)
size_t                  bcsv_writer_index_u   (const_bcsv_writer_t writer);  // rows written
const_bcsv_layout_t     bcsv_writer_layout_u  (const_bcsv_writer_t writer);
bcsv_format_t           bcsv_writer_format_u  (const_bcsv_writer_t writer);
#ifdef _WIN32
const wchar_t*          bcsv_writer_filename_u(const_bcsv_writer_t writer);
#else
const char*             bcsv_writer_filename_u(const_bcsv_writer_t writer);
#endif
const char*             bcsv_writer_error_msg_u(const_bcsv_writer_t writer);
void*                   bcsv_writer_native_u  (const_bcsv_writer_t writer);  // borrowed member (see bcsv_reader_native_u)

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
float               bcsv_row_get_float  (const_bcsv_row_t row, int col);         // strict: DOUBLE columns are an error, not narrowed
double              bcsv_row_get_double (const_bcsv_row_t row, int col);         // widens losslessly: bool/int8..int32/uint8..uint32/float/double; int64/uint64/string = error
const char*         bcsv_row_get_string (const_bcsv_row_t row, int col);         // valid until the cell changes

// Checked twins of the getters below: true + *out on success, false +
// bcsv_last_error() otherwise (*out untouched); type rules per plain getter.
bool                bcsv_row_try_get_bool   (const_bcsv_row_t row, int col, bool*        out);
bool                bcsv_row_try_get_uint8  (const_bcsv_row_t row, int col, uint8_t*     out);
bool                bcsv_row_try_get_uint16 (const_bcsv_row_t row, int col, uint16_t*    out);
bool                bcsv_row_try_get_uint32 (const_bcsv_row_t row, int col, uint32_t*    out);
bool                bcsv_row_try_get_uint64 (const_bcsv_row_t row, int col, uint64_t*    out);
bool                bcsv_row_try_get_int8   (const_bcsv_row_t row, int col, int8_t*      out);
bool                bcsv_row_try_get_int16  (const_bcsv_row_t row, int col, int16_t*     out);
bool                bcsv_row_try_get_int32  (const_bcsv_row_t row, int col, int32_t*     out);
bool                bcsv_row_try_get_int64  (const_bcsv_row_t row, int col, int64_t*     out);
bool                bcsv_row_try_get_float  (const_bcsv_row_t row, int col, float*       out);
bool                bcsv_row_try_get_double (const_bcsv_row_t row, int col, double*      out);
bool                bcsv_row_try_get_string (const_bcsv_row_t row, int col, const char** out);  // *out valid until the cell changes
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

// Vectorized: bulk get/set of count consecutive same-type columns from start_col
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

const char*         bcsv_row_to_string  (const_bcsv_row_t row);                   // debug string (thread-local buffer, valid until next call)
size_t              bcsv_row_column_count(const_bcsv_row_t row);                   // columns in this row's layout

// ============================================================================
// Row Visit API — cb once per column in [start_col, start_col+count)
// ============================================================================
void                bcsv_row_visit_const(const_bcsv_row_t row, size_t start_col, size_t count,
                                          bcsv_visit_callback_t cb, void* user_data); // bad range: cb not called, error via bcsv_last_error()

// ============================================================================
// Sampler API — expression filter + projection over an open (not owned) reader
// ============================================================================
bcsv_sampler_t      bcsv_sampler_create         (bcsv_reader_t reader);
void                bcsv_sampler_destroy        (bcsv_sampler_t sampler);        // destroy before its reader

bool                bcsv_sampler_set_conditional(bcsv_sampler_t sampler, const char* expr); // compile filter; error -> error_msg
bool                bcsv_sampler_set_selection  (bcsv_sampler_t sampler, const char* expr); // compile projection
const char*         bcsv_sampler_get_conditional(const_bcsv_sampler_t sampler);
const char*         bcsv_sampler_get_selection  (const_bcsv_sampler_t sampler);

void                bcsv_sampler_set_mode       (bcsv_sampler_t sampler, bcsv_sampler_mode_t mode); // boundary mode
bcsv_sampler_mode_t bcsv_sampler_get_mode       (const_bcsv_sampler_t sampler);

bool                bcsv_sampler_next           (bcsv_sampler_t sampler);        // advance to next match
const_bcsv_row_t    bcsv_sampler_row            (const_bcsv_sampler_t sampler);  // current output row
const_bcsv_layout_t bcsv_sampler_output_layout  (const_bcsv_sampler_t sampler);  // projected layout (source layout w/o selection)
size_t              bcsv_sampler_source_row_pos (const_bcsv_sampler_t sampler);  // source index of current row
const char*         bcsv_sampler_error_msg      (const_bcsv_sampler_t sampler);

// ============================================================================
// Columnar Bulk I/O API — column-oriented batch transfer (see impl for buffers)
// ============================================================================
size_t              bcsv_reader_read_columns    (bcsv_reader_t reader, void** bufs,
                                                 size_t num_cols, size_t max_rows); // bufs[c]: numeric pre-allocated col-type buffer; string cols ignored; returns rows read

const char*         bcsv_reader_column_string   (bcsv_reader_t reader, size_t col, size_t row); // batch-relative; "" out of range
size_t              bcsv_reader_column_string_count(bcsv_reader_t reader, size_t col);
size_t              bcsv_reader_column_strings_packed(bcsv_reader_t reader, size_t col,
                                                      char* out_buf, size_t buf_size); // '\0'-joined; returns bytes needed (NULL/0 = size query)

bool                bcsv_writer_write_columns   (bcsv_writer_t writer, const void** bufs,
                                                 size_t num_cols, size_t num_rows); // string cols: const char** per-row array

size_t              bcsv_layout_row_data_size   (const_bcsv_layout_t layout);      // compact fixed-width row bytes (strings excluded)
int                 bcsv_reader_file_flags      (const_bcsv_reader_t reader);      // flags of the opened file

#ifdef __cplusplus
}
#endif

#endif // BCSV_C_API_H
