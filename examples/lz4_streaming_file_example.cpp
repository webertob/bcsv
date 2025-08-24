#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <lz4frame.h>

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

class LZ4StreamingCompressor {
private:
    LZ4F_cctx* cctx;
    std::ofstream outFile;
    std::vector<char> buffer;
    static constexpr size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer

public:
    LZ4StreamingCompressor() : cctx(nullptr) {
        LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
        buffer.resize(BUFFER_SIZE);
    }

    ~LZ4StreamingCompressor() {
        close();
        if (cctx) {
            LZ4F_freeCompressionContext(cctx);
        }
    }

    bool open(const std::string& filename) {
        outFile.open(filename, std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "Failed to open output file: " << filename << std::endl;
            return false;
        }

        // Configure compression preferences
        LZ4F_preferences_t preferences = {};
        preferences.frameInfo.blockSizeID = LZ4F_max64KB;
        preferences.frameInfo.blockMode = LZ4F_blockLinked;
        preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
        preferences.frameInfo.blockChecksumFlag = LZ4F_noBlockChecksum;
        preferences.compressionLevel = 1; // Fast compression

        // Write frame header
        size_t headerSize = LZ4F_compressBegin(cctx, buffer.data(), buffer.size(), &preferences);
        if (LZ4F_isError(headerSize)) {
            std::cerr << "Failed to begin compression: " << LZ4F_getErrorName(headerSize) << std::endl;
            return false;
        }

        outFile.write(buffer.data(), headerSize);
        if (!outFile.good()) {
            std::cerr << "Failed to write frame header" << std::endl;
            return false;
        }

        std::cout << "Started compression with frame header (" << headerSize << " bytes)" << std::endl;
        return true;
    }

    bool compressAndWrite(const std::string& data) {
        if (!outFile.is_open()) {
            return false;
        }

        // Calculate worst-case output size for this chunk
        size_t maxCompressedSize = LZ4F_compressBound(data.size(), nullptr);
        if (maxCompressedSize > buffer.size()) {
            buffer.resize(maxCompressedSize);
        }

        size_t compressedSize = LZ4F_compressUpdate(cctx, buffer.data(), buffer.size(), 
                                                   data.c_str(), data.size(), nullptr);
        if (LZ4F_isError(compressedSize)) {
            std::cerr << "Compression failed: " << LZ4F_getErrorName(compressedSize) << std::endl;
            return false;
        }

        if (compressedSize > 0) {
            outFile.write(buffer.data(), compressedSize);
            if (!outFile.good()) {
                std::cerr << "Failed to write compressed data" << std::endl;
                return false;
            }
        }

        return true;
    }

    bool close() {
        if (!outFile.is_open()) {
            return true;
        }

        // Write frame footer
        size_t footerSize = LZ4F_compressEnd(cctx, buffer.data(), buffer.size(), nullptr);
        if (LZ4F_isError(footerSize)) {
            std::cerr << "Failed to end compression: " << LZ4F_getErrorName(footerSize) << std::endl;
            return false;
        }

        if (footerSize > 0) {
            outFile.write(buffer.data(), footerSize);
            if (!outFile.good()) {
                std::cerr << "Failed to write frame footer" << std::endl;
                return false;
            }
        }

        outFile.close();
        std::cout << "Compression finished with frame footer (" << footerSize << " bytes)" << std::endl;
        return true;
    }
};

class LZ4StreamingDecompressor {
private:
    LZ4F_dctx* dctx;
    std::ifstream inFile;
    std::vector<char> inputBuffer;
    std::vector<char> outputBuffer;
    static constexpr size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer
    size_t inputPos;
    size_t inputSize;
    bool frameComplete;

public:
    LZ4StreamingDecompressor() : dctx(nullptr), inputPos(0), inputSize(0), frameComplete(false) {
        LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
        inputBuffer.resize(BUFFER_SIZE);
        outputBuffer.resize(BUFFER_SIZE);
    }

    ~LZ4StreamingDecompressor() {
        close();
        if (dctx) {
            LZ4F_freeDecompressionContext(dctx);
        }
    }

