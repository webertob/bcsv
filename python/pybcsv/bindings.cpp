#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/iostream.h>

#include "bcsv/bcsv.h"

namespace py = pybind11;

// Helper functions to eliminate code duplication and improve maintainability

namespace {
    // Efficiently convert a single column value from BCSV row to Python object
    // Template approach eliminates virtual function calls and allows inlining
    template<typename RowType>
    [[nodiscard]] inline py::object extract_column_value_unchecked(const RowType& row, size_t column_index, bcsv::ColumnType col_type) noexcept {
        // No bounds checking - caller guarantees valid index
        switch (col_type) {
            case bcsv::ColumnType::BOOL:
                return py::cast(row.template get<bool>(column_index));
            case bcsv::ColumnType::INT8:
                return py::cast(row.template get<int8_t>(column_index));
            case bcsv::ColumnType::INT16:
                return py::cast(row.template get<int16_t>(column_index));
            case bcsv::ColumnType::INT32:
                return py::cast(row.template get<int32_t>(column_index));
            case bcsv::ColumnType::INT64:
                return py::cast(row.template get<int64_t>(column_index));
            case bcsv::ColumnType::UINT8:
                return py::cast(row.template get<uint8_t>(column_index));
            case bcsv::ColumnType::UINT16:
                return py::cast(row.template get<uint16_t>(column_index));
            case bcsv::ColumnType::UINT32:
                return py::cast(row.template get<uint32_t>(column_index));
            case bcsv::ColumnType::UINT64:
                return py::cast(row.template get<uint64_t>(column_index));
            case bcsv::ColumnType::FLOAT:
                return py::cast(row.template get<float>(column_index));
            case bcsv::ColumnType::DOUBLE:
                return py::cast(row.template get<double>(column_index));
            case bcsv::ColumnType::STRING: {
                // Safe string handling with move semantics to avoid copies
                const std::string& str_ref = row.template get<std::string>(column_index);
                return py::cast(str_ref);
            }
            default:
                [[unlikely]]
                throw std::runtime_error("Unsupported column type");
        }
    }

    // Efficiently convert entire row from BCSV to Python list
    // Uses local allocations to avoid thread_local issues
    template<typename RowType>
    [[nodiscard]] py::list row_to_python_list_optimized(const RowType& row, const bcsv::Layout& layout) {
        const size_t column_count = layout.columnCount();
        
        // Use local vectors instead of thread_local to avoid cleanup issues
        std::vector<bcsv::ColumnType> column_types;
        column_types.reserve(column_count);
        for (size_t i = 0; i < column_count; ++i) {
            column_types.emplace_back(layout.columnType(i));
        }
        
        // Create Python list directly without intermediate storage
        py::list py_row;
        for (size_t i = 0; i < column_count; ++i) {
            py_row.append(extract_column_value_unchecked(row, i, column_types[i]));
        }
        
        return py_row;
    }

    // Efficiently set column value from Python object to BCSV row
    // Template approach allows compile-time optimization, unchecked access
    template<typename RowType>
    inline void set_column_value_unchecked(RowType& row, size_t column_index, 
                                          bcsv::ColumnType col_type, const py::object& value) noexcept {
        // No bounds checking - caller guarantees valid index
        switch (col_type) {
            case bcsv::ColumnType::BOOL:
                row.set(column_index, value.cast<bool>());
                break;
            case bcsv::ColumnType::INT8:
                row.set(column_index, value.cast<int8_t>());
                break;
            case bcsv::ColumnType::INT16:
                row.set(column_index, value.cast<int16_t>());
                break;
            case bcsv::ColumnType::INT32:
                row.set(column_index, value.cast<int32_t>());
                break;
            case bcsv::ColumnType::INT64:
                row.set(column_index, value.cast<int64_t>());
                break;
            case bcsv::ColumnType::UINT8:
                row.set(column_index, value.cast<uint8_t>());
                break;
            case bcsv::ColumnType::UINT16:
                row.set(column_index, value.cast<uint16_t>());
                break;
            case bcsv::ColumnType::UINT32:
                row.set(column_index, value.cast<uint32_t>());
                break;
            case bcsv::ColumnType::UINT64:
                row.set(column_index, value.cast<uint64_t>());
                break;
            case bcsv::ColumnType::FLOAT:
                row.set(column_index, value.cast<float>());
                break;
            case bcsv::ColumnType::DOUBLE:
                row.set(column_index, value.cast<double>());
                break;
            case bcsv::ColumnType::STRING: {
                // Safe string handling for large strings
                if (py::isinstance<py::str>(value)) {
                    std::string str_value = py::cast<std::string>(value);
                    // Ensure the string doesn't exceed BCSV format limits (already validated above)
                    if (str_value.size() > 65534) {
                        str_value.resize(65534); // Truncate to max allowed
                    }
                    row.set(column_index, std::move(str_value));
                } else {
                    throw std::runtime_error("String column requires string value");
                }
                break;
            }
            default:
                [[unlikely]]
                throw std::runtime_error("Unsupported column type");
        }
    }
    
