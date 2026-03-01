/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
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
#include <string>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <chrono>
#include <cstdint>
#include <vector>
#include <bcsv/bcsv.h>
#include "cli_common.h"

struct Config {
    std::string input_file;
    std::string output_file;
    char delimiter = ',';
    bool include_header = true;
    bool verbose = false;
    bool help = false;
    
    // Row range selection options
    int64_t first_row = -1;    // -1 = not specified (0-based indexing)
    int64_t last_row = -1;     // -1 = not specified (0-based indexing, inclusive)
    std::string slice;           // empty = not specified (Python-style slice notation)
    
    // Parsed slice components (internal use)
    int64_t slice_start = INT64_MIN;  // INT64_MIN = not specified
    int64_t slice_stop = INT64_MIN;   // INT64_MIN = not specified
    int64_t slice_step = 1;
    bool slice_parsed = false;

    // Benchmark mode
    bool benchmark = false;  // Print timing stats to stderr
    bool json_output = false;  // Emit JSON timing blob to stdout
};

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

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] INPUT_FILE [OUTPUT_FILE]\n\n";
    std::cout << "Convert BCSV file to CSV format.\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  INPUT_FILE     Input BCSV file path\n";
    std::cout << "  OUTPUT_FILE    Output CSV file path (default: INPUT_FILE.csv)\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output FILE       Output CSV file path (use '-' for stdout)\n";
    std::cout << "  -d, --delimiter CHAR    Field delimiter (default: ',')\n";
    std::cout << "  --no-header             Don't include header row in output\n";
    std::cout << "  --firstRow N            Start from row N (0-based, default: 0)\n";
    std::cout << "  --lastRow N             End at row N (0-based, inclusive, default: last)\n";
    std::cout << "  --slice SLICE           Python-style slice notation (overrides firstRow/lastRow)\n";
    std::cout << "  -v, --verbose           Enable verbose output\n";
    std::cout << "  --benchmark             Print timing stats (wall clock, rows/s, MB/s) to stderr\n";
    std::cout << "  --json                  With --benchmark: emit JSON timing blob to stdout\n";
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
    std::cout << "  " << program_name << " --no-header data.bcsv\n";
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
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                config.output_file = argv[++i];
            } else {
                throw std::runtime_error("Option " + arg + " requires an argument");
            }
        } else if (arg == "--no-header") {
            config.include_header = false;
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
        } else if (arg == "--benchmark") {
            config.benchmark = true;
        } else if (arg == "--json") {
            config.json_output = true;
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
            std::cerr << "Converting: " << config.input_file << " -> " << config.output_file << std::endl;
            std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
            std::cerr << "Header: " << (config.include_header ? "yes" : "no") << std::endl;
        }
        
        // Check if input file exists
        if (!std::filesystem::exists(config.input_file)) {
            throw std::runtime_error("Input file does not exist: " + config.input_file);
        }

        auto input_file_size = std::filesystem::file_size(config.input_file);
        auto bench_start = std::chrono::steady_clock::now();

        // Open BCSV file — try ReaderDirectAccess first (supports rowCount),
        // fall back to plain Reader for stream-mode files without a footer.
        bcsv::ReaderDirectAccess<bcsv::Layout> da_reader;
        bcsv::Reader<bcsv::Layout> seq_reader;
        bool has_direct_access = da_reader.open(config.input_file);

        // Obtain a reference to the reader we'll actually use for sequential I/O.
        // Both Reader and ReaderDirectAccess expose readNext() / row() / layout().
        auto& reader = has_direct_access
                            ? static_cast<bcsv::Reader<bcsv::Layout>&>(da_reader)
                            : seq_reader;

        if (!has_direct_access) {
            if (!seq_reader.open(config.input_file)) {
                throw std::runtime_error("Cannot open BCSV file: " + config.input_file);
            }
            if (config.verbose) {
                std::cerr << "Using sequential reader (stream-mode file)" << std::endl;
            }
        }
        
        const auto& layout = reader.layout();
        
        if (config.verbose) {
            std::cerr << "Opened BCSV file successfully" << std::endl;
            std::cerr << "Layout contains " << layout.columnCount() << " columns:" << std::endl;
            std::cerr << layout << std::endl;
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
            std::cerr << "Row range: start=" << effective_start 
                      << ", stop=" << (effective_stop == INT64_MAX ? "end" : std::to_string(effective_stop))
                      << ", step=" << effective_step << std::endl;
            if (has_negative_indices) {
                std::cerr << "Note: Negative indices will be resolved after reading file" << std::endl;
            }
        }
        
        // Create CsvWriter — to file or to stdout
        const bool to_stdout = (config.output_file == "-");
        bcsv::CsvWriter<bcsv::Layout> csv_writer(layout, config.delimiter);
        if (to_stdout) {
            if (!csv_writer.open(std::cout, config.include_header)) {
                throw std::runtime_error("Cannot open stdout for CSV output");
            }
        } else {
            if (!csv_writer.open(config.output_file, true, config.include_header)) {
                throw std::runtime_error("Cannot create output file: " + config.output_file +
                                         " (" + csv_writer.getErrorMsg() + ")");
            }
        }
        
        // Convert data rows with range support
        size_t total_rows_read = 0;
        size_t output_rows_written = 0;
        int64_t file_size = -1; // Will be determined if negative indexing is used
        
        // If we have negative indices, we need to get the file size to resolve them
        std::vector<bool> rows_to_output;
        if (has_negative_indices) {
            if (config.verbose) {
                std::cerr << "Counting rows to resolve negative indices..." << std::endl;
            }
            
            // Try to use the efficient countRows() function first
            try {
                if (!has_direct_access) {
                    throw std::runtime_error("no direct access available");
                }
                file_size = static_cast<int64_t>(da_reader.rowCount());
                if (file_size == 0) {
                    throw std::runtime_error("countRows() returned 0");
                }
                if (config.verbose) {
                    std::cerr << "Used countRows(): " << file_size << " rows" << std::endl;
                }
            } catch (const std::exception& e) {
                if (config.verbose) {
                    std::cerr << "countRows() failed (" << e.what() << "), falling back to manual counting..." << std::endl;
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
                    std::cerr << "Manual counting found: " << file_size << " rows" << std::endl;
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
                std::cerr << "File contains " << file_size << " rows" << std::endl;
                std::cerr << "Resolved range: [" << effective_start << ":" << effective_stop << ":" << effective_step << "]" << std::endl;
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
                // Copy row data from BCSV reader to CSV writer via visitConst
                reader.row().visitConst([&](size_t col, const auto& val) {
                    csv_writer.row().set(col, val);
                });
                csv_writer.writeRow();
                ++output_rows_written;
            }
            
            ++total_rows_read;
            
            // Early termination for efficiency (when not using negative indices or step > 1)
            if (!has_negative_indices && effective_step == 1 && static_cast<int64_t>(total_rows_read) >= effective_stop) {
                break;
            }
            
            if (config.verbose && (total_rows_read & 0x3FFF) == 0) {  // Every 16384 rows for better performance
                std::cerr << "Processed " << total_rows_read << " rows, output " << output_rows_written << " rows..." << std::endl;
            }
        }
        
        reader.close();
        csv_writer.close();

        auto bench_end = std::chrono::steady_clock::now();
        auto bench_dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start).count();
        if (bench_dur_ms == 0) bench_dur_ms = 1;
        double bench_sec = bench_dur_ms / 1000.0;
        uintmax_t output_file_size = to_stdout ? 0 : std::filesystem::file_size(config.output_file);
        double throughput_mb_s = (static_cast<double>(input_file_size) / (1024.0 * 1024.0)) / bench_sec;
        double rows_per_sec = static_cast<double>(output_rows_written) / bench_sec;
        
        if (!to_stdout) {
            std::cerr << "Successfully converted " << output_rows_written << " rows to " << config.output_file;
            if (total_rows_read != output_rows_written) {
                std::cerr << " (from " << total_rows_read << " total rows)";
            }
            std::cerr << std::endl;
        }
        
        if (config.verbose) {
            if (!to_stdout) {
                std::cerr << "Output file size: " << output_file_size << " bytes" << std::endl;
            }
            std::cerr << "Rows written: " << output_rows_written << std::endl;
        }

        // --benchmark: print timing stats to stderr
        if (config.benchmark) {
            if (config.json_output) {
                // JSON blob — always to stderr to avoid corrupting CSV on stdout
                std::cerr << "{\"tool\":\"bcsv2csv\""
                          << ",\"input_file\":\"" << config.input_file << "\""
                          << ",\"output_file\":\"" << config.output_file << "\""
                          << ",\"rows\":" << output_rows_written
                          << ",\"columns\":" << layout.columnCount()
                          << ",\"input_bytes\":" << input_file_size
                          << ",\"output_bytes\":" << output_file_size
                          << ",\"wall_ms\":" << bench_dur_ms
                          << ",\"throughput_mb_s\":" << std::fixed << std::setprecision(2) << throughput_mb_s
                          << ",\"rows_per_sec\":" << std::fixed << std::setprecision(0) << rows_per_sec
                          << "}" << std::endl;
            } else {
                std::cerr << "[benchmark] bcsv2csv: "
                          << output_rows_written << " rows, "
                          << bench_dur_ms << " ms, "
                          << std::fixed << std::setprecision(2) << throughput_mb_s << " MB/s, "
                          << std::fixed << std::setprecision(0) << rows_per_sec << " rows/s\n";
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
