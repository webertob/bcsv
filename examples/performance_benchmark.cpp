/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <filesystem>
#include <bcsv/bcsv.h>

/**
 * BCSV Performance Benchmark
 * 
 * This comprehensive benchmark compares the performance of flexible vs static interfaces
 * for large file operations. It tests both write and read performance with configurable
 * row counts, data complexity, and compression levels.
 * 
 * Key metrics measured:
 * - Write performance (time to write all rows)
 * - Read performance (time to read all rows)  
 * - File size comparison
 * - Memory efficiency
 * - Throughput (rows per second)
 */

class PerformanceBenchmark {
private:
    static constexpr size_t DEFAULT_NUM_ROWS = 50000;
    static constexpr const char* FLEXIBLE_FILENAME = "benchmark_flexible.bcsv";
    static constexpr const char* STATIC_FILENAME = "benchmark_static.bcsv";
    static constexpr const char* FLEXIBLE_ZOH_FILENAME = "benchmark_flexible_zoh.bcsv";
    static constexpr const char* STATIC_ZOH_FILENAME = "benchmark_static_zoh.bcsv";
    
    size_t numRows_;
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<int32_t> int_dist_;
    std::uniform_real_distribution<float> float_dist_;
    std::uniform_real_distribution<double> double_dist_;
    std::vector<std::string> sampleStrings_;

public:
    // Static layout definition for performance testing
    using BenchmarkLayoutStatic = bcsv::LayoutStatic<
        int32_t,        // id
        std::string,    // name
        float,          // score1
        double,         // score2
        bool,           // active
        int64_t,        // timestamp
        uint32_t,       // count
        std::string     // category
    >;

    PerformanceBenchmark(size_t numRows = DEFAULT_NUM_ROWS) 
        : numRows_(numRows)
        , gen_(42) // Fixed seed for reproducible results
        , int_dist_(1, 1000000)
        , float_dist_(0.0f, 100.0f)
        , double_dist_(0.0, 1000.0)
    {
        // Pre-generate sample strings for consistent data
        sampleStrings_ = {
            "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta",
            "Iota", "Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi",
            "Rho", "Sigma", "Tau", "Upsilon", "Phi", "Chi", "Psi", "Omega"
        };
        
        std::cout << "BCSV Performance Benchmark\n";
        std::cout << "==========================\n";
        std::cout << "Test Configuration:\n";
        std::cout << "  Rows to process: " << numRows_ << "\n";
        std::cout << "  Columns per row: 8 (mixed data types)\n";
        std::cout << "  Data types: INT32, STRING, FLOAT, DOUBLE, BOOL, INT64, UINT32, STRING\n";
        std::cout << "  Compression: LZ4 Level 1 (balanced performance/size)\n";
        std::cout << "  Platform: " << sizeof(void*) * 8 << "-bit\n\n";
    }

    // Generate ZoH-friendly data with repeated values (simulates time-series sensor data)
    void generateZoHData(size_t rowIndex, int32_t& id, std::string& name, float& score1, 
                        double& score2, bool& active, int64_t& timestamp, uint32_t& count, std::string& category) {
        // Create time-series like patterns with gradual changes and repetition
        const size_t changeInterval = 50; // Change values every 50 rows
        const size_t segment = rowIndex / changeInterval;
        
        id = static_cast<int32_t>(1000 + (segment % 100)); // IDs repeat in blocks
        name = sampleStrings_[(segment / 4) % sampleStrings_.size()]; // Names change slowly
        
        // Simulate sensor readings with gradual drift
        score1 = 50.0f + static_cast<float>((segment % 20) * 2.5f); // Values 50-97.5 in steps
        score2 = 100.0 + (segment % 10) * 10.0; // Values 100-190 in steps of 10
        
        active = (segment % 4) < 2; // Alternating active/inactive in blocks
        timestamp = static_cast<int64_t>(1640995200000LL + segment * 60000); // 1-minute intervals
        count = static_cast<uint32_t>(segment * 10); // Incrementing counter
        category = sampleStrings_[(segment / 8) % 4]; // Categories change very slowly
    }

