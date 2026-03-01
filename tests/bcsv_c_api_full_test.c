/*
 * Comprehensive test suite for the BCSV C API — Item 15
 * Tests all new and existing C API functions including:
 * - Version API
 * - Layout API (full coverage)
 * - Reader API (sequential + random access)
 * - Writer API (Flat, ZoH, Delta)
 * - CSV Reader/Writer API
 * - Row API (all 12 scalar types, vectorized arrays)
 * - Error handling
 * - Debug/utility functions
 *
 * Pure C (.c) to verify header compatibility with C compilers.
 */

#include "../include/bcsv/bcsv_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Test infrastructure ───────────────────────────────────────────── */
static int tests_run    = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("  ✓ %s\n", msg); } \
    else      {                 printf("  ✗ FAIL: %s\n", msg); } \
} while(0)

#define TEST_ASSERT_EQ_INT(a, b, msg) TEST_ASSERT((a) == (b), msg)
#define TEST_ASSERT_EQ_STR(a, b, msg) TEST_ASSERT(strcmp((a),(b)) == 0, msg)
#define TEST_ASSERT_NEAR(a, b, eps, msg) TEST_ASSERT(fabs((double)(a) - (double)(b)) < (eps), msg)
#define TEST_START(name) printf("\n--- %s ---\n", name)

static const char* TMP_DIR = "/tmp/bcsv_c_api_test/";

static void ensure_tmp_dir(void) {
    /* portable enough for Linux */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", TMP_DIR);
    (void)system(cmd);
}

static void make_path(char* buf, size_t cap, const char* filename) {
    snprintf(buf, cap, "%s%s", TMP_DIR, filename);
}

/* ── Version API Tests ─────────────────────────────────────────────── */
static void test_version_api(void) {
    TEST_START("Version API");

    const char* ver = bcsv_version();
    TEST_ASSERT(ver != NULL && strlen(ver) > 0, "bcsv_version returns non-empty string");

    int major = bcsv_version_major();
    int minor = bcsv_version_minor();
    int patch = bcsv_version_patch();
    TEST_ASSERT(major >= 1, "version major >= 1");
    TEST_ASSERT(minor >= 0, "version minor >= 0");
    TEST_ASSERT(patch >= 0, "version patch >= 0");

    /* Verify version string matches components */
    char expected[32];
    snprintf(expected, sizeof(expected), "%d.%d.%d", major, minor, patch);
    TEST_ASSERT_EQ_STR(ver, expected, "version string matches components");

    const char* fmt_ver = bcsv_format_version();
    TEST_ASSERT(fmt_ver != NULL && strlen(fmt_ver) > 0, "format_version returns non-empty string");
}

/* ── Layout API Extended Tests ─────────────────────────────────────── */
static void test_layout_extended(void) {
    TEST_START("Layout Extended API");

    bcsv_layout_t layout = bcsv_layout_create();
    TEST_ASSERT(layout != NULL, "Layout create");

    bcsv_layout_add_column(layout, 0, "flag",   BCSV_TYPE_BOOL);
    bcsv_layout_add_column(layout, 1, "u8",     BCSV_TYPE_UINT8);
    bcsv_layout_add_column(layout, 2, "u16",    BCSV_TYPE_UINT16);
    bcsv_layout_add_column(layout, 3, "u32",    BCSV_TYPE_UINT32);
    bcsv_layout_add_column(layout, 4, "u64",    BCSV_TYPE_UINT64);
    bcsv_layout_add_column(layout, 5, "i8",     BCSV_TYPE_INT8);
    bcsv_layout_add_column(layout, 6, "i16",    BCSV_TYPE_INT16);
    bcsv_layout_add_column(layout, 7, "i32",    BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 8, "i64",    BCSV_TYPE_INT64);
    bcsv_layout_add_column(layout, 9, "f32",    BCSV_TYPE_FLOAT);
    bcsv_layout_add_column(layout, 10, "f64",   BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 11, "str",   BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 12, "flag2", BCSV_TYPE_BOOL);

    TEST_ASSERT_EQ_INT(bcsv_layout_column_count(layout), 13, "13 columns total");

    /* column_count_by_type */
    TEST_ASSERT_EQ_INT(bcsv_layout_column_count_by_type(layout, BCSV_TYPE_BOOL), 2, "2 bool columns");
    TEST_ASSERT_EQ_INT(bcsv_layout_column_count_by_type(layout, BCSV_TYPE_INT32), 1, "1 int32 column");
    TEST_ASSERT_EQ_INT(bcsv_layout_column_count_by_type(layout, BCSV_TYPE_STRING), 1, "1 string column");
    TEST_ASSERT_EQ_INT(bcsv_layout_column_count_by_type(layout, BCSV_TYPE_DOUBLE), 1, "1 double column");

    /* set_column_name */
    bool renamed = bcsv_layout_set_column_name(layout, 0, "renamed_flag");
    TEST_ASSERT(renamed, "set_column_name returns true");
    TEST_ASSERT_EQ_STR(bcsv_layout_column_name(layout, 0), "renamed_flag", "column name updated");

    /* set_column_type */
    bcsv_layout_set_column_type(layout, 1, BCSV_TYPE_INT32);
    TEST_ASSERT_EQ_INT(bcsv_layout_column_type(layout, 1), BCSV_TYPE_INT32, "column type changed to INT32");
    bcsv_layout_set_column_type(layout, 1, BCSV_TYPE_UINT8); /* restore */

    /* to_string */
    const char* str = bcsv_layout_to_string(layout);
    TEST_ASSERT(str != NULL && strlen(str) > 0, "layout_to_string returns non-empty");

    /* is_compatible */
    bcsv_layout_t layout2 = bcsv_layout_clone(layout);
    TEST_ASSERT(bcsv_layout_is_compatible(layout, layout2), "cloned layout is compatible");
    bcsv_layout_remove_column(layout2, 0);
    TEST_ASSERT(!bcsv_layout_is_compatible(layout, layout2), "modified layout is incompatible");

    /* clear */
    bcsv_layout_clear(layout2);
    TEST_ASSERT_EQ_INT(bcsv_layout_column_count(layout2), 0, "cleared layout has 0 columns");

    /* assign */
    bcsv_layout_assign(layout2, layout);
    TEST_ASSERT_EQ_INT(bcsv_layout_column_count(layout2), 13, "assigned layout has 13 columns");

    bcsv_layout_destroy(layout2);
    bcsv_layout_destroy(layout);
}

