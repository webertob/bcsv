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

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS   /* suppress C4996 for strncpy etc. */
#pragma warning(disable: 4127)  /* conditional expression is constant (do-while(0) macros) */
#endif

#include "../include/bcsv/bcsv_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <wchar.h>
#include <direct.h>  /* _mkdir */
#endif

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

static const char* TMP_DIR =
#ifdef _WIN32
    "tmp\\bcsv_c_api_test\\";
#else
    "/tmp/bcsv_c_api_test/";
#endif

static void ensure_tmp_dir(void) {
    char cmd[256];
#ifdef _WIN32
    _mkdir("tmp");
    _mkdir("tmp\\bcsv_c_api_test");
    (void)cmd;  /* unused on Windows */
#else
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", TMP_DIR);
    if (system(cmd) != 0) {
        fprintf(stderr, "Warning: failed to create tmp dir: %s\n", TMP_DIR);
    }
#endif
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
    TEST_START("CSV Writer/Reader Round-Trip (_u)");

    int num_rows = 50;
    bcsv_layout_t layout = create_test_layout();

    /* Write CSV through the unified writer */
    bcsv_writer_t cw = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(cw != NULL, "csv writer (_u) created");

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv.csv");

    bcsv_open_opts wo = BCSV_OPEN_OPTS_INIT;
    bool ok = bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_AUTO, &wo);
    TEST_ASSERT(ok, "csv writer (_u) opened (.csv -> CSV by extension)");
    TEST_ASSERT_EQ_INT(bcsv_writer_format_u(cw), BCSV_FORMAT_CSV, "writer AUTO resolved CSV");
    TEST_ASSERT(bcsv_writer_is_open_u(cw), "csv writer (_u) is_open");

    bcsv_row_t row = bcsv_writer_row_u(cw);
    TEST_ASSERT(row != NULL, "csv writer (_u) row accessible");

    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(row, i);
        ok = bcsv_writer_next_u(cw);
        TEST_ASSERT(ok, "csv write row");
    }
    TEST_ASSERT_EQ_INT((int)bcsv_writer_index_u(cw), num_rows, "writer index");
    bcsv_writer_close_u(cw);

    /* Read CSV through the unified reader (explicit CSV pin) */
    bcsv_reader_t cr = bcsv_reader_create_u();
    TEST_ASSERT(cr != NULL, "csv reader (_u) created");

    bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
    ro.csv_layout = layout;
    ok = bcsv_reader_open_u(cr, filepath, BCSV_FORMAT_CSV, &ro);
    TEST_ASSERT(ok, "csv reader (_u) opened");
    TEST_ASSERT_EQ_INT(bcsv_reader_format_u(cr), BCSV_FORMAT_CSV, "reader format CSV");
    TEST_ASSERT(bcsv_reader_is_open_u(cr), "csv reader (_u) is_open");

    int count = 0;
    while (bcsv_reader_next_u(cr)) {
        const_bcsv_row_t rrow = bcsv_reader_row_u(cr);
        /* Verify id */
        TEST_ASSERT_EQ_INT(bcsv_row_get_int32(rrow, 0), count, "csv row id");
        count++;
    }
    TEST_ASSERT_EQ_INT(count, num_rows, "csv read all rows");
    TEST_ASSERT(bcsv_reader_file_line_u(cr) > 0, "csv file_line > 0");
    TEST_ASSERT_EQ_INT((int)bcsv_reader_index_u(cr), num_rows, "csv reader index");

    const char* cerr = bcsv_reader_error_msg_u(cr);
    TEST_ASSERT(cerr != NULL, "csv reader (_u) error_msg not NULL");

    bcsv_reader_close_u(cr);
    bcsv_reader_destroy_u(cr);
    bcsv_writer_destroy_u(cw);
    bcsv_layout_destroy(layout);
}

/* ── Compressed CSV (.csv.lz4, standard LZ4 frame) — unified _u ────── */
static void test_csv_lz4_roundtrip(void) {
    TEST_START("CSV LZ4 (.csv.lz4) Round-Trip (_u)");

    int num_rows = 2000;   /* enough to span many 64KB LZ4 blocks */
    bcsv_layout_t layout = create_test_layout();

    bcsv_writer_t cw = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(cw != NULL, "csv lz4 writer (_u) created");

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv_lz4.csv.lz4");

    /* LZ4 tuning travels in bcsv_open_opts (the retired set_lz4 setter is
       gone). Invalid opts are rejected at open with a naming reason. */
    bcsv_open_opts badopts = BCSV_OPEN_OPTS_INIT;
    badopts.lz4_block_id = 99;
    TEST_ASSERT(!bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_CSV_LZ4, &badopts),
                "open rejects invalid lz4_block_id");
    TEST_ASSERT(strstr(bcsv_writer_error_msg_u(cw), "lz4_block_id") != NULL,
                "rejection names the block size field");
    badopts.lz4_block_id = -1;
    badopts.lz4_level = 13;
    TEST_ASSERT(!bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_CSV_LZ4, &badopts),
                "open rejects invalid lz4_level");
    TEST_ASSERT(strstr(bcsv_writer_error_msg_u(cw), "lz4_level") != NULL,
                "rejection names the level field");

    bcsv_open_opts wo = BCSV_OPEN_OPTS_INIT;
    wo.lz4_block_id = 5;   /* 256KB */
    wo.lz4_level = 0;
    bool ok = bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_AUTO, &wo);
    TEST_ASSERT(ok, "csv lz4 writer opened (.csv.lz4 -> CSV_LZ4 by extension)");
    TEST_ASSERT_EQ_INT(bcsv_writer_format_u(cw), BCSV_FORMAT_CSV_LZ4, "writer format CSV_LZ4");

    bcsv_row_t row = bcsv_writer_row_u(cw);
    for (int i = 0; i < num_rows; ++i) {
        fill_test_row(row, i);
        ok = bcsv_writer_next_u(cw);
        TEST_ASSERT(ok, "csv lz4 write row");
    }
    TEST_ASSERT_EQ_INT((int)bcsv_writer_index_u(cw), num_rows, "writer index");
    bcsv_writer_close_u(cw);

    /* The file must carry the LZ4 frame magic (little-endian 0x184D2204):
       this is what makes it a valid lz4 file for the official CLI. */
    {
        FILE* f = fopen(filepath, "rb");
        TEST_ASSERT(f != NULL, "lz4 file exists");
        unsigned char magic[4] = {0, 0, 0, 0};
        size_t got = fread(magic, 1, 4, f);
        fclose(f);
        TEST_ASSERT(got == 4, "read 4 magic bytes");
        TEST_ASSERT(magic[0] == 0x04 && magic[1] == 0x22 &&
                    magic[2] == 0x4D && magic[3] == 0x18, "LZ4 frame magic present");
    }

    /* Reader auto-detects LZ4 by magic — no reader-side option exists. */
    bcsv_reader_t cr = bcsv_reader_create_u();
    bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
    ro.csv_layout = layout;
    ok = bcsv_reader_open_u(cr, filepath, BCSV_FORMAT_AUTO, &ro);
    TEST_ASSERT(ok, "csv reader (_u) opened .csv.lz4 (AUTO)");
    TEST_ASSERT_EQ_INT(bcsv_reader_format_u(cr), BCSV_FORMAT_CSV_LZ4, "AUTO resolved CSV_LZ4");

    int count = 0;
    while (bcsv_reader_next_u(cr)) {
        const_bcsv_row_t rrow = bcsv_reader_row_u(cr);
        TEST_ASSERT_EQ_INT(bcsv_row_get_int32(rrow, 0), count, "csv lz4 row id");
        count++;
    }
    TEST_ASSERT_EQ_INT(count, num_rows, "csv lz4 read all rows");
    bcsv_reader_destroy_u(cr);

    /* M4: reopening the SAME handle for a plain .csv rebuilds the member as
       plain CSV — LZ4 travels per-open in opts, never in the handle. */
    {
        char p2[256];
        make_path(p2, sizeof(p2), "test_csv_lz4_then_plain.csv");
        ok = bcsv_writer_open_u(cw, p2, true, BCSV_FORMAT_AUTO, &wo);
        TEST_ASSERT(ok, "same handle reopened for plain .csv");
        TEST_ASSERT_EQ_INT(bcsv_writer_format_u(cw), BCSV_FORMAT_CSV, "plain reopen -> CSV");
        bcsv_row_t r2 = bcsv_writer_row_u(cw);
        fill_test_row(r2, 0);
        TEST_ASSERT(bcsv_writer_next_u(cw), "write row on rebuilt plain member");
        bcsv_writer_close_u(cw);
        FILE* f = fopen(p2, "rb");
        TEST_ASSERT(f != NULL, "plain file exists");
        unsigned char magic[4] = {0, 0, 0, 0};
        size_t got2 = fread(magic, 1, 4, f);
        fclose(f);
        TEST_ASSERT(!(got2 == 4 && magic[0] == 0x04 && magic[1] == 0x22 &&
                      magic[2] == 0x4D && magic[3] == 0x18),
                    "plain file carries no LZ4 magic");
    }

    bcsv_writer_destroy_u(cw);
    bcsv_layout_destroy(layout);
}

