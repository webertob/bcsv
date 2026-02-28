/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <bcsv/bcsv.h>

/**
 * BCSV Zero Order Hold (ZoH) Flexible Interface Example
 * 
 * This example specifically tests the Zero Order Hold compression functionality
 * using the flexible Layout and Row interface. ZoH compression is
 * optimized for time-series data where values remain constant for extended periods.
 */


struct SampleData {
    int32_t id;
    std::string name;
    float score;
    bool active;
};

std::vector<SampleData> generateTestData() {
    return {
        // First row - all fields will be serialized (packet start)
        {1, "Alice Johnson", 95.5f, true},
        
        // Second row - only ID changes (ZoH benefit)
        {2, "Alice Johnson", 95.5f, true},
        
        // Third row - ID and active change
        {3, "Alice Johnson", 95.5f, false},
        
        // Fourth row - all fields change
        {4, "Bob Smith", 87.2f, true},
        
        // Fifth row - only score changes
        {5, "Bob Smith", 92.8f, true},
        
        // Sixth row - only name changes
        {6, "Carol Williams", 92.8f, true},
        
        // Seventh row - only boolean changes
        {7, "Carol Williams", 92.8f, false},
        
        // Eighth row - back to first values (good for ZoH)
        {8, "Alice Johnson", 95.5f, true}
    };
}

void writeBCSV(const std::vector<SampleData>& testData) {
    std::cout << "=== Writing with Flexible Interface ===\n\n";

    // Step 1: Create a flexible layout
    // The Layout class allows you to define columns at runtime
    bcsv::Layout layout;
    
    // Add columns with name and data type
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"score", bcsv::ColumnType::FLOAT});
    layout.addColumn({"active", bcsv::ColumnType::BOOL});
    std::cout << "Created layout with " << layout.columnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_flexible.bcsv";
    bcsv::Writer<bcsv::Layout> writer(layout);
    if(!writer.open(filename, true)) {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
        return;
    }

    // Step 3: Write data rows
    for (const auto& data : testData) {
        auto& row = writer.row();
        row.set(0, data.id);
        row.set(1, data.name);
        row.set(2, data.score);
        row.set(3, data.active);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Successfully wrote " << testData.size() << " rows to " << filename << "\n\n";
}

