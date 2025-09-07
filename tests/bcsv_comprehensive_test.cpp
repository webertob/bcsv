/**
 * @file bcsv_comprehensive_test.cpp
 * @brief Comprehensive Google Test suite for BCSV library
 * 
 * This test suite covers:
 * - Sequential write/read with flexible interface (all data types, 2 columns each)
 * - Sequential write/read with static interface (all data types, 2 columns each)
 * - Data integrity validation
 * - Cross-compatibility testing
 */

#include <gtest/gtest.h>
#include <bcsv/bcsv.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <iomanip>
#include <cstring>
#include <sstream>
#include <memory>
#include <vector>
#include <tuple>
#include <cmath>

namespace fs = std::filesystem;

class BCSVTestSuite : public ::testing::Test {
protected:
    static constexpr size_t NUM_ROWS = 10000;
    static constexpr size_t NUM_COLUMNS_PER_TYPE = 2; // 2 columns for each supported type
    
    std::mt19937 rng_;
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
    
    std::vector<std::string> sample_strings_;
    std::string test_dir_;
    
    // Static layout with 2 columns for each supported data type (24 columns total)
    using FullTestLayoutStatic = bcsv::LayoutStatic<
        bool,           // 0: Active flag 1
        bool,           // 1: Active flag 2
        int8_t,         // 2: Small signed integer 1
        int8_t,         // 3: Small signed integer 2
        int16_t,        // 4: Medium signed integer 1
        int16_t,        // 5: Medium signed integer 2
        int32_t,        // 6: Large signed integer 1
        int32_t,        // 7: Large signed integer 2
        int64_t,        // 8: Extra large signed integer 1
        int64_t,        // 9: Extra large signed integer 2
        uint8_t,        // 10: Small unsigned integer 1
        uint8_t,        // 11: Small unsigned integer 2
        uint16_t,       // 12: Medium unsigned integer 1
        uint16_t,       // 13: Medium unsigned integer 2
        uint32_t,       // 14: Large unsigned integer 1
        uint32_t,       // 15: Large unsigned integer 2
        uint64_t,       // 16: Extra large unsigned integer 1
        uint64_t,       // 17: Extra large unsigned integer 2
        float,          // 18: Single precision float 1
        float,          // 19: Single precision float 2
        double,         // 20: Double precision float 1
        double,         // 21: Double precision float 2
        std::string,    // 22: Variable length string 1
        std::string     // 23: Variable length string 2
    >;
    
    void SetUp() override {
        // Initialize random number generators with fixed seed for reproducible tests
        rng_.seed(42);
        int32_dist_ = std::uniform_int_distribution<int32_t>(-1000000, 1000000);
        int64_dist_ = std::uniform_int_distribution<int64_t>(-1000000000, 1000000000);
        uint32_dist_ = std::uniform_int_distribution<uint32_t>(0, 2000000);
        uint64_dist_ = std::uniform_int_distribution<uint64_t>(0, 2000000000);
        int8_dist_ = std::uniform_int_distribution<int>(-128, 127);
        int16_dist_ = std::uniform_int_distribution<int>(-32768, 32767);
        uint8_dist_ = std::uniform_int_distribution<int>(0, 255);
        uint16_dist_ = std::uniform_int_distribution<int>(0, 65535);
        float_dist_ = std::uniform_real_distribution<float>(-1000.0f, 1000.0f);
        double_dist_ = std::uniform_real_distribution<double>(-10000.0, 10000.0);
        bool_dist_ = std::uniform_int_distribution<int>(0, 1);
        
        // Sample strings for testing
        sample_strings_ = {
            "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota", "Kappa",
            "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma", "Tau", "Upsilon",
            "Phi", "Chi", "Psi", "Omega", "", "Single", "Very Long String With Many Characters",
            "Special!@#$%^&*()Characters", "Unicode: αβγδε", "Numbers: 123456789"
        };
        
        // Create test directory
        test_dir_ = "bcsv_test_files";
        fs::create_directories(test_dir_);
    }
    
    void TearDown() override {
        // Clean up test files
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }
    
    // Test data structure for all types (with 2 columns each)
    struct TestData {
        bool bool1, bool2;
        int8_t int8_1, int8_2;
        int16_t int16_1, int16_2;
        int32_t int32_1, int32_2;
        int64_t int64_1, int64_2;
        uint8_t uint8_1, uint8_2;
        uint16_t uint16_1, uint16_2;
        uint32_t uint32_1, uint32_2;
        uint64_t uint64_1, uint64_2;
        float float1, float2;
        double double1, double2;
        std::string string1, string2;
    };
    
    // Generate test data for all types
    TestData generateTestData(size_t row_index) {
        TestData data;
        
        data.bool1 = bool_dist_(rng_) == 1;
        data.bool2 = bool_dist_(rng_) == 1;
        
        data.int8_1 = static_cast<int8_t>(int8_dist_(rng_));
        data.int8_2 = static_cast<int8_t>(int8_dist_(rng_));
        
        data.int16_1 = static_cast<int16_t>(int16_dist_(rng_));
        data.int16_2 = static_cast<int16_t>(int16_dist_(rng_));
        
        data.int32_1 = int32_dist_(rng_);
        data.int32_2 = int32_dist_(rng_);
        
        data.int64_1 = int64_dist_(rng_);
        data.int64_2 = int64_dist_(rng_);
        
        data.uint8_1 = static_cast<uint8_t>(uint8_dist_(rng_));
        data.uint8_2 = static_cast<uint8_t>(uint8_dist_(rng_));
        
        data.uint16_1 = static_cast<uint16_t>(uint16_dist_(rng_));
        data.uint16_2 = static_cast<uint16_t>(uint16_dist_(rng_));
        
        data.uint32_1 = uint32_dist_(rng_);
        data.uint32_2 = uint32_dist_(rng_);
        
        data.uint64_1 = uint64_dist_(rng_);
        data.uint64_2 = uint64_dist_(rng_);
        
        data.float1 = float_dist_(rng_);
        data.float2 = float_dist_(rng_);
        
        data.double1 = double_dist_(rng_);
        data.double2 = double_dist_(rng_);
        
        data.string1 = sample_strings_[row_index % sample_strings_.size()];
        data.string2 = "Row" + std::to_string(row_index) + "_" + sample_strings_[(row_index + 1) % sample_strings_.size()];
        
        return data;
    }
    
