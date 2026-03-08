/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/iostream.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <variant>
#include <vector>
#include <string>
#include <bcsv/bcsv.h>
#include <bcsv/sampler/sampler.h>
#include <bcsv/sampler/sampler.hpp>

namespace py = pybind11;
using namespace pybind11::literals;

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
    [[nodiscard]] inline py::object extract_column_value(
            const RowType& row, size_t col, bcsv::ColumnType ct) {
        switch (ct) {
#define X(T, E) case E: return py::cast(row.template get<T>(col));
            BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
            case bcsv::ColumnType::STRING:
                return py::cast(row.template get<std::string>(col));
            default:
                throw std::runtime_error("Unsupported column type");
        }
    }

    template<typename RowType>
    [[nodiscard]] py::list row_to_list(const RowType& row, const bcsv::Layout& layout) {
        const size_t n = layout.columnCount();
        py::list result;
        for (size_t i = 0; i < n; ++i)
            result.append(extract_column_value(row, i, layout.columnType(i)));
        return result;
    }

    // ── Python → Row helpers ───────────────────────────────────────────

    template<typename T>
    T convert_numeric(const py::object& value, const std::string& target_type) {
        try {
            return value.cast<T>();
        } catch (const py::cast_error&) {
            if (py::isinstance<py::int_>(value))
                return static_cast<T>(value.cast<int64_t>());
            if (py::isinstance<py::float_>(value))
                return static_cast<T>(value.cast<double>());
            if (py::isinstance<py::str>(value)) {
                std::string s = value.cast<std::string>();
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
                                 bcsv::ColumnType ct, const py::object& value) {
        try {
            switch (ct) {
#define X(T, E) case E: row.set(col, convert_numeric<T>(value, #T)); break;
                BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
                case bcsv::ColumnType::STRING: {
                    std::string s = py::isinstance<py::str>(value)
                        ? py::cast<std::string>(value)
                        : py::str(value).cast<std::string>();
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
        } catch (const py::cast_error& e) {
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
        void write_row_fast(RowType& row, const py::list& values) const {
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

    inline py::dtype bcsv_type_to_numpy_dtype(bcsv::ColumnType ct) {
        switch (ct) {
#define X(T, E) case E: return py::dtype::of<T>();
            BCSV_FOR_EACH_NUMERIC_TYPE(X)
#undef X
            default: return py::dtype("O");
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

    // ── Shared binding helpers for reader-like / writer-like classes ───
    // Attaches read_row, read_all, __enter__, __exit__, __iter__, __next__
    // to any class that has .readNext(), .row(), .layout(), .close().

    template<typename ReaderT, typename PyClassT>
    void bind_reader_iteration(PyClassT& cls) {
        cls
        .def("read_row", [](ReaderT& r) -> py::object {
            if (!r.readNext()) return py::none();
            return row_to_list(r.row(), r.layout());
        })
        .def("read_all", [](ReaderT& r) -> py::list {
            py::list result;
            const auto& layout = r.layout();
            while (r.readNext())
                result.append(row_to_list(r.row(), layout));
            return result;
        })
        .def("__enter__", [](ReaderT& r) -> ReaderT& { return r; })
        .def("__exit__", [](ReaderT& r, py::object, py::object, py::object) { r.close(); })
        .def("__iter__", [](ReaderT& r) -> ReaderT& { return r; })
        .def("__next__", [](ReaderT& r) -> py::object {
            if (!r.readNext()) throw py::stop_iteration();
            return row_to_list(r.row(), r.layout());
        });
    }

    // Attaches write_row, write_rows to any class that has .row(), .layout(), .writeRow().
    template<typename WriterT, typename PyClassT>
    void bind_writer_rows(PyClassT& cls) {
        cls
        .def("write_row", [](WriterT& w, const py::list& values) {
            auto& row = w.row();
            const auto& layout = row.layout();
            if (values.size() != layout.columnCount())
                throw std::runtime_error("Row length mismatch: expected " +
                    std::to_string(layout.columnCount()) + ", got " + std::to_string(values.size()));
            for (size_t i = 0; i < values.size(); ++i)
                set_column_value(row, i, layout.columnType(i), values[i]);
            return w.writeRow();
        })
        .def("write_rows", [](WriterT& w, const py::list& rows) {
            const auto& layout = w.layout();
            const size_t expected = layout.columnCount();
            for (size_t i = 0; i < rows.size(); ++i) {
                py::list vals = rows[i].cast<py::list>();
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
}

PYBIND11_MODULE(_bcsv, m) {
    m.doc() = "Python bindings for the BCSV (Binary CSV) library";

    // ColumnType enum
    py::enum_<bcsv::ColumnType>(m, "ColumnType")
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
    py::class_<bcsv::ColumnDefinition>(m, "ColumnDefinition")
        .def(py::init<const std::string&, bcsv::ColumnType>(),
             py::arg("name"), py::arg("type"))
        .def_readwrite("name", &bcsv::ColumnDefinition::name)
        .def_readwrite("type", &bcsv::ColumnDefinition::type)
        .def("__repr__", [](const bcsv::ColumnDefinition& col) {
            return "<ColumnDefinition name='" + col.name + "' type=" +
                   std::string(bcsv::toString(col.type)) + ">";
        });

    // Layout class
    py::class_<bcsv::Layout>(m, "Layout")
        .def(py::init<>())
        .def(py::init<const std::vector<bcsv::ColumnDefinition>&>())
        .def("add_column", [](bcsv::Layout& layout, const bcsv::ColumnDefinition& col) {
            return layout.addColumn(col);
        }, py::arg("column"))
        .def("add_column", [](bcsv::Layout& layout, const std::string& name, bcsv::ColumnType type) {
            return layout.addColumn({name, type});
        }, py::arg("name"), py::arg("type"))
        .def("column_count", [](const bcsv::Layout& l) { return l.columnCount(); })
        .def("column_name", &bcsv::Layout::columnName, py::arg("index"))
        .def("column_type", &bcsv::Layout::columnType, py::arg("index"))
        .def("has_column", &bcsv::Layout::hasColumn, py::arg("name"))
        .def("column_index", &bcsv::Layout::columnIndex, py::arg("name"))
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
        }, py::arg("index"))
        .def("__len__", [](const bcsv::Layout& l) { return l.columnCount(); })
        .def("__getitem__", [](const bcsv::Layout& layout, size_t index) {
            if (index >= layout.columnCount()) {
                throw py::index_error("Column index out of range");
            }
            return bcsv::ColumnDefinition{layout.columnName(index), layout.columnType(index)};
        })
        .def("__repr__", [](const bcsv::Layout& layout) {
            std::stringstream ss;
            ss << layout;
            return ss.str();
        });

    // FileFlags enum — controls codec selection and file structure
    py::enum_<bcsv::FileFlags>(m, "FileFlags", py::arithmetic())
        .value("NONE", bcsv::FileFlags::NONE)
        .value("ZERO_ORDER_HOLD", bcsv::FileFlags::ZERO_ORDER_HOLD)
        .value("NO_FILE_INDEX", bcsv::FileFlags::NO_FILE_INDEX)
        .value("STREAM_MODE", bcsv::FileFlags::STREAM_MODE)
        .value("BATCH_COMPRESS", bcsv::FileFlags::BATCH_COMPRESS)
        .value("DELTA_ENCODING", bcsv::FileFlags::DELTA_ENCODING)
        .export_values()
        .def("__or__", [](bcsv::FileFlags a, bcsv::FileFlags b) { return a | b; })
        .def("__and__", [](bcsv::FileFlags a, bcsv::FileFlags b) { return a & b; })
        .def("__invert__", [](bcsv::FileFlags a) { return ~a; });

    // Writer class - variant-based, supports runtime codec selection
    py::class_<PyWriter>(m, "Writer")
        .def(py::init<const bcsv::Layout&, const std::string&>(),
             py::arg("layout"), py::arg("row_codec") = "delta")
        .def("open", [](PyWriter& pw, const std::string& filename,
                       bool overwrite, size_t compression_level, size_t block_size_kb, bcsv::FileFlags flags) {
            pw.invalidateCache();
            bool success = pw.visit([&](auto& w) {
                return w.open(filename, overwrite, compression_level, block_size_kb, flags);
            });
            if (!success)
                throw std::runtime_error("Failed to open file for writing: " + filename);
            return success;
        }, py::arg("filename"), py::arg("overwrite") = true, py::arg("compression_level") = 1,
           py::arg("block_size_kb") = 64, py::arg("flags") = bcsv::FileFlags::BATCH_COMPRESS)
        .def("write_row", [](PyWriter& pw, const py::list& values) {
            const auto& layout = pw.visit([](auto& w) -> const bcsv::Layout& { return w.layout(); });
            if (values.size() != layout.columnCount())
                throw std::runtime_error("Row length mismatch: expected " +
                    std::to_string(layout.columnCount()) + ", got " + std::to_string(values.size()));
            auto& cached_writer = pw.ensureCachedWriter();
            pw.visit([&](auto& w) {
                auto& row = w.row();
                cached_writer.write_row_fast(row, values);
                { py::gil_scoped_release release; w.writeRow(); }
            });
        })
        .def("write_rows", [](PyWriter& pw, const py::list& rows) {
            if (rows.size() == 0) return;
            const auto& layout = pw.visit([](auto& w) -> const bcsv::Layout& { return w.layout(); });
            const size_t expected = layout.columnCount();
            auto& cached_writer = pw.ensureCachedWriter();
            pw.visit([&](auto& w) {
                for (size_t i = 0; i < rows.size(); ++i) {
                    py::list vals = rows[i].cast<py::list>();
                    if (vals.size() != expected)
                        throw std::runtime_error("Row " + std::to_string(i) + " length mismatch: expected " +
                            std::to_string(expected) + ", got " + std::to_string(vals.size()));
                    auto& row = w.row();
                    cached_writer.write_row_fast(row, vals);
                    { py::gil_scoped_release release; w.writeRow(); }
                }
            });
        }, "Write multiple rows efficiently with batching")
        .def("close", [](PyWriter& pw) {
            py::gil_scoped_release release;
            pw.visit([](auto& w) { w.close(); });
        })
        .def("flush", [](PyWriter& pw) {
            py::gil_scoped_release release;
            pw.visit([](auto& w) { w.flush(); });
        })
        .def("is_open", [](PyWriter& pw) { return pw.visit([](auto& w) { return w.isOpen(); }); })
        .def("row_count", [](PyWriter& pw) { return pw.visit([](auto& w) { return w.rowCount(); }); })
        .def("row_codec", [](PyWriter& pw) -> const std::string& { return pw.codec_name_; })
        .def("compression_level", [](PyWriter& pw) { return pw.visit([](auto& w) { return w.compressionLevel(); }); })
        .def("layout", [](PyWriter& pw) -> const bcsv::Layout& {
            return pw.visit([](auto& w) -> const bcsv::Layout& { return w.layout(); });
        }, py::return_value_policy::reference_internal)
        .def("__enter__", [](PyWriter& pw) -> PyWriter& { return pw; })
        .def("__exit__", [](PyWriter& pw, py::object, py::object, py::object) {
            pw.visit([](auto& w) { w.close(); });
        });

    // ── Reader binding ─────────────────────────────────────────────────

    using ReaderT = bcsv::Reader<bcsv::Layout>;
    auto reader_cls = py::class_<ReaderT>(m, "Reader")
        .def(py::init<>())
        .def("open", [](ReaderT& reader, const std::string& filename) {
            bool success;
            { py::gil_scoped_release release; success = reader.open(filename); }
            if (!success) {
                std::string error = reader.getErrorMsg();
                if (error.empty()) error = "Failed to open file for reading: " + filename;
                throw std::runtime_error(error);
            }
            return success;
        }, py::arg("filename"))
        .def("layout", &ReaderT::layout, py::return_value_policy::reference_internal)
        .def("read_next", &ReaderT::readNext)
        .def("close", [](ReaderT& reader) { py::gil_scoped_release release; reader.close(); })
        .def("is_open", &ReaderT::isOpen)
        .def("file_flags", &ReaderT::fileFlags)
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
                 py::gil_scoped_release release;
                 if (!da.open(reader.filePath(), true))
                     throw std::runtime_error("Failed to open file for row counting: " + reader.filePath().string());
                 count = da.rowCount();
                 da.close();
             }
             return count;
        }, "Count the total number of rows in the file")
        .def("row_value", [](ReaderT& reader, size_t col) -> py::object {
            const auto& layout = reader.layout();
            if (col >= layout.columnCount())
                throw py::index_error("Column index " + std::to_string(col) + " out of range");
            return extract_column_value(reader.row(), col, layout.columnType(col));
        }, py::arg("column"), "Get a typed value from the current row by column index")
        .def("row_dict", [](ReaderT& reader) -> py::dict {
            const auto& layout = reader.layout();
            py::dict result;
            const auto& row = reader.row();
            for (size_t i = 0; i < layout.columnCount(); ++i)
                result[py::cast(layout.columnName(i))] =
                    extract_column_value(row, i, layout.columnType(i));
            return result;
        }, "Get the current row as a dict {column_name: value}");
    bind_reader_iteration<ReaderT>(reader_cls);

    // Utility functions
    m.def("type_to_string",
         [](bcsv::ColumnType t) { return std::string(bcsv::toString(t)); },
         "Convert ColumnType to string");

    // ── CsvWriter binding ──────────────────────────────────────────────

    using CsvWriterT = bcsv::CsvWriter<bcsv::Layout>;
    auto csv_writer_cls = py::class_<CsvWriterT>(m, "CsvWriter")
        .def(py::init<const bcsv::Layout&, char, char>(),
             py::arg("layout"), py::arg("delimiter") = ',', py::arg("decimal_sep") = '.')
        .def("open", [](CsvWriterT& w, const std::string& filename,
                        bool overwrite, bool include_header) {
            bool success = w.open(filename, overwrite, include_header);
            if (!success) {
                std::string err = w.getErrorMsg();
                if (err.empty()) err = "Failed to open CSV file for writing: " + filename;
                throw std::runtime_error(err);
            }
            return success;
        }, py::arg("filename"), py::arg("overwrite") = true, py::arg("include_header") = true)
        .def("close", [](CsvWriterT& w) { w.close(); })
        .def("is_open", &CsvWriterT::isOpen)
        .def("row_count", &CsvWriterT::rowCount)
        .def("layout", &CsvWriterT::layout, py::return_value_policy::reference_internal)
        .def("delimiter", &CsvWriterT::delimiter)
        .def("decimal_separator", &CsvWriterT::decimalSeparator)
        .def("__enter__", [](CsvWriterT& w) -> CsvWriterT& { return w; })
        .def("__exit__", [](CsvWriterT& w, py::object, py::object, py::object) { w.close(); });
    bind_writer_rows<CsvWriterT>(csv_writer_cls);

    // ── CsvReader binding ──────────────────────────────────────────────

    using CsvReaderT = bcsv::CsvReader<bcsv::Layout>;
    auto csv_reader_cls = py::class_<CsvReaderT>(m, "CsvReader")
        .def(py::init<const bcsv::Layout&, char, char>(),
             py::arg("layout"), py::arg("delimiter") = ',', py::arg("decimal_sep") = '.')
        .def("open", [](CsvReaderT& r, const std::string& filename, bool has_header) {
            bool success = r.open(filename, has_header);
            if (!success) {
                std::string err = r.getErrorMsg();
                if (err.empty()) err = "Failed to open CSV file for reading: " + filename;
                throw std::runtime_error(err);
            }
            return success;
        }, py::arg("filename"), py::arg("has_header") = true)
        .def("read_next", &CsvReaderT::readNext)
        .def("close", [](CsvReaderT& r) { r.close(); })
        .def("is_open", &CsvReaderT::isOpen)
        .def("row_pos", &CsvReaderT::rowPos)
        .def("file_line", &CsvReaderT::fileLine)
        .def("layout", &CsvReaderT::layout, py::return_value_policy::reference_internal)
        .def("delimiter", &CsvReaderT::delimiter)
        .def("decimal_separator", &CsvReaderT::decimalSeparator)
        .def("error_msg", &CsvReaderT::getErrorMsg);
    bind_reader_iteration<CsvReaderT>(csv_reader_cls);

    // ── ReaderDirectAccess binding ─────────────────────────────────────

    using DaReaderT = bcsv::ReaderDirectAccess<bcsv::Layout>;
    py::class_<DaReaderT>(m, "ReaderDirectAccess")
        .def(py::init<>())
        .def("open", [](DaReaderT& r, const std::string& filename, bool rebuild_footer) {
            bool success;
            { py::gil_scoped_release release; success = r.open(filename, rebuild_footer); }
            if (!success) {
                std::string err = r.getErrorMsg();
                if (err.empty()) err = "Failed to open file: " + filename;
                throw std::runtime_error(err);
            }
            return success;
        }, py::arg("filename"), py::arg("rebuild_footer") = false)
        .def("read", [](DaReaderT& r, size_t index) {
            bool success;
            { py::gil_scoped_release release; success = r.read(index); }
            if (!success)
                throw py::index_error("Row index " + std::to_string(index) + " out of range");
            return row_to_list(r.row(), r.layout());
        }, py::arg("index"))
        .def("row_count", &DaReaderT::rowCount)
        .def("layout", &DaReaderT::layout, py::return_value_policy::reference_internal)
        .def("close", [](DaReaderT& r) { py::gil_scoped_release release; r.close(); })
        .def("is_open", &DaReaderT::isOpen)
        .def("file_flags", &DaReaderT::fileFlags)
        .def("compression_level", [](DaReaderT& r) { return r.compressionLevel(); })
        .def("version_string", [](DaReaderT& r) { return r.fileHeader().versionString(); })
        .def("creation_time", [](DaReaderT& r) { return r.fileHeader().getCreationTime(); })
        .def("__enter__", [](DaReaderT& r) -> DaReaderT& { return r; })
        .def("__exit__", [](DaReaderT& r, py::object, py::object, py::object) { r.close(); })
        .def("__len__", &DaReaderT::rowCount)
        .def("__getitem__", [](DaReaderT& r, size_t index) {
            bool success;
            { py::gil_scoped_release release; success = r.read(index); }
            if (!success)
                throw py::index_error("Row index " + std::to_string(index) + " out of range");
            return row_to_list(r.row(), r.layout());
        });

    // ── Sampler enums ──────────────────────────────────────────────────

    py::enum_<bcsv::SamplerMode>(m, "SamplerMode")
        .value("TRUNCATE", bcsv::SamplerMode::TRUNCATE)
        .value("EXPAND", bcsv::SamplerMode::EXPAND)
        .export_values();

    py::enum_<bcsv::SamplerErrorPolicy>(m, "SamplerErrorPolicy")
        .value("THROW", bcsv::SamplerErrorPolicy::THROW)
        .value("SKIP_ROW", bcsv::SamplerErrorPolicy::SKIP_ROW)
        .value("SATURATE", bcsv::SamplerErrorPolicy::SATURATE)
        .export_values();

    py::class_<bcsv::SamplerCompileResult>(m, "SamplerCompileResult")
        .def_readonly("success", &bcsv::SamplerCompileResult::success)
        .def_readonly("error_msg", &bcsv::SamplerCompileResult::error_msg)
        .def_readonly("error_position", &bcsv::SamplerCompileResult::error_position)
        .def("__bool__", [](const bcsv::SamplerCompileResult& r) { return r.success; })
        .def("__repr__", [](const bcsv::SamplerCompileResult& r) {
            if (r.success) return std::string("<SamplerCompileResult: OK>");
            return std::string("<SamplerCompileResult: ERROR at pos ") +
                   std::to_string(r.error_position) + ": " + r.error_msg + ">";
        });

    // ── Sampler binding ────────────────────────────────────────────────

    py::class_<bcsv::Sampler<bcsv::Layout>>(m, "Sampler")
        .def(py::init<bcsv::Reader<bcsv::Layout>&>(),
             py::arg("reader"), py::keep_alive<1, 2>())
        .def("set_conditional", &bcsv::Sampler<bcsv::Layout>::setConditional,
             py::arg("expr"))
        .def("get_conditional", &bcsv::Sampler<bcsv::Layout>::getConditional)
        .def("set_selection", &bcsv::Sampler<bcsv::Layout>::setSelection,
             py::arg("expr"))
        .def("get_selection", &bcsv::Sampler<bcsv::Layout>::getSelection)
        .def("set_mode", &bcsv::Sampler<bcsv::Layout>::setMode, py::arg("mode"))
        .def("get_mode", &bcsv::Sampler<bcsv::Layout>::getMode)
        .def("set_error_policy", &bcsv::Sampler<bcsv::Layout>::setErrorPolicy,
             py::arg("policy"))
        .def("get_error_policy", &bcsv::Sampler<bcsv::Layout>::getErrorPolicy)
        .def("output_layout", &bcsv::Sampler<bcsv::Layout>::outputLayout,
             py::return_value_policy::reference_internal)
        .def("next", [](bcsv::Sampler<bcsv::Layout>& s) {
            bool has_next;
            {
                py::gil_scoped_release release;
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
            py::list result;
            while (true) {
                bool has_next;
                {
                    py::gil_scoped_release release;
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
        })
        .def("__next__", [](bcsv::Sampler<bcsv::Layout>& s) -> py::object {
            bool has_next;
            {
                py::gil_scoped_release release;
                has_next = s.next();
            }
            if (!has_next) throw py::stop_iteration();
            const auto& row = s.row();
            return row_to_list(row, row.layout());
        });

    // ── Columnar I/O: read_columns ─────────────────────────────────────

    m.def("read_columns", [](const std::string& filename) -> py::dict {
        // Phase 1: Single open via ReaderDirectAccess for count + sequential read
        bcsv::ReaderDirectAccess<bcsv::Layout> reader;
        {
            py::gil_scoped_release release;
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
        std::vector<py::array> arrays(num_cols);
        std::vector<void*> bufs(num_cols, nullptr);
        std::vector<std::vector<std::string>> string_cols(num_cols);

        for (size_t c = 0; c < num_cols; ++c) {
            if (is_string[c]) {
                string_cols[c].reserve(num_rows);
            } else {
                auto shape = std::vector<py::ssize_t>{static_cast<py::ssize_t>(num_rows)};
                arrays[c] = py::array(bcsv_type_to_numpy_dtype(col_types[c]), shape);
                bufs[c] = arrays[c].mutable_data();
            }
        }

        // Phase 3: Entire read loop under GIL release — strings stay in C++
        {
            py::gil_scoped_release release;
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
        py::dict result;
        for (size_t c = 0; c < num_cols; ++c) {
            if (is_string[c]) {
                py::list str_list(string_cols[c].size());
                for (size_t i = 0; i < string_cols[c].size(); ++i) {
                    str_list[i] = py::cast(std::move(string_cols[c][i]));
                }
                result[py::cast(col_names[c])] = std::move(str_list);
            } else {
                result[py::cast(col_names[c])] = std::move(arrays[c]);
            }
        }
        return result;
    }, py::arg("filename"),
       "Read a BCSV file into a dict of numpy arrays (numeric) and lists (strings)");

    // ── Columnar I/O: write_columns ────────────────────────────────────

    m.def("write_columns", [](const std::string& filename, const py::dict& columns,
                              const py::list& col_order, const py::list& col_types_py,
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
            col_names[i] = col_order[i].cast<std::string>();
            col_types[i] = col_types_py[i].cast<bcsv::ColumnType>();
            layout.addColumn({col_names[i], col_types[i]});
        }

        // Gather column data — numpy arrays and pre-extract strings into C++ vectors
        std::vector<const void*> bufs(num_cols, nullptr);
        std::vector<py::array> owned_arrays(num_cols);
        std::vector<std::vector<std::string>> string_cols(num_cols);
        std::vector<bool> is_string(num_cols, false);
        size_t num_rows = 0;
        auto np = py::module_::import("numpy");

        for (size_t c = 0; c < num_cols; ++c) {
            py::object col_data = columns[py::cast(col_names[c])];
            size_t col_len = 0;
            if (col_types[c] == bcsv::ColumnType::STRING) {
                is_string[c] = true;
                py::list str_list = col_data.cast<py::list>();
                col_len = str_list.size();
                string_cols[c].reserve(col_len);
                for (size_t i = 0; i < col_len; ++i) {
                    string_cols[c].emplace_back(str_list[i].cast<std::string>());
                }
            } else {
                owned_arrays[c] = np.attr("ascontiguousarray")(col_data,
                    "dtype"_a = bcsv_type_to_numpy_dtype(col_types[c]));
                bufs[c] = owned_arrays[c].data();
                col_len = static_cast<size_t>(owned_arrays[c].size());
            }
            if (c == 0) {
                num_rows = col_len;
            } else if (col_len != num_rows) {
                throw std::runtime_error("Column '" + col_names[c] +
                    "' has " + std::to_string(col_len) + " rows, expected " +
                    std::to_string(num_rows));
            }
        }

        // Create writer and run entire write loop under GIL release
        PyWriter pw(layout, row_codec);
        pw.visit([&](auto& w) {
            py::gil_scoped_release release;
            if (!w.open(filename, true, compression_level, 64, flags)) {
                throw std::runtime_error("Failed to open file for writing: " + filename);
            }
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
    }, py::arg("filename"), py::arg("columns"), py::arg("col_order"),
       py::arg("col_types"), py::arg("row_codec") = "delta",
       py::arg("compression_level") = 1,
       py::arg("flags") = bcsv::FileFlags::BATCH_COMPRESS,
       "Write a dict of numpy arrays/lists to a BCSV file");
}