void writeZoHBCSV(const std::vector<SampleData>& testData) {
    std::cout << "=== Writing with Flexible Interface and ZoH ===\n\n";

    // Step 1: Create a flexible layout
    // The Layout class allows you to define columns at runtime
    bcsv::Layout layout;
    
    // Add columns with name and data type
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"score", bcsv::ColumnType::FLOAT});
    layout.addColumn({"active", bcsv::ColumnType::BOOL});
    std::cout << "Created layout with " << layout.columnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_flexible_zoh.bcsv";
    bcsv::WriterZoH<bcsv::Layout> writer(layout);
    if(!writer.open(filename, true, 1 /*compressionLevel*/, 64 /*blockSizeKB*/, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
        return;
    }

    // Step 3: Write data rows
    for (const auto& data : testData) {
        auto& row = writer.row();
        row.set(0, data.id);
        row.set(1, data.name);
        row.set(2, data.score);
        row.set(3, data.active);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Successfully wrote " << testData.size() << " rows to " << filename << "\n\n";
}

bool validateWriteSuccess(const std::string& filename) {
    std::ifstream file_check(filename, std::ios::binary | std::ios::ate);
    if (!file_check) {
        std::cerr << "VALIDATION ERROR: File does not exist: " << filename << "\n";
        return false;
    }
    size_t file_size = file_check.tellg();
    file_check.close();
    std::cout << "Write validation: File exists with size " << file_size << " bytes\n";
    if (file_size < 100) {
        std::cerr << "VALIDATION ERROR: File size too small (" << file_size << " bytes)\n";
        return false;
    }
    return true;
}

std::vector<SampleData> readBCSV() {

    std::cout << "=== Reading with Flexible Interface ===\n\n";

    std::vector<SampleData> readData;
    
    // Step 1: Create matching layout for reading
    // Must match the layout used for writing
    bcsv::Layout layoutExpected;
    layoutExpected.addColumn({"id", bcsv::ColumnType::INT32});
    layoutExpected.addColumn({"name", bcsv::ColumnType::STRING});
    layoutExpected.addColumn({"score", bcsv::ColumnType::FLOAT});
    layoutExpected.addColumn({"active", bcsv::ColumnType::BOOL});

    // Step 2: Create a reader
    const std::string filename = "example_flexible.bcsv";
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return readData;
    }
    // Validate layout compatibility (column count, types)
    if (!reader.layout().isCompatible(layoutExpected)) {
        std::cerr << "Error: File layout is not compatible with expected layout\n";
        reader.close();
        return readData;
    }

    //Optional: Compare column names
    for (size_t i = 0; i < layoutExpected.columnCount(); i++) {
        if (layoutExpected.columnName(i) != reader.layout().columnName(i)) {
            std::cerr << "Warning: Column name mismatch at index " << i
                      << " (expected: " << layoutExpected.columnName(i)
                      << ", found: " << reader.layout().columnName(i) << ")\n";
        }
    }

    std::cout << "Reading data:\n\n";
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";

    // Step 3: Read all rows
    while (reader.readNext()) {
        auto& row = reader.row();
        SampleData data;
        bool ok = true;
        ok &= row.get(0, data.id);
        ok &= row.get(1, data.name);
        ok &= row.get(2, data.score);
        ok &= row.get(3, data.active);
        if (!ok) {
            std::cerr << "Warning: Failed to read row values, skipping row.\n";
            continue;
        }
        
        readData.push_back(data);

        std::cout << std::setw(2) << data.id << " | "
                  << std::setw(14) << std::left << data.name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << data.score << " | "
                  << (data.active ? "Yes" : "No") << "\n";
    }

    reader.close();
    std::cout << "\nSuccessfully read " << readData.size() << " rows from " << filename << "\n\n";
    
    return readData;
}

std::vector<SampleData> readZoHBCSV() {
    std::cout << "=== Reading with Flexible Interface ===\n\n";

    std::vector<SampleData> readData;
    
    // Step 1: Create matching layout for reading
    // Must match the layout used for writing
    bcsv::Layout layoutExpected;
    layoutExpected.addColumn({"id", bcsv::ColumnType::INT32});
    layoutExpected.addColumn({"name", bcsv::ColumnType::STRING});
    layoutExpected.addColumn({"score", bcsv::ColumnType::FLOAT});
    layoutExpected.addColumn({"active", bcsv::ColumnType::BOOL});

    // Step 2: Create a reader
    const std::string filename = "example_flexible_zoh.bcsv";
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return readData;
    }
    // Validate layout compatibility (column count, types)
    if (!reader.layout().isCompatible(layoutExpected)) {
        std::cerr << "Error: File layout is not compatible with expected layout\n";
        reader.close();
        return readData;
    }

    //Optional: Compare column names
    for (size_t i = 0; i < layoutExpected.columnCount(); i++) {
        if (layoutExpected.columnName(i) != reader.layout().columnName(i)) {
            std::cerr << "Warning: Column name mismatch at index " << i
                      << " (expected: " << layoutExpected.columnName(i)
                      << ", found: " << reader.layout().columnName(i) << ")\n";
        }
    }

    std::cout << "Reading data:\n\n";
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";

    // Step 3: Read all rows
    while (reader.readNext()) {
        auto& row = reader.row();
        SampleData data;
        bool ok = true;
        ok &= row.get(0, data.id);
        ok &= row.get(1, data.name);
        ok &= row.get(2, data.score);
        ok &= row.get(3, data.active);
        if (!ok) {
            std::cerr << "Warning: Failed to read row values, skipping row.\n";
            continue;
        }
        
        readData.push_back(data);

        std::cout << std::setw(2) << data.id << " | "
                  << std::setw(14) << std::left << data.name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << data.score << " | "
                  << (data.active ? "Yes" : "No") << "\n";
    }

    reader.close();
    std::cout << "\nSuccessfully read " << readData.size() << " rows from " << filename << "\n\n";
    
    return readData;
}

