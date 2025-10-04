/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "bcsv/byte_buffer.h"
#include "packet_header.h"
#include <boost/crc.hpp>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace bcsv {
    /*
     * Read a packet header from the stream
     * Returns true if a valid header was read, false otherwise
     */
    inline bool PacketHeader::read(std::istream& stream, std::vector<uint16_t> &rowLengths, ByteBuffer& payloadBuffer, bool resilient)
    {
        while(stream) {
            // Read potential header 
            stream.read(reinterpret_cast<char*>(this), sizeof(PacketHeader));
            if(stream.gcount() != sizeof(PacketHeader) || stream.eof()) {
                break; // Reached end of file without having a valid header
            }

            // Search for magic number that indicates start of a potential header
            auto search_begin = reinterpret_cast<const char*>(this);
            auto search_end = search_begin + sizeof(PacketHeader);
            auto pattern_begin = reinterpret_cast<const char*>(&PCKT_MAGIC);
            auto pattern_end = pattern_begin + sizeof(PCKT_MAGIC);

            auto found = std::search(search_begin, search_end, pattern_begin, pattern_end);
            size_t size_pos = (found != search_end) ? (found - search_begin) : std::string_view::npos;
            if(size_pos == std::string_view::npos) {
                // Magic number not found, continue trying
                continue; 
            } else if(size_pos != 0) {
                // Magic number found, but we need to move it to the front
                std::memmove(reinterpret_cast<char*>(this), reinterpret_cast<const char*>(this) + size_pos, sizeof(PacketHeader) - size_pos);
                // Read rest of header from stream
                stream.read(reinterpret_cast<char*>(this) + (sizeof(PacketHeader) - size_pos), size_pos);
                if(stream.gcount() != static_cast<std::streamsize>(size_pos)) {
                    break; // Reached end of file without having a valid header
                }
            }

            // At this point we believe we have a valid header
            // Let's validate it:
            // In case validation fails we need to continue searching from here:
            std::streamoff file_pos = stream.tellg() - static_cast<std::streamoff>(sizeof(PacketHeader)-4); //in case validation fails we need to continue searching from here
            
            if(rowCount == 0 || payloadSize == 0) {
                if(!resilient) {
                    throw std::runtime_error("Error: Invalid packet header (rowCount or payloadSize is zero)");
                } else {
                    stream.seekg(file_pos);
                    continue; // Invalid header, try next position
                }
            }

            // read row lengths
            rowLengths.resize(this->rowCount-1);
            stream.read(reinterpret_cast<char*>(rowLengths.data()), (this->rowCount - 1) * sizeof(uint16_t));
            if(stream.gcount() != static_cast<std::streamsize>((this->rowCount - 1) * sizeof(uint16_t))) {
                return false; // Failed to read full row lengths
            }

            // read payload data
            payloadBuffer.resize(this->payloadSize);
            stream.read(reinterpret_cast<char*>(payloadBuffer.data()), this->payloadSize);
            if(stream.gcount() != static_cast<std::streamsize>(this->payloadSize)) {
                return false; // Failed to read full payload
            }

            // validate crc32
            if (!validateCRC32(rowLengths, payloadBuffer)) {
                if(!resilient) {
                    throw std::runtime_error("Error: CRC32 validation failed");
                } else {
                    stream.seekg(file_pos);
                    continue; // Invalid header, try next position
                }
            }
            return true;
        }
        return false;
    }

    inline void PacketHeader::updateCRC32(const std::vector<uint16_t>& rowLengths, const ByteBuffer& zipBuffer) {
        this->crc32 = 0; // Ensure CRC32 field is zeroed before calculation

        // Calculate CRC32 including row offsets and compressed data
        boost::crc_32_type crcCalculator;
        crcCalculator.process_bytes(this, sizeof(PacketHeader));
        crcCalculator.process_bytes(rowLengths.data(), rowLengths.size() * sizeof(uint16_t));
        crcCalculator.process_bytes(zipBuffer.data(), zipBuffer.size());

        // Store the CRC32 value in the header
        this->crc32 = crcCalculator.checksum();
    }

    inline bool PacketHeader::validateCRC32(const std::vector<uint16_t>& rowLengths, const ByteBuffer& zipBuffer) {
        uint32_t originalCRC32 = this->crc32;
        this->crc32 = 0; // Zero out the CRC32 field before calculating

        // Calculate CRC32 over the row offsets
        boost::crc_32_type crcCalculator;
        crcCalculator.process_bytes(this, sizeof(PacketHeader));
        crcCalculator.process_bytes(rowLengths.data(), rowLengths.size() * sizeof(uint16_t));
        crcCalculator.process_bytes(zipBuffer.data(), zipBuffer.size());

        this->crc32 = originalCRC32; // Restore original CRC32 value
        // Compare with the stored CRC32 value
        return crcCalculator.checksum() == originalCRC32;
    }

} // namespace bcsv