    // Flexible interface benchmark
    std::pair<double, double> benchmarkFlexible() {
        std::cout << "Benchmarking Flexible Interface...\n";
        
        // Create flexible layout
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        layout.addColumn({"name", bcsv::ColumnType::STRING});
        layout.addColumn({"score1", bcsv::ColumnType::FLOAT});
        layout.addColumn({"score2", bcsv::ColumnType::DOUBLE});
        layout.addColumn({"active", bcsv::ColumnType::BOOL});
        layout.addColumn({"timestamp", bcsv::ColumnType::INT64});
        layout.addColumn({"count", bcsv::ColumnType::UINT32});
        layout.addColumn({"category", bcsv::ColumnType::STRING});

        // Benchmark writing
        auto writeStart = std::chrono::steady_clock::now();
        {
            bcsv::Writer<bcsv::Layout> writer(layout);
            // Use compression level 1 for better performance vs no compression
            writer.open(FLEXIBLE_FILENAME, true, 1);
            
            // Pre-calculate frequently used values outside the loop
            const size_t stringCount = sampleStrings_.size();
            
            for (size_t i = 0; i < numRows_; ++i) {
                // Use direct assignment instead of function calls where possible
                const size_t stringIndex1 = i % stringCount;
                const size_t stringIndex2 = (i * 7) % stringCount;
                auto& row = writer.row(); // Reference for brevity
                row.set(0, int_dist_(gen_));
                row.set(1, sampleStrings_[stringIndex1]);
                row.set(2, float_dist_(gen_));
                row.set(3, double_dist_(gen_));
                row.set(4, (i & 1) == 0);  // Bitwise AND faster than modulo
                row.set(5, static_cast<int64_t>(i * 1000));
                row.set(6, static_cast<uint32_t>(i));
                row.set(7, sampleStrings_[stringIndex2]);
                writer.writeRow();
            }
            writer.close();
        }
        auto writeEnd = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();

        // Benchmark reading
        auto readStart = std::chrono::steady_clock::now();
        {
            bcsv::Reader<bcsv::Layout> reader;
            reader.open(FLEXIBLE_FILENAME);
            
            // Pre-declare variables to avoid repeated construction
            int32_t id;
            std::string name, category;
            name.reserve(32);    // Pre-allocate string capacity
            category.reserve(32);
            float score1;
            double score2;
            bool active;
            int64_t timestamp;
            uint32_t count;
            
            while (reader.readNext()) {
                // Read all values to ensure fair comparison
                // Use assignment to pre-declared variables for better performance
                const auto& row = reader.row();
                id = row.get<int32_t>(0);
                name = row.get<std::string>(1);
                score1 = row.get<float>(2);
                score2 = row.get<double>(3);
                active = row.get<bool>(4);
                timestamp = row.get<int64_t>(5);
                count = row.get<uint32_t>(6);
                category = row.get<std::string>(7);

                // Prevent optimization by using the values
                (void)id; (void)name; (void)score1; (void)score2; 
                (void)active; (void)timestamp; (void)count; (void)category;
            }
            reader.close();
        }
        auto readEnd = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();

        std::cout << "  Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";

        return {writeTime, readTime};
    }

    // Flexible interface ZoH benchmark
    std::pair<double, double> benchmarkFlexibleZoH() {
        std::cout << "Benchmarking Flexible Interface with ZoH..."; 
        
        // Create flexible layout
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        layout.addColumn({"name", bcsv::ColumnType::STRING});
        layout.addColumn({"score1", bcsv::ColumnType::FLOAT});
        layout.addColumn({"score2", bcsv::ColumnType::DOUBLE});
        layout.addColumn({"active", bcsv::ColumnType::BOOL});
        layout.addColumn({"timestamp", bcsv::ColumnType::INT64});
        layout.addColumn({"count", bcsv::ColumnType::UINT32});
        layout.addColumn({"category", bcsv::ColumnType::STRING});

        // Benchmark writing with ZoH
        auto writeStart = std::chrono::steady_clock::now();
        {
            bcsv::Writer<bcsv::Layout> writer(layout);
            // Enable ZoH compression
            writer.open(FLEXIBLE_ZOH_FILENAME, true, 1 /*compressionLevel*/, 64 /*blockSizeKB*/, bcsv::FileFlags::ZERO_ORDER_HOLD);
            
            for (size_t i = 0; i < numRows_; ++i) {
                int32_t id;
                std::string name, category;
                float score1;
                double score2;
                bool active;
                int64_t timestamp;
                uint32_t count;
                
                generateZoHData(i, id, name, score1, score2, active, timestamp, count, category);
                
                auto& row = writer.row();
                row.set(0, id);
                row.set(1, name);
                row.set(2, score1);
                row.set(3, score2);
                row.set(4, active);
                row.set(5, timestamp);
                row.set(6, count);
                row.set(7, category);
                writer.writeRow();
            }
            writer.close();
        }
        auto writeEnd = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();

        // Benchmark reading
        auto readStart = std::chrono::steady_clock::now();
        {
            bcsv::Reader<bcsv::Layout> reader;
            reader.open(FLEXIBLE_ZOH_FILENAME);
            
            // Pre-declare variables
            int32_t id;
            std::string name, category;
            name.reserve(32);
            category.reserve(32);
            float score1;
            double score2;
            bool active;
            int64_t timestamp;
            uint32_t count;
            
            while (reader.readNext()) {
                const auto& row = reader.row();
                id = row.get<int32_t>(0);
                name = row.get<std::string>(1);
                score1 = row.get<float>(2);
                score2 = row.get<double>(3);
                active = row.get<bool>(4);
                timestamp = row.get<int64_t>(5);
                count = row.get<uint32_t>(6);
                category = row.get<std::string>(7);

                // Prevent optimization
                (void)id; (void)name; (void)score1; (void)score2; 
                (void)active; (void)timestamp; (void)count; (void)category;
            }
            reader.close();
        }
        auto readEnd = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();

        std::cout << "\n  Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";

        return {writeTime, readTime};
    }

