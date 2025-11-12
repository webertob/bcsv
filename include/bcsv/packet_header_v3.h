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
#include <iostream>
#include "definitions.h"
#include "xxHash-0.8.3/xxhash.h"

namespace bcsv {

    /**
     * @brief Packet header for BCSV v1.3.0 streaming compression format
     * 
     * This structure represents the 24-byte header at the start of each data packet
     * in v1.3.0 files using streaming LZ4 compression. The header provides:
     * - Packet identification via magic number
     * - Row indexing for random access
     * - Checksum chaining for integrity validation
     * - Compact header checksum for header-only validation
     * 
     * Memory Layout (24 bytes):
     * ```
     * Offset | Size | Field                  | Description
     * -------|------|------------------------|----------------------------------------
     *   0    |  4   | magic[4]               | "PCKT" (0x50 0x43 0x4B 0x54)
     *   4    |  8   | firstRowIndex          | Absolute row index (0-based, file-wide)
     *  12    |  8   | prevPayloadChecksum    | xxHash64 of previous packet's payload
     *  20    |  4   | headerChecksum         | xxHash32 of bytes 0-19
     * ```
     * 
     * Checksum Chain Flow:
     * - Packet 1: prevPayloadChecksum = 0, calculate payload checksum P1
     * - Packet 2: prevPayloadChecksum = P1, calculate payload checksum P2
     * - Packet N: prevPayloadChecksum = P(N-1), calculate payload checksum PN
     * - Last packet: payload checksum stored in FileFooter.lastPacketPayloadChecksum
     * 
     * @note All multi-byte fields use little-endian byte ordering
     * @note Row count for packet = nextPacket.firstRowIndex - currentPacket.firstRowIndex
     * @note For last packet, row count from FileFooter or scanning payload
     */
    #pragma pack(push, 1)
    struct PacketHeaderV3 {
        char magic[4];                  ///< Magic number: "PCKT" (0x50 0x43 0x4B 0x54)
        uint64_t firstRowIndex;         ///< Absolute row index (0-based, file-wide)
        uint64_t prevPayloadChecksum;   ///< xxHash64 of previous packet's payload (0 for first packet)
        uint32_t headerChecksum;        ///< xxHash32 of bytes 0-19 (magic + firstRowIndex + prevPayloadChecksum)
        
        /**
         * @brief Default constructor - initializes to invalid state
         */
        PacketHeaderV3() 
            : magic{'P', 'C', 'K', 'T'}
            , firstRowIndex(0)
            , prevPayloadChecksum(0)
            , headerChecksum(0)
        {}
        
        /**
         * @brief Construct packet header with specified values
         * @param firstRow Absolute row index (0-based, file-wide)
         * @param prevChecksum xxHash64 of previous packet's payload (0 for first packet)
         */
        PacketHeaderV3(uint64_t firstRow, uint64_t prevChecksum)
            : magic{'P', 'C', 'K', 'T'}
            , firstRowIndex(firstRow)
            , prevPayloadChecksum(prevChecksum)
            , headerChecksum(0)
        {
            updateHeaderChecksum();
        }
        
        /**
         * @brief Validate magic number
         * @return true if magic number is "PCKT"
         */
        bool isValidMagic() const {
            return magic[0] == 'P' && magic[1] == 'C' && magic[2] == 'K' && magic[3] == 'T';
        }
        
        /**
         * @brief Calculate and update the header checksum
         * 
         * Calculates xxHash32 of the first 20 bytes (magic + firstRowIndex + prevPayloadChecksum)
         * and stores it in the headerChecksum field.
         */
        void updateHeaderChecksum() {
            // Calculate xxHash32 of bytes 0-19 (all fields except headerChecksum)
            headerChecksum = XXH32(this, 20, 0);
        }
        
        /**
         * @brief Validate the header checksum
         * @return true if calculated checksum matches stored checksum
         */
        bool validateHeaderChecksum() const {
            uint32_t calculated = XXH32(this, 20, 0);
            return calculated == headerChecksum;
        }
        
        /**
         * @brief Validate header integrity (magic + checksum)
         * @return true if both magic number and checksum are valid
         */
        bool validate() const {
            return isValidMagic() && validateHeaderChecksum();
        }
        
        /**
         * @brief Read packet header from binary stream
         * @param stream Input stream to read from
         * @return true if read successful and header is valid
         */
        bool read(std::istream& stream) {
            stream.read(reinterpret_cast<char*>(this), sizeof(PacketHeaderV3));
            if (!stream.good()) {
                return false;
            }
            
            if (!isValidMagic()) {
                std::cerr << "Error: Invalid packet magic number" << std::endl;
                return false;
            }
            
            if (!validateHeaderChecksum()) {
                std::cerr << "Error: Packet header checksum mismatch" << std::endl;
                return false;
            }
            
            return true;
        }
        
        /**
         * @brief Write packet header to binary stream
         * @param stream Output stream to write to
         * @return true if write successful
         */
        bool write(std::ostream& stream) {
            updateHeaderChecksum();
            stream.write(reinterpret_cast<const char*>(this), sizeof(PacketHeaderV3));
            return stream.good();
        }
    };
    #pragma pack(pop)
    
    static_assert(sizeof(PacketHeaderV3) == 24, "PacketHeaderV3 must be exactly 24 bytes");
    
} // namespace bcsv
