#pragma once

#include <vector>
#include <lz4frame.h>

namespace bcsv {

    /**
     * LZ4 compression algorithm wrapper with both compression and decompression contexts
     */
    class Compressor
    {
    public: 
        Compressor();
        ~Compressor();
        
        // Basic compression (frame interface)
        size_t compress(char* dstBuffer, size_t dstCapacity, const char* srcBuffer, size_t srcSize);
        size_t compress(std::vector<char> &dstVector, const std::vector<char> &srcVector, bool resize=false);
        
        // Advanced streaming compression with context
        size_t beginCompression(char* dstBuffer, size_t dstCapacity, const LZ4F_preferences_t* preferences = nullptr);
        size_t compressUpdate(char* dstBuffer, size_t dstCapacity, const char* srcBuffer, size_t srcSize);
        size_t endCompression(char* dstBuffer, size_t dstCapacity);
        
        // Utility functions
        void print_frameinfo(uint8_t* srcBuffer, size_t srcSize);
        static size_t getMaxCompressedSize(size_t srcSize);

    private:
        LZ4F_cctx* cctx;
    };

    class DeCompressor
    {
    public: 
        DeCompressor();
        ~DeCompressor();
        
        // Basic decompression (Frame interface)
        size_t decompress(char* dstBuffer, size_t *dstSize, char *srcBuffer, size_t *srcSize);
        size_t decompress(std::vector<char> &dstVector, const std::vector<char> &srcVector, bool resize = false);
        
        // Advanced streaming decompression with context
        size_t beginDecompression(char* dstBuffer, size_t dstCapacity, const LZ4F_preferences_t* preferences = nullptr);
        size_t decompressUpdate(char* dstBuffer, size_t dstCapacity, const char* srcBuffer, size_t srcSize);
        size_t endDecompression(char* dstBuffer, size_t dstCapacity);

        // Reset contexts for reuse
        void resetDecompression();

        // Utility functions
        void print_frameinfo(uint8_t* srcBuffer, size_t srcSize);
        static size_t getMaxCompressedSize(size_t srcSize);

    private:
        LZ4F_dctx* dctx;
    };

} // namespace bcsv
