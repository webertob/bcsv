/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include "bcsv/bcsv_c_api.h"
#include <string>
#include <sstream>
#include <exception>

// Include full implementations (headers + .hpp files)
#include "bcsv/bcsv.h"  // This includes all implementations

// Sampler includes
#include "bcsv/sampler/sampler.h"
#include "bcsv/sampler/sampler.hpp"

namespace {
thread_local std::string g_last_error;
thread_local bool g_has_error = false;     // flag-based: avoids string::clear() per call
thread_local std::string g_fmt_buf;        // reusable buffer for to_string helpers
thread_local std::string g_fmt_version;    // reusable buffer for format version string

inline void clear_last_error() noexcept {
    g_has_error = false;
}

void set_last_error(const char* where, const std::exception& ex) noexcept {
    g_has_error = true;
    g_last_error = std::string(where) + ": " + ex.what();
}

void set_last_error_unknown(const char* where) noexcept {
    g_has_error = true;
    g_last_error = std::string(where) + ": unknown exception";
}

/// Returns true (and sets error) when @p h is nullptr.
static bool null_handle(const char* where, const void* h) noexcept {
    if (__builtin_expect(!h, 0)) {
        g_has_error = true;
        g_last_error = std::string(where) + ": NULL handle";
        return true;
    }
    return false;
}

// ---- Writer handle: pre-bound function pointers (select-once pattern) ------
// Mirrors the RowCodecDispatch approach used in the C++ Reader: function
// pointers are wired once at construction time, so every subsequent call goes
// through a single indirect call instead of a per-call switch/case.

struct WriterHandle {
    enum class Type { Flat, ZoH, Delta } type;   // kept for open() flag auto-set
    void* ptr;                                    // concrete Writer<L,Codec>*
    bcsv::Row* cached_row;                        // stable pointer to writer's Row

    // Pre-bound trampolines — set once by createWriterHandle()
    void   (*writeRow_fn)(void*);
    void   (*write_fn)(void*, const bcsv::Row&);
    void   (*close_fn)(void*);
    void   (*flush_fn)(void*);
    void   (*delete_fn)(void*);
    bool   (*isOpen_fn)(const void*);
    size_t (*rowCount_fn)(const void*);
    const bcsv::Layout* (*layout_fn)(const void*);
    const std::string*  (*errorMsg_fn)(const void*);
    uint8_t (*compressionLevel_fn)(const void*);
#ifdef _WIN32
    const wchar_t* (*filePath_fn)(const void*);
#else
    const char* (*filePath_fn)(const void*);
#endif
    bool (*open_fn)(void*, const char*, bool, size_t, size_t, bcsv::FileFlags);
};

/// Factory: wires all function pointers once for a concrete Writer type W.
template<typename W>
WriterHandle* createWriterHandle(WriterHandle::Type type, W* writer) {
    auto* h = new WriterHandle();
    h->type       = type;
    h->ptr        = writer;
    h->cached_row = &writer->row();
    h->writeRow_fn = [](void* p) { static_cast<W*>(p)->writeRow(); };
    h->write_fn    = [](void* p, const bcsv::Row& r) { static_cast<W*>(p)->write(r); };
    h->close_fn    = [](void* p) { static_cast<W*>(p)->close(); };
    h->flush_fn    = [](void* p) { static_cast<W*>(p)->flush(); };
    h->delete_fn   = [](void* p) { delete static_cast<W*>(p); };
    h->isOpen_fn   = [](const void* p) -> bool    { return static_cast<const W*>(p)->isOpen(); };
    h->rowCount_fn = [](const void* p) -> size_t  { return static_cast<const W*>(p)->rowCount(); };
    h->layout_fn   = [](const void* p) -> const bcsv::Layout*  { return &static_cast<const W*>(p)->layout(); };
    h->errorMsg_fn = [](const void* p) -> const std::string*   { return &static_cast<const W*>(p)->getErrorMsg(); };
    h->compressionLevel_fn = [](const void* p) -> uint8_t { return static_cast<const W*>(p)->compressionLevel(); };
    h->filePath_fn = [](const void* p) { return static_cast<const W*>(p)->filePath().c_str(); };
    h->open_fn     = [](void* p, const char* fn, bool ow, size_t cl, size_t bs, bcsv::FileFlags ff) -> bool {
        return static_cast<W*>(p)->open(fn, ow, cl, bs, ff);
    };
    return h;
}

// ---- Sampler handle -------------------------------------------------------
struct SamplerHandle {
    bcsv::Sampler<bcsv::Layout>* sampler;
    std::string                  error_msg;  // cached error message
};

} // namespace

#define BCSV_CAPI_TRY_RETURN(where, fallback, expr) \
    try { \
        clear_last_error(); \
        return (expr); \
    } catch (const std::exception& ex) { \
        set_last_error(where, ex); \
        return (fallback); \
    } catch (...) { \
        set_last_error_unknown(where); \
        return (fallback); \
    }

#define BCSV_CAPI_TRY_VOID(where, stmt) \
    try { \
        clear_last_error(); \
        stmt; \
    } catch (const std::exception& ex) { \
        set_last_error(where, ex); \
    } catch (...) { \
        set_last_error_unknown(where); \
    }

// Lean hot-path macros for row get/set — skip null-handle check and error
// clearing.  Row handles are always obtained from a writer/reader that already
// validated its own handle, so the null check is redundant.  Error state is
// only set on exception (programming error: bad column index / type mismatch).
// The try/catch is required to prevent exceptions propagating through extern "C".