    // Optimized batch write function that pre-caches layout information
    class OptimizedRowWriter {
    private:
        std::vector<bcsv::ColumnType> column_types_;
        size_t column_count_;
        
    public:
        explicit OptimizedRowWriter(const bcsv::Layout& layout) 
            : column_count_(layout.columnCount()) {
            column_types_.reserve(column_count_);
            for (size_t i = 0; i < column_count_; ++i) {
                column_types_.emplace_back(layout.columnType(i));
            }
        }
        
        template<typename RowType>
        inline void write_row_fast(RowType& row, const py::list& values) const noexcept {
            // Pre-validated: values.size() == column_count_
            for (size_t i = 0; i < column_count_; ++i) {
                set_column_value_unchecked(row, i, column_types_[i], values[i]);
            }
        }
    };
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
                   bcsv::toString(col.type) + ">";
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
        .def("column_count", &bcsv::Layout::columnCount)
        .def("get_column_count", &bcsv::Layout::columnCount)  // Alias for consistency
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
        .def("__len__", &bcsv::Layout::columnCount)
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

    // FileFlags enum
    py::enum_<bcsv::FileFlags>(m, "FileFlags")
        .value("NONE", bcsv::FileFlags::NONE)
        .value("COMPRESSED", bcsv::FileFlags::ZERO_ORDER_HOLD)  // Map to available flag
        .export_values();

    // Writer class - template specialization for Layout
    py::class_<bcsv::Writer<bcsv::Layout>>(m, "Writer")
        .def(py::init<const bcsv::Layout&>(), py::arg("layout"))
        .def("open", [](bcsv::Writer<bcsv::Layout>& writer, const std::string& filename, 
                       bool overwrite = true, size_t compression_level = 1, bcsv::FileFlags flags = bcsv::FileFlags::NONE) {
            return writer.open(filename, overwrite, compression_level, flags);
        }, py::arg("filename"), py::arg("overwrite") = true, py::arg("compression_level") = 1, py::arg("flags") = bcsv::FileFlags::NONE)
        .def("write_row", [](bcsv::Writer<bcsv::Layout>& writer, const py::list& values) {
            auto& row = writer.row();
            const auto& layout = row.layout();
            
            if (values.size() != layout.columnCount()) {
                throw std::runtime_error("Row length mismatch: expected " + 
                    std::to_string(layout.columnCount()) + ", got " + std::to_string(values.size()));
            }
            
            // Pre-validate string sizes to prevent memory issues and row size overflow
            for (size_t i = 0; i < values.size(); ++i) {
                if (layout.columnType(i) == bcsv::ColumnType::STRING && py::isinstance<py::str>(values[i])) {
                    py::str str_obj = values[i].cast<py::str>();
                    size_t str_size = PyUnicode_GET_LENGTH(str_obj.ptr()) * PyUnicode_KIND(str_obj.ptr());
                    
                    // Check against our Python-level limit (100MB)
                    if (str_size > 100 * 1024 * 1024) {
                        throw std::runtime_error("String in column " + std::to_string(i) + " too large: " + 
                                                std::to_string(str_size) + " bytes (max 100MB)");
                    }
                    
                    // Check against BCSV format limit (65534 bytes per string)
                    if (str_size > 65534) {
                        throw std::runtime_error("String in column " + std::to_string(i) + " exceeds BCSV format limit: " + 
                                                std::to_string(str_size) + " bytes (max 65534 bytes per string)");
                    }
                }
            }
            
            // Estimate total row size to prevent uint16_t overflow
            size_t estimated_row_size = 0;
            for (size_t i = 0; i < layout.columnCount(); ++i) {
                if (layout.columnType(i) == bcsv::ColumnType::STRING) {
                    estimated_row_size += 8; // StringAddress size
                    if (i < values.size() && py::isinstance<py::str>(values[i])) {
                        py::str str_obj = values[i].cast<py::str>();
                        size_t str_size = PyUnicode_GET_LENGTH(str_obj.ptr()) * PyUnicode_KIND(str_obj.ptr());
                        estimated_row_size += str_size;
                    }
                } else {
                    estimated_row_size += layout.columnLength(i);
                }
            }
            
            if (estimated_row_size > 65535) {
                throw std::runtime_error("Total row size too large: " + std::to_string(estimated_row_size) + 
                                        " bytes (BCSV format limit: 65535 bytes per row)");
            }
            
            // Create local optimized writer - no static/thread_local caching
            OptimizedRowWriter cached_writer(layout);
            cached_writer.write_row_fast(row, values);
            return writer.writeRow();
        })
        .def("write_rows", [](bcsv::Writer<bcsv::Layout>& writer, const py::list& rows) {
            if (rows.size() == 0) return;
            
            // Get layout from writer
            const auto& layout = writer.layout();
            const size_t expected_cols = layout.columnCount();
            
            // Create one optimized writer for this batch
            OptimizedRowWriter cached_writer(layout);
            
            // Batch process all rows
            for (size_t i = 0; i < rows.size(); ++i) {
                py::list row_values = rows[i].cast<py::list>();
                
                if (row_values.size() != expected_cols) {
                    throw std::runtime_error("Row " + std::to_string(i) + " length mismatch: expected " + 
                        std::to_string(expected_cols) + ", got " + std::to_string(row_values.size()));
                }
                
                auto& row = writer.row();
                cached_writer.write_row_fast(row, row_values);
                writer.writeRow();
            }
        }, "Write multiple rows efficiently with batching")
        .def("close", &bcsv::Writer<bcsv::Layout>::close)
        .def("flush", &bcsv::Writer<bcsv::Layout>::flush)
        .def("is_open", &bcsv::Writer<bcsv::Layout>::is_open)
        .def("layout", &bcsv::Writer<bcsv::Layout>::layout, py::return_value_policy::reference_internal)
        .def("__enter__", [](bcsv::Writer<bcsv::Layout>& writer) -> bcsv::Writer<bcsv::Layout>& {
            return writer;
        })
        .def("__exit__", [](bcsv::Writer<bcsv::Layout>& writer, py::object, py::object, py::object) {
            writer.close();
        });

