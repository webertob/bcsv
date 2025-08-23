#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include "bcsv/compression.hpp"
#include <lz4frame.h>

void printHex(const std::vector<char>& data, const std::string& label) {
    std::cout << label << " (" << data.size() << " bytes): ";
    for (size_t i = 0; i < std::min(data.size(), size_t(16)); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (static_cast<unsigned char>(data[i])) << " ";
    }
    if (data.size() > 16) std::cout << "...";
    std::cout << std::dec << std::endl;
}

int main() {
    std::cout << "LZ4 Streaming Compression Demo\n";
    std::cout << "==============================\n\n";

    bcsv::Compressor compressor;

    // Test data chunks
    std::vector<std::string> chunks = {
        "This is the first chunk of data to compress. ",
        "This is the second chunk with more information. ",
        "And this is the final chunk to complete our test."
    };

    std::cout << "Input chunks:\n";
    for (size_t i = 0; i < chunks.size(); ++i) {
        std::cout << "  Chunk " << (i + 1) << ": \"" << chunks[i] << "\"\n";
    }
    std::cout << "\n";

    // Prepare output buffer
    std::vector<char> compressed;
    compressed.resize(4096); // Large buffer for demo

    size_t totalCompressed = 0;

    try {
        // Begin compression (writes frame header)
        std::cout << "Beginning streaming compression...\n";
        size_t headerSize = compressor.beginCompression(
            compressed.data(), compressed.size(), nullptr);
        
        if (LZ4F_isError(headerSize)) {
            std::cerr << "Error beginning compression: " << LZ4F_getErrorName(headerSize) << std::endl;
            return 1;
        }
        
        totalCompressed += headerSize;
        std::cout << "  Frame header written: " << headerSize << " bytes\n";

        // Compress each chunk
        for (size_t i = 0; i < chunks.size(); ++i) {
            std::cout << "  Compressing chunk " << (i + 1) << "...\n";
            
            size_t chunkCompressed = compressor.compressUpdate(
                compressed.data() + totalCompressed,
                compressed.size() - totalCompressed,
                chunks[i].c_str(),
                chunks[i].size());
            
            if (LZ4F_isError(chunkCompressed)) {
                std::cerr << "Error compressing chunk: " << LZ4F_getErrorName(chunkCompressed) << std::endl;
                return 1;
            }
            
            totalCompressed += chunkCompressed;
            std::cout << "    Chunk " << (i + 1) << " compressed: " << chunkCompressed << " bytes\n";
        }

        // End compression (writes frame footer)
        std::cout << "  Ending compression...\n";
        size_t footerSize = compressor.endCompression(
            compressed.data() + totalCompressed,
            compressed.size() - totalCompressed);
        
        if (LZ4F_isError(footerSize)) {
            std::cerr << "Error ending compression: " << LZ4F_getErrorName(footerSize) << std::endl;
            return 1;
        }
        
        totalCompressed += footerSize;
        std::cout << "  Frame footer written: " << footerSize << " bytes\n";

        compressed.resize(totalCompressed);

        std::cout << "\nCompression Results:\n";
        
        // Calculate total input size
        size_t totalInput = 0;
        for (const auto& chunk : chunks) {
            totalInput += chunk.size();
        }
        
        std::cout << "  Total input size: " << totalInput << " bytes\n";
        std::cout << "  Total compressed size: " << totalCompressed << " bytes\n";
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) 
                  << (double(totalInput) / double(totalCompressed)) << ":1\n";
        std::cout << "  Space saved: " << std::setprecision(1) 
                  << (100.0 * (1.0 - double(totalCompressed) / double(totalInput))) << "%\n";

        printHex(compressed, "Compressed data");

        // Test decompression
        std::cout << "\nTesting decompression...\n";
        std::vector<char> decompressed(totalInput + 100); // Extra space for safety
        
        bcsv::DeCompressor decompressor;
        size_t decompressedSize = decompressed.size();
        size_t compressedSizeCopy = compressed.size();
        
        size_t result = decompressor.decompress(
            decompressed.data(), &decompressedSize,
            compressed.data(), &compressedSizeCopy);
        
        if (LZ4F_isError(result)) {
            std::cerr << "Error decompressing: " << LZ4F_getErrorName(result) << std::endl;
            return 1;
        }

        decompressed.resize(decompressedSize);
        std::string decompressedStr(decompressed.begin(), decompressed.end());
        
        // Verify the result
        std::string originalStr;
        for (const auto& chunk : chunks) {
            originalStr += chunk;
        }
        
        if (decompressedStr == originalStr) {
            std::cout << "  ✓ Decompression successful!\n";
            std::cout << "  Decompressed: \"" << decompressedStr << "\"\n";
        } else {
            std::cout << "  ✗ Decompression failed - data mismatch!\n";
            std::cout << "  Expected: \"" << originalStr << "\"\n";
            std::cout << "  Got:      \"" << decompressedStr << "\"\n";
            return 1;
        }

        std::cout << "\nStreaming compression demo completed successfully!\n";

    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
