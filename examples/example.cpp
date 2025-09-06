#include <iostream>
#include <memory>
#include <iomanip>
#include "bcsv/bcsv.h"

/**
 * BCSV Flexible Interface Example
 * 
 * This example demonstrates the runtime flexible Layout and Row interface
 * for writing and reading BCSV files. The flexible interface allows you to
 * define column layouts at runtime and is ideal when you don't know the
 * data structure at compile time.
 */

void writeFlexibleBCSV() {
    std::cout << "=== Writing with Flexible Interface ===\n\n";

    // Step 1: Create a flexible layout
    // The Layout class allows you to define columns at runtime
    auto layout = bcsv::Layout::create();
    
    // Add columns with name and data type
    layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
    layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
    layout->insertColumn({"score", bcsv::ColumnDataType::FLOAT});
    layout->insertColumn({"active", bcsv::ColumnDataType::BOOL});

    std::cout << "Created layout with " << layout->getColumnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_flexible.bcsv";
    bcsv::Writer<bcsv::Layout> writer(layout, filename, true);

    // Step 3: Create and write data rows
    struct SampleData {
        int32_t id;
        std::string name;
        float score;
        bool active;
    };

    std::vector<SampleData> sampleData = {
        {1, "Alice Johnson", 95.5f, true},
        {2, "Bob Smith", 87.2f, true},
        {3, "Carol Williams", 92.8f, false},
        {4, "David Brown", 78.9f, true},
        {5, "Eve Davis", 88.1f, false}
    };

    for (const auto& data : sampleData) {
        auto row = layout->createRow();
        // Use the pattern from working tests: (*row).set() instead of row->set()
        (*row).set(0, data.id);
        (*row).set(1, data.name);
        (*row).set(2, data.score);
        (*row).set(3, data.active);
        writer.writeRow(*row);
    }

    writer.flush();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void readFlexibleBCSV() {
    std::cout << "=== Reading with Flexible Interface ===\n\n";

    // Step 1: Create matching layout for reading
    // Must match the layout used for writing
    auto layout = bcsv::Layout::create();
    layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
    layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
    layout->insertColumn({"score", bcsv::ColumnDataType::FLOAT});
    layout->insertColumn({"active", bcsv::ColumnDataType::BOOL});

    // Step 2: Create a reader
    const std::string filename = "example_flexible.bcsv";
    bcsv::Reader<bcsv::Layout> reader(layout, filename);

    if (!reader.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }

    std::cout << "File contains " << reader.getRowCount() << " rows\n";
    std::cout << "Reading data:\n\n";

    // Step 3: Read rows using RowView
    bcsv::RowView rowView(layout);
    size_t rowIndex = 0;
    
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";
    
    while (reader.readRow(rowView)) {
        auto id = rowView.get<int32_t>(0);
        auto name = rowView.get<std::string>(1);
        auto score = rowView.get<float>(2);
        auto active = rowView.get<bool>(3);
        
        std::cout << std::setw(2) << id << " | " 
                  << std::setw(14) << std::left << name << " | "
                  << std::setw(5) << std::right << std::fixed << std::setprecision(1) << score << " | "
                  << (active ? "Yes" : "No") << "\n";
        rowIndex++;
    }

    reader.close();
    std::cout << "\nSuccessfully read " << rowIndex << " rows from " << filename << "\n\n";
}

int main() {
    std::cout << "BCSV Flexible Interface Example\n";
    std::cout << "===============================\n\n";
    std::cout << "This example demonstrates reading and writing BCSV files\n";
    std::cout << "using the flexible Layout/Row interface for runtime-defined schemas.\n\n";
    
    try {
        // Write data using flexible interface
        writeFlexibleBCSV();
        
        // Read data back using flexible interface
        readFlexibleBCSV();
        
        std::cout << "✓ Example completed successfully!\n";
        std::cout << "The flexible interface is ideal when you need to define\n";
        std::cout << "data structures at runtime or work with varying schemas.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
