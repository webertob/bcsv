/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <nanobind/ndarray.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <optional>
#include <variant>
#include <vector>
#include <string>
#include <bcsv/bcsv.h>
#include <bcsv/sampler/sampler.h>
#include <bcsv/sampler/sampler.hpp>

namespace nb = nanobind;
using namespace nanobind::literals;

// ── Arrow C Data Interface (ABI structs) ───────────────────────────────
// Standardized by Apache Arrow — these structs define the binary interface
// for zero-copy exchange. See https://arrow.apache.org/docs/format/CDataInterface.html

extern "C" {

struct ArrowSchema {
    const char* format;
    const char* name;
    const char* metadata;
    int64_t flags;
    int64_t n_children;
    struct ArrowSchema** children;
    struct ArrowSchema* dictionary;
    void (*release)(struct ArrowSchema*);
    void* private_data;
};

struct ArrowArray {
    int64_t length;
    int64_t null_count;
    int64_t offset;
    int64_t n_buffers;
    int64_t n_children;
    const void** buffers;
    struct ArrowArray** children;
    struct ArrowArray* dictionary;
    void (*release)(struct ArrowArray*);
    void* private_data;
};

} // extern "C"

// ── Type dispatch macro ────────────────────────────────────────────────
// Expands a per-type ACTION(CppType, EnumValue) for all 11 numeric/bool types.
// STRING is intentionally excluded — it needs special handling at each call site.
#define BCSV_FOR_EACH_NUMERIC_TYPE(ACTION) \
    ACTION(bool,     bcsv::ColumnType::BOOL)   \
    ACTION(int8_t,   bcsv::ColumnType::INT8)   \
    ACTION(int16_t,  bcsv::ColumnType::INT16)  \
    ACTION(int32_t,  bcsv::ColumnType::INT32)  \
    ACTION(int64_t,  bcsv::ColumnType::INT64)  \
    ACTION(uint8_t,  bcsv::ColumnType::UINT8)  \
    ACTION(uint16_t, bcsv::ColumnType::UINT16) \
    ACTION(uint32_t, bcsv::ColumnType::UINT32) \
    ACTION(uint64_t, bcsv::ColumnType::UINT64) \
    ACTION(float,    bcsv::ColumnType::FLOAT)  \
    ACTION(double,   bcsv::ColumnType::DOUBLE)

namespace {

    // ── Row → Python helpers ───────────────────────────────────────────

    template<typename RowType>
    [[nodiscard]] inline nb::object extract_column_value(
            const RowType& row, size_t col, bcsv::ColumnType ct) {
        switch (ct) {
#define X(T, E) case E: return nb::cast(row.template get<T>(col));
            BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
            case bcsv::ColumnType::STRING:
                return nb::cast(row.template get<std::string>(col));
            default:
                throw std::runtime_error("Unsupported column type");
        }
    }

    template<typename RowType>
    [[nodiscard]] nb::list row_to_list(const RowType& row, const bcsv::Layout& layout) {
        const size_t n = layout.columnCount();
        nb::list result;
        for (size_t i = 0; i < n; ++i)
            result.append(extract_column_value(row, i, layout.columnType(i)));
        return result;
    }

    // ── Python → Row helpers ───────────────────────────────────────────

    template<typename T>
    T convert_numeric(const nb::object& value, const std::string& target_type) {
        try {
            return nb::cast<T>(value);
        } catch (const nb::cast_error&) {
            if (nb::isinstance<nb::int_>(value))
                return static_cast<T>(nb::cast<int64_t>(value));
            if (nb::isinstance<nb::float_>(value))
                return static_cast<T>(nb::cast<double>(value));
            if (nb::isinstance<nb::str>(value)) {
                std::string s = nb::cast<std::string>(value);
                try {
                    if constexpr (std::is_same_v<T, bool>) {
                        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                        if (s == "true" || s == "1") return true;
                        if (s == "false" || s == "0") return false;
                        throw std::runtime_error("Invalid boolean string: " + s);
                    } else if constexpr (std::is_integral_v<T>) {
                        return static_cast<T>(std::stoll(s));
                    } else {
                        return static_cast<T>(std::stod(s));
                    }
                } catch (const std::out_of_range&) {
                    throw std::runtime_error("Value out of range for " + target_type + ": " + s);
                } catch (const std::invalid_argument&) {
                    throw std::runtime_error("Invalid numeric string for " + target_type + ": " + s);
                }
            }
            throw std::runtime_error("Cannot convert to " + target_type);
        }
    }