#define BCSV_ROW_GET(fallback, expr) \
    try { \
        return (expr); \
    } catch (const std::exception& ex) { \
        set_last_error(__func__, ex); \
        return (fallback); \
    } catch (...) { \
        set_last_error_unknown(__func__); \
        return (fallback); \
    }

#define BCSV_ROW_SET(stmt) \
    try { \
        stmt; \
    } catch (const std::exception& ex) { \
        set_last_error(__func__, ex); \
    } catch (...) { \
        set_last_error_unknown(__func__); \
    }

extern "C" {

// ============================================================================
// Version API
// ============================================================================
const char* bcsv_version(void) {
    return bcsv::version::STRING;
}
int bcsv_version_major(void) { return bcsv::VERSION_MAJOR; }
int bcsv_version_minor(void) { return bcsv::VERSION_MINOR; }
int bcsv_version_patch(void) { return bcsv::VERSION_PATCH; }
const char* bcsv_format_version(void) {
    g_fmt_version = std::to_string(bcsv::BCSV_FORMAT_VERSION_MAJOR) + "." +
                    std::to_string(bcsv::BCSV_FORMAT_VERSION_MINOR) + "." +
                    std::to_string(bcsv::BCSV_FORMAT_VERSION_PATCH);
    return g_fmt_version.c_str();
}

// ============================================================================
// Layout API
// ============================================================================
bcsv_layout_t bcsv_layout_create() {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_create", nullptr, new bcsv::Layout())
}

bcsv_layout_t bcsv_layout_clone(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_clone", layout)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_clone", nullptr, new bcsv::Layout(static_cast<const bcsv::Layout*>(layout)->clone()))
}

void bcsv_layout_destroy(bcsv_layout_t layout) {
    if (!layout) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_destroy", delete static_cast<bcsv::Layout*>(layout))
}

bool bcsv_layout_has_column(const_bcsv_layout_t layout, const char* name) {
    if (null_handle("bcsv_layout_has_column", layout)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_has_column", false, static_cast<const bcsv::Layout*>(layout)->hasColumn(name))
}

size_t bcsv_layout_column_count(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_column_count", layout)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_count", 0u, static_cast<const bcsv::Layout*>(layout)->columnCount())
}

size_t bcsv_layout_column_index(const_bcsv_layout_t layout, const char* name) {
    if (null_handle("bcsv_layout_column_index", layout)) return SIZE_MAX;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_index", SIZE_MAX, static_cast<const bcsv::Layout*>(layout)->columnIndex(name))
}

const char* bcsv_layout_column_name(const_bcsv_layout_t layout, size_t index) {
    if (null_handle("bcsv_layout_column_name", layout)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_name", static_cast<const char*>(nullptr), static_cast<const bcsv::Layout*>(layout)->columnName(index).c_str())
}

bcsv_type_t bcsv_layout_column_type(const_bcsv_layout_t layout, size_t index) {
    if (null_handle("bcsv_layout_column_type", layout)) return (bcsv_type_t)255;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_type", (bcsv_type_t)255, (bcsv_type_t)static_cast<const bcsv::Layout*>(layout)->columnType(index))
}

bool bcsv_layout_set_column_name(bcsv_layout_t layout, size_t index, const char* name) {
    if (null_handle("bcsv_layout_set_column_name", layout)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_set_column_name", false, (static_cast<bcsv::Layout*>(layout)->setColumnName(index, name), true))
}

void bcsv_layout_set_column_type(bcsv_layout_t layout, size_t index, bcsv_type_t type) {
    if (null_handle("bcsv_layout_set_column_type", layout)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_set_column_type", static_cast<bcsv::Layout*>(layout)->setColumnType(index, static_cast<bcsv::ColumnType>(type)))
}

bool bcsv_layout_add_column(bcsv_layout_t layout, size_t index, const char* name, bcsv_type_t type) {
    if (null_handle("bcsv_layout_add_column", layout)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_add_column", false, ([&]() {
        bcsv::ColumnDefinition colDef = {name, static_cast<bcsv::ColumnType>(type)};
        static_cast<bcsv::Layout*>(layout)->addColumn(colDef, index);
        return true;
    })())
}

void bcsv_layout_remove_column(bcsv_layout_t layout, size_t index) {
    if (null_handle("bcsv_layout_remove_column", layout)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_remove_column", static_cast<bcsv::Layout*>(layout)->removeColumn(index))
}

void bcsv_layout_clear(bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_clear", layout)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_clear", static_cast<bcsv::Layout*>(layout)->clear())
}

bool bcsv_layout_is_compatible(const_bcsv_layout_t layout1, const_bcsv_layout_t layout2) {
    if (null_handle("bcsv_layout_is_compatible", layout1) || null_handle("bcsv_layout_is_compatible", layout2)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_is_compatible", false, ([&]() {
        auto& l1 = *static_cast<const bcsv::Layout*>(layout1);
        auto& l2 = *static_cast<const bcsv::Layout*>(layout2);
        return l1.isCompatible(l2);
    })())
}

void bcsv_layout_assign(bcsv_layout_t dest, const_bcsv_layout_t src) {
    if (null_handle("bcsv_layout_assign", dest) || null_handle("bcsv_layout_assign", src)) return;
    BCSV_CAPI_TRY_VOID("bcsv_layout_assign", ([&]() {
        auto& d = *static_cast<bcsv::Layout*>(dest);
        const auto& s = *static_cast<const bcsv::Layout*>(src);
        /* Use clear + addColumn to deep-copy (default operator= is shallow). */
        d.clear();
        for (size_t i = 0; i < s.columnCount(); ++i) {
            d.addColumn({s.columnName(i), s.columnType(i)}, i);
        }
    })())
}

