/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <iostream>
#include <memory>
#include <iomanip>
#include <sys/stat.h>
#include "bcsv/bcsv.h"

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


auto createSampleData()
{
    std::vector<SampleData> sampleData = {
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
    return sampleData;
}

void writeBCSV() {
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

    // Step 3: Create and write data rows
    std::vector<SampleData> sampleData = createSampleData();
    for (const auto& data : sampleData) {
        auto& row = writer.row();
        row.set(0, data.id);
        row.set(1, data.name);
        row.set(2, data.score);
        row.set(3, data.active);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void writeZoHBCSV() {
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
    bcsv::Writer<bcsv::Layout> writer(layout);
    if(!writer.open(filename, true, 1 /*compressionLevel*/, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
        return;
    }

    // Step 3: Create and write data rows
    std::vector<SampleData> sampleData = createSampleData();
    for (const auto& data : sampleData) {
        auto& row = writer.row();
        row.set(0, data.id);
        row.set(1, data.name);
        row.set(2, data.score);
        row.set(3, data.active);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void readBCSV() {

    std::cout << "=== Reading with Flexible Interface ===\n\n";

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
        return;
    }
    // Validate layout compatibility (column count, types)
    if (!reader.layout().isCompatibleWith(layoutExpected)) {
        std::cerr << "Error: File layout is not compatible with expected layout\n";
        reader.close();
        return;
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

    auto sampleData = createSampleData();
    bool error = false;
    // Step 3: Read rows using RowView
    size_t rowIndex = 0;
    while (reader.readNext()) {
        auto id = reader.row().get<int32_t>(0);
        auto name = reader.row().get<std::string>(1);
        auto score = reader.row().get<float>(2);
        auto active = reader.row().get<bool>(3);

        std::cout << std::setw(2) << id << " | "
                  << std::setw(14) << std::left << name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << score << " | "
                  << (active ? "Yes" : "No") << "\n";
    
        // Validate against original data
        if (rowIndex < sampleData.size()) {
            const auto& original = sampleData[rowIndex];
            if (id != original.id || name != original.name || score != original.score || active != original.active) {
                std::cerr << "Data mismatch at row " << rowIndex << "\n";
                error = true;
            }
        }   
        rowIndex = reader.rowIndex();
    }

    reader.close();
    std::cout << "\nSuccessfully read " << rowIndex << " rows from " << filename << "\n\n";
    std::cout << (error ? "Errors were detected in the read data!\n" : "All data verified successfully!\n") << "\n";
}

void readZoHBCSV() {
    std::cout << "=== Reading with Flexible Interface ===\n\n";

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
        return;
    }
    // Validate layout compatibility (column count, types)
    if (!reader.layout().isCompatibleWith(layoutExpected)) {
        std::cerr << "Error: File layout is not compatible with expected layout\n";
        reader.close();
        return;
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

    auto sampleData = createSampleData();
    bool error = false;
    // Step 3: Read rows using RowView
    size_t rowIndex = 0;
    while (reader.readNext()) {
        auto id = reader.row().get<int32_t>(0);
        auto name = reader.row().get<std::string>(1);
        auto score = reader.row().get<float>(2);
        auto active = reader.row().get<bool>(3);

        std::cout << std::setw(2) << id << " | "
                  << std::setw(14) << std::left << name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << score << " | "
                  << (active ? "Yes" : "No") << "\n";
    
        // Validate against original data
        if (rowIndex < sampleData.size()) {
            const auto& original = sampleData[rowIndex];
            if (id != original.id || name != original.name || score != original.score || active != original.active) {
                std::cerr << "Data mismatch at row " << rowIndex << "\n";
                error = true;
            }
        }   
        rowIndex = reader.rowIndex();
    }

    reader.close();
    std::cout << "\nSuccessfully read " << rowIndex << " rows from " << filename << "\n\n";
    std::cout << (error ? "Errors were detected in the read data!\n" : "All data verified successfully!\n") << "\n";
}

void compareCompressionEfficiency() {
    std::cout << "=== Compression Efficiency Analysis ===\n\n";
    
    // Get file sizes
    struct stat st;
    
    std::string normalFile = "example_flexible.bcsv";
    std::string zohFile = "example_flexible_zoh.bcsv";
    
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

        // Write data without ZoH compression for comparison
        writeBCSV();

        // Read data back without ZoH compression for comparison
        readBCSV();
        
        // Write data using ZoH compression
        writeZoHBCSV();
        
        // Read data back using ZoH decompression
        readZoHBCSV();
        
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