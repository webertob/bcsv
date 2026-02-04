/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cmath>
#include <sys/stat.h>
#include <bcsv/bcsv.h>

/**
 * BCSV Zero Order Hold (ZoH) Static Interface Example
 * 
 * This example specifically tests the Zero Order Hold compression functionality
 * using the static LayoutStatic and RowStatic interface. ZoH compression is
 * optimized for time-series data where values remain constant for extended periods.
 */

// Define our data structure using LayoutStatic template
using ExampleLayout = bcsv::LayoutStatic<
    int32_t,        // id
    std::string,    // name
    float,          // score
    bool            // active
>;

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

void writeStaticBCSV(const std::vector<SampleData>& testData) {
    std::cout << "=== Writing with Static Interface ===\n\n";

    // Step 1: Create static layout with column names
    ExampleLayout layout({"id", "name", "score", "active"});

    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_static.bcsv";
    bcsv::Writer<ExampleLayout> writer(layout);
    if(!writer.open(filename, true, 1, 64, bcsv::FileFlags::NONE)) {
        std::cerr << "Failed to open writer for BCSV file\n";
        return;
    }

    // Step 3: Write data rows
    for (const auto& data : testData) {
        auto& row = writer.row();
        row.set<0>(data.id);
        row.set<1>(data.name);
        row.set<2>(data.score);
        row.set<3>(data.active);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Successfully wrote " << testData.size() << " rows to " << filename << "\n\n";
}

void writeZoHStaticBCSV(const std::vector<SampleData>& testData) {
    std::cout << "=== Writing with ZoH Static Interface ===\n\n";

    // Step 1: Create static layout with column names
    ExampleLayout layout({"id", "name", "score", "active"});

    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";

    // Step 2: Create a writer with ZoH compression enabled
    const std::string filename = "example_zoh_static.bcsv";
    bcsv::Writer<ExampleLayout> writer(layout);
    if(!writer.open(filename, true, 1, 64, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
        std::cerr << "Failed to open writer for BCSV file\n";
        return;
    }

    std::cout << "ZoH compression enabled\n";

    // Step 3: Write data rows with ZoH compression
    for (const auto& data : testData) {
        auto& row = writer.row();
        row.set<0>(data.id);
        row.set<1>(data.name);
        row.set<2>(data.score);
        row.set<3>(data.active);
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

std::vector<SampleData> readStaticBCSV() {
    std::cout << "=== Reading with Static Interface ===\n\n";

    std::vector<SampleData> readData;
    
    // Step 1: Create matching layout for reading
    ExampleLayout layout({"id", "name", "score", "active"});
    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";
    
    // Step 2: Create a reader
    const std::string filename = "example_static.bcsv";
    bcsv::Reader<ExampleLayout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return readData;
    }

    if (!reader.layout().isCompatible(layout)) {
        std::cerr << "Incompatible layout for reading BCSV file\n";
        return readData;
    }

    //optional check column names
    for (size_t i = 0; i < layout.columnCount(); i++) {
        if (reader.layout().columnName(i) != layout.columnName(i)) {
            std::cerr << "Warning: Column name mismatch at index " << i 
                      << ": expected '" << layout.columnName(i) 
                      << "', got '" << reader.layout().columnName(i) << "'\n";
        }
    }

    std::cout << "Reading data:\n\n";  
    
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";
    
    // Step 3: Read all rows
    while (reader.readNext()) {
        const auto& row = reader.row();
        SampleData data;
        data.id = row.get<0>();
        data.name = row.get<1>();
        data.score = row.get<2>();
        data.active = row.get<3>();
        
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

std::vector<SampleData> readZoHStaticBCSV() {
    std::cout << "=== Reading with ZoH Static Interface ===\n\n";

    std::vector<SampleData> readData;
    
    // Step 1: Create matching layout for reading
    ExampleLayout layout({"id", "name", "score", "active"});
    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";
    
    // Step 2: Create a reader
    const std::string filename = "example_zoh_static.bcsv";
    bcsv::Reader<ExampleLayout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return readData;
    }

    // Check if file uses ZoH compression
    // Note: FileHeader access is private, so we'll assume ZoH if the file was written with it
    std::cout << "File should use Zero Order Hold compression (as written)\n";

    if (!reader.layout().isCompatible(layout)) {
        std::cerr << "Incompatible layout for reading BCSV file\n";
        return readData;
    }

    // Optional check column names
    for (size_t i = 0; i < layout.columnCount(); i++) {
        if (reader.layout().columnName(i) != layout.columnName(i)) {
            std::cerr << "Warning: Column name mismatch at index " << i 
                      << ": expected '" << layout.columnName(i) 
                      << "', got '" << reader.layout().columnName(i) << "'\n";
        }
    }

    std::cout << "Reading ZoH compressed data:\n\n";  
    
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";
    
    // Step 3: Read all rows
    while (reader.readNext()) {
        const auto& row = reader.row();
        SampleData data;
        data.id = row.get<0>();
        data.name = row.get<1>();
        data.score = row.get<2>();
        data.active = row.get<3>();
        
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
    struct stat st;
    
    std::string normalFile = "example_static.bcsv";
    std::string zohFile = "example_zoh_static.bcsv";
    
    if (stat(normalFile.c_str(), &st) == 0) {
        size_t normalSize = st.st_size;
        std::cout << "Normal BCSV file size: " << normalSize << " bytes\n";
        
        if (stat(zohFile.c_str(), &st) == 0) {
            size_t zohSize = st.st_size;
            std::cout << "ZoH BCSV file size: " << zohSize << " bytes\n";
            
            if (normalSize > 0) {
                double compressionRatio = (double)(normalSize - zohSize) / normalSize * 100.0;
                std::cout << "Compression ratio: " << std::fixed << std::setprecision(1) 
                          << compressionRatio << "% space savings\n";
            }
        }
    }
    std::cout << "\n";
}

int main() {
    std::cout << "BCSV Zero Order Hold (ZoH) Static Interface Example\n";
    std::cout << "===================================================\n\n";
    std::cout << "This example demonstrates Zero Order Hold compression\n";
    std::cout << "using the static LayoutStatic/RowStatic interface for time-series data.\n\n";
    
    try {
        // Generate test data once
        auto testData = generateTestData();

        // Write data without ZoH compression for comparison
        writeStaticBCSV(testData);
        
        // Validate write success (test code)
        if (!validateWriteSuccess("example_static.bcsv")) {
            return 1;
        }

        // Read data back without ZoH compression for comparison
        auto readData1 = readStaticBCSV();
        
        // Validate read data matches expected data (test code)
        if (!validateReadSuccess(testData, readData1)) {
            return 1;
        }
        
        // Write data using ZoH compression
        writeZoHStaticBCSV(testData);
        
        // Validate write success (test code)
        if (!validateWriteSuccess("example_zoh_static.bcsv")) {
            return 1;
        }
        
        // Read data back using ZoH decompression
        auto readData2 = readZoHStaticBCSV();
        
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