/* ── Row: All 12 scalar types ──────────────────────────────────────── */
static void test_row_all_types(void) {
    TEST_START("Row All 12 Scalar Types");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "bool",   BCSV_TYPE_BOOL);
    bcsv_layout_add_column(layout, 1, "u8",     BCSV_TYPE_UINT8);
    bcsv_layout_add_column(layout, 2, "u16",    BCSV_TYPE_UINT16);
    bcsv_layout_add_column(layout, 3, "u32",    BCSV_TYPE_UINT32);
    bcsv_layout_add_column(layout, 4, "u64",    BCSV_TYPE_UINT64);
    bcsv_layout_add_column(layout, 5, "i8",     BCSV_TYPE_INT8);
    bcsv_layout_add_column(layout, 6, "i16",    BCSV_TYPE_INT16);
    bcsv_layout_add_column(layout, 7, "i32",    BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 8, "i64",    BCSV_TYPE_INT64);
    bcsv_layout_add_column(layout, 9, "f32",    BCSV_TYPE_FLOAT);
    bcsv_layout_add_column(layout, 10, "f64",   BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 11, "str",   BCSV_TYPE_STRING);

    bcsv_row_t row = bcsv_row_create(layout);

    /* Set values */
    bcsv_row_set_bool  (row, 0, true);
    bcsv_row_set_uint8 (row, 1, 255);
    bcsv_row_set_uint16(row, 2, 65535);
    bcsv_row_set_uint32(row, 3, 4000000000u);
    bcsv_row_set_uint64(row, 4, 18000000000000000000ULL);
    bcsv_row_set_int8  (row, 5, -128);
    bcsv_row_set_int16 (row, 6, -32768);
    bcsv_row_set_int32 (row, 7, -2000000000);
    bcsv_row_set_int64 (row, 8, -9000000000000000000LL);
    bcsv_row_set_float (row, 9, 3.14f);
    bcsv_row_set_double(row, 10, 2.718281828459045);
    bcsv_row_set_string(row, 11, "hello world");

    /* Get and verify */
    TEST_ASSERT(bcsv_row_get_bool(row, 0) == true,                          "bool get/set");
    TEST_ASSERT(bcsv_row_get_uint8(row, 1) == 255,                          "uint8 get/set max");
    TEST_ASSERT(bcsv_row_get_uint16(row, 2) == 65535,                       "uint16 get/set max");
    TEST_ASSERT(bcsv_row_get_uint32(row, 3) == 4000000000u,                 "uint32 get/set");
    TEST_ASSERT(bcsv_row_get_uint64(row, 4) == 18000000000000000000ULL,     "uint64 get/set");
    TEST_ASSERT(bcsv_row_get_int8(row, 5) == -128,                          "int8 get/set min");
    TEST_ASSERT(bcsv_row_get_int16(row, 6) == -32768,                       "int16 get/set min");
    TEST_ASSERT(bcsv_row_get_int32(row, 7) == -2000000000,                  "int32 get/set");
    TEST_ASSERT(bcsv_row_get_int64(row, 8) == -9000000000000000000LL,       "int64 get/set");
    TEST_ASSERT_NEAR(bcsv_row_get_float(row, 9), 3.14f, 1e-6,              "float get/set");
    TEST_ASSERT_NEAR(bcsv_row_get_double(row, 10), 2.718281828459045, 1e-12,"double get/set");
    TEST_ASSERT_EQ_STR(bcsv_row_get_string(row, 11), "hello world",         "string get/set");

    /* row_to_string */
    const char* rs = bcsv_row_to_string(row);
    TEST_ASSERT(rs != NULL && strlen(rs) > 0, "row_to_string non-empty");

    bcsv_row_destroy(row);
    bcsv_layout_destroy(layout);
}

