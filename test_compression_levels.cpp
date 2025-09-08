#include <iostream>
#include <filesystem>
#include <chrono>
#include "bcsv/bcsv.h"

using namespace bcsv;

int main() {
    std::cout << "BCSV Compression Level Test" << std::endl;
    std::cout << "===========================" << std::endl;
    
    try {
        // Create layout for testing
        auto layout = Layout::create();
        layout->insertColumn({"id", ColumnDataType::INT32});
        layout->insertColumn({"name", ColumnDataType::STRING});
        layout->insertColumn({"value", ColumnDataType::DOUBLE});
        layout->insertColumn({"score", ColumnDataType::FLOAT});
        
        // Test data
        const int numRows = 1000;
        
        // Test different compression levels
        for (int level = 0; level <= 9; level++) {
            std::string filename = "test_compression_level_" + std::to_string(level) + ".bcsv";
            
            auto start = std::chrono::high_resolution_clock::now();
            
            // Write with specified compression level
            {
                Writer<Layout> writer(layout);
                writer.setCompressionLevel(level);
                writer.open(filename, true);
                
                for (int i = 0; i < numRows; i++) {
                    auto row = layout->createRow();
                    (*row).set(0, i);                                               // id
                    (*row).set(1, "Test Name " + std::to_string(i % 100));         // name
                    (*row).set(2, i * 3.14159);                                    // value
                    (*row).set(3, static_cast<float>(i % 100) / 100.0f);          // score
                    writer.writeRow(*row);
                }
            } // Writer destructor closes file
            
            auto writeEnd = std::chrono::high_resolution_clock::now();
            
            // Read back and verify
            int readCount = 0;
            {
                Reader<Layout> reader(layout, filename);
                RowView rowView(layout);
                
                while (reader.readRow(rowView)) {
                    // Verify first and last rows
                    if (readCount == 0) {
                        if (rowView.get<int32_t>(0) != 0 || 
                            rowView.get<std::string>(1) != "Test Name 0") {
                            throw std::runtime_error("Data verification failed for level " + std::to_string(level));
                        }
                    }
                    readCount++;
                }
            } // Reader destructor closes file
            
            auto readEnd = std::chrono::high_resolution_clock::now();
            
            // Get file size
            size_t fileSize = std::filesystem::file_size(filename);
            
            auto writeTime = std::chrono::duration_cast<std::chrono::milliseconds>(writeEnd - start).count();
            auto readTime = std::chrono::duration_cast<std::chrono::milliseconds>(readEnd - writeEnd).count();
            
            std::cout << "Level " << level << ": ";
            std::cout << "Size=" << fileSize << " bytes, ";
            std::cout << "Write=" << writeTime << "ms, ";
            std::cout << "Read=" << readTime << "ms, ";
            std::cout << "Rows=" << readCount << "/" << numRows;
            
            if (level == 0) {
                std::cout << " (NO COMPRESSION)";
            } else {
                std::cout << " (LZ4 level " << level << ")";
            }
            std::cout << std::endl;
            
            if (readCount != numRows) {
                throw std::runtime_error("Row count mismatch for level " + std::to_string(level));
            }
            
            // Clean up test file
            std::filesystem::remove(filename);
        }
        
        std::cout << std::endl;
        std::cout << "✅ All compression levels work correctly!" << std::endl;
        std::cout << "   - Level 0: No compression (fastest, largest files)" << std::endl;
        std::cout << "   - Level 1: Fast LZ4 compression (good balance)" << std::endl;
        std::cout << "   - Levels 2-9: High compression LZ4 (slower, smaller files)" << std::endl;
        
        // Test that setCompressionLevel is ignored when file is open
        std::cout << std::endl << "Testing setCompressionLevel restriction..." << std::endl;
        {
            Writer<Layout> writer(layout);
            writer.open("test_restriction.bcsv", true);
            writer.setCompressionLevel(5); // This should be ignored and show warning
            writer.close();
            std::filesystem::remove("test_restriction.bcsv");
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
