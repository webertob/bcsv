#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <filesystem>
#include "bcsv/bcsv.h"

/**
 * BCSV Performance Benchmark
 * 
 * This benchmark compares the performance of flexible vs static interfaces
 * for large file operations. It tests both write and read performance
 * with configurable row counts and data complexity.
 */

class PerformanceBenchmark {
private:
    static constexpr size_t DEFAULT_NUM_ROWS = 100000;
    static constexpr const char* FLEXIBLE_FILENAME = "benchmark_flexible.bcsv";
    static constexpr const char* STATIC_FILENAME = "benchmark_static.bcsv";
    
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
        std::cout << "Rows to process: " << numRows_ << "\n";
        std::cout << "Columns per row: 8 (mixed data types)\n\n";
    }

    // Flexible interface benchmark
    std::pair<double, double> benchmarkFlexible() {
        std::cout << "Benchmarking Flexible Interface...\n";
        
        // Create flexible layout
        auto layout = bcsv::Layout::create();
        layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
        layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
        layout->insertColumn({"score1", bcsv::ColumnDataType::FLOAT});
        layout->insertColumn({"score2", bcsv::ColumnDataType::DOUBLE});
        layout->insertColumn({"active", bcsv::ColumnDataType::BOOL});
        layout->insertColumn({"timestamp", bcsv::ColumnDataType::INT64});
        layout->insertColumn({"count", bcsv::ColumnDataType::UINT32});
        layout->insertColumn({"category", bcsv::ColumnDataType::STRING});

        // Benchmark writing
        auto writeStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Writer<bcsv::Layout> writer(layout, FLEXIBLE_FILENAME, true);
            
            for (size_t i = 0; i < numRows_; ++i) {
                auto row = layout->createRow();
                (*row).set(0, int_dist_(gen_));
                (*row).set(1, sampleStrings_[i % sampleStrings_.size()]);
                (*row).set(2, float_dist_(gen_));
                (*row).set(3, double_dist_(gen_));
                (*row).set(4, (i % 2) == 0);
                (*row).set(5, static_cast<int64_t>(i * 1000));
                (*row).set(6, static_cast<uint32_t>(i));
                (*row).set(7, sampleStrings_[(i * 7) % sampleStrings_.size()]);
                writer.writeRow(*row);
            }
        }
        auto writeEnd = std::chrono::high_resolution_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();

        // Benchmark reading
        auto readStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Reader<bcsv::Layout> reader(layout, FLEXIBLE_FILENAME);
            bcsv::RowView rowView(layout);
            size_t rowCount = 0;
            
            while (reader.readRow(rowView)) {
                // Read all values to ensure fair comparison
                auto id = rowView.get<int32_t>(0);
                auto name = rowView.get<std::string>(1);
                auto score1 = rowView.get<float>(2);
                auto score2 = rowView.get<double>(3);
                auto active = rowView.get<bool>(4);
                auto timestamp = rowView.get<int64_t>(5);
                auto count = rowView.get<uint32_t>(6);
                auto category = rowView.get<std::string>(7);
                
                // Prevent optimization by using the values
                (void)id; (void)name; (void)score1; (void)score2; 
                (void)active; (void)timestamp; (void)count; (void)category;
                rowCount++;
            }
        }
        auto readEnd = std::chrono::high_resolution_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();

        std::cout << "  Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";

        return {writeTime, readTime};
    }

    // Static interface benchmark
    std::pair<double, double> benchmarkStatic() {
        std::cout << "Benchmarking Static Interface...\n";
        
        // Create static layout
        std::vector<std::string> columnNames = {
            "id", "name", "score1", "score2", "active", "timestamp", "count", "category"
        };
        auto layout = BenchmarkLayoutStatic::create(columnNames);

        // Benchmark writing
        auto writeStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Writer<BenchmarkLayoutStatic> writer(layout, STATIC_FILENAME, true);
            
            for (size_t i = 0; i < numRows_; ++i) {
                auto row = layout->createRow();
                (*row).set<0>(int_dist_(gen_));
                (*row).set<1>(sampleStrings_[i % sampleStrings_.size()]);
                (*row).set<2>(float_dist_(gen_));
                (*row).set<3>(double_dist_(gen_));
                (*row).set<4>((i % 2) == 0);
                (*row).set<5>(static_cast<int64_t>(i * 1000));
                (*row).set<6>(static_cast<uint32_t>(i));
                (*row).set<7>(sampleStrings_[(i * 7) % sampleStrings_.size()]);
                writer.writeRow(*row);
            }
        }
        auto writeEnd = std::chrono::high_resolution_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();

        // Benchmark reading
        auto readStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Reader<BenchmarkLayoutStatic> reader(layout, STATIC_FILENAME);
            typename BenchmarkLayoutStatic::RowViewType rowView(layout);
            size_t rowCount = 0;
            
            while (reader.readRow(rowView)) {
                // Read all values to ensure fair comparison
                auto id = rowView.get<0>();
                auto name = rowView.get<1>();
                auto score1 = rowView.get<2>();
                auto score2 = rowView.get<3>();
                auto active = rowView.get<4>();
                auto timestamp = rowView.get<5>();
                auto count = rowView.get<6>();
                auto category = rowView.get<7>();
                
                // Prevent optimization by using the values
                (void)id; (void)name; (void)score1; (void)score2; 
                (void)active; (void)timestamp; (void)count; (void)category;
                rowCount++;
            }
        }
        auto readEnd = std::chrono::high_resolution_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();

        std::cout << "  Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";

        return {writeTime, readTime};
    }

    void printSummary(const std::pair<double, double>& flexibleTimes, 
                     const std::pair<double, double>& staticTimes) {
        std::cout << "Performance Summary\n";
        std::cout << "==================\n\n";

        // File size comparison
        auto flexibleSize = std::filesystem::file_size(FLEXIBLE_FILENAME);
        auto staticSize = std::filesystem::file_size(STATIC_FILENAME);
        
        std::cout << "File Sizes:\n";
        std::cout << "  Flexible: " << flexibleSize << " bytes\n";
        std::cout << "  Static:   " << staticSize << " bytes\n";
        std::cout << "  Difference: " << (flexibleSize == staticSize ? "None (binary compatible)" : "Different") << "\n\n";

        // Performance comparison
        double flexibleTotal = flexibleTimes.first + flexibleTimes.second;
        double staticTotal = staticTimes.first + staticTimes.second;
        double speedup = flexibleTotal / staticTotal;

        std::cout << "Performance Comparison:\n";
        std::cout << "  Flexible interface total: " << std::fixed << std::setprecision(2) << flexibleTotal << " ms\n";
        std::cout << "  Static interface total:   " << std::fixed << std::setprecision(2) << staticTotal << " ms\n";
        std::cout << "  Static speedup:           " << std::fixed << std::setprecision(2) << speedup << "x faster\n\n";

        // Write performance
        double writeSpeedup = flexibleTimes.first / staticTimes.first;
        std::cout << "  Write performance:\n";
        std::cout << "    Flexible: " << std::fixed << std::setprecision(2) << flexibleTimes.first << " ms\n";
        std::cout << "    Static:   " << std::fixed << std::setprecision(2) << staticTimes.first << " ms\n";
        std::cout << "    Speedup:  " << std::fixed << std::setprecision(2) << writeSpeedup << "x\n\n";

        // Read performance
        double readSpeedup = flexibleTimes.second / staticTimes.second;
        std::cout << "  Read performance:\n";
        std::cout << "    Flexible: " << std::fixed << std::setprecision(2) << flexibleTimes.second << " ms\n";
        std::cout << "    Static:   " << std::fixed << std::setprecision(2) << staticTimes.second << " ms\n";
        std::cout << "    Speedup:  " << std::fixed << std::setprecision(2) << readSpeedup << "x\n\n";

        // Throughput
        double flexibleThroughput = numRows_ / (flexibleTotal / 1000.0);
        double staticThroughput = numRows_ / (staticTotal / 1000.0);
        
        std::cout << "Throughput (rows/second):\n";
        std::cout << "  Flexible: " << std::fixed << std::setprecision(0) << flexibleThroughput << "\n";
        std::cout << "  Static:   " << std::fixed << std::setprecision(0) << staticThroughput << "\n\n";
    }

    void runBenchmark() {
        // Run benchmarks
        auto flexibleTimes = benchmarkFlexible();
        auto staticTimes = benchmarkStatic();
        
        // Print summary
        printSummary(flexibleTimes, staticTimes);
        
        // Cleanup
        std::filesystem::remove(FLEXIBLE_FILENAME);
        std::filesystem::remove(STATIC_FILENAME);
        
        std::cout << "Benchmark completed successfully!\n";
    }
};

int main() {
    try {
        // Allow user to specify number of rows
        size_t numRows = 100000; // Use a default value instead of accessing private member
        
        std::cout << "Starting performance benchmark with " << numRows << " rows...\n\n";
        
        PerformanceBenchmark benchmark(numRows);
        benchmark.runBenchmark();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
