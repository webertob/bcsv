/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

/**
 * @file bcsvHead.cpp
 * @brief CLI tool to display the first few rows of a BCSV file in CSV format
 * 
 * This tool reads a BCSV file and prints the first N rows to console in CSV format,
 * including the header. Designed for quick inspection and piping to other tools.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <cstdint>
#include <bcsv/bcsv.h>

struct Config {
    std::string input_file;
    size_t num_rows = 10;        // Default: show first 10 rows
    char delimiter = ',';
    char quote_char = '"';
    bool quote_all = false;
    bool include_header = true;  // Include header by default (changed from original ToDo.txt spec)
    bool verbose = false;
    bool help = false;
    int float_precision = -1;    // -1 means auto-detect optimal precision
};

/**
 * @brief Escapes a string value for CSV format
 * @param value The string to escape
 * @param delimiter The field delimiter character
 * @param quote_char The quote character to use
 * @param force_quote Whether to force quoting even if not needed
 * @return Properly escaped CSV field
 */
std::string escapeCsvField(const std::string& value, char delimiter, char quote_char, bool force_quote = false) {
    bool needs_quoting = force_quote || 
                        value.find(delimiter) != std::string::npos ||
                        value.find(quote_char) != std::string::npos ||
                        value.find('\n') != std::string::npos ||
                        value.find('\r') != std::string::npos ||
                        (!value.empty() && (value.front() == ' ' || value.back() == ' '));

    if (!needs_quoting) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.length() + 10); // Reserve extra space for quotes and escapes
    escaped += quote_char;
    
    for (char c : value) {
        if (c == quote_char) {
            escaped += quote_char; // Double the quote character to escape it
            escaped += quote_char;
        } else {
            escaped += c;
        }
    }
    
    escaped += quote_char;
    return escaped;
}

/**
 * @brief Formats a numeric value with optimal precision
 * @param value The numeric value as string
 * @param precision Precision to use (-1 for auto)
 * @return Formatted numeric string
 */
std::string formatNumeric(const std::string& value, int precision) {
    if (precision == -1) {
        return value; // Use the default string representation
    }
    
    try {
        double num = std::stod(value);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << num;
        return oss.str();
    } catch (...) {
        return value; // Return original if conversion fails
    }
}

/**
 * @brief Gets a string representation of a cell value
 * @param row The row to read from
 * @param col_index The column index
 * @param column_type The type of the column
 * @param precision Floating point precision
 * @return String representation of the cell value
 */
