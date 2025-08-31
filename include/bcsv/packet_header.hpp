#pragma once

#include "packet_header.h"

#include <boost/crc.hpp>

namespace bcsv {

    void PacketHeader::updateCRC32(const std::vector<uint16_t>& rowOffsets, const ByteBuffer& zipBuffer) {
        this->crc32 = 0; // Ensure CRC32 field is zeroed before calculation

        // Calculate CRC32 including row offsets and compressed data
        boost::crc_32_type crcCalculator;
        crcCalculator.process_bytes(this, sizeof(PacketHeader));
        crcCalculator.process_bytes(rowOffsets.data(), rowOffsets.size() * sizeof(uint16_t));
        crcCalculator.process_bytes(zipBuffer.data(), zipBuffer.size());

        // Store the CRC32 value in the header
        this->crc32 = crcCalculator.checksum();
    }

    bool PacketHeader::validateCRC32(const std::vector<uint16_t>& rowOffsets, const ByteBuffer& zipBuffer) {
        uint32_t originalCRC32 = this->crc32;
        this->crc32 = 0; // Zero out the CRC32 field before calculating

        // Calculate CRC32 over the row offsets
        boost::crc_32_type crcCalculator;
        crcCalculator.process_bytes(this, sizeof(PacketHeader));
        crcCalculator.process_bytes(rowOffsets.data(), rowOffsets.size() * sizeof(uint16_t));
        crcCalculator.process_bytes(zipBuffer.data(), zipBuffer.size());

        this->crc32 = originalCRC32; // Restore original CRC32 value
        // Compare with the stored CRC32 value
        return crcCalculator.checksum() == originalCRC32;
    }

} // namespace bcsv