size_t bcsv_layout_column_count_by_type(const_bcsv_layout_t layout, bcsv_type_t type) {
    if (null_handle("bcsv_layout_column_count_by_type", layout)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_count_by_type", 0u, ([&]() -> size_t {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        size_t count = 0;
        auto ct = static_cast<bcsv::ColumnType>(type);
        for (size_t i = 0; i < l->columnCount(); ++i) {
            if (l->columnType(i) == ct) ++count;
        }
        return count;
    })())
}

const char* bcsv_layout_to_string(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_layout_to_string", layout)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_layout_to_string", "", ([&]() -> const char* {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        std::ostringstream oss;
        oss << *l;
        g_fmt_buf = oss.str();
        return g_fmt_buf.c_str();
    })())
}

// ============================================================================
// Reader API
// ============================================================================
bcsv_reader_t bcsv_reader_create(void) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_create", nullptr, new bcsv::ReaderDirectAccess<bcsv::Layout>())
}

void bcsv_reader_destroy(bcsv_reader_t reader) {
    if (!reader) return;
    BCSV_CAPI_TRY_VOID("bcsv_reader_destroy", delete static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader))
}

void bcsv_reader_close(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_close", reader)) return;
    BCSV_CAPI_TRY_VOID("bcsv_reader_close", static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->close())
}

size_t bcsv_reader_count_rows(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_count_rows", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_count_rows", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->rowCount())
}

bool bcsv_reader_open(bcsv_reader_t reader, const char* filename) {
    if (null_handle("bcsv_reader_open", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->open(filename);
        if (ok) {
            clear_last_error();
        } else {
            g_last_error = r->getErrorMsg();
        }
        return ok;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_reader_open", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_reader_open");
        return false;
    }
}

bool bcsv_reader_is_open(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_is_open", reader)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_is_open", false, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_reader_filename(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename", static_cast<const wchar_t*>(nullptr), ([&]() {
        const auto* r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& path = r->filePath();
        return path.c_str();
    })())
}
#else
const char* bcsv_reader_filename(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename", static_cast<const char*>(nullptr), ([&]() {
        const auto* r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& path = r->filePath();
        return path.c_str();
    })())
}
#endif

const_bcsv_layout_t bcsv_reader_layout(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_layout", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_layout", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto l = &(r->layout());
        return reinterpret_cast<const_bcsv_layout_t>(l);
    })())
}

bool bcsv_reader_next(bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_next", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->readNext();
        if (__builtin_expect(!ok, 0)) {
            const auto& msg = r->getErrorMsg();
            if (!msg.empty()) { g_has_error = true; g_last_error = msg; }
            // lean: no clear_last_error on success or empty-error EOF
        }
        return ok;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_reader_next", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_reader_next");
        return false;
    }
}

const_bcsv_row_t bcsv_reader_row(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_row", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_row", static_cast<const_bcsv_row_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto x = &(r->row());
        return reinterpret_cast<const_bcsv_row_t>(x);
    })())
}
size_t bcsv_reader_index(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_index", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_index", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->rowPos())
}

bool bcsv_reader_open_ex(bcsv_reader_t reader, const char* filename, bool rebuild_footer) {
    if (null_handle("bcsv_reader_open_ex", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->open(filename, rebuild_footer);
        if (ok) { clear_last_error(); } else { g_last_error = r->getErrorMsg(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_reader_open_ex", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_reader_open_ex"); return false; }
}

bool bcsv_reader_read(bcsv_reader_t reader, size_t index) {
    if (null_handle("bcsv_reader_read", reader)) return false;
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->read(index);
        if (!ok && !r->getErrorMsg().empty()) { g_last_error = r->getErrorMsg(); } else { clear_last_error(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_reader_read", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_reader_read"); return false; }
}

const char* bcsv_reader_error_msg(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_error_msg", reader)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_reader_error_msg", "", static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->getErrorMsg().c_str())
}

uint8_t bcsv_reader_compression_level(const_bcsv_reader_t reader) {
    if (null_handle("bcsv_reader_compression_level", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_reader_compression_level", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->compressionLevel())
}

// ============================================================================
// Writer API
// ============================================================================
bcsv_writer_t bcsv_writer_create(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create", nullptr, ([&]() {
        bcsv::Layout empty;
        auto& l = layout ? *static_cast<bcsv::Layout*>(layout) : empty;
        return reinterpret_cast<bcsv_writer_t>(
            createWriterHandle(WriterHandle::Type::Flat,
                               new bcsv::Writer<bcsv::Layout>(l)));
    })())
}

bcsv_writer_t bcsv_writer_create_zoh(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create_zoh", nullptr, ([&]() {
        bcsv::Layout empty;
        auto& l = layout ? *static_cast<bcsv::Layout*>(layout) : empty;
        return reinterpret_cast<bcsv_writer_t>(
            createWriterHandle(WriterHandle::Type::ZoH,
                               new bcsv::WriterZoH<bcsv::Layout>(l)));
    })())
}

bcsv_writer_t bcsv_writer_create_delta(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create_delta", nullptr, ([&]() {
        bcsv::Layout empty;
        auto& l = layout ? *static_cast<bcsv::Layout*>(layout) : empty;
        return reinterpret_cast<bcsv_writer_t>(
            createWriterHandle(WriterHandle::Type::Delta,
                               new bcsv::WriterDelta<bcsv::Layout>(l)));
    })())
}