bool validateReadSuccess(const std::vector<SampleData>& expectedData, const std::vector<SampleData>& readData) {
    std::cout << "=== Validating Read Data ===\n\n";
    
    if (readData.size() != expectedData.size()) {
        std::cerr << "❌ VALIDATION FAILED: Expected " << expectedData.size() 
                  << " rows, but read " << readData.size() << " rows!\n\n";
        return false;
    }
    
    bool validationError = false;
    for (size_t i = 0; i < expectedData.size(); i++) {
        const auto& expected = expectedData[i];
        const auto& actual = readData[i];
        
        if (actual.id != expected.id || actual.name != expected.name || 
            std::abs(actual.score - expected.score) > 0.01f || actual.active != expected.active) {
            std::cerr << "ERROR: Data mismatch at row " << i << "\n";
            std::cerr << "  Expected: id=" << expected.id << ", name=\"" << expected.name 
                      << "\", score=" << expected.score << ", active=" << expected.active << "\n";
            std::cerr << "  Got:      id=" << actual.id << ", name=\"" << actual.name 
                      << "\", score=" << actual.score << ", active=" << actual.active << "\n";
            validationError = true;
        }
    }
    
    if (validationError) {
        std::cerr << "\n❌ VALIDATION FAILED: Read data does not match expected data!\n\n";
        return false;
    }
    
    std::cout << "✓ VALIDATION PASSED: All " << readData.size() << " rows verified successfully!\n\n";
    return true;
}

void compareCompressionEfficiency() {
    std::cout << "=== Compression Efficiency Analysis ===\n\n";
    
    // Get file sizes
    namespace fs = std::filesystem;
    
    std::string normalFile = "example_flexible.bcsv";
    std::string zohFile = "example_flexible_zoh.bcsv";
    
    if (fs::exists(normalFile) && fs::exists(zohFile)) {
        auto normalSize = fs::file_size(normalFile);
        auto zohSize    = fs::file_size(zohFile);
        std::cout << "Normal BCSV file size: " << normalSize << " bytes\n";
        std::cout << "ZoH BCSV file size: " << zohSize << " bytes\n";
        
        if (normalSize > 0) {
            double compressionRatio = static_cast<double>(normalSize - zohSize) / normalSize * 100.0;
            std::cout << "Compression ratio: " << std::fixed << std::setprecision(1) 
                      << compressionRatio << "% space savings\n";
        }
    }
    std::cout << "\n";
}

int main() {
    std::cout << "BCSV Zero Order Hold (ZoH) Flexible Interface Example\n";
    std::cout << "======================================================\n\n";
    std::cout << "This example demonstrates Zero Order Hold compression\n";
    std::cout << "using the flexible Layout/Row interface for time-series data.\n\n";
    
    try {
        // Generate test data once
        auto testData = generateTestData();

        // Write data without ZoH compression for comparison
        writeBCSV(testData);
        
        // Validate write success (test code)
        if (!validateWriteSuccess("example_flexible.bcsv")) {
            return 1;
        }

        // Read data back without ZoH compression for comparison
        auto readData1 = readBCSV();
        
        // Validate read data matches expected data (test code)
        if (!validateReadSuccess(testData, readData1)) {
            return 1;
        }
        
        // Write data using ZoH compression
        writeZoHBCSV(testData);
        
        // Validate write success (test code)
        if (!validateWriteSuccess("example_flexible_zoh.bcsv")) {
            return 1;
        }
        
        // Read data back using ZoH decompression
        auto readData2 = readZoHBCSV();
        
        // Validate read data matches expected data (test code)
        if (!validateReadSuccess(testData, readData2)) {
            return 1;
        }
        
        // Compare with normal compression
        compareCompressionEfficiency();
        
        std::cout << "✓ ZoH Example completed successfully!\n";
        std::cout << "Zero Order Hold compression is ideal for time-series data\n";
        std::cout << "where values remain constant for extended periods.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}