/* ── CSV with custom delimiter (_u: ctor-frozen params → opts) ────── */
static void test_csv_delimiter(void) {
    TEST_START("CSV with Semicolon Delimiter (_u)");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",  BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "val", BCSV_TYPE_DOUBLE);

    bcsv_writer_t cw = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv_delim.csv");

    bcsv_open_opts wo = BCSV_OPEN_OPTS_INIT;
    wo.delimiter = ';';
    wo.decimal_sep = ',';
    TEST_ASSERT(bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_AUTO, &wo),
                "csv writer (_u) opened with ';' delimiter");

    bcsv_row_t row = bcsv_writer_row_u(cw);
    bcsv_row_set_int32(row, 0, 1);
    bcsv_row_set_double(row, 1, 3.14);
    TEST_ASSERT(bcsv_writer_next_u(cw), "write semicolon row");
    bcsv_writer_close_u(cw);

    /* Read back with the same delimiters (opts, not ctor) */
    bcsv_reader_t cr = bcsv_reader_create_u();
    bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
    ro.csv_layout = layout;
    ro.delimiter = ';';
    ro.decimal_sep = ',';
    bool ok = bcsv_reader_open_u(cr, filepath, BCSV_FORMAT_AUTO, &ro);
    TEST_ASSERT(ok, "csv reader (_u) opened with semicolon");
    TEST_ASSERT(bcsv_reader_next_u(cr), "read semicolon csv row");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row_u(cr), 0), 1, "semicolon csv id=1");
    TEST_ASSERT_NEAR(bcsv_row_get_double(bcsv_reader_row_u(cr), 1), 3.14, 1e-2, "semicolon csv val");

    /* M4: reuse predicate must include delimiter/decimal_sep — reopening the
       SAME reader with ';' then ',' picks up the new parsing chars (member
       rebuilt), and ','-delimited content reads correctly afterwards. */
    {
        char p2[256];
        make_path(p2, sizeof(p2), "test_csv_delim_reuse.csv");
        bcsv_writer_t cw2 = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
        TEST_ASSERT(bcsv_writer_open_u(cw2, p2, true, BCSV_FORMAT_AUTO, NULL),
                    "comma writer opened (NULL opts = defaults)");
        bcsv_row_t r2 = bcsv_writer_row_u(cw2);
        bcsv_row_set_int32(r2, 0, 42);
        bcsv_row_set_double(r2, 1, 2.5);
        TEST_ASSERT(bcsv_writer_next_u(cw2), "write comma row");
        bcsv_writer_close_u(cw2);
        bcsv_writer_destroy_u(cw2);

        bcsv_open_opts ro2 = BCSV_OPEN_OPTS_INIT;
        ro2.csv_layout = layout;
        ro2.delimiter = ';';
        /* ';' parsing of a ','-separated file: 1 giant field per line != 2
           columns → the open must fail loudly (not silently misparse). */
        TEST_ASSERT(!bcsv_reader_open_u(cr, p2, BCSV_FORMAT_AUTO, &ro2),
                    "; open on comma file fails loudly");
        ro2.delimiter = ',';   /* changed → member must be rebuilt */
        TEST_ASSERT(bcsv_reader_open_u(cr, p2, BCSV_FORMAT_AUTO, &ro2), "re-open with ','");
        TEST_ASSERT(bcsv_reader_next_u(cr), "read comma row after reuse");
        TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row_u(cr), 0), 42, "comma row id=42");
        TEST_ASSERT_NEAR(bcsv_row_get_double(bcsv_reader_row_u(cr), 1), 2.5, 1e-9, "comma row val");
    }

    bcsv_reader_destroy_u(cr);
    bcsv_writer_destroy_u(cw);
    bcsv_layout_destroy(layout);
}

/* ── CSV Accessor functions via the unified handle (_u) ───────────── */
static void test_csv_accessors(void) {
    TEST_START("CSV Accessor Functions via _u (filename, layout, index, error_msg)");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",  BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "val", BCSV_TYPE_DOUBLE);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv_accessors.csv");

    /* Unified writer opened as CSV: accessors report through the CSV member */
    bcsv_writer_t cw = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_AUTO, NULL),
                "csv writer (_u) opened (NULL opts = defaults)");

    /* writer_filename_u */
#ifdef _WIN32
    const wchar_t* wfn = bcsv_writer_filename_u(cw);
    TEST_ASSERT(wfn != NULL && wcslen(wfn) > 0, "writer_filename_u non-empty");
    TEST_ASSERT(wcsstr(wfn, L"test_csv_accessors.csv") != NULL, "writer_filename_u contains expected name");
#else
    const char* wfn = bcsv_writer_filename_u(cw);
    TEST_ASSERT(wfn != NULL && strlen(wfn) > 0, "writer_filename_u non-empty");
    TEST_ASSERT(strstr(wfn, "test_csv_accessors.csv") != NULL, "writer_filename_u contains expected name");
#endif

    /* writer_layout_u */
    const_bcsv_layout_t wlayout = bcsv_writer_layout_u(cw);
    TEST_ASSERT(wlayout != NULL, "writer_layout_u non-NULL");
    TEST_ASSERT_EQ_INT((int)bcsv_layout_column_count(wlayout), 2, "writer_layout_u has 2 columns");

    /* writer_error_msg_u (non-NULL on no error) */
    const char* werr = bcsv_writer_error_msg_u(cw);
    TEST_ASSERT(werr != NULL, "writer_error_msg_u non-NULL");

    /* Write some rows */
    bcsv_row_t row = bcsv_writer_row_u(cw);
    for (int i = 0; i < 5; ++i) {
        bcsv_row_set_int32(row, 0, i);
        bcsv_row_set_double(row, 1, i * 1.5);
        TEST_ASSERT(bcsv_writer_next_u(cw), "writer_next_u csv row");
    }
    bcsv_writer_close_u(cw);

    /* Unified reader opened as CSV: accessors report through the CSV member */
    bcsv_reader_t cr = bcsv_reader_create_u();
    bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
    ro.csv_layout = layout;
    TEST_ASSERT(bcsv_reader_open_u(cr, filepath, BCSV_FORMAT_AUTO, &ro), "reader (_u) opened csv");

    /* reader_filename_u */
#ifdef _WIN32
    const wchar_t* rfn = bcsv_reader_filename_u(cr);
    TEST_ASSERT(rfn != NULL && wcslen(rfn) > 0, "reader_filename_u non-empty");
    TEST_ASSERT(wcsstr(rfn, L"test_csv_accessors.csv") != NULL, "reader_filename_u contains expected name");
#else
    const char* rfn = bcsv_reader_filename_u(cr);
    TEST_ASSERT(rfn != NULL && strlen(rfn) > 0, "reader_filename_u non-empty");
    TEST_ASSERT(strstr(rfn, "test_csv_accessors.csv") != NULL, "reader_filename_u contains expected name");
