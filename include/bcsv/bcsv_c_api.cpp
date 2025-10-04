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

// Include full implementations (headers + .hpp files)
#include "bcsv.h"  // This includes all implementations

extern "C" {

// Layout API
bcsv_layout_t bcsv_layout_create() {
    return new bcsv::Layout();
}

bcsv_layout_t bcsv_layout_clone(const_bcsv_layout_t layout) {
    return new bcsv::Layout(*static_cast<const bcsv::Layout*>(layout));
}

void bcsv_layout_destroy(bcsv_layout_t layout) {
    delete static_cast<bcsv::Layout*>(layout);
}

bool bcsv_layout_has_column(const_bcsv_layout_t layout, const char* name) {
    return static_cast<const bcsv::Layout*>(layout)->hasColumn(name);
}

size_t bcsv_layout_column_count(const_bcsv_layout_t layout) {
    return static_cast<const bcsv::Layout*>(layout)->columnCount();
}

size_t bcsv_layout_column_index(const_bcsv_layout_t layout, const char* name) {
    return static_cast<const bcsv::Layout*>(layout)->columnIndex(name);
}

const char* bcsv_layout_column_name(const_bcsv_layout_t layout, size_t index) {
    return static_cast<const bcsv::Layout*>(layout)->columnName(index).c_str();
}

bcsv_type_t bcsv_layout_column_type(const_bcsv_layout_t layout, size_t index) {
    return (bcsv_type_t)static_cast<const bcsv::Layout*>(layout)->columnType(index);
}

bool bcsv_layout_set_column_name(bcsv_layout_t layout, size_t index, const char* name) {
    return static_cast<bcsv::Layout*>(layout)->setColumnName(index, name);
}

void bcsv_layout_set_column_type(bcsv_layout_t layout, size_t index, bcsv_type_t type) {
    static_cast<bcsv::Layout*>(layout)->setColumnType(index, static_cast<bcsv::ColumnType>(type));
}

bool bcsv_layout_add_column(bcsv_layout_t layout, size_t index, const char* name, bcsv_type_t type) {
    bcsv::ColumnDefinition colDef = {name, static_cast<bcsv::ColumnType>(type)};
    return static_cast<bcsv::Layout*>(layout)->addColumn(colDef, index);
}

void bcsv_layout_remove_column(bcsv_layout_t layout, size_t index) {
    static_cast<bcsv::Layout*>(layout)->removeColumn(index);
}

void bcsv_layout_clear(bcsv_layout_t layout) {
    static_cast<bcsv::Layout*>(layout)->clear();
}

bool bcsv_layout_isCompatible(const_bcsv_layout_t layout1, const_bcsv_layout_t layout2) {
    auto& l1 = *static_cast<const bcsv::Layout*>(layout1);
    auto& l2 = *static_cast<const bcsv::Layout*>(layout2);
    return l1.isCompatible(l2);
}

void bcsv_layout_assign(bcsv_layout_t dest, const_bcsv_layout_t src) {
    auto& d = *static_cast<bcsv::Layout*>(dest);
    auto& s = *static_cast<const bcsv::Layout*>(src);
    d = s;
}

// Reader API
bcsv_reader_t bcsv_reader_create(bcsv_read_mode_t mode) {
    return new bcsv::Reader<bcsv::Layout>(static_cast<bcsv::ReaderMode>(mode));
}

void bcsv_reader_destroy(bcsv_reader_t reader) {
    delete static_cast<bcsv::Reader<bcsv::Layout>*>(reader);
}

void bcsv_reader_close(bcsv_reader_t reader) {
    static_cast<bcsv::Reader<bcsv::Layout>*>(reader)->close();
}

bool bcsv_reader_open(bcsv_reader_t reader, const char* filename) {
    return static_cast<bcsv::Reader<bcsv::Layout>*>(reader)->open(filename);
}

bool bcsv_reader_is_open(const_bcsv_reader_t reader) {
    return static_cast<const bcsv::Reader<bcsv::Layout>*>(reader)->isOpen();
}

#ifdef _WIN32
const wchar_t* bcsv_reader_filename(const_bcsv_reader_t reader) {
    const auto* r = static_cast<const bcsv::Reader<bcsv::Layout>*>(reader);
    const auto& path = r->filePath();
    // On Windows, return native wchar_t string directly (no conversion needed)
    return path.c_str();
}
#else
const char* bcsv_reader_filename(const_bcsv_reader_t reader) {
    const auto* r = static_cast<const bcsv::Reader<bcsv::Layout>*>(reader);
    const auto& path = r->filePath();
    // On POSIX systems, return native char string directly (no conversion needed)
    return path.c_str();
}
#endif

const_bcsv_layout_t bcsv_reader_layout(const_bcsv_reader_t reader) {
    auto r = static_cast<const  bcsv::Reader<bcsv::Layout>*>(reader);
    auto l = &(r->layout());
    return reinterpret_cast<const_bcsv_layout_t>(l);
}

bool bcsv_reader_next(bcsv_reader_t reader) {
    return static_cast<bcsv::Reader<bcsv::Layout>*>(reader)->readNext();
}

const_bcsv_row_t bcsv_reader_row(const_bcsv_reader_t reader) {
    auto r = static_cast<const bcsv::Reader<bcsv::Layout>*>(reader);
    auto x = &(r->row());
    return reinterpret_cast<const_bcsv_row_t>(x);
}
size_t bcsv_reader_index(const_bcsv_reader_t reader) {
    return static_cast<const bcsv::Reader<bcsv::Layout>*>(reader)->rowIndex();
}

// Writer API
bcsv_writer_t bcsv_writer_create(bcsv_layout_t layout) {
    bcsv::Writer<bcsv::Layout>* writer = nullptr;
    if (layout) {
        writer = new bcsv::Writer<bcsv::Layout>(*static_cast<bcsv::Layout*>(layout));
    } else {
        // Create empty layout if null to allow empty writers
        layout = bcsv_layout_create();
        writer = new bcsv::Writer<bcsv::Layout>(*static_cast<bcsv::Layout*>(layout));
        bcsv_layout_destroy(layout);
    }
    return reinterpret_cast<bcsv_writer_t>(writer);
}

void bcsv_writer_destroy(bcsv_writer_t writer) {
    delete static_cast<bcsv::Writer<bcsv::Layout>*>(writer);
}

void bcsv_writer_close(bcsv_writer_t writer) {
    static_cast<bcsv::Writer<bcsv::Layout>*>(writer)->close();
}

void bcsv_writer_flush(bcsv_writer_t writer) {
    static_cast<bcsv::Writer<bcsv::Layout>*>(writer)->flush();
}

bool bcsv_writer_open(bcsv_writer_t writer, const char* filename, bool overwrite, int compress, int block_size_kb, bcsv_file_flags_t flags) {
    return static_cast<bcsv::Writer<bcsv::Layout>*>(writer)->open(  std::string_view(filename), 
                                                                    static_cast<bool>(overwrite),
                                                                    static_cast<size_t>(compress),
                                                                    static_cast<size_t>(block_size_kb), 
                                                                    static_cast<bcsv::FileFlags>(flags));
}

bool bcsv_writer_is_open(const_bcsv_writer_t writer) {
    return static_cast<const bcsv::Writer<bcsv::Layout>*>(writer)->is_open();
}

#ifdef _WIN32
const wchar_t* bcsv_writer_filename(const_bcsv_writer_t writer) {
    const auto* w = static_cast<const bcsv::Writer<bcsv::Layout>*>(writer);
    const auto& path = w->filePath();
    // On Windows, return native wchar_t string directly (no conversion needed)
    return path.c_str();
}
#else
const char* bcsv_writer_filename(const_bcsv_writer_t writer) {
    const auto* w = static_cast<const bcsv::Writer<bcsv::Layout>*>(writer);
    const auto& path = w->filePath();
    // On POSIX systems, return native char string directly (no conversion needed)
    return path.c_str();
}
#endif

const_bcsv_layout_t bcsv_writer_layout(const_bcsv_writer_t writer) {
    return static_cast<const_bcsv_layout_t>(&static_cast<const bcsv::Writer<bcsv::Layout>*>(writer)->layout());
}

bool bcsv_writer_next(bcsv_writer_t writer) {
    return static_cast<bcsv::Writer<bcsv::Layout>*>(writer)->writeRow();
}

bcsv_row_t bcsv_writer_row(bcsv_writer_t writer) {
    return static_cast<bcsv_row_t>(&static_cast<bcsv::Writer<bcsv::Layout>*>(writer)->row());
}

size_t bcsv_writer_index(const_bcsv_writer_t writer) {
    return static_cast<const bcsv::Writer<bcsv::Layout>*>(writer)->rowIndex();
}

// Row API

// Row lifecycle
bcsv_row_t bcsv_row_create(const_bcsv_layout_t layout) {
    const auto* l = static_cast<const bcsv::Layout*>(layout);
    return new bcsv::Row(*l);
}

bcsv_row_t bcsv_row_clone(const_bcsv_row_t row) {
    return new bcsv::Row(*static_cast<const bcsv::Row*>(row));
}

void bcsv_row_destroy(bcsv_row_t row) {
    delete static_cast<bcsv::Row*>(row);
}

void bcsv_row_clear(bcsv_row_t row) {
    static_cast<bcsv::Row*>(row)->clear();
}

void bcsv_row_assign(bcsv_row_t dest, const_bcsv_row_t src) {
    auto& d = *static_cast<bcsv::Row*>(dest);
    auto& s = *static_cast<const bcsv::Row*>(src);
    d = s;
}

// Change tracking
bool bcsv_row_has_any_changes(const_bcsv_row_t row) {
    return static_cast<const bcsv::Row*>(row)->hasAnyChanges();
}

void bcsv_row_track_changes(bcsv_row_t row, bool enable) {
    static_cast<bcsv::Row*>(row)->trackChanges(enable);
}

bool bcsv_row_tracks_changes(const_bcsv_row_t row) {
    return static_cast<const bcsv::Row*>(row)->tracksChanges();
}

void bcsv_row_set_changes(bcsv_row_t row) {
    static_cast<bcsv::Row*>(row)->setChanges();
}

void bcsv_row_reset_changes(bcsv_row_t row) {
    static_cast<bcsv::Row*>(row)->resetChanges();
}

const_bcsv_layout_t bcsv_row_layout(const_bcsv_row_t row) {
    auto r = static_cast<const bcsv::Row*>(row);
    auto l = &(r->layout());
    return reinterpret_cast<const_bcsv_layout_t>(l);
}

bool bcsv_row_get_bool(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<bool>(col);
}
uint8_t bcsv_row_get_uint8(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<uint8_t>(col);
}
uint16_t bcsv_row_get_uint16(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<uint16_t>(col);
}
uint32_t bcsv_row_get_uint32(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<uint32_t>(col);
}
uint64_t bcsv_row_get_uint64(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<uint64_t>(col);
}
int8_t bcsv_row_get_int8(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<int8_t>(col);
}
int16_t bcsv_row_get_int16(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<int16_t>(col);
}
int32_t bcsv_row_get_int32(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<int32_t>(col);
}
int64_t bcsv_row_get_int64(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<int64_t>(col);
}
float bcsv_row_get_float(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<float>(col);
}
double bcsv_row_get_double(const_bcsv_row_t row, int col) {
    return static_cast<const bcsv::Row*>(row)->get<double>(col);
}
const char* bcsv_row_get_string(const_bcsv_row_t row, int col) {
    auto r = static_cast<const bcsv::Row*>(row);
    const auto& s = r->get<std::string>(col);
    return s.c_str();
}

void bcsv_row_set_bool(bcsv_row_t row, int col, bool value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_uint8(bcsv_row_t row, int col, uint8_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_uint16(bcsv_row_t row, int col, uint16_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_uint32(bcsv_row_t row, int col, uint32_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_uint64(bcsv_row_t row, int col, uint64_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_int8(bcsv_row_t row, int col, int8_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_int16(bcsv_row_t row, int col, int16_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_int32(bcsv_row_t row, int col, int32_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_int64(bcsv_row_t row, int col, int64_t value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_float(bcsv_row_t row, int col, float value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_double(bcsv_row_t row, int col, double value) {
    static_cast<bcsv::Row*>(row)->set(col, value);
}
void bcsv_row_set_string(bcsv_row_t row, int col, const char* value) {
    static_cast<bcsv::Row*>(row)->set(col, std::string(value));
}

// Vectorized get functions
void bcsv_row_get_bool_array(const_bcsv_row_t row, int start_col, bool* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<bool>(start_col, std::span<bool>(dst, count));
}
void bcsv_row_get_uint8_array(const_bcsv_row_t row, int start_col, uint8_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<uint8_t>(start_col, std::span<uint8_t>(dst, count));
}
void bcsv_row_get_uint16_array(const_bcsv_row_t row, int start_col, uint16_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<uint16_t>(start_col, std::span<uint16_t>(dst, count));
}
void bcsv_row_get_uint32_array(const_bcsv_row_t row, int start_col, uint32_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<uint32_t>(start_col, std::span<uint32_t>(dst, count));
}
void bcsv_row_get_uint64_array(const_bcsv_row_t row, int start_col, uint64_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<uint64_t>(start_col, std::span<uint64_t>(dst, count));
}
void bcsv_row_get_int8_array(const_bcsv_row_t row, int start_col, int8_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<int8_t>(start_col, std::span<int8_t>(dst, count));
}
void bcsv_row_get_int16_array(const_bcsv_row_t row, int start_col, int16_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<int16_t>(start_col, std::span<int16_t>(dst, count));
}
void bcsv_row_get_int32_array(const_bcsv_row_t row, int start_col, int32_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<int32_t>(start_col, std::span<int32_t>(dst, count));
}
void bcsv_row_get_int64_array(const_bcsv_row_t row, int start_col, int64_t* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<int64_t>(start_col, std::span<int64_t>(dst, count));
}
void bcsv_row_get_float_array(const_bcsv_row_t row, int start_col, float* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<float>(start_col, std::span<float>(dst, count));
}
void bcsv_row_get_double_array(const_bcsv_row_t row, int start_col, double* dst, size_t count) {
    static_cast<const bcsv::Row*>(row)->get<double>(start_col, std::span<double>(dst, count));
}

// Vectorized set functions
void bcsv_row_set_bool_array(bcsv_row_t row, int start_col, const bool* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<bool>(start_col, std::span<const bool>(src, count));
}
void bcsv_row_set_uint8_array(bcsv_row_t row, int start_col, const uint8_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<uint8_t>(start_col, std::span<const uint8_t>(src, count));
}
void bcsv_row_set_uint16_array(bcsv_row_t row, int start_col, const uint16_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<uint16_t>(start_col, std::span<const uint16_t>(src, count));
}
void bcsv_row_set_uint32_array(bcsv_row_t row, int start_col, const uint32_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<uint32_t>(start_col, std::span<const uint32_t>(src, count));
}
void bcsv_row_set_uint64_array(bcsv_row_t row, int start_col, const uint64_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<uint64_t>(start_col, std::span<const uint64_t>(src, count));
}
void bcsv_row_set_int8_array(bcsv_row_t row, int start_col, const int8_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<int8_t>(start_col, std::span<const int8_t>(src, count));
}
void bcsv_row_set_int16_array(bcsv_row_t row, int start_col, const int16_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<int16_t>(start_col, std::span<const int16_t>(src, count));
}
void bcsv_row_set_int32_array(bcsv_row_t row, int start_col, const int32_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<int32_t>(start_col, std::span<const int32_t>(src, count));
}
void bcsv_row_set_int64_array(bcsv_row_t row, int start_col, const int64_t* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<int64_t>(start_col, std::span<const int64_t>(src, count));
}
void bcsv_row_set_float_array(bcsv_row_t row, int start_col, const float* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<float>(start_col, std::span<const float>(src, count));
}
void bcsv_row_set_double_array(bcsv_row_t row, int start_col, const double* src, size_t count) {
    static_cast<bcsv::Row*>(row)->set<double>(start_col, std::span<const double>(src, count));
}

} // extern "C"