void bcsv_writer_destroy(bcsv_writer_t writer) {
    if (!writer) return;
    BCSV_CAPI_TRY_VOID("bcsv_writer_destroy", ([&]() {
        auto* h = static_cast<WriterHandle*>(writer);
        h->delete_fn(h->ptr);
        delete h;
    })())
}

void bcsv_writer_close(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_close", writer)) return;
    auto* h = static_cast<WriterHandle*>(writer);
    BCSV_CAPI_TRY_VOID("bcsv_writer_close", h->close_fn(h->ptr))
}

void bcsv_writer_flush(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_flush", writer)) return;
    auto* h = static_cast<WriterHandle*>(writer);
    BCSV_CAPI_TRY_VOID("bcsv_writer_flush", h->flush_fn(h->ptr))
}

bool bcsv_writer_open(bcsv_writer_t writer, const char* filename, bool overwrite, int compress, int block_size_kb, bcsv_file_flags_t flags) {
    if (null_handle("bcsv_writer_open", writer)) return false;
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        // Auto-set codec flags so the file header is correct
        if (h->type == WriterHandle::Type::ZoH) {
            flags = static_cast<bcsv_file_flags_t>(flags | BCSV_FLAG_ZOH);
        } else if (h->type == WriterHandle::Type::Delta) {
            flags = static_cast<bcsv_file_flags_t>(flags | BCSV_FLAG_DELTA_ENCODING);
        }
        bool ok = h->open_fn(h->ptr, filename, overwrite,
                             static_cast<size_t>(compress),
                             static_cast<size_t>(block_size_kb),
                             static_cast<bcsv::FileFlags>(flags));
        if (ok) {
            clear_last_error();
        } else {
            g_has_error = true;
            g_last_error = *h->errorMsg_fn(h->ptr);
        }
        return ok;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_open", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_open");
        return false;
    }
}

bool bcsv_writer_is_open(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_is_open", writer)) return false;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_is_open", false, h->isOpen_fn(h->ptr))
}

#ifdef _WIN32
const wchar_t* bcsv_writer_filename(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_filename", writer)) return nullptr;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename", static_cast<const wchar_t*>(nullptr), h->filePath_fn(h->ptr))
}
#else
const char* bcsv_writer_filename(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_filename", writer)) return nullptr;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename", static_cast<const char*>(nullptr), h->filePath_fn(h->ptr))
}
#endif

const_bcsv_layout_t bcsv_writer_layout(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_layout", writer)) return nullptr;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_layout", static_cast<const_bcsv_layout_t>(nullptr),
        static_cast<const_bcsv_layout_t>(h->layout_fn(h->ptr)))
}

bool bcsv_writer_next(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_next", writer)) return false;
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        h->writeRow_fn(h->ptr);   // single indirect call, no switch
        return true;              // lean: no clear_last_error (matches BCSV_ROW_SET/GET)
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_next", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_next");
        return false;
    }
}

bcsv_row_t bcsv_writer_row(bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_row", writer)) return nullptr;
    return static_cast<bcsv_row_t>(static_cast<WriterHandle*>(writer)->cached_row);
}

size_t bcsv_writer_index(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_index", writer)) return 0u;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_index", 0u, h->rowCount_fn(h->ptr))
}

bool bcsv_writer_write(bcsv_writer_t writer, const_bcsv_row_t row) {
    if (null_handle("bcsv_writer_write", writer) || null_handle("bcsv_writer_write", row)) return false;
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        h->write_fn(h->ptr, *static_cast<const bcsv::Row*>(row));
        return true;
    } catch (const std::exception& ex) { set_last_error("bcsv_writer_write", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_writer_write"); return false; }
}

const char* bcsv_writer_error_msg(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_error_msg", writer)) return "";
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_error_msg", "", h->errorMsg_fn(h->ptr)->c_str())
}

uint8_t bcsv_writer_compression_level(const_bcsv_writer_t writer) {
    if (null_handle("bcsv_writer_compression_level", writer)) return 0u;
    auto* h = static_cast<const WriterHandle*>(writer);
    BCSV_CAPI_TRY_RETURN("bcsv_writer_compression_level", 0u, h->compressionLevel_fn(h->ptr))
}

// ============================================================================
// CSV Reader API
// ============================================================================
bcsv_csv_reader_t bcsv_csv_reader_create(bcsv_layout_t layout, char delimiter, char decimal_sep) {
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_create", nullptr, ([&]() -> bcsv_csv_reader_t {
        if (layout) {
            return static_cast<bcsv_csv_reader_t>(new bcsv::CsvReader<bcsv::Layout>(
                *static_cast<bcsv::Layout*>(layout), delimiter, decimal_sep));
        } else {
            bcsv::Layout empty;
            return static_cast<bcsv_csv_reader_t>(new bcsv::CsvReader<bcsv::Layout>(empty, delimiter, decimal_sep));
        }
    })())
}

void bcsv_csv_reader_destroy(bcsv_csv_reader_t reader) {
    if (!reader) return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_reader_destroy", delete static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader))
}

bool bcsv_csv_reader_open(bcsv_csv_reader_t reader, const char* filename, bool has_header) {
    if (null_handle("bcsv_csv_reader_open", reader)) return false;
    try {
        auto* r = static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader);
        bool ok = r->open(filename, has_header);
        if (ok) { clear_last_error(); } else { g_last_error = r->getErrorMsg(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_reader_open", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_reader_open"); return false; }
}

