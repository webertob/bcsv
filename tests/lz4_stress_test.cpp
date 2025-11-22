#include <gtest/gtest.h>
#include <bcsv/lz4_stream.hpp>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <thread>
#include <future>

using namespace bcsv;

class LZ4StressTest : public ::testing::Test {
protected:
    // Helper to generate random data efficiently
    // We generate a large block once and slice it to avoid rand() overhead
    std::vector<std::byte> random_pool;
    const size_t POOL_SIZE = 64 * 1024 * 1024; // 64MB pool

    // New Time Series Pool
    std::vector<std::byte> time_series_pool;
    const size_t TS_POOL_SIZE = 64 * 1024 * 1024; // 64MB pool

    void SetUp() override {
        // Random Pool Setup
        random_pool.resize(POOL_SIZE);
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            random_pool[i] = static_cast<std::byte>(dist(rng));
        }

        // Time Series Pool Setup
        time_series_pool.resize(TS_POOL_SIZE);
        size_t offset = 0;
        int64_t timestamp = 1600000000000; // Arbitrary start time
        double value = 100.0;
        std::mt19937 rng_ts(123);
        std::normal_distribution<double> dist_val(0.0, 0.5); // Small changes for compressibility

        while (offset + 16 <= TS_POOL_SIZE) {
            // Timestamp (8 bytes)
            std::memcpy(time_series_pool.data() + offset, &timestamp, sizeof(timestamp));
            offset += 8;
            timestamp += 10; // 10ms steps

            // Value (8 bytes) - Random Walk
            value += dist_val(rng_ts);
            std::memcpy(time_series_pool.data() + offset, &value, sizeof(value));
            offset += 8;
        }
    }

    std::span<const std::byte> getRandomSpan(size_t size, size_t offset_seed) {
        size_t offset = offset_seed % (POOL_SIZE - size);
        return std::span<const std::byte>(random_pool.data() + offset, size);
    }

    std::span<const std::byte> getTimeSeriesSpan(size_t size, size_t offset_seed) {
        // Align offset to 16 bytes to preserve the structure (Timestamp + Value)
        size_t offset = (offset_seed % (TS_POOL_SIZE - size));
        offset = (offset / 16) * 16;
        if (offset + size > TS_POOL_SIZE) offset = 0;
        return std::span<const std::byte>(time_series_pool.data() + offset, size);
    }

    void RunPatternTest(const std::vector<size_t>& sizes, const std::string& test_name) {
        LZ4CompressionStream<> compressor;
        LZ4DecompressionStream<> decompressor;
        std::vector<std::vector<std::byte>> compressed_data;
        
        // Compress
        for (size_t i = 0; i < sizes.size(); ++i) {
            auto input = getRandomSpan(sizes[i], i * 123);
            compressed_data.push_back(compressor.compress(input));
        }

        // Decompress
        for (size_t i = 0; i < sizes.size(); ++i) {
            auto output = decompressor.decompress(compressed_data[i]);
            ASSERT_EQ(output.size(), sizes[i]) << test_name << " index " << i;
            auto input = getRandomSpan(sizes[i], i * 123);
            if (std::memcmp(output.data(), input.data(), sizes[i]) != 0) {
                FAIL() << test_name << " content mismatch at index " << i;
            }
        }
    }
};

// 1. Comprehensive Test
// "Compress and decompress about 5 million streams of 256-1096 packages of random size between 1byte and 16MB."
// NOTE: Running 5 million streams of this magnitude is petabytes of data. 
// We will default to a smaller number for CI, but allow scaling.
TEST_F(LZ4StressTest, ComprehensiveRandomStream) {
    // Configuration
    const size_t NUM_STREAMS = 50; // Increased from 5 to 50
    const size_t MIN_PACKAGES = 256;
    const size_t MAX_PACKAGES = 1096;
    const size_t MIN_SIZE = 1;
    const size_t MAX_SIZE = 16 * 1024 * 1024; // 16MB

    std::mt19937 rng(12345);
    std::uniform_int_distribution<size_t> pkg_count_dist(MIN_PACKAGES, MAX_PACKAGES);
    // Use a log-uniform distribution or similar? The prompt says "random size", usually implies uniform.
    // But 16MB uniform is huge. We'll stick to uniform as requested but be aware of time.
    std::uniform_int_distribution<size_t> size_dist(MIN_SIZE, MAX_SIZE);

    LZ4CompressionStream<> compressor;
    LZ4DecompressionStream<> decompressor;

    std::vector<std::vector<std::byte>> compressed_stream_data;
    std::vector<size_t> original_sizes;

    std::cout << "[ INFO     ] Running Comprehensive Random Stream Test with " << NUM_STREAMS << " streams." << std::endl;

    for (size_t s = 0; s < NUM_STREAMS; ++s) {
        compressor.reset();
        decompressor.reset();
        compressed_stream_data.clear();
        original_sizes.clear();

        size_t num_packages = pkg_count_dist(rng);
        
        // --- Compression Phase ---
        for (size_t p = 0; p < num_packages; ++p) {
            size_t pkg_size = size_dist(rng);
            auto input_span = getRandomSpan(pkg_size, p * 1024 + s); // Pseudo-random offset
            
            original_sizes.push_back(pkg_size);
            compressed_stream_data.push_back(compressor.compress(input_span));
        }

        // --- Decompression & Verification Phase ---
        for (size_t p = 0; p < num_packages; ++p) {
            auto decompressed_span = decompressor.decompress(compressed_stream_data[p]);
            
            // Verify size
            ASSERT_EQ(decompressed_span.size(), original_sizes[p]) 
                << "Stream " << s << " Package " << p << " size mismatch";

            // Verify content
            auto original_span = getRandomSpan(original_sizes[p], p * 1024 + s);
            if (std::memcmp(decompressed_span.data(), original_span.data(), original_sizes[p]) != 0) {
                FAIL() << "Stream " << s << " Package " << p << " content mismatch";
            }
        }

        if (s % 5 == 0 && s > 0) {
            std::cout << "Processed " << s << " streams..." << std::endl;
        }
    }
}

