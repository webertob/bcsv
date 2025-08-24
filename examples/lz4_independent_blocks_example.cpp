#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <lz4.h>

class CSVDataGenerator {
public:
    struct Person {
        std::string firstName;
        std::string lastName;
        std::string email;
        std::string city;
        std::string country;
        int age;
        double salary;
        std::string department;
        std::string jobTitle;
        std::string phoneNumber;
    };

    CSVDataGenerator() : gen(std::random_device{}()) {
        // Initialize sample data
        firstNames = {"John", "Jane", "Michael", "Sarah", "David", "Lisa", "Robert", "Emily", 
                     "William", "Jessica", "James", "Ashley", "Christopher", "Amanda", "Daniel"};
        
        lastNames = {"Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller", 
                    "Davis", "Rodriguez", "Martinez", "Hernandez", "Lopez", "Gonzalez", "Wilson"};
        
        cities = {"New York", "Los Angeles", "Chicago", "Houston", "Phoenix", "Philadelphia",
                 "San Antonio", "San Diego", "Dallas", "San Jose", "Austin", "Jacksonville"};
        
        countries = {"USA", "Canada", "UK", "Germany", "France", "Japan", "Australia", "Brazil"};
        
        departments = {"Engineering", "Sales", "Marketing", "HR", "Finance", "Operations", 
                      "Customer Service", "IT", "Legal", "R&D"};
        
        jobTitles = {"Manager", "Director", "Senior Developer", "Analyst", "Specialist", 
                    "Coordinator", "Associate", "Vice President", "Consultant", "Engineer"};
    }

    std::string generateCSVHeader() {
        return "FirstName,LastName,Email,City,Country,Age,Salary,Department,JobTitle,PhoneNumber\n";
    }

    std::string generateCSVRow() {
        Person person;
        person.firstName = getRandomElement(firstNames);
        person.lastName = getRandomElement(lastNames);
        person.email = person.firstName + "." + person.lastName + "@company.com";
        person.city = getRandomElement(cities);
        person.country = getRandomElement(countries);
        person.age = 22 + (gen() % 43); // Age between 22-64
        person.salary = 30000 + (gen() % 120000); // Salary between 30k-150k
        person.department = getRandomElement(departments);
        person.jobTitle = getRandomElement(jobTitles);
        person.phoneNumber = generatePhoneNumber();

        std::stringstream ss;
        ss << person.firstName << "," << person.lastName << "," << person.email << ","
           << person.city << "," << person.country << "," << person.age << ","
           << std::fixed << std::setprecision(2) << person.salary << ","
           << person.department << "," << person.jobTitle << "," << person.phoneNumber << "\n";
        
        return ss.str();
    }

private:
    std::mt19937 gen;
    std::vector<std::string> firstNames, lastNames, cities, countries, departments, jobTitles;

    std::string getRandomElement(const std::vector<std::string>& vec) {
        return vec[gen() % vec.size()];
    }

    std::string generatePhoneNumber() {
        std::stringstream ss;
        ss << "+1-" << (200 + gen() % 800) << "-" << (100 + gen() % 900) << "-" << (1000 + gen() % 9000);
        return ss.str();
    }
};

// Block index entry for fast random access
struct BlockIndex {
    uint64_t fileOffset;        // Position in file where block starts
    uint32_t compressedSize;    // Size of compressed block
    uint32_t uncompressedSize;  // Size when decompressed
    uint32_t firstRowNumber;    // First row number in this block (0-based)
    uint32_t rowCount;          // Number of rows in this block
    uint32_t checksum;          // Block checksum
};

// Independent block header
struct IndependentBlockHeader {
    uint32_t magic;             // Magic number for verification
    uint32_t compressedSize;    // Size of compressed data following this header
    uint32_t uncompressedSize;  // Size when decompressed
    uint32_t rowCount;          // Number of rows in this block
    uint32_t firstRowNumber;    // First row number (0-based)
    uint32_t checksum;          // Simple checksum for integrity
    
