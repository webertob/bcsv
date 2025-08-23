#include <bcsv/bcsv.hpp>
#include <bcsv/column_layout.hpp>
#include <bcsv/file_header.hpp>
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

    // Test FileHeader
    {
        std::cout << "Testing FileHeader... ";
        bcsv::ColumnLayout columnLayout;
        columnLayout.addColumn("test", bcsv::ColumnDataType::INT64);
        columnLayout.addColumn("data", bcsv::ColumnDataType::STRING);
        
        bcsv::FileHeader fileHeader;
        
        assert(fileHeader.isValidMagic() == true);
        assert(fileHeader.getVersionString() == "1.0.0");
        assert(fileHeader.getBinarySize(columnLayout) > 0);
        assert(fileHeader.getCompressionLevel() == 0);
        assert(fileHeader.isCompressed() == false);
        
        std::cout << "PASSED\n";
    }

    std::cout << "\nAll tests passed successfully!\n";
    return 0;
}
