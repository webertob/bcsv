/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/*
 * Regression tests for the two 1.5.20 defect reports from the com.bcsv.unity
 * consumer (see CHANGELOG 1.5.20, docs/adr/0006):
 *
 *   Defect 1 — bcsv_writer_destroy double-free aborting a host's process
 *              exit (SIGABRT / status 134, buffered rows lost). Fixed with a
 *              process-wide handle registry: idempotent destroys, borrowed
 *              handles protected, and bcsv_shutdown() bulk teardown.
 *   Defect 2 — typed getters returning 0.0 silently on a mismatched column
 *              type. Fixed per ADR-0006: bcsv_row_get_double widens the
 *              exactly-representable set, every other getter is strict,
 *              bcsv_row_try_get_* are checked twins, and the thread-local
 *              error channel always describes the most recent call.
 *
 * Pure C (.c) to keep the public header compilable by C compilers, mirroring
 * the other C API test programs. Registered with CTest via add_test().
 *
 * The exit-status children re-exec this very binary (argv[1] = mode) so each
 * scenario runs in a pristine process image whose real exit() teardown is
 * what gets asserted — the exact class of failure the Unity consumer hit.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fork/execv/waitpid/readlink in -std=cNN */
#endif
#include "../include/bcsv/bcsv_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>             /* _mkdir */
#define BCSV_MKDIR(p) _mkdir(p)
#else
#define BCSV_MKDIR(p) mkdir((p), 0755)
#endif
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>         /* _NSGetExecutablePath */
#include <stdint.h>
#endif

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("  ok  %s\n", message); \
    } else { \
        printf("  FAIL %s  (last_error: \"%s\")\n", message, bcsv_last_error()); \
    } \
} while (0)

#define TEST_START(name) printf("\n--- %s ---\n", name)

static const char* tmpname(const char* stem) {
    /* Project policy: scratch files live under tmp/, never the repo root.
     * Best-effort mkdir; if it already exists (or the dir is read-only) the
     * open below reports the real problem. */
    (void)BCSV_MKDIR("tmp");
    static char buf[256];
    snprintf(buf, sizeof(buf), "tmp/test_c_api_defects_1520_%s.bcsv", stem);
    return buf;
}

/* ── Getter matrix (ADR-0006 contract) ───────────────────────────────────── */

typedef struct { const char* name; bcsv_type_t type; } ColSpec;

static const ColSpec kCols[] = {
    { "b",   BCSV_TYPE_BOOL   },
    { "u8",  BCSV_TYPE_UINT8  },
    { "u16", BCSV_TYPE_UINT16 },
    { "u32", BCSV_TYPE_UINT32 },
    { "u64", BCSV_TYPE_UINT64 },
    { "i8",  BCSV_TYPE_INT8   },
    { "i16", BCSV_TYPE_INT16  },
    { "i32", BCSV_TYPE_INT32  },
    { "i64", BCSV_TYPE_INT64  },
    { "f32", BCSV_TYPE_FLOAT  },
    { "f64", BCSV_TYPE_DOUBLE },
    { "s",   BCSV_TYPE_STRING },
};
static const int NUM_COLS = (int)(sizeof(kCols) / sizeof(kCols[0]));

static const uint64_t kU64 = 1000000000000000000ull;   /* > 2^53: inexact as double */
static const int64_t  kI64 = -9000000000LL;            /* > 2^53 in magnitude       */

static void set_column(bcsv_row_t row, int col) {
    switch (kCols[col].type) {
        case BCSV_TYPE_BOOL:   bcsv_row_set_bool(row, col, true);          break;
        case BCSV_TYPE_UINT8:  bcsv_row_set_uint8(row, col, 250);          break;
        case BCSV_TYPE_UINT16: bcsv_row_set_uint16(row, col, 65000);       break;
        case BCSV_TYPE_UINT32: bcsv_row_set_uint32(row, col, 4000000000u); break;
        case BCSV_TYPE_UINT64: bcsv_row_set_uint64(row, col, kU64);        break;
        case BCSV_TYPE_INT8:   bcsv_row_set_int8(row, col, -120);          break;
        case BCSV_TYPE_INT16:  bcsv_row_set_int16(row, col, -32000);       break;
        case BCSV_TYPE_INT32:  bcsv_row_set_int32(row, col, -2000000000);  break;
        case BCSV_TYPE_INT64:  bcsv_row_set_int64(row, col, kI64);         break;
        case BCSV_TYPE_FLOAT:  bcsv_row_set_float(row, col, 3.5f);         break;
        case BCSV_TYPE_DOUBLE: bcsv_row_set_double(row, col, 1.0e300);     break;
        default:               bcsv_row_set_string(row, col, "hello");     break;
    }
}