    static constexpr uint32_t MAGIC_NUMBER = 0x4C5A3442; // "LZ4B" in hex
};

class LZ4IndependentBlockCompressor {
private:
    std::ofstream outFile;
    std::ofstream indexFile;
    std::vector<char> compressionBuffer;
    std::vector<BlockIndex> blockIndex;
    static constexpr size_t ROWS_PER_BLOCK = 250; // 250 rows per independent block
    uint32_t currentRowNumber;
    std::string currentBlock;
    uint32_t blocksWritten;
    
public:
    LZ4IndependentBlockCompressor() : currentRowNumber(0), blocksWritten(0) {
        compressionBuffer.resize(LZ4_COMPRESSBOUND(64 * 1024)); // Worst case for 64KB
    }

    bool open(const std::string& filename) {
        outFile.open(filename, std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "Failed to open output file: " << filename << std::endl;
            return false;
        }

        // Open index file
        std::string indexFilename = filename + ".idx";
        indexFile.open(indexFilename, std::ios::binary);
        if (!indexFile.is_open()) {
            std::cerr << "Failed to open index file: " << indexFilename << std::endl;
            return false;
        }

        currentRowNumber = 0;
        blocksWritten = 0;
        currentBlock.clear();
        blockIndex.clear();
        
        // Write file format identifier
        const char* magic = "LZ4I"; // LZ4 Independent blocks format
        outFile.write(magic, 4);
        
        std::cout << "Started LZ4 independent block compression" << std::endl;
        return true;
    }

    bool addRow(const std::string& rowData) {
        currentBlock += rowData;
        
        // Check if we have enough rows for a block
        if ((currentRowNumber % ROWS_PER_BLOCK) == (ROWS_PER_BLOCK - 1)) {
            return flushCurrentBlock();
        }
        
        currentRowNumber++;
        return true;
    }

    bool flushCurrentBlock() {
        if (currentBlock.empty()) {
            return true;
        }

        uint32_t firstRowInBlock = currentRowNumber - (currentRowNumber % ROWS_PER_BLOCK);
        uint32_t rowsInBlock = (currentRowNumber % ROWS_PER_BLOCK) + 1;
        if (currentRowNumber >= ROWS_PER_BLOCK - 1) {
            rowsInBlock = ROWS_PER_BLOCK;
        }

        // Calculate checksum
        uint32_t checksum = 0;
        for (char c : currentBlock) {
            checksum += static_cast<uint8_t>(c);
        }

        // Compress the block independently (no previous context)
        int compressedSize = LZ4_compress_default(
            currentBlock.c_str(),
            compressionBuffer.data(),
            static_cast<int>(currentBlock.size()),
            static_cast<int>(compressionBuffer.size())
        );

        if (compressedSize <= 0) {
            std::cerr << "Compression failed for block " << blocksWritten << std::endl;
            return false;
        }

        // Record current file position for index
        uint64_t blockOffset = static_cast<uint64_t>(outFile.tellp());

        // Create block header
        IndependentBlockHeader header;
        header.magic = IndependentBlockHeader::MAGIC_NUMBER;
        header.compressedSize = static_cast<uint32_t>(compressedSize);
        header.uncompressedSize = static_cast<uint32_t>(currentBlock.size());
        header.rowCount = rowsInBlock;
        header.firstRowNumber = firstRowInBlock;
        header.checksum = checksum;

        // Write block header
        outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!outFile.good()) {
            std::cerr << "Failed to write block header" << std::endl;
            return false;
        }

        // Write compressed data
        outFile.write(compressionBuffer.data(), compressedSize);
        if (!outFile.good()) {
            std::cerr << "Failed to write compressed block" << std::endl;
            return false;
        }

        // Add to index
        BlockIndex indexEntry;
        indexEntry.fileOffset = blockOffset;
        indexEntry.compressedSize = header.compressedSize;
        indexEntry.uncompressedSize = header.uncompressedSize;
        indexEntry.firstRowNumber = header.firstRowNumber;
        indexEntry.rowCount = header.rowCount;
        indexEntry.checksum = header.checksum;
        blockIndex.push_back(indexEntry);