    bool open(const std::string& filename) {
        inFile.open(filename, std::ios::binary);
        if (!inFile.is_open()) {
            std::cerr << "Failed to open input file: " << filename << std::endl;
            return false;
        }

        LZ4F_resetDecompressionContext(dctx);
        frameComplete = false;
        inputPos = 0;
        inputSize = 0;

        std::cout << "Started decompression" << std::endl;
        return true;
    }

    std::string readAndDecompress() {
        std::string result;
        size_t totalBytesProcessed = 0;
        
        while (!frameComplete) {
            // Read more data if buffer is empty or low
            if (inputPos >= inputSize || (inputSize - inputPos) < 1024) {
                // Move remaining data to the beginning
                if (inputPos < inputSize) {
                    std::memmove(inputBuffer.data(), inputBuffer.data() + inputPos, inputSize - inputPos);
                    inputSize -= inputPos;
                    inputPos = 0;
                } else {
                    inputSize = 0;
                    inputPos = 0;
                }
                
                // Read more data if file is not at end
                if (!inFile.eof()) {
                    inFile.read(inputBuffer.data() + inputSize, inputBuffer.size() - inputSize);
                    size_t bytesRead = inFile.gcount();
                    inputSize += bytesRead;
                    totalBytesProcessed += bytesRead;
                }
                
                // If no data left and we haven't completed the frame, there's an issue
                if (inputSize == 0 && !frameComplete) {
                    std::cerr << "Warning: No more input data but frame not complete!" << std::endl;
                    break;
                }
            }

            // Decompress available data
            size_t srcSize = inputSize - inputPos;
            size_t dstSize = outputBuffer.size();
            size_t originalSrcSize = srcSize;
            
            size_t decompResult = LZ4F_decompress(dctx, outputBuffer.data(), &dstSize,
                                                 inputBuffer.data() + inputPos, &srcSize, nullptr);
            
            if (LZ4F_isError(decompResult)) {
                std::cerr << "Decompression failed: " << LZ4F_getErrorName(decompResult) << std::endl;
                break;
            }

            inputPos += srcSize;

            if (dstSize > 0) {
                result.append(outputBuffer.data(), dstSize);
            }

            // Check if frame is complete
            if (decompResult == 0) {
                frameComplete = true;
                std::cout << "Frame decompression complete. Processed " << totalBytesProcessed << " total input bytes" << std::endl;
                break;
            }
            
            // If we didn't consume any input and didn't produce any output, we might be stuck
            if (srcSize == 0 && dstSize == 0 && !inFile.eof()) {
                std::cerr << "Warning: No progress in decompression!" << std::endl;
                break;
            }
        }

        return result;
    }

    void close() {
        if (inFile.is_open()) {
            inFile.close();
        }
    }

    bool isComplete() const {
        return frameComplete;
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
    std::cout << "LZ4 Streaming File Compression Example" << std::endl;
    std::cout << "======================================" << std::endl << std::endl;

    const std::string originalFile = "test_data.csv";
    const std::string compressedFile = "test_data.lz4";
    const std::string decompressedFile = "test_data_decompressed.csv";
    
    auto startTime = std::chrono::high_resolution_clock::now();

    // Step 1: Generate and write CSV data while compressing
    std::cout << "Step 1: Generating ~10MB CSV data and compressing to file..." << std::endl;
    
    CSVDataGenerator generator;
    LZ4StreamingCompressor compressor;
    
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

    // Generate data in chunks
    const size_t chunkSize = 100; // Process 100 rows at a time for better streaming
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
    
    LZ4StreamingDecompressor decompressor;
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

    std::cout << std::endl << "Performance Summary:" << std::endl;
    std::cout << "  Compression time: " << compressionDuration.count() << " ms" << std::endl;
    std::cout << "  Decompression time: " << decompressionDuration.count() << " ms" << std::endl;
    std::cout << "  Total time: " << totalDuration.count() << " ms" << std::endl;

    // Cleanup
    std::cout << std::endl << "Cleaning up temporary files..." << std::endl;
    std::remove(originalFile.c_str());
    std::remove(compressedFile.c_str());
    std::remove(decompressedFile.c_str());

    std::cout << "LZ4 streaming example completed successfully!" << std::endl;
    return 0;
}
