/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 *
 * DEPRECATED: This benchmark is superseded by the modular benchmark suite.
 * Use bench_macro_datasets with --size=L instead.
 * Build with -DBCSV_ENABLE_LEGACY_BENCHMARKS=ON to compile this file.
 * Will be removed in a future version.
 */

#include <cstddef>
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <bcsv/bcsv.h>

/**
 * Large Scale BCSV Performance Benchmark
 * 
 * This benchmark tests performance with:
 * - 500,000 rows of data
 * - 6 columns per data type (48 columns total)
 * - Comprehensive comparison: CSV vs BCSV, Flexible vs Static
 * - File size analysis
 * - Read/Write performance breakdown
 */

/**
 * TestDataGenerator - Single source of truth for deterministic test data
 * 
 * Generates the exact same data for a given (row, col, type) triplet.
 * This ensures write and read validation use identical data without
 * storing anything in memory.
 */
class TestDataGenerator {
private:
    // Type-specific hash functions optimized for each data type
    static constexpr uint8_t hashBool(size_t row, size_t col) {
        return static_cast<uint8_t>((row * 7919ULL + col * 6947ULL) & 1);
    }
    
    static constexpr int8_t hashInt8(size_t row, size_t col) {
        return static_cast<int8_t>(row * 2654435761ULL + col * 1597334677ULL);
    }
    
    static constexpr int16_t hashInt16(size_t row, size_t col) {
        return static_cast<int16_t>(row * 1000003ULL + col * 7919ULL);
    }
    
    static constexpr int32_t hashInt32(size_t row, size_t col) {
        return static_cast<int32_t>(row * 2654435761ULL + col * 1597334677ULL);
    }
    
    static constexpr int64_t hashInt64(size_t row, size_t col) {
        return static_cast<int64_t>((row * 6364136223846793005ULL) ^ (col * 1442695040888963407ULL));
    }
    
    static constexpr uint8_t hashUInt8(size_t row, size_t col) {
        return static_cast<uint8_t>(row * 7919ULL + col * 6947ULL);
    }
    
    static constexpr uint16_t hashUInt16(size_t row, size_t col) {
        return static_cast<uint16_t>(row * 48271ULL + col * 22695477ULL);
    }
    
    static constexpr uint32_t hashUInt32(size_t row, size_t col) {
        return static_cast<uint32_t>(row * 1597334677ULL + col * 2654435761ULL);
    }
    
    static constexpr uint64_t hashUInt64(size_t row, size_t col) {
        return (row * 11400714819323198485ULL) ^ (col * 14029467366897019727ULL);
    }
    
    static constexpr float hashFloat(size_t row, size_t col) {
        uint32_t h = static_cast<uint32_t>(row * 1597334677ULL + col * 2654435761ULL);
        return static_cast<float>(static_cast<int32_t>(h % 2000000U) - 1000000) / 1000.0f;
    }
    
    static constexpr double hashDouble(size_t row, size_t col) {
        uint64_t h = (row * 6364136223846793005ULL) ^ (col * 1442695040888963407ULL);
        return static_cast<double>(static_cast<int64_t>(h % 20000000ULL) - 10000000) / 1000.0;
    }

public:
    // Generate random-like deterministic values
    template<typename T>
    void getRandom(size_t row, size_t col, T&& value) const {
        using V = std::decay_t<T>;
        if constexpr (std::is_same_v<V, bool>) {
            value = hashBool(row, col) != 0;
        } else if constexpr (std::is_same_v<V, int8_t>) {
            value = hashInt8(row, col);
        } else if constexpr (std::is_same_v<V, int16_t>) {
            value = hashInt16(row, col);
        } else if constexpr (std::is_same_v<V, int32_t>) {
            value = hashInt32(row, col);
        } else if constexpr (std::is_same_v<V, int64_t>) {
            value = hashInt64(row, col);
        } else if constexpr (std::is_same_v<V, uint8_t>) {
            value = hashUInt8(row, col);
        } else if constexpr (std::is_same_v<V, uint16_t>) {
            value = hashUInt16(row, col);
        } else if constexpr (std::is_same_v<V, uint32_t>) {
            value = hashUInt32(row, col);
        } else if constexpr (std::is_same_v<V, uint64_t>) {
            value = hashUInt64(row, col);
        } else if constexpr (std::is_same_v<V, float>) {
            value = hashFloat(row, col);
        } else if constexpr (std::is_same_v<V, double>) {
            value = hashDouble(row, col);
        } else if constexpr (std::is_same_v<V, std::string>) {
            // String size round-robin through 5 sizes: 9, 48, 512, 4096, 128
            // col % 5 determines size category (branchless)
            constexpr size_t sizes[5] = {9, 48, 512, 4096, 128};
            size_t sizeCategory = col % 5;
            size_t maxLen = sizes[sizeCategory];
            
            uint64_t h = hashUInt64(row, col);
            size_t len = (h % maxLen) + 1;
            
            // Efficient string generation: resize once, fill directly
            value.resize(len);
            char baseChar = 'A' + static_cast<char>(h % 26);
            for (size_t i = 0; i < len; ++i) {
                value[i] = static_cast<char>(baseChar + (i % 26));
            }
        }
    }

    // Generate time-series data with temporal correlation
    // Value parameter serves as storage for previous value
    template<typename T>
    void getTimeSeries(size_t row, size_t col, T&& value) const {
        using V = std::decay_t<T>;
        // Change interval: values change every 100 rows
        constexpr size_t changeInterval = 100;
        size_t segment = row / changeInterval;
        
        if constexpr (std::is_same_v<V, bool>) {
            value = ((segment + col) % 3) == 0;
        } else if constexpr (std::is_same_v<V, int8_t>) {
            value = static_cast<int8_t>((segment % 50) + col * 10);
        } else if constexpr (std::is_same_v<V, int16_t>) {
            value = static_cast<int16_t>((segment % 1000) + col * 100);
        } else if constexpr (std::is_same_v<V, int32_t>) {
            value = static_cast<int32_t>(segment * 10 + col * 1000);
        } else if constexpr (std::is_same_v<V, int64_t>) {
            value = static_cast<int64_t>(1640995200000LL + segment * 60000 + col * 1000);
        } else if constexpr (std::is_same_v<V, uint8_t>) {
            value = static_cast<uint8_t>((segment + col * 20) % 200);
        } else if constexpr (std::is_same_v<V, uint16_t>) {
            value = static_cast<uint16_t>((segment % 10000) + col * 5000);
        } else if constexpr (std::is_same_v<V, uint32_t>) {
            value = static_cast<uint32_t>(segment * 100 + col * 10000);
        } else if constexpr (std::is_same_v<V, uint64_t>) {
            value = static_cast<uint64_t>(segment * 1000000ULL + col * 1000000000ULL);
        } else if constexpr (std::is_same_v<V, float>) {
            value = static_cast<float>(50.0f + (segment % 100) * 0.5f + col * 10.0f);
        } else if constexpr (std::is_same_v<V, double>) {
            value = static_cast<double>(100.0 + (segment % 500) * 0.1 + col * 25.0);
        } else if constexpr (std::is_same_v<V, std::string>) {
            // Repeated string categories for ZoH compression
            const char* categories[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta"};
            value = categories[(segment / 5 + col) % 6];
        }
    }
    
    // Backwards compatibility alias
    template<typename T>
    void get(size_t row, size_t col, T& value) const {
        getRandom(row, col, value);
    }
};

class LargeScaleBenchmark {
private:
    static constexpr size_t NUM_ROWS = 500000;
    static constexpr size_t COLUMNS_PER_TYPE = 6;
    static constexpr const char* CSV_FILENAME = "large_test.csv";
    static constexpr const char* BCSV_FLEXIBLE_FILENAME = "large_flexible.bcsv";
    static constexpr const char* BCSV_STATIC_FILENAME = "large_static.bcsv";
    static constexpr const char* BCSV_FLEXIBLE_ZOH_FILENAME = "large_flexible_zoh.bcsv";
    static constexpr const char* BCSV_STATIC_ZOH_FILENAME = "large_static_zoh.bcsv";
    