/* ── Row: Vectorized arrays for all numeric types ──────────────────── */
static void test_row_vectorized_all_types(void) {
    TEST_START("Row Vectorized Arrays All Types");

    bcsv_layout_t layout = bcsv_layout_create();
    /* 3 columns of each numeric type for vectorized 3-element access */
    bcsv_layout_add_column(layout, 0, "b0", BCSV_TYPE_BOOL);
    bcsv_layout_add_column(layout, 1, "b1", BCSV_TYPE_BOOL);
    bcsv_layout_add_column(layout, 2, "b2", BCSV_TYPE_BOOL);
    bcsv_layout_add_column(layout, 3, "u8_0", BCSV_TYPE_UINT8);
    bcsv_layout_add_column(layout, 4, "u8_1", BCSV_TYPE_UINT8);
    bcsv_layout_add_column(layout, 5, "u8_2", BCSV_TYPE_UINT8);
    bcsv_layout_add_column(layout, 6, "u16_0", BCSV_TYPE_UINT16);
    bcsv_layout_add_column(layout, 7, "u16_1", BCSV_TYPE_UINT16);
    bcsv_layout_add_column(layout, 8, "u16_2", BCSV_TYPE_UINT16);
    bcsv_layout_add_column(layout, 9, "u32_0", BCSV_TYPE_UINT32);
    bcsv_layout_add_column(layout, 10, "u32_1", BCSV_TYPE_UINT32);
    bcsv_layout_add_column(layout, 11, "u32_2", BCSV_TYPE_UINT32);
    bcsv_layout_add_column(layout, 12, "u64_0", BCSV_TYPE_UINT64);
    bcsv_layout_add_column(layout, 13, "u64_1", BCSV_TYPE_UINT64);
    bcsv_layout_add_column(layout, 14, "u64_2", BCSV_TYPE_UINT64);
    bcsv_layout_add_column(layout, 15, "i8_0", BCSV_TYPE_INT8);
    bcsv_layout_add_column(layout, 16, "i8_1", BCSV_TYPE_INT8);
    bcsv_layout_add_column(layout, 17, "i8_2", BCSV_TYPE_INT8);
    bcsv_layout_add_column(layout, 18, "i16_0", BCSV_TYPE_INT16);
    bcsv_layout_add_column(layout, 19, "i16_1", BCSV_TYPE_INT16);
    bcsv_layout_add_column(layout, 20, "i16_2", BCSV_TYPE_INT16);
    bcsv_layout_add_column(layout, 21, "i32_0", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 22, "i32_1", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 23, "i32_2", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 24, "i64_0", BCSV_TYPE_INT64);
    bcsv_layout_add_column(layout, 25, "i64_1", BCSV_TYPE_INT64);
    bcsv_layout_add_column(layout, 26, "i64_2", BCSV_TYPE_INT64);
    bcsv_layout_add_column(layout, 27, "f0", BCSV_TYPE_FLOAT);
    bcsv_layout_add_column(layout, 28, "f1", BCSV_TYPE_FLOAT);
    bcsv_layout_add_column(layout, 29, "f2", BCSV_TYPE_FLOAT);
    bcsv_layout_add_column(layout, 30, "d0", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 31, "d1", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 32, "d2", BCSV_TYPE_DOUBLE);

    bcsv_row_t row = bcsv_row_create(layout);

    /* bool array */
    { bool src[3] = {true, false, true}; bool dst[3] = {false};
      bcsv_row_set_bool_array(row, 0, src, 3);
      bcsv_row_get_bool_array(row, 0, dst, 3);
      TEST_ASSERT(dst[0]==true && dst[1]==false && dst[2]==true, "bool array round-trip"); }

    /* uint8 array */
    { uint8_t src[3] = {10, 20, 30}; uint8_t dst[3] = {0};
      bcsv_row_set_uint8_array(row, 3, src, 3);
      bcsv_row_get_uint8_array(row, 3, dst, 3);
      TEST_ASSERT(dst[0]==10 && dst[1]==20 && dst[2]==30, "uint8 array round-trip"); }

    /* uint16 array */
    { uint16_t src[3] = {1000, 2000, 3000}; uint16_t dst[3] = {0};
      bcsv_row_set_uint16_array(row, 6, src, 3);
      bcsv_row_get_uint16_array(row, 6, dst, 3);
      TEST_ASSERT(dst[0]==1000 && dst[1]==2000 && dst[2]==3000, "uint16 array round-trip"); }

    /* uint32 array */
    { uint32_t src[3] = {100000, 200000, 300000}; uint32_t dst[3] = {0};
      bcsv_row_set_uint32_array(row, 9, src, 3);
      bcsv_row_get_uint32_array(row, 9, dst, 3);
      TEST_ASSERT(dst[0]==100000 && dst[1]==200000 && dst[2]==300000, "uint32 array round-trip"); }

    /* uint64 array */
    { uint64_t src[3] = {1000000000ULL, 2000000000ULL, 3000000000ULL}; uint64_t dst[3] = {0};
      bcsv_row_set_uint64_array(row, 12, src, 3);
      bcsv_row_get_uint64_array(row, 12, dst, 3);
      TEST_ASSERT(dst[0]==1000000000ULL && dst[1]==2000000000ULL && dst[2]==3000000000ULL, "uint64 array round-trip"); }

    /* int8 array */
    { int8_t src[3] = {-10, 0, 10}; int8_t dst[3] = {0};
      bcsv_row_set_int8_array(row, 15, src, 3);
      bcsv_row_get_int8_array(row, 15, dst, 3);
      TEST_ASSERT(dst[0]==-10 && dst[1]==0 && dst[2]==10, "int8 array round-trip"); }

    /* int16 array */
    { int16_t src[3] = {-1000, 0, 1000}; int16_t dst[3] = {0};
      bcsv_row_set_int16_array(row, 18, src, 3);
      bcsv_row_get_int16_array(row, 18, dst, 3);
      TEST_ASSERT(dst[0]==-1000 && dst[1]==0 && dst[2]==1000, "int16 array round-trip"); }

    /* int32 array */
    { int32_t src[3] = {-100000, 0, 100000}; int32_t dst[3] = {0};
      bcsv_row_set_int32_array(row, 21, src, 3);
      bcsv_row_get_int32_array(row, 21, dst, 3);
      TEST_ASSERT(dst[0]==-100000 && dst[1]==0 && dst[2]==100000, "int32 array round-trip"); }

    /* int64 array */
    { int64_t src[3] = {-1000000000LL, 0, 1000000000LL}; int64_t dst[3] = {0};
      bcsv_row_set_int64_array(row, 24, src, 3);
      bcsv_row_get_int64_array(row, 24, dst, 3);
      TEST_ASSERT(dst[0]==-1000000000LL && dst[1]==0 && dst[2]==1000000000LL, "int64 array round-trip"); }

    /* float array (3D coordinates) */
    { float src[3] = {1.5f, 2.5f, 3.5f}; float dst[3] = {0};
      bcsv_row_set_float_array(row, 27, src, 3);
      bcsv_row_get_float_array(row, 27, dst, 3);
      TEST_ASSERT(dst[0]==1.5f && dst[1]==2.5f && dst[2]==3.5f, "float array round-trip (3D)"); }

    /* double array (3D coordinates) */
    { double src[3] = {1.1, 2.2, 3.3}; double dst[3] = {0};
      bcsv_row_set_double_array(row, 30, src, 3);
      bcsv_row_get_double_array(row, 30, dst, 3);
      TEST_ASSERT_NEAR(dst[0], 1.1, 1e-12, "double array[0] round-trip");
      TEST_ASSERT_NEAR(dst[1], 2.2, 1e-12, "double array[1] round-trip");
      TEST_ASSERT_NEAR(dst[2], 3.3, 1e-12, "double array[2] round-trip"); }

    bcsv_row_destroy(row);
    bcsv_layout_destroy(layout);
}