#endif

    /* reader_layout_u */
    const_bcsv_layout_t rlayout = bcsv_reader_layout_u(cr);
    TEST_ASSERT(rlayout != NULL, "reader_layout_u non-NULL");
    TEST_ASSERT_EQ_INT((int)bcsv_layout_column_count(rlayout), 2, "reader_layout_u has 2 columns");

    /* reader_index_u (before read should be 0) */
    TEST_ASSERT_EQ_INT((int)bcsv_reader_index_u(cr), 0, "reader_index_u starts at 0");

    /* Read rows and check index progression */
    TEST_ASSERT(bcsv_reader_next_u(cr), "reader read first row");
    TEST_ASSERT_EQ_INT((int)bcsv_reader_index_u(cr), 1, "reader_index_u is 1 after first read");

    TEST_ASSERT(bcsv_reader_next_u(cr), "reader read second row");
    TEST_ASSERT_EQ_INT((int)bcsv_reader_index_u(cr), 2, "reader_index_u is 2 after second read");

    /* Read remaining rows */
    while (bcsv_reader_next_u(cr)) { /* consume */ }
    TEST_ASSERT_EQ_INT((int)bcsv_reader_index_u(cr), 5, "reader_index_u is 5 after all reads");

    bcsv_reader_close_u(cr);
    bcsv_reader_destroy_u(cr);
    bcsv_writer_destroy_u(cw);
    bcsv_layout_destroy(layout);
}

/* ── CSV write external row (unified _u) ───────────────────────── */
static void test_csv_write_external_row(void) {
    TEST_START("CSV Writer write_u(external row)");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "val", BCSV_TYPE_DOUBLE);

    bcsv_writer_t cw = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_csv_ext.csv");
    TEST_ASSERT(bcsv_writer_open_u(cw, filepath, true, BCSV_FORMAT_AUTO, NULL),
                "csv writer (_u) opened");

    bcsv_row_t ext_row = bcsv_row_create(layout);
    bcsv_row_set_int32(ext_row, 0, 99);
    bcsv_row_set_double(ext_row, 1, 7.25);
    bool ok = bcsv_writer_write_u(cw, ext_row);
    TEST_ASSERT(ok, "writer_write_u(external) succeeds");
    bcsv_writer_close_u(cw);

    bcsv_reader_t cr = bcsv_reader_create_u();
    bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
    ro.csv_layout = layout;
    TEST_ASSERT(bcsv_reader_open_u(cr, filepath, BCSV_FORMAT_AUTO, &ro), "reader (_u) opened");
    TEST_ASSERT(bcsv_reader_next_u(cr), "csv read external row");
    TEST_ASSERT_EQ_INT(bcsv_row_get_int32(bcsv_reader_row_u(cr), 0), 99, "csv ext id=99");
    TEST_ASSERT_NEAR(bcsv_row_get_double(bcsv_reader_row_u(cr), 1), 7.25, 1e-9, "csv ext val");
    bcsv_reader_close_u(cr);

    bcsv_reader_destroy_u(cr);
    bcsv_row_destroy(ext_row);
    bcsv_writer_destroy_u(cw);
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
    bcsv_sampler_destroy(NULL);
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

    /* Sampler NULL safety */
    TEST_ASSERT(bcsv_sampler_create(NULL) == NULL, "sampler_create(NULL) returns NULL");
    TEST_ASSERT(bcsv_sampler_set_conditional(NULL, "x") == false, "sampler_set_conditional(NULL) returns false");
    TEST_ASSERT(bcsv_sampler_set_selection(NULL, "x") == false, "sampler_set_selection(NULL) returns false");
    TEST_ASSERT(bcsv_sampler_next(NULL) == false, "sampler_next(NULL) returns false");
    TEST_ASSERT(bcsv_sampler_row(NULL) == NULL, "sampler_row(NULL) returns NULL");
    TEST_ASSERT(bcsv_sampler_output_layout(NULL) == NULL, "sampler_output_layout(NULL) returns NULL");

    /* Row column_count / visit NULL safety */
    TEST_ASSERT(bcsv_row_column_count(NULL) == 0, "row_column_count(NULL) returns 0");

    /* bcsv_last_error should have something after NULL handle calls */
    const char* err = bcsv_last_error();
    TEST_ASSERT(err != NULL && strlen(err) > 0, "bcsv_last_error reports NULL handle");

    /* bcsv_clear_last_error should reset the error state */
    bcsv_clear_last_error();
    err = bcsv_last_error();
    TEST_ASSERT(err != NULL && strlen(err) == 0, "bcsv_clear_last_error resets to empty");
}

/* ── Row column_count ──────────────────────────────────────────────── */
static void test_row_column_count(void) {
    TEST_START("Row column_count");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "a", BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "b", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "c", BCSV_TYPE_STRING);

    bcsv_row_t row = bcsv_row_create(layout);
    TEST_ASSERT_EQ_INT((int)bcsv_row_column_count(row), 3, "row_column_count = 3");

    bcsv_row_destroy(row);
    bcsv_layout_destroy(layout);
}

/* ── Row Visit API ─────────────────────────────────────────────────── */
struct VisitResult {
    size_t count;
    int32_t int_val;
    double  dbl_val;
    char    str_val[64];
    bool    bool_val;
    bcsv_type_t types[4];
};

static void visit_cb(size_t col_index, bcsv_type_t col_type, const void* value, void* user_data) {
    (void)col_index;
    struct VisitResult* r = (struct VisitResult*)user_data;
    r->types[r->count] = col_type;
    switch (col_type) {
        case BCSV_TYPE_INT32:  r->int_val = *(const int32_t*)value; break;
        case BCSV_TYPE_DOUBLE: r->dbl_val = *(const double*)value; break;
        case BCSV_TYPE_STRING: strncpy(r->str_val, (const char*)value, sizeof(r->str_val)-1); r->str_val[sizeof(r->str_val)-1]='\0'; break;
        case BCSV_TYPE_BOOL:   r->bool_val = *(const bool*)value; break;
        default: break;
    }
    r->count++;
}

static void test_row_visit_const(void) {
    TEST_START("Row Visit Const API");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",   BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "val",  BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "name", BCSV_TYPE_STRING);
    bcsv_layout_add_column(layout, 3, "flag", BCSV_TYPE_BOOL);

    bcsv_row_t row = bcsv_row_create(layout);
    bcsv_row_set_int32 (row, 0, 42);
    bcsv_row_set_double(row, 1, 3.14);
    bcsv_row_set_string(row, 2, "hello");
    bcsv_row_set_bool  (row, 3, true);

    /* Visit all 4 columns */
    struct VisitResult vr;
    memset(&vr, 0, sizeof(vr));
    bcsv_row_visit_const(row, 0, 4, visit_cb, &vr);

    TEST_ASSERT_EQ_INT((int)vr.count, 4, "visited 4 columns");
    TEST_ASSERT_EQ_INT(vr.int_val, 42, "visit int32 = 42");
    TEST_ASSERT_NEAR(vr.dbl_val, 3.14, 1e-6, "visit double = 3.14");
    TEST_ASSERT_EQ_STR(vr.str_val, "hello", "visit string = hello");
    TEST_ASSERT(vr.bool_val == true, "visit bool = true");

    /* Visit type tracking */
    TEST_ASSERT_EQ_INT(vr.types[0], BCSV_TYPE_INT32, "col 0 type INT32");
    TEST_ASSERT_EQ_INT(vr.types[1], BCSV_TYPE_DOUBLE, "col 1 type DOUBLE");
    TEST_ASSERT_EQ_INT(vr.types[2], BCSV_TYPE_STRING, "col 2 type STRING");
    TEST_ASSERT_EQ_INT(vr.types[3], BCSV_TYPE_BOOL, "col 3 type BOOL");

    /* Visit partial range (columns 1-2 only) */
    memset(&vr, 0, sizeof(vr));
    bcsv_row_visit_const(row, 1, 2, visit_cb, &vr);
    TEST_ASSERT_EQ_INT((int)vr.count, 2, "visited 2 columns (partial range)");
    TEST_ASSERT_NEAR(vr.dbl_val, 3.14, 1e-6, "partial visit double = 3.14");
    TEST_ASSERT_EQ_STR(vr.str_val, "hello", "partial visit string = hello");

    /* Visit with out-of-bounds should set error */
    bcsv_clear_last_error();
    bcsv_row_visit_const(row, 0, 10, visit_cb, &vr);
    const char* err = bcsv_last_error();
    TEST_ASSERT(strlen(err) > 0, "visit out-of-bounds sets error");

    /* Visit with NULL callback should not crash */
    bcsv_row_visit_const(row, 0, 4, NULL, NULL);
    TEST_ASSERT(true, "visit with NULL callback doesn't crash");

    bcsv_row_destroy(row);
    bcsv_layout_destroy(layout);
}