/* Columns whose entire stored domain converts to double exactly: bool, all
 * ints up to 32 bit, float, double. u64/i64 exceed 2^53 → rejected loudly. */
static int widens_to_double(bcsv_type_t t) {
    switch (t) {
        case BCSV_TYPE_BOOL:   case BCSV_TYPE_UINT8:  case BCSV_TYPE_UINT16:
        case BCSV_TYPE_UINT32: case BCSV_TYPE_INT8:   case BCSV_TYPE_INT16:
        case BCSV_TYPE_INT32:  case BCSV_TYPE_FLOAT:  case BCSV_TYPE_DOUBLE:
            return 1;
        default:
            return 0;
    }
}
static double expected_as_double(bcsv_type_t t) {
    switch (t) {
        case BCSV_TYPE_BOOL:   return 1.0;
        case BCSV_TYPE_UINT8:  return 250.0;
        case BCSV_TYPE_UINT16: return 65000.0;
        case BCSV_TYPE_UINT32: return 4000000000.0;
        case BCSV_TYPE_INT8:   return -120.0;
        case BCSV_TYPE_INT16:  return -32000.0;
        case BCSV_TYPE_INT32:  return -2000000000.0;
        case BCSV_TYPE_FLOAT:  return 3.5;
        case BCSV_TYPE_DOUBLE: return 1.0e300;
        default:               return 0.0;
    }
}

/* One cell of the getter x type matrix. Contract (ADR-0006): on a mismatch
 * the getter returns its fallback AND sets the error channel — never silent;
 * on success it returns the true value AND the error channel is empty. */