void bcsv_csv_reader_close(bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_close", reader)) return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_reader_close", static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader)->close())
}

bool bcsv_csv_reader_is_open(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_is_open", reader)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_is_open", false, static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_csv_reader_filename(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_filename", static_cast<const wchar_t*>(nullptr),
        static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->filePath().c_str())
}
#else
const char* bcsv_csv_reader_filename(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_filename", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_filename", static_cast<const char*>(nullptr),
        static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->filePath().c_str())
}
#endif

const_bcsv_layout_t bcsv_csv_reader_layout(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_layout", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_layout", static_cast<const_bcsv_layout_t>(nullptr),
        reinterpret_cast<const_bcsv_layout_t>(&static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->layout()))
}

bool bcsv_csv_reader_next(bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_next", reader)) return false;
    try {
        auto* r = static_cast<bcsv::CsvReader<bcsv::Layout>*>(reader);
        bool ok = r->readNext();
        if (!ok && !r->getErrorMsg().empty()) { g_last_error = r->getErrorMsg(); } else { clear_last_error(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_reader_next", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_reader_next"); return false; }
}

const_bcsv_row_t bcsv_csv_reader_row(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_row", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_row", static_cast<const_bcsv_row_t>(nullptr),
        reinterpret_cast<const_bcsv_row_t>(&static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->row()))
}

size_t bcsv_csv_reader_index(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_index", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_index", 0u, static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->rowPos())
}

size_t bcsv_csv_reader_file_line(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_file_line", reader)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_file_line", 0u, static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->fileLine())
}

const char* bcsv_csv_reader_error_msg(const_bcsv_csv_reader_t reader) {
    if (null_handle("bcsv_csv_reader_error_msg", reader)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_csv_reader_error_msg", "", static_cast<const bcsv::CsvReader<bcsv::Layout>*>(reader)->getErrorMsg().c_str())
}

// ============================================================================
// CSV Writer API
// ============================================================================
bcsv_csv_writer_t bcsv_csv_writer_create(bcsv_layout_t layout, char delimiter, char decimal_sep) {
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_create", nullptr, ([&]() -> bcsv_csv_writer_t {
        if (layout) {
            return static_cast<bcsv_csv_writer_t>(new bcsv::CsvWriter<bcsv::Layout>(
                *static_cast<bcsv::Layout*>(layout), delimiter, decimal_sep));
        } else {
            bcsv::Layout empty;
            return static_cast<bcsv_csv_writer_t>(new bcsv::CsvWriter<bcsv::Layout>(empty, delimiter, decimal_sep));
        }
    })())
}

void bcsv_csv_writer_destroy(bcsv_csv_writer_t writer) {
    if (!writer) return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_writer_destroy", delete static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer))
}

bool bcsv_csv_writer_open(bcsv_csv_writer_t writer, const char* filename, bool overwrite, bool include_header) {
    if (null_handle("bcsv_csv_writer_open", writer)) return false;
    try {
        auto* w = static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer);
        bool ok = w->open(filename, overwrite, include_header);
        if (ok) { clear_last_error(); } else { g_last_error = w->getErrorMsg(); }
        return ok;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_writer_open", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_writer_open"); return false; }
}

void bcsv_csv_writer_close(bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_close", writer)) return;
    BCSV_CAPI_TRY_VOID("bcsv_csv_writer_close", static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->close())
}

bool bcsv_csv_writer_is_open(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_is_open", writer)) return false;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_is_open", false, static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_csv_writer_filename(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_filename", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_filename", static_cast<const wchar_t*>(nullptr),
        static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->filePath().c_str())
}
#else
const char* bcsv_csv_writer_filename(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_filename", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_filename", static_cast<const char*>(nullptr),
        static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->filePath().c_str())
}
#endif

const_bcsv_layout_t bcsv_csv_writer_layout(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_layout", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_layout", static_cast<const_bcsv_layout_t>(nullptr),
        reinterpret_cast<const_bcsv_layout_t>(&static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->layout()))
}

bool bcsv_csv_writer_next(bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_next", writer)) return false;
    try {
        static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->writeRow();
        clear_last_error();
        return true;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_writer_next", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_writer_next"); return false; }
}

bool bcsv_csv_writer_write(bcsv_csv_writer_t writer, const_bcsv_row_t row) {
    if (null_handle("bcsv_csv_writer_write", writer) || null_handle("bcsv_csv_writer_write", row)) return false;
    try {
        static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->write(*static_cast<const bcsv::Row*>(row));
        clear_last_error();
        return true;
    } catch (const std::exception& ex) { set_last_error("bcsv_csv_writer_write", ex); return false;
    } catch (...) { set_last_error_unknown("bcsv_csv_writer_write"); return false; }
}

bcsv_row_t bcsv_csv_writer_row(bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_row", writer)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_row", static_cast<bcsv_row_t>(nullptr),
        static_cast<bcsv_row_t>(&static_cast<bcsv::CsvWriter<bcsv::Layout>*>(writer)->row()))
}

size_t bcsv_csv_writer_index(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_index", writer)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_index", 0u, static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->rowCount())
}

const char* bcsv_csv_writer_error_msg(const_bcsv_csv_writer_t writer) {
    if (null_handle("bcsv_csv_writer_error_msg", writer)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_csv_writer_error_msg", "", static_cast<const bcsv::CsvWriter<bcsv::Layout>*>(writer)->getErrorMsg().c_str())
}

// ============================================================================
// Row API
// ============================================================================

// Row lifecycle
bcsv_row_t bcsv_row_create(const_bcsv_layout_t layout) {
    if (null_handle("bcsv_row_create", layout)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_row_create", static_cast<bcsv_row_t>(nullptr), ([&]() {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        return static_cast<bcsv_row_t>(new bcsv::Row(*l));
    })())
}