TEST_F(LZ4StressTest, CornerCasePatterns) {
    // Pattern 1: Alternating Small/Huge
    // Forces transitions between Ring Buffer (Case 1/2) and Zero-Copy (Case 3)
    std::vector<size_t> alternating_sizes;
    for (int i = 0; i < 50; ++i) {
        alternating_sizes.push_back(1024);          // 1KB (Fits)
        alternating_sizes.push_back(200 * 1024);    // 200KB (Fallback)
    }
    RunPatternTest(alternating_sizes, "Alternating Small/Huge");

    // Pattern 2: Buffer Boundary Hover
    // BUFFER_SIZE is ~128KB + 64. DICT_SIZE is 64KB.
    // We want to fill the buffer, then wrap, then fill again.
    std::vector<size_t> boundary_sizes;
    boundary_sizes.push_back(64 * 1024); // Fill half
    boundary_sizes.push_back(64 * 1024); // Fill almost full (128KB total)
    boundary_sizes.push_back(1024);      // Should wrap (Case 2)
    boundary_sizes.push_back(64 * 1024); // Fill half again
    boundary_sizes.push_back(200 * 1024);// Huge (Case 3)
    boundary_sizes.push_back(1024);      // Back to small (Case 1, new dict)
    RunPatternTest(boundary_sizes, "Buffer Boundary Hover");

    // Pattern 3: Ramp Up/Down
    std::vector<size_t> ramp_sizes;
    for (size_t s = 1; s <= 1024 * 1024; s *= 2) ramp_sizes.push_back(s);
    for (size_t s = 1024 * 1024; s >= 1; s /= 2) ramp_sizes.push_back(s);
    RunPatternTest(ramp_sizes, "Ramp Up/Down");
}

