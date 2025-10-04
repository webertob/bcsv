/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsv2csv.cpp
 * @brief CLI tool to convert BCSV files to CSV format
 * 
 * This tool reads a BCSV file and converts it to CSV format.
 * It handles proper CSV escaping and allows customization of delimiters.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <climits>
#include <cstdint>
#include <vector>
#include <bcsv/bcsv.h>

struct Config {
    std::string input_file;
    std::string output_file;
    char delimiter = ',';
    char quote_char = '"';
    bool include_header = true;
    bool quote_all = false;
    bool verbose = false;
    bool help = false;
    int float_precision = -1;  // -1 means auto-detect optimal precision
    
    // Row range selection options
    int64_t first_row = -1;    // -1 = not specified (0-based indexing)
    int64_t last_row = -1;     // -1 = not specified (0-based indexing, inclusive)
    std::string slice;           // empty = not specified (Python-style slice notation)
    
    // Parsed slice components (internal use)
    int64_t slice_start = INT64_MIN;  // INT64_MIN = not specified
    int64_t slice_stop = INT64_MIN;   // INT64_MIN = not specified
    int64_t slice_step = 1;
    bool slice_parsed = false;
};

// Escape CSV field if necessary
std::string escapeCSVField(const std::string& field, char delimiter, char quote_char, bool quote_all) {
    bool needs_quoting = quote_all || 
                        field.find(delimiter) != std::string::npos ||
                        field.find(quote_char) != std::string::npos ||
                        field.find('\n') != std::string::npos ||
                        field.find('\r') != std::string::npos ||
                        (!field.empty() && (field.front() == ' ' || field.back() == ' '));
    
    if (!needs_quoting) {
        return field;
    }
    
    std::string result;
    result += quote_char;
    
    for (char c : field) {
        if (c == quote_char) {
            result += quote_char; // Escape quote by doubling it
        }
        result += c;
    }
    
    result += quote_char;
    return result;
}

// Parse Python-style slice notation: "start:stop:step"
void parseSlice(const std::string& slice_str, Config& config) {
    if (slice_str.empty()) return;
    
    config.slice = slice_str;
    config.slice_parsed = true;
    
    // Split by colons
    std::vector<std::string> parts;
    std::stringstream ss(slice_str);
    std::string part;
    
    while (std::getline(ss, part, ':')) {
        parts.push_back(part);
    }
    
    // Handle the case where the string ends with ':' - add an empty part
    if (!slice_str.empty() && slice_str.back() == ':') {
        parts.push_back("");
    }
    
    // Handle different slice formats
    if (parts.size() == 1) {
        // Single number - treat as "0:N"
        if (!parts[0].empty()) {
            config.slice_start = 0;
            config.slice_stop = static_cast<int64_t>(std::stoll(parts[0]));
        }
    } else if (parts.size() == 2) {
        // "start:stop"
        if (!parts[0].empty()) {
            config.slice_start = static_cast<int64_t>(std::stoll(parts[0]));
        }
        if (!parts[1].empty()) {
            config.slice_stop = static_cast<int64_t>(std::stoll(parts[1]));
        }
    } else if (parts.size() == 3) {
        // "start:stop:step"
        if (!parts[0].empty()) {
            config.slice_start = static_cast<int64_t>(std::stoll(parts[0]));
        }
        if (!parts[1].empty()) {
            config.slice_stop = static_cast<int64_t>(std::stoll(parts[1]));
        }
        if (!parts[2].empty()) {
            config.slice_step = static_cast<int64_t>(std::stoll(parts[2]));
            if (config.slice_step == 0) {
                throw std::runtime_error("Slice step cannot be zero");
            }
        }
    } else {
        throw std::runtime_error("Invalid slice format. Use 'start:stop:step' (e.g., '10:20:2', ':100', '50:').");
    }
    
    if (config.slice_step < 0) {
        throw std::runtime_error("Negative step sizes are not supported yet.");
    }
}