/* ── Row: String edge cases ────────────────────────────────────────── */
static void test_row_string_edge_cases(void) {
    TEST_START("Row String Edge Cases");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "s", BCSV_TYPE_STRING);
    bcsv_row_t row = bcsv_row_create(layout);

    /* empty string */
    bcsv_row_set_string(row, 0, "");
    TEST_ASSERT_EQ_STR(bcsv_row_get_string(row, 0), "", "empty string");

    /* string with special characters */
    bcsv_row_set_string(row, 0, "hello\nworld\ttab");
    TEST_ASSERT_EQ_STR(bcsv_row_get_string(row, 0), "hello\nworld\ttab", "string with newline/tab");

    /* string with quotes */
    bcsv_row_set_string(row, 0, "say \"hello\"");
    TEST_ASSERT_EQ_STR(bcsv_row_get_string(row, 0), "say \"hello\"", "string with quotes");

    /* long string (1000 chars) */
    {
        char long_str[1001];
        memset(long_str, 'X', 1000);
        long_str[1000] = '\0';
        bcsv_row_set_string(row, 0, long_str);
        TEST_ASSERT_EQ_STR(bcsv_row_get_string(row, 0), long_str, "1000-char string");
    }

    bcsv_row_destroy(row);
    bcsv_layout_destroy(layout);
}

/* ── Helper: create standard test layout ───────────────────────────── */
static bcsv_layout_t create_test_layout(void) {
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",    BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "value", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "name",  BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 3, "flag",  BCSV_TYPE_BOOL);
    bcsv_layout_add_column(layout, 4, "x",     BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 5, "y",     BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 6, "z",     BCSV_TYPE_DOUBLE);
    return layout;
}

static void fill_test_row(bcsv_row_t row, int i) {
    bcsv_row_set_int32 (row, 0, i);
    bcsv_row_set_double(row, 1, i * 1.5);
    char name[32]; snprintf(name, sizeof(name), "row_%d", i);
    bcsv_row_set_string(row, 2, name);
    bcsv_row_set_bool  (row, 3, (i % 2) == 0);
    bcsv_row_set_double(row, 4, i * 0.1);
    bcsv_row_set_double(row, 5, i * 0.2);
    bcsv_row_set_double(row, 6, i * 0.3);
}

static void verify_test_row(const_bcsv_row_t row, int i) {
    char msg[64];

    snprintf(msg, sizeof(msg), "  row %d: id", i);
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(row, 0), i, msg);

    snprintf(msg, sizeof(msg), "  row %d: value", i);
    TEST_ASSERT_NEAR(bcsv_row_get_double(row, 1), i*1.5, 1e-9, msg);

    snprintf(msg, sizeof(msg), "  row %d: name", i);
    char expected_name[32]; snprintf(expected_name, sizeof(expected_name), "row_%d", i);
    TEST_ASSERT_EQ_STR(bcsv_row_get_string(row, 2), expected_name, msg);

    snprintf(msg, sizeof(msg), "  row %d: flag", i);
    TEST_ASSERT(bcsv_row_get_bool(row, 3) == ((i%2)==0), msg);
}

/* ── Write & read back helper (generic for Flat/ZoH/Delta) ─────────── */
enum WriterType { WT_FLAT, WT_ZOH, WT_DELTA };

