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
    bcsv::Layout layout;
    
    // Add columns with name and data type
    layout.addColumn({"id", bcsv::ColumnType::INT32});
    layout.addColumn({"name", bcsv::ColumnType::STRING});
    layout.addColumn({"score", bcsv::ColumnType::FLOAT});
    layout.addColumn({"active", bcsv::ColumnType::BOOL});
    std::cout << "Created layout with " << layout.getColumnCount() << " columns\n";

    // Step 2: Create a writer
    const std::string filename = "example_flexible.bcsv";
    bcsv::Writer<bcsv::Layout> writer(layout);
    if(!writer.open(filename, true)) {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
        return;
    }

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
        // Use the pattern from working tests: (*row).set() instead of row->set()
        writer.row.set(0, data.id);
        writer.row.set(1, data.name);
        writer.row.set(2, data.score);
        writer.row.set(3, data.active);
        if (!writer.writeRow()) {
            std::cerr << "Failed to write row\n";
            break;
        }
    }

    writer.flush();
    std::cout << "Successfully wrote " << sampleData.size() << " rows to " << filename << "\n\n";
}

void readFlexibleBCSV() {
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
    if (!reader.getLayout().isCompatibleWith(layoutExpected)) {
        std::cerr << "Error: File layout is not compatible with expected layout\n";
        reader.close();
        return;
    }

    //Optional: Compare column names
    for (size_t i = 0; i < layoutExpected.getColumnCount(); i++) {
        if (layoutExpected.getColumnName(i) != reader.getLayout().getColumnName(i)) {
            std::cerr << "Warning: Column name mismatch at index " << i
                      << " (expected: " << layoutExpected.getColumnName(i)
                      << ", found: " << reader.getLayout().getColumnName(i) << ")\n";
        }
    }


    std::cout << "Reading data:\n\n";
    // Table header
    std::cout << "ID | Name           | Score | Active\n";
    std::cout << "---|----------------|-------|-------\n";

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
        rowIndex = reader.getCurrentRowIndex();
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
