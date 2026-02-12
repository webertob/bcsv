/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <vector>
#include "bcsv/definitions.h"
#include "xxHash-0.8.3/xxhash.h"

namespace bcsv {

    /**
     * @brief Packet header for BCSV v1.3.0 streaming compression format
     * 
     * This structure represents the 16-byte header at the start of each data packet
     * in v1.3.0 files using streaming LZ4 compression. The header provides:
     * - Packet identification via magic number
     * - Row indexing for random access
     * - Compact header checksum for header-only validation
     * 
     * Memory Layout (24 bytes):
     * ```
     * Offset | Size | Field                  | Description
     * -------|------|------------------------|----------------------------------------
     *   0    |  4   | magic[4]               | "PCKT" (0x50 0x43 0x4B 0x54)
     *   4    |  8   | firstRowIndex          | Absolute row index (0-based, file-wide)
     *  12    |  4   | headerChecksum         | xxHash32 of bytes 0-12 (magic + firstRowIndex)
     * 
     * @note All multi-byte fields use little-endian byte ordering
     * @note Row count for packet = nextPacket.firstRowIndex - currentPacket.firstRowIndex
     * @note For last packet, row count from FileFooter or scanning payload
     */
    #pragma pack(push, 1)
    struct PacketHeader {
        
        uint32_t magic;           ///< Magic number: "PCKT" (0x50 0x43 0x4B 0x54)
        uint64_t first_row_index; ///< Absolute row index (0-based, file-wide)
        uint32_t checksum;        ///< xxHash32 of bytes 0-19 (magic + firstRowIndex + prevPayloadChecksum)
        
        /**
         * @brief Default constructor - initializes to invalid state
         */
        PacketHeader() 
            : magic(PCKT_MAGIC)
            , first_row_index(0)
            , checksum(0)
        {}
        
        /**
         * @brief Construct packet header with specified values
         * @param firstRow Absolute row index (0-based, file-wide)
         */
        PacketHeader(uint64_t firstRow)
            : magic(PCKT_MAGIC)
            , first_row_index(firstRow)
            , checksum(0)
        {
            updateChecksum();
        }
        
        /**
         * @brief Validate magic number
         * @return true if magic number is "PCKT"
         */
        bool isValidMagic() const {
            return magic == PCKT_MAGIC;
        }
        
        /**
         * @brief Calculate and update the header checksum
         * 
         * Calculates xxHash32 of the first bytes (magic + firstRowIndex)
         * and stores it in the headerChecksum field.
         */
        void updateChecksum() {
            // Calculate xxHash32 of bytes 0-19 (all fields except headerChecksum)
            checksum = XXH32(this, sizeof(PacketHeader)-sizeof(checksum), 0);
        }
        
        /**
         * @brief Validate the header checksum
         * @return true if calculated checksum matches stored checksum
         */
        bool validateChecksum() const {
            uint32_t calculated = XXH32(this, sizeof(PacketHeader)-sizeof(checksum), 0);
            return calculated == checksum;
        }
        
        /**
         * @brief Validate header integrity (magic + checksum)
         * @return true if both magic number and checksum are valid
         */
        bool validate() const {
            return isValidMagic() && validateChecksum();
        }
        
        /**
         * @brief Read packet header from binary stream
         * @param stream Input stream to read from
         * @return true if read successful and header is valid
         */
        bool read(std::istream& stream, bool silent = false) {
            stream.read(reinterpret_cast<char*>(this), sizeof(PacketHeader));
            if (stream.gcount() != sizeof(PacketHeader)) {
                return false;
            }
            
            if (!isValidMagic()) {
                if (!silent) {
                    std::cerr << "Error: Invalid packet header magic" << std::endl;
                }
                return false;
            }
            
            if (!validateChecksum()) {
                if (!silent) {
                    std::cerr << "Error: Packet header checksum mismatch" << std::endl;
                }
                return false;
            }
            
            return true;
        }
        
        /**
         * @brief Searches, reads and validates the next packet header from binary stream
         * @param stream Input stream to read from
         * @param position start postion to search from, updated to position of packet header magic if found, else EoL
         * @return true if read successful and header is valid
         */
        bool readNext(std::istream& stream, std::streampos& position) {
            stream.clear();
            stream.seekg(position);
            if (!stream.good()) {
                return false;
            }

            constexpr size_t CHUNK_SIZE = 8192;  // 8KB chunks
            constexpr size_t HEADER_SIZE = sizeof(PacketHeader);
            std::vector<char> buffer(CHUNK_SIZE);
            std::streamsize validBytes = 0;  // Number of valid bytes in buffer
            std::streampos bufferStartPos = position;  // Track file position of buffer start
            
            while (stream.good()) {
                // Read new data into buffer (after any existing data from sliding)
                stream.read(buffer.data() + validBytes, CHUNK_SIZE - validBytes);
                std::streamsize newBytes = stream.gcount();
                validBytes += newBytes;
                
                if (validBytes < static_cast<std::streamsize>(HEADER_SIZE)) {
                    break;  // Not enough data for a complete header
                }
                
                // Search for "PCKT" in the buffer using string_view
                std::string_view view(buffer.data(), static_cast<size_t>(validBytes));
                size_t pos = view.find("PCKT");
                
                while (pos != std::string_view::npos) {
                    // Check if we have enough bytes for complete header
                    if (pos + HEADER_SIZE > static_cast<size_t>(validBytes)) {
                        // Not enough data - slide this occurrence to buffer start
                        std::memmove(buffer.data(), buffer.data() + pos, validBytes - pos);
                        validBytes -= static_cast<std::streamsize>(pos);
                        bufferStartPos += static_cast<std::streamoff>(pos);
                        
                        // Stream is already positioned correctly after last read
                        // No seekg() needed here!
                        
                        // Continue outer loop to read more data
                        break;
                    }
                    
                    // We have enough data - read header directly from buffer
                    PacketHeader* candidate = reinterpret_cast<PacketHeader*>(buffer.data() + pos);
                    
                    // Validate magic (redundant but fast check)
                    if (candidate->isValidMagic() && candidate->validateChecksum()) {
                        // Valid header found - copy to this object
                        std::memcpy(this, candidate, HEADER_SIZE);
                        
                        // Calculate actual file position
                        position = bufferStartPos + static_cast<std::streamoff>(pos);
                        
                        return true;
                    }
                    
                    // Invalid header, search for next occurrence after this position
                    pos = view.find("PCKT", pos + 1);
                }
                
                // If no match found and we read a full chunk, prepare for next iteration
                if (pos == std::string_view::npos) {
                    if (validBytes == CHUNK_SIZE) {
                        // Full chunk read, might have more data - slide to handle boundary
                        constexpr size_t KEEP_BYTES = HEADER_SIZE - 1;
                        std::memmove(buffer.data(), buffer.data() + validBytes - KEEP_BYTES, KEEP_BYTES);
                        bufferStartPos += static_cast<std::streamoff>(validBytes - KEEP_BYTES);
                        validBytes = KEEP_BYTES;
                    } else {
                        // Partial chunk means EOF - we've checked all data
                        break;
                    }
                }
            }
            return false;
        }
        /**
         * @brief Write packet header to binary stream
         * @param stream Output stream to write to
         * @param firstRow Absolute row index (0-based, file-wide)
         * @return true if write successful
         */
        static bool write(std::ostream& stream, uint64_t firstRow) {
            PacketHeader header(firstRow);
            stream.write(reinterpret_cast<const char*>(&header), sizeof(PacketHeader));
            return stream.good();
        }
    };
    #pragma pack(pop)
    static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be exactly 16 bytes");
    
} // namespace bcsv