/* ── Sampler API ───────────────────────────────────────────────────── */
static void test_sampler_basic(void) {
    TEST_START("Sampler Basic: conditional filter");

    /* Create a BCSV file with 100 rows, id=0..99, value=i*1.5 */
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",    BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "value", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "name",  BCSV_TYPE_STRING);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_sampler_basic.bcsv");

    bcsv_writer_t writer = bcsv_writer_create(layout);
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    bcsv_row_t row = bcsv_writer_row(writer);
    for (int i = 0; i < 100; ++i) {
        bcsv_row_set_int32(row, 0, i);
        bcsv_row_set_double(row, 1, i * 1.5);
        char name[32]; snprintf(name, sizeof(name), "row_%d", i);
        bcsv_row_set_string(row, 2, name);
        bcsv_writer_next(writer);
    }
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);

    /* Open reader and create sampler */
    bcsv_reader_t reader = bcsv_reader_create();
    bool ok = bcsv_reader_open(reader, filepath);
    TEST_ASSERT(ok, "reader opened for sampler");

    bcsv_sampler_t sampler = bcsv_sampler_create(reader);
    TEST_ASSERT(sampler != NULL, "sampler created");

    /* Set conditional: only rows where id >= 50 */
    ok = bcsv_sampler_set_conditional(sampler, "X[0][0] >= 50");
    TEST_ASSERT(ok, "sampler_set_conditional compiled OK");

    /* Verify getConditional */
    const char* cond = bcsv_sampler_get_conditional(sampler);
    TEST_ASSERT_EQ_STR(cond, "X[0][0] >= 50", "get_conditional returns expression");

    /* Mode defaults */
    TEST_ASSERT_EQ_INT(bcsv_sampler_get_mode(sampler), BCSV_SAMPLER_TRUNCATE, "default mode = TRUNCATE");

    /* Set and verify mode */
    bcsv_sampler_set_mode(sampler, BCSV_SAMPLER_EXPAND);
    TEST_ASSERT_EQ_INT(bcsv_sampler_get_mode(sampler), BCSV_SAMPLER_EXPAND, "mode set to EXPAND");
    bcsv_sampler_set_mode(sampler, BCSV_SAMPLER_TRUNCATE);

    /* Iterate and count matching rows */
    int count = 0;
    while (bcsv_sampler_next(sampler)) {
        const_bcsv_row_t srow = bcsv_sampler_row(sampler);
        int32_t id = bcsv_row_get_int32(srow, 0);
        TEST_ASSERT(id >= 50, "sampler row id >= 50");
        if (count == 0) {
            TEST_ASSERT_EQ_INT(id, 50, "first sampler result id=50");
        }
        count++;
    }
    TEST_ASSERT_EQ_INT(count, 50, "sampler filtered to 50 rows");

    /* output_layout with no selection is empty (row comes from reader directly) */
    const_bcsv_layout_t out_layout = bcsv_sampler_output_layout(sampler);
    TEST_ASSERT(out_layout != NULL, "sampler output_layout non-NULL");
    TEST_ASSERT_EQ_INT((int)bcsv_layout_column_count(out_layout), 0, "output_layout has 0 columns (no selection = empty layout)");

    /* error_msg should be empty on success */
    const char* serr = bcsv_sampler_error_msg(sampler);
    TEST_ASSERT(serr != NULL && strlen(serr) == 0, "sampler error_msg empty on success");

    bcsv_sampler_destroy(sampler);
    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_layout_destroy(layout);
}

static void test_sampler_selection(void) {
    TEST_START("Sampler: selection (projection)");

    /* Create file with 5 columns: id, x, y, z, name */
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",   BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "x",    BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "y",    BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 3, "z",    BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 4, "name", BCSV_TYPE_STRING);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_sampler_select.bcsv");

    bcsv_writer_t writer = bcsv_writer_create(layout);
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    bcsv_row_t row = bcsv_writer_row(writer);
    for (int i = 0; i < 20; ++i) {
        bcsv_row_set_int32 (row, 0, i);
        bcsv_row_set_double(row, 1, i * 0.1);
        bcsv_row_set_double(row, 2, i * 0.2);
        bcsv_row_set_double(row, 3, i * 0.3);
        char name[32]; snprintf(name, sizeof(name), "item_%d", i);
        bcsv_row_set_string(row, 4, name);
        bcsv_writer_next(writer);
    }
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);

    /* Open and create sampler with selection (project to id, y only) */
    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);

    bcsv_sampler_t sampler = bcsv_sampler_create(reader);
    bool ok = bcsv_sampler_set_selection(sampler, "X[0][0], X[0][2]");
    TEST_ASSERT(ok, "sampler selection compiled");

    /* Verify getSelection */
    const char* sel = bcsv_sampler_get_selection(sampler);
    TEST_ASSERT(strlen(sel) > 0, "get_selection returns non-empty expression");

    /* Output layout should have 2 columns */
    const_bcsv_layout_t out = bcsv_sampler_output_layout(sampler);
    TEST_ASSERT_EQ_INT((int)bcsv_layout_column_count(out), 2, "output_layout has 2 columns");

    /* Iterate and verify projection */
    int count = 0;
    while (bcsv_sampler_next(sampler)) {
        const_bcsv_row_t srow = bcsv_sampler_row(sampler);
        int32_t id = bcsv_row_get_int32(srow, 0);
        double y_val = bcsv_row_get_double(srow, 1);
        TEST_ASSERT_EQ_INT(id, count, "projected id matches");
        TEST_ASSERT_NEAR(y_val, count * 0.2, 1e-9, "projected y matches");

        /* Check sourceRowPos (1-based: rowPos is incremented after read) */
        size_t src_pos = bcsv_sampler_source_row_pos(sampler);
        TEST_ASSERT_EQ_INT((int)src_pos, count + 1, "source_row_pos matches (1-based)");

        count++;
    }
    TEST_ASSERT_EQ_INT(count, 20, "all 20 rows projected");

    bcsv_sampler_destroy(sampler);
    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_layout_destroy(layout);
}

static void test_sampler_combined(void) {
    TEST_START("Sampler: conditional + selection combined");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",    BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "value", BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "name",  BCSV_TYPE_STRING);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_sampler_combined.bcsv");

    bcsv_writer_t writer = bcsv_writer_create_zoh(layout);
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    bcsv_row_t row = bcsv_writer_row(writer);
    for (int i = 0; i < 100; ++i) {
        bcsv_row_set_int32(row, 0, i);
        bcsv_row_set_double(row, 1, i * 2.0);
        char name[32]; snprintf(name, sizeof(name), "item_%d", i);
        bcsv_row_set_string(row, 2, name);
        bcsv_writer_next(writer);
    }
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);

    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);

    bcsv_sampler_t sampler = bcsv_sampler_create(reader);

    /* Filter: value < 20 (i.e. id < 10) */
    bool ok = bcsv_sampler_set_conditional(sampler, "X[0][1] < 20");
    TEST_ASSERT(ok, "conditional compiled");

    /* Project: only id and name */
    ok = bcsv_sampler_set_selection(sampler, "X[0][0], X[0][2]");
    TEST_ASSERT(ok, "selection compiled");

    int count = 0;
    while (bcsv_sampler_next(sampler)) {
        const_bcsv_row_t srow = bcsv_sampler_row(sampler);
        int32_t id = bcsv_row_get_int32(srow, 0);
        TEST_ASSERT(id < 10, "combined: id < 10");
        count++;
    }
    TEST_ASSERT_EQ_INT(count, 10, "combined filter+select yields 10 rows");

    bcsv_sampler_destroy(sampler);
    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_layout_destroy(layout);
}

static void test_sampler_compile_error(void) {
    TEST_START("Sampler: compile error handling");

    /* Create a minimal file */
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id", BCSV_TYPE_INT32);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_sampler_err.bcsv");

    bcsv_writer_t writer = bcsv_writer_create(layout);
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    bcsv_row_set_int32(bcsv_writer_row(writer), 0, 1);
    bcsv_writer_next(writer);
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);

    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);

    bcsv_sampler_t sampler = bcsv_sampler_create(reader);

    /* Invalid expression should fail */
    bool ok = bcsv_sampler_set_conditional(sampler, "X[0][999] >= 50");
    TEST_ASSERT(!ok, "invalid conditional fails");

    const char* err = bcsv_sampler_error_msg(sampler);
    TEST_ASSERT(err != NULL && strlen(err) > 0, "sampler error msg non-empty on failure");

    bcsv_sampler_destroy(sampler);
    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_layout_destroy(layout);
}

