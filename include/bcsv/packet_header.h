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
    |                    Payload Size (uint32)                      |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                    First Row Number (uint64)                  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                      Row Count (uint32)                       |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                    CRC32 Checksum (uint32)                    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |              Row Lengths - length of each row in bytes        |
    |          for rows 0 to n-1 (uint16 * (Number of Rows - 1))    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                    Payload Data (LZ4 compressed)              |
    |                         (variable)                            |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    Note: In v1.0+, CRC32 checksums and row indexing are mandatory features
    that are always present in every packet, providing data integrity and
    random access capabilities.

    Row Length Format:
    - Stores the length (in bytes) of each row from 0 to n-1
    - The last row's length is implicit (calculated from total payload size)
    - Row offset for row n is calculated by accumulating lengths 0 to n-1
    - Example: Row 0 offset = 0, Row 1 offset = length[0], Row 2 offset = length[0] + length[1], etc.

*/
    #pragma pack(push, 1)
    struct PacketHeader {
        const uint32_t magic = PCKT_MAGIC; 
        uint32_t payloadSizeZip; // Size of the compressed payload data
        uint64_t rowFirst;       // Index of the first row in the packet
        uint32_t rowCount;       // Number of rows in the packet
        uint32_t crc32;          // CRC32 checksum of the entire packet (with this field zeroed)

        bool read               (std::istream& stream);
        bool findAndRead        (std::istream& stream);
        void updateCRC32        (const std::vector<uint16_t>& rowLengths, const ByteBuffer& zipBuffer);
        bool validateCRC32      (const std::vector<uint16_t>& rowLengths, const ByteBuffer& zipBuffer);
        bool validate           () const {
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
