#include <bcsv/bcsv.h>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>

int main() {
    std::cout << "BCSV Library Example\n";
    std::cout << "====================\n\n";

    try {
        // Create a columnLayout with field definitions
        bcsv::ColumnLayout columnLayout;
        columnLayout.addColumn("name", bcsv::ColumnDataType::STRING);
        columnLayout.addColumn("age", bcsv::ColumnDataType::INT64);
        columnLayout.addColumn("salary", bcsv::ColumnDataType::DOUBLE);

        std::cout << "Created ColumnLayout with fields:\n";
        for (size_t i = 0; i < columnLayout.getColumnCount(); ++i) {
            std::cout << "  " << columnLayout.getName(i) << " (" << columnLayout.getDataTypeAsString(i) << ")\n";
        }
        std::cout << "\n";

        // Create a file header and set it from the ColumnLayout
        bcsv::FileHeader fileHeader(1, 0, 0); // version 1.0.0
        fileHeader.setCompressionLevel(0); // No compression

        std::cout << "Created FileHeader:\n";
        std::cout << "  Version: " << fileHeader.getVersionString() << "\n";
        std::cout << "  Compressed: " << (fileHeader.isCompressed() ? "Yes" : "No") << "\n";
        std::cout << "  Compression Level: " << static_cast<int>(fileHeader.getCompressionLevel()) << "\n";
        std::cout << "  Column Count: " << columnLayout.getColumnCount() << "\n";
        std::cout << "  Magic Number: 0x" << std::hex << fileHeader.getMagic() << std::dec << "\n";
        std::cout << "  Binary Size: " << fileHeader.getBinarySize(columnLayout) << " bytes\n";
        
        std::cout << "\nDetailed binary layout:\n";
        fileHeader.printBinaryLayout(columnLayout);
        std::cout << "\n";

        // Create a columnLayout with field definitions (this would normally be the same as above)
        // but we're showing how to create it separately for demonstration
        bcsv::ColumnLayout workingColumnLayout;
        workingColumnLayout.addColumn("name", bcsv::ColumnDataType::STRING);
        workingColumnLayout.addColumn("age", bcsv::ColumnDataType::INT64);
        workingColumnLayout.addColumn("salary", bcsv::ColumnDataType::DOUBLE);

        std::cout << "Created ColumnLayout with fields:\n";
        for (size_t i = 0; i < workingColumnLayout.getColumnCount(); ++i) {
            std::cout << "  " << workingColumnLayout.getName(i) << " (" << workingColumnLayout.getDataTypeAsString(i) << ")\n";
        }
        std::cout << "\n";

        // Create rows with data
        bcsv::Row row1(workingColumnLayout);
        row1.setValue("name", std::string("John Doe"));
        row1.setValue("age", static_cast<int64_t>(30));
        row1.setValue("salary", 75000.50);

        bcsv::Row row2(workingColumnLayout);
        row2.setValue("name", std::string("Jane Smith"));
        row2.setValue("age", static_cast<int64_t>(28));
        row2.setValue("salary", 82000.75);

        std::cout << "Created sample rows:\n";
        std::cout << "Row 1:\n";
        try {
            auto nameVal = std::get<std::string>(row1.getValue("name"));
            auto ageVal = std::get<int64_t>(row1.getValue("age"));
            auto salaryVal = std::get<double>(row1.getValue("salary"));
            std::cout << "  Name: " << nameVal << ", Age: " << ageVal << ", Salary: " << salaryVal << "\n";
        } catch (const std::bad_variant_access& e) {
            std::cout << "  Error accessing row data: " << e.what() << "\n";
        }

        std::cout << "Row 2:\n";
        try {
            auto nameVal = std::get<std::string>(row2.getValue("name"));
            auto ageVal = std::get<int64_t>(row2.getValue("age"));
            auto salaryVal = std::get<double>(row2.getValue("salary"));
            std::cout << "  Name: " << nameVal << ", Age: " << ageVal << ", Salary: " << salaryVal << "\n";
        } catch (const std::bad_variant_access& e) {
            std::cout << "  Error accessing row data: " << e.what() << "\n";
        }
        std::cout << "\n";

        // Demonstrate Writer usage
        std::cout << "Creating a Writer instance...\n";
        bcsv::Writer<> writer("example_output.bcsv");
        writer.setCompression(false);
        
        std::cout << "Writing file header...\n";
        if (writer.writeFileHeader(fileHeader)) {
            std::cout << "  File header written successfully\n";
        }

        std::cout << "Writing columnLayout...\n";
        if (writer.writeColumnLayout(workingColumnLayout)) {
            std::cout << "  ColumnLayout written successfully\n";
        }

        std::cout << "Writing rows...\n";
        if (writer.writeRow(row1)) {
            std::cout << "  Row 1 written successfully\n";
        }
        if (writer.writeRow(row2)) {
            std::cout << "  Row 2 written successfully\n";
        }

        writer.flush();
        writer.close();
        std::cout << "\n";

        // Demonstrate Reader usage
        std::cout << "Creating a Reader instance...\n";
        bcsv::Reader<> reader("example_output.bcsv");
        
        std::cout << "Reading file header...\n";
        if (reader.readFileHeader()) {
            const auto& readFileHeader = reader.getFileHeader();
            std::cout << "  Version: " << readFileHeader.getVersionString() << "\n";
        }

        std::cout << "Reading columnLayout...\n";
        if (reader.readColumnLayout()) {
            const auto& readColumnLayout = reader.getColumnLayout();
            std::cout << "  Fields: " << readColumnLayout.getColumnCount() << "\n";
        }

        reader.close();

        // Demonstrate binary header I/O
        std::cout << "\nDemonstrating binary header I/O:\n";
        std::stringstream binaryStream;
        
        std::cout << "Writing FileHeader to binary stream...\n";
        if (fileHeader.writeToBinary(binaryStream, columnLayout)) {
            std::cout << "  Written " << fileHeader.getBinarySize(columnLayout) << " bytes\n";
            
            // Read it back
            bcsv::FileHeader readHeader;
            bcsv::ColumnLayout restoredColumnLayout;
            binaryStream.seekg(0);
            
            std::cout << "Reading FileHeader from binary stream...\n";
            if (readHeader.readFromBinary(binaryStream, restoredColumnLayout)) {
                std::cout << "  Read successfully!\n";
                std::cout << "  Version: " << readHeader.getVersionString() << "\n";
                std::cout << "  Column Count: " << restoredColumnLayout.getColumnCount() << "\n";
                std::cout << "  Magic valid: " << (readHeader.isValidMagic() ? "Yes" : "No") << "\n";
                
                // Check column data types and names
                for (size_t i = 0; i < restoredColumnLayout.getColumnCount(); ++i) {
                    auto type = restoredColumnLayout.getDataType(i);
                    std::string typeName = restoredColumnLayout.getDataTypeAsString(i);
                    std::cout << "  Column " << i << ": " << typeName << " \"" << restoredColumnLayout.getName(i) << "\"\n";
                }
            } else {
                std::cout << "  Failed to read header!\n";
            }
        } else {
            std::cout << "  Failed to write header!\n";
        }

        std::cout << "\nDemonstrating new data types:\n";
        
        // Create a ColumnLayout with various data types
        bcsv::ColumnLayout multiTypeColumnLayout;
        multiTypeColumnLayout.addColumn("name", bcsv::ColumnDataType::STRING);
        multiTypeColumnLayout.addColumn("level", bcsv::ColumnDataType::UINT8);
        multiTypeColumnLayout.addColumn("score", bcsv::ColumnDataType::INT16);
        multiTypeColumnLayout.addColumn("population", bcsv::ColumnDataType::UINT32);
        multiTypeColumnLayout.addColumn("distance", bcsv::ColumnDataType::INT64);
        multiTypeColumnLayout.addColumn("temperature", bcsv::ColumnDataType::FLOAT);
        multiTypeColumnLayout.addColumn("precision", bcsv::ColumnDataType::DOUBLE);
        multiTypeColumnLayout.addColumn("active", bcsv::ColumnDataType::BOOL);
        
        std::cout << "Created multi-type columnLayout with " << multiTypeColumnLayout.getColumnCount() << " columns:\n";
        for (size_t i = 0; i < multiTypeColumnLayout.getColumnCount(); ++i) {
            std::cout << "  Column " << i << ": " << multiTypeColumnLayout.getDataTypeAsString(i) << " \"" << multiTypeColumnLayout.getName(i) << "\"\n";
        }
        
        // Create a row with various data types
        bcsv::ColumnLayout multiTypeFieldColumnLayout;
        multiTypeFieldColumnLayout.addColumn("name", "string");
        multiTypeFieldColumnLayout.addColumn("level", "uint8");
        multiTypeFieldColumnLayout.addColumn("score", "int16");
        multiTypeFieldColumnLayout.addColumn("population", "uint32");
        multiTypeFieldColumnLayout.addColumn("distance", "int64");
        multiTypeFieldColumnLayout.addColumn("temperature", "float");
        multiTypeFieldColumnLayout.addColumn("precision", "double");
        multiTypeFieldColumnLayout.addColumn("active", "bool");
        
        bcsv::Row multiTypeRow(multiTypeFieldColumnLayout);
        multiTypeRow.setValue("name", std::string("Test"));
        multiTypeRow.setValue("level", static_cast<uint8_t>(42));
        multiTypeRow.setValue("score", static_cast<int16_t>(-1234));
        multiTypeRow.setValue("population", static_cast<uint32_t>(1000000));
        multiTypeRow.setValue("distance", static_cast<int64_t>(-9876543210LL));
        multiTypeRow.setValue("temperature", 23.5f);
        multiTypeRow.setValue("precision", 3.141592653589793);
        multiTypeRow.setValue("active", true);
        
        std::cout << "\nMulti-type row values:\n";
        std::cout << "  name: " << std::get<std::string>(multiTypeRow.getValue("name")) << "\n";
        std::cout << "  level: " << static_cast<int>(std::get<uint8_t>(multiTypeRow.getValue("level"))) << "\n";
        std::cout << "  score: " << std::get<int16_t>(multiTypeRow.getValue("score")) << "\n";
        std::cout << "  population: " << std::get<uint32_t>(multiTypeRow.getValue("population")) << "\n";
        std::cout << "  distance: " << std::get<int64_t>(multiTypeRow.getValue("distance")) << "\n";
        std::cout << "  temperature: " << std::get<float>(multiTypeRow.getValue("temperature")) << "\n";
        std::cout << "  precision: " << std::get<double>(multiTypeRow.getValue("precision")) << "\n";
        std::cout << "  active: " << (std::get<bool>(multiTypeRow.getValue("active")) ? "true" : "false") << "\n";

        // Demonstrate PacketHeader usage (Packet class not implemented yet)
        std::cout << "\nDemonstrating PacketHeader class:\n";
        
        // Create a simple packet buffer for demonstration
        bcsv::PacketHeader header;
        header.magic = bcsv::PCKT_MAGIC;
        header.payloadSizeRaw = 5;
        header.payloadSizeZip = 5;
        header.rowFirst = 0;
        header.rowCount = 1;
        
        std::vector<char> packetBuffer;
        packetBuffer.resize(sizeof(header) + sizeof(uint32_t) + 5); // header + crc + data
        std::memcpy(packetBuffer.data(), &header, sizeof(header));
        
        // Add some sample data
        const char* sampleData = "Hello";
        std::memcpy(packetBuffer.data() + sizeof(header) + sizeof(uint32_t), sampleData, 5);
        
        // Update CRC32
        bcsv::PacketHeader::updateCRC32(packetBuffer);
        
        std::cout << "Created packet:\n";
        std::cout << "  Type: HEADER_DEMO\n";
        std::cout << "  Has CRC32: Yes\n";
        std::cout << "  Data size: 5 bytes\n";

        std::cout << "\nExample completed successfully!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