    template<typename RowType>
    inline void set_column_value(RowType& row, size_t col,
                                 bcsv::ColumnType ct, const nb::object& value) {
        try {
            switch (ct) {
#define X(T, E) case E: row.set(col, convert_numeric<T>(value, #T)); break;
                BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
                case bcsv::ColumnType::STRING: {
                    std::string s = nb::isinstance<nb::str>(value)
                        ? nb::cast<std::string>(value)
                        : nb::cast<std::string>(nb::str(value));
                    if (s.size() > bcsv::MAX_STRING_LENGTH)
                        throw std::runtime_error(
                            "String at column " + std::to_string(col) +
                            " exceeds maximum length (" + std::to_string(s.size()) +
                            " > " + std::to_string(bcsv::MAX_STRING_LENGTH) + ")");
                    row.set(col, std::move(s));
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported column type");
            }
        } catch (const nb::cast_error& e) {
            throw std::runtime_error("Type conversion error for column " +
                std::to_string(col) + ": " + e.what());
        }
    }

    // ── Cached layout metadata for hot loops ───────────────────────────

    class OptimizedRowWriter {
        std::vector<bcsv::ColumnType> col_types_;
        size_t n_;
    public:
        explicit OptimizedRowWriter(const bcsv::Layout& layout)
            : n_(layout.columnCount()) {
            col_types_.reserve(n_);
            for (size_t i = 0; i < n_; ++i)
                col_types_.emplace_back(layout.columnType(i));
        }
        template<typename RowType>
        void write_row_fast(RowType& row, const nb::list& values) const {
            for (size_t i = 0; i < n_; ++i)
                set_column_value(row, i, col_types_[i], values[i]);
        }
    };

    // ── PyWriter: variant-based codec selection ────────────────────────

    struct PyWriter {
        using FlatWriter  = bcsv::WriterFlat<bcsv::Layout>;
        using ZoHWriter   = bcsv::WriterZoH<bcsv::Layout>;
        using DeltaWriter = bcsv::WriterDelta<bcsv::Layout>;
        using WriterVariant = std::variant<FlatWriter, ZoHWriter, DeltaWriter>;

        std::optional<WriterVariant> writer_;
        std::optional<OptimizedRowWriter> cached_row_writer;
        std::string codec_name_;

        explicit PyWriter(const bcsv::Layout& layout, const std::string& row_codec = "delta")
            : codec_name_(row_codec) {
            if      (row_codec == "flat")  writer_.emplace(std::in_place_type<FlatWriter>, layout);
            else if (row_codec == "zoh")   writer_.emplace(std::in_place_type<ZoHWriter>, layout);
            else if (row_codec == "delta") writer_.emplace(std::in_place_type<DeltaWriter>, layout);
            else throw std::runtime_error("Unknown row codec: '" + row_codec +
                                          "'. Use 'flat', 'zoh', or 'delta'.");
        }

        template<typename F> decltype(auto) visit(F&& f) {
            return std::visit(std::forward<F>(f), *writer_);
        }

        OptimizedRowWriter& ensureCachedWriter() {
            if (!cached_row_writer.has_value())
                cached_row_writer.emplace(visit([](auto& w) -> const bcsv::Layout& { return w.layout(); }));
            return *cached_row_writer;
        }

        void invalidateCache() { cached_row_writer.reset(); }
    };

    // ── Numpy columnar I/O helpers ─────────────────────────────────────

    inline nb::object bcsv_type_to_numpy_dtype(bcsv::ColumnType ct) {
        auto np = nb::module_::import_("numpy");
        switch (ct) {
            case bcsv::ColumnType::BOOL:   return np.attr("dtype")("bool");
            case bcsv::ColumnType::INT8:   return np.attr("dtype")("int8");
            case bcsv::ColumnType::INT16:  return np.attr("dtype")("int16");
            case bcsv::ColumnType::INT32:  return np.attr("dtype")("int32");
            case bcsv::ColumnType::INT64:  return np.attr("dtype")("int64");
            case bcsv::ColumnType::UINT8:  return np.attr("dtype")("uint8");
            case bcsv::ColumnType::UINT16: return np.attr("dtype")("uint16");
            case bcsv::ColumnType::UINT32: return np.attr("dtype")("uint32");
            case bcsv::ColumnType::UINT64: return np.attr("dtype")("uint64");
            case bcsv::ColumnType::FLOAT:  return np.attr("dtype")("float32");
            case bcsv::ColumnType::DOUBLE: return np.attr("dtype")("float64");
            default:                        return np.attr("dtype")("object");
        }
    }

    template<typename RowType>
    inline void fill_numpy_cell(const RowType& row, size_t col,
                                bcsv::ColumnType ct, void* buf, size_t row_idx) {
        switch (ct) {
#define X(T, E) case E: static_cast<T*>(buf)[row_idx] = row.template get<T>(col); break;
            BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
            default: break; // strings handled separately
        }
    }

    template<typename RowType>
    inline void set_from_numpy(RowType& row, size_t col,
                               bcsv::ColumnType ct, const void* buf, size_t row_idx) {
        switch (ct) {
#define X(T, E) case E: row.set(col, static_cast<const T*>(buf)[row_idx]); break;
            BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
            default: break; // strings handled separately
        }
    }

    // ── Shared columnar write loop ────────────────────────────────────
    // Used by both write_columns and write_from_arrow.

    inline void write_columnar_core(
            const bcsv::Layout& layout,
            const std::string& row_codec,
            const std::string& filename,
            size_t num_rows, size_t num_cols,
            const std::vector<const void*>& bufs,
            const std::vector<std::vector<std::string>>& string_cols,
            const std::vector<bool>& is_string,
            const std::vector<bcsv::ColumnType>& col_types,
            size_t compression_level,
            bcsv::FileFlags flags) {
        PyWriter pw(layout, row_codec);
        pw.visit([&](auto& w) {
            nb::gil_scoped_release release;
            if (!w.open(filename, true, compression_level, 64, flags))
                throw std::runtime_error("Failed to open file for writing: " + filename);
            for (size_t r = 0; r < num_rows; ++r) {
                auto& row = w.row();
                for (size_t c = 0; c < num_cols; ++c) {
                    if (is_string[c]) {
                        row.set(c, string_cols[c][r]);
                    } else {
                        set_from_numpy(row, c, col_types[c], bufs[c], r);
                    }
                }
                w.writeRow();
            }
            w.close();
        });
    }

    // ── Shared binding helpers for reader-like / writer-like classes ───
    // Attaches read_row, read_all, __enter__, __exit__, __iter__, __next__
    // to any class that has .readNext(), .row(), .layout(), .close().

    template<typename ReaderT, typename PyClassT>
    void bind_reader_iteration(PyClassT& cls) {
        cls
        .def("read_row", [](ReaderT& r) -> nb::object {
            if (!r.readNext()) return nb::none();
            return row_to_list(r.row(), r.layout());
        })
        .def("read_all", [](ReaderT& r) -> nb::list {
            nb::list result;
            const auto& layout = r.layout();
            const size_t n = layout.columnCount();
            // Cache column types to avoid per-row layout lookups
            std::vector<bcsv::ColumnType> col_types(n);
            for (size_t i = 0; i < n; ++i)
                col_types[i] = layout.columnType(i);
            while (r.readNext()) {
                const auto& row = r.row();
                nb::list row_list;
                for (size_t i = 0; i < n; ++i)
                    row_list.append(extract_column_value(row, i, col_types[i]));
                result.append(std::move(row_list));
            }
            return result;
        })
        .def("__enter__", [](ReaderT& r) -> ReaderT& { return r; }, nb::rv_policy::reference)
        .def("__exit__", [](ReaderT& r, nb::args) { r.close(); })
        .def("__iter__", [](ReaderT& r) -> ReaderT& { return r; }, nb::rv_policy::reference)
        .def("__next__", [](ReaderT& r) -> nb::object {
            if (!r.readNext()) throw nb::stop_iteration();
            return row_to_list(r.row(), r.layout());
        });
    }

    // Attaches write_row, write_rows to any class that has .row(), .layout(), .writeRow().
    template<typename WriterT, typename PyClassT>
    void bind_writer_rows(PyClassT& cls) {
        cls
        .def("write_row", [](WriterT& w, const nb::list& values) {
            auto& row = w.row();
            const auto& layout = row.layout();
            if (values.size() != layout.columnCount())
                throw std::runtime_error("Row length mismatch: expected " +
                    std::to_string(layout.columnCount()) + ", got " + std::to_string(values.size()));
            for (size_t i = 0; i < values.size(); ++i)
                set_column_value(row, i, layout.columnType(i), values[i]);
            return w.writeRow();
        })
        .def("write_rows", [](WriterT& w, const nb::list& rows) {
            const auto& layout = w.layout();
            const size_t expected = layout.columnCount();
            for (size_t i = 0; i < rows.size(); ++i) {
                nb::list vals = nb::cast<nb::list>(rows[i]);
                if (vals.size() != expected)
                    throw std::runtime_error("Row " + std::to_string(i) + " length mismatch: expected " +
                        std::to_string(expected) + ", got " + std::to_string(vals.size()));
                auto& row = w.row();
                for (size_t j = 0; j < expected; ++j)
                    set_column_value(row, j, layout.columnType(j), vals[j]);
                w.writeRow();
            }
        }, "Write multiple rows efficiently");
    }

    // ── Arrow C Data Interface helpers ─────────────────────────────────

    // Returns Arrow format string for a BCSV column type
    const char* bcsv_type_to_arrow_format(bcsv::ColumnType ct) {
        switch (ct) {
            case bcsv::ColumnType::BOOL:   return "b";
            case bcsv::ColumnType::INT8:   return "c";
            case bcsv::ColumnType::INT16:  return "s";
            case bcsv::ColumnType::INT32:  return "i";
            case bcsv::ColumnType::INT64:  return "l";
            case bcsv::ColumnType::UINT8:  return "C";
            case bcsv::ColumnType::UINT16: return "S";
            case bcsv::ColumnType::UINT32: return "I";
            case bcsv::ColumnType::UINT64: return "L";
            case bcsv::ColumnType::FLOAT:  return "f";
            case bcsv::ColumnType::DOUBLE: return "g";
            case bcsv::ColumnType::STRING: return "u"; // utf-8
            default: throw std::runtime_error("Unsupported column type for Arrow");
        }
    }

    size_t bcsv_type_byte_width(bcsv::ColumnType ct) {
        switch (ct) {
            case bcsv::ColumnType::BOOL:   return 1; // stored as bytes before bit-packing
            case bcsv::ColumnType::INT8:   return 1;
            case bcsv::ColumnType::INT16:  return 2;
            case bcsv::ColumnType::INT32:  return 4;
            case bcsv::ColumnType::INT64:  return 8;
            case bcsv::ColumnType::UINT8:  return 1;
            case bcsv::ColumnType::UINT16: return 2;
            case bcsv::ColumnType::UINT32: return 4;
            case bcsv::ColumnType::UINT64: return 8;
            case bcsv::ColumnType::FLOAT:  return 4;
            case bcsv::ColumnType::DOUBLE: return 8;
            default: return 0;
        }
    }

    // Private data for release callbacks — owns allocated memory
    struct ArrowColumnData {
        std::vector<uint8_t> data_buf;       // numeric data or bool bit-packed
        std::vector<int32_t> offsets_buf;     // string offsets
        std::vector<char> string_data_buf;    // concatenated string bytes
        std::vector<const void*> buffer_ptrs; // buffer pointer array for ArrowArray
        char* name_copy = nullptr;            // schema name (heap-allocated copy)

        ~ArrowColumnData() {
            delete[] name_copy;
        }
    };

    void release_arrow_schema(ArrowSchema* schema) {
        if (!schema->release) return;
        // Free children schemas
        if (schema->children) {
            for (int64_t i = 0; i < schema->n_children; ++i) {
                if (schema->children[i] && schema->children[i]->release) {
                    schema->children[i]->release(schema->children[i]);
                }
                delete schema->children[i];
            }
            delete[] schema->children;
        }
        auto* priv = static_cast<ArrowColumnData*>(schema->private_data);
        delete priv;
        schema->release = nullptr;
    }

    void release_arrow_array(ArrowArray* array) {
        if (!array->release) return;
        // Free children arrays
        if (array->children) {
            for (int64_t i = 0; i < array->n_children; ++i) {
                if (array->children[i] && array->children[i]->release) {
                    array->children[i]->release(array->children[i]);
                }
                delete array->children[i];
            }
            delete[] array->children;
        }
        auto* priv = static_cast<ArrowColumnData*>(array->private_data);
        delete priv;
        array->release = nullptr;
    }

    void release_arrow_child_schema(ArrowSchema* schema) {
        if (!schema->release) return;
        auto* priv = static_cast<ArrowColumnData*>(schema->private_data);
        delete priv;
        schema->release = nullptr;
    }

    void release_arrow_child_array(ArrowArray* array) {
        if (!array->release) return;
        auto* priv = static_cast<ArrowColumnData*>(array->private_data);
        delete priv;
        array->release = nullptr;
    }
}

NB_MODULE(_bcsv, m) {
    m.doc() = "Python bindings for the BCSV (Binary CSV) library";

    // ColumnType enum
    nb::enum_<bcsv::ColumnType>(m, "ColumnType")
        .value("BOOL", bcsv::ColumnType::BOOL)
        .value("UINT8", bcsv::ColumnType::UINT8)
        .value("UINT16", bcsv::ColumnType::UINT16)
        .value("UINT32", bcsv::ColumnType::UINT32)
        .value("UINT64", bcsv::ColumnType::UINT64)
        .value("INT8", bcsv::ColumnType::INT8)
        .value("INT16", bcsv::ColumnType::INT16)
        .value("INT32", bcsv::ColumnType::INT32)
        .value("INT64", bcsv::ColumnType::INT64)
        .value("FLOAT", bcsv::ColumnType::FLOAT)
        .value("DOUBLE", bcsv::ColumnType::DOUBLE)
        .value("STRING", bcsv::ColumnType::STRING)
        .export_values();

    // ColumnDefinition struct
    nb::class_<bcsv::ColumnDefinition>(m, "ColumnDefinition")
        .def(nb::init<const std::string&, bcsv::ColumnType>(),
             nb::arg("name"), nb::arg("type"))
        .def_rw("name", &bcsv::ColumnDefinition::name)
        .def_rw("type", &bcsv::ColumnDefinition::type)
        .def("__repr__", [](const bcsv::ColumnDefinition& col) {
            return "<ColumnDefinition name='" + col.name + "' type=" +
                   std::string(bcsv::toString(col.type)) + ">";
        });

    // Layout class
    nb::class_<bcsv::Layout>(m, "Layout")
        .def(nb::init<>())
        .def(nb::init<const std::vector<bcsv::ColumnDefinition>&>())
        .def("add_column", [](bcsv::Layout& layout, const bcsv::ColumnDefinition& col) {
            return layout.addColumn(col);
        }, nb::arg("column"))
        .def("add_column", [](bcsv::Layout& layout, const std::string& name, bcsv::ColumnType type) {
            return layout.addColumn({name, type});
        }, nb::arg("name"), nb::arg("type"))
        .def("column_count", [](const bcsv::Layout& l) { return l.columnCount(); })
        .def("column_name", &bcsv::Layout::columnName, nb::arg("index"))
        .def("column_type", &bcsv::Layout::columnType, nb::arg("index"))
        .def("has_column", &bcsv::Layout::hasColumn, nb::arg("name"))
        .def("column_index", &bcsv::Layout::columnIndex, nb::arg("name"))
        .def("get_column_names", [](const bcsv::Layout& layout) {
            std::vector<std::string> names;
            for (size_t i = 0; i < layout.columnCount(); ++i) {
                names.push_back(layout.columnName(i));
            }
            return names;
        })
        .def("get_column_types", [](const bcsv::Layout& layout) {
            std::vector<bcsv::ColumnType> types;
            for (size_t i = 0; i < layout.columnCount(); ++i) {
                types.push_back(layout.columnType(i));
            }
            return types;
        })
        .def("get_column", [](const bcsv::Layout& layout, size_t index) {
            return bcsv::ColumnDefinition{layout.columnName(index), layout.columnType(index)};
        }, nb::arg("index"))
        .def("__len__", [](const bcsv::Layout& l) { return l.columnCount(); })
        .def("__getitem__", [](const bcsv::Layout& layout, size_t index) {
            if (index >= layout.columnCount()) {
                throw nb::index_error("Column index out of range");
            }
            return bcsv::ColumnDefinition{layout.columnName(index), layout.columnType(index)};
        })
        .def("__repr__", [](const bcsv::Layout& layout) {
            std::stringstream ss;
            ss << layout;
            return ss.str();
        });

    // FileFlags enum — controls codec selection and file structure
    nb::enum_<bcsv::FileFlags>(m, "FileFlags", nb::is_arithmetic())
        .value("NONE", bcsv::FileFlags::NONE)
        .value("ZERO_ORDER_HOLD", bcsv::FileFlags::ZERO_ORDER_HOLD)
        .value("NO_FILE_INDEX", bcsv::FileFlags::NO_FILE_INDEX)
        .value("STREAM_MODE", bcsv::FileFlags::STREAM_MODE)
        .value("BATCH_COMPRESS", bcsv::FileFlags::BATCH_COMPRESS)
        .value("DELTA_ENCODING", bcsv::FileFlags::DELTA_ENCODING)
        .export_values()
        .def("__or__", [](bcsv::FileFlags a, bcsv::FileFlags b) -> int {
            return static_cast<int>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
        })
        .def("__and__", [](bcsv::FileFlags a, bcsv::FileFlags b) -> int {
            return static_cast<int>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
        })
        .def("__invert__", [](bcsv::FileFlags a) -> int {
            return static_cast<int>(static_cast<uint16_t>(~a));
        });

    // Default file flags: batch compression on platforms that support it
#ifdef BCSV_HAS_BATCH_CODEC
    constexpr auto DEFAULT_FILE_FLAGS = bcsv::FileFlags::BATCH_COMPRESS;
#else
    constexpr auto DEFAULT_FILE_FLAGS = bcsv::FileFlags::NONE;
#endif

    // Writer class - variant-based, supports runtime codec selection
    nb::class_<PyWriter>(m, "Writer")
        .def(nb::init<const bcsv::Layout&, const std::string&>(),
             nb::arg("layout"), nb::arg("row_codec") = "delta")
        .def("open", [](PyWriter& pw, const std::string& filename,
                       bool overwrite, size_t compression_level, size_t block_size_kb, bcsv::FileFlags flags) {
            pw.invalidateCache();
            bool success = pw.visit([&](auto& w) {
                return w.open(filename, overwrite, compression_level, block_size_kb, flags);
            });
            if (!success) {
                std::string err = pw.visit([](auto& w) { return w.getErrorMsg(); });
                throw std::runtime_error("Failed to open file for writing: " + filename +
                                         (err.empty() ? "" : " (" + err + ")"));
            }
            return success;
        }, nb::arg("filename"), nb::arg("overwrite") = false, nb::arg("compression_level") = 1,
           nb::arg("block_size_kb") = 64, nb::arg("flags") = DEFAULT_FILE_FLAGS)
        .def("write_row", [](PyWriter& pw, const nb::list& values) {
            const auto& layout = pw.visit([](auto& w) -> const bcsv::Layout& { return w.layout(); });
            if (values.size() != layout.columnCount())
                throw std::runtime_error("Row length mismatch: expected " +
                    std::to_string(layout.columnCount()) + ", got " + std::to_string(values.size()));
            auto& cached_writer = pw.ensureCachedWriter();
            pw.visit([&](auto& w) {
                auto& row = w.row();
                cached_writer.write_row_fast(row, values);
                { nb::gil_scoped_release release; w.writeRow(); }
            });
        })
        .def("write_rows", [](PyWriter& pw, const nb::list& rows) {
            if (rows.size() == 0) return;
            const auto& layout = pw.visit([](auto& w) -> const bcsv::Layout& { return w.layout(); });
            const size_t expected = layout.columnCount();
            auto& cached_writer = pw.ensureCachedWriter();
            pw.visit([&](auto& w) {
                for (size_t i = 0; i < rows.size(); ++i) {
                    nb::list vals = nb::cast<nb::list>(rows[i]);
                    if (vals.size() != expected)
                        throw std::runtime_error("Row " + std::to_string(i) + " length mismatch: expected " +
                            std::to_string(expected) + ", got " + std::to_string(vals.size()));
                    auto& row = w.row();
                    cached_writer.write_row_fast(row, vals);
                    { nb::gil_scoped_release release; w.writeRow(); }
                }
            });
        }, "Write multiple rows efficiently with batching")
        .def("close", [](PyWriter& pw) {
            nb::gil_scoped_release release;
            pw.visit([](auto& w) { w.close(); });
        })
        .def("flush", [](PyWriter& pw) {
            nb::gil_scoped_release release;
            pw.visit([](auto& w) { w.flush(); });
        })
        .def("is_open", [](PyWriter& pw) { return pw.visit([](auto& w) { return w.isOpen(); }); })
        .def("row_count", [](PyWriter& pw) { return pw.visit([](auto& w) { return w.rowCount(); }); })
        .def("row_codec", [](PyWriter& pw) -> const std::string& { return pw.codec_name_; })
        .def("compression_level", [](PyWriter& pw) { return pw.visit([](auto& w) { return w.compressionLevel(); }); })
        .def("layout", [](PyWriter& pw) -> const bcsv::Layout& {
            return pw.visit([](auto& w) -> const bcsv::Layout& { return w.layout(); });
        }, nb::rv_policy::reference_internal)
        .def("__enter__", [](PyWriter& pw) -> PyWriter& { return pw; }, nb::rv_policy::reference)
        .def("__exit__", [](PyWriter& pw, nb::args) {
            pw.visit([](auto& w) { w.close(); });
        })
        .def("__repr__", [](PyWriter& pw) {
            bool open = pw.visit([](auto& w) { return w.isOpen(); });
            size_t rows = pw.visit([](auto& w) { return w.rowCount(); });
            return "<Writer codec='" + pw.codec_name_ + "' open=" +
                   (open ? "True" : "False") + " rows=" + std::to_string(rows) + ">";
        });

    // ── Reader binding ─────────────────────────────────────────────────

    using ReaderT = bcsv::Reader<bcsv::Layout>;
    auto reader_cls = nb::class_<ReaderT>(m, "Reader")
        .def(nb::init<>())
        .def("open", [](ReaderT& reader, const std::string& filename) {
            bool success;
            { nb::gil_scoped_release release; success = reader.open(filename); }
            if (!success) {
                std::string error = reader.getErrorMsg();
                if (error.empty()) error = "Failed to open file for reading: " + filename;
                throw std::runtime_error(error);
            }
            return success;
        }, nb::arg("filename"))
        .def("layout", &ReaderT::layout, nb::rv_policy::reference_internal)
        .def("read_next", [](ReaderT& r) {
            nb::gil_scoped_release release;
            return r.readNext();
        })
        .def("close", [](ReaderT& reader) { nb::gil_scoped_release release; reader.close(); })
        .def("is_open", &ReaderT::isOpen)
        .def("file_flags", [](ReaderT& r) -> int { return static_cast<int>(r.fileFlags()); })
        .def("compression_level", [](ReaderT& r) { return r.compressionLevel(); })
        .def("row_pos", &ReaderT::rowPos)
        .def("version_string", [](ReaderT& r) { return r.fileHeader().versionString(); })
        .def("creation_time", [](ReaderT& r) { return r.fileHeader().getCreationTime(); })
        .def("count_rows", [](ReaderT& reader) -> size_t {
             if (!reader.isOpen())
                 throw std::runtime_error("Reader is not open");
             bcsv::ReaderDirectAccess<bcsv::Layout> da;
             size_t count;
             {
                 nb::gil_scoped_release release;
                 if (!da.open(reader.filePath(), true))
                     throw std::runtime_error("Failed to open file for row counting: " + reader.filePath().string());
                 count = da.rowCount();
                 da.close();
             }
             return count;
        }, "Count the total number of rows in the file")
        .def("row_value", [](ReaderT& reader, size_t col) -> nb::object {
            const auto& layout = reader.layout();
            if (col >= layout.columnCount())
                throw nb::index_error(("Column index " + std::to_string(col) + " out of range").c_str());
            return extract_column_value(reader.row(), col, layout.columnType(col));
        }, nb::arg("column"), "Get a typed value from the current row by column index")
        .def("row_dict", [](ReaderT& reader) -> nb::dict {
            const auto& layout = reader.layout();
            nb::dict result;
            const auto& row = reader.row();
            for (size_t i = 0; i < layout.columnCount(); ++i)
                result[nb::cast(layout.columnName(i))] =
                    extract_column_value(row, i, layout.columnType(i));
            return result;
        }, "Get the current row as a dict {column_name: value}")
        .def("read_batch", [](ReaderT& r, size_t batch_size) -> nb::object {
            const auto& layout = r.layout();
            const size_t num_cols = layout.columnCount();
            if (num_cols == 0)
                throw std::runtime_error("Reader has no columns");

            // Cache column metadata
            std::vector<bcsv::ColumnType> col_types(num_cols);
            std::vector<std::string> col_names(num_cols);
            std::vector<bool> is_str(num_cols, false);
            for (size_t c = 0; c < num_cols; ++c) {
                col_types[c] = layout.columnType(c);
                col_names[c] = layout.columnName(c);
                is_str[c] = (col_types[c] == bcsv::ColumnType::STRING);
            }

            // Allocate numpy arrays (numeric) and C++ string vectors (strings)
            auto np = nb::module_::import_("numpy");
            std::vector<nb::object> arrays(num_cols);
            std::vector<void*> bufs(num_cols, nullptr);
            std::vector<std::vector<std::string>> string_cols(num_cols);

            for (size_t c = 0; c < num_cols; ++c) {
                if (is_str[c]) {
                    string_cols[c].reserve(batch_size);
                } else {
                    nb::object shape = nb::make_tuple(static_cast<int64_t>(batch_size));
                    arrays[c] = np.attr("empty")(shape,
                        "dtype"_a = bcsv_type_to_numpy_dtype(col_types[c]));
                    bufs[c] = reinterpret_cast<void*>(
                        nb::cast<intptr_t>(arrays[c].attr("ctypes").attr("data")));
                }
            }

            // Read loop under GIL release
            size_t rows_read = 0;
            {
                nb::gil_scoped_release release;
                while (rows_read < batch_size && r.readNext()) {
                    const auto& row = r.row();
                    for (size_t c = 0; c < num_cols; ++c) {
                        if (is_str[c]) {
                            string_cols[c].emplace_back(row.template get<std::string>(c));
                        } else {
                            fill_numpy_cell(row, c, col_types[c], bufs[c], rows_read);
                        }
                    }
                    ++rows_read;
                }
            }

            if (rows_read == 0) return nb::none();

            // Build result dict
            nb::dict result;
            for (size_t c = 0; c < num_cols; ++c) {
                if (is_str[c]) {
                    nb::list str_list;
                    for (auto& s : string_cols[c])
                        str_list.append(nb::cast(std::move(s)));
                    result[nb::cast(col_names[c])] = std::move(str_list);
                } else {
                    if (rows_read < batch_size) {
                        // Slice to actual row count
                        auto stop = nb::int_(static_cast<int64_t>(rows_read));
                        auto sl = nb::steal(PySlice_New(Py_None, stop.ptr(), Py_None));
                        arrays[c] = arrays[c][sl];
                    }
                    result[nb::cast(col_names[c])] = std::move(arrays[c]);
                }
            }
            return result;
        }, nb::arg("batch_size") = 10000,
           "Read up to batch_size rows into a dict of numpy arrays/lists. Returns None at EOF.");
    bind_reader_iteration<ReaderT>(reader_cls);
    reader_cls.def("__repr__", [](ReaderT& r) {
        bool open = r.isOpen();
        size_t pos = r.rowPos();
        return std::string("<Reader open=") + (open ? "True" : "False") +
               " row_pos=" + std::to_string(pos) + ">";
    });

    // Utility functions
    m.def("type_to_string",
         [](bcsv::ColumnType t) { return std::string(bcsv::toString(t)); },
         "Convert ColumnType to string");

    // ── CsvWriter binding ──────────────────────────────────────────────

    using CsvWriterT = bcsv::CsvWriter<bcsv::Layout>;
    auto csv_writer_cls = nb::class_<CsvWriterT>(m, "CsvWriter")
        .def(nb::init<const bcsv::Layout&, char, char>(),
             nb::arg("layout"), nb::arg("delimiter") = ',', nb::arg("decimal_sep") = '.')
        .def("open", [](CsvWriterT& w, const std::string& filename,
                        bool overwrite, bool include_header) {
            bool success = w.open(filename, overwrite, include_header);
            if (!success) {
                std::string err = w.getErrorMsg();
                if (err.empty()) err = "Failed to open CSV file for writing: " + filename;
                throw std::runtime_error(err);
            }
            return success;
        }, nb::arg("filename"), nb::arg("overwrite") = false, nb::arg("include_header") = true)
        .def("close", [](CsvWriterT& w) { w.close(); })
        .def("is_open", &CsvWriterT::isOpen)
        .def("row_count", &CsvWriterT::rowCount)
        .def("layout", &CsvWriterT::layout, nb::rv_policy::reference_internal)
        .def("delimiter", &CsvWriterT::delimiter)
        .def("decimal_separator", &CsvWriterT::decimalSeparator)
        .def("__enter__", [](CsvWriterT& w) -> CsvWriterT& { return w; }, nb::rv_policy::reference)
        .def("__exit__", [](CsvWriterT& w, nb::args) { w.close(); });
    bind_writer_rows<CsvWriterT>(csv_writer_cls);

    // ── CsvReader binding ──────────────────────────────────────────────

    using CsvReaderT = bcsv::CsvReader<bcsv::Layout>;
    auto csv_reader_cls = nb::class_<CsvReaderT>(m, "CsvReader")
        .def(nb::init<const bcsv::Layout&, char, char>(),
             nb::arg("layout"), nb::arg("delimiter") = ',', nb::arg("decimal_sep") = '.')
        .def("open", [](CsvReaderT& r, const std::string& filename, bool has_header) {
            bool success = r.open(filename, has_header);
            if (!success) {
                std::string err = r.getErrorMsg();
                if (err.empty()) err = "Failed to open CSV file for reading: " + filename;
                throw std::runtime_error(err);
            }
            return success;
        }, nb::arg("filename"), nb::arg("has_header") = true)
        .def("read_next", &CsvReaderT::readNext)
        .def("close", [](CsvReaderT& r) { r.close(); })
        .def("is_open", &CsvReaderT::isOpen)
        .def("row_pos", &CsvReaderT::rowPos)
        .def("file_line", &CsvReaderT::fileLine)
        .def("layout", &CsvReaderT::layout, nb::rv_policy::reference_internal)
        .def("delimiter", &CsvReaderT::delimiter)
        .def("decimal_separator", &CsvReaderT::decimalSeparator)
        .def("error_msg", &CsvReaderT::getErrorMsg);
    bind_reader_iteration<CsvReaderT>(csv_reader_cls);

    // ── ReaderDirectAccess binding ─────────────────────────────────────

    using DaReaderT = bcsv::ReaderDirectAccess<bcsv::Layout>;
    nb::class_<DaReaderT>(m, "ReaderDirectAccess")
        .def(nb::init<>())
        .def("open", [](DaReaderT& r, const std::string& filename, bool rebuild_footer) {
            bool success;
            { nb::gil_scoped_release release; success = r.open(filename, rebuild_footer); }
            if (!success) {
                std::string err = r.getErrorMsg();
                if (err.empty()) err = "Failed to open file: " + filename;
                throw std::runtime_error(err);
            }
            return success;
        }, nb::arg("filename"), nb::arg("rebuild_footer") = false)
        .def("read", [](DaReaderT& r, size_t index) {
            bool success;
            { nb::gil_scoped_release release; success = r.read(index); }
            if (!success)
                throw nb::index_error(("Row index " + std::to_string(index) + " out of range").c_str());
            return row_to_list(r.row(), r.layout());
        }, nb::arg("index"))
        .def("row_count", &DaReaderT::rowCount)
        .def("layout", &DaReaderT::layout, nb::rv_policy::reference_internal)
        .def("close", [](DaReaderT& r) { nb::gil_scoped_release release; r.close(); })
        .def("is_open", &DaReaderT::isOpen)
        .def("file_flags", [](DaReaderT& r) -> int { return static_cast<int>(r.fileFlags()); })
        .def("compression_level", [](DaReaderT& r) { return r.compressionLevel(); })
        .def("version_string", [](DaReaderT& r) { return r.fileHeader().versionString(); })
        .def("creation_time", [](DaReaderT& r) { return r.fileHeader().getCreationTime(); })
        .def("__enter__", [](DaReaderT& r) -> DaReaderT& { return r; }, nb::rv_policy::reference)
        .def("__exit__", [](DaReaderT& r, nb::args) { r.close(); })
        .def("__len__", &DaReaderT::rowCount)
        .def("__getitem__", [](DaReaderT& r, size_t index) {
            bool success;
            { nb::gil_scoped_release release; success = r.read(index); }
            if (!success)
                throw nb::index_error(("Row index " + std::to_string(index) + " out of range").c_str());
            return row_to_list(r.row(), r.layout());
        })
        .def("__repr__", [](DaReaderT& r) {
            bool open = r.isOpen();
            size_t rows = open ? r.rowCount() : 0;
            return std::string("<ReaderDirectAccess open=") + (open ? "True" : "False") +
                   " rows=" + std::to_string(rows) + ">";
        });

    // ── Sampler enums ──────────────────────────────────────────────────

    nb::enum_<bcsv::SamplerMode>(m, "SamplerMode")
        .value("TRUNCATE", bcsv::SamplerMode::TRUNCATE)
        .value("EXPAND", bcsv::SamplerMode::EXPAND)
        .export_values();

    nb::enum_<bcsv::SamplerErrorPolicy>(m, "SamplerErrorPolicy")
        .value("THROW", bcsv::SamplerErrorPolicy::THROW)
        .value("SKIP_ROW", bcsv::SamplerErrorPolicy::SKIP_ROW)
        .value("SATURATE", bcsv::SamplerErrorPolicy::SATURATE)
        .export_values();

    nb::class_<bcsv::SamplerCompileResult>(m, "SamplerCompileResult")
        .def_ro("success", &bcsv::SamplerCompileResult::success)
        .def_ro("error_msg", &bcsv::SamplerCompileResult::error_msg)
        .def_ro("error_position", &bcsv::SamplerCompileResult::error_position)
        .def("__bool__", [](const bcsv::SamplerCompileResult& r) { return r.success; })
        .def("__repr__", [](const bcsv::SamplerCompileResult& r) {
            if (r.success) return std::string("<SamplerCompileResult: OK>");
            return std::string("<SamplerCompileResult: ERROR at pos ") +
                   std::to_string(r.error_position) + ": " + r.error_msg + ">";
        });

    // ── Sampler binding ────────────────────────────────────────────────

    nb::class_<bcsv::Sampler<bcsv::Layout>>(m, "Sampler")
        .def(nb::init<bcsv::Reader<bcsv::Layout>&>(),
             nb::arg("reader"), nb::keep_alive<1, 2>())
        .def("set_conditional", [](bcsv::Sampler<bcsv::Layout>& s, const std::string& expr) {
                 return s.setConditional(expr);
             }, nb::arg("expr"))
        .def("get_conditional", &bcsv::Sampler<bcsv::Layout>::getConditional)
        .def("set_selection", [](bcsv::Sampler<bcsv::Layout>& s, const std::string& expr) {
                 return s.setSelection(expr);
             }, nb::arg("expr"))
        .def("get_selection", &bcsv::Sampler<bcsv::Layout>::getSelection)
        .def("set_mode", &bcsv::Sampler<bcsv::Layout>::setMode, nb::arg("mode"))
        .def("get_mode", &bcsv::Sampler<bcsv::Layout>::getMode)
        .def("set_error_policy", &bcsv::Sampler<bcsv::Layout>::setErrorPolicy,
             nb::arg("policy"))
        .def("get_error_policy", &bcsv::Sampler<bcsv::Layout>::getErrorPolicy)
        .def("output_layout", &bcsv::Sampler<bcsv::Layout>::outputLayout,
             nb::rv_policy::reference_internal)
        .def("next", [](bcsv::Sampler<bcsv::Layout>& s) {
            bool has_next;
            {
                nb::gil_scoped_release release;
                has_next = s.next();
            }
            return has_next;
        })
        .def("row", [](bcsv::Sampler<bcsv::Layout>& s) {
            const auto& row = s.row();
            return row_to_list(row, row.layout());
        })
        .def("source_row_pos", &bcsv::Sampler<bcsv::Layout>::sourceRowPos)
        .def("bulk", [](bcsv::Sampler<bcsv::Layout>& s) {
            nb::list result;
            while (true) {
                bool has_next;
                {
                    nb::gil_scoped_release release;
                    has_next = s.next();
                }
                if (!has_next) break;
                const auto& row = s.row();
                result.append(row_to_list(row, row.layout()));
            }
            return result;
        })
        .def("is_conditional_passthrough", &bcsv::Sampler<bcsv::Layout>::isConditionalPassthrough)
        .def("is_selection_passthrough", &bcsv::Sampler<bcsv::Layout>::isSelectionPassthrough)
        .def("window_capacity", &bcsv::Sampler<bcsv::Layout>::windowCapacity)
        .def("disassemble", &bcsv::Sampler<bcsv::Layout>::disassemble)
        .def("__iter__", [](bcsv::Sampler<bcsv::Layout>& s) -> bcsv::Sampler<bcsv::Layout>& {
            return s;
        }, nb::rv_policy::reference)
        .def("__next__", [](bcsv::Sampler<bcsv::Layout>& s) -> nb::object {
            bool has_next;
            {
                nb::gil_scoped_release release;
                has_next = s.next();
            }
            if (!has_next) throw nb::stop_iteration();
            const auto& row = s.row();
            return row_to_list(row, row.layout());
        });

    // ── Columnar I/O: read_columns ─────────────────────────────────────

    m.def("read_columns", [](const std::string& filename) -> nb::dict {
        // Phase 1: Single open via ReaderDirectAccess for count + sequential read
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        {
            nb::gil_scoped_release release;
            if (!reader.open(filename, true)) {
                throw std::runtime_error("Failed to open file: " + filename);
            }
        }
        const size_t num_rows = reader.rowCount();
        const auto& layout = reader.layout();
        const size_t num_cols = layout.columnCount();
        if (num_cols == 0)
            throw std::runtime_error("File has no columns: " + filename);

        // Cache column metadata
        std::vector<std::string> col_names(num_cols);
        std::vector<bcsv::ColumnType> col_types(num_cols);
        std::vector<bool> is_string(num_cols, false);
        for (size_t i = 0; i < num_cols; ++i) {
            col_names[i] = layout.columnName(i);
            col_types[i] = layout.columnType(i);
            is_string[i] = (col_types[i] == bcsv::ColumnType::STRING);
        }

        // Phase 2: Allocate numpy arrays (numeric) and C++ string vectors (strings)
        auto np = nb::module_::import_("numpy");
        std::vector<nb::object> arrays(num_cols);
        std::vector<void*> bufs(num_cols, nullptr);
        std::vector<std::vector<std::string>> string_cols(num_cols);

        for (size_t c = 0; c < num_cols; ++c) {
            if (is_string[c]) {
                string_cols[c].reserve(num_rows);
            } else {
                nb::object shape = nb::make_tuple(static_cast<int64_t>(num_rows));
                arrays[c] = np.attr("empty")(shape, "dtype"_a = bcsv_type_to_numpy_dtype(col_types[c]));
                // Get mutable data pointer via numpy array's ctypes interface
                auto arr_obj = arrays[c];
                bufs[c] = reinterpret_cast<void*>(nb::cast<intptr_t>(arr_obj.attr("ctypes").attr("data")));
            }
        }

        // Phase 3: Entire read loop under GIL release — strings stay in C++
        {
            nb::gil_scoped_release release;
            size_t row_idx = 0;
            while (reader.readNext()) {
                const auto& row = reader.row();
                for (size_t c = 0; c < num_cols; ++c) {
                    if (is_string[c]) {
                        string_cols[c].emplace_back(row.template get<std::string>(c));
                    } else {
                        fill_numpy_cell(row, c, col_types[c], bufs[c], row_idx);
                    }
                }
                ++row_idx;
            }
            reader.close();
        }

        // Phase 4: Build result dict — convert C++ strings to Python lists
        nb::dict result;
        for (size_t c = 0; c < num_cols; ++c) {
            if (is_string[c]) {
                nb::list str_list;
                for (size_t i = 0; i < string_cols[c].size(); ++i) {
                    str_list.append(nb::cast(std::move(string_cols[c][i])));
                }
                result[nb::cast(col_names[c])] = std::move(str_list);
            } else {
                result[nb::cast(col_names[c])] = std::move(arrays[c]);
            }
        }
        return result;
    }, nb::arg("filename"),
       "Read a BCSV file into a dict of numpy arrays (numeric) and lists (strings)");

    // ── Columnar I/O: write_columns ────────────────────────────────────

    m.def("write_columns", [](const std::string& filename, const nb::dict& columns,
                              const nb::list& col_order, const nb::list& col_types_py,
                              const std::string& row_codec,
                              size_t compression_level,
                              bcsv::FileFlags flags) {
        // Build layout
        const size_t num_cols = col_order.size();
        if (num_cols == 0)
            throw std::runtime_error("Cannot write file with empty layout");
        if (col_types_py.size() != num_cols) {
            throw std::runtime_error("col_order and col_types must have equal length");
        }

        bcsv::Layout layout;
        std::vector<bcsv::ColumnType> col_types(num_cols);
        std::vector<std::string> col_names(num_cols);
        for (size_t i = 0; i < num_cols; ++i) {
            col_names[i] = nb::cast<std::string>(col_order[i]);
            col_types[i] = nb::cast<bcsv::ColumnType>(col_types_py[i]);
            layout.addColumn({col_names[i], col_types[i]});
        }

        // Gather column data — numpy arrays and pre-extract strings into C++ vectors
        std::vector<const void*> bufs(num_cols, nullptr);
        std::vector<nb::object> owned_arrays(num_cols);
        std::vector<std::vector<std::string>> string_cols(num_cols);
        std::vector<bool> is_string(num_cols, false);
        size_t num_rows = 0;
        auto np = nb::module_::import_("numpy");

        for (size_t c = 0; c < num_cols; ++c) {
            nb::object col_data = columns[nb::cast(col_names[c])];
            size_t col_len = 0;
            if (col_types[c] == bcsv::ColumnType::STRING) {
                is_string[c] = true;
                nb::list str_list = nb::cast<nb::list>(col_data);
                col_len = str_list.size();
                string_cols[c].reserve(col_len);
                for (size_t i = 0; i < col_len; ++i) {
                    string_cols[c].emplace_back(nb::cast<std::string>(str_list[i]));
                }
            } else {
                owned_arrays[c] = np.attr("ascontiguousarray")(col_data,
                    "dtype"_a = bcsv_type_to_numpy_dtype(col_types[c]));
                bufs[c] = reinterpret_cast<const void*>(
                    nb::cast<intptr_t>(owned_arrays[c].attr("ctypes").attr("data")));
                col_len = static_cast<size_t>(nb::cast<int64_t>(owned_arrays[c].attr("size")));
            }
            if (c == 0) {
                num_rows = col_len;
            } else if (col_len != num_rows) {
                throw std::runtime_error("Column '" + col_names[c] +
                    "' has " + std::to_string(col_len) + " rows, expected " +
                    std::to_string(num_rows));
            }
        }

        // Write via shared helper
        write_columnar_core(layout, row_codec, filename, num_rows, num_cols,
                            bufs, string_cols, is_string, col_types,
                            compression_level, flags);
    }, nb::arg("filename"), nb::arg("columns"), nb::arg("col_order"),
       nb::arg("col_types"), nb::arg("row_codec") = "delta",
       nb::arg("compression_level") = 1,
       nb::arg("flags") = bcsv::FileFlags::BATCH_COMPRESS,
       "Write a dict of numpy arrays/lists to a BCSV file");

    // ── Arrow C Data Interface: read_to_arrow ──────────────────────────

    m.def("read_to_arrow", [](const std::string& filename,
                               const std::optional<nb::list>& columns,
                               int64_t chunk_size) -> nb::object {
        // Import pyarrow (optional dependency)
        nb::object pa;
        try {
            pa = nb::module_::import_("pyarrow");
        } catch (...) {
            throw std::runtime_error(
                "pyarrow is required for Arrow interop. Install it with: pip install pyarrow");
        }

        // Open the file
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        {
            nb::gil_scoped_release release;
            if (!reader.open(filename, true))
                throw std::runtime_error("Failed to open file: " + filename);
        }
        const size_t total_rows = reader.rowCount();
        const auto& layout = reader.layout();
        const size_t num_cols = layout.columnCount();
        if (num_cols == 0)
            throw std::runtime_error("File has no columns: " + filename);

        // Determine which columns to read
        std::vector<size_t> col_indices;
        std::vector<std::string> col_names;
        std::vector<bcsv::ColumnType> col_types;
        if (columns.has_value()) {
            const nb::list& cols = columns.value();
            for (size_t i = 0; i < cols.size(); ++i) {
                std::string name = nb::cast<std::string>(cols[i]);
                if (!layout.hasColumn(name))
                    throw std::runtime_error("Column not found: " + name);
                size_t idx = layout.columnIndex(name);
                col_indices.push_back(idx);
                col_names.push_back(name);
                col_types.push_back(layout.columnType(idx));
            }
        } else {
            for (size_t i = 0; i < num_cols; ++i) {
                col_indices.push_back(i);
                col_names.push_back(layout.columnName(i));
                col_types.push_back(layout.columnType(i));
            }
        }
        const size_t out_cols = col_indices.size();

        // Lambda to build a RecordBatch from a chunk of rows
        // Takes numeric_bufs by non-const ref since we move-steal buffers for zero-copy.
        auto build_batch = [&](size_t batch_rows,
                               std::vector<std::vector<uint8_t>>& numeric_bufs,
                               const std::vector<std::vector<std::string>>& string_bufs) -> nb::object {
            // Stack-allocate top-level struct shells. pyarrow's _import_from_c
            // calls release callbacks which free children + heap private_data.
            // The struct shells themselves are freed by stack unwinding.
            ArrowSchema schema{};
            ArrowArray array{};

            // Track all heap allocations for exception-safety cleanup.
            // On success (after _import_from_c), ownership transfers to pyarrow
            // and we clear the tracking vectors. On exception, cleanup frees everything.
            std::vector<ArrowColumnData*> alloc_privs;  // all heap ArrowColumnData
            alloc_privs.reserve(2 + 2 * out_cols);
            std::vector<ArrowSchema*> alloc_child_schemas;
            alloc_child_schemas.reserve(out_cols);
            std::vector<ArrowArray*> alloc_child_arrays;
            alloc_child_arrays.reserve(out_cols);

            auto cleanup = [&]() {
                for (auto* p : alloc_privs) delete p;
                for (auto* s : alloc_child_schemas) delete s;
                for (auto* a : alloc_child_arrays) delete a;
                delete[] schema.children;
                schema.children = nullptr;
                delete[] array.children;
                array.children = nullptr;
            };

            try {

            // Build parent schema (struct format "+s")
            auto* schema_priv = new ArrowColumnData{};
            alloc_privs.push_back(schema_priv);
            schema.format = "+s";
            schema.name = nullptr;
            schema.metadata = nullptr;
            schema.flags = 0;
            schema.n_children = static_cast<int64_t>(out_cols);
            schema.children = new ArrowSchema*[out_cols];
            schema.dictionary = nullptr;
            schema.release = release_arrow_schema;
            schema.private_data = schema_priv;

            // Build parent array
            auto* array_priv = new ArrowColumnData{};
            alloc_privs.push_back(array_priv);
            array.length = static_cast<int64_t>(batch_rows);
            array.null_count = 0;
            array.offset = 0;
            array.n_buffers = 1;
            array_priv->buffer_ptrs = {nullptr}; // no validity bitmap
            array.buffers = array_priv->buffer_ptrs.data();
            array.n_children = static_cast<int64_t>(out_cols);
            array.children = new ArrowArray*[out_cols];
            array.dictionary = nullptr;
            array.release = release_arrow_array;
            array.private_data = array_priv;

            for (size_t c = 0; c < out_cols; ++c) {
                bool is_str = (col_types[c] == bcsv::ColumnType::STRING);
                bool is_bool = (col_types[c] == bcsv::ColumnType::BOOL);

                // Child schema
                auto* child_schema = new ArrowSchema{};
                alloc_child_schemas.push_back(child_schema);
                auto* cs_priv = new ArrowColumnData{};
                alloc_privs.push_back(cs_priv);
                cs_priv->name_copy = new char[col_names[c].size() + 1];
                std::memcpy(cs_priv->name_copy, col_names[c].c_str(), col_names[c].size() + 1);
                child_schema->format = bcsv_type_to_arrow_format(col_types[c]);
                child_schema->name = cs_priv->name_copy;
                child_schema->metadata = nullptr;
                child_schema->flags = 0;
                child_schema->n_children = 0;
                child_schema->children = nullptr;
                child_schema->dictionary = nullptr;
                child_schema->release = release_arrow_child_schema;
                child_schema->private_data = cs_priv;
                schema.children[c] = child_schema;

                // Child array
                auto* child_array = new ArrowArray{};
                alloc_child_arrays.push_back(child_array);
                auto* ca_priv = new ArrowColumnData{};
                alloc_privs.push_back(ca_priv);
                child_array->length = static_cast<int64_t>(batch_rows);
                child_array->null_count = 0;
                child_array->offset = 0;
                child_array->dictionary = nullptr;
                child_array->n_children = 0;
                child_array->children = nullptr;
                child_array->release = release_arrow_child_array;
                child_array->private_data = ca_priv;

                if (is_str) {
                    // String: offsets buffer + data buffer
                    // Build offsets and concatenated data
                    const auto& strings = string_bufs[c];
                    ca_priv->offsets_buf.resize(batch_rows + 1);
                    ca_priv->offsets_buf[0] = 0;
                    size_t total_bytes = 0;
                    for (size_t i = 0; i < batch_rows; ++i)
                        total_bytes += strings[i].size();
                    // Arrow utf8 ("u") uses int32 offsets — guard against >2GB
                    if (total_bytes > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
                        throw std::overflow_error(
                            "String column '" + col_names[c] + "' exceeds 2 GB "
                            "Arrow utf8 offset limit (" + std::to_string(total_bytes) + " bytes)");
                    ca_priv->string_data_buf.resize(total_bytes);
                    char* dest = ca_priv->string_data_buf.data();
                    int32_t offset = 0;
                    for (size_t i = 0; i < batch_rows; ++i) {
                        std::memcpy(dest + offset, strings[i].data(), strings[i].size());
                        offset += static_cast<int32_t>(strings[i].size());
                        ca_priv->offsets_buf[i + 1] = offset;
                    }
                    child_array->n_buffers = 3;
                    ca_priv->buffer_ptrs = {nullptr, ca_priv->offsets_buf.data(),
                                            ca_priv->string_data_buf.data()};
                } else if (is_bool) {
                    // Bool: bit-packed validity buffer
                    const size_t byte_count = (batch_rows + 7) / 8;
                    ca_priv->data_buf.resize(byte_count, 0);
                    const auto& src = numeric_bufs[c];
                    for (size_t i = 0; i < batch_rows; ++i) {
                        if (src[i])
                            ca_priv->data_buf[i / 8] |= static_cast<uint8_t>(1 << (i % 8));
                    }
                    child_array->n_buffers = 2;
                    ca_priv->buffer_ptrs = {nullptr, ca_priv->data_buf.data()};
                } else {
                    // Numeric: move data buffer ownership (zero-copy)
                    ca_priv->data_buf = std::move(numeric_bufs[c]);
                    child_array->n_buffers = 2;
                    ca_priv->buffer_ptrs = {nullptr, ca_priv->data_buf.data()};
                }
                child_array->buffers = ca_priv->buffer_ptrs.data();
                array.children[c] = child_array;
            }

            // After successful _import_from_c, pyarrow owns the data via release
            // callbacks. Clear our tracking vectors so cleanup won't
            // double-free — ownership transferred.
            auto schema_ptr = reinterpret_cast<uintptr_t>(&schema);
            auto array_ptr = reinterpret_cast<uintptr_t>(&array);
            auto result = pa.attr("RecordBatch").attr("_import_from_c")(array_ptr, schema_ptr);
            // Ownership transferred — don't cleanup
            alloc_privs.clear();
            alloc_child_schemas.clear();
            alloc_child_arrays.clear();
            schema.children = nullptr;  // already freed by release callback
            array.children = nullptr;
            return result;

            } catch (...) {
                cleanup();
                throw;
            }
        };

        // Read data — either chunked or full
        if (chunk_size > 0) {
            // Chunked reading: return a list of RecordBatches
            nb::list batches;
            size_t rows_read = 0;

            while (rows_read < total_rows) {
                size_t batch_rows = std::min(static_cast<size_t>(chunk_size),
                                            total_rows - rows_read);
                // Allocate per-chunk buffers
                std::vector<std::vector<uint8_t>> numeric_bufs(out_cols);
                std::vector<std::vector<std::string>> string_bufs(out_cols);
                for (size_t c = 0; c < out_cols; ++c) {
                    if (col_types[c] == bcsv::ColumnType::STRING)
                        string_bufs[c].reserve(batch_rows);
                    else
                        numeric_bufs[c].resize(batch_rows * bcsv_type_byte_width(col_types[c]));
                }

                // Read chunk under GIL release
                size_t actual_rows = 0;
                {
                    nb::gil_scoped_release release;
                    for (size_t r = 0; r < batch_rows && reader.readNext(); ++r) {
                        const auto& row = reader.row();
                        for (size_t c = 0; c < out_cols; ++c) {
                            size_t src_col = col_indices[c];
                            if (col_types[c] == bcsv::ColumnType::STRING) {
                                string_bufs[c].emplace_back(row.template get<std::string>(src_col));
                            } else {
                                fill_numpy_cell(row, src_col, col_types[c],
                                                numeric_bufs[c].data(), r);
                            }
                        }
                        ++actual_rows;
                    }
                }
                if (actual_rows == 0) break;

                // Trim buffers to actual rows if needed
                if (actual_rows < batch_rows) {
                    for (size_t c = 0; c < out_cols; ++c) {
                        if (col_types[c] != bcsv::ColumnType::STRING)
                            numeric_bufs[c].resize(actual_rows * bcsv_type_byte_width(col_types[c]));
                    }
                }

                batches.append(build_batch(actual_rows, numeric_bufs, string_bufs));
                rows_read += actual_rows;
            }
            reader.close();

            // Return a pyarrow.Table from chunks
            return pa.attr("Table").attr("from_batches")(batches);
        } else {
            // Full read: single RecordBatch
            std::vector<std::vector<uint8_t>> numeric_bufs(out_cols);
            std::vector<std::vector<std::string>> string_bufs(out_cols);
            for (size_t c = 0; c < out_cols; ++c) {
                if (col_types[c] == bcsv::ColumnType::STRING)
                    string_bufs[c].reserve(total_rows);
                else
                    numeric_bufs[c].resize(total_rows * bcsv_type_byte_width(col_types[c]));
            }

            size_t row_idx = 0;
            {
                nb::gil_scoped_release release;
                while (reader.readNext()) {
                    const auto& row = reader.row();
                    for (size_t c = 0; c < out_cols; ++c) {
                        size_t src_col = col_indices[c];
                        if (col_types[c] == bcsv::ColumnType::STRING) {
                            string_bufs[c].emplace_back(row.template get<std::string>(src_col));
                        } else {
                            fill_numpy_cell(row, src_col, col_types[c],
                                            numeric_bufs[c].data(), row_idx);
                        }
                    }
                    ++row_idx;
                }
                reader.close();
            }

            // Use actual rows read, not total_rows (handles truncated/stream files)
            if (row_idx < total_rows) {
                for (size_t c = 0; c < out_cols; ++c) {
                    if (col_types[c] != bcsv::ColumnType::STRING)
                        numeric_bufs[c].resize(row_idx * bcsv_type_byte_width(col_types[c]));
                }
            }

            nb::object batch = build_batch(row_idx, numeric_bufs, string_bufs);
            return pa.attr("Table").attr("from_batches")(nb::make_tuple(batch));
        }
    }, nb::arg("filename"), nb::arg("columns") = nb::none(),
       nb::arg("chunk_size") = 0,
       "Read a BCSV file into a pyarrow.Table via Arrow C Data Interface (zero-copy).\n"
       "Set chunk_size > 0 for chunked reading (returns Table from multiple batches).");

    // ── Arrow C Data Interface: write_from_arrow ───────────────────────

    m.def("write_from_arrow", [](const std::string& filename,
                                  nb::object table,
                                  const std::string& row_codec,
                                  size_t compression_level,
                                  bcsv::FileFlags flags) {
        // Import pyarrow
        nb::object pa;
        try {
            pa = nb::module_::import_("pyarrow");
        } catch (...) {
            throw std::runtime_error(
                "pyarrow is required for Arrow interop. Install it with: pip install pyarrow");
        }

        // Get schema from the table
        nb::object schema = table.attr("schema");
        int64_t num_cols = nb::cast<int64_t>(table.attr("num_columns"));
        int64_t num_rows = nb::cast<int64_t>(table.attr("num_rows"));

        if (num_cols == 0)
            throw std::runtime_error("Cannot write file with empty schema");

        // Build BCSV layout from Arrow schema
        bcsv::Layout layout;
        std::vector<bcsv::ColumnType> col_types(num_cols);
        std::vector<std::string> col_names(num_cols);

        for (int64_t i = 0; i < num_cols; ++i) {
            nb::object field = schema.attr("field")(i);
            col_names[i] = nb::cast<std::string>(field.attr("name"));
            nb::object arrow_type = field.attr("type");
            std::string type_str = nb::cast<std::string>(nb::str(arrow_type));

            // Map Arrow types to BCSV types
            if (type_str == "bool") col_types[i] = bcsv::ColumnType::BOOL;
            else if (type_str == "int8") col_types[i] = bcsv::ColumnType::INT8;
            else if (type_str == "int16") col_types[i] = bcsv::ColumnType::INT16;
            else if (type_str == "int32") col_types[i] = bcsv::ColumnType::INT32;
            else if (type_str == "int64") col_types[i] = bcsv::ColumnType::INT64;
            else if (type_str == "uint8") col_types[i] = bcsv::ColumnType::UINT8;
            else if (type_str == "uint16") col_types[i] = bcsv::ColumnType::UINT16;
            else if (type_str == "uint32") col_types[i] = bcsv::ColumnType::UINT32;
            else if (type_str == "uint64") col_types[i] = bcsv::ColumnType::UINT64;
            else if (type_str == "float") col_types[i] = bcsv::ColumnType::FLOAT;
            else if (type_str == "double") col_types[i] = bcsv::ColumnType::DOUBLE;
            else if (type_str == "string" || type_str == "utf8" ||
                     type_str == "large_string" || type_str == "large_utf8")
                col_types[i] = bcsv::ColumnType::STRING;
            else
                throw std::runtime_error("Unsupported Arrow type '" + type_str +
                                         "' for column '" + col_names[i] + "'");

            layout.addColumn({col_names[i], col_types[i]});
        }

        // Convert Arrow columns to numpy/Python data via pyarrow
        // For numeric columns, use .to_numpy(zero_copy_only=False)
        // For string columns, use .to_pylist()
        std::vector<const void*> bufs(num_cols, nullptr);
        std::vector<nb::object> owned_arrays(num_cols);
        std::vector<std::vector<std::string>> string_cols(num_cols);
        std::vector<bool> is_string(num_cols, false);

        auto np = nb::module_::import_("numpy");

        for (int64_t c = 0; c < num_cols; ++c) {
            nb::object col = table.attr("column")(c);
            if (col_types[c] == bcsv::ColumnType::STRING) {
                is_string[c] = true;
                nb::list pylist = nb::cast<nb::list>(col.attr("to_pylist")());
                string_cols[c].reserve(num_rows);
                for (size_t i = 0; i < static_cast<size_t>(num_rows); ++i) {
                    nb::handle item = pylist[i];
                    if (item.is_none())
                        string_cols[c].emplace_back("");
                    else
                        string_cols[c].emplace_back(nb::cast<std::string>(item));
                }
            } else {
                nb::object arr = col.attr("to_numpy")(
                    "zero_copy_only"_a = false);
                owned_arrays[c] = np.attr("ascontiguousarray")(arr,
                    "dtype"_a = bcsv_type_to_numpy_dtype(col_types[c]));
                bufs[c] = reinterpret_cast<const void*>(
                    nb::cast<intptr_t>(owned_arrays[c].attr("ctypes").attr("data")));
            }
        }

        // Write via shared helper
        write_columnar_core(layout, row_codec, filename,
                            static_cast<size_t>(num_rows),
                            static_cast<size_t>(num_cols),
                            bufs, string_cols, is_string, col_types,
                            compression_level, flags);
    }, nb::arg("filename"), nb::arg("table"),
       nb::arg("row_codec") = "delta",
       nb::arg("compression_level") = 1,
       nb::arg("flags") = bcsv::FileFlags::BATCH_COMPRESS,
       "Write a pyarrow Table/RecordBatch to a BCSV file");
}