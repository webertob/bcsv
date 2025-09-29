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

void writeStaticBCSV() {
    std::cout << "=== Writing with Static Interface ===\n\n";

    // Step 1: Create static layout with column names
    ExampleLayout layout({"id", "name", "score", "active"});

    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_static.bcsv";
    bcsv::Writer<ExampleLayout> writer(layout);
    if(!writer.open(filename, true, 1, bcsv::FileFlags::NONE)) {
        std::cerr << "Failed to open writer for BCSV file\n";
        return;
    }

    // Step 3: Create and write data rows
    std::vector<SampleData> sampleData = createSampleData();
    for (const auto& data : sampleData) {
        auto& row = writer.row();
        row.set<0>(data.id);
        row.set<1>(data.name);
        row.set<2>(data.score);
        row.set<3>(data.active);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void writeZoHStaticBCSV() {
    std::cout << "=== Writing with ZoH Static Interface ===\n\n";

    // Step 1: Create static layout with column names
    ExampleLayout layout({"id", "name", "score", "active"});

    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";

    // Step 2: Create a writer with ZoH compression enabled
    const std::string filename = "example_zoh_static.bcsv";
    bcsv::Writer<ExampleLayout> writer(layout);
    if(!writer.open(filename, true, 1, bcsv::FileFlags::ZERO_ORDER_HOLD)) {
        std::cerr << "Failed to open writer for BCSV file\n";
        return;
    }

    std::cout << "ZoH compression enabled\n";

    // Step 3: Create test data specifically designed for ZoH compression
    // This data has repeating values to demonstrate compression benefits
    std::vector<SampleData> sampleData = createSampleData();
    std::cout << "Writing " << sampleData.size() << " rows with ZoH compression patterns...\n";

    for (size_t i = 0; i < sampleData.size(); ++i) {
        const auto& data = sampleData[i];
        auto& row = writer.row();
                
        row.set<0>(data.id);
        row.set<1>(data.name);
        row.set<2>(data.score);
        row.set<3>(data.active);
        
        std::cout << "  Row " << (i+1) << ": id=" << data.id 
                  << ", name=\"" << data.name << "\""
                  << ", score=" << data.score
                  << ", active=" << (data.active ? "true" : "false");        
        writer.writeRow();
    }

    writer.close();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void readStaticBCSV() {
    std::cout << "=== Reading with Static Interface ===\n\n";

    // Step 1: Create matching layout for reading
    ExampleLayout layout({"id", "name", "score", "active"});
    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";
    
    // Step 2: Create a reader
    const std::string filename = "example_static.bcsv";
    bcsv::Reader<ExampleLayout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }

    if (!reader.layout().isCompatible(layout)) {
        std::cerr << "Incompatible layout for reading BCSV file\n";
        return;
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
    
    size_t rowIndex = 0;
    while (reader.readNext()) {
        // Use template get<N>() method for type-safe access
        const auto& row = reader.row();
        auto id = row.get<0>();
        auto name = row.get<1>();
        auto score = row.get<2>();
        auto active = row.get<3>();

        std::cout << std::setw(2) << id << " | "
                  << std::setw(14) << std::left << name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << score << " | "
                  << (active ? "Yes" : "No") << "\n";
        rowIndex++;
    }

    reader.close();
    std::cout << "\nSuccessfully read " << rowIndex << " rows from " << filename << "\n\n";
}

void readZoHStaticBCSV() {
    std::cout << "=== Reading with ZoH Static Interface ===\n\n";

    // Step 1: Create matching layout for reading
    ExampleLayout layout({"id", "name", "score", "active"});
    std::cout << "Created static layout with " << layout.columnCount() << " columns\n";
    
    // Step 2: Create a reader
    const std::string filename = "example_zoh_static.bcsv";
    bcsv::Reader<ExampleLayout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }

    // Check if file uses ZoH compression
    // Note: FileHeader access is private, so we'll assume ZoH if the file was written with it
    std::cout << "File should use Zero Order Hold compression (as written)\n";

    if (!reader.layout().isCompatible(layout)) {
        std::cerr << "Incompatible layout for reading BCSV file\n";
        return;
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
    std::cout << "Row | ID | Name           | Score | Active\n";
    std::cout << "----|----|--------------  |-------|-------\n";
    
    size_t rowIndex = 0;
    while (reader.readNext()) {
        // Use template get<N>() method for type-safe access
        const auto& row = reader.row();
        auto id = row.get<0>();
        auto name = row.get<1>();
        auto score = row.get<2>();
        auto active = row.get<3>();

        std::cout << std::setw(3) << (reader.rowIndex()) << " | "
                  << std::setw(2) << id << " | "
                  << std::setw(14) << std::left << name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << score << " | "
                  << (active ? "Yes" : "No") << "\n";
    }

    reader.close();
    std::cout << "\nSuccessfully read " << rowIndex << " rows from " << filename << "\n\n";
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

        // Write data without ZoH compression for comparison
        writeStaticBCSV();

        // Read data back without ZoH compression for comparison
        readStaticBCSV();
        // Write data using ZoH compression
        writeZoHStaticBCSV();
        
        // Read data back using ZoH decompression
        readZoHStaticBCSV();
        
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