bcsv_row_t bcsv_row_clone(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_clone", row)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_row_clone", static_cast<bcsv_row_t>(nullptr), static_cast<bcsv_row_t>(new bcsv::Row(*static_cast<const bcsv::Row*>(row))))
}

void bcsv_row_destroy(bcsv_row_t row) {
    if (!row) return;
    BCSV_CAPI_TRY_VOID("bcsv_row_destroy", delete static_cast<bcsv::Row*>(row))
}

void bcsv_row_clear(bcsv_row_t row) {
    if (null_handle("bcsv_row_clear", row)) return;
    BCSV_CAPI_TRY_VOID("bcsv_row_clear", static_cast<bcsv::Row*>(row)->clear())
}

void bcsv_row_assign(bcsv_row_t dest, const_bcsv_row_t src) {
    if (null_handle("bcsv_row_assign", dest) || null_handle("bcsv_row_assign", src)) return;
    BCSV_CAPI_TRY_VOID("bcsv_row_assign", ([&]() {
        auto& d = *static_cast<bcsv::Row*>(dest);
        auto& s = *static_cast<const bcsv::Row*>(src);
        d = s;
    })())
}

const_bcsv_layout_t bcsv_row_layout(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_layout", row)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_row_layout", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::Row*>(row);
        auto l = &(r->layout());
        return reinterpret_cast<const_bcsv_layout_t>(l);
    })())
}

bool bcsv_row_get_bool(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(false, static_cast<const bcsv::Row*>(row)->get<bool>(col))
}
uint8_t bcsv_row_get_uint8(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint8_t{0}, static_cast<const bcsv::Row*>(row)->get<uint8_t>(col))
}
uint16_t bcsv_row_get_uint16(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint16_t{0}, static_cast<const bcsv::Row*>(row)->get<uint16_t>(col))
}
uint32_t bcsv_row_get_uint32(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint32_t{0}, static_cast<const bcsv::Row*>(row)->get<uint32_t>(col))
}
uint64_t bcsv_row_get_uint64(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(uint64_t{0}, static_cast<const bcsv::Row*>(row)->get<uint64_t>(col))
}
int8_t bcsv_row_get_int8(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int8_t{0}, static_cast<const bcsv::Row*>(row)->get<int8_t>(col))
}
int16_t bcsv_row_get_int16(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int16_t{0}, static_cast<const bcsv::Row*>(row)->get<int16_t>(col))
}
int32_t bcsv_row_get_int32(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int32_t{0}, static_cast<const bcsv::Row*>(row)->get<int32_t>(col))
}
int64_t bcsv_row_get_int64(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(int64_t{0}, static_cast<const bcsv::Row*>(row)->get<int64_t>(col))
}
float bcsv_row_get_float(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(0.0f, static_cast<const bcsv::Row*>(row)->get<float>(col))
}
double bcsv_row_get_double(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(0.0, static_cast<const bcsv::Row*>(row)->get<double>(col))
}
const char* bcsv_row_get_string(const_bcsv_row_t row, int col) {
    BCSV_ROW_GET(static_cast<const char*>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::Row*>(row);
        const auto& s = r->get<std::string>(col);
        return s.c_str();
    })())
}

void bcsv_row_set_bool(bcsv_row_t row, int col, bool value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint8(bcsv_row_t row, int col, uint8_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint16(bcsv_row_t row, int col, uint16_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint32(bcsv_row_t row, int col, uint32_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint64(bcsv_row_t row, int col, uint64_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int8(bcsv_row_t row, int col, int8_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int16(bcsv_row_t row, int col, int16_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int32(bcsv_row_t row, int col, int32_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int64(bcsv_row_t row, int col, int64_t value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_float(bcsv_row_t row, int col, float value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_double(bcsv_row_t row, int col, double value) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_string(bcsv_row_t row, int col, const char* value) {
    if (__builtin_expect(!value, 0)) { g_has_error = true; g_last_error = "bcsv_row_set_string: value is NULL"; return; }
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set(col, std::string(value)))
}

// Vectorized get functions
void bcsv_row_get_bool_array(const_bcsv_row_t row, int start_col, bool* dst, size_t count) {
    auto s = std::span<bool>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<bool>(start_col, s))
}
void bcsv_row_get_uint8_array(const_bcsv_row_t row, int start_col, uint8_t* dst, size_t count) {
    auto s = std::span<uint8_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint8_t>(start_col, s))
}
void bcsv_row_get_uint16_array(const_bcsv_row_t row, int start_col, uint16_t* dst, size_t count) {
    auto s = std::span<uint16_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint16_t>(start_col, s))
}
void bcsv_row_get_uint32_array(const_bcsv_row_t row, int start_col, uint32_t* dst, size_t count) {
    auto s = std::span<uint32_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint32_t>(start_col, s))
}
void bcsv_row_get_uint64_array(const_bcsv_row_t row, int start_col, uint64_t* dst, size_t count) {
    auto s = std::span<uint64_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<uint64_t>(start_col, s))
}
void bcsv_row_get_int8_array(const_bcsv_row_t row, int start_col, int8_t* dst, size_t count) {
    auto s = std::span<int8_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int8_t>(start_col, s))
}
void bcsv_row_get_int16_array(const_bcsv_row_t row, int start_col, int16_t* dst, size_t count) {
    auto s = std::span<int16_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int16_t>(start_col, s))
}
void bcsv_row_get_int32_array(const_bcsv_row_t row, int start_col, int32_t* dst, size_t count) {
    auto s = std::span<int32_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int32_t>(start_col, s))
}
void bcsv_row_get_int64_array(const_bcsv_row_t row, int start_col, int64_t* dst, size_t count) {
    auto s = std::span<int64_t>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<int64_t>(start_col, s))
}
void bcsv_row_get_float_array(const_bcsv_row_t row, int start_col, float* dst, size_t count) {
    auto s = std::span<float>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<float>(start_col, s))
}
void bcsv_row_get_double_array(const_bcsv_row_t row, int start_col, double* dst, size_t count) {
    auto s = std::span<double>(dst, count);
    BCSV_ROW_SET(static_cast<const bcsv::Row*>(row)->get<double>(start_col, s))
}

