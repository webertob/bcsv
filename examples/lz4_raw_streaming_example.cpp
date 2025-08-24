#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
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

// Simple block header for our custom format
struct BlockHeader {
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint32_t checksum; // Simple checksum for integrity
};

class LZ4RawStreamingCompressor {
private:
    LZ4_stream_t* lz4Stream;
    std::ofstream outFile;
    std::vector<char> compressionBuffer;
    std::vector<char> inputRingBuffer;
    static constexpr size_t RING_BUFFER_SIZE = 64 * 1024; // 64KB ring buffer
    static constexpr size_t BLOCK_SIZE = 16 * 1024; // 16KB blocks
    size_t ringBufferPos;
    
public:
    LZ4RawStreamingCompressor() : lz4Stream(nullptr), ringBufferPos(0) {
        lz4Stream = LZ4_createStream();
        compressionBuffer.resize(LZ4_COMPRESSBOUND(BLOCK_SIZE));
        inputRingBuffer.resize(RING_BUFFER_SIZE);
    }

    ~LZ4RawStreamingCompressor() {
        close();
        if (lz4Stream) {
            LZ4_freeStream(lz4Stream);
        }
    }

    bool open(const std::string& filename) {
        outFile.open(filename, std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "Failed to open output file: " << filename << std::endl;
            return false;
        }

        LZ4_resetStream(lz4Stream);
        ringBufferPos = 0;
        
        // Write magic header to identify our format
        const char* magic = "LZ4S"; // LZ4 Streaming format identifier
        outFile.write(magic, 4);
        
        std::cout << "Started LZ4 raw streaming compression" << std::endl;
        return true;
    }

    bool compressAndWrite(const std::string& data) {
        if (!outFile.is_open()) {
            return false;
        }

        size_t dataPos = 0;
        
        while (dataPos < data.size()) {
            // Calculate how much data to process in this block
            size_t blockDataSize = std::min(BLOCK_SIZE, data.size() - dataPos);
            
            // Get source data pointer
            const char* src = data.c_str() + dataPos;
            
            // Copy data to ring buffer position (simplified - no wrap around for now)
            char* dst = inputRingBuffer.data() + ringBufferPos;
            
            // For simplicity, ensure we don't wrap around the ring buffer in this example
            if (ringBufferPos + blockDataSize > RING_BUFFER_SIZE) {
                ringBufferPos = 0; // Reset to beginning
                dst = inputRingBuffer.data();
            }
            
            std::memcpy(dst, src, blockDataSize);
            
            // Compress the block using the ring buffer as reference
            int compressedSize = LZ4_compress_fast_continue(
                lz4Stream,
                dst,
                compressionBuffer.data(),
                static_cast<int>(blockDataSize),
                static_cast<int>(compressionBuffer.size()),
                1 // acceleration
            );
            
            if (compressedSize <= 0) {
                std::cerr << "Compression failed for block" << std::endl;
                return false;
            }
            
            // Calculate simple checksum on source data
            uint32_t checksum = 0;
            for (size_t i = 0; i < blockDataSize; ++i) {
                checksum += static_cast<uint8_t>(src[i]);
            }
            
            // Write block header
            BlockHeader header;
            header.compressedSize = static_cast<uint32_t>(compressedSize);
            header.uncompressedSize = static_cast<uint32_t>(blockDataSize);
            header.checksum = checksum;
            
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
            
            // Update positions
            dataPos += blockDataSize;
            ringBufferPos += blockDataSize;
        }
        
        return true;
    }

    bool close() {
        if (!outFile.is_open()) {
            return true;
        }

        // Write end marker (header with all zeros)
        BlockHeader endMarker = {0, 0, 0};
        outFile.write(reinterpret_cast<const char*>(&endMarker), sizeof(endMarker));
        
        outFile.close();
        std::cout << "LZ4 raw streaming compression finished" << std::endl;
        return true;
    }
};

