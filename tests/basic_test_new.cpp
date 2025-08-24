#include <bcsv/bcsv.hpp>
#include <bcsv/layout.hpp>
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
        bcsv::Layout columnLayout;
        columnLayout.addColumn("id", bcsv::ColumnDataType::INT32);
        columnLayout.addColumn("name", bcsv::ColumnDataType::STRING);
        
        assert(columnLayout.getColumnCount() == 2);
        assert(columnLayout.getColumnType(0) == bcsv::ColumnDataType::INT32);
        assert(columnLayout.getColumnType(1) == bcsv::ColumnDataType::STRING);
        assert(columnLayout.getColumnIndex("id") == 0);
        assert(columnLayout.getColumnIndex("name") == 1);
        assert(columnLayout.hasColumn("id") == true);
        assert(columnLayout.hasColumn("unknown") == false);
        
        std::cout << "PASSED\n";
    }

    // Test FileHeader
    {
        std::cout << "Testing FileHeader... ";
        bcsv::Layout columnLayout;
        columnLayout.addColumn("test", bcsv::ColumnDataType::INT64);
        columnLayout.addColumn("data", bcsv::ColumnDataType::STRING);
        
        bcsv::FileHeader fileHeader;
        
        assert(fileHeader.isValidMagic() == true);
        assert(fileHeader.getVersionString() == "1.0.0");
        assert(fileHeader.getBinarySize(columnLayout) > 0);
        assert(fileHeader.getCompressionLevel() == 6);  // Default compression level (mandatory in v1.0+)
        assert(fileHeader.isCompressed() == true);      // Compression is always enabled in v1.0+
        
        std::cout << "PASSED\n";
    }

    std::cout << "\nAll tests passed successfully!\n";
    return 0;
}
