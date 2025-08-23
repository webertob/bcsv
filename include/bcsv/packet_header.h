#pragma once

#include <vector>
#include <cstdint>

#include "definitions.h"
#include "column_layout.h"

namespace bcsv {

    /**
     * @brief Represents a data packet that can contain column layouts or rows
     */

     /*
     ### Packet Header Binary Layout
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                      Packet Magic (uint32)                    |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                     Original Payload Size (uint32)            |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                    Compressed Payload Size (uint32)           |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                    First Row Number (uint64)                  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                      Number of Rows (uint64)                  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | Optional(FileFlage CHECKSUMS):       CRC32 Checksum (uint32)  |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    | Optional(FileFlage CHECKSUMS):  offset to the start of each   |
    | row, except for the first (uint16 * Number of rows - 1)       |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    |                    LZ4 Compressed Payload Data                |
    |                         (variable)                            |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/
    #pragma pack(push, 1)
    struct PacketHeader {
        uint32_t magic;          // Should be PCKT_MAGIC
        uint32_t payloadSizeRaw; // Size of the payload data
        uint32_t payloadSizeZip; // Size of the compressed payload data
        uint64_t rowFirst;       // Index of the first row in the packet
        uint64_t rowCount;       // Number of rows in the packet

        static void updateCRC32(std::vector<char>& packetRawBuffer);
        static bool validateCRC32(const std::vector<char>& packetRawBuffer);
    };
    #pragma pack(pop)
    static_assert(sizeof(PacketHeader) == 28, "PacketHeader must be exactly 28 bytes");

} // namespace bcsv