/* ── Visit on reader rows (integration) ────────────────────────────── */
static void test_visit_on_reader_rows(void) {
    TEST_START("Visit on Reader Rows (integration)");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 0, "id",   BCSV_TYPE_INT32);
    bcsv_layout_add_column(layout, 1, "val",  BCSV_TYPE_DOUBLE);
    bcsv_layout_add_column(layout, 2, "name", BCSV_TYPE_STRING);

    char filepath[256];
    make_path(filepath, sizeof(filepath), "test_visit_reader.bcsv");

    bcsv_writer_t writer = bcsv_writer_create_zoh(layout);
    bcsv_writer_open(writer, filepath, true, 1, 64, BCSV_FLAG_NONE);
    bcsv_row_t row = bcsv_writer_row(writer);
    bcsv_row_set_int32(row, 0, 7);
    bcsv_row_set_double(row, 1, 99.5);
    bcsv_row_set_string(row, 2, "test");
    bcsv_writer_next(writer);
    bcsv_writer_close(writer);
    bcsv_writer_destroy(writer);

    bcsv_reader_t reader = bcsv_reader_create();
    bcsv_reader_open(reader, filepath);
    TEST_ASSERT(bcsv_reader_next(reader), "reader next");

    const_bcsv_row_t rrow = bcsv_reader_row(reader);

    struct VisitResult vr;
    memset(&vr, 0, sizeof(vr));
    bcsv_row_visit_const(rrow, 0, 3, visit_cb, &vr);

    TEST_ASSERT_EQ_INT((int)vr.count, 3, "visited 3 reader columns");
    TEST_ASSERT_EQ_INT(vr.int_val, 7, "visit reader int32 = 7");
    TEST_ASSERT_NEAR(vr.dbl_val, 99.5, 1e-6, "visit reader double = 99.5");
    TEST_ASSERT_EQ_STR(vr.str_val, "test", "visit reader string = test");

    bcsv_reader_close(reader);
    bcsv_reader_destroy(reader);
    bcsv_layout_destroy(layout);
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

    /* Step 2: Read BCSV → Write CSV (unified writer) */
    bcsv_reader_t br = bcsv_reader_create();
    bcsv_reader_open(br, bcsv_path);

    bcsv_writer_t cw = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(bcsv_writer_open_u(cw, csv_path, true, BCSV_FORMAT_AUTO, NULL),
                "cross: unified CSV writer opened (.csv → CSV)");
    while (bcsv_reader_next(br)) {
        TEST_ASSERT(bcsv_writer_write_u(cw, bcsv_reader_row(br)), "cross: CSV write");
    }
    bcsv_writer_close_u(cw);
    bcsv_reader_close(br);

    /* Step 3: Read CSV → Write BCSV (Delta) */
    bcsv_reader_t cr = bcsv_reader_create_u();
    bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
    ro.csv_layout = layout;
    TEST_ASSERT(bcsv_reader_open_u(cr, csv_path, BCSV_FORMAT_AUTO, &ro),
                "cross: unified CSV reader opened");

    bcsv_writer_t bw2 = bcsv_writer_create_delta(layout);
    bcsv_writer_open(bw2, bcsv2_path, true, 1, 64, BCSV_FLAG_NONE);
    while (bcsv_reader_next_u(cr)) {
        TEST_ASSERT(bcsv_writer_write(bw2, bcsv_reader_row_u(cr)), "cross: BCSV write");
    }
    bcsv_writer_close(bw2);
    bcsv_reader_close_u(cr);

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
    bcsv_reader_destroy_u(cr);
    bcsv_writer_destroy_u(cw);
    bcsv_writer_destroy(bw);
    bcsv_writer_destroy(bw2);
    bcsv_layout_destroy(layout);
}

/* ── Multi-packet file (10K rows) ──────────────────────────────────── */
/* ------------------------------------------------------------------------
 * Row-codec selection, and the flags the header ends up carrying.
 *
 * Two defects met here, both silent, both fixed in 1.5.17:
 *
 *   1. bcsv_writer_create is documented as the flat writer and tagged its
 *      handle Type::Flat, but constructed bcsv::Writer<Layout> — which takes
 *      the DEFAULT template argument, RowCodecDelta002. So every caller asking
 *      for flat got delta, including both C# bindings, which route "flat" here.
 *      Nothing reported it: the file was valid, its header honestly said delta,
 *      and only comparing the header against the request would have shown it.
 *
 *   2. There was no way to ask a writer what it had written. The row-codec
 *      flags are set from the codec rather than from bcsv_writer_open's flags
 *      argument, so the only way to check was to close the file and reopen it.
 *      bcsv_writer_file_flags answers it directly.
 *
 * The assertion that matters is that the three constructors produce three
 * DIFFERENT codecs, and that each writer agrees with the reader about which.
 * ---------------------------------------------------------------------- */
static void test_row_codec_selection_and_flags(void) {
    TEST_START("Row Codec Selection and File Flags");

    /* ZERO_ORDER_HOLD=1, BATCH_COMPRESS=8, DELTA_ENCODING=16 */
    const int expect[3] = { 8, 1 | 8, 8 | 16 };
    const char* names[3] = { "flat", "zoh", "delta" };
    int reported[3];

    for (int i = 0; i < 3; i++) {
        char path[256];
        snprintf(path, sizeof(path), "test_codec_flags_%d.bcsv", i);

        bcsv_layout_t layout = bcsv_layout_create();
        bcsv_layout_add_column(layout, 0, "v", BCSV_TYPE_INT32);

        bcsv_writer_t writer = (i == 0) ? bcsv_writer_create(layout)
                             : (i == 1) ? bcsv_writer_create_zoh(layout)
                                        : bcsv_writer_create_delta(layout);
        TEST_ASSERT(writer != NULL, "writer created");

        /* Ask for BATCH_COMPRESS only. The row-codec bits are not ours to set;
         * whatever we passed for them would be replaced anyway. */
        TEST_ASSERT(bcsv_writer_open(writer, path, true, 6, 64, 8), "writer opened");

        bcsv_row_t row = bcsv_writer_row(writer);
        for (int r = 0; r < 64; r++) {
            bcsv_row_set_int32(row, 0, r);
            bcsv_writer_next(writer);
        }

        reported[i] = bcsv_writer_file_flags(writer);
        bcsv_writer_close(writer);

        TEST_ASSERT_EQ_INT(reported[i], expect[i],
                           "writer reports the codec it was constructed with");

        bcsv_reader_t reader = bcsv_reader_create();
        TEST_ASSERT(bcsv_reader_open(reader, path), "reader opened");
        TEST_ASSERT_EQ_INT(bcsv_reader_file_flags(reader), reported[i],
                           "reader and writer agree on the header flags");
        bcsv_reader_close(reader);
        bcsv_reader_destroy(reader);

        bcsv_writer_destroy(writer);
        bcsv_layout_destroy(layout);
        remove(path);
        printf("  %-5s -> flags %d\n", names[i], reported[i]);
    }

    /* The point of the whole test: three constructors, three codecs. Before
     * 1.5.17 flat and delta were the same writer and this would have failed. */
    TEST_ASSERT(reported[0] != reported[2], "flat is not delta");
    TEST_ASSERT(reported[1] != reported[2], "zoh is not delta");
    TEST_ASSERT(reported[0] != reported[1], "flat is not zoh");
}

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

