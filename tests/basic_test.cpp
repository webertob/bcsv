#include <bcsv/bcsv.h>
#include <iostream>
#include <cassert>
#include <sstream>
#include <cmath>

int main() {
    std::cout << "Running basic BCSV library tests...\n";

    // Test ColumnLayout
    {
        std::cout << "Testing ColumnLayout... ";
        bcsv::ColumnLayout columnLayout;
        columnLayout.addColumn("id", bcsv::ColumnDataType::INT32);
        columnLayout.addColumn("name", bcsv::ColumnDataType::STRING);
        
        assert(columnLayout.getColumnCount() == 2);
        assert(columnLayout.getDataType(0) == bcsv::ColumnDataType::INT32);
        assert(columnLayout.getDataType(1) == bcsv::ColumnDataType::STRING);
        assert(columnLayout.getIndex("id") == 0);
        assert(columnLayout.getIndex("name") == 1);
        assert(columnLayout.hasColumn("id") == true);
        assert(columnLayout.hasColumn("unknown") == false);
        
        std::cout << "PASSED\n";
    }

    // Test Row
    {
        std::cout << "Testing Row... ";
        bcsv::ColumnLayout columnLayout;
        columnLayout.addColumn("id", bcsv::ColumnDataType::INT64);
        columnLayout.addColumn("name", bcsv::ColumnDataType::STRING);
        
        bcsv::Row row(columnLayout);
        row.setValue("id", static_cast<int64_t>(42));
        row.setValue("name", std::string("test"));
        
        assert(std::get<int64_t>(row.getValue("id")) == 42);
        assert(std::get<std::string>(row.getValue("name")) == "test");
        assert(row.getColumnCount() == 2);
        assert(row.hasColumn("id") == true);
        assert(row.hasColumn("unknown") == false);
        
        std::cout << "PASSED\n";
    }

    std::cout << "\nAll tests passed successfully!\n";
    return 0;
}
