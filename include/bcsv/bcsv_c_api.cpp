/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include "bcsv_c_api.h"
#include <string>
#include <exception>

// Include full implementations (headers + .hpp files)
#include "bcsv.h"  // This includes all implementations

namespace {
thread_local std::string g_last_error;

void clear_last_error() noexcept {
    g_last_error.clear();
}

void set_last_error(const char* where, const std::exception& ex) noexcept {
    g_last_error = std::string(where) + ": " + ex.what();
}

void set_last_error_unknown(const char* where) noexcept {
    g_last_error = std::string(where) + ": unknown exception";
}

// Tagged wrapper to support both Flat and ZoH writers behind void*
struct WriterHandle {
    enum class Type { Flat, ZoH } type;
    void* ptr;
};

template<typename F>
auto visit_writer(WriterHandle* h, F&& fn) {
    if (h->type == WriterHandle::Type::ZoH)
        return fn(static_cast<bcsv::WriterZoH<bcsv::Layout>*>(h->ptr));
    else
        return fn(static_cast<bcsv::Writer<bcsv::Layout>*>(h->ptr));
}

template<typename F>
auto visit_writer(const WriterHandle* h, F&& fn) {
    if (h->type == WriterHandle::Type::ZoH)
        return fn(static_cast<const bcsv::WriterZoH<bcsv::Layout>*>(h->ptr));
    else
        return fn(static_cast<const bcsv::Writer<bcsv::Layout>*>(h->ptr));
}

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

extern "C" {

// Layout API
bcsv_layout_t bcsv_layout_create() {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_create", nullptr, new bcsv::Layout())
}

bcsv_layout_t bcsv_layout_clone(const_bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_clone", nullptr, new bcsv::Layout(*static_cast<const bcsv::Layout*>(layout)))
}

void bcsv_layout_destroy(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_VOID("bcsv_layout_destroy", delete static_cast<bcsv::Layout*>(layout))
}

bool bcsv_layout_has_column(const_bcsv_layout_t layout, const char* name) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_has_column", false, static_cast<const bcsv::Layout*>(layout)->hasColumn(name))
}

size_t bcsv_layout_column_count(const_bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_count", 0u, static_cast<const bcsv::Layout*>(layout)->columnCount())
}

size_t bcsv_layout_column_index(const_bcsv_layout_t layout, const char* name) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_index", 0u, static_cast<const bcsv::Layout*>(layout)->columnIndex(name))
}

const char* bcsv_layout_column_name(const_bcsv_layout_t layout, size_t index) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_name", static_cast<const char*>(nullptr), static_cast<const bcsv::Layout*>(layout)->columnName(index).c_str())
}

bcsv_type_t bcsv_layout_column_type(const_bcsv_layout_t layout, size_t index) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_column_type", BCSV_TYPE_BOOL, (bcsv_type_t)static_cast<const bcsv::Layout*>(layout)->columnType(index))
}

bool bcsv_layout_set_column_name(bcsv_layout_t layout, size_t index, const char* name) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_set_column_name", false, (static_cast<bcsv::Layout*>(layout)->setColumnName(index, name), true))
}

void bcsv_layout_set_column_type(bcsv_layout_t layout, size_t index, bcsv_type_t type) {
    BCSV_CAPI_TRY_VOID("bcsv_layout_set_column_type", static_cast<bcsv::Layout*>(layout)->setColumnType(index, static_cast<bcsv::ColumnType>(type)))
}

bool bcsv_layout_add_column(bcsv_layout_t layout, size_t index, const char* name, bcsv_type_t type) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_add_column", false, ([&]() {
        bcsv::ColumnDefinition colDef = {name, static_cast<bcsv::ColumnType>(type)};
        static_cast<bcsv::Layout*>(layout)->addColumn(colDef, index);
        return true;
    })())
}

void bcsv_layout_remove_column(bcsv_layout_t layout, size_t index) {
    BCSV_CAPI_TRY_VOID("bcsv_layout_remove_column", static_cast<bcsv::Layout*>(layout)->removeColumn(index))
}

void bcsv_layout_clear(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_VOID("bcsv_layout_clear", static_cast<bcsv::Layout*>(layout)->clear())
}

bool bcsv_layout_is_compatible(const_bcsv_layout_t layout1, const_bcsv_layout_t layout2) {
    BCSV_CAPI_TRY_RETURN("bcsv_layout_is_compatible", false, ([&]() {
        auto& l1 = *static_cast<const bcsv::Layout*>(layout1);
        auto& l2 = *static_cast<const bcsv::Layout*>(layout2);
        return l1.isCompatible(l2);
    })())
}