    TestDataGenerator dataGen_;
    
    // Optimization prevention helper
    template<typename T>
    void prevent_optimization(const volatile T& value) {
        volatile T temp;
        temp = const_cast<const T&>(value);
        (void)temp;
    }
    
    // Specialization for string types that can't handle volatile assignment
    void prevent_optimization(const volatile std::string& value) {
        volatile const void* ptr = &value;
        (void)ptr;
    }
    
    // Validate that a file exists, is accessible, and has non-zero size
    // Returns file size in bytes
    size_t validateFile(const std::string& filepath) {
        if (!std::filesystem::exists(filepath)) {
            throw std::runtime_error("File does not exist: " + filepath);
        }
        
        if (!std::filesystem::is_regular_file(filepath)) {
            throw std::runtime_error("Path is not a regular file: " + filepath);
        }
        
        size_t fileSize = std::filesystem::file_size(filepath);
        if (fileSize == 0) {
            throw std::runtime_error("File has zero size: " + filepath);
        }         
        return fileSize;
    }

public:
    // Static layout with 6 columns per type (48 columns total)
    using LargeTestLayoutStatic = bcsv::LayoutStatic<
        // 6 bool columns
        bool, bool, bool, bool, bool, bool,
        // 6 int8_t columns  
        int8_t, int8_t, int8_t, int8_t, int8_t, int8_t,
        // 6 int16_t columns
        int16_t, int16_t, int16_t, int16_t, int16_t, int16_t,
        // 6 int32_t columns
        int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
        // 6 int64_t columns
        int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
        // 6 uint8_t columns
        uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t,
        // 6 uint16_t columns
        uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
        // 6 uint32_t columns
        uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
        // 6 uint64_t columns
        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
        // 6 float columns
        float, float, float, float, float, float,
        // 6 double columns
        double, double, double, double, double, double,
        // 6 string columns
        std::string, std::string, std::string, std::string, std::string, std::string
    >;

    LargeScaleBenchmark() {
        std::cout << "Large Scale BCSV Performance Benchmark\n";
        std::cout << "=====================================\n";
        std::cout << "Test Configuration:\n";
        std::cout << "  Rows: " << NUM_ROWS << "\n";
        std::cout << "  Columns: " << (COLUMNS_PER_TYPE * 12) << " (6 per data type)\n";
        std::cout << "  Data types: BOOL(6), INT8(6), INT16(6), INT32(6), INT64(6), UINT8(6), UINT16(6), UINT32(6), UINT64(6), FLOAT(6), DOUBLE(6), STRING(6)\n";
        std::cout << "  Data generation: Deterministic (TestDataGenerator)\n";
        std::cout << "  Compression: LZ4 Level 1\n";
        std::cout << "  Platform: " << sizeof(void*) * 8 << "-bit\n\n";
    }

    // Create flexible layout
    bcsv::Layout createFlexibleLayout() {
        bcsv::Layout layout;
        
        // Add columns for each type (6 per type)
        const std::vector<std::string> typeNames = {"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float", "double", "string"};
        const std::vector<bcsv::ColumnType> types = {
            bcsv::ColumnType::BOOL, bcsv::ColumnType::INT8, bcsv::ColumnType::INT16, bcsv::ColumnType::INT32, 
            bcsv::ColumnType::INT64, bcsv::ColumnType::UINT8, bcsv::ColumnType::UINT16, bcsv::ColumnType::UINT32, 
            bcsv::ColumnType::UINT64, bcsv::ColumnType::FLOAT, bcsv::ColumnType::DOUBLE, bcsv::ColumnType::STRING
        };
        
        for (size_t typeIdx = 0; typeIdx < types.size(); ++typeIdx) {
            for (size_t colIdx = 0; colIdx < 6; ++colIdx) {
                std::string colName = typeNames[typeIdx] + "_" + std::to_string(colIdx);
                layout.addColumn({colName, types[typeIdx]});
            }
        }
        
        return layout;
    }

    // Create static layout
    LargeTestLayoutStatic createStaticLayout() {
        std::array<std::string, 72> columnNames;
        const std::vector<std::string> typeNames = {"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float", "double", "string"};
        
        size_t idx = 0;
        for (size_t typeIdx = 0; typeIdx < typeNames.size(); ++typeIdx) {
            for (size_t colIdx = 0; colIdx < 6; ++colIdx) {
                columnNames[idx++] = typeNames[typeIdx] + "_" + std::to_string(colIdx);
            }
        }
        
        return LargeTestLayoutStatic(columnNames);
    }