static void test_writer_reader_roundtrip(enum WriterType wt, const char* label, int num_rows) {
    char section[64];
    snprintf(section, sizeof(section), "%s Writer/Reader Round-Trip (%d rows)", label, num_rows);
    TEST_START(section);

    bcsv_layout_t layout = create_test_layout();

    /* Create writer */
    bcsv_writer_t writer = NULL;
    switch (wt) {
        case WT_FLAT:  writer = bcsv_writer_create(layout);       break;
        case WT_ZOH:   writer = bcsv_writer_create_zoh(layout);   break;
        case WT_DELTA: writer = bcsv_writer_create_delta(layout);  break;
    }
    TEST_ASSERT(writer != NULL, "writer created");

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%stest_%s_%d.bcsv", TMP_DIR, label, num_rows);

    bcsv_file_flags_t flags = BCSV_FLAG_NONE;
    bool ok = bcsv_writer_open(writer, filepath, true, 1, 64, flags);
    TEST_ASSERT(ok, "writer opened");
    TEST_ASSERT(bcsv_writer_is_open(writer), "writer is_open");

    bcsv_row_t row = bcsv_writer_row(writer);
    TEST_ASSERT(row != NULL, "writer row accessible");

    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(row, i);
        ok = bcsv_writer_next(writer);
        TEST_ASSERT(ok, "write row");
    }

    TEST_ASSERT_EQ_INT((int)bcsv_writer_index(writer), num_rows, "writer index matches");
    bcsv_writer_close(writer);

    /* Read back */
    bcsv_reader_t reader = bcsv_reader_create();
    ok = bcsv_reader_open(reader, filepath);
    TEST_ASSERT(ok, "reader opened");
    TEST_ASSERT(bcsv_reader_is_open(reader), "reader is_open");

    /* Check layout */
    const_bcsv_layout_t rlayout = bcsv_reader_layout(reader);
    TEST_ASSERT_EQ_INT((int)bcsv_layout_column_count(rlayout), 7, "reader layout has 7 columns");

    /* Sequential read */
    int count = 0;
    while (bcsv_reader_next(reader)) {
        const_bcsv_row_t rrow = bcsv_reader_row(reader);
        verify_test_row(rrow, count);

        /* Verify vectorized 3D coordinate read */
        if (count < 3) { /* only first few to keep output manageable */
            double xyz[3] = {0};
            bcsv_row_get_double_array(rrow, 4, xyz, 3);
            char vmsg[64];
            snprintf(vmsg, sizeof(vmsg), "  row %d: xyz vector", count);
            TEST_ASSERT_NEAR(xyz[0], count*0.1, 1e-9, vmsg);
        }
        count++;
    }
    TEST_ASSERT_EQ_INT(count, num_rows, "read all rows");

    /* Row count from footer */
    size_t rc = bcsv_reader_count_rows(reader);
    TEST_ASSERT_EQ_INT((int)rc, num_rows, "count_rows matches");

    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}

/* ── Random access read ────────────────────────────────────────────── */
static void test_random_access(void) {
    TEST_START("Random Access Read");

    int num_rows = 100;
    bcsv_layout_t layout = create_test_layout();
    bcsv_writer_t writer = bcsv_writer_create(layout);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_random_access.bcsv");

    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    bcsv_row_t row = bcsv_writer_row(writer);
    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(row, i);
        bcsv_writer_next(writer);
    }
    bcsv_writer_close(writer);

    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);

    /* Read specific indices */
    bool ok = bcsv_reader_read(reader, 0);
    TEST_ASSERT(ok, "read index 0");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row(reader), 0), 0, "row 0 id=0");

    ok = bcsv_reader_read(reader, 50);
    TEST_ASSERT(ok, "read index 50");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row(reader), 0), 50, "row 50 id=50");

    ok = bcsv_reader_read(reader, 99);
    TEST_ASSERT(ok, "read index 99");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row(reader), 0), 99, "row 99 id=99");

    /* Read backwards */
    ok = bcsv_reader_read(reader, 10);
    TEST_ASSERT(ok, "read index 10 (backwards)");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row(reader), 0), 10, "row 10 id=10");

    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}

/* ── Writer write (external row) ───────────────────────────────────── */
static void test_writer_write_external_row(void) {
    TEST_START("Writer write(external row)");

    bcsv_layout_t layout = create_test_layout();
    bcsv_writer_t writer = bcsv_writer_create(layout);
    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_write_ext.bcsv");
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);

    /* Create an external row and write it */
    bcsv_row_t ext_row = bcsv_row_create(layout);
    fill_test_row(ext_row, 42);
    bool ok = bcsv_writer_write(writer, ext_row);
    TEST_ASSERT(ok, "writer_write(external row) succeeds");
    TEST_ASSERT_EQ_INT((int)bcsv_writer_index(writer), 1, "writer index is 1 after write");

    bcsv_writer_close(writer);

    /* Read back */
    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);
    TEST_ASSERT(bcsv_reader_next(reader), "read external row");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row(reader), 0), 42, "external row id=42");
    bcsv_reader_close(reader);

    bcsv_reader_destroy(reader);
    bcsv_row_destroy(ext_row);
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}