        blocksWritten++;
        currentBlock.clear();
        currentRowNumber++;

        return true;
    }

    bool close() {
        if (!outFile.is_open()) {
            return true;
        }

        // Flush any remaining data
        if (!currentBlock.empty()) {
            flushCurrentBlock();
        }

        // Write index to index file
        uint32_t indexSize = static_cast<uint32_t>(blockIndex.size());
        indexFile.write(reinterpret_cast<const char*>(&indexSize), sizeof(indexSize));
        for (const auto& entry : blockIndex) {
            indexFile.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        }

        outFile.close();
        indexFile.close();
        
        std::cout << "LZ4 independent block compression finished. Wrote " << blocksWritten 
                  << " blocks with " << blockIndex.size() << " index entries" << std::endl;
        return true;
    }

    uint32_t getBlockCount() const { return blocksWritten; }
    uint32_t getTotalRows() const { return currentRowNumber; }
};

class LZ4IndependentBlockReader {
private:
    std::ifstream inFile;
    std::ifstream indexFile;
    std::vector<char> decompressionBuffer;
    std::vector<BlockIndex> blockIndex;
    bool indexLoaded;

public:
    LZ4IndependentBlockReader() : indexLoaded(false) {
        decompressionBuffer.resize(64 * 1024); // 64KB decompression buffer
    }

    bool open(const std::string& filename) {
        inFile.open(filename, std::ios::binary);
        if (!inFile.is_open()) {
            std::cerr << "Failed to open input file: " << filename << std::endl;
            return false;
        }

        // Open index file
        std::string indexFilename = filename + ".idx";
        indexFile.open(indexFilename, std::ios::binary);
        if (!indexFile.is_open()) {
            std::cerr << "Failed to open index file: " << indexFilename << std::endl;
            return false;
        }

        // Check magic header
        char magic[4];
        inFile.read(magic, 4);
        if (!inFile.good() || std::string(magic, 4) != "LZ4I") {
            std::cerr << "Invalid file format" << std::endl;
            return false;
        }

        // Load index
        return loadIndex();
    }

    bool loadIndex() {
        uint32_t indexSize;
        indexFile.read(reinterpret_cast<char*>(&indexSize), sizeof(indexSize));
        if (!indexFile.good()) {
            std::cerr << "Failed to read index size" << std::endl;
            return false;
        }

        blockIndex.resize(indexSize);
        for (uint32_t i = 0; i < indexSize; ++i) {
            indexFile.read(reinterpret_cast<char*>(&blockIndex[i]), sizeof(BlockIndex));
            if (!indexFile.good()) {
                std::cerr << "Failed to read index entry " << i << std::endl;
                return false;
            }
        }

        indexLoaded = true;
        std::cout << "Loaded index with " << indexSize << " block entries" << std::endl;
        return true;
    }