static int check_get(bcsv_row_t row, int want, int col) {
    const int exact = (want == col);
    const char* err;
    bcsv_clear_last_error();

    switch (kCols[want].type) {
        case BCSV_TYPE_BOOL: {
            bool v = bcsv_row_get_bool(row, col); err = bcsv_last_error();
            return exact ? (v && !err[0]) : (!v && err[0] != '\0');
        }
        case BCSV_TYPE_UINT8: {
            uint8_t v = bcsv_row_get_uint8(row, col); err = bcsv_last_error();
            return exact ? (v == 250 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_UINT16: {
            uint16_t v = bcsv_row_get_uint16(row, col); err = bcsv_last_error();
            return exact ? (v == 65000 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_UINT32: {
            uint32_t v = bcsv_row_get_uint32(row, col); err = bcsv_last_error();
            return exact ? (v == 4000000000u && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_UINT64: {
            uint64_t v = bcsv_row_get_uint64(row, col); err = bcsv_last_error();
            return exact ? (v == kU64 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_INT8: {
            int8_t v = bcsv_row_get_int8(row, col); err = bcsv_last_error();
            return exact ? (v == -120 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_INT16: {
            int16_t v = bcsv_row_get_int16(row, col); err = bcsv_last_error();
            return exact ? (v == -32000 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_INT32: {
            int32_t v = bcsv_row_get_int32(row, col); err = bcsv_last_error();
            return exact ? (v == -2000000000 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_INT64: {
            int64_t v = bcsv_row_get_int64(row, col); err = bcsv_last_error();
            return exact ? (v == kI64 && !err[0]) : (v == 0 && err[0] != '\0');
        }
        case BCSV_TYPE_FLOAT: {
            /* get_float is strict everywhere: a DOUBLE column is an error,
             * never a silent narrowing — not even for exactly-representable
             * values like the 3.5f stored here. */
            float v = bcsv_row_get_float(row, col); err = bcsv_last_error();
            return exact ? (v == 3.5f && !err[0]) : (v == 0.0f && err[0] != '\0');
        }
        case BCSV_TYPE_DOUBLE: {
            double v = bcsv_row_get_double(row, col); err = bcsv_last_error();
            if (widens_to_double(kCols[col].type))
                return (v == expected_as_double(kCols[col].type)) && err[0] == '\0';
            return (v == 0.0) && err[0] != '\0';   /* u64, i64, string: loud */
        }
        case BCSV_TYPE_STRING: {
            const char* s = bcsv_row_get_string(row, col); err = bcsv_last_error();
            return exact ? (s && strcmp(s, "hello") == 0 && !err[0])
                         : (s == NULL && err[0] != '\0');
        }
        default:
            return 0;
    }
}

static void test_getter_matrix(void) {
    TEST_START("Defect 2: getter x type matrix (ADR-0006 contract)");

    bcsv_layout_t layout = bcsv_layout_create();
    for (int c = 0; c < NUM_COLS; ++c)
        bcsv_layout_add_column(layout, (size_t)(c + 1), kCols[c].name, kCols[c].type);

    bcsv_row_t row = bcsv_row_create(layout);
    for (int c = 0; c < NUM_COLS; ++c) set_column(row, c);

    int fails = 0;
    for (int want = 0; want < NUM_COLS; ++want) {
        for (int col = 0; col < NUM_COLS; ++col) {
            if (!check_get(row, want, col)) {
                printf("    contract broken: getter '%s' on column '%s'\n",
                       kCols[want].name, kCols[col].name);
                ++fails;
            }
        }
    }
    TEST_ASSERT(fails == 0, "all 144 getter/type combinations follow the contract");

    /* The consumer's exact case: float poses + uint32 indices via GetDouble. */
    bcsv_clear_last_error();
    TEST_ASSERT(bcsv_row_get_double(row, 9) == 3.5,
                "float column reads through get_double (was silent 0.0)");
    TEST_ASSERT(bcsv_row_get_double(row, 3) == 4000000000.0,
                "uint32 column reads through get_double (was silent 0.0)");

    /* Fresh-channel rule: a success clears a stale error. */
    (void)bcsv_row_get_double(row, 4);                 /* u64 -> strict error */
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "u64 via get_double: loud error");
    double v = bcsv_row_get_double(row, 10);           /* f64 -> exact */
    TEST_ASSERT(v == 1.0e300 && bcsv_last_error()[0] == '\0',
                "success clears the stale error (fresh-channel rule)");

    /* try_get_* checked twins: same type rule as the plain getters, reported
     * as a bool with *out untouched on failure. */
    double dv = -1.0;
    TEST_ASSERT(bcsv_row_try_get_double(row, 9, &dv) && dv == 3.5,
                "try_get_double widens float column (mirrors get_double)");
    TEST_ASSERT(!bcsv_row_try_get_double(row, 4, &dv) && dv == 3.5 &&
                    bcsv_last_error()[0] != '\0',
                "try_get_double rejects u64 column (inexact), *out untouched");
    TEST_ASSERT(!bcsv_row_try_get_double(row, 11, &dv) && dv == 3.5,
                "string column via try_get_double: rejected, *out untouched");
    TEST_ASSERT(bcsv_row_try_get_double(row, 10, &dv) && dv == 1.0e300,
                "try_get_double exact read succeeds");
    float fv = -1.0f;
    TEST_ASSERT(bcsv_row_try_get_float(row, 9, &fv) && fv == 3.5f,
                "try_get_float exact read succeeds");
    const char* sp = (const char*)0x1;                 /* poison sentinel */
    TEST_ASSERT(!bcsv_row_try_get_string(row, 9, &sp) && sp == (const char*)0x1,
                "try_get_string wrong type: false, *out untouched");

    /* Out-of-range columns: loud error, never an out-of-bounds read. */
    v = bcsv_row_get_double(row, NUM_COLS);
    TEST_ASSERT(v == 0.0 && bcsv_last_error()[0] != '\0', "column index == count -> error");
    v = bcsv_row_get_double(row, -3);
    TEST_ASSERT(v == 0.0 && bcsv_last_error()[0] != '\0', "negative column index -> error");
    int32_t tv = 7;
    TEST_ASSERT(!bcsv_row_try_get_int32(row, 100000, &tv) && tv == 7,
                "try_get out-of-range -> false, *out untouched");

    /* Vectorized getters keep the strict rule (no widening there). */
    double dst[2] = { -1.0, -1.0 };
    bcsv_clear_last_error();
    bcsv_row_get_double_array(row, 9, dst, 1);         /* single float col -> strict */
    TEST_ASSERT(dst[0] == -1.0 && bcsv_last_error()[0] != '\0',
                "vectorized get_double_array stays strict (no widening)");

    bcsv_row_destroy(row);
    bcsv_layout_destroy(layout);
}

/* ── Idempotent destroy + borrowed handles (defect 1) ─────────────────────── */

static void test_idempotent_destroy(void) {
    TEST_START("Defect 1: idempotent destroys + borrowed-handle refusal");

    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 1, "x", BCSV_TYPE_INT32);

    bcsv_writer_t w = bcsv_writer_create_delta(layout);
    TEST_ASSERT(bcsv_writer_open(w, tmpname("idempotent"), true, 1, 64,
                                BCSV_FLAG_BATCH_COMPRESS | BCSV_FLAG_DELTA_ENCODING),
                "writer opens");
    bcsv_row_t r = bcsv_writer_row(w);
    bcsv_row_set_int32(r, 0, 42);
    TEST_ASSERT(bcsv_writer_write(w, r), "row written");

    /* Borrowed row handle: destroying it must be refused (registry protects
     * it), and the writer's row must remain usable afterwards. */
    bcsv_row_t own = bcsv_row_clone(r);      /* owned copy, while w is alive */
    bcsv_row_destroy(r);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "destroy of borrowed row handle refused + logged");
    bcsv_row_set_int32(r, 0, 43);
    TEST_ASSERT(bcsv_writer_write(w, r), "writer still functional after refused destroy");
    bcsv_row_destroy(own);
    bcsv_row_destroy(own);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "owned row double-destroy reported, not fatal");

    bcsv_writer_close(w);
    bcsv_clear_last_error();
    bcsv_writer_destroy(w);
    bcsv_writer_destroy(w);                  /* finalizer-style second destroy */
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "writer double-destroy reported, not fatal");
    bcsv_clear_last_error();
    bcsv_writer_flush(w);                    /* close/flush after destroy too */
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-destroy flush reported, not fatal");
    bcsv_clear_last_error();
    bcsv_writer_close(w);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-destroy close reported, not fatal");
    bcsv_writer_destroy(NULL);               /* NULL is a documented no-op */

    bcsv_layout_destroy(layout);
    bcsv_layout_destroy(layout);

    /* The file is intact: 2 rows, correct values. */
    bcsv_reader_t rd = bcsv_reader_create();
    TEST_ASSERT(bcsv_reader_open(rd, tmpname("idempotent")), "file reopens");
    TEST_ASSERT(bcsv_reader_count_rows(rd) == 2, "both rows stored");
    (void)bcsv_reader_read(rd, 1);
    TEST_ASSERT(bcsv_row_get_int32(bcsv_reader_row(rd), 0) == 43, "second row decodes");
    bcsv_reader_destroy(rd);
}

/* ── bcsv_shutdown() bulk teardown ────────────────────────────────────────── */

static void test_shutdown_bulk(void) {
    TEST_START("Defect 1b: bcsv_shutdown closes writers and frees everything");

    /* Leak a pile of handles deliberately — this is what a host without
     * Dispose discipline arrives at. bcsv_shutdown() must close the open
     * writers (footer on disk!) and free everything, then stay idempotent. */
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 1, "x", BCSV_TYPE_INT32);

    bcsv_writer_t w1 = bcsv_writer_create_delta(layout);
    TEST_ASSERT(bcsv_writer_open(w1, tmpname("bulk1"), true, 1, 64,
                                 BCSV_FLAG_BATCH_COMPRESS | BCSV_FLAG_DELTA_ENCODING),
                "bulk writer 1 opens");
    /* compress=1 with FLAG_NONE resolves to the footer-carrying packet codec,
     * so count_rows() is meaningful below. */
    bcsv_writer_t w2 = bcsv_writer_create(layout);
    TEST_ASSERT(bcsv_writer_open(w2, tmpname("bulk2"), true, 1, 64, BCSV_FLAG_NONE),
                "bulk writer 2 opens");

    int all_ok = 1;
    for (int i = 0; i < 100; ++i) {
        bcsv_row_t r1 = bcsv_writer_row(w1);
        bcsv_row_set_int32(r1, 0, i);
        bcsv_row_t r2 = bcsv_writer_row(w2);
        bcsv_row_set_int32(r2, 0, i);
        if (!bcsv_writer_write(w1, r1) || !bcsv_writer_write(w2, r2)) all_ok = 0;
    }
    TEST_ASSERT(all_ok, "100 rows into each leaked writer");

    /* A leaked reader + orphan row, too. */
    bcsv_reader_t rd = bcsv_reader_create();
    (void)bcsv_reader_open(rd, tmpname("bulk1"));
    bcsv_row_t orphan = bcsv_row_create(layout);

    bcsv_clear_last_error();
    bcsv_shutdown();
    TEST_ASSERT(bcsv_last_error()[0] == '\0', "bcsv_shutdown completes clean");

    /* The leaked writers were closed with valid footers → fully readable. */
    bcsv_reader_t chk = bcsv_reader_create();
    TEST_ASSERT(bcsv_reader_open(chk, tmpname("bulk1")), "bulk1 reopens after shutdown");
    TEST_ASSERT(bcsv_reader_count_rows(chk) == 100, "shutdown wrote a real footer (delta)");
    (void)bcsv_reader_read(chk, 99);
    TEST_ASSERT(bcsv_row_get_int32(bcsv_reader_row(chk), 0) == 99, "last row decodes");
    bcsv_reader_destroy(chk);

    chk = bcsv_reader_create();
    TEST_ASSERT(bcsv_reader_open(chk, tmpname("bulk2")), "bulk2 reopens after shutdown");
    TEST_ASSERT(bcsv_reader_count_rows(chk) == 100, "packet file closed too");
    bcsv_reader_destroy(chk);

    /* All pre-shutdown handles are dead now. The finalizer ordering the
     * managed layer cannot control (Dispose = close BEFORE destroy) must be
     * safe here too: close and flush follow the same registry rule as
     * destroy — a logged no-op, never a touch of freed memory. */
    bcsv_clear_last_error();
    bcsv_writer_flush(w1);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-shutdown flush: logged no-op");
    bcsv_clear_last_error();
    bcsv_writer_close(w1);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-shutdown writer close: logged no-op");
    bcsv_clear_last_error();
    bcsv_writer_destroy(w1);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-shutdown destroy: logged no-op");
    bcsv_clear_last_error();
    bcsv_reader_close(rd);
    TEST_ASSERT(bcsv_last_error()[0] != '\0', "post-shutdown reader close: logged no-op");
    bcsv_reader_destroy(rd);
    bcsv_row_destroy(orphan);
    bcsv_layout_destroy(layout);

    bcsv_clear_last_error();
    bcsv_shutdown();
    TEST_ASSERT(bcsv_last_error()[0] == '\0', "second shutdown is a clean no-op");
}

/* ── Poison visibility through the C API (B1 companion) ───────────────────── */

static void test_poison_api(void) {
    TEST_START("Writer poison API + empty-boundary flush (B1)");

    /* C surface of gtest FlushDepoisonsAtEmptyPacketBoundary: a first row of
     * 300 max-length strings exceeds MAX_ROW_LENGTH. With the batch LZ4 file
     * codec the row is rejected while the packet buffer is still empty, so a
     * flush() is a legal resync point: it must clear the poison (1.5.19 left
     * the writer permanently write-refused). */
    enum { N = 300 };
    bcsv_layout_t layout = bcsv_layout_create();
    char nmn[8];
    for (int i = 0; i < N; ++i) {
        snprintf(nmn, sizeof(nmn), "s%d", i);
        bcsv_layout_add_column(layout, (size_t)(i + 1), nmn, BCSV_TYPE_STRING);
    }
    static char big[65536];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    bcsv_writer_t w = bcsv_writer_create_zoh(layout);
    TEST_ASSERT(bcsv_writer_open(w, tmpname("poison"), true, 1, 64,
                                 (bcsv_file_flags_t)(BCSV_FLAG_ZOH | BCSV_FLAG_BATCH_COMPRESS)),
                "poison writer opens");
    TEST_ASSERT(!bcsv_writer_is_poisoned(w), "fresh writer not poisoned");

    bcsv_row_t r = bcsv_writer_row(w);
    for (int i = 0; i < N; ++i) bcsv_row_set_string(r, i, big);
    TEST_ASSERT(!bcsv_writer_write(w, r), "oversized row rejected");
    TEST_ASSERT(bcsv_writer_is_poisoned(w), "is_poisoned reports true after rejection");
    for (int i = 0; i < N; ++i) bcsv_row_set_string(r, i, "ok");
    TEST_ASSERT(!bcsv_writer_write(w, r), "small rows refused while poisoned");

    bcsv_writer_flush(w);
    TEST_ASSERT(!bcsv_writer_is_poisoned(w), "flush() at empty packet boundary de-poisons");
    TEST_ASSERT(bcsv_writer_write(w, r), "writing resumes after flush()");
    TEST_ASSERT(bcsv_writer_next(w), "next() after flush writes the internal row");

    bcsv_writer_close(w);
    bcsv_writer_destroy(w);

    bcsv_reader_t rd = bcsv_reader_create();
    TEST_ASSERT(bcsv_reader_open(rd, tmpname("poison")), "file reopens");
    TEST_ASSERT(bcsv_reader_count_rows(rd) == 2, "exactly the valid rows stored");
    bcsv_reader_destroy(rd);
    bcsv_layout_destroy(layout);
}

/* ── Exit-status children: the consumer's tripwire (POSIX) ────────────────── */
/* The Unity consumer's abort happened while the *process* tore the library
 * down at exit. These children re-exec this binary in one of the modes below
 * (fork+execv → pristine image), run a scenario, and call exit(0). Their
 * exit STATUS is what a CI smoke asserts: 0, no signal. */

#ifndef _WIN32

static bcsv_writer_t g_child_writer;   /* torn down via atexit */

/* Open a batch-LZ4 delta writer, write 500 rows, leave it OPEN. */
static bcsv_writer_t child_open_writer(const char* stem) {
    bcsv_layout_t layout = bcsv_layout_create();
    bcsv_layout_add_column(layout, 1, "x", BCSV_TYPE_INT32);
    bcsv_writer_t w = bcsv_writer_create_delta(layout);
    if (!bcsv_writer_open(w, tmpname(stem), true, 1, 64,
                          BCSV_FLAG_BATCH_COMPRESS | BCSV_FLAG_DELTA_ENCODING)) {
        fprintf(stderr, "child: open: %s\n", bcsv_last_error());
        return NULL;
    }
    for (int i = 0; i < 500; ++i) {
        bcsv_row_t r = bcsv_writer_row(w);
        bcsv_row_set_int32(r, 0, i);
        if (!bcsv_writer_write(w, r)) {
            fprintf(stderr, "child: write: %s\n", bcsv_last_error());
            return NULL;
        }
    }
    return w;
}

static void atexit_destroy_writer(void) { bcsv_writer_destroy(g_child_writer); }
static void atexit_shutdown(void)       { bcsv_shutdown(); }

/* C analogue of the managed finalizer path: destroy at exit (the Writer
 * destructor closes it, footer on disk — this aborted in 1.5.19). */
static int mode_atexit_destroy(const char* stem) {
    g_child_writer = child_open_writer(stem);
    if (!g_child_writer) return 1;
    atexit(atexit_destroy_writer);
    return 0;
}

/* Quit path: close now (OnApplicationQuit), finalizer destroys later. */
static int mode_close_then_finalize(const char* stem) {
    g_child_writer = child_open_writer(stem);
    if (!g_child_writer) return 1;
    bcsv_writer_close(g_child_writer);
    atexit(atexit_destroy_writer);
    return 0;
}

/* The residual window from the 1.5.20 review: shutdown runs at exit (it
 * closes and destroys the leaked writer), and a LATER hook — the managed
 * finalizer's close-before-destroy step, replayed here as a late close()
 * through the stale pointer — then touches the dead handle. atexit runs
 * LIFO: register the late close FIRST so shutdown (registered second) runs
 * before it. The guard must turn the late close into a logged no-op;
 * unfixed, this is a use-after-free abort (ASan-verified). */
static void atexit_close_writer(void) {
    bcsv_writer_close(g_child_writer);
    bcsv_writer_flush(g_child_writer);
}
static int mode_shutdown_then_close(const char* stem) {
    g_child_writer = child_open_writer(stem);
    if (!g_child_writer) return 1;
    atexit(atexit_close_writer);   /* runs LAST (LIFO) — after shutdown */
    atexit(atexit_shutdown);       /* runs first: claims + frees handles */
    return 0;
}

/* Recommended host integration: bcsv_shutdown() at exit, everything leaked.
 * atexit-registered last ⇒ runs first, closes open writers before teardown. */
static int mode_shutdown_at_exit(const char* stem) {
    if (!child_open_writer(stem)) return 1;
    atexit(atexit_shutdown);
    return 0;
}

/* Worst case (the 1.5.19 scenario): no Dispose discipline, no shutdown at
 * all. Every handle still live when exit() runs — must not abort. Data
 * safety is not claimed here, so this file is not re-read. */
static int mode_all_leaked(const char* stem) {
    (void)child_open_writer(stem);
    (void)bcsv_layout_create();
    (void)bcsv_reader_create();
    return 0;
}

/* Re-exec the own image with [mode, stem]; assert the child exits cleanly.
 * Own path: /proc/self/exe on Linux, _NSGetExecutablePath on macOS — the
 * first macOS CI run of this file lost all five children to ENOENT. */
static int self_exe_path(char* exe, size_t cap) {
#ifdef __APPLE__
    uint32_t n = (uint32_t)cap;
    if (_NSGetExecutablePath(exe, &n) != 0) return 0;
    return 1;
#else
    ssize_t n = readlink("/proc/self/exe", exe, cap - 1);
    if (n <= 0) return 0;
    exe[n] = '\0';
    return 1;
#endif
}

static void run_child_mode(const char* label, const char* mode, const char* stem) {
    char exe[4096];
    if (!self_exe_path(exe, sizeof(exe))) { TEST_ASSERT(0, "resolve own executable"); return; }

    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        char* argv[] = { exe, (char*)mode, (char*)stem, NULL };
        execv(exe, argv);
        _exit(127);
    }
    if (pid < 0) { TEST_ASSERT(0, "fork failed"); return; }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) { /* EINTR */ }
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: exit status 0, no signal", label);
    TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, msg);
}

static void test_exit_status_children(void) {
    TEST_START("Defect 1: real process-exit children (consumer tripwire)");
    run_child_mode("destroy writer at exit", "c_atexit_destroy", "x_atexit");
    run_child_mode("close then finalizer destroy", "c_close_finalize", "x_quit");
    run_child_mode("bcsv_shutdown at exit", "c_shutdown_exit", "x_shutdown");
    run_child_mode("everything leaked, bare exit()", "c_all_leaked", "x_leaked");
    run_child_mode("close after shutdown (late finalizer)", "c_shutdown_close", "x_shutclose");
}

static void test_child_files_readable(void) {
    TEST_START("Teardown across process exit left complete files");
    /* x_leaked never closed — intentionally unchecked. */
    const char* stems[] = { "x_atexit", "x_quit", "x_shutdown", "x_shutclose" };
    for (size_t i = 0; i < sizeof(stems) / sizeof(stems[0]); ++i) {
        bcsv_reader_t rd = bcsv_reader_create();
        int opened = bcsv_reader_open(rd, tmpname(stems[i]));
        size_t rows = opened ? bcsv_reader_count_rows(rd) : 0;
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: complete footer, all 500 rows", stems[i]);
        TEST_ASSERT(opened && rows == 500, msg);
        if (opened && rows == 500) {
            (void)bcsv_reader_read(rd, 499);
            TEST_ASSERT(bcsv_row_get_int32(bcsv_reader_row(rd), 0) == 499, "last row intact");
        }
        bcsv_reader_destroy(rd);
    }
}

#endif /* !_WIN32 */

int main(int argc, char** argv) {
    if (argc == 3) {   /* child mode requested via re-exec */
        const char* mode = argv[1];
#ifndef _WIN32
        const char* stem = argv[2];
        if (!strcmp(mode, "c_atexit_destroy")) return mode_atexit_destroy(stem);
        if (!strcmp(mode, "c_close_finalize")) return mode_close_then_finalize(stem);
        if (!strcmp(mode, "c_shutdown_exit"))  return mode_shutdown_at_exit(stem);
        if (!strcmp(mode, "c_all_leaked"))     return mode_all_leaked(stem);
        if (!strcmp(mode, "c_shutdown_close")) return mode_shutdown_then_close(stem);
#endif
        fprintf(stderr, "unknown child mode '%s'\n", mode);
        return 2;
    }

    printf("=== BCSV C API defect-regression tests (1.5.20) ===\n");
    test_getter_matrix();
    test_idempotent_destroy();
    test_shutdown_bulk();
    test_poison_api();
#ifndef _WIN32
    test_exit_status_children();
    test_child_files_readable();
#endif

    printf("\n%d/%d assertions passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