/* ── Reader/Writer error_msg and compression_level ─────────────────── */
static void test_error_msg_and_compression(void) {
    TEST_START("Error Msg & Compression Level");

    bcsv_layout_t layout = create_test_layout();
    bcsv_writer_t writer = bcsv_writer_create(layout);

    /* Before open, error_msg should be empty */
    const char* emsg = bcsv_writer_error_msg(writer);
    TEST_ASSERT(emsg != NULL, "writer error_msg not NULL before open");

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_errmsg.bcsv");
    bcsv_writer_open(writer, filepath, true, 3, 64, BCSV_FLAG_NONE);

    uint8_t wlevel = bcsv_writer_compression_level(writer);
    TEST_ASSERT(wlevel > 0, "writer compression_level > 0");

    fill_test_row(bcsv_writer_row(writer), 1);
    bcsv_writer_next(writer);
    bcsv_writer_close(writer);

    /* Reader */
    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);

    uint8_t rlevel = bcsv_reader_compression_level(reader);
    TEST_ASSERT(rlevel > 0, "reader compression_level > 0");

    const char* rmsg = bcsv_reader_error_msg(reader);
    TEST_ASSERT(rmsg != NULL, "reader error_msg not NULL");

    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}

/* ── Reader open_ex (with rebuild_footer) ──────────────────────────── */
static void test_reader_open_ex(void) {
    TEST_START("Reader open_ex");

    bcsv_layout_t layout = create_test_layout();
    bcsv_writer_t writer = bcsv_writer_create(layout);
    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_open_ex.bcsv");
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    fill_test_row(bcsv_writer_row(writer), 1);
    bcsv_writer_next(writer);
    bcsv_writer_close(writer);

    bcsv_reader_t reader = bcsv_reader_create();
    bool ok = bcsv_reader_open_ex(reader, filepath, false);
    TEST_ASSERT(ok, "open_ex with rebuild_footer=false");
    TEST_ASSERT_EQ_INT((int)bcsv_reader_count_rows(reader), 1, "row count = 1");
    bcsv_reader_close(reader);

    /* With rebuild */
    ok = bcsv_reader_open_ex(reader, filepath, true);
    TEST_ASSERT(ok, "open_ex with rebuild_footer=true");
    bcsv_reader_close(reader);

    bcsv_reader_destroy(reader);
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}

/* ── CSV Writer/Reader Round-Trip ──────────────────────────────────── */
static void test_csv_roundtrip(void) {
    TEST_START("CSV Writer/Reader Round-Trip");

    int num_rows = 50;
    bcsv_layout_t layout = create_test_layout();

    /* Write CSV */
    bcsv_csv_writer_t cw = bcsv_csv_writer_create(layout, ',', '.');
    TEST_ASSERT(cw != NULL, "csv_writer created");

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv.csv");

    bool ok = bcsv_csv_writer_open(cw, filepath, true, true);
    TEST_ASSERT(ok, "csv_writer opened");
    TEST_ASSERT(bcsv_csv_writer_is_open(cw), "csv_writer is_open");

    bcsv_row_t row = bcsv_csv_writer_row(cw);
    TEST_ASSERT(row != NULL, "csv_writer row accessible");

    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(row, i);
        ok = bcsv_csv_writer_next(cw);
        TEST_ASSERT(ok, "csv write row");
    }
    TEST_ASSERT_EQ_INT((int)bcsv_csv_writer_index(cw), num_rows, "csv_writer index");
    bcsv_csv_writer_close(cw);

    /* Read CSV */
    bcsv_csv_reader_t cr = bcsv_csv_reader_create(layout, ',', '.');
    TEST_ASSERT(cr != NULL, "csv_reader created");

    ok = bcsv_csv_reader_open(cr, filepath, true);
    TEST_ASSERT(ok, "csv_reader opened");
    TEST_ASSERT(bcsv_csv_reader_is_open(cr), "csv_reader is_open");

    int count = 0;
    while (bcsv_csv_reader_next(cr)) {
        const_bcsv_row_t rrow = bcsv_csv_reader_row(cr);
        /* Verify id */
        TEST_ASSERT_EQ_INT(bcsv_row_get_int32(rrow, 0), count, "csv row id");
        count++;
    }
    TEST_ASSERT_EQ_INT(count, num_rows, "csv read all rows");
    TEST_ASSERT(bcsv_csv_reader_file_line(cr) > 0, "csv file_line > 0");

    const char* cerr = bcsv_csv_reader_error_msg(cr);
    TEST_ASSERT(cerr != NULL, "csv_reader error_msg not NULL");

    bcsv_csv_reader_close(cr);
    bcsv_csv_reader_destroy(cr);
    bcsv_csv_writer_destroy(cw);
    bcsv_layout_destroy(layout);
}

/* ── CSV with custom delimiter ─────────────────────────────────────── */
static void test_csv_delimiter(void) {
    TEST_START("CSV with Semicolon Delimiter");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",  BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "val", BCSV_TYPE_DOUBLE);

    bcsv_csv_writer_t cw = bcsv_csv_writer_create(layout, ';', ',');
    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv_delim.csv");
    bcsv_csv_writer_open(cw, filepath, true, true);

    bcsv_row_t row = bcsv_csv_writer_row(cw);
    bcsv_row_set_int32(row, 0, 1);
    bcsv_row_set_double(row, 1, 3.14);
    bcsv_csv_writer_next(cw);
    bcsv_csv_writer_close(cw);

    /* Read back with same delimiters */
    bcsv_csv_reader_t cr = bcsv_csv_reader_create(layout, ';', ',');
    bool ok = bcsv_csv_reader_open(cr, filepath, true);
    TEST_ASSERT(ok, "csv_reader opened with semicolon");
    TEST_ASSERT(bcsv_csv_reader_next(cr), "read semicolon csv row");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_csv_reader_row(cr), 0), 1, "semicolon csv id=1");
    TEST_ASSERT_NEAR(bcsv_row_get_double(bcsv_csv_reader_row(cr), 1), 3.14, 1e-2, "semicolon csv val");

    bcsv_csv_reader_close(cr);
    bcsv_csv_reader_destroy(cr);
    bcsv_csv_writer_destroy(cw);
    bcsv_layout_destroy(layout);
}

