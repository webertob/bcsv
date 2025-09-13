#pragma once

#include "packet_header.h"

#include <boost/crc.hpp>

namespace bcsv {

    bool PacketHeader::read(std::istream& stream)
    {
        if(!stream) {
            return false;
        }
        stream.read(reinterpret_cast<char*>(this), sizeof(PacketHeader));
        bool size_ok = stream.gcount() == sizeof(PacketHeader);
        bool valid = validate();
        return size_ok && valid;
    }

    bool PacketHeader::findAndRead(std::istream& stream)
    {
        // Search for the magic number in the stream
        char buffer[4];
        while(stream) {
            // 1st byte read and evaluate
            stream.read(buffer, 1);
            if (buffer[0] != ((PCKT_MAGIC >> 24) & 0xFF)) {
                continue;
            }
            // 2nd byte read and evaluate
            stream.read(buffer + 1, 1);
            if (buffer[1] != ((PCKT_MAGIC >> 16) & 0xFF)) {
                continue;
            }
            // 3rd byte read and evaluate
            stream.read(buffer + 2, 1);
            if (buffer[2] != ((PCKT_MAGIC >> 8) & 0xFF)) {
                continue;
            }
            // 4th byte read and evaluate
            stream.read(buffer + 3, 1);
            if (buffer[3] != ((PCKT_MAGIC >> 0) & 0xFF)) {
                continue;
            }
            // Magic number matched, read the rest of the header
            stream.read(reinterpret_cast<char*>(this) + 4, sizeof(PacketHeader) - 4);
            return validate() && stream.good();
        }
        return false; // Magic number not found
    }

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
