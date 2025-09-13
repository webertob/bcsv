#include <iostream>
#include <memory>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "bcsv/bcsv.h"

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

class LargeScaleBenchmark {
private:
    static constexpr size_t NUM_ROWS = 500000;
    static constexpr size_t COLUMNS_PER_TYPE = 6;
    static constexpr const char* CSV_FILENAME = "large_test.csv";
    static constexpr const char* BCSV_FLEXIBLE_FILENAME = "large_flexible.bcsv";
    static constexpr const char* BCSV_STATIC_FILENAME = "large_static.bcsv";
    
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_int_distribution<int32_t> int32_dist_;
    std::uniform_int_distribution<int64_t> int64_dist_;
    std::uniform_int_distribution<uint32_t> uint32_dist_;
    std::uniform_int_distribution<uint64_t> uint64_dist_;
    std::uniform_int_distribution<int> int8_dist_;
    std::uniform_int_distribution<int> int16_dist_;
    std::uniform_int_distribution<int> uint8_dist_;
    std::uniform_int_distribution<int> uint16_dist_;
    std::uniform_real_distribution<float> float_dist_;
    std::uniform_real_distribution<double> double_dist_;
    std::uniform_int_distribution<int> bool_dist_;
    std::vector<std::string> sampleStrings_;

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

    LargeScaleBenchmark() 
        : gen_(42) // Fixed seed for reproducible results
        , int32_dist_(-1000000, 1000000)
        , int64_dist_(-1000000000LL, 1000000000LL)
        , uint32_dist_(0, 2000000)
        , uint64_dist_(0, 2000000000ULL)
        , int8_dist_(-128, 127)
        , int16_dist_(-32768, 32767)
        , uint8_dist_(0, 255)
        , uint16_dist_(0, 65535)
        , float_dist_(-1000.0f, 1000.0f)
        , double_dist_(-10000.0, 10000.0)
        , bool_dist_(0, 1)
    {
        // Generate diverse sample strings
        sampleStrings_ = {
            "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota", "Kappa",
            "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma", "Tau", "Upsilon",
            "Phi", "Chi", "Psi", "Omega", "ProductA", "ProductB", "CategoryX", "CategoryY",
            "DepartmentSales", "DepartmentIT", "LocationNY", "LocationCA", "StatusActive", "StatusInactive",
            "Very Long Product Name With Multiple Words And Detailed Description",
            "Short", "", "NULL", "UNDEFINED", "TempData123", "TempData456", "TempData789"
        };
        
        std::cout << "Large Scale BCSV Performance Benchmark\n";
        std::cout << "=====================================\n";
        std::cout << "Test Configuration:\n";
        std::cout << "  Rows: " << NUM_ROWS << "\n";
        std::cout << "  Columns: " << (COLUMNS_PER_TYPE * 8) << " (6 per data type)\n";
        std::cout << "  Data types: BOOL(6), INT8(6), INT16(6), INT32(6), INT64(6), UINT8(6), UINT16(6), UINT32(6), UINT64(6), FLOAT(6), DOUBLE(6), STRING(6)\n";
        std::cout << "  Compression: LZ4 Level 1\n";
        std::cout << "  Platform: " << sizeof(void*) * 8 << "-bit\n\n";
    }

    // Generate test data for a single row
    struct RowData {
        std::array<bool, 6> bools;
        std::array<int8_t, 6> int8s;
        std::array<int16_t, 6> int16s;
        std::array<int32_t, 6> int32s;
        std::array<int64_t, 6> int64s;
        std::array<uint8_t, 6> uint8s;
        std::array<uint16_t, 6> uint16s;
        std::array<uint32_t, 6> uint32s;
        std::array<uint64_t, 6> uint64s;
        std::array<float, 6> floats;
        std::array<double, 6> doubles;
        std::array<std::string, 6> strings;
    };