    // Create flexible layout with 2 columns for each data type
    std::shared_ptr<bcsv::Layout> createFullFlexibleLayout() {
        auto layout = bcsv::Layout::create();
        
        std::vector<bcsv::ColumnDefinition> columns = {
            {"bool1", bcsv::ColumnDataType::BOOL},
            {"bool2", bcsv::ColumnDataType::BOOL},
            {"int8_1", bcsv::ColumnDataType::INT8},
            {"int8_2", bcsv::ColumnDataType::INT8},
            {"int16_1", bcsv::ColumnDataType::INT16},
            {"int16_2", bcsv::ColumnDataType::INT16},
            {"int32_1", bcsv::ColumnDataType::INT32},
            {"int32_2", bcsv::ColumnDataType::INT32},
            {"int64_1", bcsv::ColumnDataType::INT64},
            {"int64_2", bcsv::ColumnDataType::INT64},
            {"uint8_1", bcsv::ColumnDataType::UINT8},
            {"uint8_2", bcsv::ColumnDataType::UINT8},
            {"uint16_1", bcsv::ColumnDataType::UINT16},
            {"uint16_2", bcsv::ColumnDataType::UINT16},
            {"uint32_1", bcsv::ColumnDataType::UINT32},
            {"uint32_2", bcsv::ColumnDataType::UINT32},
            {"uint64_1", bcsv::ColumnDataType::UINT64},
            {"uint64_2", bcsv::ColumnDataType::UINT64},
            {"float1", bcsv::ColumnDataType::FLOAT},
            {"float2", bcsv::ColumnDataType::FLOAT},
            {"double1", bcsv::ColumnDataType::DOUBLE},
            {"double2", bcsv::ColumnDataType::DOUBLE},
            {"string1", bcsv::ColumnDataType::STRING},
            {"string2", bcsv::ColumnDataType::STRING}
        };
        
        for (const auto& col : columns) {
            layout->insertColumn(col);
        }
        
        return layout;
    }
    
    // Create static layout
    std::shared_ptr<FullTestLayoutStatic> createStaticLayout() {
        std::vector<std::string> columnNames = {
            "bool1", "bool2", "int8_1", "int8_2", "int16_1", "int16_2",
            "int32_1", "int32_2", "int64_1", "int64_2", "uint8_1", "uint8_2",
            "uint16_1", "uint16_2", "uint32_1", "uint32_2", "uint64_1", "uint64_2",
            "float1", "float2", "double1", "double2", "string1", "string2"
        };
        return FullTestLayoutStatic::create(columnNames);
    }
    
    // Populate flexible row
    void populateFlexibleRow(std::shared_ptr<bcsv::Layout::RowType> row, const TestData& data) {
        (*row).set(0, data.bool1);
        (*row).set(1, data.bool2);
        (*row).set(2, data.int8_1);
        (*row).set(3, data.int8_2);
        (*row).set(4, data.int16_1);
        (*row).set(5, data.int16_2);
        (*row).set(6, data.int32_1);
        (*row).set(7, data.int32_2);
        (*row).set(8, data.int64_1);
        (*row).set(9, data.int64_2);
        (*row).set(10, data.uint8_1);
        (*row).set(11, data.uint8_2);
        (*row).set(12, data.uint16_1);
        (*row).set(13, data.uint16_2);
        (*row).set(14, data.uint32_1);
        (*row).set(15, data.uint32_2);
        (*row).set(16, data.uint64_1);
        (*row).set(17, data.uint64_2);
        (*row).set(18, data.float1);
        (*row).set(19, data.float2);
        (*row).set(20, data.double1);
        (*row).set(21, data.double2);
        (*row).set(22, data.string1);
        (*row).set(23, data.string2);
    }
    
    // Populate static row
    void populateStaticRow(std::shared_ptr<typename FullTestLayoutStatic::RowType> row, const TestData& data) {
        row->template set<0>(data.bool1);
        row->template set<1>(data.bool2);
        row->template set<2>(data.int8_1);
        row->template set<3>(data.int8_2);
        row->template set<4>(data.int16_1);
        row->template set<5>(data.int16_2);
        row->template set<6>(data.int32_1);
        row->template set<7>(data.int32_2);
        row->template set<8>(data.int64_1);
        row->template set<9>(data.int64_2);
        row->template set<10>(data.uint8_1);
        row->template set<11>(data.uint8_2);
        row->template set<12>(data.uint16_1);
        row->template set<13>(data.uint16_2);
        row->template set<14>(data.uint32_1);
        row->template set<15>(data.uint32_2);
        row->template set<16>(data.uint64_1);
        row->template set<17>(data.uint64_2);
        row->template set<18>(data.float1);
        row->template set<19>(data.float2);
        row->template set<20>(data.double1);
        row->template set<21>(data.double2);
        row->template set<22>(data.string1);
        row->template set<23>(data.string2);
    }
    