/* ── Unified File API (_u, ADR-0009) ─────────────────────────────── */
static void test_unified_api(void) {
    TEST_START("Unified File API (_u)");

    bcsv_layout_t layout = create_test_layout();
    char pcsv[256], plz4[256], pbcsv[256], pnop[256];
    make_path(pcsv,  sizeof(pcsv),  "u_plain.csv");
    make_path(plz4,  sizeof(plz4),  "u_plain.csv.lz4");
    make_path(pbcsv, sizeof(pbcsv), "u_plain.bcsv");
    make_path(pnop,  sizeof(pnop),  "u_does_not_exist.bcsv");

    /* Plain CSV fixture written with raw stdio (no BCSV involvement) */
    {
        FILE* f = fopen(pcsv, "w");
        TEST_ASSERT(f != NULL, "fixture csv created");
        fputs("id,value,name,flag,x,y,z\n", f);
        for (int i = 0; i < 5; ++i) {
            fprintf(f, "%d,%.6f,row_%d,%d,%.6f,%.6f,%.6f\n",
                    i, i * 1.5, i, (i % 2) == 0, i * 0.1, i * 0.2, i * 0.3);
        }
        fclose(f);
    }

    /* CSV_LZ4 + BCSV fixtures through the unified writer */
    bcsv_writer_t w = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(w != NULL, "writer (_u) created");
    {
        TEST_ASSERT(bcsv_writer_open_u(w, plz4, true, BCSV_FORMAT_AUTO, NULL),
                    "writer open .csv.lz4 (extension rule)");
        TEST_ASSERT_EQ_INT(bcsv_writer_format_u(w), BCSV_FORMAT_CSV_LZ4, ".lz4 extension -> CSV_LZ4");
        bcsv_row_t row = bcsv_writer_row_u(w);
        for (int i = 0; i < 5; ++i) { fill_test_row(row, i); TEST_ASSERT(bcsv_writer_next_u(w), "lz4 fixture row"); }
        TEST_ASSERT_EQ_INT(bcsv_writer_index_u(w), 5, "writer index 5");
        bcsv_writer_close_u(w);
        TEST_ASSERT(!bcsv_writer_is_open_u(w), "writer not open after close");

        TEST_ASSERT(bcsv_writer_open_u(w, pbcsv, true, BCSV_FORMAT_AUTO, NULL),
                    "writer open .bcsv (extension rule)");
        TEST_ASSERT_EQ_INT(bcsv_writer_format_u(w), BCSV_FORMAT_BCSV, ".bcsv extension -> BCSV");
        row = bcsv_writer_row_u(w);   /* new member: fresh row */
        for (int i = 0; i < 3; ++i) { fill_test_row(row, i); TEST_ASSERT(bcsv_writer_next_u(w), "bcsv fixture row"); }
        bcsv_writer_close_u(w);
    }

    /* probe_format on all three formats + a nonexistent path */
    {
        bcsv_format_t f = BCSV_FORMAT_AUTO;
        TEST_ASSERT(bcsv_probe_format(pcsv, &f), "probe plain csv");
        TEST_ASSERT_EQ_INT(f, BCSV_FORMAT_CSV, "probe -> CSV");
        TEST_ASSERT(bcsv_probe_format(plz4, &f), "probe csv.lz4");
        TEST_ASSERT_EQ_INT(f, BCSV_FORMAT_CSV_LZ4, "probe -> CSV_LZ4");
        TEST_ASSERT(bcsv_probe_format(pbcsv, &f), "probe bcsv");
        TEST_ASSERT_EQ_INT(f, BCSV_FORMAT_BCSV, "probe -> BCSV");
        f = BCSV_FORMAT_BCSV;
        TEST_ASSERT(!bcsv_probe_format(pnop, &f), "probe nonexistent fails");
        TEST_ASSERT(bcsv_last_error()[0] != '\0', "probe failure sets last_error");
    }

    /* B2 ergonomics: AUTO + NULL opts on plain CSV = loud fail, no crash */
    {
        bcsv_reader_t r = bcsv_reader_create_u();
        TEST_ASSERT(!bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_AUTO, NULL),
                    "AUTO+NULL opts on plain CSV fails loudly (B2)");
        TEST_ASSERT(strstr(bcsv_reader_error_msg_u(r), "csv_layout") != NULL,
                    "failure names csv_layout");
        TEST_ASSERT(!bcsv_reader_is_open_u(r), "failed open leaves handle closed");
        TEST_ASSERT_EQ_INT(bcsv_reader_format_u(r), BCSV_FORMAT_AUTO, "failed open leaves format AUTO");

        /* BCSV needs no schema: AUTO + NULL opts on .bcsv works */
        TEST_ASSERT(bcsv_reader_open_u(r, pbcsv, BCSV_FORMAT_AUTO, NULL),
                    "BCSV AUTO + NULL opts works (defaults)");
        TEST_ASSERT_EQ_INT(bcsv_reader_format_u(r), BCSV_FORMAT_BCSV, "format_u AUTO -> BCSV");
        /* M5: after close, format returns to AUTO */
        bcsv_reader_close_u(r);
        TEST_ASSERT_EQ_INT(bcsv_reader_format_u(r), BCSV_FORMAT_AUTO, "format_u AUTO after close");
        /* close_u safe when already closed */
        bcsv_reader_close_u(r);
        /* navigation on a closed handle is inert */
        TEST_ASSERT(!bcsv_reader_next_u(r), "next_u on closed reader false");
        TEST_ASSERT(!bcsv_reader_is_open_u(r), "is_open_u closed false");
        TEST_ASSERT(bcsv_reader_layout_u(r) == NULL, "layout_u NULL when closed");
        TEST_ASSERT(bcsv_reader_native_u(r) == NULL, "native_u NULL when closed");
        bcsv_reader_destroy_u(r);
    }

    /* Pinned mismatches are loud and specific; format enum validated */
    {
        bcsv_reader_t r = bcsv_reader_create_u();
        bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
        ro.csv_layout = layout;

        TEST_ASSERT(!bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_BCSV, &ro),
                    "pinned BCSV on CSV fails");
        TEST_ASSERT(strstr(bcsv_reader_error_msg_u(r), "pinned format") != NULL,
                    "pinned mismatch wording (BCSV on CSV)");

        TEST_ASSERT(!bcsv_reader_open_u(r, pbcsv, BCSV_FORMAT_CSV, &ro),
                    "pinned CSV on BCSV fails");
        TEST_ASSERT(strstr(bcsv_reader_error_msg_u(r), "pinned format") != NULL,
                    "pinned mismatch wording (CSV on BCSV)");

        TEST_ASSERT(!bcsv_reader_open_u(r, plz4, BCSV_FORMAT_CSV, &ro),
                    "pinned CSV on LZ4 frame fails (magic cross-refusal)");
        TEST_ASSERT(!bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_CSV_LZ4, &ro),
                    "pinned CSV_LZ4 on plain CSV fails");

        TEST_ASSERT(!bcsv_reader_open_u(r, pcsv, (bcsv_format_t)99, &ro),
                    "unknown format enum rejected");
        TEST_ASSERT(strstr(bcsv_reader_error_msg_u(r), "invalid format") != NULL,
                    "unknown format reports 'invalid format'");

        /* A matching pin succeeds */
        TEST_ASSERT(bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_CSV, &ro), "pinned CSV on CSV works");
        TEST_ASSERT_EQ_INT(bcsv_reader_format_u(r), BCSV_FORMAT_CSV, "pinned format sticks");
        bcsv_reader_destroy_u(r);
    }

    /* B3 reader kind-switch: one handle migrates BCSV -> CSV -> BCSV; the
       REPLACED member must never serve stale rows. */
    {
        bcsv_reader_t r = bcsv_reader_create_u();
        bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
        ro.csv_layout = layout;

        TEST_ASSERT(bcsv_reader_open_u(r, pbcsv, BCSV_FORMAT_AUTO, NULL), "switch open 1 (bcsv)");
        const_bcsv_layout_t lay_bin = bcsv_reader_layout_u(r);   /* borrowed from bin member */
        int n = 0; while (bcsv_reader_next_u(r)) n++;
        TEST_ASSERT_EQ_INT(n, 3, "bcsv member yields 3 rows");
        TEST_ASSERT_EQ_INT((int)bcsv_reader_file_line_u(r), 0, "file_line_u 0 in BCSV mode");

        TEST_ASSERT(bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_AUTO, &ro), "switch open 2 (csv)");
        TEST_ASSERT_EQ_INT(bcsv_reader_format_u(r), BCSV_FORMAT_CSV, "switched to CSV");
        /* Reopen across kinds invalidates borrowed layout handles: the active
           member (and its layout object) is a different one now. */
        TEST_ASSERT(bcsv_reader_layout_u(r) != lay_bin, "layout handle replaced by kind-switch");
        n = 0; while (bcsv_reader_next_u(r)) n++;
        TEST_ASSERT_EQ_INT(n, 5, "kind-switched csv member yields 5 rows (was B3: 0)");
        TEST_ASSERT(bcsv_reader_file_line_u(r) > 0, "file_line_u > 0 in CSV mode");
        TEST_ASSERT(bcsv_reader_layout_u(r) != NULL, "layout_u valid after switch");

        TEST_ASSERT(bcsv_reader_open_u(r, pbcsv, BCSV_FORMAT_AUTO, NULL), "switch open 3 (bcsv)");
        TEST_ASSERT_EQ_INT(bcsv_reader_format_u(r), BCSV_FORMAT_BCSV, "switched back to BCSV");
        n = 0; while (bcsv_reader_next_u(r)) n++;
        TEST_ASSERT_EQ_INT(n, 3, "binary rows again after second switch");
        TEST_ASSERT_EQ_INT((int)bcsv_reader_file_line_u(r), 0, "file_line_u back to 0");

        /* native_u: BCSV member impersonates the legacy binary reader */
        void* nat = bcsv_reader_native_u(r);
        TEST_ASSERT(nat != NULL, "native_u non-NULL while open");
        bcsv_reader_destroy_u(r);
    }

    /* B3 writer kind-switch + M5 row handle refresh across formats */
    {
        char pw1[256], pw2[256];
        make_path(pw1, sizeof(pw1), "u_switch.bcsv");
        make_path(pw2, sizeof(pw2), "u_switch.csv");

        bcsv_writer_t w2 = bcsv_writer_create_u(layout, BCSV_CODEC_ZOH);
        TEST_ASSERT(bcsv_writer_open_u(w2, pw1, true, BCSV_FORMAT_AUTO, NULL), "switch write 1 (bcsv)");
        bcsv_row_t r1 = bcsv_writer_row_u(w2);
        for (int i = 0; i < 4; ++i) { fill_test_row(r1, i); TEST_ASSERT(bcsv_writer_next_u(w2), "switch bcsv row"); }
        bcsv_writer_close_u(w2);

        TEST_ASSERT(bcsv_writer_open_u(w2, pw2, true, BCSV_FORMAT_AUTO, NULL), "switch write 2 (csv)");
        TEST_ASSERT_EQ_INT(bcsv_writer_format_u(w2), BCSV_FORMAT_CSV, "writer switched to CSV");
        bcsv_row_t r2 = bcsv_writer_row_u(w2);
        TEST_ASSERT(r2 != NULL, "writer row after switch");
        TEST_ASSERT(r2 != r1, "row handle replaced by kind-switch");
        for (int i = 0; i < 6; ++i) { fill_test_row(r2, i); TEST_ASSERT(bcsv_writer_next_u(w2), "switch csv row"); }
        bcsv_writer_close_u(w2);

        /* Verify both outputs independently */
        {
            bcsv_reader_t br = bcsv_reader_create();
            TEST_ASSERT(bcsv_reader_open(br, pw1), "legacy reader on switched bcsv file");
            int n = 0; while (bcsv_reader_next(br)) n++;
            TEST_ASSERT_EQ_INT(n, 4, "bcsv switch output has 4 rows");
            bcsv_reader_destroy(br);

            bcsv_reader_t cr = bcsv_reader_create_u();
            bcsv_open_opts cro = BCSV_OPEN_OPTS_INIT;
            cro.csv_layout = layout;
            TEST_ASSERT(bcsv_reader_open_u(cr, pw2, BCSV_FORMAT_AUTO, &cro), "unified reader on switched csv file");
            n = 0; while (bcsv_reader_next_u(cr)) n++;
            TEST_ASSERT_EQ_INT(n, 6, "csv switch output has 6 rows");
            bcsv_reader_destroy_u(cr);
        }
        bcsv_writer_destroy_u(w2);
    }

    /* struct_size forward-extension rule (M2): v1/current size accepted,
       anything larger (future library) or smaller rejected naming struct_size */
    {
        bcsv_reader_t r = bcsv_reader_create_u();
        bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT;
        ro.csv_layout = layout;

        ro.struct_size = sizeof(bcsv_open_opts);            /* current == v1 size */
        TEST_ASSERT(bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_AUTO, &ro), "struct_size == sizeof accepted");
        bcsv_reader_close_u(r);

        ro.struct_size = sizeof(bcsv_open_opts) + 64;       /* newer caller */
        TEST_ASSERT(!bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_AUTO, &ro), "oversized struct_size rejected");
        TEST_ASSERT(strstr(bcsv_reader_error_msg_u(r), "struct_size") != NULL,
                    "rejection names struct_size");

        ro.struct_size = offsetof(bcsv_open_opts, lz4_block_id);  /* below v1 size */
        TEST_ASSERT(!bcsv_reader_open_u(r, pcsv, BCSV_FORMAT_AUTO, &ro), "sub-v1 struct_size rejected");

        bcsv_reader_destroy_u(r);
    }

    /* Cross-family destroy is a registry kind-checked no-op: legacy destroy
       names (still exported for the binary family) cannot free a _u handle. */
    {
        bcsv_reader_t r = bcsv_reader_create_u();
        bcsv_writer_t w3 = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
        bcsv_clear_last_error();
        bcsv_reader_destroy(r);          /* wrong family: must NOT free */
        bcsv_writer_destroy(w3);         /* wrong family: must NOT free */
        /* Handle still alive: using it must work, then destroy_u frees it */
        TEST_ASSERT(bcsv_reader_open_u(r, pbcsv, BCSV_FORMAT_AUTO, NULL),
                    "reader survives cross-family destroy attempt");
        bcsv_reader_destroy_u(r);
        TEST_ASSERT(bcsv_writer_format_u(w3) == BCSV_FORMAT_AUTO, "writer still alive after wrong destroy");
        bcsv_writer_destroy_u(w3);
    }

    bcsv_writer_destroy_u(w);
    bcsv_layout_destroy(layout);
}