class LZ4RawStreamingDecompressor {
private:
    LZ4_streamDecode_t* lz4StreamDecode;
    std::ifstream inFile;
    std::vector<char> decompressionBuffer;
    std::vector<char> outputRingBuffer;
    static constexpr size_t RING_BUFFER_SIZE = 64 * 1024; // 64KB ring buffer
    size_t ringBufferPos;

public:
    LZ4RawStreamingDecompressor() : lz4StreamDecode(nullptr), ringBufferPos(0) {
        lz4StreamDecode = LZ4_createStreamDecode();
        decompressionBuffer.resize(64 * 1024); // 64KB decompression buffer
        outputRingBuffer.resize(RING_BUFFER_SIZE);
    }

    ~LZ4RawStreamingDecompressor() {
        close();
        if (lz4StreamDecode) {
            LZ4_freeStreamDecode(lz4StreamDecode);
        }
    }

    bool open(const std::string& filename) {
        inFile.open(filename, std::ios::binary);
        if (!inFile.is_open()) {
            std::cerr << "Failed to open input file: " << filename << std::endl;
            return false;
        }

        // Check magic header
        char magic[4];
        inFile.read(magic, 4);
        if (!inFile.good() || std::string(magic, 4) != "LZ4S") {
            std::cerr << "Invalid file format" << std::endl;
            return false;
        }

        LZ4_setStreamDecode(lz4StreamDecode, nullptr, 0);
        ringBufferPos = 0;

        std::cout << "Started LZ4 raw streaming decompression" << std::endl;
        return true;
    }

    std::string readAndDecompress() {
        std::string result;
        
        while (!inFile.eof()) {
            // Read block header
            BlockHeader header;
            inFile.read(reinterpret_cast<char*>(&header), sizeof(header));
            
            if (inFile.gcount() != sizeof(header)) {
                break; // End of file
            }
            
            // Check for end marker
            if (header.compressedSize == 0 && header.uncompressedSize == 0) {
                std::cout << "Reached end marker" << std::endl;
                break;
            }
            
            // Read compressed data
            if (header.compressedSize > decompressionBuffer.size()) {
                decompressionBuffer.resize(header.compressedSize);
            }
            
            inFile.read(decompressionBuffer.data(), header.compressedSize);
            if (inFile.gcount() != static_cast<std::streamsize>(header.compressedSize)) {
                std::cerr << "Failed to read compressed block" << std::endl;
                break;
            }
            
            // Prepare decompression buffer position (simplified - no wrap around)
            char* dst = outputRingBuffer.data() + ringBufferPos;
            
            // Ensure we don't wrap around for simplicity
            if (ringBufferPos + header.uncompressedSize > RING_BUFFER_SIZE) {
                ringBufferPos = 0;
                dst = outputRingBuffer.data();
            }
            
            // Decompress block
            int decompressedSize = LZ4_decompress_safe_continue(
                lz4StreamDecode,
                decompressionBuffer.data(),
                dst,
                static_cast<int>(header.compressedSize),
                static_cast<int>(header.uncompressedSize)
            );
            
            if (decompressedSize != static_cast<int>(header.uncompressedSize)) {
                std::cerr << "Decompression failed for block. Expected: " << header.uncompressedSize 
                          << ", Got: " << decompressedSize << std::endl;
                break;
            }
            
            // Verify checksum
            uint32_t checksum = 0;
            for (int i = 0; i < decompressedSize; ++i) {
                checksum += static_cast<uint8_t>(dst[i]);
            }
            
            if (checksum != header.checksum) {
                std::cerr << "Checksum mismatch for block. Expected: " << header.checksum 
                          << ", Got: " << checksum << std::endl;
                break;
            }
            
            // Append decompressed data to result
            result.append(dst, header.uncompressedSize);
            
            // Update ring buffer position
            ringBufferPos += header.uncompressedSize;
        }

        std::cout << "LZ4 raw streaming decompression complete" << std::endl;
        return result;
    }

