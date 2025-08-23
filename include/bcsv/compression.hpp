#pragma once

#include "compression.h"

namespace bcsv {

// Compressor class implementation
inline Compressor::Compressor() { 
    LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
}

inline Compressor::~Compressor() { 
    LZ4F_freeCompressionContext(cctx);
}

// Compressor methods
inline size_t Compressor::compress(char* dstBuffer, size_t dstCapacity, const char* srcBuffer, size_t srcSize)
{
    /* Simplified compression of data using one-shot frame compression */
    auto result = LZ4F_compressBegin(cctx, dstBuffer, dstCapacity, NULL);
    if(LZ4F_isError(result)) 
        return result;
    else
        return LZ4F_compressEnd(cctx, dstBuffer, dstCapacity, nullptr);
}

inline size_t Compressor::compress(std::vector<char> &dstVector, const std::vector<char> &srcVector, bool resize)
{
    /* bytevector-version of compression. allow option of resizing dstVector (before & after) */
    if (resize && dstVector.size() < getMaxCompressedSize(srcVector.size())) {
        dstVector.resize(getMaxCompressedSize(srcVector.size()));
    }
    auto result = compress(dstVector.data(), dstVector.size(), srcVector.data(), srcVector.size());
    if(LZ4F_isError(result))
        return result;
    else if (resize) 
        dstVector.resize(result);   
    return result;
}

// Advanced streaming compression methods
inline size_t Compressor::beginCompression(char* dstBuffer, size_t dstCapacity, const LZ4F_preferences_t* preferences)
{
    /* Begin compression and write frame header */
    // Note: Some LZ4 versions don't have LZ4F_resetCompressionContext
    // The context is automatically reset on LZ4F_compressBegin
    return LZ4F_compressBegin(cctx, dstBuffer, dstCapacity, preferences);
}

inline size_t Compressor::compressUpdate(char* dstBuffer, size_t dstCapacity, const char* srcBuffer, size_t srcSize)
{
    /* Compress a chunk of data */
    return LZ4F_compressUpdate(cctx, dstBuffer, dstCapacity, srcBuffer, srcSize, nullptr);
}

inline size_t Compressor::endCompression(char* dstBuffer, size_t dstCapacity)
{
    /* Finish compression and write frame footer */
    return LZ4F_compressEnd(cctx, dstBuffer, dstCapacity, nullptr);
}

inline size_t Compressor::getMaxCompressedSize(size_t srcSize)
{
    /* Calculate maximum possible compressed size including frame overhead */
    return LZ4F_compressFrameBound(srcSize, nullptr);
}




// DeCompressor class implementation
inline DeCompressor::DeCompressor() { 
    LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION); 
}

inline DeCompressor::~DeCompressor() { 
    LZ4F_freeDecompressionContext(dctx); 
}

// DeCompressor methods
inline size_t DeCompressor::decompress(char* dstBuffer, size_t *dstSize, char *srcBuffer, size_t *srcSize)
{
    /* Simplified decompression of data. return number of bytes decompressed, unless there is 
        an error (return error value) */
    resetDecompression();
    size_t res = LZ4F_decompress(dctx, dstBuffer, dstSize, srcBuffer, srcSize, nullptr);
    if (res == 0) return *dstSize;
    else return res;
}

inline size_t DeCompressor::decompress(std::vector<char> &dstVector, const std::vector<char> &srcVector, bool resize)
{
    /* bytevector-version of decompression. allow option of resizing dstVector */
    size_t szsrc = srcVector.size();
    if (resize && dstVector.size() < (srcVector.size() * 2)) {
        dstVector.resize(srcVector.size() * 2);
    }
    size_t szdst = dstVector.size();
    size_t res = decompress(dstVector.data(), &szdst, const_cast<char*>(srcVector.data()), &szsrc);
    if (resize) dstVector.resize(res);
    return res;
}

// Advanced streaming decompression methods
inline size_t DeCompressor::beginDecompression(char* dstBuffer, size_t dstCapacity, const LZ4F_preferences_t* preferences)
{
    /* Begin decompression - typically just resets context */
    resetDecompression();
    return 0; // No header output for decompression begin
}

inline size_t DeCompressor::decompressUpdate(char* dstBuffer, size_t dstCapacity, const char* srcBuffer, size_t srcSize)
{
    /* Decompress a chunk of data */
    size_t dstSize = dstCapacity;
    size_t srcSizeLocal = srcSize;
    size_t res = LZ4F_decompress(dctx, dstBuffer, &dstSize, srcBuffer, &srcSizeLocal, nullptr);
    if (LZ4F_isError(res)) return res;
    return dstSize;
}

inline size_t DeCompressor::endDecompression(char* dstBuffer, size_t dstCapacity)
{
    /* Finish decompression - no footer processing needed */
    return 0;
}

inline void DeCompressor::resetDecompression()
{
    LZ4F_resetDecompressionContext(dctx);
}

inline size_t DeCompressor::getMaxCompressedSize(size_t srcSize)
{
    /* Calculate maximum possible compressed size including frame overhead */
    return LZ4F_compressFrameBound(srcSize, nullptr);
}

inline void DeCompressor::print_frameinfo(uint8_t* srcBuffer, size_t srcSize)
{
    size_t header_sz = LZ4F_headerSize(srcBuffer, srcSize);
    LZ4F_frameInfo_t info;
    resetDecompression();
    LZ4F_getFrameInfo(dctx, &info, srcBuffer, &srcSize);
    
    printf("header size:       %zu\n", header_sz);
    printf("blockChecksumFlag: %d\n", info.blockChecksumFlag);
    printf("blockMode:         %u\n", info.blockMode);
    printf("blockSizeID:       %u\n", info.blockSizeID);
    printf("contentChksumFlag: %u\n", info.contentChecksumFlag);
    printf("contentSize:       %llu\n", info.contentSize);
    printf("dictID:            %u\n", info.dictID);
    printf("frameType:         %u\n", info.frameType);
}

} // namespace bcsv
