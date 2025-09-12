/**
 * @file csv2bcsv.cpp
 * @brief CLI tool to convert CSV files to BCSV format
 * 
 * This tool reads a CSV file and converts it to the binary BCSV format.
 * It automatically detects data types and creates an appropriate layout.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <regex>
#include <bcsv/bcsv.h>

struct Config {
    std::string input_file;
    std::string output_file;
    char delimiter = '\0';  // '\0' means auto-detect
    char quote_char = '"';
    char decimal_separator = '.';  // Default to point, can be changed to comma
    bool has_header = true;
    bool verbose = false;
    bool help = false;
    bool force_delimiter = false;  // True if user explicitly set delimiter
};

// Enhanced data type detection with range analysis
struct ColumnStats {
    int64_t min_int = INT64_MAX;
    int64_t max_int = INT64_MIN;
    double min_double = std::numeric_limits<double>::max();
    double max_double = std::numeric_limits<double>::lowest();
    bool has_decimals = false;
    bool all_integers = true;
    bool all_booleans = true;
    bool all_empty = true;
    bool all_float_compatible = true;  // New: tracks if all values can be represented exactly in float
    uint32_t max_decimal_places = 0;  // Track the maximum decimal places in input strings
    bool requires_high_precision = false;  // True if any value has >6 decimal places or >7 total digits
    size_t sample_count = 0;
};

// Detect optimal data type based on column statistics
bcsv::ColumnDataType detectOptimalType(const ColumnStats& stats) {
    if (stats.all_empty || stats.sample_count == 0) {
        return bcsv::ColumnDataType::STRING;
    }
    
    if (stats.all_booleans && stats.sample_count > 0) {
        return bcsv::ColumnDataType::BOOL;
    }
    
    if (stats.all_integers && !stats.has_decimals) {
        // Choose smallest integer type that can hold the range
        if (stats.min_int >= 0 && stats.max_int <= 255) {
            return bcsv::ColumnDataType::UINT8;
        } else if (stats.min_int >= -128 && stats.max_int <= 127) {
            return bcsv::ColumnDataType::INT8;
        } else if (stats.min_int >= 0 && stats.max_int <= 65535) {
            return bcsv::ColumnDataType::UINT16;
        } else if (stats.min_int >= -32768 && stats.max_int <= 32767) {
            return bcsv::ColumnDataType::INT16;
        } else if (stats.min_int >= 0 && stats.max_int <= 4294967295ULL) {
            return bcsv::ColumnDataType::UINT32;
        } else if (stats.min_int >= INT32_MIN && stats.max_int <= INT32_MAX) {
            return bcsv::ColumnDataType::INT32;
        } else if (stats.min_int >= 0) {
            return bcsv::ColumnDataType::UINT64;
        } else {
            return bcsv::ColumnDataType::INT64;
        }
    }
    
    if (stats.has_decimals) {
        // Use string-based precision analysis to choose optimal floating-point type
        if (stats.requires_high_precision) {
            // High precision explicitly requested by user (>6 decimal places or >7 total digits)
#if BCSV_HAS_FLOAT128
            // Use 128-bit quadruple precision for maximum accuracy
            return bcsv::ColumnDataType::FLOAT128;
#else
            // Fall back to double precision
            return bcsv::ColumnDataType::DOUBLE;
#endif
        } else if (stats.max_decimal_places <= 2) {
            // Very low precision requirements - consider half precision types
#if BCSV_HAS_FLOAT16
            // Use 16-bit half precision for maximum space efficiency
            return bcsv::ColumnDataType::FLOAT16;
#else
            // Fall back to single precision
            return bcsv::ColumnDataType::FLOAT;
#endif
        } else if (stats.max_decimal_places <= 6) {
            // User provided reasonable precision - use single precision
            // Float provides ~7 decimal digits, which is sufficient for ≤6 decimal places
            return bcsv::ColumnDataType::FLOAT;
        } else {
            // Higher precision requirements need double precision
            return bcsv::ColumnDataType::DOUBLE;
        }
    }
    
    return bcsv::ColumnDataType::STRING;
}

// Analyze the precision requirements from the original string
std::pair<uint32_t, bool> analyzeStringPrecision(const std::string& value, char decimal_separator = '.') {
    // Find decimal point
    size_t decimal_pos = value.find(decimal_separator);
    if (decimal_pos == std::string::npos) {
        return {0, false}; // No decimal places, not high precision
    }
    
    // Count meaningful decimal places (excluding trailing zeros)
    std::string decimal_part = value.substr(decimal_pos + 1);
    
    // Remove trailing zeros
    while (!decimal_part.empty() && decimal_part.back() == '0') {
        decimal_part.pop_back();
    }
    
    uint32_t decimal_places = static_cast<uint32_t>(decimal_part.length());
    
    // Count total significant digits
    std::string digits_only;
    bool found_first_nonzero = false;
    bool after_decimal = false;
    
    for (char c : value) {
        if (c == decimal_separator) {
            after_decimal = true;
        } else if (std::isdigit(c)) {
            if (c != '0' || found_first_nonzero || after_decimal) {
                digits_only += c;
                if (c != '0') found_first_nonzero = true;
            }
        }
    }
    
    // Determine if high precision is required
    // Use >6 decimal places or >7 total significant digits as threshold
    bool high_precision = (decimal_places > 6) || (digits_only.length() > 7);
    
    return {decimal_places, high_precision};
}

// Enhanced data type detection helper
void analyzeValue(const std::string& value, ColumnStats& stats, char decimal_separator = '.') {
    if (value.empty()) {
        return; // Don't count empty values
    }
    
    stats.all_empty = false;
    stats.sample_count++;
    
    // Check for boolean
    std::string lower_val = value;
    std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), 
        [](char c) { return static_cast<char>(::tolower(c)); });
    if (lower_val == "true" || lower_val == "false" || lower_val == "1" || lower_val == "0") {
        if (stats.all_booleans) {
            return; // Still could be boolean
        }
    } else {
        stats.all_booleans = false;
    }
    
    // Normalize decimal separator for parsing
    std::string normalized_value = value;
    if (decimal_separator != '.') {
        std::replace(normalized_value.begin(), normalized_value.end(), decimal_separator, '.');
    }
    
    // Try to parse as number
    char* end;
    errno = 0;
    
    // Try integer first (only if no decimal separator in original value)
    if (value.find(decimal_separator) == std::string::npos) {
        long long int_val = strtoll(normalized_value.c_str(), &end, 10);
        if (errno == 0 && *end == '\0' && end != normalized_value.c_str()) {
            // Valid integer
            stats.min_int = std::min(stats.min_int, static_cast<int64_t>(int_val));
            stats.max_int = std::max(stats.max_int, static_cast<int64_t>(int_val));
            return;
        }
    }
    
    // Try double
    double double_val = strtod(normalized_value.c_str(), &end);
    if (errno == 0 && *end == '\0' && end != normalized_value.c_str()) {
        // Valid double
        stats.all_integers = false;
        stats.has_decimals = true;
        stats.min_double = std::min(stats.min_double, double_val);
        stats.max_double = std::max(stats.max_double, double_val);
        
        // Analyze the precision requirements from the original string
        auto [decimal_places, high_precision] = analyzeStringPrecision(value, decimal_separator);
        stats.max_decimal_places = std::max(stats.max_decimal_places, decimal_places);
        if (high_precision) {
            stats.requires_high_precision = true;
        }
        
        // Check if this specific value can be represented exactly in float
        // (only relevant if string precision doesn't already require double)
        if (stats.all_float_compatible && !high_precision) {
            float float_val = static_cast<float>(double_val);
            if (static_cast<double>(float_val) != double_val) {
                stats.all_float_compatible = false;
            }
        }
        
        return;
    }
    
    // Must be string
    stats.all_integers = false;
    stats.all_booleans = false;
    stats.has_decimals = false;
}

// Automatic delimiter detection
char detectDelimiter(const std::string& sample_line) {
    const std::vector<char> delimiters = {',', ';', '\t', '|'};
    std::map<char, int> delimiter_counts;
    
    bool in_quotes = false;
    char quote_char = '"';
    
    for (char c : sample_line) {
        if (c == quote_char) {
            in_quotes = !in_quotes;
        } else if (!in_quotes) {
            for (char delim : delimiters) {
                if (c == delim) {
                    delimiter_counts[delim]++;
                }
            }
        }
    }
    
    // Return delimiter with highest count
    char best_delimiter = ',';
    int max_count = 0;
    for (const auto& pair : delimiter_counts) {
        if (pair.second > max_count) {
            max_count = pair.second;
            best_delimiter = pair.first;
        }
    }
    
    return best_delimiter;
}

// Legacy simple type detection (kept for compatibility)
bcsv::ColumnDataType detectDataType(const std::string& value) {
    if (value.empty()) {
        return bcsv::ColumnDataType::STRING; // Default to string for empty values
    }
    
    // Check for boolean
    std::string lower_val = value;
    std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), 
        [](char c) { return static_cast<char>(::tolower(c)); });
    if (lower_val == "true" || lower_val == "false" || lower_val == "1" || lower_val == "0") {
        return bcsv::ColumnDataType::BOOL;
    }
    
    // Check for integer
    std::regex int_pattern(R"(^[-+]?\d+$)");
    if (std::regex_match(value, int_pattern)) {
        long long num = std::stoll(value);
        if (num >= INT32_MIN && num <= INT32_MAX) {
            return bcsv::ColumnDataType::INT32;
        } else {
            return bcsv::ColumnDataType::INT64;
        }
    }
    
    // Check for float/double
    std::regex float_pattern(R"(^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$)");
    if (std::regex_match(value, float_pattern)) {
        return bcsv::ColumnDataType::DOUBLE;
    }
    
    // Default to string
    return bcsv::ColumnDataType::STRING;
}

// Parse CSV line with proper quote handling
std::vector<std::string> parseCSVLine(const std::string& line, char delimiter, char quote_char) {
    std::vector<std::string> fields;
    std::string current_field;
    bool in_quotes = false;
    bool quote_started = false;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        
        if (c == quote_char) {
            if (!quote_started && current_field.empty()) {
                // Starting quoted field
                in_quotes = true;
                quote_started = true;
            } else if (in_quotes) {
                // Check for escaped quote (double quote)
                if (i + 1 < line.length() && line[i + 1] == quote_char) {
                    current_field += quote_char;
                    ++i; // Skip the next quote
                } else {
                    // End of quoted field
                    in_quotes = false;
                }
            } else {
                // Quote in unquoted field (treat as regular character)
                current_field += c;
            }
        } else if (c == delimiter && !in_quotes) {
            // Field separator
            fields.push_back(current_field);
            current_field.clear();
            quote_started = false;
        } else {
            current_field += c;
        }
    }
    
    // Add the last field
    fields.push_back(current_field);
    return fields;
}

// Convert string value to appropriate type and set in row
void setRowValue(bcsv::Writer<bcsv::Layout>& writer, size_t column_index, 
                 const std::string& value, bcsv::ColumnDataType type, char decimal_separator = '.') {
    if (value.empty()) {
        // Handle empty values - set default values
        switch (type) {
            case bcsv::ColumnDataType::BOOL:
                writer.row.set(column_index, false);
                break;
            case bcsv::ColumnDataType::INT8:
                writer.row.set(column_index, static_cast<int8_t>(0));
                break;
            case bcsv::ColumnDataType::UINT8:
                writer.row.set(column_index, static_cast<uint8_t>(0));
                break;
            case bcsv::ColumnDataType::INT16:
                writer.row.set(column_index, static_cast<int16_t>(0));
                break;
            case bcsv::ColumnDataType::UINT16:
                writer.row.set(column_index, static_cast<uint16_t>(0));
                break;
            case bcsv::ColumnDataType::INT32:
                writer.row.set(column_index, static_cast<int32_t>(0));
                break;
            case bcsv::ColumnDataType::UINT32:
                writer.row.set(column_index, static_cast<uint32_t>(0));
                break;
            case bcsv::ColumnDataType::INT64:
                writer.row.set(column_index, static_cast<int64_t>(0));
                break;
            case bcsv::ColumnDataType::UINT64:
                writer.row.set(column_index, static_cast<uint64_t>(0));
                break;
            case bcsv::ColumnDataType::FLOAT:
                writer.row.set(column_index, 0.0f);
                break;
            case bcsv::ColumnDataType::DOUBLE:
                writer.row.set(column_index, 0.0);
                break;
            case bcsv::ColumnDataType::STRING:
                writer.row.set(column_index, std::string(""));
                break;
            default:
                writer.row.set(column_index, std::string(""));
        }
        return;
    }
    
    // Normalize decimal separator for parsing
    std::string normalized_value = value;
    if (decimal_separator != '.' && (type == bcsv::ColumnDataType::FLOAT || type == bcsv::ColumnDataType::DOUBLE)) {
        std::replace(normalized_value.begin(), normalized_value.end(), decimal_separator, '.');
    }
    
    try {
        switch (type) {
            case bcsv::ColumnDataType::BOOL: {
                std::string lower_val = value;
                std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), 
                    [](char c) { return static_cast<char>(::tolower(c)); });
                bool bool_val = (lower_val == "true" || lower_val == "1");
                writer.row.set(column_index, bool_val);
                break;
            }
            case bcsv::ColumnDataType::INT8:
                writer.row.set(column_index, static_cast<int8_t>(std::stoll(value)));
                break;
            case bcsv::ColumnDataType::UINT8:
                writer.row.set(column_index, static_cast<uint8_t>(std::stoull(value)));
                break;
            case bcsv::ColumnDataType::INT16:
                writer.row.set(column_index, static_cast<int16_t>(std::stoll(value)));
                break;
            case bcsv::ColumnDataType::UINT16:
                writer.row.set(column_index, static_cast<uint16_t>(std::stoull(value)));
                break;
            case bcsv::ColumnDataType::INT32:
                writer.row.set(column_index, static_cast<int32_t>(std::stoll(value)));
                break;
            case bcsv::ColumnDataType::UINT32:
                writer.row.set(column_index, static_cast<uint32_t>(std::stoull(value)));
                break;
            case bcsv::ColumnDataType::INT64:
                writer.row.set(column_index, static_cast<int64_t>(std::stoll(value)));
                break;
            case bcsv::ColumnDataType::UINT64:
                writer.row.set(column_index, static_cast<uint64_t>(std::stoull(value)));
                break;
            case bcsv::ColumnDataType::FLOAT:
                writer.row.set(column_index, std::stof(normalized_value));
                break;
            case bcsv::ColumnDataType::DOUBLE:
                writer.row.set(column_index, std::stod(normalized_value));
                break;
            case bcsv::ColumnDataType::STRING:
            default:
                writer.row.set(column_index, value);
                break;
        }
    } catch (const std::exception&) {
        // If conversion fails, store as string
        writer.row.set(column_index, value);
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE [OUTPUT_FILE]\n\n";
    std::cout << "Convert CSV file to BCSV format.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input CSV file path\n";
    std::cout << "  OUTPUT_FILE    Output BCSV file path (default: INPUT_FILE.bcsv)\n\n";
    std::cout << "Options:\n";
    std::cout << "  -d, --delimiter CHAR    Field delimiter (default: auto-detect)\n";
    std::cout << "  -q, --quote CHAR        Quote character (default: '\"')\n";
    std::cout << "  --no-header             CSV file has no header row\n";
    std::cout << "  --decimal-separator CHAR  Decimal separator: '.' or ',' (default: '.')\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -h, --help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " data.csv\n";
    std::cout << "  " << program_name << " -d ';' data.csv output.bcsv\n";
    std::cout << "  " << program_name << " --no-header -v data.csv\n";
    std::cout << "  " << program_name << " --decimal-separator ',' german_data.csv\n";
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
                config.force_delimiter = true;
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "-q" || arg == "--quote") {
            if (i + 1 < argc) {
                config.quote_char = argv[++i][0];
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "--no-header") {
            config.has_header = false;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--decimal-separator") {
            if (i + 1 < argc) {
                std::string sep = argv[++i];
                if (sep.length() == 1 && (sep[0] == '.' || sep[0] == ',')) {
                    config.decimal_separator = sep[0];
                } else {
                    throw std::runtime_error("Decimal separator must be '.' or ','");
                }
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
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
        config.output_file = input_path.stem().string() + ".bcsv";
    }
    
    // Validate character conflicts
    if (!config.help) {
        if (config.delimiter == config.quote_char) {
            throw std::runtime_error("Delimiter and quote character cannot be the same ('" + 
                                   std::string(1, config.delimiter) + "')");
        }
        if (config.delimiter == config.decimal_separator) {
            throw std::runtime_error("Delimiter and decimal separator cannot be the same ('" + 
                                   std::string(1, config.delimiter) + "')");
        }
        if (config.quote_char == config.decimal_separator) {
            throw std::runtime_error("Quote character and decimal separator cannot be the same ('" + 
                                   std::string(1, config.quote_char) + "')");
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
            std::cout << "Header: " << (config.has_header ? "yes" : "no") << std::endl;
            std::cout << "Decimal separator: '" << config.decimal_separator << "'" << std::endl;
        }
        
        // Check if input file exists
        if (!std::filesystem::exists(config.input_file)) {
            throw std::runtime_error("Input file does not exist: " + config.input_file);
        }
        
        std::ifstream input(config.input_file);
        if (!input.is_open()) {
            throw std::runtime_error("Cannot open input file: " + config.input_file);
        }

        std::string line;
        std::vector<std::string> headers;
        std::vector<bcsv::ColumnDataType> column_types;
        std::vector<std::vector<std::string>> sample_data;
        
        // Read first line for auto-detection
        if (!std::getline(input, line)) {
            throw std::runtime_error("Input file is empty");
        }
        
        // Auto-detect delimiter if not specified
        if (!config.force_delimiter) {
            config.delimiter = detectDelimiter(line);
            if (config.verbose) {
                std::cout << "Auto-detected delimiter: '" << config.delimiter << "'" << std::endl;
            }
        }
        
        if (config.verbose) {
            std::cout << "Converting: " << config.input_file << " -> " << config.output_file << std::endl;
            std::cout << "Delimiter: '" << config.delimiter << "', Quote: '" << config.quote_char << "'" << std::endl;
            std::cout << "Header: " << (config.has_header ? "yes" : "no") << std::endl;
        }
        
        std::vector<std::string> first_row = parseCSVLine(line, config.delimiter, config.quote_char);
        
        if (config.has_header) {
            headers = first_row;
            // Filter out empty column names (from trailing delimiters)
            while (!headers.empty() && headers.back().empty()) {
                headers.pop_back();
            }
        } else {
            // Filter out empty trailing fields from first row
            while (!first_row.empty() && first_row.back().empty()) {
                first_row.pop_back();
            }
            // Generate column names
            for (size_t i = 0; i < first_row.size(); ++i) {
                headers.push_back("column_" + std::to_string(i + 1));
            }
            sample_data.push_back(first_row);
        }
        
        // Read sample data to analyze types (up to 1000 rows for better detection)
        size_t sample_count = 0;
        while (std::getline(input, line) && sample_count < 1000) {
            auto row_data = parseCSVLine(line, config.delimiter, config.quote_char);
            
            // Trim trailing empty fields to match header count
            while (row_data.size() > headers.size() && !row_data.empty() && row_data.back().empty()) {
                row_data.pop_back();
            }
            
            // Accept rows that match header count, or pad/truncate if close
            if (row_data.size() == headers.size()) {
                sample_data.push_back(row_data);
                sample_count++;
            } else if (row_data.size() < headers.size()) {
                // Pad with empty strings if row is shorter
                row_data.resize(headers.size(), "");
                sample_data.push_back(row_data);
                sample_count++;
            } else if (row_data.size() <= headers.size() + 3) {
                // If only a few extra fields, truncate (likely trailing delimiters)
                row_data.resize(headers.size());
                sample_data.push_back(row_data);
                sample_count++;
            } else if (config.verbose) {
                // Only warn for significantly different row sizes
                std::cerr << "Warning: Row " << (sample_count + 1) << " has " << row_data.size() 
                          << " fields, expected " << headers.size() << ". Skipping." << std::endl;
            }
        }
        
        if (sample_data.empty()) {
            throw std::runtime_error("No valid data rows found");
        }
        
        // Analyze column statistics for optimal type detection
        std::vector<ColumnStats> column_stats(headers.size());
        
        for (const auto& row : sample_data) {
            for (size_t col = 0; col < std::min(row.size(), headers.size()); ++col) {
                analyzeValue(row[col], column_stats[col], config.decimal_separator);
            }
        }
        
        // Determine optimal types based on statistics
        column_types.resize(headers.size());
        for (size_t col = 0; col < headers.size(); ++col) {
            column_types[col] = detectOptimalType(column_stats[col]);
        }
        
        if (config.verbose) {
            std::cout << "Detected " << headers.size() << " columns:" << std::endl;
            for (size_t i = 0; i < headers.size(); ++i) {
                std::cout << "  " << headers[i] << " -> ";
                switch (column_types[i]) {
                    case bcsv::ColumnDataType::BOOL: std::cout << "BOOL"; break;
                    case bcsv::ColumnDataType::INT8: std::cout << "INT8"; break;
                    case bcsv::ColumnDataType::UINT8: std::cout << "UINT8"; break;
                    case bcsv::ColumnDataType::INT16: std::cout << "INT16"; break;
                    case bcsv::ColumnDataType::UINT16: std::cout << "UINT16"; break;
                    case bcsv::ColumnDataType::INT32: std::cout << "INT32"; break;
                    case bcsv::ColumnDataType::UINT32: std::cout << "UINT32"; break;
                    case bcsv::ColumnDataType::INT64: std::cout << "INT64"; break;
                    case bcsv::ColumnDataType::UINT64: std::cout << "UINT64"; break;
                    case bcsv::ColumnDataType::FLOAT: std::cout << "FLOAT"; break;
                    case bcsv::ColumnDataType::DOUBLE: std::cout << "DOUBLE"; break;
                    case bcsv::ColumnDataType::STRING: std::cout << "STRING"; break;
                    default: std::cout << "UNKNOWN"; break;
                }
                std::cout << std::endl;
            }
        }
        
        // Create BCSV layout
        bcsv::Layout layout;
        for (size_t i = 0; i < headers.size(); ++i) {
            bcsv::ColumnDefinition col(headers[i], column_types[i]);
            layout.addColumn(col);
        }
        
        // Reset file and skip header if present
        input.clear();
        input.seekg(0, std::ios::beg);
        if (config.has_header) {
            std::getline(input, line); // Skip header
        }
        
        // Create BCSV writer and convert data
        {
            bcsv::Writer<bcsv::Layout> writer(layout);
            // Use compression level 1 for better performance vs file size
            writer.open(config.output_file, true, 1);
            size_t row_count = 0;
            
            // Pre-calculate frequently used values outside the loop
            const size_t num_columns = headers.size();
            
            while (std::getline(input, line)) {
                auto row_data = parseCSVLine(line, config.delimiter, config.quote_char);
                
                // Apply same flexible row handling as during sampling
                while (row_data.size() > num_columns && !row_data.empty() && row_data.back().empty()) {
                    row_data.pop_back();
                }
                
                bool process_row = false;
                if (row_data.size() == num_columns) {
                    process_row = true;
                } else if (row_data.size() < num_columns) {
                    // Pad with empty strings if row is shorter
                    row_data.resize(num_columns, "");
                    process_row = true;
                } else if (row_data.size() <= num_columns + 3) {
                    // If only a few extra fields, truncate
                    row_data.resize(num_columns);
                    process_row = true;
                } else if (config.verbose) {
                    std::cerr << "Warning: Row " << (row_count + 1) << " has " << row_data.size() 
                              << " fields, expected " << num_columns << ". Skipping." << std::endl;
                }
                
                if (process_row) {
                    for (size_t col = 0; col < num_columns; ++col) {
                        setRowValue(writer, col, row_data[col], column_types[col], config.decimal_separator);
                    }
                    
                    writer.writeRow();
                    ++row_count;  // Pre-increment is slightly faster
                    
                    if (config.verbose && (row_count & 0x3FFF) == 0) {  // Every 16384 rows for better performance
                        std::cout << "Processed " << row_count << " rows..." << std::endl;
                    }
                }
            }
            
            writer.close();
            std::cout << "Successfully converted " << row_count << " rows to " << config.output_file << std::endl;
        }
        
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