/* ══════════════════════════════════════════════════════════════════ */
/* native_u round-trip through the LEGACY deep APIs (round-2 N1): while the
   container is open, its member is registered under the legacy kind, so
   native handles pass the legacy registry guards; after the container closes
   the borrowed entry is gone and legacy calls refuse it. Covers the writer
   bin-member same-format reuse and the file_flags tri-state guard too. */
static void test_unified_native_and_reuse(void) {
    TEST_START("Unified native round-trip + reuse arms");
    bcsv_layout_t layout = create_test_layout();
    char pbcsv[256]; make_path(pbcsv, sizeof(pbcsv), "u_nr.bcsv");
    {
        bcsv_writer_t w0 = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
        TEST_ASSERT(bcsv_writer_open_u(w0, pbcsv, true, BCSV_FORMAT_BCSV, NULL), "nr fixture open");
        bcsv_row_t row = bcsv_writer_row_u(w0);
        for (int i = 0; i < 3; ++i) { fill_test_row(row, i); bcsv_writer_next_u(w0); }
        bcsv_writer_close_u(w0);
        bcsv_writer_destroy_u(w0);
    }
    {
        bcsv_reader_t r = bcsv_reader_create_u();
        TEST_ASSERT(bcsv_reader_open_u(r, pbcsv, BCSV_FORMAT_AUTO, NULL), "reader open for native");
        void* nat = bcsv_reader_native_u(r);
        TEST_ASSERT(nat != NULL, "reader native_u non-NULL");
        bcsv_clear_last_error();
        TEST_ASSERT(bcsv_reader_is_open((bcsv_reader_t)nat), "legacy is_open on native member");
        bcsv_sampler_t smp = bcsv_sampler_create((bcsv_reader_t)nat);
        TEST_ASSERT(smp != NULL, "sampler accepts unified reader native");
        bcsv_sampler_destroy(smp);
        bcsv_reader_close_u(r);
        bcsv_clear_last_error();
        /* is_open is a legacy blind-read (kept allocated, file closed) -> false;
           the registry-GUARDED refusal after container close is sampler create,
           whose kind check sees the borrowed entry gone. */
        TEST_ASSERT(!bcsv_reader_is_open((bcsv_reader_t)nat), "native member reports closed");
        TEST_ASSERT(bcsv_sampler_create((bcsv_reader_t)nat) == NULL, "sampler refuses native after close");
        TEST_ASSERT(bcsv_last_error()[0] != '\0', "reader refusal reported");
        bcsv_reader_destroy_u(r);
    }
    {
        bcsv_writer_t wz = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
        char pn1[256]; make_path(pn1, sizeof(pn1), "u_nr_w.bcsv");
        TEST_ASSERT(bcsv_writer_open_u(wz, pn1, true, BCSV_FORMAT_BCSV, NULL), "writer open for native");
        void* wnat = bcsv_writer_native_u(wz);
        TEST_ASSERT(wnat != NULL, "writer native_u non-NULL");
        bcsv_clear_last_error();
        TEST_ASSERT(bcsv_writer_is_open((bcsv_writer_t)wnat), "legacy is_open on native writer");
        bcsv_writer_flush((bcsv_writer_t)wnat);
        TEST_ASSERT(bcsv_last_error()[0] == '\0', "writer flush through native passes registry");
        TEST_ASSERT(bcsv_writer_is_open((bcsv_writer_t)wnat), "writer still open after flush");
        bcsv_writer_close_u(wz);
        bcsv_clear_last_error();
        TEST_ASSERT(!bcsv_writer_is_open((bcsv_writer_t)wnat), "writer native refused after close");
        bcsv_writer_destroy_u(wz);
    }
    /* Writer reuses the bin member across same-format reopens (row identical).
       The CSV member is REBUILT per open (M4) so this only holds for .bcsv. */
    {
        char prp[256]; make_path(prp, sizeof(prp), "u_nr_reuse.bcsv");
        bcsv_writer_t w2 = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
        TEST_ASSERT(bcsv_writer_open_u(w2, prp, true, BCSV_FORMAT_AUTO, NULL), "reuse open 1");
        bcsv_row_t rw1 = bcsv_writer_row_u(w2);
        fill_test_row(rw1, 1); TEST_ASSERT(bcsv_writer_next_u(w2), "reuse write 1");
        bcsv_writer_close_u(w2);
        TEST_ASSERT(bcsv_writer_open_u(w2, prp, true, BCSV_FORMAT_AUTO, NULL), "reuse open 2");
        bcsv_row_t rw2 = bcsv_writer_row_u(w2);
        TEST_ASSERT(rw2 == rw1, "bin member reused for same format+codec");
        fill_test_row(rw2, 2); TEST_ASSERT(bcsv_writer_next_u(w2), "reuse write 2");
        TEST_ASSERT_EQ_INT(bcsv_writer_index_u(w2), 1, "overwrite reopen resets count");
        bcsv_writer_close_u(w2);
        bcsv_writer_destroy_u(w2);
    }
    /* file_flags tri-state range (round-2 N2) */
    {
        char pff[256]; make_path(pff, sizeof(pff), "u_nr_flags.bcsv");
        bcsv_writer_t wf = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
        bcsv_open_opts fo = BCSV_OPEN_OPTS_INIT;
        fo.file_flags = -2;                     /* neither -1 (auto) nor a pattern */
        TEST_ASSERT(!bcsv_writer_open_u(wf, pff, true, BCSV_FORMAT_BCSV, &fo), "file_flags -2 rejected");
        TEST_ASSERT(strstr(bcsv_writer_error_msg_u(wf), "file_flags") != NULL, "names file_flags");
        bcsv_writer_destroy_u(wf);
    }
    bcsv_layout_destroy(layout);
}