    // Helper to populate flexible row directly from TestDataGenerator
    void populateFlexibleRow(bcsv::Writer<bcsv::Layout>& writer, size_t rowIndex) {
        auto& row = writer.row();
        size_t colIdx = 0;
        
        // Generate and set all columns directly
        for (size_t i = 0; i < 6; ++i) { bool v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { int8_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { int16_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { int32_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { int64_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { uint8_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { uint16_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { uint32_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { uint64_t v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { float v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { double v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
        for (size_t i = 0; i < 6; ++i) { std::string v; dataGen_.getRandom(rowIndex, colIdx, v); row.set(colIdx++, v); }
    }

    // Helper to populate static row directly from TestDataGenerator  
    template<typename WriterType, size_t... Is>
    void populateStaticRowImpl(WriterType& writer, size_t rowIndex, std::index_sequence<Is...>) {
        auto& row = writer.row();
        // Generate values on the fly based on column index
        (populateStaticColumn<Is>(row, rowIndex), ...);
    }
    
    template<size_t ColIdx>
    void populateStaticColumn(auto& row, size_t rowIndex) {
        if constexpr (ColIdx < 6) { // bool columns 0-5
            bool v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 12) { // int8 columns 6-11
            int8_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 18) { // int16 columns 12-17
            int16_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 24) { // int32 columns 18-23
            int32_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 30) { // int64 columns 24-29
            int64_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 36) { // uint8 columns 30-35
            uint8_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 42) { // uint16 columns 36-41
            uint16_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 48) { // uint32 columns 42-47
            uint32_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 54) { // uint64 columns 48-53
            uint64_t v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 60) { // float columns 54-59
            float v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 66) { // double columns 60-65
            double v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else { // string columns 66-71
            std::string v; dataGen_.getRandom(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        }
    }
    
    template<typename WriterType>
    void populateStaticRow(WriterType& writer, size_t rowIndex) {
        populateStaticRowImpl(writer, rowIndex, std::make_index_sequence<72>{});
    }
    
    // Helper to populate static row with time-series data for ZoH optimization
    template<typename WriterType, size_t... Is>
    void populateStaticRowZoHImpl(WriterType& writer, size_t rowIndex, std::index_sequence<Is...>) {
        auto& row = writer.row();
        // Generate values on the fly based on column index
        (populateStaticColumnZoH<Is>(row, rowIndex), ...);
    }
    
    template<size_t ColIdx>
    void populateStaticColumnZoH(auto& row, size_t rowIndex) {
        if constexpr (ColIdx < 6) { // bool columns 0-5
            bool v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 12) { // int8 columns 6-11
            int8_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 18) { // int16 columns 12-17
            int16_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 24) { // int32 columns 18-23
            int32_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 30) { // int64 columns 24-29
            int64_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 36) { // uint8 columns 30-35
            uint8_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 42) { // uint16 columns 36-41
            uint16_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 48) { // uint32 columns 42-47
            uint32_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 54) { // uint64 columns 48-53
            uint64_t v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 60) { // float columns 54-59
            float v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else if constexpr (ColIdx < 66) { // double columns 60-65
            double v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        } else { // string columns 66-71
            std::string v; dataGen_.getTimeSeries(rowIndex, ColIdx, v); row.template set<ColIdx>(v);
        }
    }
    
    template<typename WriterType>
    void populateStaticRowZoH(WriterType& writer, size_t rowIndex) {
        populateStaticRowZoHImpl(writer, rowIndex, std::make_index_sequence<72>{});
    }

    // CSV benchmark
    std::pair<double, double> benchmarkCSV() {
        std::cout << "Benchmarking CSV format...\n";
        
        // Write CSV
        auto writeStart = std::chrono::steady_clock::now();
        {
            std::ofstream csv(CSV_FILENAME);
            
            // Write header
            const std::vector<std::string> typeNames = {"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float", "double", "string"};
            for (size_t typeIdx = 0; typeIdx < typeNames.size(); ++typeIdx) {
                for (size_t colIdx = 0; colIdx < 6; ++colIdx) {
                    if (typeIdx > 0 || colIdx > 0) csv << ",";
                    csv << typeNames[typeIdx] << "_" << colIdx;
                }
            }
            csv << "\n";
            
            // Write data
            for (size_t row = 0; row < NUM_ROWS; ++row) {
                bool first = true;
                auto writeValue = [&](const auto& val) {
                    if (!first) csv << ",";
                    first = false;
                    csv << val;
                };
                
                // Generate values on-the-fly and write directly
                for (size_t i = 0; i < 6; ++i) { bool v; dataGen_.getRandom(row, i, v); writeValue(v ? "true" : "false"); }
                for (size_t i = 0; i < 6; ++i) { int8_t v; dataGen_.getRandom(row, i + 6, v); writeValue(static_cast<int>(v)); }
                for (size_t i = 0; i < 6; ++i) { int16_t v; dataGen_.getRandom(row, i + 12, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { int32_t v; dataGen_.getRandom(row, i + 18, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { int64_t v; dataGen_.getRandom(row, i + 24, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { uint8_t v; dataGen_.getRandom(row, i + 30, v); writeValue(static_cast<int>(v)); }
                for (size_t i = 0; i < 6; ++i) { uint16_t v; dataGen_.getRandom(row, i + 36, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { uint32_t v; dataGen_.getRandom(row, i + 42, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { uint64_t v; dataGen_.getRandom(row, i + 48, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { float v; dataGen_.getRandom(row, i + 54, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) { double v; dataGen_.getRandom(row, i + 60, v); writeValue(v); }
                for (size_t i = 0; i < 6; ++i) {
                    std::string v; dataGen_.getRandom(row, i + 66, v);
                    csv << ",\"" << v << "\"";
                }
                csv << "\n";
                
                if (row % 50000 == 0) {
                    std::cout << "  CSV Progress: " << row << "/" << NUM_ROWS << " rows written\n";
                }
            }
        }
        auto writeEnd = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
        
        // Read CSV
        auto readStart = std::chrono::steady_clock::now();
        {
            std::ifstream csv(CSV_FILENAME);
            std::string line;
            std::getline(csv, line); // Skip header
            
            size_t rowCount = 0;
            while (std::getline(csv, line)) {
                // Simple parsing simulation
                std::stringstream ss(line);
                std::string cell;
                size_t colCount = 0;
                
                while (std::getline(ss, cell, ',') && colCount < 72) {
                    // Simulate type conversion overhead
                    volatile int dummy = static_cast<int>(cell.length()); (void)dummy;
                    ++colCount;
                }
                ++rowCount;
                
                if (rowCount % 50000 == 0) {
                    std::cout << "  CSV Progress: " << rowCount << "/" << NUM_ROWS << " rows read\n";
                }
            }
        }
        auto readEnd = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();
        
        std::cout << "  CSV Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  CSV Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        
        return {writeTime, readTime};
    }

    // Write BCSV Flexible file
    void writeBCSVFlexible(const std::string& filepath, const bcsv::Layout& layout, size_t numberOfRows) {
        bcsv::Writer<bcsv::Layout> writer(layout);
        if (!writer.open(filepath, true, 1)) { // Compression level 1, no ZoH flag
            throw std::runtime_error("Failed to open file for writing: " + filepath + " - " + writer.getErrorMsg());
        }
        
        // Temporary variables to hold generated data
        bcsv::Row testData(layout);

        const size_t colCount = layout.columnCount();
        for (size_t i = 0; i < numberOfRows; ++i) {
            auto& row = writer.row();
            for(size_t k = 0; k < colCount; ++k) {
                // Generate random data
                switch(layout.columnType(k)) {
                    case bcsv::ColumnType::BOOL: {
                        bool tmp; dataGen_.getRandom(i, k, tmp);
                        testData.set(k, tmp);
                        row.set(k, testData.get<bool>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT8: {
                        dataGen_.getRandom(i, k, testData.ref<int8_t>(k));
                        row.set(k, testData.get<int8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT16: {
                        dataGen_.getRandom(i, k, testData.ref<int16_t>(k));
                        row.set(k, testData.get<int16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT32: {
                        dataGen_.getRandom(i, k, testData.ref<int32_t>(k));
                        row.set(k, testData.get<int32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT64: {
                        dataGen_.getRandom(i, k, testData.ref<int64_t>(k));
                        row.set(k, testData.get<int64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT8: {
                        dataGen_.getRandom(i, k, testData.ref<uint8_t>(k));
                        row.set(k, testData.get<uint8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT16: {
                        dataGen_.getRandom(i, k, testData.ref<uint16_t>(k));
                        row.set(k, testData.get<uint16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT32: {
                        dataGen_.getRandom(i, k, testData.ref<uint32_t>(k));
                        row.set(k, testData.get<uint32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT64: {
                        dataGen_.getRandom(i, k, testData.ref<uint64_t>(k));
                        row.set(k, testData.get<uint64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::FLOAT: {
                        dataGen_.getRandom(i, k, testData.ref<float>(k));
                        row.set(k, testData.get<float>(k));
                        break;
                    }
                    case bcsv::ColumnType::DOUBLE: {
                        dataGen_.getRandom(i, k, testData.ref<double>(k));
                        row.set(k, testData.get<double>(k));
                        break;
                    }
                    case bcsv::ColumnType::STRING: {
                        dataGen_.getRandom(i, k, testData.ref<std::string>(k));
                        row.set(k, testData.get<std::string>(k));
                        break;
                    }
                    default:
                        throw std::runtime_error("Unknown column type encountered.");
                }
            }
            writer.writeRow();
            if (i % 50000 == 0) {
                std::cout << "  BCSV Flexible Progress: " << i << "/" << numberOfRows << " rows written\n";
            }
        }
        writer.close();
    }
    
    // Read BCSV Flexible file and return number of rows read
    size_t readBCSVFlexible(const std::string& filepath, const bcsv::Layout& layoutExpected) {
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(filepath)) {
            throw std::runtime_error("Failed to open file for reading: " + filepath + " - " + reader.getErrorMsg());
        }
        
        if(!reader.layout().isCompatible(layoutExpected)) {
            throw std::runtime_error("Layout mismatch when reading BCSV Flexible file.");
        }

        // Temporary row to hold data for comparison avoids reallocations
        bcsv::Row tempRow(reader.layout());

        size_t i = 0;
        const size_t colCount = reader.layout().columnCount();
        while (reader.readNext()) {
            const auto& row = reader.row();

            // Access all columns and validate
            for (size_t k = 0; k < colCount; ++k) {
                bool success;
                switch(reader.layout().columnType(k)) {
                    case bcsv::ColumnType::BOOL: {
                        bool tmp; dataGen_.getRandom(i, k, tmp);
                        tempRow.set(k, tmp);
                        const bool& val_read = row.get<bool>(k);
                        success = (val_read == tempRow.get<bool>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT8: {
                        dataGen_.getRandom(i, k, tempRow.ref<int8_t>(k));
                        const int8_t& val_read = row.get<int8_t>(k);
                        success = (val_read == tempRow.get<int8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT16: {
                        dataGen_.getRandom(i, k, tempRow.ref<int16_t>(k));
                        const int16_t& val_read = row.get<int16_t>(k);
                        success = (val_read == tempRow.get<int16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT32: {
                        dataGen_.getRandom(i, k, tempRow.ref<int32_t>(k));
                        const int32_t& val_read = row.get<int32_t>(k);
                        success = (val_read == tempRow.get<int32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT64: {
                        dataGen_.getRandom(i, k, tempRow.ref<int64_t>(k));
                        const int64_t& val_read = row.get<int64_t>(k);
                        success = (val_read == tempRow.get<int64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT8: {
                        dataGen_.getRandom(i, k, tempRow.ref<uint8_t>(k));
                        const uint8_t& val_read = row.get<uint8_t>(k);
                        success = (val_read == tempRow.get<uint8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT16: {
                        dataGen_.getRandom(i, k, tempRow.ref<uint16_t>(k));
                        const uint16_t& val_read = row.get<uint16_t>(k);
                        success = (val_read == tempRow.get<uint16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT32: {
                        dataGen_.getRandom(i, k, tempRow.ref<uint32_t>(k));
                        const uint32_t& val_read = row.get<uint32_t>(k);
                        success = (val_read == tempRow.get<uint32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT64: {
                        dataGen_.getRandom(i, k, tempRow.ref<uint64_t>(k));
                        const uint64_t& val_read = row.get<uint64_t>(k);
                        success = (val_read == tempRow.get<uint64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::FLOAT: {
                        dataGen_.getRandom(i, k, tempRow.ref<float>(k));
                        const float& val_read = row.get<float>(k);
                        success = (val_read == tempRow.get<float>(k));
                        break;
                    }
                    case bcsv::ColumnType::DOUBLE: {
                        dataGen_.getRandom(i, k, tempRow.ref<double>(k));
                        const double& val_read = row.get<double>(k);
                        success = (val_read == tempRow.get<double>(k));
                        break;
                    }
                    case bcsv::ColumnType::STRING: {
                        dataGen_.getRandom(i, k, tempRow.ref<std::string>(k));
                        const std::string& val_read = row.get<std::string>(k);
                        success = (val_read == tempRow.get<std::string>(k));
                        break;
                    }
                    default:
                        throw std::runtime_error("Unknown column type encountered.");
                }
                if(!success) {
                    throw std::runtime_error("Data mismatch at row " + std::to_string(i) + ", column " + std::to_string(k));
                }
            }
            ++i;
            
            if (i % 50000 == 0) {
                std::cout << "  BCSV Flexible Progress: " << i << "/" << NUM_ROWS << " rows read\n";
            }
        }
        reader.close();
        return i;
    }

    // BCSV Flexible benchmark
    std::pair<double, double> benchmarkBCSVFlexible() {
        std::cout << "Benchmarking BCSV Flexible interface...\n";
        
        auto layout = createFlexibleLayout();
        
        // Write BCSV Flexible
        std::chrono::steady_clock::time_point t_start, t_end;
        t_start = std::chrono::steady_clock::now();
        writeBCSVFlexible(BCSV_FLEXIBLE_FILENAME, layout, NUM_ROWS);
        t_end = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate file was written successfully
        size_t fileSize = validateFile(BCSV_FLEXIBLE_FILENAME);

        
        // Read BCSV Flexible
        t_start = std::chrono::steady_clock::now();
        size_t rowsRead = readBCSVFlexible(BCSV_FLEXIBLE_FILENAME, layout);
        t_end = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate row count matches
        if (rowsRead != NUM_ROWS) {
            throw std::runtime_error("Row count mismatch: expected " + std::to_string(NUM_ROWS) + 
                                   " but read " + std::to_string(rowsRead));
        }
        
        std::cout << "  BCSV Flexible Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  BCSV Flexible Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        std::cout << "  BCSV Flexible File size:  " << fileSize << " bytes (" << std::fixed << std::setprecision(2) << (fileSize / 1024.0 / 1024.0) << " MB)\n";
        return {writeTime, readTime};
    }

    // Write BCSV Static file
    void writeBCSVStatic(const std::string& filepath, size_t numberOfRows) {
        // For static layouts, create layout with column names directly
        std::array<std::string, 72> columnNames;
        const std::vector<std::string> typeNames = {"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float", "double", "string"};
        
        size_t idx = 0;
        for (size_t typeIdx = 0; typeIdx < typeNames.size(); ++typeIdx) {
            for (size_t colIdx = 0; colIdx < 6; ++colIdx) {
                columnNames[idx++] = typeNames[typeIdx] + "_" + std::to_string(colIdx);
            }
        }
        
        LargeTestLayoutStatic layout(columnNames);
        bcsv::Writer<LargeTestLayoutStatic> writer(layout);
        if (!writer.open(filepath, true, 1)) { // Compression level 1, no ZoH flag
            throw std::runtime_error("Failed to open file for writing: " + filepath + " - " + writer.getErrorMsg());
        }
        
        for (size_t i = 0; i < numberOfRows; ++i) {
            populateStaticRow(writer, i);
            writer.writeRow();
            
            if (i % 50000 == 0) {
                std::cout << "  BCSV Static Progress: " << i << "/" << numberOfRows << " rows written\n";
            }
        }
        writer.close();
    }
    
    // Read BCSV Static file and return number of rows read with validation
    size_t readBCSVStatic(const std::string& filepath, const LargeTestLayoutStatic& /*layoutExpected*/) {
        bcsv::Reader<LargeTestLayoutStatic> reader;
        // For static layouts, reader automatically uses the compile-time layout
        // No need to pass layout - it constructs from the file header
        if (!reader.open(filepath)) {
            throw std::runtime_error("Failed to open file for reading: " + filepath + " - " + reader.getErrorMsg());
        }

        size_t i = 0;
        while (reader.readNext()) {
            const auto& row = reader.row();

            // Validate all 72 columns using runtime loop
            for (size_t k = 0; k < 72; ++k) {
                bool success = false;
                
                // Use compile-time column access for validation
                if (k < 6) { // bool columns 0-5
                    bool expected; dataGen_.getRandom(i, k, expected);
                    bool actual = (k == 0) ? row.get<0>() : (k == 1) ? row.get<1>() : (k == 2) ? row.get<2>() : (k == 3) ? row.get<3>() : (k == 4) ? row.get<4>() : row.get<5>();
                    success = (actual == expected);
                } else if (k < 12) { // int8 columns 6-11
                    int8_t expected; dataGen_.getRandom(i, k, expected);
                    int8_t actual = (k == 6) ? row.get<6>() : (k == 7) ? row.get<7>() : (k == 8) ? row.get<8>() : (k == 9) ? row.get<9>() : (k == 10) ? row.get<10>() : row.get<11>();
                    success = (actual == expected);
                } else if (k < 18) { // int16 columns 12-17
                    int16_t expected; dataGen_.getRandom(i, k, expected);
                    int16_t actual = (k == 12) ? row.get<12>() : (k == 13) ? row.get<13>() : (k == 14) ? row.get<14>() : (k == 15) ? row.get<15>() : (k == 16) ? row.get<16>() : row.get<17>();
                    success = (actual == expected);
                } else if (k < 24) { // int32 columns 18-23
                    int32_t expected; dataGen_.getRandom(i, k, expected);
                    int32_t actual = (k == 18) ? row.get<18>() : (k == 19) ? row.get<19>() : (k == 20) ? row.get<20>() : (k == 21) ? row.get<21>() : (k == 22) ? row.get<22>() : row.get<23>();
                    success = (actual == expected);
                } else if (k < 30) { // int64 columns 24-29
                    int64_t expected; dataGen_.getRandom(i, k, expected);
                    int64_t actual = (k == 24) ? row.get<24>() : (k == 25) ? row.get<25>() : (k == 26) ? row.get<26>() : (k == 27) ? row.get<27>() : (k == 28) ? row.get<28>() : row.get<29>();
                    success = (actual == expected);
                } else if (k < 36) { // uint8 columns 30-35
                    uint8_t expected; dataGen_.getRandom(i, k, expected);
                    uint8_t actual = (k == 30) ? row.get<30>() : (k == 31) ? row.get<31>() : (k == 32) ? row.get<32>() : (k == 33) ? row.get<33>() : (k == 34) ? row.get<34>() : row.get<35>();
                    success = (actual == expected);
                } else if (k < 42) { // uint16 columns 36-41
                    uint16_t expected; dataGen_.getRandom(i, k, expected);
                    uint16_t actual = (k == 36) ? row.get<36>() : (k == 37) ? row.get<37>() : (k == 38) ? row.get<38>() : (k == 39) ? row.get<39>() : (k == 40) ? row.get<40>() : row.get<41>();
                    success = (actual == expected);
                } else if (k < 48) { // uint32 columns 42-47
                    uint32_t expected; dataGen_.getRandom(i, k, expected);
                    uint32_t actual = (k == 42) ? row.get<42>() : (k == 43) ? row.get<43>() : (k == 44) ? row.get<44>() : (k == 45) ? row.get<45>() : (k == 46) ? row.get<46>() : row.get<47>();
                    success = (actual == expected);
                } else if (k < 54) { // uint64 columns 48-53
                    uint64_t expected; dataGen_.getRandom(i, k, expected);
                    uint64_t actual = (k == 48) ? row.get<48>() : (k == 49) ? row.get<49>() : (k == 50) ? row.get<50>() : (k == 51) ? row.get<51>() : (k == 52) ? row.get<52>() : row.get<53>();
                    success = (actual == expected);
                } else if (k < 60) { // float columns 54-59
                    float expected; dataGen_.getRandom(i, k, expected);
                    float actual = (k == 54) ? row.get<54>() : (k == 55) ? row.get<55>() : (k == 56) ? row.get<56>() : (k == 57) ? row.get<57>() : (k == 58) ? row.get<58>() : row.get<59>();
                    success = (actual == expected);
                } else if (k < 66) { // double columns 60-65
                    double expected; dataGen_.getRandom(i, k, expected);
                    double actual = (k == 60) ? row.get<60>() : (k == 61) ? row.get<61>() : (k == 62) ? row.get<62>() : (k == 63) ? row.get<63>() : (k == 64) ? row.get<64>() : row.get<65>();
                    success = (actual == expected);
                } else { // string columns 66-71
                    std::string expected; dataGen_.getRandom(i, k, expected);
                    const std::string& actual = (k == 66) ? row.get<66>() : (k == 67) ? row.get<67>() : (k == 68) ? row.get<68>() : (k == 69) ? row.get<69>() : (k == 70) ? row.get<70>() : row.get<71>();
                    success = (actual == expected);
                }
                
                if (!success) {
                    throw std::runtime_error("Data mismatch at row " + std::to_string(i) + ", column " + std::to_string(k));
                }
            }
            ++i;
            
            if (i % 50000 == 0) {
                std::cout << "  BCSV Static Progress: " << i << "/" << NUM_ROWS << " rows read\n";
            }
        }
        reader.close();
        return i;
    }

    // BCSV Static benchmark
    std::pair<double, double> benchmarkBCSVStatic() {
        std::cout << "Benchmarking BCSV Static interface...\n";
        
        // Write BCSV Static
        std::chrono::steady_clock::time_point t_start, t_end;
        t_start = std::chrono::steady_clock::now();
        writeBCSVStatic(BCSV_STATIC_FILENAME, NUM_ROWS);
        t_end = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate file was written successfully
        size_t fileSize = validateFile(BCSV_STATIC_FILENAME);

        auto layout = createStaticLayout();
        
        // Read BCSV Static
        t_start = std::chrono::steady_clock::now();
        size_t rowsRead = readBCSVStatic(BCSV_STATIC_FILENAME, layout);
        t_end = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate row count matches
        if (rowsRead != NUM_ROWS) {
            throw std::runtime_error("Row count mismatch: expected " + std::to_string(NUM_ROWS) + 
                                   " but read " + std::to_string(rowsRead));
        }
        
        std::cout << "  BCSV Static Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  BCSV Static Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        std::cout << "  BCSV Static File size:  " << fileSize << " bytes (" << std::fixed << std::setprecision(2) << (fileSize / 1024.0 / 1024.0) << " MB)\n";
        return {writeTime, readTime};
    }

    // Write BCSV Flexible ZoH file
    void writeBCSVFlexibleZoH(const std::string& filepath, const bcsv::Layout& layout, size_t numberOfRows) {
        bcsv::Writer<bcsv::Layout, bcsv::TrackingPolicy::Enabled> writer(layout);
        if (!writer.open(filepath, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
            throw std::runtime_error("Failed to open file for writing: " + filepath + " - " + writer.getErrorMsg());
        }
        
        // Temporary variables to hold generated data
        bcsv::Row testData(layout);

        const size_t colCount = layout.columnCount();
        for (size_t i = 0; i < numberOfRows; ++i) {
            auto& row = writer.row();
            for(size_t k = 0; k < colCount; ++k) {
                // Pre-fill with time-series data to simulate ZoH patterns
                switch(layout.columnType(k)) {
                    case bcsv::ColumnType::BOOL: {
                        bool tmp; dataGen_.getTimeSeries(i, k, tmp);
                        testData.set(k, tmp);
                        row.set(k, testData.get<bool>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT8: {
                        dataGen_.getTimeSeries(i, k, testData.ref<int8_t>(k));
                        row.set(k, testData.get<int8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT16: {
                        dataGen_.getTimeSeries(i, k, testData.ref<int16_t>(k));
                        row.set(k, testData.get<int16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT32: {
                        dataGen_.getTimeSeries(i, k, testData.ref<int32_t>(k));
                        row.set(k, testData.get<int32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT64: {
                        dataGen_.getTimeSeries(i, k, testData.ref<int64_t>(k));
                        row.set(k, testData.get<int64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT8: {
                        dataGen_.getTimeSeries(i, k, testData.ref<uint8_t>(k));
                        row.set(k, testData.get<uint8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT16: {
                        dataGen_.getTimeSeries(i, k, testData.ref<uint16_t>(k));
                        row.set(k, testData.get<uint16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT32: {
                        dataGen_.getTimeSeries(i, k, testData.ref<uint32_t>(k));
                        row.set(k, testData.get<uint32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT64: {
                        dataGen_.getTimeSeries(i, k, testData.ref<uint64_t>(k));
                        row.set(k, testData.get<uint64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::FLOAT: {
                        dataGen_.getTimeSeries(i, k, testData.ref<float>(k));
                        row.set(k, testData.get<float>(k));
                        break;
                    }
                    case bcsv::ColumnType::DOUBLE: {
                        dataGen_.getTimeSeries(i, k, testData.ref<double>(k));
                        row.set(k, testData.get<double>(k));
                        break;
                    }
                    case bcsv::ColumnType::STRING: {
                        dataGen_.getTimeSeries(i, k, testData.ref<std::string>(k));
                        row.set(k, testData.get<std::string>(k));
                        break;
                    }
                    default:
                        throw std::runtime_error("Unknown column type encountered.");
                }
            }
            writer.writeRow();
            if (i % 50000 == 0) {
                std::cout << "  BCSV Flexible ZoH Progress: " << i << "/" << numberOfRows << " rows written\n";
            }
        }
        writer.close();
    }
    
    // Read BCSV Flexible ZoH file and return number of rows read
    size_t readBCSVFlexibleZoH(const std::string& filepath, const bcsv::Layout& layoutExpected) {
        bcsv::Reader<bcsv::Layout, bcsv::TrackingPolicy::Enabled> reader;
        if (!reader.open(filepath)) {
            throw std::runtime_error("Failed to open file for reading: " + filepath + " - " + reader.getErrorMsg());
        }
        
        if(!reader.layout().isCompatible(layoutExpected)) {
            throw std::runtime_error("Layout mismatch when reading BCSV Flexible ZoH file.");
        }

        // Temporary row to hold data for comparison avoids reallocations
        bcsv::Row tempRow(reader.layout());

        size_t i = 0;
        const size_t colCount = reader.layout().columnCount();
        while (reader.readNext()) {
            const auto& row = reader.row();

            // Access all columns to ensure fair comparison - match the actual layout pattern
            for (size_t k = 0; k < colCount; ++k) {
                bool success;
                switch(reader.layout().columnType(k)) {
                    case bcsv::ColumnType::BOOL: {
                        bool tmp; dataGen_.getTimeSeries(i, k, tmp);
                        tempRow.set(k, tmp);
                        const bool& val_read = row.get<bool>(k);
                        success = (val_read == tempRow.get<bool>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT8: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<int8_t>(k));
                        const int8_t& val_read = row.get<int8_t>(k);
                        success = (val_read == tempRow.get<int8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT16: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<int16_t>(k));
                        const int16_t& val_read = row.get<int16_t>(k);
                        success = (val_read == tempRow.get<int16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT32: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<int32_t>(k));
                        const int32_t& val_read = row.get<int32_t>(k);
                        success = (val_read == tempRow.get<int32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::INT64: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<int64_t>(k));
                        const int64_t& val_read = row.get<int64_t>(k);
                        success = (val_read == tempRow.get<int64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT8: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<uint8_t>(k));
                        const uint8_t& val_read = row.get<uint8_t>(k);
                        success = (val_read == tempRow.get<uint8_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT16: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<uint16_t>(k));
                        const uint16_t& val_read = row.get<uint16_t>(k);
                        success = (val_read == tempRow.get<uint16_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT32: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<uint32_t>(k));
                        const uint32_t& val_read = row.get<uint32_t>(k);
                        success = (val_read == tempRow.get<uint32_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::UINT64: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<uint64_t>(k));
                        const uint64_t& val_read = row.get<uint64_t>(k);
                        success = (val_read == tempRow.get<uint64_t>(k));
                        break;
                    }
                    case bcsv::ColumnType::FLOAT: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<float>(k));
                        const float& val_read = row.get<float>(k);
                        success = (val_read == tempRow.get<float>(k));
                        break;
                    }
                    case bcsv::ColumnType::DOUBLE: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<double>(k));
                        const double& val_read = row.get<double>(k);
                        success = (val_read == tempRow.get<double>(k));
                        break;
                    }
                    case bcsv::ColumnType::STRING: {
                        dataGen_.getTimeSeries(i, k, tempRow.ref<std::string>(k));
                        const std::string& val_read = row.get<std::string>(k);
                        success = (val_read == tempRow.get<std::string>(k));
                        break;
                    }
                    default:
                        throw std::runtime_error("Unknown column type encountered.");
                }
                if(!success) {
                    throw std::runtime_error("Data mismatch at row " + std::to_string(i) + ", column " + std::to_string(k));
                }
            }
            ++i;
            
            if (i % 50000 == 0) {
                std::cout << "  BCSV Flexible ZoH Progress: " << i << "/" << NUM_ROWS << " rows read\n";
            }
        }
        reader.close();
        return i;
    }

    // BCSV Flexible ZoH benchmark
    std::pair<double, double> benchmarkBCSVFlexibleZoH() {
        std::cout << "Benchmarking BCSV Flexible interface with ZoH...\n";
        
        auto layout = createFlexibleLayout();
        
        // Write BCSV Flexible ZoH
        std::chrono::steady_clock::time_point t_start, t_end;
        t_start = std::chrono::steady_clock::now();
        writeBCSVFlexibleZoH(BCSV_FLEXIBLE_ZOH_FILENAME, layout, NUM_ROWS);
        t_end = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate file was written successfully
        size_t fileSize = validateFile(BCSV_FLEXIBLE_ZOH_FILENAME);

        
        // Read BCSV Flexible ZoH
        t_start = std::chrono::steady_clock::now();
        size_t rowsRead = readBCSVFlexibleZoH(BCSV_FLEXIBLE_ZOH_FILENAME, layout);
        t_end = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate row count matches
        if (rowsRead != NUM_ROWS) {
            throw std::runtime_error("Row count mismatch: expected " + std::to_string(NUM_ROWS) + 
                                   " but read " + std::to_string(rowsRead));
        }
        
        std::cout << "  BCSV Flexible ZoH Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  BCSV Flexible ZoH Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        std::cout << "  BCSV Flexible ZoH File size:  " << fileSize << " bytes (" << std::fixed << std::setprecision(2) << (fileSize / 1024.0 / 1024.0) << " MB)\n";
        return {writeTime, readTime};
    }

    // Write BCSV Static ZoH file
    void writeBCSVStaticZoH(const std::string& filepath, size_t numberOfRows) {
        // For static layouts, create layout with column names directly
        std::array<std::string, 72> columnNames;
        const std::vector<std::string> typeNames = {"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float", "double", "string"};
        
        size_t idx = 0;
        for (size_t typeIdx = 0; typeIdx < typeNames.size(); ++typeIdx) {
            for (size_t colIdx = 0; colIdx < 6; ++colIdx) {
                columnNames[idx++] = typeNames[typeIdx] + "_" + std::to_string(colIdx);
            }
        }
        
        LargeTestLayoutStatic layout(columnNames);
        bcsv::Writer<LargeTestLayoutStatic, bcsv::TrackingPolicy::Enabled> writer(layout);
        if (!writer.open(filepath, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
            throw std::runtime_error("Failed to open file for writing: " + filepath + " - " + writer.getErrorMsg());
        }
        
        for (size_t i = 0; i < numberOfRows; ++i) {
            populateStaticRowZoH(writer, i);
            writer.writeRow();
            
            if (i % 50000 == 0) {
                std::cout << "  BCSV Static ZoH Progress: " << i << "/" << numberOfRows << " rows written\n";
            }
        }
        writer.close();
    }
    
    // Read BCSV Static ZoH file and return number of rows read with validation
    size_t readBCSVStaticZoH(const std::string& filepath, const LargeTestLayoutStatic& /*layoutExpected*/) {
        bcsv::Reader<LargeTestLayoutStatic, bcsv::TrackingPolicy::Enabled> reader;
        if (!reader.open(filepath)) {
            throw std::runtime_error("Failed to open file for reading: " + filepath + " - " + reader.getErrorMsg());
        }

        size_t i = 0;
        while (reader.readNext()) {
            const auto& row = reader.row();

            // Validate all 72 columns using runtime loop with time-series data
            for (size_t k = 0; k < 72; ++k) {
                bool success = false;
                
                // Use compile-time column access for validation
                if (k < 6) { // bool columns 0-5
                    bool expected; dataGen_.getTimeSeries(i, k, expected);
                    bool actual = (k == 0) ? row.get<0>() : (k == 1) ? row.get<1>() : (k == 2) ? row.get<2>() : (k == 3) ? row.get<3>() : (k == 4) ? row.get<4>() : row.get<5>();
                    success = (actual == expected);
                } else if (k < 12) { // int8 columns 6-11
                    int8_t expected; dataGen_.getTimeSeries(i, k, expected);
                    int8_t actual = (k == 6) ? row.get<6>() : (k == 7) ? row.get<7>() : (k == 8) ? row.get<8>() : (k == 9) ? row.get<9>() : (k == 10) ? row.get<10>() : row.get<11>();
                    success = (actual == expected);
                } else if (k < 18) { // int16 columns 12-17
                    int16_t expected; dataGen_.getTimeSeries(i, k, expected);
                    int16_t actual = (k == 12) ? row.get<12>() : (k == 13) ? row.get<13>() : (k == 14) ? row.get<14>() : (k == 15) ? row.get<15>() : (k == 16) ? row.get<16>() : row.get<17>();
                    success = (actual == expected);
                } else if (k < 24) { // int32 columns 18-23
                    int32_t expected; dataGen_.getTimeSeries(i, k, expected);
                    int32_t actual = (k == 18) ? row.get<18>() : (k == 19) ? row.get<19>() : (k == 20) ? row.get<20>() : (k == 21) ? row.get<21>() : (k == 22) ? row.get<22>() : row.get<23>();
                    success = (actual == expected);
                } else if (k < 30) { // int64 columns 24-29
                    int64_t expected; dataGen_.getTimeSeries(i, k, expected);
                    int64_t actual = (k == 24) ? row.get<24>() : (k == 25) ? row.get<25>() : (k == 26) ? row.get<26>() : (k == 27) ? row.get<27>() : (k == 28) ? row.get<28>() : row.get<29>();
                    success = (actual == expected);
                } else if (k < 36) { // uint8 columns 30-35
                    uint8_t expected; dataGen_.getTimeSeries(i, k, expected);
                    uint8_t actual = (k == 30) ? row.get<30>() : (k == 31) ? row.get<31>() : (k == 32) ? row.get<32>() : (k == 33) ? row.get<33>() : (k == 34) ? row.get<34>() : row.get<35>();
                    success = (actual == expected);
                } else if (k < 42) { // uint16 columns 36-41
                    uint16_t expected; dataGen_.getTimeSeries(i, k, expected);
                    uint16_t actual = (k == 36) ? row.get<36>() : (k == 37) ? row.get<37>() : (k == 38) ? row.get<38>() : (k == 39) ? row.get<39>() : (k == 40) ? row.get<40>() : row.get<41>();
                    success = (actual == expected);
                } else if (k < 48) { // uint32 columns 42-47
                    uint32_t expected; dataGen_.getTimeSeries(i, k, expected);
                    uint32_t actual = (k == 42) ? row.get<42>() : (k == 43) ? row.get<43>() : (k == 44) ? row.get<44>() : (k == 45) ? row.get<45>() : (k == 46) ? row.get<46>() : row.get<47>();
                    success = (actual == expected);
                } else if (k < 54) { // uint64 columns 48-53
                    uint64_t expected; dataGen_.getTimeSeries(i, k, expected);
                    uint64_t actual = (k == 48) ? row.get<48>() : (k == 49) ? row.get<49>() : (k == 50) ? row.get<50>() : (k == 51) ? row.get<51>() : (k == 52) ? row.get<52>() : row.get<53>();
                    success = (actual == expected);
                } else if (k < 60) { // float columns 54-59
                    float expected; dataGen_.getTimeSeries(i, k, expected);
                    float actual = (k == 54) ? row.get<54>() : (k == 55) ? row.get<55>() : (k == 56) ? row.get<56>() : (k == 57) ? row.get<57>() : (k == 58) ? row.get<58>() : row.get<59>();
                    success = (actual == expected);
                } else if (k < 66) { // double columns 60-65
                    double expected; dataGen_.getTimeSeries(i, k, expected);
                    double actual = (k == 60) ? row.get<60>() : (k == 61) ? row.get<61>() : (k == 62) ? row.get<62>() : (k == 63) ? row.get<63>() : (k == 64) ? row.get<64>() : row.get<65>();
                    success = (actual == expected);
                } else { // string columns 66-71
                    std::string expected; dataGen_.getTimeSeries(i, k, expected);
                    const std::string& actual = (k == 66) ? row.get<66>() : (k == 67) ? row.get<67>() : (k == 68) ? row.get<68>() : (k == 69) ? row.get<69>() : (k == 70) ? row.get<70>() : row.get<71>();
                    success = (actual == expected);
                }
                
                if (!success) {
                    throw std::runtime_error("Data mismatch at row " + std::to_string(i) + ", column " + std::to_string(k));
                }
            }
            ++i;
            
            if (i % 50000 == 0) {
                std::cout << "  BCSV Static ZoH Progress: " << i << "/" << NUM_ROWS << " rows read\n";
            }
        }
        reader.close();
        return i;
    }

    // BCSV Static ZoH benchmark
    std::pair<double, double> benchmarkBCSVStaticZoH() {
        std::cout << "Benchmarking BCSV Static interface with ZoH...\n";
        
        // Write BCSV Static ZoH
        std::chrono::steady_clock::time_point t_start, t_end;
        t_start = std::chrono::steady_clock::now();
        writeBCSVStaticZoH(BCSV_STATIC_ZOH_FILENAME, NUM_ROWS);
        t_end = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate file was written successfully
        size_t fileSize = validateFile(BCSV_STATIC_ZOH_FILENAME);

        auto layout = createStaticLayout();
        
        // Read BCSV Static ZoH
        t_start = std::chrono::steady_clock::now();
        size_t rowsRead = readBCSVStaticZoH(BCSV_STATIC_ZOH_FILENAME, layout);
        t_end = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        
        // Validate row count matches
        if (rowsRead != NUM_ROWS) {
            throw std::runtime_error("Row count mismatch: expected " + std::to_string(NUM_ROWS) + 
                                   " but read " + std::to_string(rowsRead));
        }
        
        std::cout << "  BCSV Static ZoH Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  BCSV Static ZoH Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        std::cout << "  BCSV Static ZoH File size:  " << fileSize << " bytes (" << std::fixed << std::setprecision(2) << (fileSize / 1024.0 / 1024.0) << " MB)\n";
        return {writeTime, readTime};
    }

    void printComprehensiveResults(const std::pair<double, double>& csvTimes,
                                 const std::pair<double, double>& flexibleTimes,
                                 const std::pair<double, double>& staticTimes,
                                 const std::pair<double, double>& flexibleZoHTimes,
                                 const std::pair<double, double>& staticZoHTimes) {
        std::cout << "Comprehensive Large Scale Performance Results\n";
        std::cout << "============================================\n\n";
        
        // File sizes
        auto csvSize = std::filesystem::file_size(CSV_FILENAME);
        auto flexibleSize = std::filesystem::file_size(BCSV_FLEXIBLE_FILENAME);
        auto staticSize = std::filesystem::file_size(BCSV_STATIC_FILENAME);
        auto flexibleZoHSize = std::filesystem::file_size(BCSV_FLEXIBLE_ZOH_FILENAME);
        auto staticZoHSize = std::filesystem::file_size(BCSV_STATIC_ZOH_FILENAME);
        
        std::cout << "File Sizes:\n";
        std::cout << "  CSV:             " << csvSize << " bytes (" << std::fixed << std::setprecision(1) << (csvSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  BCSV Flexible:   " << flexibleSize << " bytes (" << std::setprecision(1) << (flexibleSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  BCSV Static:     " << staticSize << " bytes (" << std::setprecision(1) << (staticSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  BCSV Flex ZoH:   " << flexibleZoHSize << " bytes (" << std::setprecision(1) << (flexibleZoHSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  BCSV Static ZoH: " << staticZoHSize << " bytes (" << std::setprecision(1) << (staticZoHSize / 1024.0 / 1024.0) << " MB)\n\n";
        
        std::cout << "Compression Ratios:\n";
        std::cout << "  BCSV vs CSV:        " << std::setprecision(1) << (100.0 - (flexibleSize * 100.0 / csvSize)) << "% smaller\n";
        std::cout << "  Static vs Flexible: " << std::setprecision(1) << (100.0 - (staticSize * 100.0 / flexibleSize)) << "% difference\n";
        std::cout << "  ZoH vs Regular:     " << std::setprecision(1) << (100.0 - (flexibleZoHSize * 100.0 / flexibleSize)) << "% smaller (Flexible)\n";
        std::cout << "  ZoH vs CSV:         " << std::setprecision(1) << (100.0 - (flexibleZoHSize * 100.0 / csvSize)) << "% smaller\n\n";
        
        // Performance comparison table
        std::cout << "Performance Comparison (500,000 rows, 72 columns):\n\n";
        std::cout << "Format          | Write (ms) | Read (ms)  | Total (ms) | Write MB/s | Read MB/s  | Total MB/s\n";
        std::cout << "----------------|------------|------------|------------|------------|------------|------------\n";
        
        auto printRow = [&](const std::string& name, double writeTime, double readTime, size_t fileSize) {
            double totalTime = writeTime + readTime;
            double fileSizeMB = fileSize / 1024.0 / 1024.0;
            double writeMBps = fileSizeMB / (writeTime / 1000.0);
            double readMBps = fileSizeMB / (readTime / 1000.0);
            double totalMBps = fileSizeMB / (totalTime / 1000.0);
            
            std::cout << std::left << std::setw(15) << name << " | "
                      << std::right << std::setw(10) << std::fixed << std::setprecision(1) << writeTime << " | "
                      << std::setw(10) << readTime << " | "
                      << std::setw(10) << totalTime << " | "
                      << std::setw(10) << std::setprecision(1) << writeMBps << " | "
                      << std::setw(10) << readMBps << " | "
                      << std::setw(10) << totalMBps << "\n";
        };
        
        printRow("CSV", csvTimes.first, csvTimes.second, csvSize);
        printRow("BCSV Flexible", flexibleTimes.first, flexibleTimes.second, flexibleSize);
        printRow("BCSV Static", staticTimes.first, staticTimes.second, staticSize);
        printRow("BCSV Flex ZoH", flexibleZoHTimes.first, flexibleZoHTimes.second, flexibleZoHSize);
        printRow("BCSV Static ZoH", staticZoHTimes.first, staticZoHTimes.second, staticZoHSize);
        
        std::cout << "\n";
        
        // Speedup analysis
        std::cout << "Performance Speedups:\n";
        std::cout << "  BCSV Flexible vs CSV:\n";
        std::cout << "    Write speedup: " << std::setprecision(2) << (csvTimes.first / flexibleTimes.first) << "x\n";
        std::cout << "    Read speedup:  " << std::setprecision(2) << (csvTimes.second / flexibleTimes.second) << "x\n";
        std::cout << "    Total speedup: " << std::setprecision(2) << ((csvTimes.first + csvTimes.second) / (flexibleTimes.first + flexibleTimes.second)) << "x\n\n";
        
        std::cout << "  BCSV Static vs CSV:\n";
        std::cout << "    Write speedup: " << std::setprecision(2) << (csvTimes.first / staticTimes.first) << "x\n";
        std::cout << "    Read speedup:  " << std::setprecision(2) << (csvTimes.second / staticTimes.second) << "x\n";
        std::cout << "    Total speedup: " << std::setprecision(2) << ((csvTimes.first + csvTimes.second) / (staticTimes.first + staticTimes.second)) << "x\n\n";
        
        std::cout << "  BCSV Static vs Flexible:\n";
        std::cout << "    Write speedup: " << std::setprecision(2) << (flexibleTimes.first / staticTimes.first) << "x\n";
        std::cout << "    Read speedup:  " << std::setprecision(2) << (flexibleTimes.second / staticTimes.second) << "x\n";
        std::cout << "    Total speedup: " << std::setprecision(2) << ((flexibleTimes.first + flexibleTimes.second) / (staticTimes.first + staticTimes.second)) << "x\n\n";
        
        std::cout << "  BCSV Flexible ZoH vs Flexible:\n";
        std::cout << "    Write speedup: " << std::setprecision(2) << (flexibleTimes.first / flexibleZoHTimes.first) << "x\n";
        std::cout << "    Read speedup:  " << std::setprecision(2) << (flexibleTimes.second / flexibleZoHTimes.second) << "x\n";
        std::cout << "    Total speedup: " << std::setprecision(2) << ((flexibleTimes.first + flexibleTimes.second) / (flexibleZoHTimes.first + flexibleZoHTimes.second)) << "x\n\n";
        
        std::cout << "  BCSV Static ZoH vs CSV:\n";
        std::cout << "    Write speedup: " << std::setprecision(2) << (csvTimes.first / staticZoHTimes.first) << "x\n";
        std::cout << "    Read speedup:  " << std::setprecision(2) << (csvTimes.second / staticZoHTimes.second) << "x\n";
        std::cout << "    Total speedup: " << std::setprecision(2) << ((csvTimes.first + csvTimes.second) / (staticZoHTimes.first + staticZoHTimes.second)) << "x\n\n";
        
        // Throughput analysis
        double csvThroughput = NUM_ROWS / ((csvTimes.first + csvTimes.second) / 1000.0);
        double flexibleThroughput = NUM_ROWS / ((flexibleTimes.first + flexibleTimes.second) / 1000.0);
        double staticThroughput = NUM_ROWS / ((staticTimes.first + staticTimes.second) / 1000.0);
        double flexibleZoHThroughput = NUM_ROWS / ((flexibleZoHTimes.first + flexibleZoHTimes.second) / 1000.0);
        double staticZoHThroughput = NUM_ROWS / ((staticZoHTimes.first + staticZoHTimes.second) / 1000.0);
        
        std::cout << "Throughput (rows/second):\n";
        std::cout << "  CSV:             " << std::fixed << std::setprecision(0) << csvThroughput << "\n";
        std::cout << "  BCSV Flexible:   " << flexibleThroughput << "\n";
        std::cout << "  BCSV Static:     " << staticThroughput << "\n";
        std::cout << "  BCSV Flex ZoH:   " << flexibleZoHThroughput << "\n";
        std::cout << "  BCSV Static ZoH: " << staticZoHThroughput << "\n\n";
        
        std::cout << "Recommendations for Large-Scale Data Processing:\n";
            // Only recommend BCSV over CSV if it is actually faster or smaller
            bool bcsvFaster = (flexibleTimes.first + flexibleTimes.second) < (csvTimes.first + csvTimes.second);
            bool bcsvSmaller = flexibleSize < csvSize;
            if (bcsvFaster && bcsvSmaller) {
                std::cout << "  ✓ BCSV provides significant performance and storage benefits over CSV\n";
            } else if (bcsvFaster) {
                std::cout << "  ✓ BCSV is faster than CSV, but CSV is smaller in this run\n";
            } else if (bcsvSmaller) {
                std::cout << "  ✓ BCSV is smaller than CSV, but CSV is faster in this run\n";
            } else {
                std::cout << "  → CSV outperformed BCSV in both speed and size in this run\n";
            }
            std::cout << "  File size reduction: " << std::setprecision(1) << (100.0 - (flexibleSize * 100.0 / csvSize)) << "%\n";
    }

    void runLargeScaleBenchmark() {
        std::cout << "Starting large scale benchmark...\n\n";
        
        auto csvTimes = benchmarkCSV();
        auto flexibleTimes = benchmarkBCSVFlexible();
        auto staticTimes = benchmarkBCSVStatic();
        auto flexibleZoHTimes = benchmarkBCSVFlexibleZoH();
        auto staticZoHTimes = benchmarkBCSVStaticZoH();
        
        printComprehensiveResults(csvTimes, flexibleTimes, staticTimes, flexibleZoHTimes, staticZoHTimes);
        
        // Cleanup
        std::filesystem::remove(CSV_FILENAME);
        std::filesystem::remove(BCSV_FLEXIBLE_FILENAME);
        std::filesystem::remove(BCSV_STATIC_FILENAME);
        std::filesystem::remove(BCSV_FLEXIBLE_ZOH_FILENAME);
        std::filesystem::remove(BCSV_STATIC_ZOH_FILENAME);
        
        std::cout << "\nLarge scale benchmark completed successfully!\n";
    }
};

int main() {
    try {
        LargeScaleBenchmark benchmark;
        benchmark.runLargeScaleBenchmark();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}