TEST_F(LZ4StressTest, ParallelExecution) {
    const int NUM_THREADS = 8;
    const int STREAMS_PER_THREAD = 5;
    
    std::cout << "[ INFO     ] Running Parallel Stress Test with " << NUM_THREADS << " threads." << std::endl;

    std::vector<std::future<void>> futures;
    for (int t = 0; t < NUM_THREADS; ++t) {
        futures.push_back(std::async(std::launch::async, [this, t]() {
            std::mt19937 rng(t * 999 + 1);
            std::uniform_int_distribution<size_t> size_dist(1, 1024 * 1024); // Up to 1MB for speed
            
            LZ4CompressionStream<> compressor;
            LZ4DecompressionStream<> decompressor;
            
            for (int s = 0; s < STREAMS_PER_THREAD; ++s) {
                compressor.reset();
                decompressor.reset();
                std::vector<std::vector<std::byte>> compressed_data;
                std::vector<size_t> sizes;
                
                // Compress 100 packets
                for (int p = 0; p < 100; ++p) {
                    size_t sz = size_dist(rng);
                    sizes.push_back(sz);
                    auto input = getRandomSpan(sz, t*s*p);
                    compressed_data.push_back(compressor.compress(input));
                }
                
                // Decompress
                for (int p = 0; p < 100; ++p) {
                    auto output = decompressor.decompress(compressed_data[p]);
                    if (output.size() != sizes[p]) {
                        throw std::runtime_error("Size mismatch in parallel test");
                    }
                    auto input = getRandomSpan(sizes[p], t*s*p);
                    if (std::memcmp(output.data(), input.data(), sizes[p]) != 0) {
                        throw std::runtime_error("Content mismatch in parallel test");
                    }
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.get(); // Will throw if any thread threw exception
    }
}

// 2. Benchmark
// "5 million streams, with 25, 50 and 100 packages, with 32...4096 packet size each."
TEST_F(LZ4StressTest, Benchmark) {
    const size_t NUM_STREAMS = 10; // Reduced for CI. Set to 5000000 for full benchmark.
    
    struct Config {
        size_t packages_per_stream;
        size_t packet_size;
    };

    std::vector<size_t> pkg_counts = {5, 25, 50, 100, 256};
    std::vector<size_t> pkt_sizes = {
        32, 64, 128, 256, 512, 1024, 2048, 4096,
        8192, 16384, 32768, 65536, 131072, 262144, 524288, 
        1024 * 1024, 2 * 1024 * 1024, 4 * 1024 * 1024, 8 * 1024 * 1024, 16 * 1024 * 1024
    };

    std::cout << "\n========================================================================================================================" << std::endl;
    std::cout << " BENCHMARK REPORT (Simulated " << NUM_STREAMS << " streams per config, Time-Series Data)" << std::endl;
    std::cout << "========================================================================================================================" << std::endl;
    std::cout << std::left << std::setw(10) << "Pkts/Str" 
              << std::setw(10) << "Size(B)" 
              << std::setw(15) << "MB/sec" 
              << std::setw(15) << "Pkts/sec" 
              << std::setw(15) << "Ratio"
              << std::setw(15) << "Lat 1%(us)" 
              << std::setw(15) << "Lat 50%(us)" 
              << std::setw(15) << "Lat 99%(us)" << std::endl;
    std::cout << "------------------------------------------------------------------------------------------------------------------------" << std::endl;

    LZ4CompressionStream<> compressor;
    LZ4DecompressionStream<> decompressor;

    // Pre-allocate vectors for latencies to avoid allocation during measurement
    std::vector<double> latencies;
    latencies.reserve(NUM_STREAMS * 100);

    // Reusable buffer for compression to avoid repeated allocations
    std::vector<std::byte> compressed_buffer;
    // Reserve enough for largest packet + overhead
    compressed_buffer.reserve(16 * 1024 * 1024 + 1024); 

    for (size_t pkg_count : pkg_counts) {
        for (size_t pkt_size : pkt_sizes) {
            compressor.reset();
            decompressor.reset();
            latencies.clear();

            auto start_total = std::chrono::high_resolution_clock::now();
            size_t total_bytes_processed = 0;
            size_t total_compressed_bytes = 0;
            size_t total_packets = 0;

            for (size_t s = 0; s < NUM_STREAMS; ++s) {
                compressor.reset();
                decompressor.reset();

                for (size_t p = 0; p < pkg_count; ++p) {
                    auto current_input = getTimeSeriesSpan(pkt_size, s * pkg_count + p);

                    auto t1 = std::chrono::high_resolution_clock::now();
                    
                    // Compress
                    compressed_buffer.clear();
                    compressor.compress(current_input, compressed_buffer);
                    
                    // Decompress
                    auto decompressed = decompressor.decompress(compressed_buffer);
                    
                    // Prevent optimization
                    if (decompressed.empty() && pkt_size > 0) {
                        FAIL() << "Decompression returned empty";
                    }

                    auto t2 = std::chrono::high_resolution_clock::now();
                    
                    std::chrono::duration<double, std::micro> lat_us = t2 - t1;
                    latencies.push_back(lat_us.count());
                    
                    total_bytes_processed += pkt_size;
                    total_compressed_bytes += compressed_buffer.size();
                    total_packets++;
                }
            }

            auto end_total = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> total_sec = end_total - start_total;

            // Calculate Metrics
            double mb_sec = (double)total_bytes_processed / (1024.0 * 1024.0) / total_sec.count();
            double pkts_sec = (double)total_packets / total_sec.count();
            double ratio = (total_bytes_processed > 0) ? (double)total_compressed_bytes / (double)total_bytes_processed : 0.0;

            // Percentiles
            std::sort(latencies.begin(), latencies.end());
            double p1 = latencies[latencies.size() * 0.01];
            double p50 = latencies[latencies.size() * 0.50];
            double p99 = latencies[latencies.size() * 0.99];

            std::cout << std::left << std::setw(10) << pkg_count 
                      << std::setw(10) << pkt_size 
                      << std::fixed << std::setprecision(2) << std::setw(15) << mb_sec 
                      << std::setw(15) << pkts_sec 
                      << std::setw(15) << ratio
                      << std::setprecision(3) << std::setw(15) << p1 
                      << std::setw(15) << p50 
                      << std::setw(15) << p99 << std::endl;
        }
    }
    std::cout << "========================================================================================================================" << std::endl;
}
