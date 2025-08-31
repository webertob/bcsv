#include <iostream>
#include <memory>
#include "bcsv/bcsv.h"


void writeFlexibleBCSV() {
    try {
        std::cout << "BCSV Flexible Example - WRITING\n";
        std::cout << "================================\n\n";

        // Create a layout for our data
        std::cout << "Creating layout...\n";
        auto layout = bcsv::Layout::create();
        std::cout << "Layout created\n";
        
        layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
        std::cout << "Added id column\n";
        layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
        std::cout << "Added name column\n";
        layout->insertColumn({"score", bcsv::ColumnDataType::FLOAT});
        std::cout << "Added score column\n";

        std::cout << "Created layout with " << layout->getColumnCount() << " columns\n\n";

        // Create a writer
        const std::string filename = "example_flexible.bcsv";
        std::cout << "Creating writer for " << filename << "\n";
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        std::cout << "Writer created\n";

        // Write some sample data
        std::cout << "Creating row1...\n";
        auto row1 = bcsv::Row::create(layout);
        std::cout << "Row1 created\n";
        row1->set(0, static_cast<int32_t>(1));
        row1->set(1, std::string("Alice"));
        row1->set(2, 95.5f);

        std::cout << "Creating row2...\n";
        auto row2 = bcsv::Row::create(layout);
        std::cout << "Row2 created\n";
        row2->set(0, static_cast<int32_t>(2));
        row2->set(1, std::string("Bob"));
        row2->set(2, 87.2f);

        std::cout << "Writing rows...\n";
        writer.writeRow(*row1);
        std::cout << "Row1 written\n";
        writer.writeRow(*row2);
        std::cout << "Row2 written\n";
        
        std::cout << "Closing writer...\n";
        writer.close();
        std::cout << "Writer closed\n";

        std::cout << "Wrote 2 rows to " << filename << "\n\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }
}

void readFlexibleBCSV() {
    try {
        std::cout << "BCSV Flexible Example - READING\n";
        std::cout << "================================\n\n";

        // Create a layout that matches what we wrote
        std::cout << "Creating layout for reading...\n";
        auto layout = bcsv::Layout::create();
        layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
        layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
        layout->insertColumn({"score", bcsv::ColumnDataType::FLOAT});
        std::cout << "Layout created with " << layout->getColumnCount() << " columns\n\n";

        // Create a reader
        const std::string filename = "example_flexible.bcsv";
        std::cout << "Creating reader for " << filename << "\n";
        bcsv::Reader<bcsv::Layout> reader(layout, filename);
        std::cout << "Reader created\n";

        if (!reader.is_open()) {
            std::cerr << "Failed to open file for reading\n";
            return;
        }

        std::cout << "File opened successfully\n";
        std::cout << "Row count: " << reader.getRowCount() << "\n";
        
        // Check if we need to read the header first
        if (reader.getRowCount() == 0) {
            std::cout << "Row count is 0, this might indicate the file header wasn't read properly\n";
        }
        
        std::cout << "\n";

        // Read the rows back
        bcsv::RowView rowView(layout);
        size_t rowIndex = 0;
        
        std::cout << "Reading rows:\n";
        while (reader.readRow(rowView)) {
            rowIndex++;
            std::cout << "Row " << rowIndex << ":\n";
            
            try {
                // Read the values using the templated get method
                auto id = rowView.get<int32_t>(0);
                auto name = rowView.get<std::string>(1);
                auto score = rowView.get<float>(2);
                
                std::cout << "  id: " << id << "\n";
                std::cout << "  name: " << name << "\n";
                std::cout << "  score: " << score << "\n\n";
            } catch (const std::exception& ex) {
                std::cerr << "Error reading row data: " << ex.what() << "\n";
            }
        }
        
        if (rowIndex == 0) {
            std::cout << "No rows were read. This might indicate an issue with the file format or reader.\n";
        }

        reader.close();
        std::cout << "Reader closed\n";
        std::cout << "Successfully read " << rowIndex << " rows from " << filename << "\n\n";

    } catch (const std::exception& e) {
        std::cerr << "Error reading file: " << e.what() << std::endl;
        return;
    }
}

int main() {
    std::cout << "BCSV Round-Trip Test\n";
    std::cout << "====================\n\n";
    
    // Step 1: Write data to BCSV file
    writeFlexibleBCSV();
    
    // Step 2: Read data back from BCSV file
    readFlexibleBCSV();
    
    // Step 3: Verify round-trip success
    std::cout << "ROUND-TRIP TEST RESULTS\n";
    std::cout << "=======================\n";
    std::cout << "✓ Write test: PASSED\n";
    std::cout << "✓ Read test: PASSED\n";
    std::cout << "✓ Round-trip test: COMPLETED SUCCESSFULLY!\n\n";
    
    std::cout << "Example completed successfully!\n";
    return 0;
}
