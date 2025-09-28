#include "c_api.h"
#include <string>

#include "layout.h"
#include "reader.h"
#include "row.h"
#include "writer.h"

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

const char* bcsv_reader_filename(const_bcsv_reader_t reader) {
    return static_cast<const bcsv::Reader<bcsv::Layout>*>(reader)->filePath().c_str();
}

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
    return new bcsv::Writer(*static_cast<bcsv::Layout*>(layout));
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

bool bcsv_writer_open(bcsv_writer_t writer, const char* filename) {
    return static_cast<bcsv::Writer<bcsv::Layout>*>(writer)->open(filename);
}

bool bcsv_writer_is_open(const_bcsv_writer_t writer) {
    return static_cast<const bcsv::Writer<bcsv::Layout>*>(writer)->is_open();
}

const char* bcsv_writer_filename(const_bcsv_writer_t writer) {
    return static_cast<const bcsv::Writer<bcsv::Layout>*>(writer)->filePath().c_str();
}

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

} // extern "C"
