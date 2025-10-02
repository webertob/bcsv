/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

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
#include <filesystem>
#include <regex>
#include <chrono>
#include <iomanip>
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
    bool use_zoh = true;  // Use Zero-Order Hold compression by default
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
bcsv::ColumnType detectOptimalType(const ColumnStats& stats) {
    if (stats.all_empty || stats.sample_count == 0) {
        return bcsv::ColumnType::STRING;
    }
    
    if (stats.all_booleans && stats.sample_count > 0) {
        return bcsv::ColumnType::BOOL;
    }
    
    if (stats.all_integers && !stats.has_decimals) {
        // Choose smallest integer type that can hold the range
        if (stats.min_int >= 0 && stats.max_int <= 255) {
            return bcsv::ColumnType::UINT8;
        } else if (stats.min_int >= -128 && stats.max_int <= 127) {
            return bcsv::ColumnType::INT8;
        } else if (stats.min_int >= 0 && stats.max_int <= 65535) {
            return bcsv::ColumnType::UINT16;
        } else if (stats.min_int >= -32768 && stats.max_int <= 32767) {
            return bcsv::ColumnType::INT16;
        } else if (stats.min_int >= 0 && stats.max_int <= static_cast<int64_t>(4294967295ULL)) {
            return bcsv::ColumnType::UINT32;
        } else if (stats.min_int >= INT32_MIN && stats.max_int <= INT32_MAX) {
            return bcsv::ColumnType::INT32;
        } else if (stats.min_int >= 0) {
            return bcsv::ColumnType::UINT64;
        } else {
            return bcsv::ColumnType::INT64;
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
            return bcsv::ColumnType::DOUBLE;
#endif
        } else if (stats.max_decimal_places <= 2) {
            // Very low precision requirements - consider half precision types
#if BCSV_HAS_FLOAT16
            // Use 16-bit half precision for maximum space efficiency
            return bcsv::ColumnDataType::FLOAT16;
#else
            // Fall back to single precision
            return bcsv::ColumnType::FLOAT;
#endif
        } else if (stats.max_decimal_places <= 6) {
            // User provided reasonable precision - use single precision
            // Float provides ~7 decimal digits, which is sufficient for ≤6 decimal places
            return bcsv::ColumnType::FLOAT;
        } else {
            // Higher precision requirements need double precision
            return bcsv::ColumnType::DOUBLE;
        }
    }
    
    return bcsv::ColumnType::STRING;
}