    RowData generateRowData(size_t rowIndex) {
        RowData data;
        
        for (size_t i = 0; i < 6; ++i) {
            data.bools[i] = bool_dist_(gen_) == 1;
            data.int8s[i] = static_cast<int8_t>(int8_dist_(gen_));
            data.int16s[i] = static_cast<int16_t>(int16_dist_(gen_));
            data.int32s[i] = int32_dist_(gen_);
            data.int64s[i] = int64_dist_(gen_);
            data.uint8s[i] = static_cast<uint8_t>(uint8_dist_(gen_));
            data.uint16s[i] = static_cast<uint16_t>(uint16_dist_(gen_));
            data.uint32s[i] = uint32_dist_(gen_);
            data.uint64s[i] = uint64_dist_(gen_);
            data.floats[i] = float_dist_(gen_);
            data.doubles[i] = double_dist_(gen_);
            data.strings[i] = sampleStrings_[(rowIndex * 7 + i) % sampleStrings_.size()] + "_" + std::to_string(rowIndex) + "_" + std::to_string(i);
        }
        
        return data;
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

    // Populate flexible row
    void populateFlexibleRow(bcsv::Writer<bcsv::Layout>& writer, const RowData& data) {
        size_t colIdx = 0;
        
        auto& row = writer.row();
        // Populate all columns in order
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.bools[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.int8s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.int16s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.int32s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.int64s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.uint8s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.uint16s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.uint32s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.uint64s[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.floats[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.doubles[i]);
        for (size_t i = 0; i < 6; ++i) row.set(colIdx++, data.strings[i]);
    }

    // Populate static row (using template magic)
    void populateStaticRow(bcsv::Writer<LargeTestLayoutStatic>& writer, const RowData& data) {
        auto& row = writer.row();
        // Using template indices for static setting
        row.set<0>(data.bools[0]); row.set<1>(data.bools[1]); row.set<2>(data.bools[2]); 
        row.set<3>(data.bools[3]); row.set<4>(data.bools[4]); row.set<5>(data.bools[5]);
        
        row.set<6>(data.int8s[0]); row.set<7>(data.int8s[1]); row.set<8>(data.int8s[2]); 
        row.set<9>(data.int8s[3]); row.set<10>(data.int8s[4]); row.set<11>(data.int8s[5]);

        row.set<12>(data.int16s[0]); row.set<13>(data.int16s[1]); row.set<14>(data.int16s[2]); 
        row.set<15>(data.int16s[3]); row.set<16>(data.int16s[4]); row.set<17>(data.int16s[5]);

        row.set<18>(data.int32s[0]); row.set<19>(data.int32s[1]); row.set<20>(data.int32s[2]); 
        row.set<21>(data.int32s[3]); row.set<22>(data.int32s[4]); row.set<23>(data.int32s[5]);
        
        row.set<24>(data.int64s[0]); row.set<25>(data.int64s[1]); row.set<26>(data.int64s[2]); 
        row.set<27>(data.int64s[3]); row.set<28>(data.int64s[4]); row.set<29>(data.int64s[5]);
        
        row.set<30>(data.uint8s[0]); row.set<31>(data.uint8s[1]); row.set<32>(data.uint8s[2]); 
        row.set<33>(data.uint8s[3]); row.set<34>(data.uint8s[4]); row.set<35>(data.uint8s[5]);

        row.set<36>(data.uint16s[0]); row.set<37>(data.uint16s[1]); row.set<38>(data.uint16s[2]); 
        row.set<39>(data.uint16s[3]); row.set<40>(data.uint16s[4]); row.set<41>(data.uint16s[5]);

        row.set<42>(data.uint32s[0]); row.set<43>(data.uint32s[1]); row.set<44>(data.uint32s[2]); 
        row.set<45>(data.uint32s[3]); row.set<46>(data.uint32s[4]); row.set<47>(data.uint32s[5]);

        row.set<48>(data.uint64s[0]); row.set<49>(data.uint64s[1]); row.set<50>(data.uint64s[2]); 
        row.set<51>(data.uint64s[3]); row.set<52>(data.uint64s[4]); row.set<53>(data.uint64s[5]);

        row.set<54>(data.floats[0]); row.set<55>(data.floats[1]); row.set<56>(data.floats[2]); 
        row.set<57>(data.floats[3]); row.set<58>(data.floats[4]); row.set<59>(data.floats[5]);

        row.set<60>(data.doubles[0]); row.set<61>(data.doubles[1]); row.set<62>(data.doubles[2]); 
        row.set<63>(data.doubles[3]); row.set<64>(data.doubles[4]); row.set<65>(data.doubles[5]);
        
        row.set<66>(data.strings[0]); row.set<67>(data.strings[1]); row.set<68>(data.strings[2]); 
        row.set<69>(data.strings[3]); row.set<70>(data.strings[4]); row.set<71>(data.strings[5]);
    }

    // CSV benchmark
    std::pair<double, double> benchmarkCSV() {
        std::cout << "Benchmarking CSV format...\n";
        
        // Write CSV
        auto writeStart = std::chrono::high_resolution_clock::now();
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
                auto data = generateRowData(row);
                
                bool first = true;
                auto writeValue = [&](const auto& val) {
                    if (!first) csv << ",";
                    first = false;
                    csv << val;
                };
                
                for (size_t i = 0; i < 6; ++i) writeValue(data.bools[i] ? "true" : "false");
                for (size_t i = 0; i < 6; ++i) writeValue(static_cast<int>(data.int8s[i]));
                for (size_t i = 0; i < 6; ++i) writeValue(data.int16s[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(data.int32s[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(data.int64s[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(static_cast<int>(data.uint8s[i]));
                for (size_t i = 0; i < 6; ++i) writeValue(data.uint16s[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(data.uint32s[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(data.uint64s[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(data.floats[i]);
                for (size_t i = 0; i < 6; ++i) writeValue(data.doubles[i]);
                for (size_t i = 0; i < 6; ++i) {
                    csv << ",\"" << data.strings[i] << "\"";
                }
                csv << "\n";
                
                if (row % 50000 == 0) {
                    std::cout << "  CSV Progress: " << row << "/" << NUM_ROWS << " rows written\n";
                }
            }
        }
        auto writeEnd = std::chrono::high_resolution_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
        
        // Read CSV
        auto readStart = std::chrono::high_resolution_clock::now();
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
                    volatile int dummy = cell.length(); (void)dummy;
                    ++colCount;
                }
                ++rowCount;
                
                if (rowCount % 50000 == 0) {
                    std::cout << "  CSV Progress: " << rowCount << "/" << NUM_ROWS << " rows read\n";
                }
            }
        }
        auto readEnd = std::chrono::high_resolution_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();
        
        std::cout << "  CSV Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  CSV Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        
        return {writeTime, readTime};
    }

    // BCSV Flexible benchmark
    std::pair<double, double> benchmarkBCSVFlexible() {
        std::cout << "Benchmarking BCSV Flexible interface...\n";
        
        auto layout = createFlexibleLayout();
        
        // Write BCSV Flexible
        auto writeStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Writer<bcsv::Layout> writer(layout);
            writer.open(BCSV_FLEXIBLE_FILENAME, true, 1); // Compression level 1
            
            for (size_t row = 0; row < NUM_ROWS; ++row) {
                auto data = generateRowData(row);
                populateFlexibleRow(writer, data);
                writer.writeRow();
                
                if (row % 50000 == 0) {
                    std::cout << "  BCSV Flexible Progress: " << row << "/" << NUM_ROWS << " rows written\n";
                }
            }
            writer.close();
        }
        auto writeEnd = std::chrono::high_resolution_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
        
        // Read BCSV Flexible
        auto readStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Reader<bcsv::Layout> reader;
            reader.open(BCSV_FLEXIBLE_FILENAME);
            
            size_t rowCount = 0;
            while (reader.readNext()) {
                // Read all values for fair comparison
                const auto& row = reader.row();
                for (size_t col = 0; col < 72; ++col) {
                    auto val = row.get<bcsv::ValueType>(col);
                    volatile size_t dummy = std::visit([](const auto& v) { return sizeof(v); }, val);
                    (void)dummy;
                }
                ++rowCount;
                
                if (rowCount % 50000 == 0) {
                    std::cout << "  BCSV Flexible Progress: " << rowCount << "/" << NUM_ROWS << " rows read\n";
                }
            }
            reader.close();
        }
        auto readEnd = std::chrono::high_resolution_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();
        
        std::cout << "  BCSV Flexible Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  BCSV Flexible Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        
        return {writeTime, readTime};
    }

    // BCSV Static benchmark
    std::pair<double, double> benchmarkBCSVStatic() {
        std::cout << "Benchmarking BCSV Static interface...\n";
        
        auto layout = createStaticLayout();
        
        // Write BCSV Static
        auto writeStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Writer<LargeTestLayoutStatic> writer(layout);
            writer.open(BCSV_STATIC_FILENAME, true, 1); // Compression level 1
            
            for (size_t row = 0; row < NUM_ROWS; ++row) {
                auto data = generateRowData(row);
                populateStaticRow(writer, data);
                writer.writeRow();
                
                if (row % 50000 == 0) {
                    std::cout << "  BCSV Static Progress: " << row << "/" << NUM_ROWS << " rows written\n";
                }
            }
            writer.close();
        }
        auto writeEnd = std::chrono::high_resolution_clock::now();
        double writeTime = std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
        
        // Read BCSV Static
        auto readStart = std::chrono::high_resolution_clock::now();
        {
            bcsv::Reader<LargeTestLayoutStatic> reader;
            reader.open(BCSV_STATIC_FILENAME);
            
            size_t rowCount = 0;
            while (reader.readNext()) {
                // Read all values using template indices for fair comparison
                const auto& row = reader.row();
                volatile const auto& v0 = row.get<0>();
                volatile const auto& v1 = row.get<1>(); 
                volatile const auto& v2 = row.get<2>();
                volatile const auto& v3 = row.get<3>();
                volatile const auto& v4 = row.get<4>();
                volatile const auto& v5 = row.get<5>();
                volatile const auto& v6 = row.get<6>();
                volatile const auto& v7 = row.get<7>();
                volatile const auto& v8 = row.get<8>();
                volatile const auto& v9 = row.get<9>();
                volatile const auto& v10 = row.get<10>();
                volatile const auto& v11 = row.get<11>();
                volatile const auto& v12 = row.get<12>();
                volatile const auto& v13 = row.get<13>();
                volatile const auto& v14 = row.get<14>();
                volatile const auto& v15 = row.get<15>();
                volatile const auto& v16 = row.get<16>();
                volatile const auto& v17 = row.get<17>();
                volatile const auto& v18 = row.get<18>();
                volatile const auto& v19 = row.get<19>();
                volatile const auto& v20 = row.get<20>();
                volatile const auto& v21 = row.get<21>();
                volatile const auto& v22 = row.get<22>();
                volatile const auto& v23 = row.get<23>();
                volatile const auto& v24 = row.get<24>();
                volatile const auto& v25 = row.get<25>();
                volatile const auto& v26 = row.get<26>();
                volatile const auto& v27 = row.get<27>();
                volatile const auto& v28 = row.get<28>();
                volatile const auto& v29 = row.get<29>();
                volatile const auto& v30 = row.get<30>();
                volatile const auto& v31 = row.get<31>();
                volatile const auto& v32 = row.get<32>();
                volatile const auto& v33 = row.get<33>();
                volatile const auto& v34 = row.get<34>();
                volatile const auto& v35 = row.get<35>();
                volatile const auto& v36 = row.get<36>();
                volatile const auto& v37 = row.get<37>();
                volatile const auto& v38 = row.get<38>();
                volatile const auto& v39 = row.get<39>();
                volatile const auto& v40 = row.get<40>();
                volatile const auto& v41 = row.get<41>();
                volatile const auto& v42 = row.get<42>();
                volatile const auto& v43 = row.get<43>();
                volatile const auto& v44 = row.get<44>();
                volatile const auto& v45 = row.get<45>();
                volatile const auto& v46 = row.get<46>();
                volatile const auto& v47 = row.get<47>();
                volatile const auto& v48 = row.get<48>();
                volatile const auto& v49 = row.get<49>();
                volatile const auto& v50 = row.get<50>();
                volatile const auto& v51 = row.get<51>();
                volatile const auto& v52 = row.get<52>();
                volatile const auto& v53 = row.get<53>();
                volatile const auto& v54 = row.get<54>();
                volatile const auto& v55 = row.get<55>();
                volatile const auto& v56 = row.get<56>();
                volatile const auto& v57 = row.get<57>();
                volatile const auto& v58 = row.get<58>();
                volatile const auto& v59 = row.get<59>();
                volatile const auto& v60 = row.get<60>();
                volatile const auto& v61 = row.get<61>();
                volatile const auto& v62 = row.get<62>();
                volatile const auto& v63 = row.get<63>();
                volatile const auto& v64 = row.get<64>();
                volatile const auto& v65 = row.get<65>();
                volatile const auto& v66 = row.get<66>();
                volatile const auto& v67 = row.get<67>();
                volatile const auto& v68 = row.get<68>();
                volatile const auto& v69 = row.get<69>();
                volatile const auto& v70 = row.get<70>();
                volatile const auto& v71 = row.get<71>();
                // Continue pattern for performance but simplified for brevity
                (void)v0; (void)v1; (void)v2; (void)v3; (void)v4; (void)v5; (void)v6; (void)v7; (void)v8; (void)v9; (void)v10; (void)v11; (void)v12; (void)v13; (void)v14; (void)v15; (void)v16; (void)v17; (void)v18; (void)v19; (void)v20; (void)v21; (void)v22; (void)v23; (void)v24; (void)v25; (void)v26; (void)v27; (void)v28; (void)v29; (void)v30; (void)v31; (void)v32; (void)v33; (void)v34; (void)v35; (void)v36; (void)v37; (void)v38; (void)v39; (void)v40; (void)v41; (void)v42; (void)v43; (void)v44; (void)v45; (void)v46; (void)v47; (void)v48; (void)v49; (void)v50; (void)v51; (void)v52; (void)v53; (void)v54; (void)v55; (void)v56; (void)v57; (void)v58; (void)v59; (void)v60; (void)v61; (void)v62; (void)v63; (void)v64; (void)v65; (void)v66; (void)v67; (void)v68; (void)v69; (void)v70; (void)v71;
                ++rowCount;
                
                if (rowCount % 50000 == 0) {
                    std::cout << "  BCSV Static Progress: " << rowCount << "/" << NUM_ROWS << " rows read\n";
                }
            }
            reader.close();
        }
        auto readEnd = std::chrono::high_resolution_clock::now();
        double readTime = std::chrono::duration<double, std::milli>(readEnd - readStart).count();
        
        std::cout << "  BCSV Static Write time: " << std::fixed << std::setprecision(2) << writeTime << " ms\n";
        std::cout << "  BCSV Static Read time:  " << std::fixed << std::setprecision(2) << readTime << " ms\n\n";
        
        return {writeTime, readTime};
    }

    void printComprehensiveResults(const std::pair<double, double>& csvTimes,
                                 const std::pair<double, double>& flexibleTimes,
                                 const std::pair<double, double>& staticTimes) {
        std::cout << "Comprehensive Large Scale Performance Results\n";
        std::cout << "============================================\n\n";
        
        // File sizes
        auto csvSize = std::filesystem::file_size(CSV_FILENAME);
        auto flexibleSize = std::filesystem::file_size(BCSV_FLEXIBLE_FILENAME);
        auto staticSize = std::filesystem::file_size(BCSV_STATIC_FILENAME);
        
        std::cout << "File Sizes:\n";
        std::cout << "  CSV:             " << csvSize << " bytes (" << std::fixed << std::setprecision(1) << (csvSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  BCSV Flexible:   " << flexibleSize << " bytes (" << std::setprecision(1) << (flexibleSize / 1024.0 / 1024.0) << " MB)\n";
        std::cout << "  BCSV Static:     " << staticSize << " bytes (" << std::setprecision(1) << (staticSize / 1024.0 / 1024.0) << " MB)\n\n";
        
        std::cout << "Compression Ratios:\n";
        std::cout << "  BCSV vs CSV:     " << std::setprecision(1) << (100.0 - (flexibleSize * 100.0 / csvSize)) << "% smaller\n";
        std::cout << "  Static vs Flexible: " << std::setprecision(1) << (100.0 - (staticSize * 100.0 / flexibleSize)) << "% difference\n\n";
        
        // Performance comparison table
        std::cout << "Performance Comparison (500,000 rows, 72 columns):\n\n";
        std::cout << "Format         | Write (ms) | Read (ms)  | Total (ms) | Write MB/s | Read MB/s  | Total MB/s\n";
        std::cout << "---------------|------------|------------|------------|------------|------------|------------\n";
        
        auto printRow = [&](const std::string& name, double writeTime, double readTime, size_t fileSize) {
            double totalTime = writeTime + readTime;
            double fileSizeMB = fileSize / 1024.0 / 1024.0;
            double writeMBps = fileSizeMB / (writeTime / 1000.0);
            double readMBps = fileSizeMB / (readTime / 1000.0);
            double totalMBps = fileSizeMB / (totalTime / 1000.0);
            
            std::cout << std::left << std::setw(14) << name << " | "
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
        
        // Throughput analysis
        double csvThroughput = NUM_ROWS / ((csvTimes.first + csvTimes.second) / 1000.0);
        double flexibleThroughput = NUM_ROWS / ((flexibleTimes.first + flexibleTimes.second) / 1000.0);
        double staticThroughput = NUM_ROWS / ((staticTimes.first + staticTimes.second) / 1000.0);
        
        std::cout << "Throughput (rows/second):\n";
        std::cout << "  CSV:             " << std::fixed << std::setprecision(0) << csvThroughput << "\n";
        std::cout << "  BCSV Flexible:   " << flexibleThroughput << "\n";
        std::cout << "  BCSV Static:     " << staticThroughput << "\n\n";
        
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
        
        printComprehensiveResults(csvTimes, flexibleTimes, staticTimes);
        
        // Cleanup
        std::filesystem::remove(CSV_FILENAME);
        std::filesystem::remove(BCSV_FLEXIBLE_FILENAME);
        std::filesystem::remove(BCSV_STATIC_FILENAME);
        
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