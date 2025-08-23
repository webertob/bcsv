#pragma once

#include <boost/crc.hpp>
#include <cstring>
#include "packet_header.h"

namespace bcsv {

    constexpr size_t crc32_offset = sizeof(PacketHeader); 
    constexpr size_t crc32_size = sizeof(uint32_t);
    constexpr size_t min_packet_size = sizeof(PacketHeader) + crc32_size; // Minimum packet size including CRC32

    void PacketHeader::updateCRC32(std::vector<char>& packetRawBuffer) {
        if(packetRawBuffer.size() < min_packet_size) {
            return; // Not enough data to update CRC32
        }
        
        // Zero out the CRC32 field before calculating
        std::memset(packetRawBuffer.data() + crc32_offset, 0, crc32_size);

        // Calculate CRC32 over the entire buffer
        boost::crc_32_type crc32;
        crc32.process_bytes(packetRawBuffer.data(), packetRawBuffer.size());
        uint32_t crcValue = crc32.checksum();
        
        // Store the CRC32 value in the buffer
        std::memcpy(packetRawBuffer.data() + crc32_offset, &crcValue, sizeof(crcValue));
    }

    bool PacketHeader::validateCRC32(const std::vector<char>& packetRawBuffer) {
        if (packetRawBuffer.size() < min_packet_size) {
            return false; // Not enough data for CRC32
        }

        // Extract the stored CRC32 checksum from the packet
        uint32_t crc32_stored;
        std::memcpy(&crc32_stored, packetRawBuffer.data() + crc32_offset, sizeof(crc32_stored));

        // Calculate CRC32 in 3 pieces to avoid buffer copy
        boost::crc_32_type crc32;
        
        // 1st: Process buffer up to CRC32 field
        crc32.process_bytes(packetRawBuffer.data(), crc32_offset);

        
        // 2nd: Process zeroed CRC32 field (4 bytes of zeros)
        uint32_t zeroCRC = 0;
        crc32.process_bytes(&zeroCRC, sizeof(zeroCRC));
        
        // 3rd: Process remainder of buffer after CRC32 field
        size_t remainderOffset = crc32_offset + crc32_size;
        if (remainderOffset < packetRawBuffer.size()) {
            size_t remainderSize = packetRawBuffer.size() - remainderOffset;
            crc32.process_bytes(packetRawBuffer.data() + remainderOffset, remainderSize);
        }

        uint32_t crc32_calculated = crc32.checksum();
        return crc32_stored == crc32_calculated;
    }

}   