    // Find which block contains the specified row number
    int findBlockForRow(uint32_t rowNumber) const {
        if (!indexLoaded) return -1;

        for (size_t i = 0; i < blockIndex.size(); ++i) {
            const auto& block = blockIndex[i];
            if (rowNumber >= block.firstRowNumber && 
                rowNumber < (block.firstRowNumber + block.rowCount)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Read a specific block by index
    std::string readBlock(int blockIndex) {
        if (!indexLoaded || blockIndex < 0 || 
            blockIndex >= static_cast<int>(this->blockIndex.size())) {
            return "";
        }

        const auto& block = this->blockIndex[blockIndex];
        
        // Seek to block position
        inFile.seekg(static_cast<std::streampos>(block.fileOffset));
        if (!inFile.good()) {
            std::cerr << "Failed to seek to block " << blockIndex << std::endl;
            return "";
        }

        // Read block header
        IndependentBlockHeader header;
        inFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!inFile.good() || header.magic != IndependentBlockHeader::MAGIC_NUMBER) {
            std::cerr << "Invalid block header for block " << blockIndex << std::endl;
            return "";
        }

        // Verify header matches index
        if (header.compressedSize != block.compressedSize ||
            header.uncompressedSize != block.uncompressedSize ||
            header.firstRowNumber != block.firstRowNumber ||
            header.rowCount != block.rowCount) {
            std::cerr << "Block header mismatch for block " << blockIndex << std::endl;
            return "";
        }

        // Read compressed data
        std::vector<char> compressedData(header.compressedSize);
        inFile.read(compressedData.data(), header.compressedSize);
        if (inFile.gcount() != static_cast<std::streamsize>(header.compressedSize)) {
            std::cerr << "Failed to read compressed data for block " << blockIndex << std::endl;
            return "";
        }

        // Ensure decompression buffer is large enough
        if (header.uncompressedSize > decompressionBuffer.size()) {
            decompressionBuffer.resize(header.uncompressedSize);
        }

        // Decompress block independently
        int decompressedSize = LZ4_decompress_safe(
            compressedData.data(),
            decompressionBuffer.data(),
            static_cast<int>(header.compressedSize),
            static_cast<int>(header.uncompressedSize)
        );

        if (decompressedSize != static_cast<int>(header.uncompressedSize)) {
            std::cerr << "Decompression failed for block " << blockIndex 
                      << ". Expected: " << header.uncompressedSize 
                      << ", Got: " << decompressedSize << std::endl;
            return "";
        }

        // Verify checksum
        uint32_t checksum = 0;
        for (int i = 0; i < decompressedSize; ++i) {
            checksum += static_cast<uint8_t>(decompressionBuffer[i]);
        }

        if (checksum != header.checksum) {
            std::cerr << "Checksum mismatch for block " << blockIndex << std::endl;
            return "";
        }

        return std::string(decompressionBuffer.data(), header.uncompressedSize);
    }

    // Read a specific row by row number
    std::string readRow(uint32_t rowNumber) {
        int blockIndex = findBlockForRow(rowNumber);
        if (blockIndex < 0) {
            std::cerr << "Row " << rowNumber << " not found in any block" << std::endl;
            return "";
        }

        std::string blockData = readBlock(blockIndex);
        if (blockData.empty()) {
            return "";
        }

        // Parse rows from block data
        std::vector<std::string> rows;
        std::istringstream stream(blockData);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                rows.push_back(line + "\n");
            }
        }

        // Calculate relative row position within block
        const auto& block = this->blockIndex[blockIndex];
        uint32_t relativeRow = rowNumber - block.firstRowNumber;
        
        if (relativeRow >= rows.size()) {
            std::cerr << "Row " << rowNumber << " not found in block " << blockIndex << std::endl;
            return "";
        }

        return rows[relativeRow];
    }

    // Read all data (for integrity verification)
    std::string readAllData() {
        if (!indexLoaded) return "";

        std::string result;
        for (size_t i = 0; i < blockIndex.size(); ++i) {
            std::string blockData = readBlock(static_cast<int>(i));
            if (blockData.empty()) {
                std::cerr << "Failed to read block " << i << std::endl;
                return "";
            }
            result += blockData;
        }
        return result;
    }

    void printIndexInfo() const {
        if (!indexLoaded) {
            std::cout << "Index not loaded" << std::endl;
            return;
        }

        std::cout << "\nBlock Index Information:" << std::endl;
        std::cout << "========================" << std::endl;
        for (size_t i = 0; i < blockIndex.size(); ++i) {
            const auto& block = blockIndex[i];
            std::cout << "Block " << i << ": " 
                      << "Rows " << block.firstRowNumber << "-" 
                      << (block.firstRowNumber + block.rowCount - 1)
                      << " (" << block.rowCount << " rows), "
                      << "Compressed: " << block.compressedSize << " bytes, "
                      << "Uncompressed: " << block.uncompressedSize << " bytes"
                      << std::endl;
        }
    }

    void close() {
        if (inFile.is_open()) inFile.close();
        if (indexFile.is_open()) indexFile.close();
    }