std::string getCellValue(const bcsv::Row& row, size_t col_index, bcsv::ColumnType column_type, int precision) {
    try {
        switch (column_type) {
            case bcsv::ColumnType::BOOL:
                return row.get<bool>(col_index) ? "true" : "false";
            case bcsv::ColumnType::INT8:
                return std::to_string(row.get<int8_t>(col_index));
            case bcsv::ColumnType::UINT8:
                return std::to_string(row.get<uint8_t>(col_index));
            case bcsv::ColumnType::INT16:
                return std::to_string(row.get<int16_t>(col_index));
            case bcsv::ColumnType::UINT16:
                return std::to_string(row.get<uint16_t>(col_index));
            case bcsv::ColumnType::INT32:
                return std::to_string(row.get<int32_t>(col_index));
            case bcsv::ColumnType::UINT32:
                return std::to_string(row.get<uint32_t>(col_index));
            case bcsv::ColumnType::INT64:
                return std::to_string(row.get<int64_t>(col_index));
            case bcsv::ColumnType::UINT64:
                return std::to_string(row.get<uint64_t>(col_index));
            case bcsv::ColumnType::FLOAT:
                return formatNumeric(std::to_string(row.get<float>(col_index)), precision);
            case bcsv::ColumnType::DOUBLE:
                return formatNumeric(std::to_string(row.get<double>(col_index)), precision);
            case bcsv::ColumnType::STRING:
                return row.get<std::string>(col_index);
            default:
                return ""; // Unknown type
        }
    } catch (const std::exception&) {
        return ""; // Return empty string if value cannot be retrieved
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE\n\n";
    std::cout << "Display the first few rows of a BCSV file in CSV format.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input BCSV file path\n\n";
    std::cout << "Options:\n";
    std::cout << "  -n, --lines N           Number of rows to display (default: 10)\n";
    std::cout << "  -d, --delimiter CHAR    Field delimiter (default: ',')\n";
    std::cout << "  -q, --quote CHAR        Quote character (default: '\"')\n";
    std::cout << "  --quote-all             Quote all fields (not just those that need it)\n";
    std::cout << "  --no-header             Don't include header row in output\n";
    std::cout << "  -p, --precision N       Floating point precision (default: auto)\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  -h, --help              Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " data.bcsv\n";
    std::cout << "  " << program_name << " -n 20 data.bcsv\n";
    std::cout << "  " << program_name << " --no-header data.bcsv\n";
    std::cout << "  " << program_name << " -d ';' --quote-all data.bcsv\n";
    std::cout << "  " << program_name << " data.bcsv | grep \"pattern\"\n";
}

Config parseArgs(int argc, char* argv[]) {
    Config config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            config.help = true;
            return config;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--quote-all") {
            config.quote_all = true;
        } else if (arg == "--no-header") {
            config.include_header = false;
        } else if ((arg == "-n" || arg == "--lines") && i + 1 < argc) {
            try {
                int num = std::stoi(argv[++i]);
                if (num <= 0) {
                    std::cerr << "Error: Number of lines must be positive: " << num << std::endl;
                    exit(1);
                }
                config.num_rows = static_cast<size_t>(num);
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid number of lines: " << argv[i] << std::endl;
                exit(1);
            }
        } else if ((arg == "-d" || arg == "--delimiter") && i + 1 < argc) {
            std::string delim = argv[++i];
            if (delim.length() != 1) {
                std::cerr << "Error: Delimiter must be a single character: " << delim << std::endl;
                exit(1);
            }
            config.delimiter = delim[0];
        } else if ((arg == "-q" || arg == "--quote") && i + 1 < argc) {
            std::string quote = argv[++i];
            if (quote.length() != 1) {
                std::cerr << "Error: Quote character must be a single character: " << quote << std::endl;
                exit(1);
            }
            config.quote_char = quote[0];
        } else if ((arg == "-p" || arg == "--precision") && i + 1 < argc) {
            try {
                config.float_precision = std::stoi(argv[++i]);
                if (config.float_precision < 0) {
                    std::cerr << "Error: Precision must be non-negative: " << config.float_precision << std::endl;
                    exit(1);
                }
            } catch (const std::exception&) {
                std::cerr << "Error: Invalid precision: " << argv[i] << std::endl;
                exit(1);
            }
        } else if (arg.substr(0, 1) == "-") {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            exit(1);
        } else {
            if (config.input_file.empty()) {
                config.input_file = arg;
            } else {
                std::cerr << "Error: Too many arguments. Only one input file expected." << std::endl;
                exit(1);
            }
        }
    }
    
    if (config.input_file.empty() && !config.help) {
        std::cerr << "Error: Input file is required" << std::endl;
        exit(1);
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
            std::cerr << "Reading: " << config.input_file << std::endl;
            std::cerr << "Lines: " << config.num_rows << std::endl;
            std::cerr << "Include header: " << (config.include_header ? "yes" : "no") << std::endl;
            std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
            std::cerr << "Quote: '" << config.quote_char << "'" << std::endl;
            std::cerr << "Quote all: " << (config.quote_all ? "yes" : "no") << std::endl;
        }
        
        // Open BCSV file
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(config.input_file)) {
            std::cerr << "Error: Cannot open BCSV file: " << config.input_file << std::endl;
            return 1;
        }
        
        if (config.verbose) {
            std::cerr << "Opened BCSV file successfully" << std::endl;
        }
        
        const auto& layout = reader.layout();
        if (config.verbose) {
            std::cerr << "Layout contains " << layout.columnCount() << " columns" << std::endl;
        }
        
        // Print header row (if enabled)
        if (config.include_header) {
            bool first = true;
            for (size_t i = 0; i < layout.columnCount(); ++i) {
                if (!first) std::cout << config.delimiter;
                std::cout << escapeCsvField(layout.columnName(i), config.delimiter, config.quote_char, config.quote_all);
                first = false;
            }
            std::cout << std::endl;
        }
        
        // Print data rows
        size_t rows_printed = 0;
        while (reader.readNext() && rows_printed < config.num_rows) {
            const auto& row = reader.row();
            
            bool first = true;
            for (size_t i = 0; i < layout.columnCount(); ++i) {
                if (!first) std::cout << config.delimiter;
                
                std::string value = getCellValue(row, i, layout.columnType(i), config.float_precision);
                std::cout << escapeCsvField(value, config.delimiter, config.quote_char, config.quote_all);
                first = false;
            }
            std::cout << std::endl;
            rows_printed++;
        }
        
        reader.close();
        
        if (config.verbose) {
            std::cerr << "Successfully displayed " << rows_printed << " rows" << std::endl;
        }
        
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}