// Analyze the precision requirements from the original string
std::pair<uint32_t, bool> analyzeStringPrecision(const std::string& value, char decimal_separator = '.') {
    // Handle scientific notation (e.g., "1.0233453e+23")
    std::string mantissa_part = value;
    size_t exp_pos = value.find_first_of("eE");
    if (exp_pos != std::string::npos) {
        mantissa_part = value.substr(0, exp_pos);
    }
    
    // Find decimal point in mantissa
    size_t decimal_pos = mantissa_part.find(decimal_separator);
    if (decimal_pos == std::string::npos) {
        // No decimal point - check if this is an integer or scientific notation without decimal
        if (exp_pos != std::string::npos) {
            // Scientific notation without decimal (e.g., "123e+5")
            // Count digits in the integer part as significant digits
            uint32_t significant_digits = 0;
            bool found_first_nonzero = false;
            
            for (char c : mantissa_part) {
                if (std::isdigit(c)) {
                    if (c != '0' || found_first_nonzero) {
                        significant_digits++;
                        if (c != '0') found_first_nonzero = true;
                    }
                }
            }
            
            // No decimal places, but check if total significant digits require high precision
            bool high_precision = significant_digits > 7;
            return {0, high_precision};
        }
        return {0, false}; // Plain integer, no decimal places, not high precision
    }
    
    // Count decimal places - RESPECT USER INTENT by keeping trailing zeros
    // The user explicitly wrote those trailing zeros, indicating desired precision
    std::string decimal_part = mantissa_part.substr(decimal_pos + 1);
    uint32_t decimal_places = static_cast<uint32_t>(decimal_part.length());
    
    // Count total significant digits in the mantissa
    std::string digits_only;
    bool found_first_nonzero = false;
    bool after_decimal = false;
    
    for (char c : mantissa_part) {
        if (c == decimal_separator) {
            after_decimal = true;
        } else if (std::isdigit(c)) {
            if (c != '0' || found_first_nonzero || after_decimal) {
                digits_only += c;
                if (c != '0') found_first_nonzero = true;
            }
        }
    }
    
    // For scientific notation, the meaningful precision is determined by the mantissa
    // Example: "1.0233453e+23" has 8 meaningful digits (1 + 7 after decimal)
    uint32_t total_significant_digits = static_cast<uint32_t>(digits_only.length());
    
    // Determine if high precision is required
    // Use >6 decimal places or >7 total significant digits as threshold
    bool high_precision = (decimal_places > 6) || (total_significant_digits > 7);
    
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
bcsv::ColumnType detectDataType(const std::string& value) {
    if (value.empty()) {
        return bcsv::ColumnType::STRING; // Default to string for empty values
    }
    
    // Check for boolean
    std::string lower_val = value;
    std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), 
        [](char c) { return static_cast<char>(::tolower(c)); });
    if (lower_val == "true" || lower_val == "false" || lower_val == "1" || lower_val == "0") {
        return bcsv::ColumnType::BOOL;
    }
    
    // Check for integer
    std::regex int_pattern(R"(^[-+]?\d+$)");
    if (std::regex_match(value, int_pattern)) {
        long long num = std::stoll(value);
        if (num >= INT32_MIN && num <= INT32_MAX) {
            return bcsv::ColumnType::INT32;
        } else {
            return bcsv::ColumnType::INT64;
        }
    }
    
    // Check for float/double
    std::regex float_pattern(R"(^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$)");
    if (std::regex_match(value, float_pattern)) {
        return bcsv::ColumnType::DOUBLE;
    }
    
    // Default to string
    return bcsv::ColumnType::STRING;
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
                 const std::string& value, bcsv::ColumnType type, char decimal_separator = '.') {
    if (value.empty()) {
        // Handle empty values - set default values
        switch (type) {
            case bcsv::ColumnType::BOOL:
                writer.row().set(column_index, false);
                break;
            case bcsv::ColumnType::INT8:
                writer.row().set(column_index, static_cast<int8_t>(0));
                break;
            case bcsv::ColumnType::UINT8:
                writer.row().set(column_index, static_cast<uint8_t>(0));
                break;
            case bcsv::ColumnType::INT16:
                writer.row().set(column_index, static_cast<int16_t>(0));
                break;
            case bcsv::ColumnType::UINT16:
                writer.row().set(column_index, static_cast<uint16_t>(0));
                break;
            case bcsv::ColumnType::INT32:
                writer.row().set(column_index, static_cast<int32_t>(0));
                break;
            case bcsv::ColumnType::UINT32:
                writer.row().set(column_index, static_cast<uint32_t>(0));
                break;
            case bcsv::ColumnType::INT64:
                writer.row().set(column_index, static_cast<int64_t>(0));
                break;
            case bcsv::ColumnType::UINT64:
                writer.row().set(column_index, static_cast<uint64_t>(0));
                break;
            case bcsv::ColumnType::FLOAT:
                writer.row().set(column_index, 0.0f);
                break;
            case bcsv::ColumnType::DOUBLE:
                writer.row().set(column_index, 0.0);
                break;
            case bcsv::ColumnType::STRING:
                writer.row().set(column_index, std::string(""));
                break;
            default:
                writer.row().set(column_index, std::string(""));
        }
        return;
    }
    
    // Normalize decimal separator for parsing
    std::string normalized_value = value;
    if (decimal_separator != '.' && (type == bcsv::ColumnType::FLOAT || type == bcsv::ColumnType::DOUBLE)) {
        std::replace(normalized_value.begin(), normalized_value.end(), decimal_separator, '.');
    }
    
    try {
        switch (type) {
            case bcsv::ColumnType::BOOL: {
                std::string lower_val = value;
                std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), 
                    [](char c) { return static_cast<char>(::tolower(c)); });
                bool bool_val = (lower_val == "true" || lower_val == "1");
                writer.row().set(column_index, bool_val);
                break;
            }
            case bcsv::ColumnType::INT8:
                writer.row().set(column_index, static_cast<int8_t>(std::stoll(value)));
                break;
            case bcsv::ColumnType::UINT8:
                writer.row().set(column_index, static_cast<uint8_t>(std::stoull(value)));
                break;
            case bcsv::ColumnType::INT16:
                writer.row().set(column_index, static_cast<int16_t>(std::stoll(value)));
                break;
            case bcsv::ColumnType::UINT16:
                writer.row().set(column_index, static_cast<uint16_t>(std::stoull(value)));
                break;
            case bcsv::ColumnType::INT32:
                writer.row().set(column_index, static_cast<int32_t>(std::stoll(value)));
                break;
            case bcsv::ColumnType::UINT32:
                writer.row().set(column_index, static_cast<uint32_t>(std::stoull(value)));
                break;
            case bcsv::ColumnType::INT64:
                writer.row().set(column_index, static_cast<int64_t>(std::stoll(value)));
                break;
            case bcsv::ColumnType::UINT64:
                writer.row().set(column_index, static_cast<uint64_t>(std::stoull(value)));
                break;
            case bcsv::ColumnType::FLOAT:
                writer.row().set(column_index, std::stof(normalized_value));
                break;
            case bcsv::ColumnType::DOUBLE:
                writer.row().set(column_index, std::stod(normalized_value));
                break;
            case bcsv::ColumnType::STRING:
            default:
                writer.row().set(column_index, value);
                break;
        }
    } catch (const std::exception&) {
        // If conversion fails, store as string
        writer.row().set(column_index, value);
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
    std::cout << "  --no-zoh               Disable Zero-Order Hold compression (default: enabled)\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -h, --help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " data.csv\n";
    std::cout << "  " << program_name << " -d ';' data.csv output.bcsv\n";
    std::cout << "  " << program_name << " --no-header -v data.csv\n";
    std::cout << "  " << program_name << " --decimal-separator ',' german_data.csv\n";
    std::cout << "  " << program_name << " --no-zoh data.csv  # Disable ZoH compression\n";
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
        } else if (arg == "--no-zoh") {
            config.use_zoh = false;
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
            std::cout << "ZoH compression: " << (config.use_zoh ? "enabled" : "disabled") << std::endl;
        }
        
        // Check if input file exists
        if (!std::filesystem::exists(config.input_file)) {
            throw std::runtime_error("Input file does not exist: " + config.input_file);
        }
        
        // Get input file size for compression statistics
        auto input_file_size = std::filesystem::file_size(config.input_file);
        
        // Start timing the conversion process
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::ifstream input(config.input_file);
        if (!input.is_open()) {
            throw std::runtime_error("Cannot open input file: " + config.input_file);
        }

        std::string line;
        std::vector<std::string> headers;
        std::vector<bcsv::ColumnType> column_types;
        std::vector<std::vector<std::string>> sample_data;
        
        // Read first line for auto-detection
        if (!std::getline(input, line)) {
            throw std::runtime_error("Input file is empty");
        }
        
        // Trim carriage return (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
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
            // Trim carriage return (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
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
                std::cout << "  " << headers[i] << " -> " << column_types[i] << std::endl;
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
            // Trim carriage return (Windows line endings)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
        }
        
        // Create BCSV writer and convert data
        {
            bcsv::Writer<bcsv::Layout> writer(layout);
            // Use compression level 1 for better performance vs file size
            // Enable ZoH compression by default for optimal compression of time-series data
            bcsv::FileFlags flags = config.use_zoh ? bcsv::FileFlags::ZERO_ORDER_HOLD : bcsv::FileFlags::NONE;
            writer.open(config.output_file, true, 1, 64, flags);
            size_t row_count = 0;
            
            // Pre-calculate frequently used values outside the loop
            const size_t num_columns = headers.size();
            
            while (std::getline(input, line)) {
                // Trim carriage return (Windows line endings)
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                
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
            
            // Calculate conversion timing and statistics
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            auto output_file_size = std::filesystem::file_size(config.output_file);
            
            // Ensure minimum duration for throughput calculation
            long long duration_ms = duration.count();
            if (duration_ms == 0) duration_ms = 1;  // Minimum 1ms for calculation
            double duration_seconds = duration_ms / 1000.0;
            
            // Calculate compression ratio and throughput
            double compression_ratio = (static_cast<double>(input_file_size - output_file_size) / input_file_size) * 100.0;
            double throughput_mb_s = (static_cast<double>(input_file_size) / (1024.0 * 1024.0)) / duration_seconds;
            double rows_per_sec = static_cast<double>(row_count) / duration_seconds;
            
            // Display comprehensive conversion statistics
            std::cout << "\n=== Conversion Complete ==="<< std::endl;
            std::cout << "Successfully converted " << row_count << " rows to " << config.output_file << std::endl;
            std::cout << "Columns detected: " << headers.size() << std::endl;
            std::cout << layout << std::endl;
            std::cout << "Performance Statistics:" << std::endl;
            std::cout << "  Conversion time: " << duration.count() << " ms" << std::endl;
            std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << throughput_mb_s << " MB/s" << std::endl;
            std::cout << "  Rows/second: " << std::fixed << std::setprecision(0) << rows_per_sec << " rows/s" << std::endl;
            std::cout << "\nCompression Statistics:" << std::endl;
            std::cout << "  Input CSV size: " << input_file_size << " bytes (" << std::fixed << std::setprecision(2) << (input_file_size / 1024.0) << " KB)" << std::endl;
            std::cout << "  Output BCSV size: " << output_file_size << " bytes (" << std::fixed << std::setprecision(2) << (output_file_size / 1024.0) << " KB)" << std::endl;
            
            if (output_file_size <= input_file_size) {
                std::cout << "  Compression ratio: " << std::fixed << std::setprecision(1) << compression_ratio << "%" << std::endl;
                std::cout << "  Space saved: " << (input_file_size - output_file_size) << " bytes" << std::endl;
            } else {
                double size_increase_ratio = (static_cast<double>(output_file_size - input_file_size) / input_file_size) * 100.0;
                std::cout << "  File size increase: " << std::fixed << std::setprecision(1) << size_increase_ratio << "% (overhead from binary format and metadata)" << std::endl;
                std::cout << "  Additional space used: " << (output_file_size - input_file_size) << " bytes" << std::endl;
            }
            std::cout << "  Compression mode: " << (config.use_zoh ? "ZoH enabled" : "Standard") << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
