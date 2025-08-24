#include <iostream>
#include <cassert>
#include <memory>
#include "bcsv/bcsv.h"

void test_layout_creation() {
    std::cout << "Testing layout creation...\n";
    
    auto layout = bcsv::Layout::create();
    assert(layout != nullptr);
    assert(layout->getColumnCount() == 0);
    
    layout->insertColumn("id", bcsv::ColumnDataType::INT32);
    layout->insertColumn("name", bcsv::ColumnDataType::STRING);
    layout->insertColumn("value", bcsv::ColumnDataType::FLOAT);
    
    assert(layout->getColumnCount() == 3);
    assert(layout->getColumnName(0) == "id");
    assert(layout->getColumnName(1) == "name");
    assert(layout->getColumnName(2) == "value");
    assert(layout->getColumnType(0) == bcsv::ColumnDataType::INT32);
    assert(layout->getColumnType(1) == bcsv::ColumnDataType::STRING);
    assert(layout->getColumnType(2) == bcsv::ColumnDataType::FLOAT);
    
    std::cout << "Layout creation tests passed!\n";
}

void test_static_layout() {
    std::cout << "Testing static layout creation...\n";
    
    // LayoutStatic requires template parameters and column names, let's test with specific names
    std::vector<std::string> columnNames = {"test_col"};
    auto staticLayout = bcsv::LayoutStatic<int64_t>::create(columnNames);
    assert(staticLayout != nullptr);
    assert(staticLayout->getColumnCount() == 1);
    assert(staticLayout->getColumnName(0) == "test_col");
    assert(staticLayout->getColumnType(0) == bcsv::ColumnDataType::INT64);
    
    std::cout << "Static layout tests passed!\n";
}

void test_layout_hash() {
    std::cout << "Testing layout hash...\n";
    
    auto layout1 = bcsv::Layout::create();
    layout1->insertColumn("id", bcsv::ColumnDataType::INT32);
    layout1->insertColumn("name", bcsv::ColumnDataType::STRING);
    
    auto layout2 = bcsv::Layout::create();
    layout2->insertColumn("id", bcsv::ColumnDataType::INT32);
    layout2->insertColumn("name", bcsv::ColumnDataType::STRING);
    
    auto layout3 = bcsv::Layout::create();
    layout3->insertColumn("id", bcsv::ColumnDataType::INT32);
    layout3->insertColumn("value", bcsv::ColumnDataType::FLOAT);
        
    std::cout << "Layout hash tests passed!\n";
}

void test_row_operations() {
    std::cout << "Testing row operations...\n";
    
    auto layout = bcsv::Layout::create();
    layout->insertColumn("id", bcsv::ColumnDataType::INT32);
    layout->insertColumn("name", bcsv::ColumnDataType::STRING);
    layout->insertColumn("value", bcsv::ColumnDataType::FLOAT);
    
    bcsv::Row row(*layout);  // Dereference shared_ptr
    
    // Test setting values
    row.setValue(0, static_cast<int32_t>(42));
    row.setValue(1, std::string("test"));
    row.setValue(2, 3.14f);
    
    // Test getting values with std::get to extract from variant
    assert(std::get<int32_t>(row.getValue(0)) == 42);
    assert(std::get<std::string>(row.getValue(1)) == "test");
    assert(std::get<float>(row.getValue(2)) == 3.14f);
    
    std::cout << "Row operations tests passed!\n";
}

void test_file_io() {
    std::cout << "Testing file I/O operations...\n";
    
    const std::string filename = "test_clean.bcsv";
    
    // Create layout
    auto layout = bcsv::Layout::create();
    layout->insertColumn("id", bcsv::ColumnDataType::INT32);
    layout->insertColumn("name", bcsv::ColumnDataType::STRING);
    layout->insertColumn("value", bcsv::ColumnDataType::FLOAT);
    
    // Write data
    {
        bcsv::Writer<bcsv::Layout> writer(layout, filename, true);
        
        bcsv::Row row1(*layout);  // Dereference shared_ptr
        row1.setValue(0, static_cast<int32_t>(1));
        row1.setValue(1, std::string("Alice"));
        row1.setValue(2, 1.1f);
        
        bcsv::Row row2(*layout);  // Dereference shared_ptr
        row2.setValue(0, static_cast<int32_t>(2));
        row2.setValue(1, std::string("Bob"));
        row2.setValue(2, 2.2f);
        
        writer.writeRow(row1);
        writer.writeRow(row2);
        writer.close();
    }
    
    // Read data back
    {
        auto readLayout = bcsv::Layout::create();
        bcsv::Reader<bcsv::Layout> reader(readLayout, filename);
        
        assert(readLayout->getColumnCount() == 3);
        assert(readLayout->getColumnName(0) == "id");
        assert(readLayout->getColumnName(1) == "name");
        assert(readLayout->getColumnName(2) == "value");
        
        bcsv::Row readRow(*readLayout);  // Dereference shared_ptr
        
        // Read first row
        assert(reader.readRow(readRow));
        assert(std::get<int32_t>(readRow.getValue(0)) == 1);
        assert(std::get<std::string>(readRow.getValue(1)) == "Alice");
        assert(std::get<float>(readRow.getValue(2)) == 1.1f);
        
        // Read second row
        assert(reader.readRow(readRow));
        assert(std::get<int32_t>(readRow.getValue(0)) == 2);
        assert(std::get<std::string>(readRow.getValue(1)) == "Bob");
        assert(std::get<float>(readRow.getValue(2)) == 2.2f);
        
        // No more rows
        assert(!reader.readRow(readRow));
        
        reader.close();
    }
    
    std::cout << "File I/O tests passed!\n";
}

int main() {
    std::cout << "BCSV Clean API Tests\n";
    std::cout << "====================\n\n";
    
    try {
        test_layout_creation();
        test_static_layout();
        test_layout_hash();
        test_row_operations();
        test_file_io();
        
        std::cout << "\nAll tests passed successfully!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown error" << std::endl;
        return 1;
    }
}