void bcsv_layout_assign(bcsv_layout_t dest, const_bcsv_layout_t src) {
    BCSV_CAPI_TRY_VOID("bcsv_layout_assign", ([&]() {
        auto& d = *static_cast<bcsv::Layout*>(dest);
        auto& s = *static_cast<const bcsv::Layout*>(src);
        d = s;
    })())
}

// Reader API
bcsv_reader_t bcsv_reader_create(bcsv_read_mode_t mode) {
    // Mode is currently ignored as ReaderDirectAccess provides full functionality
    // and strict/resilient modes are not yet exposed in the new C++ API
    (void)mode; 
    BCSV_CAPI_TRY_RETURN("bcsv_reader_create", nullptr, new bcsv::ReaderDirectAccess<bcsv::Layout>())
}

void bcsv_reader_destroy(bcsv_reader_t reader) {
    BCSV_CAPI_TRY_VOID("bcsv_reader_destroy", delete static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader))
}

void bcsv_reader_close(bcsv_reader_t reader) {
    BCSV_CAPI_TRY_VOID("bcsv_reader_close", static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->close())
}

size_t bcsv_reader_count_rows(const_bcsv_reader_t reader) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_count_rows", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->rowCount())
}

bool bcsv_reader_open(bcsv_reader_t reader, const char* filename) {
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
    BCSV_CAPI_TRY_RETURN("bcsv_reader_is_open", false, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->isOpen())
}

#ifdef _WIN32
const wchar_t* bcsv_reader_filename(const_bcsv_reader_t reader) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename", static_cast<const wchar_t*>(nullptr), ([&]() {
        const auto* r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& path = r->filePath();
        return path.c_str();
    })())
}
#else
const char* bcsv_reader_filename(const_bcsv_reader_t reader) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_filename", static_cast<const char*>(nullptr), ([&]() {
        const auto* r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        const auto& path = r->filePath();
        return path.c_str();
    })())
}
#endif

const_bcsv_layout_t bcsv_reader_layout(const_bcsv_reader_t reader) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_layout", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto l = &(r->layout());
        return reinterpret_cast<const_bcsv_layout_t>(l);
    })())
}

bool bcsv_reader_next(bcsv_reader_t reader) {
    try {
        auto* r = static_cast<bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        bool ok = r->readNext();
        if (!ok && !r->getErrorMsg().empty()) {
            g_last_error = r->getErrorMsg();
        } else {
            clear_last_error();
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
    BCSV_CAPI_TRY_RETURN("bcsv_reader_row", static_cast<const_bcsv_row_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader);
        auto x = &(r->row());
        return reinterpret_cast<const_bcsv_row_t>(x);
    })())
}
size_t bcsv_reader_index(const_bcsv_reader_t reader) {
    BCSV_CAPI_TRY_RETURN("bcsv_reader_index", 0u, static_cast<const bcsv::ReaderDirectAccess<bcsv::Layout>*>(reader)->rowPos())
}

// Writer API
bcsv_writer_t bcsv_writer_create(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create", nullptr, ([&]() {
        auto* h = new WriterHandle();
        h->type = WriterHandle::Type::Flat;
        if (layout) {
            h->ptr = new bcsv::Writer<bcsv::Layout>(*static_cast<bcsv::Layout*>(layout));
        } else {
            bcsv::Layout empty;
            h->ptr = new bcsv::Writer<bcsv::Layout>(empty);
        }
        return reinterpret_cast<bcsv_writer_t>(h);
    })())
}

bcsv_writer_t bcsv_writer_create_zoh(bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_create_zoh", nullptr, ([&]() {
        auto* h = new WriterHandle();
        h->type = WriterHandle::Type::ZoH;
        if (layout) {
            h->ptr = new bcsv::WriterZoH<bcsv::Layout>(*static_cast<bcsv::Layout*>(layout));
        } else {
            bcsv::Layout empty;
            h->ptr = new bcsv::WriterZoH<bcsv::Layout>(empty);
        }
        return reinterpret_cast<bcsv_writer_t>(h);
    })())
}

void bcsv_writer_destroy(bcsv_writer_t writer) {
    BCSV_CAPI_TRY_VOID("bcsv_writer_destroy", ([&]() {
        auto* h = static_cast<WriterHandle*>(writer);
        visit_writer(h, [](auto* w) { delete w; });
        delete h;
    })())
}

