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
#include <cmath>
#include <bcsv/bcsv.h>

/**
 * BCSV Flexible Interface Example
 * 
 * This example demonstrates the runtime flexible Layout and Row interface
 * for writing and reading BCSV files. The flexible interface allows you to
 * define column layouts at runtime and is ideal when you don't know the
 * data structure at compile time.
 */

struct SampleData {
    int32_t id;
    std::string name;
    float score;
    bool active;
};

std::vector<SampleData> generateTestData() {
    return {
        {1, "Alice Johnson", 95.5f, true},
        {2, "Bob Smith", 87.2f, true},
        {3, "Carol Williams", 92.8f, false},
        {4, "David Brown", 78.9f, true},
        {5, "Eve Davis", 88.1f, false}
    };
}

void writeFlexibleBCSV(const std::vector<SampleData>& testData) {
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

    writer.flush();
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

std::vector<SampleData> readFlexibleBCSV() {
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

    // Step 3: Read rows
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

int main() {
    std::cout << "BCSV Flexible Interface Example\n";
    std::cout << "===============================\n\n";
    std::cout << "This example demonstrates reading and writing BCSV files\n";
    std::cout << "using the flexible Layout/Row interface for runtime-defined schemas.\n\n";
    
    try {
        // Generate test data once
        auto testData = generateTestData();
        
        // Write data using flexible interface
        writeFlexibleBCSV(testData);
        
        // Validate write success (test code)
        if (!validateWriteSuccess("example_flexible.bcsv")) {
            return 1;
        }
        
        // Read data back using flexible interface
        auto readData = readFlexibleBCSV();
        
        // Validate read data matches expected data (test code)
        if (!validateReadSuccess(testData, readData)) {
            return 1;
        }
        
        std::cout << "✓ Example completed successfully!\n";
        std::cout << "The flexible interface is ideal when you need to define\n";
        std::cout << "data structures at runtime or work with varying schemas.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