// Convert BCSV value to string with precision preservation
template<typename T>
std::string valueToString(const T& value, int precision = -1) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_floating_point_v<T>) {
        // Use default precision without fixed format to avoid representation artifacts
        std::ostringstream oss;
        
        int effective_precision;
        if (precision >= 0) {
            effective_precision = precision;
        } else if constexpr (std::is_same_v<T, float>) {
            // For float, use enough precision to represent the value accurately
            effective_precision = 7;
        } else {
            // For double, use enough precision to represent the value accurately  
            effective_precision = 15;
        }
        
        oss << std::setprecision(effective_precision) << value;
        
        std::string result = oss.str();
        
        // Handle scientific notation and very long decimal representations
        if (precision < 0 && result.find('e') == std::string::npos && result.find('.') != std::string::npos) {
            // Remove trailing zeros only if we have a decimal point and no scientific notation
            size_t last_non_zero = result.find_last_not_of('0');
            if (last_non_zero != std::string::npos && result[last_non_zero] != '.') {
                result.erase(last_non_zero + 1);
            } else if (last_non_zero != std::string::npos && result[last_non_zero] == '.') {
                result.erase(last_non_zero);
            }
        }
        
        return result;
    } else {
        return std::to_string(value);
    }
}

// Get string value from BCSV row based on column type
std::string getRowValueAsString(const bcsv::Reader<bcsv::Layout>& reader, size_t column_index, bcsv::ColumnType type, int float_precision = -1) {
    try {
        switch (type) {
            case bcsv::ColumnType::BOOL:
                return valueToString(reader.row().get<bool>(column_index));
            case bcsv::ColumnType::INT8:
                return valueToString(reader.row().get<int8_t>(column_index));
            case bcsv::ColumnType::INT16:
                return valueToString(reader.row().get<int16_t>(column_index));
            case bcsv::ColumnType::INT32:
                return valueToString(reader.row().get<int32_t>(column_index));
            case bcsv::ColumnType::INT64:
                return valueToString(reader.row().get<int64_t>(column_index));
            case bcsv::ColumnType::UINT8:
                return valueToString(reader.row().get<uint8_t>(column_index));
            case bcsv::ColumnType::UINT16:
                return valueToString(reader.row().get<uint16_t>(column_index));
            case bcsv::ColumnType::UINT32:
                return valueToString(reader.row().get<uint32_t>(column_index));
            case bcsv::ColumnType::UINT64:
                return valueToString(reader.row().get<uint64_t>(column_index));
#if BCSV_HAS_FLOAT16
            case bcsv::ColumnDataType::FLOAT16:
                return valueToString(reader.row().get<std::float16_t>(column_index), float_precision);
#endif
#if BCSV_HAS_BFLOAT16
            case bcsv::ColumnDataType::BFLOAT16:
                return valueToString(reader.row().get<std::bfloat16_t>(column_index), float_precision);
#endif
            case bcsv::ColumnType::FLOAT:
                return valueToString(reader.row().get<float>(column_index), float_precision);
            case bcsv::ColumnType::DOUBLE:
                return valueToString(reader.row().get<double>(column_index), float_precision);
#if BCSV_HAS_FLOAT128
            case bcsv::ColumnDataType::FLOAT128:
                return valueToString(reader.row().get<std::float128_t>(column_index), float_precision);
#endif
            case bcsv::ColumnType::STRING:
                return reader.row().get<std::string>(column_index);
            default:
                return "";
        }
    } catch (const std::exception&) {
        return ""; // Return empty string if value cannot be retrieved
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE [OUTPUT_FILE]\n\n";
    std::cout << "Convert BCSV file to CSV format.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input BCSV file path\n";
    std::cout << "  OUTPUT_FILE    Output CSV file path (default: INPUT_FILE.csv)\n\n";
    std::cout << "Options:\n";
    std::cout << "  -d, --delimiter CHAR    Field delimiter (default: ',')\n";
    std::cout << "  -q, --quote CHAR        Quote character (default: '\"')\n";
    std::cout << "  --no-header             Don't include header row in output\n";
    std::cout << "  --quote-all             Quote all fields (not just those that need it)\n";
    std::cout << "  -p, --precision N       Floating point precision (default: auto)\n";
    std::cout << "  --firstRow N            Start from row N (0-based, default: 0)\n";
    std::cout << "  --lastRow N             End at row N (0-based, inclusive, default: last)\n";
    std::cout << "  --slice SLICE           Python-style slice notation (overrides firstRow/lastRow)\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -h, --help              Show this help message\n\n";
    std::cout << "Row Selection Examples:\n";
    std::cout << "  --firstRow 100 --lastRow 200    # Rows 100-200 (inclusive)\n";
    std::cout << "  --slice 10:20                   # Rows 10-19 (Python-style)\n";
    std::cout << "  --slice :100                    # First 100 rows\n";
    std::cout << "  --slice 50:                     # From row 50 to end\n";
    std::cout << "  --slice ::2                     # Every 2nd row\n";
    std::cout << "  --slice -10:                    # Last 10 rows\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " data.bcsv\n";
    std::cout << "  " << program_name << " -d ';' data.bcsv output.csv\n";
    std::cout << "  " << program_name << " --no-header --quote-all data.bcsv\n";
}

Config parseArgs(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            config.help = true;
            return config;
        } else if (arg == "-d" || arg == "--delimiter") {
            if (i + 1 < argc) {
                config.delimiter = argv[++i][0];
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "-q" || arg == "--quote") {
            if (i + 1 < argc) {
                config.quote_char = argv[++i][0];
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "-p" || arg == "--precision") {
            if (i + 1 < argc) {
                config.float_precision = std::stoi(argv[++i]);
                if (config.float_precision < 0) {
                    throw std::runtime_error("Precision must be non-negative");
                }
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "--no-header") {
            config.include_header = false;
        } else if (arg == "--quote-all") {
            config.quote_all = true;
        } else if (arg == "--firstRow") {
            if (i + 1 < argc) {
                config.first_row = std::stoll(argv[++i]);
                if (config.first_row < 0) {
                    throw std::runtime_error("firstRow must be non-negative (0-based indexing)");
                }
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "--lastRow") {
            if (i + 1 < argc) {
                config.last_row = std::stoll(argv[++i]);
                if (config.last_row < 0) {
                    throw std::runtime_error("lastRow must be non-negative (0-based indexing)");
                }
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "--slice") {
            if (i + 1 < argc) {
                try {
                    parseSlice(argv[++i], config);
                } catch (const std::exception& e) {
                    throw std::runtime_error("Invalid slice argument: " + std::string(e.what()));
                }
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + arg);
        } else {
            // Positional arguments
            if (config.input_file.empty()) {
                config.input_file = arg;
            } else if (config.output_file.empty()) {
                config.output_file = arg;
            } else {
                throw std::runtime_error("Too many arguments");
            }
        }
    }
    
    if (config.input_file.empty() && !config.help) {
        throw std::runtime_error("Input file is required");
    }
    
    // Set default output file if not specified
    if (config.output_file.empty() && !config.input_file.empty()) {
        std::filesystem::path input_path(config.input_file);
        config.output_file = input_path.stem().string() + ".csv";
    }
    
    // Validate character conflicts
    if (!config.help) {
        if (config.delimiter == config.quote_char) {
            throw std::runtime_error("Delimiter and quote character cannot be the same ('" + 
                                   std::string(1, config.delimiter) + "')");
        }
        
        // Validate row range arguments
        if (config.slice_parsed && (config.first_row != -1 || config.last_row != -1)) {
            std::cerr << "Warning: --slice overrides --firstRow and --lastRow arguments" << std::endl;
        }
        
        if (!config.slice_parsed && config.first_row != -1 && config.last_row != -1) {
            if (config.first_row > config.last_row) {
                throw std::runtime_error("firstRow (" + std::to_string(config.first_row) + 
                                       ") cannot be greater than lastRow (" + std::to_string(config.last_row) + ")");
            }
        }
    }
    
    return config;
}

int main(int argc, char* argv[]) {
    try {
        Config config = parseArgs(argc, argv);
        
        if (config.help) {
            printUsage(argv[0]);
            return 0;
        }
        
        if (config.verbose) {
            std::cout << "Converting: " << config.input_file << " -> " << config.output_file << std::endl;
            std::cout << "Delimiter: '" << config.delimiter << "', Quote: '" << config.quote_char << "'" << std::endl;
            std::cout << "Header: " << (config.include_header ? "yes" : "no") << std::endl;
            std::cout << "Quote all: " << (config.quote_all ? "yes" : "no") << std::endl;
        }
        
        // Check if input file exists
        if (!std::filesystem::exists(config.input_file)) {
            throw std::runtime_error("Input file does not exist: " + config.input_file);
        }
        
        // Open BCSV file and get layout information
        bcsv::Reader<bcsv::Layout> reader;
        reader.open(config.input_file);
        
        const auto& layout = reader.layout();
        
        if (config.verbose) {
            std::cout << "Opened BCSV file successfully" << std::endl;
            std::cout << "Layout contains " << layout.columnCount() << " columns:" << std::endl;
            std::cout << layout << std::endl;
        }
        
        // Resolve row range parameters
        int64_t effective_start = 0;
        int64_t effective_stop = INT64_MAX;  // Will be limited by actual file size
        int64_t effective_step = 1;
        
        if (config.slice_parsed) {
            // Use slice parameters
            effective_start = (config.slice_start == INT64_MIN) ? 0 : config.slice_start;
            effective_stop = (config.slice_stop == INT64_MIN) ? INT64_MAX : config.slice_stop;
            effective_step = config.slice_step;
        } else {
            // Use firstRow/lastRow parameters  
            if (config.first_row != -1) {
                effective_start = config.first_row;
            }
            if (config.last_row != -1) {
                effective_stop = config.last_row + 1; // Convert inclusive to exclusive
            }
        }
        
        // Handle negative indexing (will be resolved after we know file size)
        bool has_negative_indices = (effective_start < 0) || 
                                  (effective_stop < 0 && effective_stop != INT64_MAX);
        
        if (config.verbose && (effective_start != 0 || effective_stop != INT64_MAX || effective_step != 1)) {
            std::cout << "Row range: start=" << effective_start 
                      << ", stop=" << (effective_stop == INT64_MAX ? "end" : std::to_string(effective_stop))
                      << ", step=" << effective_step << std::endl;
            if (has_negative_indices) {
                std::cout << "Note: Negative indices will be resolved after reading file" << std::endl;
            }
        }
        
        // Open output CSV file
        std::ofstream output(config.output_file);
        if (!output.is_open()) {
            throw std::runtime_error("Cannot create output file: " + config.output_file);
        }
        
        // Write header if requested
        if (config.include_header) {
            for (size_t i = 0; i < layout.columnCount(); ++i) {
                if (i > 0) output << config.delimiter;
                output << escapeCSVField(layout.columnName(i), config.delimiter, config.quote_char, config.quote_all);
            }
            output << "\n";
        }
        
        // Convert data rows with range support
        size_t total_rows_read = 0;
        size_t output_rows_written = 0;
        int64_t file_size = -1; // Will be determined if negative indexing is used
        
        // Pre-calculate frequently used values outside the loop
        const size_t num_columns = layout.columnCount();
        std::string value;  // Reuse string object to reduce allocations
        value.reserve(256); // Pre-allocate capacity for typical field sizes
        
        // If we have negative indices, we need to get the file size to resolve them
        std::vector<bool> rows_to_output;
        if (has_negative_indices) {
            if (config.verbose) {
                std::cout << "Counting rows to resolve negative indices..." << std::endl;
            }
            
            // Try to use the efficient countRows() function first
            try {
                file_size = static_cast<int64_t>(reader.countRows());
                if (file_size == 0) {
                    throw std::runtime_error("countRows() returned 0");
                }
                if (config.verbose) {
                    std::cout << "Used countRows(): " << file_size << " rows" << std::endl;
                }
            } catch (const std::exception& e) {
                if (config.verbose) {
                    std::cout << "countRows() failed (" << e.what() << "), falling back to manual counting..." << std::endl;
                }
                // Fallback: manual counting
                file_size = 0;
                while (reader.readNext()) {
                    file_size++;
                }
                // Reset reader for actual processing
                reader.close();
                reader.open(config.input_file);
                total_rows_read = 0;
                if (config.verbose) {
                    std::cout << "Manual counting found: " << file_size << " rows" << std::endl;
                }
            }
            
            // Resolve negative indices
            if (effective_start < 0) {
                effective_start = file_size + effective_start;
                effective_start = std::max(static_cast<int64_t>(0), effective_start);
            }
            if (effective_stop < 0 && effective_stop != INT64_MAX && effective_stop != INT64_MIN) {
                effective_stop = file_size + effective_stop;
            }
            
            // Clamp to valid range
            effective_start = std::max(static_cast<int64_t>(0), effective_start);
            effective_stop = std::min(file_size, effective_stop);
            
            if (config.verbose) {
                std::cout << "File contains " << file_size << " rows" << std::endl;
                std::cout << "Resolved range: [" << effective_start << ":" << effective_stop << ":" << effective_step << "]" << std::endl;
            }
            
            // Create mask for which rows to output
            rows_to_output.resize(file_size, false);
            for (int64_t i = effective_start; i < effective_stop; i += effective_step) {
                if (i >= 0 && i < file_size) {
                    rows_to_output[i] = true;
                }
            }
        }
        
        // Main conversion loop
        while (reader.readNext()) {
            bool should_output = false;
            
            if (has_negative_indices) {
                // Use pre-calculated mask
                should_output = (total_rows_read < rows_to_output.size()) && rows_to_output[total_rows_read];
            } else {
                // Real-time range checking (cast to avoid signed/unsigned comparison warnings)
                if (static_cast<int64_t>(total_rows_read) >= effective_start) {
                    if (static_cast<int64_t>(total_rows_read) < effective_stop) {
                        // Check step
                        int64_t offset_from_start = static_cast<int64_t>(total_rows_read) - effective_start;
                        should_output = (offset_from_start % effective_step) == 0;
                    }
                }
            }
            
            if (should_output) {
                for (size_t col = 0; col < num_columns; ++col) {
                    if (col > 0) output << config.delimiter;
                    
                    value = getRowValueAsString(reader, col, layout.columnType(col), config.float_precision);
                    output << escapeCSVField(value, config.delimiter, config.quote_char, config.quote_all);
                }
                output << "\n";
                ++output_rows_written;
            }
            
            ++total_rows_read;
            
            // Early termination for efficiency (when not using negative indices or step > 1)
            if (!has_negative_indices && effective_step == 1 && static_cast<int64_t>(total_rows_read) >= effective_stop) {
                break;
            }
            
            if (config.verbose && (total_rows_read & 0x3FFF) == 0) {  // Every 16384 rows for better performance
                std::cout << "Processed " << total_rows_read << " rows, output " << output_rows_written << " rows..." << std::endl;
            }
        }
        
        reader.close();
        output.flush();  // Ensure all data is written to file
        output.close();  // Close the output file
        
        std::cout << "Successfully converted " << output_rows_written << " rows to " << config.output_file;
        if (total_rows_read != output_rows_written) {
            std::cout << " (from " << total_rows_read << " total rows)";
        }
        std::cout << std::endl;
        
        if (config.verbose) {
            auto x = std::filesystem::file_size(config.output_file);
            std::cout << "Output file size: " << x << " bytes" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