    // Static interface benchmark
    std::pair<double, double> benchmarkStatic() {
        std::cout << "Benchmarking Static Interface...\n";
        
        // Create static layout
        std::array<std::string, 8> columnNames = {
            "id", "name", "score1", "score2", "active", "timestamp", "count", "category"
        };
        BenchmarkLayoutStatic layout(columnNames);

        // Benchmark writing
        auto writeStart = std::chrono::steady_clock::now();
        {
            bcsv::Writer<BenchmarkLayoutStatic> writer(layout);
            // Use compression level 1 for better performance vs no compression
            writer.open(STATIC_FILENAME, true, 1);
            
            // Pre-calculate frequently used values outside the loop
            const size_t stringCount = sampleStrings_.size();
            
            for (size_t i = 0; i < numRows_; ++i) {
                // Use direct assignment instead of function calls where possible
                const size_t stringIndex1 = i % stringCount;
                const size_t stringIndex2 = (i * 7) % stringCount;
                auto& row = writer.row(); // Reference for brevity
                row.set<0>(int_dist_(gen_));
                row.set<1>(sampleStrings_[stringIndex1]);
                row.set<2>(float_dist_(gen_));
                row.set<3>(double_dist_(gen_));
                row.set<4>((i & 1) == 0);  // Bitwise AND faster than modulo
                row.set<5>(static_cast<int64_t>(i * 1000));
                row.set<6>(static_cast<uint32_t>(i));
                row.set<7>(sampleStrings_[stringIndex2]);
                writer.writeRow();
            }
            writer.close();
        }
        auto writeEnd = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();

        // Benchmark reading
        auto readStart = std::chrono::steady_clock::now();
        {
            bcsv::Reader<BenchmarkLayoutStatic> reader;
            reader.open(STATIC_FILENAME);
            
            // Pre-declare variables to avoid repeated construction
            int32_t id;
            std::string name, category;
            name.reserve(32);    // Pre-allocate string capacity
            category.reserve(32);
            float score1;
            double score2;
            bool active;
            int64_t timestamp;
            uint32_t count;
            
            while (reader.readNext()) {
                // Read all values to ensure fair comparison
                // Use assignment to pre-declared variables for better performance
                const auto& row = reader.row();
                id = row.get<0>();
                name = row.get<1>();
                score1 = row.get<2>();
                score2 = row.get<3>();
                active = row.get<4>();
                timestamp = row.get<5>();
                count = row.get<6>();
                category = row.get<7>();

                // Prevent optimization by using the values
                (void)id; (void)name; (void)score1; (void)score2; 
                (void)active; (void)timestamp; (void)count; (void)category;
            }
            reader.close();
        }
        auto readEnd = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();

        std::cout << "  Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";

        return {writeTime, readTime};
    }

