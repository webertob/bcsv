#pragma once

#include <cstdint>
#include <istream>

#include "definitions.h"
#include "byte_buffer.h"

namespace bcsv {

    /**
     * @brief Represents a data packet that can contain column layouts or rows
     */

     /*
     ### Packet Header Binary Layout (v1.0+ - Mandatory Features)
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                      Packet Magic (uint32)                    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                    Payload Size (compressed) (uint32)         |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                    First Row Number (uint64)                  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                      Number of Rows (uint32)                  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                    CRC32 Checksum (uint32)                    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |              Row Index - offset to the start of each          |
    |              row, except for the first (uint16 * Rows - 1)    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                    Payload Data (LZ4 compressed)              |
    |                         (variable)                            |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Note: In v1.0+, CRC32 checksums and row indexing are mandatory features
    that are always present in every packet, providing data integrity and
    random access capabilities.

*/
    #pragma pack(push, 1)
    struct PacketHeader {
        const uint32_t magic = PCKT_MAGIC; 
        uint32_t payloadSizeZip; // Size of the compressed payload data
        uint64_t rowFirst;       // Index of the first row in the packet
        uint32_t rowCount;       // Number of rows in the packet
        uint32_t crc32;          // CRC32 checksum of the entire packet (with this field zeroed)

        bool read(std::istream& stream);
        bool findAndRead(std::istream& stream);
        void updateCRC32(const std::vector<uint16_t>& rowOffsets, const ByteBuffer& zipBuffer);
        bool validateCRC32(const std::vector<uint16_t>& rowOffsets, const ByteBuffer& zipBuffer);
        bool validate() const {
            if(magic != PCKT_MAGIC) {
                return false;
            }
            if(payloadSizeZip == 0) {
                return false;
            }
            if(rowCount == 0) {
                return false;
            }
            return true;
        }
    };
    #pragma pack(pop)
    static_assert(sizeof(PacketHeader) == 24, "PacketHeader must be exactly 24 bytes");
} // namespace bcsv