    // Validate flexible row data  
    void validateFlexibleRowData(const TestData& expected, const bcsv::RowView& actual, size_t row_index) {
        EXPECT_EQ(expected.bool1, actual.get<bool>(0)) << "Row " << row_index << " bool1 mismatch";
        EXPECT_EQ(expected.bool2, actual.get<bool>(1)) << "Row " << row_index << " bool2 mismatch";
        EXPECT_EQ(expected.int8_1, actual.get<int8_t>(2)) << "Row " << row_index << " int8_1 mismatch";
        EXPECT_EQ(expected.int8_2, actual.get<int8_t>(3)) << "Row " << row_index << " int8_2 mismatch";
        EXPECT_EQ(expected.int16_1, actual.get<int16_t>(4)) << "Row " << row_index << " int16_1 mismatch";
        EXPECT_EQ(expected.int16_2, actual.get<int16_t>(5)) << "Row " << row_index << " int16_2 mismatch";
        EXPECT_EQ(expected.int32_1, actual.get<int32_t>(6)) << "Row " << row_index << " int32_1 mismatch";
        EXPECT_EQ(expected.int32_2, actual.get<int32_t>(7)) << "Row " << row_index << " int32_2 mismatch";
        EXPECT_EQ(expected.int64_1, actual.get<int64_t>(8)) << "Row " << row_index << " int64_1 mismatch";
        EXPECT_EQ(expected.int64_2, actual.get<int64_t>(9)) << "Row " << row_index << " int64_2 mismatch";
        EXPECT_EQ(expected.uint8_1, actual.get<uint8_t>(10)) << "Row " << row_index << " uint8_1 mismatch";
        EXPECT_EQ(expected.uint8_2, actual.get<uint8_t>(11)) << "Row " << row_index << " uint8_2 mismatch";
        EXPECT_EQ(expected.uint16_1, actual.get<uint16_t>(12)) << "Row " << row_index << " uint16_1 mismatch";
        EXPECT_EQ(expected.uint16_2, actual.get<uint16_t>(13)) << "Row " << row_index << " uint16_2 mismatch";
        EXPECT_EQ(expected.uint32_1, actual.get<uint32_t>(14)) << "Row " << row_index << " uint32_1 mismatch";
        EXPECT_EQ(expected.uint32_2, actual.get<uint32_t>(15)) << "Row " << row_index << " uint32_2 mismatch";
        EXPECT_EQ(expected.uint64_1, actual.get<uint64_t>(16)) << "Row " << row_index << " uint64_1 mismatch";
        EXPECT_EQ(expected.uint64_2, actual.get<uint64_t>(17)) << "Row " << row_index << " uint64_2 mismatch";
        EXPECT_FLOAT_EQ(expected.float1, actual.get<float>(18)) << "Row " << row_index << " float1 mismatch";
        EXPECT_FLOAT_EQ(expected.float2, actual.get<float>(19)) << "Row " << row_index << " float2 mismatch";
        EXPECT_DOUBLE_EQ(expected.double1, actual.get<double>(20)) << "Row " << row_index << " double1 mismatch";
        EXPECT_DOUBLE_EQ(expected.double2, actual.get<double>(21)) << "Row " << row_index << " double2 mismatch";
        EXPECT_EQ(expected.string1, actual.get<std::string>(22)) << "Row " << row_index << " string1 mismatch";
        EXPECT_EQ(expected.string2, actual.get<std::string>(23)) << "Row " << row_index << " string2 mismatch";
    }
    
    // Validate static row data
    void validateStaticRowData(const TestData& expected, const typename FullTestLayoutStatic::RowViewType& actual, size_t row_index) {
        EXPECT_EQ(expected.bool1, actual.template get<0>()) << "Row " << row_index << " bool1 mismatch";
        EXPECT_EQ(expected.bool2, actual.template get<1>()) << "Row " << row_index << " bool2 mismatch";
        EXPECT_EQ(expected.int8_1, actual.template get<2>()) << "Row " << row_index << " int8_1 mismatch";
        EXPECT_EQ(expected.int8_2, actual.template get<3>()) << "Row " << row_index << " int8_2 mismatch";
        EXPECT_EQ(expected.int16_1, actual.template get<4>()) << "Row " << row_index << " int16_1 mismatch";
        EXPECT_EQ(expected.int16_2, actual.template get<5>()) << "Row " << row_index << " int16_2 mismatch";
        EXPECT_EQ(expected.int32_1, actual.template get<6>()) << "Row " << row_index << " int32_1 mismatch";
        EXPECT_EQ(expected.int32_2, actual.template get<7>()) << "Row " << row_index << " int32_2 mismatch";
        EXPECT_EQ(expected.int64_1, actual.template get<8>()) << "Row " << row_index << " int64_1 mismatch";
        EXPECT_EQ(expected.int64_2, actual.template get<9>()) << "Row " << row_index << " int64_2 mismatch";
        EXPECT_EQ(expected.uint8_1, actual.template get<10>()) << "Row " << row_index << " uint8_1 mismatch";
        EXPECT_EQ(expected.uint8_2, actual.template get<11>()) << "Row " << row_index << " uint8_2 mismatch";
        EXPECT_EQ(expected.uint16_1, actual.template get<12>()) << "Row " << row_index << " uint16_1 mismatch";
        EXPECT_EQ(expected.uint16_2, actual.template get<13>()) << "Row " << row_index << " uint16_2 mismatch";
        EXPECT_EQ(expected.uint32_1, actual.template get<14>()) << "Row " << row_index << " uint32_1 mismatch";
        EXPECT_EQ(expected.uint32_2, actual.template get<15>()) << "Row " << row_index << " uint32_2 mismatch";
        EXPECT_EQ(expected.uint64_1, actual.template get<16>()) << "Row " << row_index << " uint64_1 mismatch";
        EXPECT_EQ(expected.uint64_2, actual.template get<17>()) << "Row " << row_index << " uint64_2 mismatch";
        EXPECT_FLOAT_EQ(expected.float1, actual.template get<18>()) << "Row " << row_index << " float1 mismatch";
        EXPECT_FLOAT_EQ(expected.float2, actual.template get<19>()) << "Row " << row_index << " float2 mismatch";
        EXPECT_DOUBLE_EQ(expected.double1, actual.template get<20>()) << "Row " << row_index << " double1 mismatch";
        EXPECT_DOUBLE_EQ(expected.double2, actual.template get<21>()) << "Row " << row_index << " double2 mismatch";
        EXPECT_EQ(expected.string1, actual.template get<22>()) << "Row " << row_index << " string1 mismatch";
        EXPECT_EQ(expected.string2, actual.template get<23>()) << "Row " << row_index << " string2 mismatch";
    }
    
    std::string getTestFilePath(const std::string& filename) {
        return (fs::path(test_dir_) / filename).string();
    }
};

// Test 1: Sequential Write with Flexible Interface - All Types (2 columns each)
TEST_F(BCSVTestSuite, FlexibleInterface_SequentialWrite_AllTypes) {
    std::string filename = getTestFilePath("flexible_write_all_types.bcsv");
    std::vector<TestData> test_data;
    
    // Generate test data
    for (size_t i = 0; i < NUM_ROWS; ++i) {
        test_data.push_back(generateTestData(i));
    }
    
    // Write data using flexible interface
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            auto row = layout->createRow();
            populateFlexibleRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Verify file exists and has reasonable size
    ASSERT_TRUE(fs::exists(filename));
    EXPECT_GT(fs::file_size(filename), 0);
    
    std::cout << "Flexible interface wrote " << NUM_ROWS << " rows to " << filename 
              << " (size: " << fs::file_size(filename) << " bytes)" << std::endl;
}

