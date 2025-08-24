#include <iostream>
#include <memory>
#include "bcsv/bcsv.h"

int main() {
    try {
        std::cout << "BCSV Simple Example\n";
        std::cout << "===================\n\n";

        // Create a layout for our data
        auto layout = bcsv::Layout::create();
        layout->insertColumn("id", bcsv::ColumnDataType::INT32);
        layout->insertColumn("name", bcsv::ColumnDataType::STRING);
        layout->insertColumn("value", bcsv::ColumnDataType::FLOAT);
        
        std::cout << "Created layout with " << layout->getColumnCount() << " columns:\n";
        for (size_t i = 0; i < layout->getColumnCount(); ++i) {
            std::cout << "  [" << i << "] " << layout->getColumnName(i) 
                      << " (" << layout->getColumnTypeAsString(i) << ")\n";
        }
        std::cout << "\n";

        // Create a writer
        const std::string filename = "simple_example.bcsv";
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        
        std::cout << "Created writer for file: " << filename << "\n";
        
        // Write some data rows
        bcsv::Row row1(*layout);  // Dereference shared_ptr
        row1.setValue(0, static_cast<int32_t>(1));
        row1.setValue(1, std::string("Alice"));
        row1.setValue(2, 3.14f);
        
        bcsv::Row row2(*layout);  // Dereference shared_ptr
        row2.setValue(0, static_cast<int32_t>(2));
        row2.setValue(1, std::string("Bob"));
        row2.setValue(2, 2.71f);
        
        std::cout << "Writing rows...\n";
        writer.writeRow(row1);
        writer.writeRow(row2);
        
        writer.close();
        std::cout << "Wrote 2 rows successfully.\n\n";

        // Read the data back
        auto readLayout = bcsv::Layout::create();
        bcsv::Reader<bcsv::Layout> reader(readLayout, filename);
        
        std::cout << "Reading back data...\n";
        std::cout << "File layout has " << readLayout->getColumnCount() << " columns:\n";
        for (size_t i = 0; i < readLayout->getColumnCount(); ++i) {
            std::cout << "  [" << i << "] " << readLayout->getColumnName(i) 
                      << " (" << readLayout->getColumnTypeAsString(i) << ")\n";
        }
        std::cout << "\n";
        
        bcsv::Row readRow(*readLayout);  // Dereference shared_ptr
        int rowCount = 0;
        while (reader.readRow(readRow)) {
            rowCount++;
            std::cout << "Row " << rowCount << ": ";
            std::cout << "id=" << std::get<int32_t>(readRow.getValue(0)) << ", ";
            std::cout << "name=\"" << std::get<std::string>(readRow.getValue(1)) << "\", ";
            std::cout << "value=" << std::get<float>(readRow.getValue(2)) << "\n";
        }
        
        reader.close();
        std::cout << "\nRead " << rowCount << " rows successfully.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
