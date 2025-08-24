#include <iostream>
#include <memory>
#include "bcsv/bcsv.h"

int main() {
    try {
        std::cout << "BCSV Basic Example\n";
        std::cout << "==================\n\n";

        // Create a layout for our data
        auto layout = bcsv::Layout::create();
        layout->insertColumn("id", bcsv::ColumnDataType::INT32);
        layout->insertColumn("name", bcsv::ColumnDataType::STRING);
        layout->insertColumn("score", bcsv::ColumnDataType::FLOAT);
        
        std::cout << "Created layout with " << layout->getColumnCount() << " columns\n\n";

        // Create a writer
        const std::string filename = "example.bcsv";
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        
        // Write some sample data
        bcsv::Row row1(*layout);  // Dereference shared_ptr
        row1.setValue(0, static_cast<int32_t>(1));
        row1.setValue(1, std::string("Alice"));
        row1.setValue(2, 95.5f);
        
        bcsv::Row row2(*layout);  // Dereference shared_ptr
        row2.setValue(0, static_cast<int32_t>(2));
        row2.setValue(1, std::string("Bob"));
        row2.setValue(2, 87.2f);
        
        writer.writeRow(row1);
        writer.writeRow(row2);
        writer.close();
        
        std::cout << "Wrote 2 rows to " << filename << "\n\n";

        // Read the data back
        auto readLayout = bcsv::Layout::create();
        bcsv::Reader<bcsv::Layout> reader(readLayout, filename);
        
        std::cout << "Reading data back:\n";
        bcsv::Row readRow(*readLayout);  // Dereference shared_ptr
        while (reader.readRow(readRow)) {
            std::cout << "ID: " << std::get<int32_t>(readRow.getValue(0));
            std::cout << ", Name: " << std::get<std::string>(readRow.getValue(1));
            std::cout << ", Score: " << std::get<float>(readRow.getValue(2)) << "\n";
        }
        
        reader.close();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