/* ── CSV write external row ────────────────────────────────────────── */
static void test_csv_write_external_row(void) {
    TEST_START("CSV Writer write(external row)");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id", BCSV_TYPE_INT32);

    bcsv_csv_writer_t cw = bcsv_csv_writer_create(layout, ',', '.');
    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv_ext.csv");
    bcsv_csv_writer_open(cw, filepath, true, true);

    bcsv_row_t ext_row = bcsv_row_create(layout);
    bcsv_row_set_int32(ext_row, 0, 99);
    bool ok = bcsv_csv_writer_write(cw, ext_row);
    TEST_ASSERT(ok, "csv_writer_write(external) succeeds");
    bcsv_csv_writer_close(cw);

    bcsv_csv_reader_t cr = bcsv_csv_reader_create(layout, ',', '.');
    bcsv_csv_reader_open(cr, filepath, true);
    TEST_ASSERT(bcsv_csv_reader_next(cr), "csv read external row");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_csv_reader_row(cr), 0), 99, "csv ext id=99");
    bcsv_csv_reader_close(cr);

    bcsv_csv_reader_destroy(cr);
    bcsv_row_destroy(ext_row);
    bcsv_csv_writer_destroy(cw);
    bcsv_layout_destroy(layout);
}

/* ── Error handling: NULL handles ──────────────────────────────────── */
static void test_null_handles(void) {
    TEST_START("NULL Handle Safety");

    /* All of these should not crash */
    bcsv_layout_destroy(NULL);
    bcsv_reader_destroy(NULL);
    bcsv_writer_destroy(NULL);
    bcsv_row_destroy(NULL);
    TEST_ASSERT(true, "destroy(NULL) doesn't crash");

    TEST_ASSERT(bcsv_layout_clone(NULL) == NULL, "clone(NULL) returns NULL");
    TEST_ASSERT(bcsv_layout_column_count(NULL) == 0, "column_count(NULL) returns 0");
    TEST_ASSERT(bcsv_row_create(NULL) == NULL, "row_create(NULL) returns NULL");
    TEST_ASSERT(bcsv_row_clone(NULL) == NULL, "row_clone(NULL) returns NULL");

    TEST_ASSERT(bcsv_reader_open(NULL, "x") == false, "reader_open(NULL) returns false");
    TEST_ASSERT(bcsv_reader_is_open(NULL) == false, "reader_is_open(NULL) returns false");
    TEST_ASSERT(bcsv_reader_next(NULL) == false, "reader_next(NULL) returns false");
    TEST_ASSERT(bcsv_reader_read(NULL, 0) == false, "reader_read(NULL) returns false");

    TEST_ASSERT(bcsv_writer_open(NULL, "x", false, 1, 64, BCSV_FLAG_NONE) == false, "writer_open(NULL) returns false");
    TEST_ASSERT(bcsv_writer_next(NULL) == false, "writer_next(NULL) returns false");
    TEST_ASSERT(bcsv_writer_write(NULL, NULL) == false, "writer_write(NULL) returns false");

    /* bcsv_last_error should have something after NULL handle calls */
    const char* err = bcsv_last_error();
    TEST_ASSERT(err != NULL && strlen(err) > 0, "bcsv_last_error reports NULL handle");

    /* bcsv_clear_last_error should reset the error state */
    bcsv_clear_last_error();
    err = bcsv_last_error();
    TEST_ASSERT(err != NULL && strlen(err) == 0, "bcsv_clear_last_error resets to empty");
}

/* ── Cross-format: BCSV → CSV → BCSV ──────────────────────────────── */
static void test_cross_format(void) {
    TEST_START("Cross-Format BCSV → CSV → BCSV");

    int num_rows = 20;
    bcsv_layout_t layout = create_test_layout();

    /* Step 1: Write BCSV */
    char bcsv_path[256], csv_path[256], bcsv2_path[256];
    make_path(bcsv_path, sizeof(bcsv_path), "cross_original.bcsv");
    make_path(csv_path, sizeof(csv_path), "cross_intermediate.csv");
    make_path(bcsv2_path, sizeof(bcsv2_path), "cross_converted.bcsv");

    bcsv_writer_t bw = bcsv_writer_create_zoh(layout);
    bcsv_writer_open(bw, bcsv_path, true, 1, 64, BCSV_FLAG_NONE);
    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(bcsv_writer_row(bw), i);
        bcsv_writer_next(bw);
    }
    bcsv_writer_close(bw);

    /* Step 2: Read BCSV → Write CSV */
    bcsv_reader_t br = bcsv_reader_create();
    bcsv_reader_open(br, bcsv_path);

    bcsv_csv_writer_t cw = bcsv_csv_writer_create(layout, ',', '.');
    bcsv_csv_writer_open(cw, csv_path, true, true);
    while (bcsv_reader_next(br)) {
        bcsv_csv_writer_write(cw, bcsv_reader_row(br));
    }
    bcsv_csv_writer_close(cw);
    bcsv_reader_close(br);

    /* Step 3: Read CSV → Write BCSV (Delta) */
    bcsv_csv_reader_t cr = bcsv_csv_reader_create(layout, ',', '.');
    bcsv_csv_reader_open(cr, csv_path, true);

    bcsv_writer_t bw2 = bcsv_writer_create_delta(layout);
    bcsv_writer_open(bw2, bcsv2_path, true, 1, 64, BCSV_FLAG_NONE);
    while (bcsv_csv_reader_next(cr)) {
        bcsv_writer_write(bw2, bcsv_csv_reader_row(cr));
    }
    bcsv_writer_close(bw2);
    bcsv_csv_reader_close(cr);

    /* Step 4: Read converted BCSV and verify */
    bcsv_reader_t br2 = bcsv_reader_create();
    bcsv_reader_open(br2, bcsv2_path);
    int count = 0;
    while (bcsv_reader_next(br2)) {
        verify_test_row(bcsv_reader_row(br2), count);
        count++;
    }
    TEST_ASSERT_EQ_INT(count, num_rows, "cross-format: all rows preserved");
    bcsv_reader_close(br2);

    bcsv_reader_destroy(br);
    bcsv_reader_destroy(br2);
    bcsv_csv_reader_destroy(cr);
    bcsv_csv_writer_destroy(cw);
    bcsv_writer_destroy(bw);
    bcsv_writer_destroy(bw2);
    bcsv_layout_destroy(layout);
}