    void close() {
        if (inFile.is_open()) {
            inFile.close();
        }
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
    std::cout << "LZ4 Raw Streaming Compression Example" << std::endl;
    std::cout << "=====================================" << std::endl << std::endl;

    const std::string originalFile = "test_data_raw.csv";
    const std::string compressedFile = "test_data_raw.lz4s";
    const std::string decompressedFile = "test_data_raw_decompressed.csv";
    
    auto startTime = std::chrono::high_resolution_clock::now();

    // Step 1: Generate and write CSV data while compressing
    std::cout << "Step 1: Generating ~10MB CSV data and compressing with raw LZ4 streaming..." << std::endl;
    
    CSVDataGenerator generator;
    LZ4RawStreamingCompressor compressor;
    
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
    compressor.compressAndWrite(header);

    size_t totalRows = 0;
    size_t totalUncompressedBytes = header.size();
    const size_t targetSize = 10 * 1024 * 1024; // 10MB target

    // Generate data in chunks (smaller chunks for better streaming demonstration)
    const size_t chunkSize = 50; // Process 50 rows at a time
    while (totalUncompressedBytes < targetSize) {
        std::string chunk;
        
        for (size_t i = 0; i < chunkSize && totalUncompressedBytes < targetSize; ++i) {
            std::string row = generator.generateCSVRow();
            chunk += row;
            totalUncompressedBytes += row.size();
            totalRows++;
        }

        originalOut << chunk;
        compressor.compressAndWrite(chunk);

        if (totalRows % 10000 == 0) {
            std::cout << "  Generated " << totalRows << " rows, " 
                      << (totalUncompressedBytes / 1024 / 1024) << " MB" << std::endl;
        }
    }

    originalOut.close();
    compressor.close();

    auto compressionTime = std::chrono::high_resolution_clock::now();
    
    std::cout << "Generated " << totalRows << " rows" << std::endl;
    printFileSize(originalFile, "Original file size");
    printFileSize(compressedFile, "Compressed file size");

    // Calculate compression ratio
    std::ifstream origFile(originalFile, std::ios::binary | std::ios::ate);
    std::ifstream compFile(compressedFile, std::ios::binary | std::ios::ate);
    if (origFile.is_open() && compFile.is_open()) {
        auto origSize = origFile.tellg();
        auto compSize = compFile.tellg();
        double ratio = static_cast<double>(origSize) / static_cast<double>(compSize);
        double savings = (1.0 - static_cast<double>(compSize) / static_cast<double>(origSize)) * 100.0;
        std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1" << std::endl;
        std::cout << "Space saved: " << std::setprecision(1) << savings << "%" << std::endl;
    }

    std::cout << std::endl;

    // Step 2: Decompress the file
    std::cout << "Step 2: Decompressing file..." << std::endl;
    
    LZ4RawStreamingDecompressor decompressor;
    if (!decompressor.open(compressedFile)) {
        return 1;
    }

    std::ofstream decompressedOut(decompressedFile);
    if (!decompressedOut.is_open()) {
        std::cerr << "Failed to open decompressed file for writing" << std::endl;
        return 1;
    }

    std::string decompressedData = decompressor.readAndDecompress();
    decompressedOut << decompressedData;
    decompressedOut.close();
    decompressor.close();

    auto decompressionTime = std::chrono::high_resolution_clock::now();

    printFileSize(decompressedFile, "Decompressed file size");

    // Step 3: Verify data integrity
    std::cout << std::endl << "Step 3: Verifying data integrity..." << std::endl;
    
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
    auto decompressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decompressionTime - compressionTime);
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decompressionTime - startTime);

    std::cout << std::endl << "Raw Streaming Performance Summary:" << std::endl;
    std::cout << "  Compression time: " << compressionDuration.count() << " ms" << std::endl;
    std::cout << "  Decompression time: " << decompressionDuration.count() << " ms" << std::endl;
    std::cout << "  Total time: " << totalDuration.count() << " ms" << std::endl;

    // Cleanup
    std::cout << std::endl << "Cleaning up temporary files..." << std::endl;
    std::remove(originalFile.c_str());
    std::remove(compressedFile.c_str());
    std::remove(decompressedFile.c_str());

    std::cout << "LZ4 raw streaming example completed successfully!" << std::endl;
    return 0;
}