// Vectorized set functions
void bcsv_row_set_bool_array(bcsv_row_t row, int start_col, const bool* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<bool>(start_col, std::span<const bool>(src, count)))
}
void bcsv_row_set_uint8_array(bcsv_row_t row, int start_col, const uint8_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint8_t>(start_col, std::span<const uint8_t>(src, count)))
}
void bcsv_row_set_uint16_array(bcsv_row_t row, int start_col, const uint16_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint16_t>(start_col, std::span<const uint16_t>(src, count)))
}
void bcsv_row_set_uint32_array(bcsv_row_t row, int start_col, const uint32_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint32_t>(start_col, std::span<const uint32_t>(src, count)))
}
void bcsv_row_set_uint64_array(bcsv_row_t row, int start_col, const uint64_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<uint64_t>(start_col, std::span<const uint64_t>(src, count)))
}
void bcsv_row_set_int8_array(bcsv_row_t row, int start_col, const int8_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int8_t>(start_col, std::span<const int8_t>(src, count)))
}
void bcsv_row_set_int16_array(bcsv_row_t row, int start_col, const int16_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int16_t>(start_col, std::span<const int16_t>(src, count)))
}
void bcsv_row_set_int32_array(bcsv_row_t row, int start_col, const int32_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int32_t>(start_col, std::span<const int32_t>(src, count)))
}
void bcsv_row_set_int64_array(bcsv_row_t row, int start_col, const int64_t* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<int64_t>(start_col, std::span<const int64_t>(src, count)))
}
void bcsv_row_set_float_array(bcsv_row_t row, int start_col, const float* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<float>(start_col, std::span<const float>(src, count)))
}
void bcsv_row_set_double_array(bcsv_row_t row, int start_col, const double* src, size_t count) {
    BCSV_ROW_SET(static_cast<bcsv::Row*>(row)->set<double>(start_col, std::span<const double>(src, count)))
}

const char* bcsv_row_to_string(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_to_string", row)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_row_to_string", "", ([&]() -> const char* {
        const auto* r = static_cast<const bcsv::Row*>(row);
        std::ostringstream oss;
        oss << *r;
        g_fmt_buf = oss.str();
        return g_fmt_buf.c_str();
    })())
}

size_t bcsv_row_column_count(const_bcsv_row_t row) {
    if (null_handle("bcsv_row_column_count", row)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_row_column_count", 0u, static_cast<const bcsv::Row*>(row)->layout().columnCount())
}

// ============================================================================
// Row Visit API
// ============================================================================
void bcsv_row_visit_const(const_bcsv_row_t row, size_t start_col, size_t count,
                           bcsv_visit_callback_t cb, void* user_data) {
    if (null_handle("bcsv_row_visit_const", row) || !cb) return;
    try {
        const auto* r = static_cast<const bcsv::Row*>(row);
        const auto& layout = r->layout();
        const size_t end = start_col + count;
        if (end > layout.columnCount()) {
            g_has_error = true;
            g_last_error = "bcsv_row_visit_const: column range out of bounds";
            return;
        }
        r->visitConst(start_col, [&](size_t col, auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                bool v = val;
                cb(col, BCSV_TYPE_BOOL, &v, user_data);
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                cb(col, BCSV_TYPE_UINT8, &val, user_data);
            } else if constexpr (std::is_same_v<T, uint16_t>) {
                cb(col, BCSV_TYPE_UINT16, &val, user_data);
            } else if constexpr (std::is_same_v<T, uint32_t>) {
                cb(col, BCSV_TYPE_UINT32, &val, user_data);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                cb(col, BCSV_TYPE_UINT64, &val, user_data);
            } else if constexpr (std::is_same_v<T, int8_t>) {
                cb(col, BCSV_TYPE_INT8, &val, user_data);
            } else if constexpr (std::is_same_v<T, int16_t>) {
                cb(col, BCSV_TYPE_INT16, &val, user_data);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                cb(col, BCSV_TYPE_INT32, &val, user_data);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                cb(col, BCSV_TYPE_INT64, &val, user_data);
            } else if constexpr (std::is_same_v<T, float>) {
                cb(col, BCSV_TYPE_FLOAT, &val, user_data);
            } else if constexpr (std::is_same_v<T, double>) {
                cb(col, BCSV_TYPE_DOUBLE, &val, user_data);
            } else if constexpr (std::is_same_v<T, std::string>) {
                const char* cstr = val.c_str();
                cb(col, BCSV_TYPE_STRING, cstr, user_data);
            }
        }, count);
    } catch (const std::exception& ex) {
        set_last_error("bcsv_row_visit_const", ex);
    } catch (...) {
        set_last_error_unknown("bcsv_row_visit_const");
    }
}