void bcsv_writer_close(bcsv_writer_t writer) {
    BCSV_CAPI_TRY_VOID("bcsv_writer_close", visit_writer(static_cast<WriterHandle*>(writer), [](auto* w) { w->close(); }))
}

void bcsv_writer_flush(bcsv_writer_t writer) {
    BCSV_CAPI_TRY_VOID("bcsv_writer_flush", visit_writer(static_cast<WriterHandle*>(writer), [](auto* w) { w->flush(); }))
}

bool bcsv_writer_open(bcsv_writer_t writer, const char* filename, bool overwrite, int compress, int block_size_kb, bcsv_file_flags_t flags) {
    try {
        auto* h = static_cast<WriterHandle*>(writer);
        // Auto-set ZoH flag for ZoH writers so the file header is correct
        // even if the caller forgets to pass BCSV_FLAG_ZOH
        if (h->type == WriterHandle::Type::ZoH) {
            flags = static_cast<bcsv_file_flags_t>(flags | BCSV_FLAG_ZOH);
        }
        bool ok = visit_writer(h, [&](auto* w) {
            return w->open(filename,
                          overwrite,
                          static_cast<size_t>(compress),
                          static_cast<size_t>(block_size_kb),
                          static_cast<bcsv::FileFlags>(flags));
        });
        if (ok) {
            clear_last_error();
        } else {
            visit_writer(h, [](auto* w) { g_last_error = w->getErrorMsg(); });
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
    BCSV_CAPI_TRY_RETURN("bcsv_writer_is_open", false, visit_writer(static_cast<const WriterHandle*>(writer), [](auto* w) { return w->isOpen(); }))
}

#ifdef _WIN32
const wchar_t* bcsv_writer_filename(const_bcsv_writer_t writer) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename", static_cast<const wchar_t*>(nullptr), visit_writer(static_cast<const WriterHandle*>(writer), [](auto* w) -> const wchar_t* {
        return w->filePath().c_str();
    }))
}
#else
const char* bcsv_writer_filename(const_bcsv_writer_t writer) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_filename", static_cast<const char*>(nullptr), visit_writer(static_cast<const WriterHandle*>(writer), [](auto* w) -> const char* {
        return w->filePath().c_str();
    }))
}
#endif

const_bcsv_layout_t bcsv_writer_layout(const_bcsv_writer_t writer) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_layout", static_cast<const_bcsv_layout_t>(nullptr), visit_writer(static_cast<const WriterHandle*>(writer), [](auto* w) -> const_bcsv_layout_t {
        return static_cast<const_bcsv_layout_t>(&w->layout());
    }))
}

bool bcsv_writer_next(bcsv_writer_t writer) {
    try {
        visit_writer(static_cast<WriterHandle*>(writer), [](auto* w) { w->writeRow(); });
        clear_last_error();
        return true;
    } catch (const std::exception& ex) {
        set_last_error("bcsv_writer_next", ex);
        return false;
    } catch (...) {
        set_last_error_unknown("bcsv_writer_next");
        return false;
    }
}

bcsv_row_t bcsv_writer_row(bcsv_writer_t writer) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_row", static_cast<bcsv_row_t>(nullptr), visit_writer(static_cast<WriterHandle*>(writer), [](auto* w) -> bcsv_row_t {
        return static_cast<bcsv_row_t>(&w->row());
    }))
}

size_t bcsv_writer_index(const_bcsv_writer_t writer) {
    BCSV_CAPI_TRY_RETURN("bcsv_writer_index", 0u, visit_writer(static_cast<const WriterHandle*>(writer), [](auto* w) -> size_t { return w->rowCount(); }))
}

// Row API

// Row lifecycle
bcsv_row_t bcsv_row_create(const_bcsv_layout_t layout) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_create", static_cast<bcsv_row_t>(nullptr), ([&]() {
        const auto* l = static_cast<const bcsv::Layout*>(layout);
        return static_cast<bcsv_row_t>(new bcsv::Row(*l));
    })())
}

bcsv_row_t bcsv_row_clone(const_bcsv_row_t row) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_clone", static_cast<bcsv_row_t>(nullptr), static_cast<bcsv_row_t>(new bcsv::Row(*static_cast<const bcsv::Row*>(row))))
}

void bcsv_row_destroy(bcsv_row_t row) {
    BCSV_CAPI_TRY_VOID("bcsv_row_destroy", delete static_cast<bcsv::Row*>(row))
}