    // Static interface ZoH benchmark
    std::pair<double, double> benchmarkStaticZoH() {
        std::cout << "Benchmarking Static Interface with ZoH...";
        
        // Create static layout
        std::array<std::string, 8> columnNames = {
            "id", "name", "score1", "score2", "active", "timestamp", "count", "category"
        };
        BenchmarkLayoutStatic layout(columnNames);

        // Benchmark writing with ZoH
        auto writeStart = std::chrono::steady_clock::now();
        {
            bcsv::Writer<BenchmarkLayoutStatic> writer(layout);
            // Enable ZoH compression
            writer.open(STATIC_ZOH_FILENAME, true, 1 /*compressionLevel*/, 64 /*blockSizeKB*/, bcsv::FileFlags::ZERO_ORDER_HOLD);
            
            for (size_t i = 0; i < numRows_; ++i) {
                int32_t id;
                std::string name, category;
                float score1;
                double score2;
                bool active;
                int64_t timestamp;
                uint32_t count;
                
                generateZoHData(i, id, name, score1, score2, active, timestamp, count, category);
                
                auto& row = writer.row();
                row.set<0>(id);
                row.set<1>(name);
                row.set<2>(score1);
                row.set<3>(score2);
                row.set<4>(active);
                row.set<5>(timestamp);
                row.set<6>(count);
                row.set<7>(category);
                writer.writeRow();
            }
            writer.close();
        }
        auto writeEnd = std::chrono::steady_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();

        // Benchmark reading
        auto readStart = std::chrono::steady_clock::now();
        {
            bcsv::Reader<BenchmarkLayoutStatic> reader;
            reader.open(STATIC_ZOH_FILENAME);
            
            // Pre-declare variables
            int32_t id;
            std::string name, category;
            name.reserve(32);
            category.reserve(32);
            float score1;
            double score2;
            bool active;
            int64_t timestamp;
            uint32_t count;
            
            while (reader.readNext()) {
                const auto& row = reader.row();
                id = row.get<0>();
                name = row.get<1>();
                score1 = row.get<2>();
                score2 = row.get<3>();
                active = row.get<4>();
                timestamp = row.get<5>();
                count = row.get<6>();
                category = row.get<7>();

                // Prevent optimization
                (void)id; (void)name; (void)score1; (void)score2; 
                (void)active; (void)timestamp; (void)count; (void)category;
            }
            reader.close();
        }
        auto readEnd = std::chrono::steady_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();

        std::cout << "\n  Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";

        return {writeTime, readTime};
    }