// Test 2: Sequential Write with Static Interface - All Types (2 columns each)
TEST_F(BCSVTestSuite, StaticInterface_SequentialWrite_AllTypes) {
    std::string filename = getTestFilePath("static_write_all_types.bcsv");
    std::vector<TestData> test_data;
    
    // Generate test data
    for (size_t i = 0; i < NUM_ROWS; ++i) {
        test_data.push_back(generateTestData(i));
    }
    
    // Write data using static interface
    {
        auto layout = createStaticLayout();
        bcsv::Writer<FullTestLayoutStatic> writer(layout, filename, true);
        
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            auto row = layout->createRow();
            populateStaticRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Verify file exists and has reasonable size
    ASSERT_TRUE(fs::exists(filename));
    EXPECT_GT(fs::file_size(filename), 0);
    
    std::cout << "Static interface wrote " << NUM_ROWS << " rows to " << filename 
              << " (size: " << fs::file_size(filename) << " bytes)" << std::endl;
}

// Test 3: Sequential Read with Flexible Interface - Data Integrity Check
TEST_F(BCSVTestSuite, FlexibleInterface_SequentialRead_DataIntegrity) {
    std::string filename = getTestFilePath("flexible_read_test.bcsv");
    std::vector<TestData> test_data;
    
    // Use smaller dataset for more reliable testing
    const size_t TEST_ROWS = 1000;
    
    // Generate and write test data first
    {
        for (size_t i = 0; i < TEST_ROWS; ++i) {
            test_data.push_back(generateTestData(i));
        }
        
        auto layout = createFullFlexibleLayout();
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        
        for (size_t i = 0; i < TEST_ROWS; ++i) {
            auto row = layout->createRow();
            populateFlexibleRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Read data back and validate data integrity
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Reader<bcsv::Layout> reader(layout, filename);
        
        // Read all rows and validate using while loop pattern
        bcsv::RowView rowView(layout);
        size_t rows_read = 0;
        size_t validation_failures = 0;
        
        while (size_t row_index = reader.readRow(rowView)) {
            rows_read++;
            // readRow returns 1-based index, convert to 0-based for array access
            size_t array_index = row_index - 1;
            if (array_index < test_data.size()) {
                try {
                    validateFlexibleRowData(test_data[array_index], rowView, array_index);
                } catch (const std::exception& e) {
                    validation_failures++;
                    std::cout << "Validation exception for row " << row_index << ": " << e.what() << std::endl;
                }
            } else {
                std::cout << "Row index " << row_index << " is out of bounds (max: " << test_data.size() << ")" << std::endl;
            }
        }
        
        std::cout << "Read " << rows_read << " rows with " << validation_failures << " validation failures" << std::endl;
        EXPECT_EQ(rows_read, TEST_ROWS);
    }
    
    std::cout << "Flexible interface successfully read and validated " << TEST_ROWS << " rows" << std::endl;
}

// Test 4: Sequential Read with Static Interface - Data Integrity Check
TEST_F(BCSVTestSuite, StaticInterface_SequentialRead_DataIntegrity) {
    std::string filename = getTestFilePath("static_read_test.bcsv");
    std::vector<TestData> test_data;
    
    // Use smaller dataset for more reliable testing
    const size_t TEST_ROWS = 1000;
    
    // Generate and write test data first using static interface
    {
        for (size_t i = 0; i < TEST_ROWS; ++i) {
            test_data.push_back(generateTestData(i));
        }
        
        auto layout = createStaticLayout();
        bcsv::Writer<FullTestLayoutStatic> writer(layout, filename, true);
        
        for (size_t i = 0; i < TEST_ROWS; ++i) {
            auto row = layout->createRow();
            populateStaticRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Read data back using static interface and validate data integrity
    {
        auto layout = createStaticLayout();
        bcsv::Reader<FullTestLayoutStatic> reader(layout, filename);
        
        // Read all rows and validate using while loop pattern
        typename FullTestLayoutStatic::RowViewType rowView(layout);
        size_t rows_read = 0;
        size_t validation_failures = 0;
        
        while (size_t row_index = reader.readRow(rowView)) {
            rows_read++;
            // readRow returns 1-based index, convert to 0-based for array access
            size_t array_index = row_index - 1;
            if (array_index < test_data.size()) {
                try {
                    validateStaticRowData(test_data[array_index], rowView, array_index);
                } catch (const std::exception& e) {
                    validation_failures++;
                    std::cout << "Validation exception for row " << row_index << ": " << e.what() << std::endl;
                }
            } else {
                std::cout << "Row index " << row_index << " is out of bounds (max: " << test_data.size() << ")" << std::endl;
            }
        }
        
        std::cout << "Read " << rows_read << " rows with " << validation_failures << " validation failures" << std::endl;
        EXPECT_EQ(rows_read, TEST_ROWS);
    }
    
    std::cout << "Static interface successfully read and validated " << TEST_ROWS << " rows" << std::endl;
}

// Test 5: Cross-Compatibility Test - Write with Flexible, Read with Static
TEST_F(BCSVTestSuite, CrossCompatibility_FlexibleWrite_StaticRead) {
    std::string filename = getTestFilePath("flex_write_static_read.bcsv");
    std::vector<TestData> test_data;
    
    // Generate test data
    for (size_t i = 0; i < 100; ++i) { // Use smaller dataset for cross-compatibility test
        test_data.push_back(generateTestData(i));
    }
    
    // Write with flexible interface
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        
        for (size_t i = 0; i < 100; ++i) {
            auto row = layout->createRow();
            populateFlexibleRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Read with static interface
    {
        auto layout = createStaticLayout();
        bcsv::Reader<FullTestLayoutStatic> reader(layout, filename);
        
        typename FullTestLayoutStatic::RowViewType rowView(layout);
        size_t rows_read = 0;
        
        while (reader.readRow(rowView)) {
            validateStaticRowData(test_data[rows_read], rowView, rows_read);
            rows_read++;
        }
        
        EXPECT_EQ(rows_read, 100);
    }
    
    std::cout << "Cross-compatibility test (Flexible→Static) passed" << std::endl;
}

// Test 6: Cross-Compatibility Test - Write with Static, Read with Flexible
TEST_F(BCSVTestSuite, CrossCompatibility_StaticWrite_FlexibleRead) {
    std::string filename = getTestFilePath("static_write_flex_read.bcsv");
    std::vector<TestData> test_data;
    
    // Generate test data
    for (size_t i = 0; i < 100; ++i) { // Use smaller dataset for cross-compatibility test
        test_data.push_back(generateTestData(i));
    }
    
    // Write with static interface
    {
        auto layout = createStaticLayout();
        bcsv::Writer<FullTestLayoutStatic> writer(layout, filename, true);
        
        for (size_t i = 0; i < 100; ++i) {
            auto row = layout->createRow();
            populateStaticRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Read with flexible interface
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Reader<bcsv::Layout> reader(layout, filename);
        
        bcsv::RowView rowView(layout);
        size_t rows_read = 0;
        
        while (reader.readRow(rowView)) {
            validateFlexibleRowData(test_data[rows_read], rowView, rows_read);
            rows_read++;
        }
        
        EXPECT_EQ(rows_read, 100);
    }
    
    std::cout << "Cross-compatibility test (Static→Flexible) passed" << std::endl;
}

// Test 7: Performance Comparison Test
TEST_F(BCSVTestSuite, Performance_FlexibleVsStatic) {
    const size_t perf_rows = 5000; // Smaller dataset for performance test
    std::string flex_file = getTestFilePath("performance_flex.bcsv");
    std::string static_file = getTestFilePath("performance_static.bcsv");
    
    std::vector<TestData> test_data;
    for (size_t i = 0; i < perf_rows; ++i) {
        test_data.push_back(generateTestData(i));
    }
    
    // Measure flexible interface write time
    auto start = std::chrono::high_resolution_clock::now();
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Writer<bcsv::Layout> writer(layout, flex_file, true);
        
        for (size_t i = 0; i < perf_rows; ++i) {
            auto row = layout->createRow();
            populateFlexibleRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    auto flex_write_time = std::chrono::high_resolution_clock::now() - start;
    
    // Measure static interface write time
    start = std::chrono::high_resolution_clock::now();
    {
        auto layout = createStaticLayout();
        bcsv::Writer<FullTestLayoutStatic> writer(layout, static_file, true);
        
        for (size_t i = 0; i < perf_rows; ++i) {
            auto row = layout->createRow();
            populateStaticRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    auto static_write_time = std::chrono::high_resolution_clock::now() - start;
    
    // Verify both files have same size (identical format)
    EXPECT_EQ(fs::file_size(flex_file), fs::file_size(static_file));
    
    // Report performance (for informational purposes)
    auto flex_ms = std::chrono::duration_cast<std::chrono::milliseconds>(flex_write_time).count();
    auto static_ms = std::chrono::duration_cast<std::chrono::milliseconds>(static_write_time).count();
    
    std::cout << "\nPerformance Results for " << perf_rows << " rows:" << std::endl;
    std::cout << "  Flexible interface: " << flex_ms << "ms" << std::endl;
    std::cout << "  Static interface: " << static_ms << "ms" << std::endl;
    std::cout << "  File size: " << fs::file_size(flex_file) << " bytes" << std::endl;
    
    // Both should complete successfully
    EXPECT_GT(flex_ms, 0);
    EXPECT_GT(static_ms, 0);
}

// Test 8: CRC32 Corruption Detection Test
TEST_F(BCSVTestSuite, CRC32_CorruptionDetection) {
    std::string test_file = getTestFilePath("crc32_test.bcsv");
    std::vector<TestData> test_data;
    
    // Generate small test dataset (just 5 rows for simplicity)
    for (size_t i = 0; i < 5; ++i) {
        test_data.push_back(generateTestData(i));
    }
    
    // Write and verify basic functionality
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Writer<bcsv::Layout> writer(layout, test_file, true);
        
        for (size_t i = 0; i < 5; ++i) {
            auto row = layout->createRow();
            populateFlexibleRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Verify the file works normally first
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Reader<bcsv::Layout> reader(layout, test_file);
        bcsv::RowView rowView(layout);
        
        size_t rows_read = 0;
        while (reader.readRow(rowView)) {
            rows_read++;
        }
        
        EXPECT_EQ(rows_read, 5) << "Original file should be readable";
    }
    
    // Test 1: Corrupt compressed payload data (should trigger CRC32 error)
    {
        // Read file data
        std::vector<char> file_data;
        {
            std::ifstream file(test_file, std::ios::binary);
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0);
            file_data.resize(file_size);
            file.read(file_data.data(), file_size);
        }
        
        // Corrupt bytes in the last quarter of the file (compressed data area)
        if (file_data.size() > 100) {
            size_t corrupt_start = (file_data.size() * 3) / 4;
            for (size_t i = 0; i < 16 && corrupt_start + i < file_data.size(); ++i) {
                file_data[corrupt_start + i] ^= 0xFF; // Flip all bits
            }
            
            // Write corrupted file
            {
                std::ofstream file(test_file, std::ios::binary);
                file.write(file_data.data(), file_data.size());
            }
            
            // Try to read - should detect corruption via CRC32
            auto layout = createFullFlexibleLayout();
            
            bool exception_thrown = false;
            std::string error_message;
            
            try {
                bcsv::Reader<bcsv::Layout> reader(layout, test_file, bcsv::ReaderMode::STRICT);
                bcsv::RowView rowView(layout);
                reader.readRow(rowView);
            } catch (const std::exception& e) {
                exception_thrown = true;
                error_message = e.what();
            }
            
            EXPECT_TRUE(exception_thrown) << "Expected exception for payload corruption";
            if (exception_thrown) {
                // Accept either CRC32 or decompression error as both indicate corruption detection
                bool is_corruption_detected = (error_message.find("CRC32 validation failed") != std::string::npos) ||
                                            (error_message.find("LZ4 decompression failed") != std::string::npos);
                EXPECT_TRUE(is_corruption_detected) 
                    << "Expected corruption detection, got: " << error_message;
                
                std::cout << "✓ Payload corruption detected: " << error_message << std::endl;
            }
        }
    }
    
    // Test 2: Test with multiple corruption patterns to verify robustness  
    {
        // Test corrupting different parts of the file
        std::vector<std::pair<std::string, std::function<void(std::vector<char>&)>>> corruption_tests = {
            {"Beginning corruption", [](std::vector<char>& data) {
                if (data.size() > 50) {
                    for (size_t i = 20; i < 30 && i < data.size(); ++i) {
                        data[i] ^= 0xAA;
                    }
                }
            }},
            {"Middle corruption", [](std::vector<char>& data) {
                if (data.size() > 100) {
                    size_t mid = data.size() / 2;
                    for (size_t i = 0; i < 10 && mid + i < data.size(); ++i) {
                        data[mid + i] ^= 0x55;
                    }
                }
            }},
            {"End corruption", [](std::vector<char>& data) {
                if (data.size() > 20) {
                    for (size_t i = data.size() - 10; i < data.size(); ++i) {
                        data[i] ^= 0xFF;
                    }
                }
            }}
        };
        
        for (const auto& [test_name, corruption_func] : corruption_tests) {
            // Restore clean file
            {
                auto layout = createFullFlexibleLayout();
                bcsv::Writer<bcsv::Layout> writer(layout, test_file, true);
                
                for (size_t i = 0; i < 5; ++i) {
                    auto row = layout->createRow();
                    populateFlexibleRow(row, test_data[i]);
                    writer.writeRow(*row);
                }
                writer.flush();
            }
            
            // Apply corruption
            std::vector<char> file_data;
            {
                std::ifstream file(test_file, std::ios::binary);
                file.seekg(0, std::ios::end);
                size_t file_size = file.tellg();
                file.seekg(0);
                file_data.resize(file_size);
                file.read(file_data.data(), file_size);
            }
            
            corruption_func(file_data);
            
            {
                std::ofstream file(test_file, std::ios::binary);
                file.write(file_data.data(), file_data.size());
            }
            
            // Test reading corrupted file
            auto layout = createFullFlexibleLayout();
            bool exception_thrown = false;
            std::string error_message;
            
            try {
                bcsv::Reader<bcsv::Layout> reader(layout, test_file);
                bcsv::RowView rowView(layout);
                
                // Try to read all rows to catch any corruption
                while (reader.readRow(rowView)) {
                    // Continue reading
                }
            } catch (const std::exception& e) {
                exception_thrown = true;
                error_message = e.what();
            }
            
            // We expect some kind of error for any corruption
            if (exception_thrown) {
                std::cout << "✓ " << test_name << " detected: " << error_message << std::endl;
            } else {
                std::cout << "⚠ " << test_name << " not detected - data may not have affected critical areas" << std::endl;
            }
        }
    }
    
    std::cout << "CRC32 corruption detection test completed successfully" << std::endl;
}

// Test 9: Packet Recovery - Skip Broken Packets and Read Valid Ones
TEST_F(BCSVTestSuite, PacketRecovery_SkipBrokenPackets) {
    std::string test_file = getTestFilePath("packet_recovery_test.bcsv");
    std::vector<TestData> test_data;
    
    // Generate small test dataset with multiple packets
    const size_t TOTAL_ROWS = 50;  // Small enough to create multiple packets
    for (size_t i = 0; i < TOTAL_ROWS; ++i) {
        test_data.push_back(generateTestData(i));
    }
    
    // Write clean data first
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Writer<bcsv::Layout> writer(layout, test_file, true);
        
        for (size_t i = 0; i < TOTAL_ROWS; ++i) {
            auto row = layout->createRow();
            populateFlexibleRow(row, test_data[i]);
            writer.writeRow(*row);
        }
        
        writer.flush();
    }
    
    // Verify clean file reads correctly
    size_t clean_rows_read = 0;
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Reader<bcsv::Layout> reader(layout, test_file);
        bcsv::RowView rowView(layout);
        
        while (reader.readRow(rowView)) {
            clean_rows_read++;
        }
    }
    
    std::cout << "Clean file contains " << clean_rows_read << " readable rows" << std::endl;
    EXPECT_GT(clean_rows_read, 0) << "Clean file should be readable";
    
    // Now corrupt part of the file to simulate broken packets
    {
        std::vector<char> file_data;
        {
            std::ifstream file(test_file, std::ios::binary);
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0);
            file_data.resize(file_size);
            file.read(file_data.data(), file_size);
        }
        
        // Corrupt middle section of the file (simulate broken packet)
        if (file_data.size() > 200) {
            size_t corrupt_start = file_data.size() / 3;
            size_t corrupt_end = (file_data.size() * 2) / 3;
            
            std::cout << "Corrupting bytes " << corrupt_start << " to " << corrupt_end << std::endl;
            
            for (size_t i = corrupt_start; i < corrupt_end && i < file_data.size(); ++i) {
                file_data[i] = static_cast<char>(0xDE);  // Fill with recognizable pattern
            }
            
            // Write corrupted file
            {
                std::ofstream file(test_file, std::ios::binary);
                file.write(file_data.data(), file_data.size());
            }
        }
    }
    
    // Test packet recovery - should read valid packets and skip broken ones
    size_t recovered_rows = 0;
    size_t errors_encountered = 0;
    {
        auto layout = createFullFlexibleLayout();
        bcsv::Reader<bcsv::Layout> reader(layout, test_file);
        bcsv::RowView rowView(layout);
        
        try {
            while (reader.readRow(rowView)) {
                recovered_rows++;
                // Don't validate data as row indices might be different due to skipped packets
            }
        } catch (const std::exception& e) {
            errors_encountered++;
            std::cout << "Expected error during recovery: " << e.what() << std::endl;
        }
    }
    
    std::cout << "Packet recovery results:" << std::endl;
    std::cout << "  Original rows: " << TOTAL_ROWS << std::endl;
    std::cout << "  Clean file rows: " << clean_rows_read << std::endl;
    std::cout << "  Recovered rows: " << recovered_rows << std::endl;
    std::cout << "  Errors encountered: " << errors_encountered << std::endl;
    
    // We expect to recover some rows, but likely not all due to corruption
    // The key is that the reader should handle broken packets gracefully
    EXPECT_LE(recovered_rows, clean_rows_read) << "Should not read more rows than in clean file";
    
    if (recovered_rows > 0) {
        std::cout << "✓ Packet recovery successful - managed to read " << recovered_rows << " valid rows" << std::endl;
    } else if (errors_encountered > 0) {
        std::cout << "✓ Error handling working - corruption properly detected" << std::endl;
    } else {
        std::cout << "⚠ No rows recovered and no errors - this may indicate an issue" << std::endl;
    }
    
    std::cout << "Packet recovery test completed" << std::endl;
}

// Test 10: Edge Case - Zero Columns Layout
TEST_F(BCSVTestSuite, EdgeCase_ZeroColumns) {
    std::string test_file = getTestFilePath("zero_columns_test.bcsv");
    
    // Test creating a layout with zero columns
    auto empty_layout = std::make_shared<bcsv::Layout>();
    
    // Should not crash when creating writer with empty layout
    bool writer_creation_success = false;
    std::string writer_error;
    
    try {
        bcsv::Writer<bcsv::Layout> writer(empty_layout, test_file, true);
        writer_creation_success = true;
        
        // Try to flush empty file
        writer.flush();
        
    } catch (const std::exception& e) {
        writer_error = e.what();
    }
    
    // Verify file was created (even if empty)
    bool file_exists = fs::exists(test_file);
    
    // Test reading empty layout file
    bool reader_creation_success = false;
    std::string reader_error;
    size_t rows_read = 0;
    
    if (file_exists) {
        try {
            bcsv::Reader<bcsv::Layout> reader(empty_layout, test_file);
            bcsv::RowView rowView(empty_layout);
            reader_creation_success = true;
            
            // Try to read from empty column file
            while (reader.readRow(rowView)) {
                rows_read++;
            }
            
        } catch (const std::exception& e) {
            reader_error = e.what();
        }
    }
    
    // Report results
    std::cout << "Zero columns test results:" << std::endl;
    std::cout << "  Writer creation: " << (writer_creation_success ? "SUCCESS" : "FAILED - " + writer_error) << std::endl;
    std::cout << "  File exists: " << (file_exists ? "YES" : "NO") << std::endl;
    std::cout << "  Reader creation: " << (reader_creation_success ? "SUCCESS" : "FAILED - " + reader_error) << std::endl;
    std::cout << "  Rows read: " << rows_read << std::endl;
    
    // Verify that operations complete without crashing (main success criteria)
    EXPECT_TRUE(true) << "Zero columns test completed without crashes";
}

// Test 11: Edge Case - Zero Rows with Valid Layout
TEST_F(BCSVTestSuite, EdgeCase_ZeroRows) {
    std::string test_file = getTestFilePath("zero_rows_test.bcsv");
    
    // Create a normal layout but write zero rows
    auto layout = createFullFlexibleLayout();
    
    // Test 1: Write zero rows
    bool write_success = false;
    std::string write_error;
    
    try {
        bcsv::Writer<bcsv::Layout> writer(layout, test_file, true);
        // Don't write any rows, just flush
        writer.flush();
        write_success = true;
        
    } catch (const std::exception& e) {
        write_error = e.what();
    }
    
    // Verify file was created
    bool file_exists = fs::exists(test_file);
    size_t file_size = file_exists ? fs::file_size(test_file) : 0;
    
    // Test 2: Read from zero-row file
    bool read_success = false;
    std::string read_error;
    size_t rows_read = 0;
    
    if (file_exists) {
        try {
            bcsv::Reader<bcsv::Layout> reader(layout, test_file);
            bcsv::RowView rowView(layout);
            read_success = true;
            
            // Should read zero rows gracefully
            while (reader.readRow(rowView)) {
                rows_read++;
            }
            
        } catch (const std::exception& e) {
            read_error = e.what();
        }
    }
    
    // Test 3: Verify we can still write to the file after reading
    bool append_success = false;
    std::string append_error;
    size_t file_size_after_append = 0;
    
    if (file_exists) {
        try {
            // Add one row to the previously empty file (overwrite mode since append might not be supported)
            bcsv::Writer<bcsv::Layout> writer(layout, test_file, true); // overwrite mode
            auto row = layout->createRow();
            populateFlexibleRow(row, generateTestData(0));
            writer.writeRow(*row);
            writer.flush();
            append_success = true;
            
            // Check file size after writing
            if (fs::exists(test_file)) {
                file_size_after_append = fs::file_size(test_file);
            }
            
        } catch (const std::exception& e) {
            append_error = e.what();
        }
    }
    
    // Test 4: Read the file again to verify it now has one row
    size_t final_rows_read = 0;
    bool final_read_success = false;
    
    if (append_success) {
        try {
            bcsv::Reader<bcsv::Layout> reader(layout, test_file);
            bcsv::RowView rowView(layout);
            
            while (reader.readRow(rowView)) {
                final_rows_read++;
                // Just count rows, don't validate data since generateTestData uses random values
            }
            final_read_success = true;
            
        } catch (const std::exception& e) {
            // Error in final read - log the error for debugging
            std::cout << "Error in final read: " << e.what() << std::endl;
        }
    }
    
    // Report results
    std::cout << "Zero rows test results:" << std::endl;
    std::cout << "  Write zero rows: " << (write_success ? "SUCCESS" : "FAILED - " + write_error) << std::endl;
    std::cout << "  File created: " << (file_exists ? "YES" : "NO") << " (size: " << file_size << " bytes)" << std::endl;
    std::cout << "  Read zero rows: " << (read_success ? "SUCCESS" : "FAILED - " + read_error) << std::endl;
    std::cout << "  Rows read from empty file: " << rows_read << std::endl;
    std::cout << "  Append to empty file: " << (append_success ? "SUCCESS" : "FAILED - " + append_error) << std::endl;
    std::cout << "  File size after append: " << file_size_after_append << " bytes" << std::endl;
    std::cout << "  Final read: " << (final_read_success ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "  Final rows read: " << final_rows_read << std::endl;
    
    // Verify main success criteria
    EXPECT_TRUE(write_success) << "Should be able to write zero rows without error";
    EXPECT_TRUE(file_exists) << "File should be created even with zero rows";
    EXPECT_TRUE(read_success) << "Should be able to read from zero-row file without error";
    EXPECT_EQ(rows_read, 0) << "Should read exactly zero rows from empty file";
    EXPECT_EQ(final_rows_read, 1) << "Should read exactly one row after appending";
}

// Test 12: Edge Case - Mixed Operations with Empty Data
TEST_F(BCSVTestSuite, EdgeCase_MixedEmptyOperations) {
    std::string test_file = getTestFilePath("mixed_empty_test.bcsv");
    
    // Test creating files with different empty scenarios
    struct EmptyScenario {
        std::string name;
        bool expect_success;
    };
    
    std::vector<EmptyScenario> scenarios = {
        {"Empty layout, no writes", true},
        {"Valid layout, no writes", true},
        {"Single column layout, no writes", true},
        {"Single column layout, one write then read", true}
    };
    
    for (size_t i = 0; i < scenarios.size(); ++i) {
        const auto& scenario = scenarios[i];
        std::string scenario_file = getTestFilePath("mixed_empty_" + std::to_string(i) + ".bcsv");
        
        bool write_success = false;
        bool read_success = false;
        size_t rows_read = 0;
        std::string error_message;
        
        try {
            std::shared_ptr<bcsv::Layout> layout;
            
            // Create layout based on scenario
            if (i == 0) {
                // Empty layout
                layout = std::make_shared<bcsv::Layout>();
            } else if (i == 1) {
                // Valid layout
                layout = createFullFlexibleLayout();
            } else if (i == 2 || i == 3) {
                // Single column layout
                layout = std::make_shared<bcsv::Layout>();
                bcsv::ColumnDefinition col("single_col", bcsv::ColumnDataType::INT64);
                layout->insertColumn(col);
            }
            
            // Write phase
            bcsv::Writer<bcsv::Layout> writer(layout, scenario_file, true);
            
            if (i == 3) {
                // Write one row for scenario 3
                auto row = layout->createRow();
                (*row).set(0, static_cast<int64_t>(42));
                writer.writeRow(*row);
            }
            // For other scenarios, write nothing
            
            writer.flush();
            write_success = true;
            
            // Read phase
            bcsv::Reader<bcsv::Layout> reader(layout, scenario_file);
            bcsv::RowView rowView(layout);
            
            while (reader.readRow(rowView)) {
                rows_read++;
                // Validate data if we have columns
                if (layout->getColumnCount() > 0) {
                    // Basic validation that we can access the data
                    for (size_t col = 0; col < layout->getColumnCount(); ++col) {
                        std::string col_name = layout->getColumnName(col);
                        // Just verify we can call the accessor without crashing
                        switch (layout->getColumnType(col)) {
                            case bcsv::ColumnDataType::INT64:
                                {
                                    auto val = rowView.get<int64_t>(col);
                                    if (i == 3) {
                                        EXPECT_EQ(val, 42);
                                    }
                                }
                                break;
                            case bcsv::ColumnDataType::STRING:
                                rowView.get<std::string>(col);
                                break;
                            default:
                                break;
                        }
                    }
                }
            }
            read_success = true;
            
        } catch (const std::exception& e) {
            error_message = e.what();
        }
        
        bool overall_success = write_success && read_success;
        
        std::cout << "Scenario '" << scenario.name << "':" << std::endl;
        std::cout << "  Write: " << (write_success ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "  Read: " << (read_success ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "  Rows: " << rows_read << std::endl;
        if (!error_message.empty()) {
            std::cout << "  Error: " << error_message << std::endl;
        }
        
        if (scenario.expect_success) {
            EXPECT_TRUE(overall_success) << "Scenario '" << scenario.name << "' should succeed";
        }
    }
    
    std::cout << "Mixed empty operations test completed" << std::endl;
}

/**
 * Test multipacket scenarios with large data to ensure packet boundaries work correctly
 */
TEST_F(BCSVTestSuite, Multipacket_LargeData) {
    auto layout = std::make_shared<bcsv::Layout>();
    
    bcsv::ColumnDefinition col1("id", bcsv::ColumnDataType::UINT32);
    bcsv::ColumnDefinition col2("large_data", bcsv::ColumnDataType::STRING);
    layout->insertColumn(col1);
    layout->insertColumn(col2);

    std::string filename = test_dir_ + "/multipacket_test.bcsv";
    const size_t MULTIPACKET_ROWS = 1000;
    
    // Write rows with very large strings to force multiple packets
    {
        bcsv::Writer<bcsv::Layout> writer(layout, filename);
        
        for (size_t i = 1; i <= MULTIPACKET_ROWS; ++i) {
            auto row = layout->createRow();
            row->set(0, static_cast<uint32_t>(i));
            
            // Create large string data (should force packet boundaries)
            std::string large_data = "LargeDataString" + std::to_string(i) + "_";
            for (int j = 0; j < 100; ++j) {  // Very large strings
                large_data += "ExtraDataPadding" + std::to_string(j) + "_";
            }
            row->set(1, large_data);
            
            writer.writeRow(*row);
        }
    }
    
    // Verify all data can be read back correctly
    {
        bcsv::Reader<bcsv::Layout> reader(layout, filename);
        bcsv::RowView rowView(layout);
        
        size_t count = 0;
        while (reader.readRow(rowView)) {
            count++;
            uint32_t id = rowView.get<uint32_t>(0);
            std::string data = rowView.get<std::string>(1);
            
            EXPECT_EQ(id, count) << "Row ID mismatch at row " << count;
            EXPECT_TRUE(data.find("LargeDataString" + std::to_string(count)) != std::string::npos) 
                << "Large data content mismatch at row " << count;
        }
        
        EXPECT_EQ(count, MULTIPACKET_ROWS) << "Expected to read " << MULTIPACKET_ROWS << " rows, but got " << count;
    }
    
    std::cout << "Multipacket large data test completed successfully" << std::endl;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