void bcsv_row_clear(bcsv_row_t row) {
    BCSV_CAPI_TRY_VOID("bcsv_row_clear", static_cast<bcsv::Row*>(row)->clear())
}

void bcsv_row_assign(bcsv_row_t dest, const_bcsv_row_t src) {
    BCSV_CAPI_TRY_VOID("bcsv_row_assign", ([&]() {
        auto& d = *static_cast<bcsv::Row*>(dest);
        auto& s = *static_cast<const bcsv::Row*>(src);
        d = s;
    })())
}

const_bcsv_layout_t bcsv_row_layout(const_bcsv_row_t row) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_layout", static_cast<const_bcsv_layout_t>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::Row*>(row);
        auto l = &(r->layout());
        return reinterpret_cast<const_bcsv_layout_t>(l);
    })())
}

bool bcsv_row_get_bool(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_bool", false, static_cast<const bcsv::Row*>(row)->get<bool>(col))
}
uint8_t bcsv_row_get_uint8(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_uint8", uint8_t{0}, static_cast<const bcsv::Row*>(row)->get<uint8_t>(col))
}
uint16_t bcsv_row_get_uint16(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_uint16", uint16_t{0}, static_cast<const bcsv::Row*>(row)->get<uint16_t>(col))
}
uint32_t bcsv_row_get_uint32(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_uint32", uint32_t{0}, static_cast<const bcsv::Row*>(row)->get<uint32_t>(col))
}
uint64_t bcsv_row_get_uint64(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_uint64", uint64_t{0}, static_cast<const bcsv::Row*>(row)->get<uint64_t>(col))
}
int8_t bcsv_row_get_int8(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_int8", int8_t{0}, static_cast<const bcsv::Row*>(row)->get<int8_t>(col))
}
int16_t bcsv_row_get_int16(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_int16", int16_t{0}, static_cast<const bcsv::Row*>(row)->get<int16_t>(col))
}
int32_t bcsv_row_get_int32(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_int32", int32_t{0}, static_cast<const bcsv::Row*>(row)->get<int32_t>(col))
}
int64_t bcsv_row_get_int64(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_int64", int64_t{0}, static_cast<const bcsv::Row*>(row)->get<int64_t>(col))
}
float bcsv_row_get_float(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_float", 0.0f, static_cast<const bcsv::Row*>(row)->get<float>(col))
}
double bcsv_row_get_double(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_double", 0.0, static_cast<const bcsv::Row*>(row)->get<double>(col))
}
const char* bcsv_row_get_string(const_bcsv_row_t row, int col) {
    BCSV_CAPI_TRY_RETURN("bcsv_row_get_string", static_cast<const char*>(nullptr), ([&]() {
        auto r = static_cast<const bcsv::Row*>(row);
        const auto& s = r->get<std::string>(col);
        return s.c_str();
    })())
}