/* bcsv_shutdown() with LIVE unified containers (ADR-0006/0009 exit path):
   open writers close first (footer/LZ4 trailer land), then every handle is
   freed; later destroys on pre-shutdown handles are logged no-ops; the
   registry is usable again afterwards. */
static void test_unified_shutdown(void) {
    TEST_START("Unified shutdown (_u handles)");
    bcsv_layout_t layout = create_test_layout();
    char pw[256], pl[256];
    make_path(pw, sizeof(pw), "u_shut.bcsv");
    make_path(pl, sizeof(pl), "u_shut.csv.lz4");

    bcsv_writer_t wb = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(bcsv_writer_open_u(wb, pw, true, BCSV_FORMAT_AUTO, NULL), "shutdown: open bcsv");
    bcsv_row_t r1 = bcsv_writer_row_u(wb);
    fill_test_row(r1, 7); TEST_ASSERT(bcsv_writer_next_u(wb), "shutdown: bcsv row");

    bcsv_writer_t wl = bcsv_writer_create_u(layout, BCSV_CODEC_FLAT);
    TEST_ASSERT(bcsv_writer_open_u(wl, pl, true, BCSV_FORMAT_AUTO, NULL), "shutdown: open lz4");
    bcsv_row_t r2 = bcsv_writer_row_u(wl);
    fill_test_row(r2, 8); TEST_ASSERT(bcsv_writer_next_u(wl), "shutdown: lz4 row");

    bcsv_reader_t rd = bcsv_reader_create_u();

    bcsv_shutdown();   /* closes wb + wl (trailers!) and frees rd + handles */
    TEST_ASSERT(bcsv_last_error()[0] == '\0', "shutdown clean");

    /* Both files valid afterwards: binary footer, LZ4 frame trailer.
       NOTE: shutdown also destroyed every registered layout, so the readers
       below must work with FRESH post-shutdown layouts — `layout` is dangling. */
    {
        bcsv_reader_t chk = bcsv_reader_create();
        TEST_ASSERT(bcsv_reader_open(chk, pw), "shutdown: bcsv readable after shutdown");
        int n = 0; while (bcsv_reader_next(chk)) n++;
        TEST_ASSERT_EQ_INT(n, 1, "shutdown: bcsv footer + 1 row survived");
        bcsv_reader_destroy(chk);

        bcsv_layout_t fresh = create_test_layout();   /* post-shutdown registry works */
        bcsv_open_opts ro = BCSV_OPEN_OPTS_INIT; ro.csv_layout = fresh;
        chk = bcsv_reader_create_u();
        TEST_ASSERT(bcsv_reader_open_u(chk, pl, BCSV_FORMAT_AUTO, &ro),
                    "shutdown: lz4 frame valid on read");
        n = 0; while (bcsv_reader_next_u(chk)) n++;
        TEST_ASSERT_EQ_INT(n, 1, "shutdown: lz4 1 row survived (trailer written)");
        bcsv_reader_destroy_u(chk);
        bcsv_layout_destroy(fresh);
    }

    /* Post-shutdown operations on dangling handles: the registry-guarded
       entry points are logged no-ops (same class of guarantee as legacy —
       data-path calls on dangling handles are UB in both APIs). */
    bcsv_clear_last_error();
    bcsv_writer_close_u(wb);   /* reg guard -> logged refusal, no double close */
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-shutdown writer close reported");
    bcsv_reader_destroy_u(rd);   /* reg_take miss -> logged no-op, no double free */
    bcsv_writer_destroy_u(wb);
    bcsv_writer_destroy_u(wl);
    TEST_ASSERT(1, "post-shutdown destroys are no-ops");

    /* Registry works again: fresh handles usable after shutdown */
    {
        char pn[256]; make_path(pn, sizeof(pn), "u_shut_after.bcsv");
        bcsv_layout_t l2 = create_test_layout();
        bcsv_writer_t wa = bcsv_writer_create_u(l2, BCSV_CODEC_FLAT);
        TEST_ASSERT(bcsv_writer_open_u(wa, pn, true, BCSV_FORMAT_AUTO, NULL), "post-shutdown writer works");
        bcsv_writer_destroy_u(wa);
        bcsv_layout_destroy(l2);
    }
    bcsv_layout_destroy(layout);
}

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
    test_csv_accessors();
    test_csv_write_external_row();
    test_csv_lz4_roundtrip();

    /* Unified File API (ADR-0009) */
    test_unified_api();

    /* Unified native round-trip + reuse arms (round-2 gaps) */
    test_unified_native_and_reuse();

    /* Cross-format */
    test_cross_format();

    /* Row codec selection */
    test_row_codec_selection_and_flags();

    /* Multi-packet */
    test_multi_packet();

    /* NULL safety */
    test_null_handles();

    /* Row column_count */
    test_row_column_count();

    /* Row Visit API */
    test_row_visit_const();
    test_visit_on_reader_rows();

    /* Sampler API */
    test_sampler_basic();
    test_sampler_selection();
    test_sampler_combined();
    test_sampler_compile_error();

    /* Bulk teardown — LAST: clears the whole handle registry */
    test_unified_shutdown();

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