    void printSummary(const std::pair<double, double>& flexibleTimes, 
                     const std::pair<double, double>& staticTimes,
                     const std::pair<double, double>& flexibleZoHTimes,
                     const std::pair<double, double>& staticZoHTimes) {
        std::cout << "Performance Summary\n";
        std::cout << "==================\n\n";

        // File size comparison
        auto flexibleSize = std::filesystem::file_size(FLEXIBLE_FILENAME);
        auto staticSize = std::filesystem::file_size(STATIC_FILENAME);
        auto flexibleZoHSize = std::filesystem::file_size(FLEXIBLE_ZOH_FILENAME);
        auto staticZoHSize = std::filesystem::file_size(STATIC_ZOH_FILENAME);
        
        std::cout << "File Sizes:\n";
        std::cout << "  Flexible:     " << flexibleSize << " bytes (" << std::fixed << std::setprecision(1) << (flexibleSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  Static:       " << staticSize << " bytes (" << std::fixed << std::setprecision(1) << (staticSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  Flexible ZoH: " << flexibleZoHSize << " bytes (" << std::fixed << std::setprecision(1) << (flexibleZoHSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  Static ZoH:   " << staticZoHSize << " bytes (" << std::fixed << std::setprecision(1) << (staticZoHSize / 1024.0 / 1024.0) << " MB)\n";
        
        // ZoH compression effectiveness
        double flexibleZoHRatio = static_cast<double>(flexibleSize) / flexibleZoHSize;
        double staticZoHRatio = static_cast<double>(staticSize) / staticZoHSize;
        std::cout << "\nZoH Compression Effectiveness:\n";
        std::cout << "  Flexible ZoH ratio: " << std::fixed << std::setprecision(2) << flexibleZoHRatio << ":1 (" << std::setprecision(1) << (100.0 - (flexibleZoHSize * 100.0 / flexibleSize)) << "% reduction)\n";
        std::cout << "  Static ZoH ratio:   " << std::fixed << std::setprecision(2) << staticZoHRatio << ":1 (" << std::setprecision(1) << (100.0 - (staticZoHSize * 100.0 / staticSize)) << "% reduction)\n";
        
        // Calculate compression ratio (estimate based on raw data size)
        size_t estimatedRawSize = numRows_ * (4 + 8 + 4 + 8 + 1 + 8 + 4 + 8); // Approximate raw data size
        double compressionRatio = static_cast<double>(estimatedRawSize) / flexibleSize;
        std::cout << "  Overall compression ratio: " << std::fixed << std::setprecision(1) << compressionRatio << ":1 (" << std::setprecision(1) << (100.0 - (flexibleSize * 100.0 / estimatedRawSize)) << "% reduction)\n\n";

        // Performance comparison - all formats
        double flexibleTotal = flexibleTimes.first + flexibleTimes.second;
        double staticTotal = staticTimes.first + staticTimes.second;
        double flexibleZoHTotal = flexibleZoHTimes.first + flexibleZoHTimes.second;
        double staticZoHTotal = staticZoHTimes.first + staticZoHTimes.second;

        std::cout << "Performance Comparison (Total Time):\n";
        std::cout << "  Flexible interface:      " << std::fixed << std::setprecision(2) << flexibleTotal << " ms\n";
        std::cout << "  Static interface:        " << std::fixed << std::setprecision(2) << staticTotal << " ms\n";
        std::cout << "  Flexible interface ZoH:  " << std::fixed << std::setprecision(2) << flexibleZoHTotal << " ms\n";
        std::cout << "  Static interface ZoH:    " << std::fixed << std::setprecision(2) << staticZoHTotal << " ms\n\n";

        // Speedup calculations
        double staticSpeedup = flexibleTotal / staticTotal;
        double flexibleZoHSpeedup = flexibleTotal / flexibleZoHTotal;
        double staticZoHSpeedup = flexibleTotal / staticZoHTotal;
        
        std::cout << "Speedup vs Flexible baseline:\n";
        std::cout << "  Static speedup:        " << std::fixed << std::setprecision(2) << staticSpeedup << "x\n";
        std::cout << "  Flexible ZoH speedup:  " << std::fixed << std::setprecision(2) << flexibleZoHSpeedup << "x\n";
        std::cout << "  Static ZoH speedup:    " << std::fixed << std::setprecision(2) << staticZoHSpeedup << "x\n\n";

        // Write performance breakdown
        std::cout << "Write Performance:\n";
        std::cout << "  Flexible:     " << std::fixed << std::setprecision(2) << flexibleTimes.first << " ms\n";
        std::cout << "  Static:       " << std::fixed << std::setprecision(2) << staticTimes.first << " ms (" << std::setprecision(2) << (flexibleTimes.first / staticTimes.first) << "x)\n";
        std::cout << "  Flexible ZoH: " << std::fixed << std::setprecision(2) << flexibleZoHTimes.first << " ms (" << std::setprecision(2) << (flexibleTimes.first / flexibleZoHTimes.first) << "x)\n";
        std::cout << "  Static ZoH:   " << std::fixed << std::setprecision(2) << staticZoHTimes.first << " ms (" << std::setprecision(2) << (flexibleTimes.first / staticZoHTimes.first) << "x)\n\n";

        // Read performance breakdown
        std::cout << "Read Performance:\n";
        std::cout << "  Flexible:     " << std::fixed << std::setprecision(2) << flexibleTimes.second << " ms\n";
        std::cout << "  Static:       " << std::fixed << std::setprecision(2) << staticTimes.second << " ms (" << std::setprecision(2) << (flexibleTimes.second / staticTimes.second) << "x)\n";
        std::cout << "  Flexible ZoH: " << std::fixed << std::setprecision(2) << flexibleZoHTimes.second << " ms (" << std::setprecision(2) << (flexibleTimes.second / flexibleZoHTimes.second) << "x)\n";
        std::cout << "  Static ZoH:   " << std::fixed << std::setprecision(2) << staticZoHTimes.second << " ms (" << std::setprecision(2) << (flexibleTimes.second / staticZoHTimes.second) << "x)\n\n";

        // Throughput analysis
        double flexibleThroughput = numRows_ / (flexibleTotal / 1000.0);
        double staticThroughput = numRows_ / (staticTotal / 1000.0);
        double flexibleZoHThroughput = numRows_ / (flexibleZoHTotal / 1000.0);
        double staticZoHThroughput = numRows_ / (staticZoHTotal / 1000.0);
        
        std::cout << "Throughput (rows/second):\n";
        std::cout << "  Flexible:     " << std::fixed << std::setprecision(0) << flexibleThroughput << "\n";
        std::cout << "  Static:       " << std::fixed << std::setprecision(0) << staticThroughput << "\n";
        std::cout << "  Flexible ZoH: " << std::fixed << std::setprecision(0) << flexibleZoHThroughput << "\n";
        std::cout << "  Static ZoH:   " << std::fixed << std::setprecision(0) << staticZoHThroughput << "\n\n";
        
        // Data transfer rates
        std::cout << "Data Transfer Rate (MB/s):\n";
        std::cout << "  Flexible:     " << std::fixed << std::setprecision(1) << (flexibleSize / 1024.0 / 1024.0) / (flexibleTotal / 1000.0) << " MB/s\n";
        std::cout << "  Static:       " << std::fixed << std::setprecision(1) << (staticSize / 1024.0 / 1024.0) / (staticTotal / 1000.0) << " MB/s\n";
        std::cout << "  Flexible ZoH: " << std::fixed << std::setprecision(1) << (flexibleZoHSize / 1024.0 / 1024.0) / (flexibleZoHTotal / 1000.0) << " MB/s\n";
        std::cout << "  Static ZoH:   " << std::fixed << std::setprecision(1) << (staticZoHSize / 1024.0 / 1024.0) / (staticZoHTotal / 1000.0) << " MB/s\n\n";
        
        std::cout << "ZoH Performance Analysis:\n";
        double zohWriteOverhead = ((flexibleZoHTimes.first + staticZoHTimes.first) / 2.0) / ((flexibleTimes.first + staticTimes.first) / 2.0) - 1.0;
        double zohReadOverhead = ((flexibleZoHTimes.second + staticZoHTimes.second) / 2.0) / ((flexibleTimes.second + staticTimes.second) / 2.0) - 1.0;
        std::cout << "  Write overhead: " << std::fixed << std::setprecision(1) << (zohWriteOverhead * 100.0) << "%\n";
        std::cout << "  Read overhead:  " << std::fixed << std::setprecision(1) << (zohReadOverhead * 100.0) << "%\n";
        std::cout << "  Space savings:  " << std::fixed << std::setprecision(1) << (100.0 - (((flexibleZoHSize + staticZoHSize) / 2.0) * 100.0 / ((flexibleSize + staticSize) / 2.0))) << "%\n\n";
        
        std::cout << "Recommendations:\n";
        if (flexibleZoHRatio > 1.5) {
            std::cout << "  ✓ ZoH provides significant space savings (" << std::setprecision(1) << flexibleZoHRatio << ":1 ratio) for time-series data\n";
        }
        if (zohWriteOverhead < 0.2) {
            std::cout << "  ✓ ZoH write overhead is minimal (" << std::setprecision(1) << (zohWriteOverhead * 100.0) << "%)\n";
        }
        if (zohReadOverhead < 0.1) {
            std::cout << "  ✓ ZoH read overhead is minimal (" << std::setprecision(1) << (zohReadOverhead * 100.0) << "%)\n";
        }
        std::cout << "  → Use ZoH for time-series data with repeated values\n";
        std::cout << "  → Use regular compression for diverse/random data\n";
        std::cout << "  → Static interface provides best overall performance\n";
        std::cout << "  → Flexible interface offers runtime schema flexibility\n\n";
    }

    void runBenchmark() {
        // Run all benchmarks
        auto flexibleTimes = benchmarkFlexible();
        auto staticTimes = benchmarkStatic();
        auto flexibleZoHTimes = benchmarkFlexibleZoH();
        auto staticZoHTimes = benchmarkStaticZoH();
        
        // Print comprehensive summary
        printSummary(flexibleTimes, staticTimes, flexibleZoHTimes, staticZoHTimes);
        
        // Test compression levels
        testCompressionLevels();
        
        // Compare with CSV baseline
        benchmarkCSVBaseline();
        
        // Cleanup
        std::filesystem::remove(FLEXIBLE_FILENAME);
        std::filesystem::remove(STATIC_FILENAME);
        std::filesystem::remove(FLEXIBLE_ZOH_FILENAME);
        std::filesystem::remove(STATIC_ZOH_FILENAME);
        
        std::cout << "Benchmark completed successfully!\n";
    }
    
    void testCompressionLevels() {
        std::cout << "Compression Level Analysis\n";
        std::cout << "=========================\n";
        
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        layout.addColumn({"name", bcsv::ColumnType::STRING});
        layout.addColumn({"score", bcsv::ColumnType::FLOAT});
        layout.addColumn({"data", bcsv::ColumnType::STRING});
        
        // Test different compression levels (0-9)
        std::vector<int> compressionLevels = {0, 1, 3, 6, 9};
        const size_t testRows = 10000; // Smaller dataset for compression test
        
        std::cout << "Testing " << testRows << " rows with different compression levels:\n\n";
        std::cout << "Level | Time (ms) | Size (bytes) | Ratio | Speed (MB/s)\n";
        std::cout << "------|-----------|--------------|-------|-------------\n";
        
        for (int level : compressionLevels) {
            std::string filename = "compression_test_" + std::to_string(level) + ".bcsv";
            
            auto start = std::chrono::steady_clock::now();
            {
                bcsv::Writer<bcsv::Layout> writer(layout);
                writer.open(filename, true, level);
                
                for (size_t i = 0; i < testRows; ++i) {
                    auto& row = writer.row();
                    row.set(0, static_cast<int32_t>(i));
                    row.set(1, sampleStrings_[i % sampleStrings_.size()]);
                    row.set(2, float_dist_(gen_));
                    row.set(3, "Data row " + std::to_string(i) + " with some additional text for compression testing");
                    writer.writeRow();
                }
                writer.close();
            }
            auto end = std::chrono::steady_clock::now();
            
            double timeMs = std::chrono::duration<double, std::milli>(end - start).count();
            size_t fileSize = std::filesystem::file_size(filename);
            size_t estimatedRaw = testRows * 50; // Rough estimate
            double ratio = static_cast<double>(estimatedRaw) / fileSize;
            double mbps = (fileSize / 1024.0 / 1024.0) / (timeMs / 1000.0);
            
            std::cout << std::setw(5) << level << " | " 
                      << std::setw(9) << std::fixed << std::setprecision(1) << timeMs << " | " 
                      << std::setw(12) << fileSize << " | " 
                      << std::setw(5) << std::setprecision(1) << ratio << " | " 
                      << std::setw(11) << std::setprecision(1) << mbps << "\n";
            
            std::filesystem::remove(filename);
        }
        
        std::cout << "\nCompression Notes:\n";
        std::cout << "  Level 0: No compression (fastest)\n";
        std::cout << "  Level 1: Fast compression (recommended default)\n";
        std::cout << "  Level 3: Balanced compression/speed\n";
        std::cout << "  Level 6: High compression\n";
        std::cout << "  Level 9: Maximum compression (slowest)\n\n";
    }
    
    void benchmarkCSVBaseline() {
        std::cout << "CSV Baseline Comparison\n";
        std::cout << "======================\n";
        
        const size_t testRows = 50000; // Smaller dataset for CSV comparison
        const std::string csvFilename = "baseline_test.csv";
        
        // Write CSV file
        auto csvWriteStart = std::chrono::steady_clock::now();
        {
            std::ofstream csv(csvFilename);
            csv << "id,name,score1,score2,active,timestamp,count,category\n";
            
            for (size_t i = 0; i < testRows; ++i) {
                csv << int_dist_(gen_) << ","
                    << sampleStrings_[i % sampleStrings_.size()] << ","
                    << float_dist_(gen_) << ","
                    << double_dist_(gen_) << ","
                    << ((i & 1) == 0 ? "true" : "false") << ","
                    << (i * 1000) << ","
                    << i << ","
                    << sampleStrings_[(i * 7) % sampleStrings_.size()] << "\n";
            }
        }
        auto csvWriteEnd = std::chrono::steady_clock::now();
        double csvWriteTime = std::chrono::duration<double, std::milli>(csvWriteEnd - csvWriteStart).count();
        
        // Read CSV file
        auto csvReadStart = std::chrono::steady_clock::now();
        {
            std::ifstream csv(csvFilename);
            std::string line;
            std::getline(csv, line); // Skip header
            
            while (std::getline(csv, line)) {
                // Simple parsing to simulate data access
                size_t pos = 0;
                for (int col = 0; col < 8; ++col) {
                    size_t nextPos = line.find(',', pos);
                    if (nextPos == std::string::npos) nextPos = line.length();
                    std::string value = line.substr(pos, nextPos - pos);
                    pos = nextPos + 1;
                    
                    // Simulate type conversion overhead
                    if (col == 0 || col == 5 || col == 6) {
                        volatile int dummy = std::stoi(value); (void)dummy;
                    } else if (col == 2) {
                        volatile float dummy = std::stof(value); (void)dummy;
                    } else if (col == 3) {
                        volatile double dummy = std::stod(value); (void)dummy;
                    }
                }
            }
        }
        auto csvReadEnd = std::chrono::steady_clock::now();
        double csvReadTime = std::chrono::duration<double, std::milli>(csvReadEnd - csvReadStart).count();
        
        // Write BCSV equivalent for comparison
        const std::string bcsvFilename = "baseline_test.bcsv";
        bcsv::Layout layout;
        layout.addColumn({"id", bcsv::ColumnType::INT32});
        layout.addColumn({"name", bcsv::ColumnType::STRING});
        layout.addColumn({"score1", bcsv::ColumnType::FLOAT});
        layout.addColumn({"score2", bcsv::ColumnType::DOUBLE});
        layout.addColumn({"active", bcsv::ColumnType::BOOL});
        layout.addColumn({"timestamp", bcsv::ColumnType::INT64});
        layout.addColumn({"count", bcsv::ColumnType::UINT32});
        layout.addColumn({"category", bcsv::ColumnType::STRING});
        
        auto bcsvWriteStart = std::chrono::steady_clock::now();
        {
            bcsv::Writer<bcsv::Layout> writer(layout);
            writer.open(bcsvFilename, true, 1);
            
            for (size_t i = 0; i < testRows; ++i) {
                auto& row = writer.row();
                row.set(0, int_dist_(gen_));
                row.set(1, sampleStrings_[i % sampleStrings_.size()]);
                row.set(2, float_dist_(gen_));
                row.set(3, double_dist_(gen_));
                row.set(4, (i & 1) == 0);
                row.set(5, static_cast<int64_t>(i * 1000));
                row.set(6, static_cast<uint32_t>(i));
                row.set(7, sampleStrings_[(i * 7) % sampleStrings_.size()]);
                writer.writeRow();
            }
            writer.close();
        }
        auto bcsvWriteEnd = std::chrono::steady_clock::now();
        double bcsvWriteTime = std::chrono::duration<double, std::milli>(bcsvWriteEnd - bcsvWriteStart).count();
        
        // Read BCSV
        auto bcsvReadStart = std::chrono::steady_clock::now();
        {
            bcsv::Reader<bcsv::Layout> reader;
            reader.open(bcsvFilename);
            
            while (reader.readNext()) {
                // Access all fields for fair comparison
                const auto& row = reader.row();
                volatile auto id = row.get<int32_t>(0);
                volatile auto name = row.get<std::string>(1);
                volatile auto score1 = row.get<float>(2);
                volatile auto score2 = row.get<double>(3);
                volatile auto active = row.get<bool>(4);
                volatile auto timestamp = row.get<int64_t>(5);
                volatile auto count = row.get<uint32_t>(6);
                volatile auto category = row.get<std::string>(7);
                
                (void)id; (void)name; (void)score1; (void)score2;
                (void)active; (void)timestamp; (void)count; (void)category;
            }
            reader.close();
        }
        auto bcsvReadEnd = std::chrono::steady_clock::now();
        double bcsvReadTime = std::chrono::duration<double, std::milli>(bcsvReadEnd - bcsvReadStart).count();
        
        // File sizes
        size_t csvSize = std::filesystem::file_size(csvFilename);
        size_t bcsvSize = std::filesystem::file_size(bcsvFilename);
        
        std::cout << "Testing " << testRows << " rows:\n\n";
        std::cout << "Format | Write (ms) | Read (ms) | Total (ms) | Size (bytes) | Size (MB)\n";
        std::cout << "-------|------------|-----------|------------|--------------|----------\n";
        
        double csvTotal = csvWriteTime + csvReadTime;
        double bcsvTotal = bcsvWriteTime + bcsvReadTime;
        
        std::cout << "CSV    | " << std::setw(10) << std::fixed << std::setprecision(1) << csvWriteTime
                  << " | " << std::setw(9) << csvReadTime
                  << " | " << std::setw(10) << csvTotal
                  << " | " << std::setw(12) << csvSize
                  << " | " << std::setw(8) << std::setprecision(2) << (csvSize / 1024.0 / 1024.0) << "\n";
        
        std::cout << "BCSV   | " << std::setw(10) << std::fixed << std::setprecision(1) << bcsvWriteTime
                  << " | " << std::setw(9) << bcsvReadTime
                  << " | " << std::setw(10) << bcsvTotal
                  << " | " << std::setw(12) << bcsvSize
                  << " | " << std::setw(8) << std::setprecision(2) << (bcsvSize / 1024.0 / 1024.0) << "\n";
        
        std::cout << "\nBCSV vs CSV Performance:\n";
        std::cout << "  Write speedup: " << std::fixed << std::setprecision(2) << (csvWriteTime / bcsvWriteTime) << "x\n";
        std::cout << "  Read speedup:  " << std::setprecision(2) << (csvReadTime / bcsvReadTime) << "x\n";
        std::cout << "  Total speedup: " << std::setprecision(2) << (csvTotal / bcsvTotal) << "x\n";
        std::cout << "  Size reduction: " << std::setprecision(1) << (100.0 - (bcsvSize * 100.0 / csvSize)) << "%\n\n";
        
        // Cleanup
        std::filesystem::remove(csvFilename);
        std::filesystem::remove(bcsvFilename);
    }
};

int main() {
    try {
        // Allow user to specify number of rows
        size_t numRows = 50000; // Use a default value that works with ZoH limits
        
        std::cout << "Starting performance benchmark with " << numRows << " rows...\n\n";
        
        PerformanceBenchmark benchmark(numRows);
        benchmark.runBenchmark();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
