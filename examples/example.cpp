#include <iostream>
#include <memory>
#include "bcsv/bcsv.h"


void writeFlexibleBCSV() {
    try {
        std::cout << "BCSV Flexible Example\n";
        std::cout << "=====================\n\n";

        // Create a layout for our data
        auto layout = bcsv::Layout::create();
        layout->insertColumn({"id", bcsv::ColumnDataType::INT32});
        layout->insertColumn({"name", bcsv::ColumnDataType::STRING});
        layout->insertColumn({"score", bcsv::ColumnDataType::FLOAT});

        std::cout << "Created layout with " << layout->getColumnCount() << " columns\n\n";

        // Create a writer
        const std::string filename = "example_flexible.bcsv";
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);

        // Write some sample data
        auto row1 = bcsv::Layout::RowType::create(layout);
        row1->set(0, static_cast<int32_t>(1));
        row1->set(1, std::string("Alice"));
        row1->set(2, 95.5f);

        auto row2 = bcsv::Layout::RowType::create(layout);
        row2->set(0, static_cast<int32_t>(2));
        row2->set(1, std::string("Bob"));
        row2->set(2, 87.2f);

        writer.writeRow(*row1);
        writer.writeRow(row2);
        writer.close();

        std::cout << "Wrote 2 rows to " << filename << "\n\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main() {

    writeFlexibleBCSV();
    //readFlexibleBCSV();
    //writeStaticBCSV();
    //readStaticBCSV();
    return 0;
}