    // Reader class - template specialization for Layout
    py::class_<bcsv::Reader<bcsv::Layout>>(m, "Reader")
        .def(py::init<>())
        .def("open", [](bcsv::Reader<bcsv::Layout>& reader, const std::string& filename) {
            return reader.open(filename);
        }, py::arg("filename"))
        .def("layout", &bcsv::Reader<bcsv::Layout>::layout, py::return_value_policy::reference_internal)
        .def("get_layout", &bcsv::Reader<bcsv::Layout>::layout, py::return_value_policy::reference_internal)  // Alias
        .def("read_next", &bcsv::Reader<bcsv::Layout>::readNext)
        .def("read_row", [](bcsv::Reader<bcsv::Layout>& reader) -> py::object {
            if (!reader.readNext()) {
                return py::none();
            }
            
            return row_to_python_list_optimized(reader.row(), reader.layout());
        })
        .def("read_all", [](bcsv::Reader<bcsv::Layout>& reader) -> py::list {
            py::list all_rows;
            const auto& layout = reader.layout();
            
            // Pre-allocate if we can estimate size (reader might have row count)
            // For now, we'll let Python list grow dynamically but avoid repeated layout lookups
            
            while (reader.readNext()) {
                all_rows.append(row_to_python_list_optimized(reader.row(), layout));
            }
            return all_rows;
        })
        .def("close", &bcsv::Reader<bcsv::Layout>::close)
        .def("is_open", &bcsv::Reader<bcsv::Layout>::isOpen)
        .def("__enter__", [](bcsv::Reader<bcsv::Layout>& reader) -> bcsv::Reader<bcsv::Layout>& {
            return reader;
        })
        .def("__exit__", [](bcsv::Reader<bcsv::Layout>& reader, py::object, py::object, py::object) {
            reader.close();
        })
        .def("__iter__", [](bcsv::Reader<bcsv::Layout>& reader) -> bcsv::Reader<bcsv::Layout>& {
            return reader;
        })
        .def("__next__", [](bcsv::Reader<bcsv::Layout>& reader) -> py::object {
            if (!reader.readNext()) {
                throw py::stop_iteration();
            }
            
            return row_to_python_list_optimized(reader.row(), reader.layout());
        });

    // Utility functions
    m.def("type_to_string", &bcsv::toString, "Convert ColumnType to string");
}