    size_t getBlockCount() const { return blockIndex.size(); }
    uint32_t getTotalRows() const {
        if (blockIndex.empty()) return 0;
        const auto& lastBlock = blockIndex.back();
        return lastBlock.firstRowNumber + lastBlock.rowCount;
    }
};

void printFileSize(const std::string& filename, const std::string& description) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        auto size = file.tellg();
        std::cout << description << ": " << size << " bytes (" 
                  << std::fixed << std::setprecision(2) << (size / 1024.0 / 1024.0) << " MB)" << std::endl;
    }
}

int main() {
    std::cout << "LZ4 Independent Blocks with Random Access Example" << std::endl;
    std::cout << "==================================================" << std::endl << std::endl;

    const std::string originalFile = "test_data_independent.csv";
    const std::string compressedFile = "test_data_independent.lz4i";
    const std::string indexFile = compressedFile + ".idx";
    const std::string decompressedFile = "test_data_independent_decompressed.csv";
    
    auto startTime = std::chrono::high_resolution_clock::now();

    // Step 1: Generate and write CSV data while compressing
    std::cout << "Step 1: Generating ~10MB CSV data with independent block compression..." << std::endl;
    
    CSVDataGenerator generator;
    LZ4IndependentBlockCompressor compressor;
    
    if (!compressor.open(compressedFile)) {
        return 1;
    }

    // Also write uncompressed data for comparison
    std::ofstream originalOut(originalFile);
    if (!originalOut.is_open()) {
        std::cerr << "Failed to open original file for writing" << std::endl;
        return 1;
    }

    // Write CSV header
    std::string header = generator.generateCSVHeader();
    originalOut << header;
    compressor.addRow(header);

    size_t totalUncompressedBytes = header.size();
    const size_t targetSize = 10 * 1024 * 1024; // 10MB target

    // Generate data row by row
    while (totalUncompressedBytes < targetSize) {
        std::string row = generator.generateCSVRow();
        originalOut << row;
        compressor.addRow(row);
        totalUncompressedBytes += row.size();

        if (compressor.getTotalRows() % 10000 == 0) {
            std::cout << "  Generated " << compressor.getTotalRows() << " rows, " 
                      << (totalUncompressedBytes / 1024 / 1024) << " MB" << std::endl;
        }
    }

    originalOut.close();
    compressor.close();

    auto compressionTime = std::chrono::high_resolution_clock::now();
    
    std::cout << "Generated " << compressor.getTotalRows() << " rows in " 
              << compressor.getBlockCount() << " independent blocks" << std::endl;
    printFileSize(originalFile, "Original file size");
    printFileSize(compressedFile, "Compressed file size");
    printFileSize(indexFile, "Index file size");

    // Calculate compression ratio
    std::ifstream origFile(originalFile, std::ios::binary | std::ios::ate);
    std::ifstream compFile(compressedFile, std::ios::binary | std::ios::ate);
    std::ifstream idxFile(indexFile, std::ios::binary | std::ios::ate);
    if (origFile.is_open() && compFile.is_open() && idxFile.is_open()) {
        auto origSize = origFile.tellg();
        auto compSize = compFile.tellg();
        auto idxSize = idxFile.tellg();
        auto totalCompSize = compSize + idxSize;
        double ratio = static_cast<double>(origSize) / static_cast<double>(totalCompSize);
        double savings = (1.0 - static_cast<double>(totalCompSize) / static_cast<double>(origSize)) * 100.0;
        std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1" << std::endl;
        std::cout << "Space saved: " << std::setprecision(1) << savings << "% (including index)" << std::endl;
    }

    std::cout << std::endl;

    // Step 2: Demonstrate random access
    std::cout << "Step 2: Testing random access capabilities..." << std::endl;
    
    LZ4IndependentBlockReader reader;
    if (!reader.open(compressedFile)) {
        return 1;
    }

    reader.printIndexInfo();

    // Test random row access
    std::cout << "\nTesting random row access:" << std::endl;
    std::vector<uint32_t> testRows = {0, 1000, 5000, 10000, 25000, 50000, 75000};
    
    auto randomAccessStart = std::chrono::high_resolution_clock::now();
    for (uint32_t rowNum : testRows) {
        if (rowNum < reader.getTotalRows()) {
            auto rowStart = std::chrono::high_resolution_clock::now();
            std::string row = reader.readRow(rowNum);
            auto rowEnd = std::chrono::high_resolution_clock::now();
            auto rowTime = std::chrono::duration_cast<std::chrono::microseconds>(rowEnd - rowStart);
            
            if (!row.empty()) {
                // Show first 50 characters of the row
                std::string preview = row.substr(0, std::min<size_t>(50, row.size()));
                if (row.size() > 50) preview += "...";
                std::cout << "  Row " << rowNum << " (" << rowTime.count() << "μs): " << preview << std::endl;
            } else {
                std::cout << "  Row " << rowNum << ": Failed to read" << std::endl;
            }
        }
    }
    auto randomAccessEnd = std::chrono::high_resolution_clock::now();
    auto randomAccessTime = std::chrono::duration_cast<std::chrono::milliseconds>(randomAccessEnd - randomAccessStart);

    std::cout << "\nRandom access performance: " << randomAccessTime.count() << " ms for " 
              << testRows.size() << " rows" << std::endl;

    // Step 3: Full decompression for integrity check
    std::cout << "\nStep 3: Full decompression for integrity verification..." << std::endl;
    
    auto fullDecompStart = std::chrono::high_resolution_clock::now();
    std::string decompressedData = reader.readAllData();
    auto fullDecompEnd = std::chrono::high_resolution_clock::now();
    
    reader.close();

    std::ofstream decompressedOut(decompressedFile);
    if (!decompressedOut.is_open()) {
        std::cerr << "Failed to open decompressed file for writing" << std::endl;
        return 1;
    }
    decompressedOut << decompressedData;
    decompressedOut.close();

    printFileSize(decompressedFile, "Decompressed file size");

    // Step 4: Verify data integrity
    std::cout << std::endl << "Step 4: Verifying data integrity..." << std::endl;
    
    std::ifstream orig(originalFile, std::ios::binary);
    std::ifstream decomp(decompressedFile, std::ios::binary);
    
    if (orig.is_open() && decomp.is_open()) {
        std::string origContent((std::istreambuf_iterator<char>(orig)), std::istreambuf_iterator<char>());
        std::string decompContent((std::istreambuf_iterator<char>(decomp)), std::istreambuf_iterator<char>());
        
        if (origContent == decompContent) {
            std::cout << "✅ Data integrity verified: Files are identical!" << std::endl;
        } else {
            std::cout << "❌ Data integrity failed: Files differ!" << std::endl;
            std::cout << "  Original size: " << origContent.size() << " bytes" << std::endl;
            std::cout << "  Decompressed size: " << decompContent.size() << " bytes" << std::endl;
        }
    }

    // Performance summary
    auto compressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(compressionTime - startTime);
    auto decompressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fullDecompEnd - fullDecompStart);
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fullDecompEnd - startTime);

    std::cout << std::endl << "Independent Blocks Performance Summary:" << std::endl;
    std::cout << "  Compression time: " << compressionDuration.count() << " ms" << std::endl;
    std::cout << "  Full decompression time: " << decompressionDuration.count() << " ms" << std::endl;
    std::cout << "  Random access time: " << randomAccessTime.count() << " ms (7 rows)" << std::endl;
    std::cout << "  Total time: " << totalDuration.count() << " ms" << std::endl;
    std::cout << "  Blocks created: " << reader.getBlockCount() << std::endl;
    std::cout << "  Fault tolerance: Each block is independent" << std::endl;

    // Cleanup
    std::cout << std::endl << "Cleaning up temporary files..." << std::endl;
    std::remove(originalFile.c_str());
    std::remove(compressedFile.c_str());
    std::remove(indexFile.c_str());
    std::remove(decompressedFile.c_str());

    std::cout << "LZ4 independent blocks example completed successfully!" << std::endl;
    return 0;
}
