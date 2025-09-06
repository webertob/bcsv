#include <iostream>
#include <memory>
#include <iomanip>
#include "bcsv/bcsv.h"

/**
 * BCSV Static Interface Example
 * 
 * This example demonstrates the compile-time static LayoutStatic and RowStatic
 * interface for writing and reading BCSV files. The static interface provides
 * better performance by defining the data structure at compile time with
 * template parameters.
 */

// Define our data structure using LayoutStatic template
using ExampleLayout = bcsv::LayoutStatic<
    int32_t,        // id
    std::string,    // name
    float,          // score
    bool            // active
>;

void writeStaticBCSV() {
    std::cout << "=== Writing with Static Interface ===\n\n";

    // Step 1: Create static layout with column names
    std::vector<std::string> columnNames = {"id", "name", "score", "active"};
    auto layout = ExampleLayout::create(columnNames);
    
    std::cout << "Created static layout with " << layout->getColumnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_static.bcsv";
    bcsv::Writer<ExampleLayout> writer(layout, filename, true);

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
        // Use the pattern from working tests: (*row).set<N>() instead of row->set<N>()
        (*row).set<0>(data.id);
        (*row).set<1>(data.name);
        (*row).set<2>(data.score);
        (*row).set<3>(data.active);
        writer.writeRow(*row);
    }

    writer.close();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void readStaticBCSV() {
    std::cout << "=== Reading with Static Interface ===\n\n";

    // Step 1: Create matching layout for reading
    std::vector<std::string> columnNames = {"id", "name", "score", "active"};
    auto layout = ExampleLayout::create(columnNames);

    // Step 2: Create a reader
    const std::string filename = "example_static.bcsv";
    bcsv::Reader<ExampleLayout> reader(layout, filename);

    if (!reader.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }

    std::cout << "File contains " << reader.getRowCount() << " rows\n";
    std::cout << "Reading data:\n\n";

    // Step 3: Read rows using static RowViewType
    typename ExampleLayout::RowViewType rowView(layout);
    size_t rowIndex = 0;
    
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";
    
    while (reader.readRow(rowView)) {
        // Use template get<N>() method for type-safe access
        auto id = rowView.get<0>();
        auto name = rowView.get<1>();
        auto score = rowView.get<2>();
        auto active = rowView.get<3>();
        
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
    std::cout << "BCSV Static Interface Example\n";
    std::cout << "=============================\n\n";
    std::cout << "This example demonstrates reading and writing BCSV files\n";
    std::cout << "using the static LayoutStatic/RowStatic interface for compile-time schemas.\n\n";
    
    try {
        // Write data using static interface
        writeStaticBCSV();
        
        // Read data back using static interface
        readStaticBCSV();
        
        std::cout << "✓ Example completed successfully!\n";
        std::cout << "The static interface provides better performance through\n";
        std::cout << "compile-time type checking and template optimization.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