void bcsv_row_set_bool(bcsv_row_t row, int col, bool value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_bool", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint8(bcsv_row_t row, int col, uint8_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint8", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint16(bcsv_row_t row, int col, uint16_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint16", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint32(bcsv_row_t row, int col, uint32_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint32", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_uint64(bcsv_row_t row, int col, uint64_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint64", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int8(bcsv_row_t row, int col, int8_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int8", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int16(bcsv_row_t row, int col, int16_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int16", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int32(bcsv_row_t row, int col, int32_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int32", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_int64(bcsv_row_t row, int col, int64_t value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int64", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_float(bcsv_row_t row, int col, float value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_float", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_double(bcsv_row_t row, int col, double value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_double", static_cast<bcsv::Row*>(row)->set(col, value))
}
void bcsv_row_set_string(bcsv_row_t row, int col, const char* value) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_string", static_cast<bcsv::Row*>(row)->set(col, std::string(value)))
}

// Vectorized get functions
void bcsv_row_get_bool_array(const_bcsv_row_t row, int start_col, bool* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_bool_array", ([&]() {
        auto span = std::span<bool>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<bool>(start_col, span);
    })())
}
void bcsv_row_get_uint8_array(const_bcsv_row_t row, int start_col, uint8_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_uint8_array", ([&]() {
        auto span = std::span<uint8_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<uint8_t>(start_col, span);
    })())
}
void bcsv_row_get_uint16_array(const_bcsv_row_t row, int start_col, uint16_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_uint16_array", ([&]() {
        auto span = std::span<uint16_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<uint16_t>(start_col, span);
    })())
}
void bcsv_row_get_uint32_array(const_bcsv_row_t row, int start_col, uint32_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_uint32_array", ([&]() {
        auto span = std::span<uint32_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<uint32_t>(start_col, span);
    })())
}
void bcsv_row_get_uint64_array(const_bcsv_row_t row, int start_col, uint64_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_uint64_array", ([&]() {
        auto span = std::span<uint64_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<uint64_t>(start_col, span);
    })())
}
void bcsv_row_get_int8_array(const_bcsv_row_t row, int start_col, int8_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_int8_array", ([&]() {
        auto span = std::span<int8_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<int8_t>(start_col, span);
    })())
}
void bcsv_row_get_int16_array(const_bcsv_row_t row, int start_col, int16_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_int16_array", ([&]() {
        auto span = std::span<int16_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<int16_t>(start_col, span);
    })())
}
void bcsv_row_get_int32_array(const_bcsv_row_t row, int start_col, int32_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_int32_array", ([&]() {
        auto span = std::span<int32_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<int32_t>(start_col, span);
    })())
}
void bcsv_row_get_int64_array(const_bcsv_row_t row, int start_col, int64_t* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_int64_array", ([&]() {
        auto span = std::span<int64_t>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<int64_t>(start_col, span);
    })())
}
void bcsv_row_get_float_array(const_bcsv_row_t row, int start_col, float* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_float_array", ([&]() {
        auto span = std::span<float>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<float>(start_col, span);
    })())
}
void bcsv_row_get_double_array(const_bcsv_row_t row, int start_col, double* dst, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_get_double_array", ([&]() {
        auto span = std::span<double>(dst, count);
        static_cast<const bcsv::Row*>(row)->get<double>(start_col, span);
    })())
}

// Vectorized set functions
void bcsv_row_set_bool_array(bcsv_row_t row, int start_col, const bool* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_bool_array", static_cast<bcsv::Row*>(row)->set<bool>(start_col, std::span<const bool>(src, count)))
}
void bcsv_row_set_uint8_array(bcsv_row_t row, int start_col, const uint8_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint8_array", static_cast<bcsv::Row*>(row)->set<uint8_t>(start_col, std::span<const uint8_t>(src, count)))
}
void bcsv_row_set_uint16_array(bcsv_row_t row, int start_col, const uint16_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint16_array", static_cast<bcsv::Row*>(row)->set<uint16_t>(start_col, std::span<const uint16_t>(src, count)))
}
void bcsv_row_set_uint32_array(bcsv_row_t row, int start_col, const uint32_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint32_array", static_cast<bcsv::Row*>(row)->set<uint32_t>(start_col, std::span<const uint32_t>(src, count)))
}
void bcsv_row_set_uint64_array(bcsv_row_t row, int start_col, const uint64_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_uint64_array", static_cast<bcsv::Row*>(row)->set<uint64_t>(start_col, std::span<const uint64_t>(src, count)))
}
void bcsv_row_set_int8_array(bcsv_row_t row, int start_col, const int8_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int8_array", static_cast<bcsv::Row*>(row)->set<int8_t>(start_col, std::span<const int8_t>(src, count)))
}
void bcsv_row_set_int16_array(bcsv_row_t row, int start_col, const int16_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int16_array", static_cast<bcsv::Row*>(row)->set<int16_t>(start_col, std::span<const int16_t>(src, count)))
}
void bcsv_row_set_int32_array(bcsv_row_t row, int start_col, const int32_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int32_array", static_cast<bcsv::Row*>(row)->set<int32_t>(start_col, std::span<const int32_t>(src, count)))
}
void bcsv_row_set_int64_array(bcsv_row_t row, int start_col, const int64_t* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_int64_array", static_cast<bcsv::Row*>(row)->set<int64_t>(start_col, std::span<const int64_t>(src, count)))
}
void bcsv_row_set_float_array(bcsv_row_t row, int start_col, const float* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_float_array", static_cast<bcsv::Row*>(row)->set<float>(start_col, std::span<const float>(src, count)))
}
void bcsv_row_set_double_array(bcsv_row_t row, int start_col, const double* src, size_t count) {
    BCSV_CAPI_TRY_VOID("bcsv_row_set_double_array", static_cast<bcsv::Row*>(row)->set<double>(start_col, std::span<const double>(src, count)))
}

const char* bcsv_last_error() {
    return g_last_error.c_str();
}

} // extern "C"