/* ── Multi-packet file (10K rows) ──────────────────────────────────── */
static void test_multi_packet(void) {
    TEST_START("Multi-Packet File (10K rows)");

    int num_rows = 10000;
    bcsv_layout_t layout = create_test_layout();
    bcsv_writer_t writer = bcsv_writer_create_zoh(layout);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_10k.bcsv");

    /* Small block size to force multiple packets */
    bcsv_writer_open(writer, filepath, true, 1, 4, BCSV_FLAG_NONE);
    bcsv_row_t row = bcsv_writer_row(writer);
    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(row, i);
        bcsv_writer_next(writer);
    }
    bcsv_writer_close(writer);

    /* Read and verify */
    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);

    size_t rc = bcsv_reader_count_rows(reader);
    TEST_ASSERT_EQ_INT((int)rc, num_rows, "10K count_rows");

    int count = 0;
    while (bcsv_reader_next(reader)) {
        if (count == 0 || count == 5000 || count == 9999) {
            verify_test_row(bcsv_reader_row(reader), count);
        }
        count++;
    }
    TEST_ASSERT_EQ_INT(count, num_rows, "read all 10K rows");

    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_writer_destroy(writer);
    bcsv_layout_destroy(layout);
}

/* ── File flags ────────────────────────────────────────────────────── */
static void test_file_flags(void) {
    TEST_START("File Flags Constants");

    TEST_ASSERT_EQ_INT(BCSV_FLAG_NONE,           0,    "FLAG_NONE = 0");
    TEST_ASSERT_EQ_INT(BCSV_FLAG_ZOH,            1,    "FLAG_ZOH = 1");
    TEST_ASSERT_EQ_INT(BCSV_FLAG_NO_FILE_INDEX,  2,    "FLAG_NO_FILE_INDEX = 2");
    TEST_ASSERT_EQ_INT(BCSV_FLAG_STREAM_MODE,    4,    "FLAG_STREAM_MODE = 4");
    TEST_ASSERT_EQ_INT(BCSV_FLAG_BATCH_COMPRESS, 8,    "FLAG_BATCH_COMPRESS = 8");
    TEST_ASSERT_EQ_INT(BCSV_FLAG_DELTA_ENCODING, 16,   "FLAG_DELTA_ENCODING = 16");

    /* bitwise combination */
    bcsv_file_flags_t combined = (bcsv_file_flags_t)(BCSV_FLAG_ZOH | BCSV_FLAG_BATCH_COMPRESS);
    TEST_ASSERT_EQ_INT(combined, 9, "ZOH|BATCH_COMPRESS = 9");
}

/* ══════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("BCSV C API Full Test Suite (Item 15)\n");
    printf("=====================================\n");

    ensure_tmp_dir();

    /* Version */
    test_version_api();

    /* Layout extended */
    test_layout_extended();

    /* Row: all types */
    test_row_all_types();
    test_row_vectorized_all_types();
    test_row_string_edge_cases();

    /* File flags */
    test_file_flags();

    /* Flat writer/reader */
    test_writer_reader_roundtrip(WT_FLAT, "Flat", 20);

    /* ZoH writer/reader */
    test_writer_reader_roundtrip(WT_ZOH, "ZoH", 20);

    /* Delta writer/reader */
    test_writer_reader_roundtrip(WT_DELTA, "Delta", 20);

    /* Random access */
    test_random_access();

    /* Writer write(external row) */
    test_writer_write_external_row();

    /* Error msg and compression */
    test_error_msg_and_compression();

    /* Reader open_ex */
    test_reader_open_ex();

    /* CSV */
    test_csv_roundtrip();
    test_csv_delimiter();
    test_csv_write_external_row();

    /* Cross-format */
    test_cross_format();

    /* Multi-packet */
    test_multi_packet();

    /* NULL safety */
    test_null_handles();

    /* Summary */
    printf("\n=====================================\n");
    printf("Test Results: %d/%d tests passed\n", tests_passed, tests_run);
    if (tests_passed == tests_run) {
        printf("🎉 All C API tests passed!\n");
        return 0;
    } else {
        printf("❌ %d tests FAILED\n", tests_run - tests_passed);
        return 1;
    }
}
