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
#include <vector>
#include <sstream>
#include <filesystem>
#include <iomanip>
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
std::string getRowValueAsString(const bcsv::Reader<bcsv::Layout>& reader, size_t column_index, bcsv::ColumnDataType type, int float_precision = -1) {
    try {
        switch (type) {
            case bcsv::ColumnDataType::BOOL:
                return valueToString(reader.row().get<bool>(column_index));
            case bcsv::ColumnDataType::INT8:
                return valueToString(reader.row().get<int8_t>(column_index));
            case bcsv::ColumnDataType::INT16:
                return valueToString(reader.row().get<int16_t>(column_index));
            case bcsv::ColumnDataType::INT32:
                return valueToString(reader.row().get<int32_t>(column_index));
            case bcsv::ColumnDataType::INT64:
                return valueToString(reader.row().get<int64_t>(column_index));
            case bcsv::ColumnDataType::UINT8:
                return valueToString(reader.row().get<uint8_t>(column_index));
            case bcsv::ColumnDataType::UINT16:
                return valueToString(reader.row().get<uint16_t>(column_index));
            case bcsv::ColumnDataType::UINT32:
                return valueToString(reader.row().get<uint32_t>(column_index));
            case bcsv::ColumnDataType::UINT64:
                return valueToString(reader.row().get<uint64_t>(column_index));
#if BCSV_HAS_FLOAT16
            case bcsv::ColumnDataType::FLOAT16:
                return valueToString(reader.row().get<std::float16_t>(column_index), float_precision);
#endif
#if BCSV_HAS_BFLOAT16
            case bcsv::ColumnDataType::BFLOAT16:
                return valueToString(reader.row().get<std::bfloat16_t>(column_index), float_precision);
#endif
            case bcsv::ColumnDataType::FLOAT:
                return valueToString(reader.row().get<float>(column_index), float_precision);
            case bcsv::ColumnDataType::DOUBLE:
                return valueToString(reader.row().get<double>(column_index), float_precision);
#if BCSV_HAS_FLOAT128
            case bcsv::ColumnDataType::FLOAT128:
                return valueToString(reader.row().get<std::float128_t>(column_index), float_precision);
#endif
            case bcsv::ColumnDataType::STRING:
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
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -h, --help              Show this help message\n\n";
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
        
        const auto& layout = reader.getLayout();
        
        if (config.verbose) {
            std::cout << "Opened BCSV file successfully" << std::endl;
            std::cout << "Layout contains " << layout.getColumnCount() << " columns:" << std::endl;
            
            for (size_t i = 0; i < layout.getColumnCount(); ++i) {
                std::cout << "  " << layout.getColumnName(i) << " (";
                switch (layout.getColumnType(i)) {
                    case bcsv::ColumnDataType::BOOL: std::cout << "BOOL"; break;
                    case bcsv::ColumnDataType::INT8: std::cout << "INT8"; break;
                    case bcsv::ColumnDataType::INT16: std::cout << "INT16"; break;
                    case bcsv::ColumnDataType::INT32: std::cout << "INT32"; break;
                    case bcsv::ColumnDataType::INT64: std::cout << "INT64"; break;
                    case bcsv::ColumnDataType::UINT8: std::cout << "UINT8"; break;
                    case bcsv::ColumnDataType::UINT16: std::cout << "UINT16"; break;
                    case bcsv::ColumnDataType::UINT32: std::cout << "UINT32"; break;
                    case bcsv::ColumnDataType::UINT64: std::cout << "UINT64"; break;
#if BCSV_HAS_FLOAT16
                    case bcsv::ColumnDataType::FLOAT16: std::cout << "FLOAT16"; break;
#endif
#if BCSV_HAS_BFLOAT16
                    case bcsv::ColumnDataType::BFLOAT16: std::cout << "BFLOAT16"; break;
#endif
                    case bcsv::ColumnDataType::FLOAT: std::cout << "FLOAT"; break;
                    case bcsv::ColumnDataType::DOUBLE: std::cout << "DOUBLE"; break;
#if BCSV_HAS_FLOAT128
                    case bcsv::ColumnDataType::FLOAT128: std::cout << "FLOAT128"; break;
#endif
                    case bcsv::ColumnDataType::STRING: std::cout << "STRING"; break;
                    default: std::cout << "UNKNOWN"; break;
                }
                std::cout << ")" << std::endl;
            }
        }
        
        // Open output CSV file
        std::ofstream output(config.output_file);
        if (!output.is_open()) {
            throw std::runtime_error("Cannot create output file: " + config.output_file);
        }
        
        // Write header if requested
        if (config.include_header) {
            for (size_t i = 0; i < layout.getColumnCount(); ++i) {
                if (i > 0) output << config.delimiter;
                output << escapeCSVField(layout.getColumnName(i), config.delimiter, config.quote_char, config.quote_all);
            }
            output << "\n";
        }
        
        // Convert data rows
        size_t row_count = 0;
        
        // Pre-calculate frequently used values outside the loop
        const size_t num_columns = layout.getColumnCount();
        std::string value;  // Reuse string object to reduce allocations
        value.reserve(256); // Pre-allocate capacity for typical field sizes
        
        while (reader.readNext()) {
            for (size_t col = 0; col < num_columns; ++col) {
                if (col > 0) output << config.delimiter;
                
                value = getRowValueAsString(reader, col, layout.getColumnType(col), config.float_precision);
                output << escapeCSVField(value, config.delimiter, config.quote_char, config.quote_all);
            }
            output << "\n";
            ++row_count;  // Pre-increment is slightly faster
            
            if (config.verbose && (row_count & 0x3FFF) == 0) {  // Every 16384 rows for better performance
                std::cout << "Processed " << row_count << " rows..." << std::endl;
            }
        }
        
        reader.close();
        
        std::cout << "Successfully converted " << row_count << " rows to " << config.output_file << std::endl;
        
        if (config.verbose) {
            auto file_size = std::filesystem::file_size(config.output_file);
            std::cout << "Output file size: " << file_size << " bytes" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