// ============================================================================
// Sampler API
// ============================================================================
bcsv_sampler_t bcsv_sampler_create(bcsv_reader_t reader) {
    if (null_handle("bcsv_sampler_create", reader)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_create", nullptr, ([&]() -> bcsv_sampler_t {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto* h = new SamplerHandle();
        h->sampler = new bcsv::Sampler<bcsv::Layout>(*r);
        return static_cast<bcsv_sampler_t>(h);
    })())
}

void bcsv_sampler_destroy(bcsv_sampler_t sampler) {
    if (!sampler) return;
    BCSV_CAPI_TRY_VOID("bcsv_sampler_destroy", ([&]() {
        auto* h = static_cast<SamplerHandle*>(sampler);
        delete h->sampler;
        delete h;
    })())
}

bool bcsv_sampler_set_conditional(bcsv_sampler_t sampler, const char* expr) {
    if (null_handle("bcsv_sampler_set_conditional", sampler)) return false;
    try {
        auto* h = static_cast<SamplerHandle*>(sampler);
        auto result = h->sampler->setConditional(expr ? expr : "");
        if (result.success) {
            h->error_msg.clear();
            clear_last_error();
            return true;
        } else {
            h->error_msg = result.error_msg;
            g_has_error = true;
            g_last_error = "bcsv_sampler_set_conditional: " + result.error_msg;
            return false;
        }
    } catch (const std::exception& ex) {
        set_last_error("bcsv_sampler_set_conditional", ex);
        auto* h = static_cast<SamplerHandle*>(sampler);
        h->error_msg = ex.what();
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_sampler_set_conditional");
        return false;
    }
}

bool bcsv_sampler_set_selection(bcsv_sampler_t sampler, const char* expr) {
    if (null_handle("bcsv_sampler_set_selection", sampler)) return false;
    try {
        auto* h = static_cast<SamplerHandle*>(sampler);
        auto result = h->sampler->setSelection(expr ? expr : "");
        if (result.success) {
            h->error_msg.clear();
            clear_last_error();
            return true;
        } else {
            h->error_msg = result.error_msg;
            g_has_error = true;
            g_last_error = "bcsv_sampler_set_selection: " + result.error_msg;
            return false;
        }
    } catch (const std::exception& ex) {
        set_last_error("bcsv_sampler_set_selection", ex);
        auto* h = static_cast<SamplerHandle*>(sampler);
        h->error_msg = ex.what();
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_sampler_set_selection");
        return false;
    }
}

const char* bcsv_sampler_get_conditional(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_get_conditional", sampler)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_get_conditional", "", static_cast<const SamplerHandle*>(sampler)->sampler->getConditional().c_str())
}

const char* bcsv_sampler_get_selection(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_get_selection", sampler)) return "";
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_get_selection", "", static_cast<const SamplerHandle*>(sampler)->sampler->getSelection().c_str())
}

void bcsv_sampler_set_mode(bcsv_sampler_t sampler, bcsv_sampler_mode_t mode) {
    if (null_handle("bcsv_sampler_set_mode", sampler)) return;
    BCSV_CAPI_TRY_VOID("bcsv_sampler_set_mode",
        static_cast<SamplerHandle*>(sampler)->sampler->setMode(static_cast<bcsv::SamplerMode>(mode)))
}

bcsv_sampler_mode_t bcsv_sampler_get_mode(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_get_mode", sampler)) return BCSV_SAMPLER_TRUNCATE;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_get_mode", BCSV_SAMPLER_TRUNCATE,
        static_cast<bcsv_sampler_mode_t>(static_cast<const SamplerHandle*>(sampler)->sampler->getMode()))
}

bool bcsv_sampler_next(bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_next", sampler)) return false;
    try {
        auto* h = static_cast<SamplerHandle*>(sampler);
        return h->sampler->next();
    } catch (const std::exception& ex) {
        set_last_error("bcsv_sampler_next", ex);
        static_cast<SamplerHandle*>(sampler)->error_msg = ex.what();
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_sampler_next");
        return false;
    }
}

const_bcsv_row_t bcsv_sampler_row(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_row", sampler)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_row", static_cast<const_bcsv_row_t>(nullptr),
        reinterpret_cast<const_bcsv_row_t>(&static_cast<const SamplerHandle*>(sampler)->sampler->row()))
}

const_bcsv_layout_t bcsv_sampler_output_layout(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_output_layout", sampler)) return nullptr;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_output_layout", static_cast<const_bcsv_layout_t>(nullptr),
        reinterpret_cast<const_bcsv_layout_t>(&static_cast<const SamplerHandle*>(sampler)->sampler->outputLayout()))
}

size_t bcsv_sampler_source_row_pos(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_source_row_pos", sampler)) return 0u;
    BCSV_CAPI_TRY_RETURN("bcsv_sampler_source_row_pos", 0u,
        static_cast<const SamplerHandle*>(sampler)->sampler->sourceRowPos())
}

const char* bcsv_sampler_error_msg(const_bcsv_sampler_t sampler) {
    if (null_handle("bcsv_sampler_error_msg", sampler)) return "";
    return static_cast<const SamplerHandle*>(sampler)->error_msg.c_str();
}

// ============================================================================
// Error API
// ============================================================================
const char* bcsv_last_error() {
    return g_has_error ? g_last_error.c_str() : "";
}

void bcsv_clear_last_error() {
    g_has_error = false;
}

} // extern "C"
